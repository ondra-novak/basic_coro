# basic_coro

Header-only C++20 coroutine library providing awaitables, schedulers, synchronization primitives, and generators. Single include: `#include <basic_coro/basic_coro.hpp>`.

## Quick Lookup

| Component | What it does | Header | Thread-safe |
|-----------|-------------|--------|-------------|
| `coroutine<T>` | Return type for coroutine functions | `coroutine.hpp` | No |
| `awaitable<T>` | Result of an async operation (promise + future in one) | `awaitable.hpp` | No (optional callback) |
| `awaitable_result<T>` | Callback/promise side — set value, exception, or cancel | `awaitable.hpp` | No |
| `prepared_coro` | Handle to a coroutine ready to resume; controls when resumption happens | `prepared_coro.hpp` | No |
| `coro_frame<T>` | CRTP base to build custom async primitives that look like coroutines | `coro_frame.hpp` | No |
| `pending<T>` | Launch an awaitable now, synchronize with `co_await` later | `pending.hpp` | No |
| `awaitable_transform<Awt,Closure...>` | Transform awaitable result without heap allocation (`.then()` pattern) | `awaitable_transform.hpp` | No |
| `mutex` | Async mutex — can be held across `co_await` | `mutex.hpp` | Yes |
| `queue<T>` | Async FIFO with backpressure | `queue.hpp` | Optional |
| `distributor<T>` | Broadcast value to N waiting coroutines | `distributor.hpp` | Optional |
| `when_all` | Await all of N awaitables | `when_all.hpp` | No |
| `when_each<N>` | Await N awaitables, get results in completion order | `when_each.hpp` | No |
| `scheduler` | Sleep for / sleep until / schedule at | `scheduler.hpp` | Yes |
| `async_generator<T>` | Generator with full `co_await` support inside body | `async_generator.hpp` | No |
| `dispatch_thread` | Background worker thread for coroutine resumption | `dispatch_thread.hpp` | Yes |
| `cancel_signal` | Atomic cancellation token | `cancel_signal.hpp` | Yes |
| `flat_stack_allocator` | Stack-like memory resource for coroutine frames | `flat_stack_allocator.hpp` | No |

## Docs

- [core.md](core.md) — coroutines + async tools (main reference)
- [extras.md](extras.md) — mutex, queue, distributor, scheduler, generator, dispatch_thread
- [trace.md](trace.md) — coroutine lifecycle tracing (`-DBASIC_CORO_ENABLE_TRACE`)
