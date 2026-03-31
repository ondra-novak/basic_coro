#pragma once

#include "awaitable.hpp"
#include <vector>
#include <algorithm>
#include "cancel_signal.hpp"
#include "basic_lockable.hpp"

namespace coro {


template<typename T, basic_lockable Lock = empty_lockable>
class distributor {
public:
    using value_type = voidless_type<T>;

    using awaitable = coro::awaitable<T>;
    using result_object = typename awaitable::result;
    using prepared = std::vector<prepared_coro>;
    using ident = cancel_signal *;

    struct awaiting_info {
        result_object r;
        ident i;
    };

    ///register coroutine to receive broadcast
    /**
     * @return awaitable, which returns reference to broadcasted value.
     * @note the function is MT-Safe if the Lock is std::mutex
     */
    awaitable operator()(ident id = {}) {
        return [this,id](result_object r){
            add_listener(std::move(r), id);
        };
    }


    prepared_coro add_listener(result_object r, cancel_signal *a) {
        lock_guard _(_mx);
        if (a && *a) return r.set_empty();
        _results.push_back({std::move(r), a});
        return {};
    }

    ///broadcast the value
    /**
     * @param buffer (preallocated) buffer to store prepared_coroutines. You
     * need to clear the buffer to resume all these coroutines. You can
     * use thread pool to enqueue coroutines to run
     * @params args arguments need to construct value
     *
     * @note This function is MT-Safe if the Lock is std::mutex
     */
    template<typename ... Args>
    requires(std::is_constructible_v<value_type, Args...>)
    void broadcast(prepared &buffer, Args && ... args) {
        lock_guard _(_mx);
        for (auto &r: _results) {
            buffer.push_back(r.r(args...));
        }
        _results.clear();
    }

    ///broadcast the value and resume awaiting coroutines in current thread
    /**
     * @param v value to broadcast
     * @note This function is not MT-Safe for this function, only one
     * thread can call broadcast() at the same time. For other functions
     * this function is MT-Safe.
     */
    template<typename ... Args>
    requires(std::is_constructible_v<value_type, Args...>)
    void broadcast(Args && ... args) {
        broadcast(_ready_to_run, std::forward<Args>(args)...);
        _ready_to_run.clear();
    }

    prepared_coro cancel(cancel_signal *cancel_signal) {
        prepared_coro out;
        lock_guard _(_mx);
        cancel_signal->request_cancel();
        auto iter = std::find_if(_results.begin(), _results.end(), [&](const awaiting_info &x){
            return x.i == cancel_signal;
        });
        if (iter != _results.end()) {
            out = (iter->r = std::nullopt);

            auto last = _results.end();
            --last;
            if (iter != last) std::swap(*iter, *last);
            _results.pop_back();
        }
        return out;

    }

    bool empty() const {
        lock_guard _(_mx);
        return _results.empty();
    }

protected:
    mutable Lock _mx;
    std::vector<awaiting_info> _results;
    std::vector<prepared_coro> _ready_to_run;

};


}
