#include <basic_coro/scheduler.hpp>


#include "check.h"


using namespace coro;


awaitable<int> cycle_coro(scheduler &sch, cancel_signal &flag) {
    int count_cycles = 0;
    while (!flag) {
        count_cycles++;
        co_await sch.sleep_for(std::chrono::milliseconds(100),&flag);
        co_await sch.sleep_for(std::chrono::milliseconds(100));
    }
    co_return count_cycles;
}


awaitable<int> main_coro(scheduler &sch, std::chrono::milliseconds ms) {
    cancel_signal flag;

    auto c = cycle_coro(sch, flag).launch();
    co_await sch.sleep_for(ms);
    sch.cancel(&flag);
    co_return co_await c;

}

int main() {
    scheduler sch;
    int count = sch.await(main_coro(sch,std::chrono::milliseconds(950)));
    CHECK_EQUAL(count,5);
    count = sch.await(main_coro(sch,std::chrono::milliseconds(550)));
    CHECK_EQUAL(count,3);
    return 0;
}

