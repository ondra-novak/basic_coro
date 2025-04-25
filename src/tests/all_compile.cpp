#include <basic_coro/basic_coro.hpp>
#include <iostream>



template class coro::async_generator<int>;
template class coro::queue<int, 128>;
template class coro::queue<int>;
template class coro::multi_lock<10>;
template class coro::awaitable<const int &>;
template class coro::awaitable<int &>;
template class coro::distributor<const int>;

template class coro::awaiting_callback<coro::awaitable<int>, int, float, std::string>;

int main() {
    std::cout << sizeof(coro::awaitable<int>) << std::endl;
    return 0;
}

