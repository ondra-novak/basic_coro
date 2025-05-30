#include <basic_coro/async_generator.hpp>

#include "check.h"

#include <thread>
using namespace coro;

awaitable<void> thread_sleep(std::chrono::system_clock::duration dur) {
    return [dur](auto p) {
        std::thread thr([dur, p = std::move(p)]() mutable {
            std::this_thread::sleep_for(dur);
            p();
        });
        thr.detach();
    };
}


generator<int> fibo(int count) {
    unsigned int a = 0;
    unsigned int b = 1;

    for (int i = 0; i < count; ++i) {
        co_yield a;
        int c = a+b;
        a = b;
        b = c;
    }

}

generator<int> async_fibo(int count) {
    int a = 0;
    int b = 1;

    for (int i = 0; i < count; ++i) {
        co_await thread_sleep(std::chrono::milliseconds(0));
        co_yield a;
        unsigned int c = a+b;
        a = b;
        b = c;
    }
}


coroutine<void> async_fibo_test2() {
    int results[] = {0,1,1,2,3,5,8,13,21,34};
    auto gen = async_fibo(10);
    auto iter = std::begin(results);
    auto val = gen();
    while (co_await val.ready()) {
        int v = co_await val;
        CHECK_EQUAL(v,*iter);
        val = gen();
        ++iter;
    }
    CHECK(iter == std::end(results));
}

coroutine<void> async_fibo_test3() {
    int results[] = {0,1,1,2,3,5,8,13,21,34};
    auto gen = async_fibo(10);
    auto iter = std::begin(results);
    for (auto val = gen(); co_await val.ready(); val = gen()) {
        int v = co_await val;
        CHECK_EQUAL(v,*iter);
        ++iter;
    }
    CHECK(iter == std::end(results));
}

int test_end() {
    int r = 0;
    auto g = fibo(10);
    auto a = g();
    while (a.ready().get()) {
        a = g();
        ++r;
    }
    return r;
}

int main() {

    int results[] = {0,1,1,2,3,5,8,13,21,34};

    auto iter = std::begin(results);
    for (int v: fibo(10)) {
        int x = *iter;
        ++iter;
        CHECK_EQUAL(x,v);
    }

    CHECK_EQUAL(test_end(), 10);

    iter = std::begin(results);
    for (int v: async_fibo(10)) {
        int x = *iter;
        ++iter;
        CHECK_EQUAL(x,v);
    }

    async_fibo_test2().get();
    async_fibo_test3().get();
}
