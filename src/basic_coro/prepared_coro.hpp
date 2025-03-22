#pragma once
#include <coroutine>
#include <memory>

namespace coro {

class prepared_coro {
public:

    ///construct empty
    prepared_coro() = default;
    ///construct by handle
    prepared_coro(std::coroutine_handle<> h):_coro(h.address()) {}

    ///test if empty
    explicit operator bool() const {return static_cast<bool>(_coro);}

    ///resume
    void resume(){
        if (*this) std::coroutine_handle<>::from_address(_coro.release()).resume();
    }
    ///resume
    void operator()() {
        if (*this) std::coroutine_handle<>::from_address(_coro.release()).resume();
    }
    ///destroy coroutine
    void destroy(){
        if (*this) std::coroutine_handle<>::from_address(_coro.release()).destroy();
    }
    ///release handle and return it for symmetric transfer
    std::coroutine_handle<> symmetric_transfer(){
        if (!_coro) return std::noop_coroutine();
        return std::coroutine_handle<>::from_address(_coro.release());
    }

    std::coroutine_handle<> release() {
        if (*this) return std::coroutine_handle<>::from_address(_coro.release());
        else return {};
    }

protected:
    struct deleter{
        void operator()(void *ptr) {
            std::coroutine_handle<>::from_address(ptr).resume();
        }
    };

    std::unique_ptr<void,deleter> _coro;
};
        
          


}