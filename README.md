# Basic coroutines

list of useful templates to make work with coroutines much easier

Header only library

## Classes

* **awaitable<T>** - used as generic return value allows to co_await on return value. You can return awaitable<T> from a coroutine.
* **coroutine<T, Allocator>** - generic class to write coroutines allows to specify allocator. Result object is convertible to **awaitable<T>**
* **when_all** - co_await on multiple awaiters
* **when_each** - co_await and enumerate each awaiter in order of completion
* **awaiting_callback** - helps to manage callbacks instead coroutines, contains method await() which allows to await on an awaiter. When operation is complete, the callback is called
* **prepared_coro** - instance holds a coroutine handle which is ready to run. When instance is destroyed, coroutine is resumed. Movable
* **coro_frame** -helps to mimic coroutine. You can write methods do_resume() and do_destroy() and retrieve coroutine_handle to such class. When handle is used for resumption, do_resume() is called, when for destruction, do_destroy() is called
* **distributor** - broadcast event for multiple awaiting coroutines
* **async_generator** - generator which can use co_await in its body
* **mutex** - mutex for coroutines - can be safely held over co_await
* **queue** - a queue with awaitable push() and pop(). 
* **sync_await** - like co_await, but not in coroutine, performs blocking await on an awaiter or an awaitable
* **scheduler** - schedules executions of coroutines

## awaitable features

### construction

* awaitable<T>() = construct containing value T constructed by default constructor T()
* awaitable<T>(std::nullopt) = construct empty (like std::optional)
* awaitable<T>(std::exception_ptr e) = construct in exception state
* awaitable<T>(&& ... args) = construct T(args...)
* awaitable<T>(lambda(awaitable<T>::result)) = initializes by lambda, which is called at the beginning of await_suspend. The result contains reference to the awaitable and works as callback function. By calling result with a value sets the awaitable result and resume awaiting coroutine
* awaitable<T>(coroutine<T>) = initializes by suspended coroutine, which is resumed after await_suspend

### usage

awaitable<T> awt;

* co_await awt - await on result
* awt.await() - synchronously await
* co_await awt.has_value() - await and test whether result has a value (is not nullopt)
* awt.has_value() - synchronous await and test whether result has a value (is not nullopt)
* is_ready() - determines, whether result is ready
* await_resume() - return result if ready
* set_callback() - associate with callback, the callback is called when operation is complete (callback is allocated at heap), to prevent allocations **awaiting_callback** 
* cancel() - cancels scheduled asynchronous operation (before co_await)

### note

if awaitable is discarded, scheduled asynchronous operation is executed in detached mode.

## prepared_coro usage

The class **prepared_coro** can be used to store coroutine_handle for some time. The object is movable, not copyable. When the object is held, underlying coroutine is on hold. Once the object is destroyed, underlying coroutine is resumed

The class is intended to postpone resumption when operation is complete out of current context. It is often returned from various function. You
can:
  * discard return value, which causes resumption underlying coroutine at place where return value si discrarded
  * pass the return value to upper level, and left resuption on responsible of upper level
  * pass object and execute it in thread pool - to resume coroutine in an assigned thread
  * move and manage objects **prepared_coro** otherways to choose best place for resumption


The class prepared coro can be used to resume coroutine outside of lock

```
void foo() {
    prepared_coro resmp;
    std::lock_guard _(_mutex);
    resmp = bar();
    //unlock
    //resume
}
```

## co_return performing RVO

It is possible to `co_return [&]{ return value; }`. This allows to perform RVO in coroutine, The RVO is performed by return command inside of the lambda. The co_return must return such lambda. You cannot use co_await inside of lambda

The same is applied to result object

```
awaitable<T>::result r = ...
r([&]{return value});   // initialize by lambda function, perform RVO
```

## coroutine ownership

coroutines are not owned, there is no object which owns a coroutine. The coroutine is owned by its code and it is destroyed after co_return. The result is stored in associated awaitable

You can run coroutine in detached mode. Just discard return value. When coroutine is running in detached mode, it is automatically destroyed when
it is finished, its return value is discarded.

## coroutine destruction

It is not recommended to destroy running coroutines. If you apply destroy() on coroutine handle, the coroutine is terminated and associated
awaitable is resolved with nullopt

## async_generator ownership and destruction

The async_generator is owned. The underlying coroutine is controlled by its object. When this object is destroyed, underlying coroutine is terminated.
The only safe place to perform destruction is when underlying coroutine is awaiting on co_yield, otherwise UB.


