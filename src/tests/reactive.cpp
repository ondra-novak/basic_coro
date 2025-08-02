#include <basic_coro/reactive.hpp>
#include <basic_coro/coroutine.hpp>
#include "check.h"



coro::coroutine<void> watcher1(coro::reactive<int, coro::reactive_hash_compare_policy<int> > &example, bool &processed) {
    auto v =co_await watch(example);
    CHECK_NOT_EQUAL(v, nullptr);
    CHECK_EQUAL(56 , *v);
    processed = true;
    v =co_await watch(example);
    CHECK_EQUAL(v , nullptr);
}




int main(int, char **) {

    bool processed = false;
    coro::reactive<int, coro::reactive_hash_compare_policy<int> > example(42);
    watcher1(example,processed);
    example = 56;
    CHECK(processed);
    return 0;
}