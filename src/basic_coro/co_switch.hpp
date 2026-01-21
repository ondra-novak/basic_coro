#pragma once

#include "defer_impl.hpp"
#include <coroutine>

namespace coro {

///Awaiter - performs switch between coroutines running in cooperative multitasking on same thread
/**
 * @code
 * co_await co_switch();
 * @endcode
 *
 * This function suspends current coroutine and resumes next coroutine
 * prepared to run. If this function is called by the first time, it
 * resumes itself, however, now you can introduce other coroutines to the
 * queue
 *
 * @code
 * co_await co_switch();    //initiate switching
 * run_coro1();         //run a coroutine which is using co_switch();
 * run_coro2();         //run another coroutine which is using co_switch()
 * @endcode
 */
class co_switch : public std::suspend_always{
public:

    void await_suspend(std::coroutine_handle<> h) {
        defer_context::get_instance().lazy_resume(h);
    }

};

}
