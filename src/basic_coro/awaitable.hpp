#pragma once
#include "concepts.hpp"
#include "sync_await.hpp"
#include "coroutine.hpp"
#include <optional>
#include <memory>


namespace coro {

///replaces simple void everywhere valid type is required
struct void_type {};


template<typename T> class awaitable;
template<typename T> class awaitable_result;
template<typename T> using voidless_type = std::conditional_t<std::is_void_v<T>, void_type, T>;


template<typename T>
class awaitable_result {
public:

    ///construct uninitialized
    /**
     * Uninitialized object can be used, however it will not invoke any
     * action and set result or exception is ignored
     */
    awaitable_result() = default;
    ///construct result using pointer to awaitable (which contains space to result)
    awaitable_result(awaitable<T> *ptr):_ptr(ptr) {}
    ///you can move
    awaitable_result(awaitable_result &&other) = default;
    ///you can move
    awaitable_result &operator=(awaitable_result &&other) = default;

    using const_reference = std::add_lvalue_reference_t<std::add_const_t<std::conditional_t<std::is_void_v<T>, void_type, T> > >;
    using rvalue_reference = std::add_rvalue_reference_t<std::conditional_t<std::is_void_v<T>, void_type, T> > ;

    ///set the result
    /**
     * @param val value to set. The value must be convertible to result. It can be also std::exception_ptr or std::nullopt with their
     * special meaning
     * @return prepared coroutine. If the result is discarded, coroutine
     * is resumed immediately. You can store result and destroy it
     * later to postpone resumption.
     */

    template<typename _Val>
    requires(is_awaitable_valid_result_type<T, _Val>)
    prepared_coro operator=(_Val && val) {
        return set_value(std::forward<_Val>(val));
    }

    prepared_coro operator=(std::nullopt_t) {
        return set_empty();
    }

    prepared_coro operator=(std::exception_ptr e) {
        return set_exception(e);
    }

    ///set result by calling its constructor
    /**
     * @param args arguments required to construct the result
     * @return prepared coroutine. If the result is discarded, coroutine
     * is resumed immediately. You can store result and destroy it
     * later to postpone resumption.
     */
    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    prepared_coro operator()(Args && ... args) {
        return set_value(std::forward<Args>(args)...);
    }

     /// set value
    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    prepared_coro set_value(Args && ... args);

    /// set exception as result
    prepared_coro set_exception(std::exception_ptr e);

    ///sets empty result.
    /**
     * the awaiting coroutine receives exception canceled_exception()
     */
    prepared_coro set_empty();

    ///Release internal pointer to be used elswhere
    /**
     * This is for special purpose, if you need carry the pointer to
     * result by other way, for example you need to cast it as uintptr_t.
     * Remember that you need it convert back later and initialize
     * awaitable_result with it to restore its purpose
     *
     * @return internal pointer.
     *
     * @note the object itself becomes unintialized
     */
    awaitable<T> *release() {return _ptr.release();}

    ///returns true if the result is expected
    /**
     * @retval true result is expected
     * @retval false result is not excpected (detached mode?)
     */
    explicit operator bool() const {return static_cast<bool>(_ptr);}

    awaitable<T> *get_handle() const {return _ptr.get();}

protected:
    struct deleter {
        void operator()(awaitable<T> *ptr) const;
    };
    std::unique_ptr<awaitable<T>, deleter> _ptr;



};

///allows to override reserved space in awaitable class for given T
/**
 * @tparam parameter type of awaitable<T>
 *
 * You need to overload this definition with own value (as a constant value).
 * Ensure that both sides see the same definition. It is best to declare overload with the
 * type itself
 *
 * @note You cannot set this value to be less than sizeof(T). Larger value is always used
 */
template<typename T>
struct awaitable_reserved_space {
    static constexpr std::size_t value = 4*sizeof(void *);
};


///Awatable object. Indicates asynchronous result
/**
 * To access value
 *
 * @code
 * co_await obj
 * obj >> callback(result)
 * type result = obj
 * @endcode
 *
 * To set value
 *
 * @code
 * return coroutine(args)
 * return [=](awaitable_result r) {
 *         //do async stuff
 *         r = result;
 *         // or r(result)
 *  };
 * @endcode
 *
 * @tparam T type of return value - can be void
 */
template<typename T>
class awaitable {
public:
    ///contains actually stored value type. It is T unless for void, it is bool
    using store_type = std::conditional_t<std::is_reference_v<T>,std::reference_wrapper<voidless_type<std::remove_reference_t<T> > >, voidless_type<T> >;
    ///contains alias for result object
    using result = awaitable_result<T>;
    ///allows to use awaitable to write coroutines
    using promise_type = coroutine<T>::promise_type;

    ///table of control methods for generic callback
    struct CBVTable {
        ///call the callback
        /**
         * @param inst pointer to an instance
         * @param result result object
         */
        prepared_coro (*call)(void *inst, result);
        ///destroy callback (end lifetime)
        /**
         * @param inst pointer to an instance to destroy
         */
        void (*destroy)(void *inst);
        ///move callback
        /**
         * @param from pointer to an instance to move from
         * @param to pointer to uninitialized space where to move instance. Function starts lifetime at this place
         *
         * @note original instance is still active (but in moved out state)
         */
        void (*move)(void *from, void *to);
    };

    ///this class is used to hold dynamically allocated callback (on heap), acting as its proxy
    /**
     * @tparam Fn callback function (closure)
     */
    template<typename Fn>
    class DynamicAllocatedCB {
    public:
        DynamicAllocatedCB(Fn &&fn): _ptr(std::make_unique<Fn>(std::forward<Fn>(fn))) {}
        prepared_coro operator()(result r) {return (*_ptr)(std::move(r));}
    protected:
        std::unique_ptr<Fn> _ptr;
    };

    ///declaraton of constexpr method table for function Fn
    /**
     * @tparam Fn function (closure object)
     */
    template<typename Fn>
    static constexpr auto cbvtable = CBVTable {
        [](void *me, result r)->prepared_coro{
            if constexpr(std::is_convertible_v<std::invoke_result_t<Fn, result>, prepared_coro>) {
                return (*reinterpret_cast<Fn *>(me))(std::move(r));
            } else {
                 (*reinterpret_cast<Fn *>(me))(std::move(r));
                 return {};
            }
        },
        [](void *me){std::destroy_at(reinterpret_cast<Fn *>(me));},
        [](void *from, void *to){std::construct_at(reinterpret_cast<Fn *>(to), std::move(*reinterpret_cast<Fn *>(from)));},
    };


    ///construct with no value
    awaitable(std::nullopt_t):_state(no_value) {};
    ///destructor
    /**
     * @note if there is prepared asynchronous operation, it is started
     * in detached mode. If you need to cancel such operation, use cancel()
     */
    ~awaitable() {
        dtor();
    }

    ///construct containing result constructed by arguments
    template<typename ... Args>
    requires (std::is_constructible_v<store_type, Args...>  && (sizeof...(Args) != 1 || (
                (!std::is_same_v<std::remove_reference_t<Args>, awaitable> && ...)
                && (!std::is_base_of_v<coroutine_tag, Args> && ...)
            )))
    awaitable(Args &&... args)
        :_state(value),_value(std::forward<Args>(args)...) {}

    ///construct by coroutine awaitable for its completion
    template<coro_allocator _Alloc>
    awaitable(coroutine<T, _Alloc> coroutine):_state(coro),_coro(std::move(coroutine)) {}

    ///construct containing result constructed by arguments
    template<typename ... Args>
    requires (std::is_constructible_v<store_type, Args...>)
    awaitable(std::in_place_t, Args &&... args)
        :_state(value),_value(std::forward<Args>(args)...) {}

    ///construct unresolved containing function which is after suspension of the awaiting coroutine
    template<std::invocable<result> Fn>
    awaitable(Fn &&fn) {
        if constexpr(sizeof(Fn) <= callback_max_size) {
            new(_callback_space) Fn(std::forward<Fn>(fn));
            _vtable = &cbvtable<Fn>;

        } else {
            new(_callback_space) DynamicAllocatedCB<Fn>(std::forward<Fn>(fn));
            _vtable = &cbvtable<DynamicAllocatedCB<Fn> >;
        }
    }

    ///construct containing result - in exception state
    awaitable(std::exception_ptr e):_state(exception),_exception(std::move(e)) {}

    ///construct unresolved containing member function which is after suspension of the awaiting coroutine
    /**
     * @param ptr a pointer or any object which defines dereference operator (*) which
     * returns reference to an instance of a class
     * @param fn pointer to member function, which is called on the instance of the class
     */
    template<typename ObjPtr, typename Fn>
    requires(is_member_fn_call_for_result<result, ObjPtr, Fn>)
    awaitable(ObjPtr ptr, Fn fn):awaitable([ptr, fn](result res){
        ((*ptr).*fn)(std::move(res));
    }) {}


    ///awaitable can be moved
    awaitable(awaitable &&other) {
        switch (other._state) {
            case no_value:_state = no_value; break;
            case value: std::construct_at(&_value, std::move(other._value));_state = value;break;
            case exception: std::construct_at(&_exception, std::move(other._exception));_state = exception;break;
            case coro: std::construct_at(&_coro, std::move(other._coro));_state = coro;break;
            default:
                _vtable = other._vtable;
                _vtable->move(other._callback_space, _callback_space);
        }
        other.destroy_state();
        other._state = no_value;
    }

    ///awaitable can be assigned by move
    awaitable &operator=(awaitable &&other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }


    ///returns whether the object has a value
    /**
     * The object must be in resolved state. Use co_await ready() if not
     */
    bool has_value() const {
        return _state == value || _state == exception;
    }

    ///returns whether there is an exception in the object
    /**
     * The object must be in resolved state. Use co_await ready() if not
     */
    bool has_exception() const {
        return _state == exception;
    }


    ///returns true if the awaitable is resolved
    bool is_ready() const {
        return await_ready();
    }


    ///returns value of resolved awaitable
    std::add_rvalue_reference_t<T> await_resume() {
        if (_state == value) {
            if constexpr(std::is_void_v<T>) {
                return;
            } else if constexpr(std::is_reference_v<T>) {
                return _value.get();
            } else {
                return std::move(_value);
            }
        } else if (_state == exception) {
            std::rethrow_exception(_exception);
        }
        throw await_canceled_exception();
    }


    ///return true, if the awaitable is resolved
    bool await_ready() const noexcept {
        return _state == no_value || _state == value || _state == exception;
    }

    ///handles suspension
    /**
     * @param h coroutine currently suspended
     * @return coroutine being resumed
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _owner = h;
        switch (_state) {
            case no_value:
            case value:
            case exception: return h;
            case coro: return _coro.start(result(this)).symmetric_transfer();
            default:
                return _vtable->call(_callback_space, result(this)).symmetric_transfer();
        }
    }

    ///set callback, which is called once the awaitable is resolved
    /**
     * @param cb callback function. The function receives reference
     * to awaitable in resolved state
     * @return prepared coroutine (if there is an involved one), you
     * can postpone its resumption by storing result and release it
     * later
     *
     * @note you can set only one callback or coroutine
     */
    template<std::invocable<awaitable &> _Callback>
    prepared_coro operator >> (_Callback &&cb) {
        objstdalloc a;
        return set_callback_internal(std::forward<_Callback>(cb), a);
    }

    ///set callback, which is called once the awaitable is resolved
    /**
     * @param cb callback function. The function receives reference
     * to awaitable in resolved state
     * @return prepared coroutine (if there is an involved one), you
     * can postpone its resumption by storing result and release it
     * later
     *
     * @note you can set only one callback or coroutine
     */
    template<std::invocable<awaitable &> _Callback>
    prepared_coro set_callback (_Callback &&cb) {
        objstdalloc alloc;
        return set_callback_internal(std::forward<_Callback>(cb), alloc);
    }

    ///set callback, which is called once the awaitable is resolved
    /**
     * @param cb callback function. The function receives reference
     * to awaitable in resolved state
     * @param a allocator instance, allows to allocate callback
     * instance using this allocator
     * @return prepared coroutine (if there is an involved one), you
     * can postpone its resumption by storing result and release it
     * later
     *
     * @note you can set only one callback or coroutine
     */
    template<std::invocable<awaitable &> _Callback, coro_allocator _Allocator>
    prepared_coro set_callback (_Callback &&cb, _Allocator &a) {
        return set_callback_internal(std::forward<_Callback>(cb), a);
    }


    ///synchronous await
    decltype(auto) await() {
        wait();
        return await_resume();
    }

    ///synchronous await
    operator voidless_type<T> &&() {
        wait();
        return await_resume();
    }

    ///allows to await on this awaitable without processing result
    /**
     * @code
     * co_await awt.ready();
     * @endcode
     *
     * @return awaitable
     */
    awaitable<void> ready();

    ///evaluate asynchronous operation, waiting for result synchronously
    void wait();


    ///copy evaluated awaitable object.
    /**
     * If the object has value or exception, returned object contains copy
     * of the value or exception. Otherwise default constructed object is
     * returned
     *
     * @return evaluated awaitable
     */
    awaitable copy_value() const  {
        switch (_state) {
            default:
            case no_value: return std::nullopt;
            case value:return awaitable(std::in_place, _value);
            case exception:return awaitable(_exception);

        }

    }
    ///return if there is someone awaiting on object
    /**
     * @retval true someone is awaiting, do not destroy the object
     * @retval false nobody awaiting
     */
    bool is_awaiting() const {
        return _owner != std::coroutine_handle<>();
    }

    ///Create result object from existing awaitable object
    /**
     * This function is used by awaiting_callback to create result object
     * on its internal awaiter. However it allows you to create result from
     * coroutine handle which is awaiting on result and awaitable object itself
     * where the coroutine expects the result. The awaitable object is set
     * to is_awaiting() state
     *
     * @param h handle of coroutine which is resumed once the value is set
     * @return result object
     */
    result create_result(std::coroutine_handle<> h) {
        if (_owner) throw invalid_state();
        _owner = h;
        return result(this);
    }

    ///cancel futher execution
    /** This prevents to execute prepared asynchronous operation. You need
     * to invoke this function if you requested only non-blocking part of
     * operation and don't want to contine asynchronously
     *
     * If the state is not pending, function does nothing
     */
    void cancel() {
        if (_owner) throw invalid_state();
        switch (_state) {
            case no_value:
            case value:
            case exception: return;
            case coro:
                _coro.cancel();
                std::destroy_at(&_coro);
                break;
            default:
                _vtable->destroy(_callback_space);
                break;
        }
        _state = no_value;
    }

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
     * awaitable<int> foo() {
     *      bool detached = co_await awaitable<int>::is_detached();
     *      std::cout << detached?"detached":"not detached" << std::endl;
     *      co_return 42;
     * }
     *
     * @return awaitable which returns true - detached, false - not detached
     */
    static typename coroutine<T>::detached_test_awaitable is_detached() {return {};}

    ///forward result of this awaiter to existing result object
    /**
     * This function causes that result of this awaiter is forwarded to result
     * object regardless on state of this object. If the
     * state is pending, then result object is passed to the async lambda
     * for completion
     *
     * @param r result object
     *
     * @return prepared coroutine handle. The operation can resume a coroutine
     * its handle is returned there
     *
     * @note after forward operation, current awaiter is in uninicialized state
     */
    prepared_coro forward(result &r) {
        prepared_coro out;
        if (r) {
            switch (_state) {
                case no_value: out = r.set_empty();break;
                case value: out = r.set_value(std::move(_value));break;
                case exception: out = r.set_exception(std::move(_exception));break;
                case coro:out = _coro.start(std::move(r)).symmetric_transfer();break;
                default: {
                    auto h = r.get_handle();
                    h->destroy_state();
                    h->_vtable = _vtable;
                    _vtable->move(_callback_space, h->_callback_space);
                    out = _vtable->call(h->_callback_space,std::move(r));
                }
            }
        }
        destroy_state();
        _state = no_value;
        return out;
    }

    ///forward result of this awaiter to existing result object
    /**
     * This function causes that result of this awaiter is forwarded to result
     * object regardless on state of this object. If the
     * state is pending, then result object is passed to the async lambda
     * for completion
     *
     * @param r result object
     *
     * @return prepared coroutine handle. The operation can resume a coroutine
     * its handle is returned there
     *
     * @note after forward operation, current awaiter is in uninicialized state
     *
     */
    void forward(result &&r) {
        forward(r);
    }

protected:

    enum State : std::uintptr_t {
        ///awaitable is resolved with no value
        no_value = 0,
        ///awaitable is resolved with a value
        value = 1,
        ///awaitable is resolved with exception
        exception = 2,
        ///awaitable is not resolved, a coroutine is ready to generate result once awaited
        coro = 3
    };


    static constexpr auto callback_max_size = std::max(awaitable_reserved_space<T>::value, sizeof(store_type));

    ///current state of object
    union {
        State _state;
        const CBVTable *_vtable;
    };
    ///handle of owning coroutine. If not set, no coroutine owns, nobody awaiting
    /**@note the handle must not be destroyed in destructor. The awaitable instance
     * is always inside of coroutine's frame, so it will be only destroyed with the owner
     */
    std::coroutine_handle<> _owner;
    union {
        ///holds current value
        store_type _value;
        ///holds current exception
        std::exception_ptr _exception;
        ///holds coroutine registration (to start coroutine when awaited)
        coroutine<T> _coro;
        ///holds reserved space for local callback
        /**@see get_local_callback() */
        char _callback_space[callback_max_size];

    };

    void dtor() {
        if (is_awaiting()) throw invalid_state();
        switch (_state) {
            case no_value:break;
            case value: std::destroy_at(&_value);break;
            case exception: std::destroy_at(&_exception);break;
            case coro: std::destroy_at(&_coro);break;
            default:
                _vtable->call(_callback_space, result(this));
                _vtable->destroy(_callback_space);
                break;
        }
    }

    void destroy_state() {
        switch (_state) {
            case no_value:break;
            case value: std::destroy_at(&_value);break;
            case exception: std::destroy_at(&_exception);break;
            case coro: _coro.cancel();std::destroy_at(&_coro);break;
            default:
               _vtable->destroy(_callback_space);break;
        }
    }

    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    void set_value(Args && ... args) {
        destroy_state();
        try {
            _state = value;
            void *trg = const_cast<std::remove_const_t<store_type> *>(&_value);
            if constexpr (sizeof...(Args) == 1 && (std::is_invocable_r_v<store_type, Args>  && ...)) {
                new(trg) store_type((...,args()));
            } else {
                new(trg) store_type(std::forward<Args>(args)...);
            }
        } catch (...) {
            _state = exception;
            std::construct_at(&_exception, std::current_exception());
        }
    }

    void set_exception(std::exception_ptr e) {
        destroy_state();
        _state = exception;
        std::construct_at(&_exception, std::move(e));
    }

    void drop() {
        destroy_state();
        _state = no_value;
    }

    prepared_coro wakeup() {
        if (!is_ready()) drop();
        return prepared_coro(std::exchange(_owner, {}));
    }

    template<typename _Callback, typename _Allocator>
    prepared_coro set_callback_internal(_Callback &&cb, _Allocator &a);





    struct ready_frame: coro_frame<ready_frame> {
        awaitable *src;
        awaitable_result<void> r = {};

        ready_frame(awaitable *src):src(src) {};
        prepared_coro operator()(awaitable_result<void> r) {
            if (!r) return {};
            this->r = std::move(r);
            return src->await_suspend(this->create_handle());
        }

        prepared_coro do_resume();
    };


    friend class awaitable_result<T>;
    friend struct details::promise_type_base<T>;
    friend struct details::promise_type_base_generic<T>;

};


template <typename T>
template <typename... Args>
requires(is_awaitable_valid_result_type<T, Args...>)
inline prepared_coro awaitable_result<T>::set_value(Args &&...args)
{
    awaitable<T> *p = _ptr.release();
    if (p) {
        p->set_value(std::forward<Args>(args)...);
        return p->wakeup();
    }
    return {};
}

namespace details {

template<typename T, std::invocable<awaitable<T> &> _CB, coro_allocator _Allocator >
class awaiting_callback : public coro_frame<awaiting_callback<T, _CB, _Allocator> >
                        , public _Allocator::overrides
{
public:

    static prepared_coro init(awaitable<T> &&awt, _CB &&cb, _Allocator &alloc) {
        awaiting_callback *n = new(alloc) awaiting_callback(std::move(awt), std::forward<_CB>(cb));
        return n->run();
    }
protected:
    ///constructor is not visible on the API
    awaiting_callback(awaitable<T> &&awt, _CB &&cb):_cb(std::forward<_CB>(cb)),_awt(std::move(awt)) {}

    prepared_coro run() {
        if (_awt.await_ready()) {
            do_resume();
            return {};
        } else {
            return call_await_suspend(_awt, this->create_handle());
        }
    }

    _CB _cb;
    ///awaitable object associated with the function
    awaitable<T> _awt = {nullptr};

    ///called when resume is triggered
    void do_resume() {
        try {
            _cb(_awt);
        } catch (...) {
            async_unhandled_exception();
        }
        do_destroy();
    }
    void do_destroy() {
        delete this;
    }

    friend coro_frame<awaiting_callback<T, _CB, _Allocator> >;
};

}

template <typename T>
template <typename _Callback, typename _Allocator>
inline prepared_coro awaitable<T>::set_callback_internal(_Callback &&cb, _Allocator &a)
{
    prepared_coro out = {};

    if (await_ready()) {
        cb(*this);
    } else {
        out = details::awaiting_callback<T, _Callback, _Allocator>::init(std::move(*this), std::forward<_Callback>(cb), a);
        cancel();
    }
    return out;
}



template<typename T>
prepared_coro awaitable<T>::ready_frame::do_resume() {
    return r();
}


template<typename T>
inline void awaitable<T>::wait() {
    if (!await_ready()) {
        sync_frame sync;
        await_suspend(sync.create_handle()).resume();
        sync.wait();
    }
}

template<typename T>
inline void awaitable_result<T>::deleter::operator()(awaitable<T> *ptr) const {
    ptr->wakeup();
}

template <typename T>
inline prepared_coro awaitable_result<T>::set_exception(std::exception_ptr e)
{
    awaitable<T> *p = _ptr.release();
    if (p) {
        p->set_exception(e);
        return p->wakeup();
    }
    return {};
}

template <typename T>
inline prepared_coro awaitable_result<T>::set_empty()
{
    awaitable<T> *p = _ptr.release();
    if (p) {
        p->drop();
        return p->wakeup();
    }
    return {};
}
}
