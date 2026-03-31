#include <basic_coro/basic_coro.hpp>
#include <basic_coro/result_proxy.hpp>
#include <iostream>




template class coro::async_generator<int>;
template class coro::queue<int, 128>;
template class coro::queue<int>;
template class coro::multi_lock<10>;
template class coro::awaitable<const int &>;
template class coro::awaitable<int &>;
template class coro::distributor<const int>;
template class coro::pmr_allocator<>;


int main() {
    std::cout << sizeof(coro::awaitable<int>) << std::endl;
    return 0;
}

