#pragma once

#include "concepts.hpp"
#include "allocator.hpp"
#include "exceptions.hpp"
#include "sync_await.hpp"
#include <utility>

namespace coro {

template<typename T> class awaitable;
template<typename T> class awaitable_result;

namespace details {


    template<typename T>
    struct promise_type_base_generic {
    public:

        awaitable<T> *_target = {};

        template<typename X>
        requires(is_awaitable_valid_result_type<T, X>)
        void return_value(X &&x) {
            if (_target) _target->set_value(std::forward<X>(x));
        }
    };
    template<typename T>
    requires(std::is_void_v<T>)
    struct promise_type_base_generic<T> {
    public:
        awaitable<T> *_target = {};

        void return_void() {
            if (_target) _target->set_value();
        }
    };

    template<typename T>
    struct promise_type_base : promise_type_base_generic<T> {
        
       void set_exception(std::exception_ptr e) {
            if (this->_target) this->_target->set_exception(std::move(e));
        }
        prepared_coro wakeup() {
            if (this->_target) return this->_target->wakeup();
            return {};
        }

    };
}

struct coroutine_tag {};

///construct coroutine
/**
 * @tparam T result type
 * @tparam _Allocator allocator. If it is void, it uses standard allocator (malloc)
 *
 * The coroutine can return anything which is convertible to T. It can
 * also return a function (lambda function) which returns T - this can achieve
 * RVO.
 *
 * @code
 * co_return [&]{ return 42;}
 * @endcode
 *
 * The returned value from the lambda function is returned with respect to RVO,
 * so the returned object can be immovable, and still can be returned from
 * the coroutine
 *
 * If the object is initialized and not awaited, the coroutine is
 * started in detached state. You need to call destroy() if you need
 * to destroy already initialized coroutine.
 *
 * You can destroy the coroutine anytime when you have a handle. In this
 * case, the awaiting coroutine receives exception canceled_exception
 */
template<typename T, coro_allocator _Allocator = objstdalloc>
class coroutine; 
template<typename T>
class coroutine<T, objstdalloc>: public coroutine_tag {
public:

    struct promise_type: public details::promise_type_base<T> {
    public:
        struct finisher {
            promise_type *me;
            constexpr bool await_ready() const noexcept {
                return !me->_target;
            }
            #if _MSC_VER && defined(_DEBUG)
            //BUG in MSVC - in debug mode, symmetric transfer cannot be used
            //because return value of await_suspend is located in destroyed
            //coroutine context. This is not issue for release
            //build as the return value is stored in register
            constexpr void await_suspend(std::coroutine_handle<> h) noexcept {
                auto p = me->wakeup();
                h.destroy();
                p.resume();
            }
            #else
            constexpr std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) noexcept {
                auto p = me->wakeup();
                h.destroy();
                return p.symmetric_transfer();
            }
            #endif
            static constexpr void await_resume() noexcept {}

        };

        promise_type() = default;
        ~promise_type() {
            this->wakeup();
        }

        bool is_detached() const {
            return this->_target == nullptr;
        }

        static constexpr std::suspend_always initial_suspend() noexcept {return {};}
        constexpr finisher final_suspend() noexcept {return {this};}
        void unhandled_exception() {
            if (this->_target) {
                this->set_exception(std::current_exception());
            } else {
                async_unhandled_exception();
            }
        }
        coroutine get_return_object()  {
            return this;
        }
    };

    ///construct empty object
    coroutine()  = default;

    template<typename Alloc>
    coroutine(coroutine<T, Alloc> &&other):_coro(other._coro) {other._coro = nullptr;}
    ///move object
    coroutine(coroutine &&other) :_coro(other._coro) {other._coro = nullptr;}
    ///move object
    coroutine &operator=(coroutine &&other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }
    ///destroy object
    /**
     * When object is destroyed with the coroutine, the coroutine is started.
     * If the coroutine is suspended, it continues in detached mode.
     *
     */
    ~coroutine()  {
        if (_coro) {
            release().resume();
        }
    }

    ///start coroutine - set result object
    /**
     * @param res result object will be used to put result there. If
     * the result object is not initialized, the coroutine is started in
     * detached mode
     * @return prepared coroutine object. If result is ignored, the coroutine
     * is started immediately. However, you can store the result to
     * perform symmetric transfer for example
     */
    prepared_coro start(awaitable_result<T> &&res) {
        auto c = std::exchange(_coro, nullptr);
        if (c) {
            c->_target = res.release();
            return prepared_coro(std::coroutine_handle<promise_type>::from_promise(*c));
        }
        return {};
    }

    awaitable<T> operator co_await() {
        return std::move(*this);
    }

    ///await synchronously on result
    T get() {
        return sync_await(operator co_await());
    }

    ///retrieve result synchronously (conversion to result)
    operator decltype(auto)() {
        return get();
    }

    ///destroy initialized coroutine
    /**
     * By default, if coroutine object leaves scope, the coroutine
     * is resumed in detached mode. If you need to prevent this, you
     * need to explicitly call destroy().
     */
    void cancel() {
        if (_coro) {
            release().destroy();
        }
    }

    ///Release coroutine from the object, you get its handle for any usage
    auto release() {
        return std::coroutine_handle<promise_type>::from_promise(*std::exchange(_coro, nullptr));
    }

    ///struct that helps to detect detached mode
    struct detached_test_awaitable : std::suspend_always {
        bool _detached = false;
        bool await_resume() noexcept {return _detached;}
        bool await_suspend(std::coroutine_handle<> h) noexcept {
            std::coroutine_handle<promise_type> ph =
                    std::coroutine_handle<promise_type>::from_address(h.address());
            promise_type &p = ph.promise();
            _detached = p.is_detached();
            return false;
        }
    };

    ///determines whether coroutine is running in detached mode
    /**
     * This can optimize processing when coroutine knows, that no result
     * is requested, so it can skip certain parts of its code. It still
     * needs to generate result, but it can return inaccurate result or
     * complete invalid result
     *
     * to use this function, you need call it inside of coroutine body
     * with co_await
     *
     * @code
     * coroutine<int> foo() {
     *      bool detached = co_await coroutine<int>::is_detached();
     *      std::cout << detached?"detached":"not detached" << std::endl;
     *      co_return 42;
     * }
     *
     * @return awaitable which returns true - detached, false - not detached
     */
    static detached_test_awaitable is_detached() {return {};}
protected:

    promise_type *_coro = nullptr;

    coroutine(promise_type *pt):_coro(pt) {}

    std::coroutine_handle<promise_type> get_handle() const {
        return std::coroutine_handle<promise_type>::from_promise(*_coro);
    }
};

///Declaration of basic coroutine with allocator
/**
 * @tparam T type of result
 * @tparam _Allocator allocator must be of type coro_allocator
 */
template<typename T, coro_allocator _Allocator>
class coroutine: public coroutine<T, objstdalloc> {
public:
    using coroutine<T, objstdalloc>::coroutine;

    class promise_type: public coroutine<T, objstdalloc>::promise_type,  public _Allocator::overrides {
    public:
        coroutine<T, _Allocator> get_return_object() {
          return this;
        }

        using _Allocator::overrides::operator new;
        using _Allocator::overrides::operator delete;

    };
    coroutine(promise_type *p):coroutine<T, objstdalloc>(p) {}
};


///this class helps to write a function, which is called once the coroutine is destroyed
template<std::invocable<> Fn>
class on_destroy {
public:
    on_destroy(Fn &&fn):_fn(fn) {}
    ~on_destroy() {_fn();}
    on_destroy(const on_destroy &) = delete;
    on_destroy &operator=(const on_destroy &) = delete;
protected:
    Fn _fn;
};

template<std::invocable<> Fn>
on_destroy(Fn) -> on_destroy<Fn>;

}