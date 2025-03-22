#pragma once
#include <stdexcept>


namespace coro {

///pointer to function, which is called when unhandled exception is caused by a coroutine (or similar function)
/**
 * Called when unhandled_exception() function cannot pass exception object to the result. This can
 * happen when coroutine is started in detached mode and throws an exception.
 *
 * You can change function and implement own version. Returning from the function ignores any futher
 * processing of the exception, so it is valid code to store or log the exception and return to
 * resume normal execution.
 */
inline void (*async_unhandled_exception)() = []{std::terminate();};


///await or co_await function has been canceled
/**
 * The cancelation can be due several reasons. For example, the coroutine has
 * been destroyed, result has not been delivered or attempt to await
 * on non-coroutine object
 *
 */
class await_canceled_exception: public std::exception {
public:
    virtual const char *what() const noexcept override {return "await canceled exception";}
};
    
///object is in invalid state
class invalid_state: public std::exception {
public:
    virtual const char *what() const noexcept override {return "invalid state";}
};
    

}