#pragma once
#include <concepts>
#include <coroutine>
#include <type_traits>
#include <iterator>
#include <optional>

namespace coro {


template<typename T>
struct is_coroutine_handle : std::false_type {};

template<typename Promise>
struct is_coroutine_handle<std::coroutine_handle<Promise>> : std::true_type {};


template<typename T>
constexpr bool is_coroutine_handle_v = is_coroutine_handle<T>::value;


template<typename T>
concept await_suspend_result = std::is_void_v<T> || std::is_convertible_v<T, bool> || is_coroutine_handle_v<T>;

template<typename T>
concept is_awaiter = requires(T a, std::coroutine_handle<> h) {
    {a.await_ready()} -> std::convertible_to<bool>;
    {a.await_suspend(h)} -> await_suspend_result;
    {a.await_resume()};
};

template<typename T>
concept has_co_await = requires(T a) {
    { a.operator co_await() } -> is_awaiter;
};

template<typename T>
concept has_global_co_await = requires(T a) {
    { operator co_await(a) } -> is_awaiter;
};

template<typename T>
concept is_awaitable = is_awaiter<T> || has_global_co_await<T> || has_co_await<T>;


template<is_awaitable T>
decltype(auto) extract_awaiter(T&& value) {
    if constexpr (has_co_await<T>) {
        return std::forward<T>(value).operator co_await();
    }
    else if constexpr (has_global_co_await<T>) {
        return operator co_await(std::forward<T>(value));
    }
    else {
        return std::forward<T>(value);
    }
}

template<is_awaitable T>
using extract_awaiter_t = decltype(extract_awaiter(std::declval<T>()));

template<typename T>
concept range_for_iterable = requires(T t) {
    { std::begin(t) } -> std::input_or_output_iterator;
    { std::end(t) } -> std::input_or_output_iterator;
    requires std::sentinel_for<decltype(std::end(t)), decltype(std::begin(t))>;
};

template<is_awaitable T>
struct awaiter_result_def {
    using type = decltype(extract_awaiter(std::declval<T>()).await_resume());
};

///definition of allocator interface
template<typename T>
concept coro_allocator = (requires(T &val, void *ptr, std::size_t sz, float b, char c) {
    ///static function alloc which should accept multiple arguments where one argument can be reference to its instance - returns void *
    /** the first argument must be size */
    {T::overrides::operator new(sz, val, b, c, ptr)} -> std::same_as<void *>;
    ///static function dealloc which should accept pointer and size of allocated space
    {T::overrides::operator delete(ptr, sz)};
});  //void can be specified in meaning of default allocator


///Determines type returned by the awaiter
template<is_awaitable T>
using awaiter_result = typename awaiter_result_def<T>::type;

template<typename T>
struct temporary_awaiter_def;

struct empty_type {};

template<is_awaiter T>
struct temporary_awaiter_def<T> {
    using type = empty_type;
};

template<has_co_await T>
struct temporary_awaiter_def<T> {
    using type = decltype(std::declval<T>().operator co_await());
};

template<has_global_co_await T>
struct temporary_awaiter_def<T> {
    using type = decltype(operator co_await(std::declval<T>()));
};

template<is_awaitable T>
using temporary_awaiter_type = typename temporary_awaiter_def<T>::type;


///tests whether object T  can be used as member function pointer with Obj as pointer
template<typename T, typename Obj, typename Fn>
concept is_member_fn_call_for_result = requires(T val, Obj obj, Fn fn) {
    {((*obj).*fn)(std::move(val))};
};

template<typename _AwaitableResultType, typename ... _ResultArguments>
concept is_awaitable_valid_result_type = (
    (std::is_void_v<_AwaitableResultType> && sizeof...(_ResultArguments) == 0)
    || std::is_constructible_v<_AwaitableResultType, _ResultArguments...>
    || (sizeof...(_ResultArguments) == 1 &&
        (std::is_invocable_r_v<_AwaitableResultType, _ResultArguments> &&...))
    || (sizeof...(_ResultArguments) == 1 &&
         (std::is_same_v<std::nullopt_t, std::decay_t<_ResultArguments>> &&...))
);

///replaces simple void everywhere valid type is required
struct void_type {};
template<typename T> using voidless_type = std::conditional_t<std::is_void_v<T>, void_type, T>;

template<typename T> concept has_has_value = requires(T val) {
    {val.has_value()}->std::convertible_to<bool>;
};


}
