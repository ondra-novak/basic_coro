#pragma once
#include "prepared_coro.hpp"
#include "concepts.hpp"

namespace coro {



/// perform call await_suspend and switch to different coroutine manually
/** 
 * @param awt awaiter, must be direct awaiter (operator co_await is not supported)
 * @param h coroutine handle, which is suspended on the awaiter. The coroutine must be already
 * in suspended state. The function can't suspend coroutine (this can handle only co_await), but
 * it can associate the coroutine with the awaiter
 * @return prepared_coro which should be resumed by caller or anytime later. It can
 * also contain argument h, if the suspend has been rejected and the coroutine
 * must be resumed immediately
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
         return {};
     }
 }
 


}