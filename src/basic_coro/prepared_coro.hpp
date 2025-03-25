#pragma once
#include <memory>
#include <coroutine>


namespace coro {

class prepared_coro {
public:

    ///construct empty
    prepared_coro() = default;
    ///construct by handle
    prepared_coro(std::coroutine_handle<> h):_h(h) {}
    //can be moved
    prepared_coro(prepared_coro &&other):_h(other._h) {other._h = {};}
    //creates group of prepared coroutines

    ~prepared_coro() {
        if (_h) _h.resume();
    }

    prepared_coro &operator=(prepared_coro &&other) {
        if (this != &other) {
            auto h = _h;
            _h = other._h;
            other._h = {};
            if (h) h.resume();
        }
        return *this;
    }

    ///test if empty
    explicit operator bool() const {return static_cast<bool>(_h);}

    std::coroutine_handle<> release() {
        std::coroutine_handle<> h = _h;
        _h = {};
        return h;
    }



    ///resume
    void resume(){
        auto h = release();
        if (h) h.resume();
    }
    ///resume
    void operator()() {
        auto h = release();
        if (h) h.resume();
    }
    ///destroy coroutine
    void destroy(){
        auto h = release();
        if (h) h.destroy();
    }
    ///release handle and return it for symmetric transfer
    std::coroutine_handle<> symmetric_transfer(){
        auto h = release();
        if (h) return h; else return std::noop_coroutine();
    }


protected:
    struct deleter{
        void operator()(void *ptr) {
            std::coroutine_handle<>::from_address(ptr).resume();
        }
    };

    std::coroutine_handle<> _h;

 
};
        
          


}