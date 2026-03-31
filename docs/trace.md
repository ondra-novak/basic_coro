# basic_coro — Coroutine Tracing (`-DBASIC_CORO_ENABLE_TRACE`)

Opt-in runtime tracing of coroutine lifecycle events. When disabled (the default), all trace calls compile to inline no-ops — zero overhead, nothing to link.

---

## Enabling

Compile with the preprocessor flag:

```sh
# CMake
cmake -DCMAKE_CXX_FLAGS="-DBASIC_CORO_ENABLE_TRACE" ...

# Direct compiler
g++ -DBASIC_CORO_ENABLE_TRACE ...
```

Or define it before the first include:

```cpp
#define BASIC_CORO_ENABLE_TRACE
#include <basic_coro/basic_coro.hpp>
```

---

## What gets traced

When enabled, the library calls these hook functions at every significant coroutine event:

| Hook | When called | Extra arg |
|------|------------|-----------|
| `basic_coro_trace_create` | Coroutine frame allocated | — |
| `basic_coro_trace_init` | Coroutine body starts (first `initial_suspend`) | source location |
| `basic_coro_trace_suspend` | Coroutine suspends at a `co_await` | source location |
| `basic_coro_trace_resume` | Coroutine is resumed | — |
| `basic_coro_trace_exception` | `unhandled_exception()` fires inside the body | — |
| `basic_coro_trace_destroy` | Coroutine frame deallocated | — |
| `basic_coro_trace_setname` | `co_await coro::set_name(...)` executed | `string_view` name |

The source location arguments use `std::source_location::current()` captured at the call site, giving you the function name, file, and line of the `co_await` expression.

---

## Implementing the hooks

The hooks are ordinary free functions with external linkage — declare them somewhere in **one** translation unit:

```cpp
#ifdef BASIC_CORO_ENABLE_TRACE
#include <coroutine>
#include <source_location>
#include <iostream>

void basic_coro_trace_create(std::coroutine_handle<> h) noexcept {
    std::cout << "Create  " << h.address() << "\n";
}
void basic_coro_trace_destroy(std::coroutine_handle<> h) noexcept {
    std::cout << "Destroy " << h.address() << "\n";
}
void basic_coro_trace_init(std::coroutine_handle<> h, std::source_location loc) noexcept {
    std::cout << "Init    " << h.address()
              << "  at " << loc.function_name()
              << " (" << loc.file_name() << ":" << loc.line() << ")\n";
}
void basic_coro_trace_suspend(std::coroutine_handle<> h, std::source_location loc) noexcept {
    std::cout << "Suspend " << h.address()
              << "  at " << loc.function_name()
              << " (" << loc.file_name() << ":" << loc.line() << ")\n";
}
void basic_coro_trace_resume(std::coroutine_handle<> h) noexcept {
    std::cout << "Resume  " << h.address() << "\n";
}
void basic_coro_trace_exception(std::coroutine_handle<> h) noexcept {
    std::cout << "Except  " << h.address() << "\n";
}
void basic_coro_trace_setname(std::coroutine_handle<> h, std::string_view name) noexcept {
    std::cout << "Name    " << h.address() << "  = " << name << "\n";
}
#endif
```

This is exactly what `src/tests/trace.cpp` does — the test build always compiles that file so the hooks are available whenever the flag is set.

---

## Naming coroutines — `coro::set_name`

Coroutine handles are identified only by their memory address. Assign a human-readable name early in the body:

```cpp
#include <basic_coro/trace.hpp>   // or basic_coro.hpp

coro::coroutine<void> worker(int id) {
    co_await coro::set_name("worker");   // fires basic_coro_trace_setname
    // ...
}
```

`set_name` is a `co_await`-able that **does not suspend** the coroutine — it calls the hook and immediately continues. When `BASIC_CORO_ENABLE_TRACE` is not defined it compiles to `co_await std::suspend_never{}`, which the compiler optimises away entirely.

---

## Sample output

For a simple coroutine that suspends once then completes:

```
Create  0x55a3f4c02eb0
Init    0x55a3f4c02eb0  at worker(worker.cpp:12)
Name    0x55a3f4c02eb0  = worker
Suspend 0x55a3f4c02eb0  at worker(worker.cpp:15)
Resume  0x55a3f4c02eb0
Destroy 0x55a3f4c02eb0
```

---

## Integration points in the library

The hooks are called from:

| Source file | Hook(s) called |
|-------------|---------------|
| `coroutine.hpp` — `promise_type` ctor/dtor | `create`, `destroy` |
| `coroutine.hpp` — `initial_suspend()` | `init` (with source location) |
| `coroutine.hpp` — `unhandled_exception()` | `exception` |
| `awaitable.hpp` — `await_suspend()` | `suspend` (with source location) |
| `awaitable.hpp` — `wakeup()` | `resume` |
| `coro_frame.hpp` — `basic_coro_frame` ctor/dtor | `create`, `destroy` |
| `coro_frame.hpp` — `emulated_coro_frame` promise ctor/dtor | `create`, `destroy` |
| `trace.hpp` — `set_name::await_suspend()` | `setname` |

---

## Notes

- All hook functions are declared `noexcept`. Throwing from a hook is undefined behaviour.
- Hooks are called on the same thread that drives the coroutine — no synchronisation is provided. If coroutines run on multiple threads, guard your output (e.g. with a mutex or an atomic log buffer).
- The `create` hook fires in the constructor of the promise type, **before** the coroutine body runs. The `init` hook fires at `initial_suspend`, which is the first point the body executes.
- `coro_frame`-based pseudo-coroutines (used to build custom async primitives) also call `create`/`destroy`, so every handle that participates in the scheduler appears in the trace.
