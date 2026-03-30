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

### `prepared_coro` — controlling when a coroutine resumes

`prepared_coro` is a move-only handle to a coroutine that is ready to be resumed. Holding it **delays** resumption; releasing or destroying it **triggers** resumption.

```cpp
#include <basic_coro/prepared_coro.hpp>

// Resume immediately
coro::prepared_coro pc = get_some_prepared_coro();
pc.resume();  // resumes right now on current stack

// Resume lazily (avoids deep stack growth)
pc.lazy_resume();  // enqueues, flushes at bottom of current call stack

// Return for symmetric transfer (inside await_suspend)
return pc.symmetric_transfer();  // tail-call to the next coroutine

// Destroy instead of resuming
pc.destroy();  // terminates the coroutine; awaiter gets await_canceled_exception
```

**`resume()` vs `lazy_resume()`:** When a chain of coroutines each resume the next one, `resume()` grows the call stack. `lazy_resume()` enqueues handles in a thread-local queue and drains from the bottom — the entire chain executes at constant stack depth.

Use `lazy_resume()` when you are inside a synchronization primitive (mutex unlock, distributor broadcast) and could be resuming many coroutines.

`prepared_coro` as a coroutine return type creates a "start-suspended" coroutine:

```cpp
coro::prepared_coro my_task() noexcept {
    co_await something();
    co_return;
}

auto task = my_task();  // created, suspended at initial_suspend
task.resume();          // actually starts it
```

`prepared_coros<N>` holds up to N handles and resumes/lazy-resumes all at once:

```cpp
coro::prepared_coros<4> batch;
batch.add(std::move(pc1));
batch.add(std::move(pc2));
batch.lazy_resume();  // resumes all at constant stack depth
```

### `coro_frame<T>` — custom async primitives

`coro_frame<T>` lets you build an object that *looks like* a coroutine (has a `coroutine_handle`) without actually being one. Used internally by `pending`, `awaitable_transform`, `when_all`, etc.

CRTP pattern: inherit from `coro_frame<YourClass>` and implement `do_resume()` (and optionally `do_destroy()`).

```cpp
#include <basic_coro/coro_frame.hpp>

// A frame that counts how many times it is resumed
struct CountingFrame : coro::coro_frame<CountingFrame> {
    int count = 0;

    coro::prepared_coro do_resume() {
        ++count;
        return {};  // return empty — nobody to resume next
    }

    void do_destroy() {
        // called when handle.destroy() is invoked; default calls delete
    }
};

CountingFrame frame;
auto handle = frame.create_handle();  // coroutine_handle<> pointing at frame
handle.resume();  // calls frame.do_resume()
```

`do_resume()` can return `void` or `prepared_coro`. If it returns `prepared_coro`, the returned coro is resumed via `lazy_resume()` automatically.

`coro_frame_cb<CB>` is a convenience wrapper when you only need a single callback:

```cpp
int count = 0;
coro::coro_frame_cb frame([&count]() -> coro::prepared_coro {
    ++count;
    return {};
});
frame.create_handle().resume();  // count == 1
```

**When to use:** Write your own `coro_frame` when you need an object to receive resumption events (i.e., be usable as the `h` in `await_suspend(h)`) but you don't want the overhead of a full coroutine allocation.
