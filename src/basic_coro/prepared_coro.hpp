#pragma once
#include <memory>
#include <coroutine>
#include "co_switch.hpp"


namespace coro {

///contains coroutine prepared to be resumed
/**
 * This class holds a single coroutine handle, which is ready to be resumed
 * You can postpone resumption during lifetime of this object. When
 * object is destroyed, the coroutine is resumed. You can resume coroutine manually
 * or you can return value suitable for symmetric transfer
 *
 * You can also create a coroutine which returns prepared_coro.
 *
 * @code
 * prepared_coro a_coroutine() noexcept {
 *      co_await ...
 *      co_return ;
 * }
 * @endcode
 *
 * This special usage allows to create prepared_coro handle and execute it
 * as standard detached coroutine.
 *
 */
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

    ///resume lazily
    /**
     * Ensures that resume will not create additional stack frame.
     * Resume is postponed until code reaches initial stack level
     * @note this function is effective only if another lazy_resume
     * is used during pefroming first lazy_resume (in recursion)
     */
    void lazy_resume() {
        co_switch::lazy_resume(release());
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

    struct promise_type {
        static constexpr std::suspend_always initial_suspend() noexcept {return {};}
        static constexpr std::suspend_never final_suspend() noexcept {return {};}
        prepared_coro get_return_object() noexcept {
            return prepared_coro(std::coroutine_handle<promise_type>::from_promise(*this));}
        void return_void() noexcept {}
        void unhandled_exception() noexcept {std::terminate();}
    };


protected:
    struct deleter{
        void operator()(void *ptr) noexcept {
            std::coroutine_handle<>::from_address(ptr).resume();
        }
    };

    std::coroutine_handle<> _h;
};


///Allows to hold multiple prepared coroutines
/**
 * @tparam N maximum number of prepared coroutines
 */
template<std::size_t N>
class prepared_coros {
public:
    ///construct empty
    prepared_coros():_cnt(0) {}
    ///destroy
    ~prepared_coros() {
        clear();
    }
    ///construct from existing prepared coro
    prepared_coros(prepared_coro &x) {
        add(x);
    }
    ///construct from existing prepared coro
    prepared_coros(prepared_coro &&x) {
        add(x);
    }
    ///construct from list of M
    /**
     * @param lst array of coroutines
     */
    template<std::size_t M>
    requires(M <= N)
    prepared_coros(prepared_coro (&lst)[M]):_cnt(0) {
        for (auto &x : lst) add(x);
    }

    template<std::size_t M>
    requires(M <= N)
    prepared_coros(prepared_coros<M> &&lst):_cnt(0) {
        for (auto &x : lst) add(x);
    }

    template<std::size_t M>
    requires(M <= N)
    prepared_coros(prepared_coros<M> &lst):_cnt(0) {
        for (auto &x : lst) add(x);
    }

    template<std::size_t M1, std::size_t M2>
    requires(M1+M2 <= N)
    prepared_coros(prepared_coros<M1> &&lst1, prepared_coros<M2> &&lst2):_cnt(0) {
        for (auto &x : lst1) add(x);
        for (auto &x : lst2) add(x);
    }


    prepared_coros(prepared_coros &&other):_cnt(0) {
        for (auto &x: other) add(x);
    }

    prepared_coros &operator=(prepared_coros &&) = delete;


    prepared_coro *begin() {return _coros;}
    prepared_coro *end() {return _coros+_cnt;}

    void add(prepared_coro &x) {
        if (_cnt >= N) throw std::invalid_argument("Too many coroutines");
        std::construct_at(&_coros+_cnt, std::move(x));
        ++_cnt;
    }
    void add(prepared_coro &&x) {
        add(x);
    }

    void clear() {
        for (auto &x: *this) std::destroy_at(&x);
        _cnt = 0;
    }

    void resume() {
        clear();
    }
    void lazy_resume() {
        for (auto &x: *this) {
            x.lazy_resume();
            std::destroy_at(&x);
        }
        _cnt = 0;
    }
    void operator()() {
        resume();
    }

protected:
    union {
        prepared_coro _coros[N];
    };
    std::size_t _cnt;
};

template<std::size_t N>
prepared_coros(prepared_coro (&)[N]) -> prepared_coros<N>;

template<std::size_t M1, std::size_t M2>
prepared_coros(prepared_coros<M1> &&, prepared_coros<M2> &&) -> prepared_coros<M1+M2>;


///converts multiple prepared_coro object into single prepared_coro object
/**
 * This function is implemented as a coroutine.
 * @param list of prepared coros
 * @return aggregated prepared coro
 */
template<std::same_as<prepared_coro> ... Args>
prepared_coro aggregate_prepared_coros(Args ...  ) noexcept {
    //the function contains only co_return. All arguments are destroyed at the end of
    //the coroutine, which causes resumption
    co_return;
}


}
