#include <basic_coro/awaitable_transform.hpp>
#include "check.h"
#include <basic_coro/sync_await.hpp>
#include <stdexcept>


void test_ready_awaiter_return_value() {
    coro::awaitable_transform<coro::awaitable<int>, int> trn;
    coro::awaitable<int> val(42);
    auto resawt = trn(std::move(val),[](int v){return 100-v;});
    CHECK(resawt.is_ready());
    CHECK_EQUAL(resawt.get(), 100-42);
}

void test_ready_awaiter_return_nullopt() {
    coro::awaitable_transform<coro::awaitable<int>, int> trn;
    coro::awaitable<int> val(std::nullopt);
    auto resawt = trn(std::move(val),[](int v){return 100-v;});
    CHECK(resawt.is_ready());
    CHECK(!resawt.has_value());
}

void test_ready_awaiter_return_exception() {
    coro::awaitable_transform<coro::awaitable<int>, int> trn;
    coro::awaitable<int> val(std::make_exception_ptr(std::runtime_error("error")));
    auto resawt = trn(std::move(val),[](int v){return 100-v;});
    CHECK(resawt.is_ready());
    CHECK_EXCEPTION(std::runtime_error, resawt.get());
}

void test_async_awaiter_return_value() {
    coro::awaitable_transform<coro::awaitable<int>, int> trn;    
    coro::awaitable<int> val = [](auto promise){return promise(42);};
    auto resawt = trn(std::move(val),[](int v){return 100-v;});
    CHECK(!resawt.is_ready());
    auto ret = coro::sync_await(resawt);
    CHECK_EQUAL(ret, 100-42);
}

void test_async_awaiter_return_nullopt() {
    coro::awaitable_transform<coro::awaitable<int>, int> trn;
    coro::awaitable<int> val = [](auto promise){return promise(std::nullopt);};
    auto resawt = trn(std::move(val),[](int v){return 100-v;});
    CHECK(!resawt.is_ready());
    auto state = coro::sync_await(resawt.ready());
    CHECK(!state);
}

void test_async_awaiter_return_exception() {
    coro::awaitable_transform<coro::awaitable<int>, int> trn;
    coro::awaitable<int> val = [](auto promise){return promise(std::make_exception_ptr(std::runtime_error("error")));};
    auto resawt = trn(std::move(val),[](int v){return 100-v;});
    CHECK(!resawt.is_ready());
    coro::sync_await(resawt.ready());
    CHECK_EXCEPTION(std::runtime_error, resawt.get());
}

coro::awaitable<int> cycle_convert(auto &awt, auto &trn) {
    return trn(std::move(awt), [&trn](int v){
        if (v == 100) return coro::awaitable<int>(v);
        else {
            coro::awaitable<int> init_val([v](auto prom){prom(v+1);});
            return cycle_convert(init_val, trn);
        }
    });
}

void test_async_awaiter_return_awaiter() {
    coro::awaitable_transform<coro::awaitable<int>, int,int> trn;
    coro::awaitable<int> val = [](auto promise){return promise(42);};
    auto resawt = cycle_convert(val, trn);
    int r = coro::sync_await(resawt);
    CHECK_EQUAL(r, 100);
}

int main() {
    test_async_awaiter_return_awaiter();
    test_ready_awaiter_return_value();
    test_ready_awaiter_return_nullopt();
    test_ready_awaiter_return_exception();
    test_async_awaiter_return_value();
    test_async_awaiter_return_nullopt();
    test_async_awaiter_return_exception();
}