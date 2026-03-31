#include <iostream>
#ifdef BASIC_CORO_ENABLE_TRACE
#include <coroutine>
#include <source_location>
#endif

#ifdef BASIC_CORO_ENABLE_TRACE

template<typename IO> IO &operator << (IO &io, std::coroutine_handle<> h) {
    io << h.address();
    return io;
}

template<typename IO> IO &operator << (IO &io, const std::source_location &loc) {
    io << loc.function_name() << "(" << loc.file_name() << ":" <<  loc.line() <<")";
    return io;
}

void basic_coro_trace_create(std::coroutine_handle<> h) noexcept {
    std::cout << "Create coro: " << h << std::endl;
}
void basic_coro_trace_destroy(std::coroutine_handle<> h) noexcept {
    std::cout << "Destroy coro: " << h << std::endl;
}
void basic_coro_trace_init(std::coroutine_handle<> h, std::source_location loc) noexcept {
    std::cout << "Init coro: " << h << " at " << loc << std::endl;
}

void basic_coro_trace_suspend(std::coroutine_handle<> h, std::source_location loc) noexcept {
    std::cout << "Suspend coro: " << h << " at " << loc << std::endl;
}
void basic_coro_trace_resume(std::coroutine_handle<> h) noexcept {
    std::cout << "Resume coro: " << h << std::endl;
}
void basic_coro_trace_exception(std::coroutine_handle<> h) noexcept {
    std::cout << "Exception coro: " << h << std::endl;
}
void basic_coro_trace_setname(std::coroutine_handle<> h, std::string_view name) noexcept {
    std::cout << "Set name of  coro: " << h << " = " << name <<  std::endl;
}
#endif