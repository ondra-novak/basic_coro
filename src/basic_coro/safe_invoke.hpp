#pragma once

#include <functional>
#include <type_traits>

template<typename A, typename Callable,  typename ... Args>
A invoke_force_result(Callable &&callable, Args && ... args) {
    using Ret = std::invoke_result_t<Callable, Args...>;
    if constexpr(std::is_constructible_v<A, Ret>) {
        return A(std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...));
    } else {
        std::invoke(std::forward<Callable>(callable), std::forward<Args>(args)...);
        return A{};
    }
}
