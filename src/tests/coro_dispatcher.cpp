#include "basic_coro/dispatch_thread.hpp"
#include "basic_coro/sync_await.hpp"
#include "check.h"
#include <basic_coro/awaitable.hpp>
#include <basic_coro/coroutine.hpp>
#include <basic_coro/trace.hpp>
#include <thread>

coro::awaitable<int> api_call_run() {
    return [](auto promise) {
        auto disp_promise = coro::dispatch_result(std::move(promise));
        std::thread thr([disp_promise = std::move(disp_promise)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            disp_promise(42);
        });
        thr.detach();

    };
}


coro::awaitable<int> run_fn() {
    co_await coro::set_name("standard run, resume from other thread");
    auto id1 = std::this_thread::get_id();    
    int r = co_await api_call_run();
    auto id2 = std::this_thread::get_id();    
    CHECK(id1 == id2);
    co_return r;
}

int main() {
    auto disp = coro::dispatch_thread::create();
    //launch in dispatcher
    auto fnres = disp->launch(run_fn());
    //await for result
    int r = coro::sync_await(fnres);
    //check result
    CHECK_EQUAL(r, 42);
    //join dispatcher
    coro::sync_await(disp->join(std::move(disp)));
}