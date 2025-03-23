#include <basic_coro/queue.hpp>
#include <basic_coro/when_all.hpp>
#include "check.h"

using namespace coro;

coroutine<void> push_coro(coro_queue<char, 5> &q) {
    for (char c = '0'; c <='9'; ++c) {
        co_await q.push(c);
    }
    q.close();
    co_return;
}

void queue_push_test() {
    coro_queue<char, 5> q;
    push_coro(q);
    std::string out;

    awaitable<char> r = q.pop();
    while (r.has_value()) {
        out.push_back(r.await_resume());
        r = q.pop();
    }

    CHECK_EQUAL(out, "0123456789");

}

coroutine<void> pop_coro(coro_queue<char, 10> &q, std::string expect) {
    std::string out;
    auto r = q.pop();
    while (co_await r.has_value()) {
        out.push_back(r);
        r = q.pop();
    }
    CHECK_EQUAL(out, expect);
    co_return;
}

void queue_pop_test() {
    coro_queue<char, 10> q;
    awaitable<void> c1 = pop_coro(q, "02468");
    awaitable<void> c2 = pop_coro(q, "13579");
    when_all wall(c1,c2);
    for (char i = '0'; i<='9'; ++i) {
        q.push(i);
    }
    q.close();
    wall.wait();
}

int main() {
    queue_push_test();
    queue_pop_test();
    return 0;
}
