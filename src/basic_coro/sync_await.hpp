#pragma once
#include "concepts.hpp"
#include "coro_frame.hpp"
#include "await_proxy.hpp"
#include <atomic>

namespace coro {

/// perform synchronous await (for non-coroutines)
/** 
 * @param awt awaiter
 * @return return value of the awaiter
 * @exception any can rethrow any exception from the awaiter
 * 
 * @note Do not use sync_await for awaiters expecting some action in context of resume,
 * because the awaiter's result is accessed from a different thread. 
 */ 
template<is_awaitabe T>
inline awaiter_result<T> sync_await(T &&awt);



///a emulation of coroutine which sets atomic flag when it is resumed
class sync_frame: public coro_frame<sync_frame> {
    public:
    
        sync_frame() = default;
        sync_frame(const sync_frame&) = delete;
        sync_frame& operator =(const sync_frame&) = delete;
    
        ///wait for synchronization
        void wait() {
            _signal.wait(false);
        }
    
        ///reset synchronization
        void reset() {
            _signal = false;
        }
    
        void set() {
            _signal = true;
            _signal.notify_all();
        }

    protected:
        friend coro_frame<sync_frame> ;
        std::atomic<bool> _signal = { };
        void do_resume() {set();}
        void do_destroy() {set();}
    
    };


template<is_awaitabe T>
inline awaiter_result<T> sync_await(T &&awt) {
    if constexpr(has_co_await<T>) {
        return sync_await(awt.operator co_await());
    } else if constexpr(has_global_co_await<T>) {
        return sync_await(operator co_await(awt));
    } else {
        if (awt.await_ready()) return awt.await_resume();
        sync_frame sf;
        auto h = sf.get_handle();
        call_await_suspend(awt,h);
        sf.wait();
    }
    return awt.await_resume();
}



}