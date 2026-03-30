#pragma once
#include <atomic>

namespace coro {

    ///A flag used to signal cancelation of periodic operation
    /**
        This object is often passed to various periodic co_await operation as an identification and cancelation token
        You should only directly access this signal through interface of the operations that use it. The operations should provide
        a way to atomically cancel the operation and set the flag, so you should not set the flag directly.
        You can check if the flag is set to prevent calling periodic operations again after cancelation,
        but you should not rely on the flag as a signal for cancelation since there might be a delay between setting the flag 
        and actual cancelation of the operation.  
     */
    class cancel_signal : protected std::atomic<bool>{
    public:        
        cancel_signal():std::atomic<bool>(false) {};
        ///Check if the cancelation has been requested
        explicit operator bool() const {return is_canceled();}
        ///Request cancelation by setting the flag to true. This should be used together with interrupt function of the scheduler to properly cancel periodic operations.
        void request_cancel() {this->store(true, std::memory_order_relaxed);}
        ///Check if cancelation has been requested
        bool is_canceled() const {return this->load(std::memory_order_relaxed);}
        ///Reset the cancel signal to false. This does not interrupt any operation by itself, but allows to reuse the same signal for future operations.
        void reset() {this->store(false, std::memory_order_relaxed);}
    };
        
}