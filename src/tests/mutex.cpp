#include <basic_coro/mutex.hpp>
#include "check.h"
#include <vector>

using namespace coro;

void test1() {
    mutex mx;

    auto l1 = mx.lock();
    auto l2 = mx.lock();
    auto l3 = mx.lock();
    CHECK(l1.is_ready());
    CHECK(!l2.is_ready());
    CHECK(!l3.is_ready());
    std::vector<int> res;
    l2 >> [&](awaitable<mutex::ownership> &r){
        mutex::ownership own = *std::move(r);
        res.push_back(2);
    };
    l3 >> [&](awaitable<mutex::ownership> &r){
        mutex::ownership own = *std::move(r);
        res.push_back(3);
    };
    mutex::ownership own = l1.get();
    res.push_back(1);
    own.release();
    CHECK_EQUAL(res.size(),3);
    CHECK_EQUAL(res[0],1);
    CHECK_EQUAL(res[1],2);
    CHECK_EQUAL(res[2],3);

}


int main() {
    test1();
    return 0;
}