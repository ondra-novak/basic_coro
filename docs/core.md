# basic_coro — Core Reference

## 1. Coroutines

### Creating a coroutine

Any function that returns `coroutine<T>` is a coroutine. Use `co_return` to produce the result.

```cpp
#include <basic_coro/coroutine.hpp>

coro::coroutine<int> compute(int x) {
    co_return x * 2;
}

coro::coroutine<void> log_message(std::string msg) {
    std::cout << msg << '\n';
    co_return;
}
```

`coroutine<T>` is move-only. If you hold it and never `co_await` it, dropping the object starts the coroutine in **detached mode** (fire-and-forget).

### Attached vs. detached mode

```cpp
// Attached: caller co_awaits the result
coro::awaitable<int> outer() {
    int r = co_await compute(21);  // waits for result
    co_return r;
}

// Detached: drop the coroutine — starts immediately, result is lost
void fire_and_forget() {
    auto c = compute(21);  // created but not co_awaited
    // c goes out of scope → coroutine starts detached
}
```

To detect from inside whether you are running detached:

```cpp
coro::coroutine<int> compute(int x) {
    bool detached = co_await coro::coroutine<int>::is_detached();
    if (!detached) {
        // result matters — do expensive work
    }
    co_return x * 2;
}
```

To cancel a coroutine you hold (prevents it from starting detached):

```cpp
auto c = compute(21);
c.cancel();  // destroys coroutine; awaiting caller receives await_canceled_exception
```

### Allocator support

Pass an allocator as the **first argument** of the coroutine function to use custom allocation for the coroutine frame:

```cpp
#include <basic_coro/allocator.hpp>
#include <basic_coro/flat_stack_allocator.hpp>

// Reusable allocator — recycles same buffer for same coroutine created repeatedly
coro::reusable_allocator<> alloc;

coro::coroutine<int, decltype(alloc)> tight_loop(coro::reusable_allocator<> &, int x) {
    co_return x * 2;
}

// flat_stack_allocator — fast LIFO slab; good for recursive or deeply nested coroutines
coro::flat_stack_memory_resource slab(65536);  // 64 KB slab
coro::pmr_allocator pmr_alloc(&slab);

coro::coroutine<int, coro::pmr_allocator> nested(coro::pmr_allocator &, int depth) {
    if (depth == 0) co_return 0;
    co_return co_await nested(pmr_alloc, depth - 1) + 1;
}
```

The allocator type becomes part of the coroutine's type signature. The default (`objstdalloc`) uses `::operator new/delete`.

### RVO via lambda return

To return an immovable object, return a lambda that constructs it:

```cpp
coro::coroutine<std::string> build_string() {
    co_return [&]{ return std::string("hello"); };  // RVO-friendly
}
```
