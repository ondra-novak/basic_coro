#pragma once
#include "concepts.hpp"
#include <coroutine>
#include <source_location>


#ifdef BASIC_CORO_ENABLE_TRACE
    void basic_coro_trace_create(std::coroutine_handle<> h) noexcept;
    void basic_coro_trace_destroy(std::coroutine_handle<> h) noexcept;
    void basic_coro_trace_init(std::coroutine_handle<> h, std::source_location loc) noexcept;
    void basic_coro_trace_setname(std::coroutine_handle<> h, std::string_view name) noexcept;
    void basic_coro_trace_suspend(std::coroutine_handle<> h, std::source_location loc) noexcept;
    void basic_coro_trace_resume(std::coroutine_handle<> h) noexcept;
    void basic_coro_trace_exception(std::coroutine_handle<> h) noexcept;
#else
    inline void basic_coro_trace_create(std::coroutine_handle<>) noexcept {}
    inline void basic_coro_trace_destroy(std::coroutine_handle<>) noexcept {}
    inline void basic_coro_trace_init(std::coroutine_handle<>, std::source_location) noexcept {}
    inline void basic_coro_trace_suspend(std::coroutine_handle<>, std::source_location) noexcept {}
    inline void basic_coro_trace_setname(std::coroutine_handle<>, std::string_view) noexcept {}
    inline void basic_coro_trace_resume(std::coroutine_handle<> ) noexcept {}
    inline void basic_coro_trace_exception(std::coroutine_handle<> ) noexcept {}
#endif

namespace coro {
#ifdef BASIC_CORO_ENABLE_TRACE

    class set_name : public std::suspend_always{
    public:
        set_name(std::string_view name):name(name) {};
        bool await_suspend(std::coroutine_handle<> h) {
            basic_coro_trace_setname(h, name);
            return false;
        }        
    protected:
        std::string_view name;
    };
#else
    inline std::suspend_never set_name(const auto &) {return {};}
#endif
}
