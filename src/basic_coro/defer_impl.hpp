#pragma once

#include <cstddef>
#include <utility>
#include <type_traits>
#include <deque>
#include <coroutine>

namespace coro {


class defer_context {
public:
    
    static defer_context &get_instance() {
        static thread_local defer_context ctx;
        return ctx;
    }

    bool is_active() const {
        return !_queue.empty();
    }

    void push(void *addr) {
        _queue.push_back(addr);
    }
    void push(std::coroutine_handle<> h) {
        if (h == std::coroutine_handle<>() || h == std::noop_coroutine()) return;
        push(h.address());
    }
    void *pop_in_flush() {   //this function is expected to be called during flush()
        _queue.pop_front();     //pop current item from the queue
        return _queue.front();   //retrieve next one
    }

    void flush() {
        while (!_queue.empty()) {
                void *a = _queue.front();
                std::coroutine_handle<>::from_address(a).resume();
                _queue.pop_front();
        }
    }

    void lazy_resume(std::coroutine_handle<> h) {
        if (_queue.empty()) {
            push(h);
            flush();
        } else {
            push(h);
        }
    }

protected:


    std::deque<void *> _queue;

};

inline void lazy_resume(std::coroutine_handle<> h) {
    defer_context::get_instance().lazy_resume(h);
}



}