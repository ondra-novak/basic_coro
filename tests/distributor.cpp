#include <basic_coro/distributor.hpp>
#include <basic_coro/when_each.hpp>
#include "basic_coro/cancel_signal.hpp"
#include "check.h"


using namespace coro;

static int count_resume = 0;


awaitable<void> coro_test(distributor<int, empty_lockable> &dist, cancel_signal *ident) {

    int i = co_await dist(ident);
    count_resume++;
    CHECK_EQUAL(i, 10);
    i = co_await dist(ident);
    count_resume++;
    CHECK_EQUAL(i, 20);
    i = co_await dist(ident);
    count_resume++;
    CHECK_EQUAL(i, 30);
    i = co_await dist(ident);
    count_resume++;
    CHECK_EQUAL(i, 40);
}

awaitable<void> coro_alert_test(distributor<int, empty_lockable> &dist, cancel_signal *b) {

    int p = 10;
    while (true) {
        int i = co_await dist(b);
        count_resume++;
        CHECK_EQUAL(i, p);
        p+=10;
    }
}


int main() {
    cancel_signal ident_a;
    cancel_signal ident_b;
    cancel_signal ident_c;
    cancel_signal alt;
    cancel_signal alt2;
    distributor<int, empty_lockable> dist;
    awaitable<void> a = coro_test(dist, &ident_a);
    awaitable<void> b = coro_test(dist, &ident_b);
    awaitable<void> c = coro_test(dist, &ident_c);
    awaitable<void> d = coro_alert_test(dist, &alt);
    awaitable<void> e = coro_alert_test(dist, &alt2);
    when_each as(a,b,c,d,e);

    std::vector<prepared_coro> buff;
    dist.broadcast(buff,10);
    dist.cancel(&alt2);
    buff.clear();
    int n = as.wait();
    CHECK_EQUAL(n,4);
    dist.broadcast(20);
    dist.cancel(&ident_b);
    n = as.wait();
    CHECK_EQUAL(n,1);
    dist.broadcast(30);
    dist.cancel(&alt);
    CHECK_EQUAL(static_cast<bool>(alt), true);
    n = as.wait();
    CHECK_EQUAL(n,3);
    dist.broadcast(40);
    as.wait();
    as.wait();
    CHECK_EQUAL(count_resume, 14);



}
