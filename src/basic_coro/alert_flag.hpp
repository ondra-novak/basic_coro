#pragma once
#include <atomic>

namespace coro {


    ///Alert flag is flag used to alert special state for coroutine pushed into various aggregators
    /**
     * When alert flag is set, the coroutine is not added to the aggregator, and completes
     * with no-value state. Alert flag is set to true when coroutine is removed from the aggregator
     * however, this operation can fail if the coroutine is not yet registered. So by setting
     * alert flag along with the removal, this prevents to future registrations. The address of
     * the alert flag is also used as identification
     */
    class alert_flag {
        public:
        
            explicit operator bool() const {return _flag.load(std::memory_order_relaxed);}
            void set() {_flag.store(true, std::memory_order_relaxed);}
            bool test_and_reset() {return _flag.exchange(false, std::memory_order_relaxed);}
            void reset() {_flag.store(false, std::memory_order_relaxed);}
            alert_flag() = default;
            alert_flag(bool v):_flag(v) {};
            alert_flag(const alert_flag &) = delete;
            alert_flag &operator=(const alert_flag &) = delete;
        
        protected:
            std::atomic<bool> _flag = {false};
        
    };
        
}