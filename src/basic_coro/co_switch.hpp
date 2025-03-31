#pragma once

#include <coroutine>
#include <queue>

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

    ///allows to push handle to the co_switch queue
    /**
     * This function can be used to prevent uncessery recursion. If the
     * current stack state is result of coroutine resumption, specified
     * coroutine is resumed after current coroutine is finished or suspended.
     *
     * If there is no such coroutine, the function simply resumes h, but
     * also enables queue for any furher coroutines while current coroutine
     * is active
     *
     * @param h
     */
    static void lazy_resume(std::coroutine_handle<> h) {
        thread_local std::queue<std::coroutine_handle<> > coro_local_queue = {};
        if (!h) return;
        bool e= coro_local_queue.empty();
        coro_local_queue.push(h);
        if (e) {
            while (!coro_local_queue.empty()) {
                coro_local_queue.front().resume();
                coro_local_queue.pop();
            }
        }
    }

    void await_suspend(std::coroutine_handle<> h) {
        lazy_resume(h);
    }

};

}
