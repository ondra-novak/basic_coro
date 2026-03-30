#pragma once
#include "prepared_coro.hpp"
#include "concepts.hpp"

namespace coro {



    
///Manually calls await_suspend of the awaiter and returns prepared_coro
/** The function tests variant return types of await_suspend and handles them according to the standard. 
    It also provides static assertion for unsupported return types. 
    
    @tparam _Awt type of the awaiter
    @param awt awaiter object
    @param handle coroutine handle to pass to await_suspend
    @return prepared_coro returned by await_suspend or created according to the return value of await_suspend
    
    if the await_susopend returns void, it returns empty prepared_coro - nothing to be resumed
    if the await_suspend returns bool, it returns empty prepared_coro if the value is true - nothing to be resumed, 
                    otherwise it returns prepared_coro with the handle - the caller should resume the handle
    if the await_suspend returns coroutine_handle, it returns prepared_coro with the returned handle - the caller should resume the handle. 
        This simulates the symmetric transfer of control between coroutines described in the standard.
*/
template<is_awaiter _Awt>
prepared_coro call_await_suspend(_Awt &awt, std::coroutine_handle<> handle) {
    using Ret = decltype(awt.await_suspend(handle));
    static_assert(std::is_void_v<Ret>
                || std::is_convertible_v<Ret, std::coroutine_handle<> >
                || std::is_convertible_v<Ret, bool>);
 
    if constexpr(std::is_convertible_v<Ret, std::coroutine_handle<> >) {
        return prepared_coro(awt.await_suspend(handle));
    } else if constexpr(std::is_convertible_v<Ret, bool>) {
        bool b = awt.await_suspend(handle);
        return b?prepared_coro():prepared_coro(handle);
    } else {
        awt.await_suspend(handle);
        return {};
    }
}
 



///Implements awaiter proxy, which can be used to convert return value to different return value
/**
 * @tparam Awt type of awaiter
 * @tparam Callback type of callback. It must be callable with awaiter as first argument and return value of awaitable<T> type
 *
 * This class is used to convert awaiter to different type. It is used in the following way:
 *
 * @code
 * auto awt = awaitable_function();
 * awaiter_proxy proxy(awt, [](auto &awt) {
 *      return awt.await_resume()*42;
 * });
 * auto res = co_await proxy;
 * @endcode
 */
template<is_awaiter Awt, std::invocable<Awt &> Callback>
class awaiter_proxy {
public:

    awaiter_proxy(Awt &awt, Callback &&cb):_awaiter(awt), _callback(std::forward<Callback>(cb)) {}
    awaiter_proxy( awaiter_proxy &&) = default;

    bool await_ready() const {return _awaiter.await_ready();}
    auto await_suspend(std::coroutine_handle<> h) {
        return _awaiter.await_suspend(h);
    }
    auto await_resume() {
        return _callback(_awaiter);
    }

    ///synchronous get value
    decltype(auto) get() {
        return sync_await(*this);
    }
    ///synchronous get value();
    decltype(auto) operator *() {
        return get();
    }

protected:
    Awt &_awaiter;
    Callback _callback;
};

}