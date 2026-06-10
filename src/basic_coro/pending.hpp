#pragma  once


#include "concepts.hpp"
#include "coro_frame.hpp"
#include "prepared_coro.hpp"
#include <atomic>
#include <cassert>
#include <coroutine>
#include <memory>
#include <type_traits>
namespace coro {



///synchronize with awaitable allows to run coroutine in parallel with an asynchronous operation and co_await later
/** 
 * when instance is created, it emits await_suspend to awaitable, which allows to run asynchronous operation concurrently with the coroutine.
 * You can synchronize with the operation by co_awaiting this object.
 * @tparam T type of awaitable to synchronize with
 * @note this object is not thread safe, so it should be used only in one thread.
 * @note the object is not movable, and final synchronization is mandatory. 
 *       You must call co_await on this object before destroying it.
 * Another way to construct this object is to use launch() method of awaitable, which returns pending object. 
 */ 
template<typename T>
class pending;

template<is_awaiter T>
class pending<T> : private coro_frame<pending<T> >{
public:
    ///construct by awaitable. It starts asynchronous operation immediately
     /**
      * @param awt awaitable to synchronize with. It must be movable and its ownership is transferred to pending object
      * @note the object is not movable, and final synchronization is mandatory. You must call co_await on this object before destroying it.
      */
    pending(T awt)
        : _awt(std::move(awt))
        , _awaiting_state(_awt.await_ready()?get_sentinel():nullptr) {
            if (_awaiting_state.load(std::memory_order_relaxed) == nullptr) {
                call_await_suspend(_awt, this->create_handle());
            }
        }

    template<typename X>
    requires (std::is_invocable_r_v<T, X>)
    explicit pending(X &&fn)
        :_awt(std::forward<X>(fn)())
        , _awaiting_state(_awt.await_ready()?get_sentinel():nullptr) {
            if (_awaiting_state.load(std::memory_order_relaxed) == nullptr) {
                call_await_suspend(_awt, this->create_handle());
            }
        }


    ///construct by awaitable, It starts asynchronous operation immediately through the executor
    /**
    @param awt awaitable
    @param executor executor to start / resume coroutine
    */
    template<std::invocable<prepared_coro> _Executor>
    pending(T awt, _Executor executor)
        : _awt(std::move(awt))
        , _awaiting_state(_awt.await_ready()?get_sentinel():nullptr) {
            if (_awaiting_state.load(std::memory_order_relaxed) == nullptr) {
                executor(call_await_suspend(_awt, this->create_handle()));
            }
        }

    template<typename X, std::invocable<prepared_coro> _Executor>
    requires (std::is_invocable_r_v<T, X>)
    pending(X &&fn, _Executor executor)
        :_awt(std::forward<X>(fn)())
        , _awaiting_state(_awt.await_ready()?get_sentinel():nullptr) {
            if (_awaiting_state.load(std::memory_order_relaxed) == nullptr) {
                executor(call_await_suspend(_awt, this->create_handle()));
            }
        }

    ///copying is not allowed
    pending(const pending &) = delete;
    ///copy assignment is not allowed
    pending &operator=(const pending &) = delete;
    
    ///awaiting on pending object. It synchronizes with asynchronous operation and returns its result
     /**
      * @return result of awaiting the synchronized awaitable
      * @note you must call co_await on this object before destroying it, otherwise the behaviour is undefined.
      */
    bool await_ready() const noexcept {
        return _awaiting_state.load() == get_sentinel();
    }
    
    bool await_suspend(std::coroutine_handle<> h) noexcept {
        void *addr = h.address();
        void *r = _awaiting_state.exchange(addr);
        if (r == get_sentinel()) {
            return false;
        }
        return true;
    }

    ///await_resume returns result of awaiting the synchronized awaitable
     /**
      * @return result of awaiting the synchronized awaitable
      * @note you must call co_await on this object before destroying it, otherwise the behaviour is undefined.
      */     
    decltype(auto) await_resume() {
        return _awt.await_resume();
    }

    ///destroying pending object without awaiting is undefined behaviour, but we can detect it and report it by assertion     
    ~pending() {
        assert(_awaiting_state.load(std::memory_order_relaxed) != nullptr); //destroying pending instance
    }
    

protected:
    T _awt;
    std::atomic<void *> _awaiting_state = {nullptr};
    
    static void *get_sentinel() {
        return reinterpret_cast<void *>(0x1);
    }

    friend coro_frame<pending<T> >;
    
    prepared_coro do_resume() {
        auto r = _awaiting_state.exchange(get_sentinel());
        if (r) {
            return prepared_coro(std::coroutine_handle<>::from_address(r));
        } else {
            return {};
        }
    }
    prepared_coro do_destroy() {
        return {};
    }
};

///specialization of pending - stores the awaitable and awaiter 
template<typename T>
requires(has_co_await<T> || has_global_co_await<T>)
class pending<T> {
public:
    //retrieve type of awaiter
    using PendingAwt = pending<extract_awaiter_t<T> >;
    pending(T &&awt)
        :_awt(std::forward<T>(awt))
        ,_pending([&]{return extract_awaiter(std::forward<T>(_awt));}) {

    }
    
   template<std::invocable<prepared_coro> _Executor>
   pending(T &&awt, _Executor &&executor)   
        :_awt(std::forward<T>(awt))
        ,_pending([&]{return extract_awaiter(std::forward<T>(_awt));}, std::forward<_Executor>(executor)) {

    }

    pending(const pending &other) = delete;
    pending &operator=(const pending &other) = delete;

        
    bool await_ready() const noexcept {return _pending.await_ready();}
    bool await_suspend(std::coroutine_handle<> h) noexcept {return _pending.await_suspend(h);}
    decltype(auto) await_resume() {return _pending.await_resume();}


protected:

    T _awt;
    PendingAwt _pending;
    
};

template<is_awaitable T>
pending(T) -> pending<T>;

}