#pragma once

#include "awaitable.hpp"
#include "prepared_coro.hpp"
#include <concepts>
#include <exception>
#include <functional>
#include <optional>
#include <utility>


namespace coro {

template<typename T> concept is_awaitable_result_compatible = requires(T obj, 
        typename T::const_reference cval, 
        typename T::rvalue_reference  rval,
        std::exception_ptr eptr
    ) {
    {std::invoke(obj, cval)}->std::convertible_to<prepared_coro>;
    {std::invoke(obj, std::move(rval))}->std::convertible_to<prepared_coro>;
    {std::invoke(obj, eptr)}->std::convertible_to<prepared_coro>;
    {std::invoke(obj, std::nullopt)}->std::convertible_to<prepared_coro>;
};

///converts awaitable result to a callback with executor, which allows to resume awaiting coroutine in different context
/**
The executor can be for example a thread pool or a thread dispatcher. 
@param T type of result of the awaitable

*/
template<is_awaitable_result_compatible _AwaitableResult, std::invocable<prepared_coro> _Executor>
class result_proxy {
public:

    result_proxy(_AwaitableResult awt_result, _Executor executor)
        :_result(std::move(awt_result))
        ,_executor(std::move(executor)) {}

    template<typename ... Args>
    requires(std::is_invocable_v<_AwaitableResult, Args...>)
    auto operator()(Args && ... args) {
        return _executor(_result(std::forward<Args>(args)...));
    }
protected:
    _AwaitableResult _result;
    _Executor _executor;
};

template<typename T, typename CB>
result_proxy(awaitable_result<T>, CB cb) -> result_proxy<awaitable_result<T>, CB>;

}