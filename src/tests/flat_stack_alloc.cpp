#include "check.h"
#include "../basic_coro/flat_stack_allocator.hpp"
#include "../basic_coro/pmr_allocator.hpp"
#include "../basic_coro/coroutine.hpp"
#include "../basic_coro/awaitable.hpp"
#include "../basic_coro/co_switch.hpp"


coro::coroutine<int, coro::pmr_allocator<> > recursive_fibo(coro::pmr_allocator<> alloc, int val) {
    if (val <= 1) {
        co_return val;
    }
    int a = co_await recursive_fibo(alloc, val - 1);
    int b = co_await recursive_fibo(alloc, val - 2);
    co_return a+b;
}

coro::coroutine<int, coro::pmr_allocator<coro::flat_stack_memory_resource *> > recursive_fibo_2(coro::pmr_allocator<coro::flat_stack_memory_resource *> alloc, int val) {
    if (val <= 1) {
        co_return val;
    }
    auto awt1 = recursive_fibo_2(alloc, val - 1);
    auto awt2 = recursive_fibo_2(alloc, val - 2);
    co_return (co_await awt1) + (co_await awt2);
}

int main() {
    coro::flat_stack_memory_resource mres(10000);
    int val = recursive_fibo(&mres, 20);
    CHECK_EQUAL(val, 6765);
    val = recursive_fibo_2(&mres, 20);
    CHECK_EQUAL(val, 6765);
}