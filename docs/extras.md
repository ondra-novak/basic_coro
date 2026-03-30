# basic_coro — Extra Components

Brief reference for supplementary components. See [core.md](core.md) for the primary tutorial.

---

## `mutex` — async mutex

Can be held across `co_await`. Ownership is a movable RAII guard.

```cpp
#include <basic_coro/mutex.hpp>

coro::mutex mtx;

coro::coroutine<void> worker() {
    auto lock = co_await mtx.lock();   // suspends until acquired
    // ... critical section (can co_await here) ...
}   // lock released here (or call lock.release() to release early)

// Non-blocking attempt:
if (auto lock = mtx.try_lock()) {
    // acquired immediately
}
```

Releasing early schedules the next waiter and returns a `prepared_coro`:

```cpp
auto next = lock.release();  // schedules next waiter, returns handle
next.lazy_resume();          // resume at safe stack depth
```

---

## `queue<T>` — async FIFO with backpressure

```cpp
#include <basic_coro/queue.hpp>

coro::limited_queue<int, 16> q;   // bounded
// coro::unlimited_queue<int> q;  // unbounded

// Producer
co_await q.push(42);   // suspends if full

// Consumer
auto opt = co_await q.pop();   // suspends if empty; returns nullopt if closed
if (opt) use(*opt);

// Signal end of data
q.close();
```

Thread-safety: pass `std::mutex` as the Lock template parameter for multi-threaded use.

---

## `distributor<T>` — broadcast to N coroutines

```cpp
#include <basic_coro/distributor.hpp>

coro::distributor<int> dist;

// Listener (register with operator()):
coro::coroutine<void> listener(int id) {
    while (true) {
        int v = co_await dist(id);   // id is an identification token
        process(v);
    }
}

// Broadcaster:
dist.broadcast(42);        // wakes all waiting listeners
dist.kick_out(id);         // wakes one listener with await_canceled_exception
dist.alert(cancel_sig);    // wakes all listeners associated with a cancel_signal
```

---

## `when_all` — wait for all

```cpp
#include <basic_coro/when_all.hpp>

coro::awaitable<int> a = op1();
coro::awaitable<std::string> b = op2();

coro::when_all all(a, b);
co_await all;        // suspends until both resolve
int r1 = *a;
std::string r2 = *b;
```

---

## `when_each<N>` — process results in completion order

```cpp
#include <basic_coro/when_each.hpp>

coro::awaitable<int> ops[3] = {op1(), op2(), op3()};
coro::when_each<3> each(ops);

while (each) {
    std::size_t idx = co_await each;   // index of next completed awaitable
    process(*ops[idx]);
}
```

---

## `scheduler` — time-based delays and scheduling

`sleep_for` and `sleep_until` return `awaitable<bool>`: **true** = timed out normally, **false** = interrupted by cancel signal.

```cpp
#include <basic_coro/scheduler.hpp>

coro::scheduler sched;

coro::coroutine<void> periodic() {
    while (true) {
        co_await sched.sleep_for(std::chrono::milliseconds(100));
        tick();
    }
}

// Absolute time:
co_await sched.sleep_until(std::chrono::system_clock::now() + std::chrono::seconds(1));

// Interruptible sleep — pass cancel_signal pointer:
coro::cancel_signal stop;
bool timed_out = co_await sched.sleep_for(std::chrono::milliseconds(500), &stop);
// stop.request_cancel() from another coroutine/thread wakes the sleeper early
// timed_out == false if woken by cancel_signal
```

---

## `async_generator<T>` — generator with full async support

```cpp
#include <basic_coro/async_generator.hpp>

coro::async_generator<int> ticker(coro::scheduler &sched) {
    for (int i = 0; ; ++i) {
        co_await sched.sleep_for(std::chrono::milliseconds(100));
        co_yield i;
    }
}

// Manual iteration:
auto gen = ticker(sched);
while (auto opt = co_await gen()) {
    use(*opt);
}
```

---

## `dispatch_thread` — background worker for coroutine dispatch

Routes coroutine resumption to a dedicated background thread. Useful when async callbacks arrive from foreign threads but you want coroutines to resume in a single consistent thread.

```cpp
#include <basic_coro/dispatch_thread.hpp>

auto disp = coro::dispatch_thread::create();

// Launch a coroutine in the dispatcher thread
auto result_awt = disp->launch(my_coroutine());
int r = coro::sync_await(result_awt);

// Async callbacks using dispatch_result reroute resumption to dispatcher thread:
coro::awaitable<int> api_call() {
    return [](coro::awaitable_result<int> promise) {
        auto dp = coro::dispatch_result(std::move(promise));  // wraps result
        std::thread([dp = std::move(dp)]() mutable {
            dp(42);  // resumes coroutine in dispatcher thread, not this thread
        }).detach();
    };
}

// Shut down dispatcher (waits for queue to drain):
co_await disp->join(std::move(disp));
```

---

## `cancel_signal` — atomic cancellation token

```cpp
#include <basic_coro/cancel_signal.hpp>

coro::cancel_signal stop;

// From any thread:
stop.request_cancel();

// Inside coroutine:
if (stop.is_canceled()) break;

// Pass to scheduler (as pointer), distributor alert, etc.:
bool ok = co_await sched.sleep_for(std::chrono::milliseconds(500), &stop);
dist.alert(stop);

// Reset for reuse:
stop.reset();
```

---

## Memory: `flat_stack_allocator`

Fast LIFO slab allocator for coroutine frames. Useful for deeply recursive or high-frequency coroutines. Preallocates a large block; allocation is a pointer bump.

```cpp
#include <basic_coro/flat_stack_allocator.hpp>

coro::flat_stack_memory_resource slab(65536);  // 64 KB
coro::pmr_allocator alloc(&slab);

// Pass alloc as first argument to coroutine:
coro::coroutine<int, coro::pmr_allocator> deep(coro::pmr_allocator &, int n) {
    if (n == 0) co_return 0;
    co_return co_await deep(alloc, n - 1) + 1;
}
```

`reusable_allocator` recycles the same buffer for a coroutine that is created and destroyed in a tight loop.
