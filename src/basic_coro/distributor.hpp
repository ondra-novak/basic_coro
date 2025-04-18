#pragma once

#include "awaitable.hpp"
#include <vector>
#include <algorithm>
#include "alert_flag.hpp"
#include "basic_lockable.hpp"

namespace coro {


template<typename T, basic_lockable Lock = empty_lockable>
class distributor {
public:
    using value_type = voidless_type<T>;

    using awaitable = coro::awaitable<T>;
    using result_object = typename awaitable::result;
    using prepared = std::vector<prepared_coro>;
    using ident = const void *;

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

    ///register coroutine to receive broadcast with alert support
    /**
     * @param alert_flag reference to alert flag. This must be set to false to register. If
     * it is set to true, the co_await immediately returns
     *
     * @see alert
     */
    awaitable operator()(alert_flag &aflag) {
        return [this,&aflag](result_object r){
            return add_listener(aflag, std::move(r));
        };
    }

    void add_listener(result_object r, ident id = {}) {
        lock_guard _(_mx);
        _results.push_back({std::move(r), id});

    }
    prepared_coro add_listener(alert_flag &a, result_object r) {
        lock_guard _(_mx);
        if (a) return r.set_empty();
        _results.push_back({std::move(r), &a});
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

    ///kicks out awaiting coroutine
    /**
     * @param id identification of coroutine (used during registration). If the id
     * is not unique, a random coroutine with equal id is kicked out - but only one (not recommended)
     * @param resolver function receives result object. It should set result to wake up the coroutine
     *  the function should return prepared_coro, which is returned from the function.
     * @return Returns prepared_coro instance of kicked out coroutine. If there is no such
     * coroutine, empty is returned. The result also depends on return value of the resolver
     */
    template<std::invocable<result_object> Resolver>
    prepared_coro kick_out(ident id, Resolver &&resolver) {
        lock_guard _(_mx);
        prepared_coro out;
        auto iter = std::find_if(_results.begin(), _results.end(), [&](const awaiting_info &x){
            return x.i == id;
        });
        if (iter != _results.end()) {
            if constexpr(std::is_void_v<std::invoke_result_t<Resolver, result_object> >) {
                resolver(std::move(iter->r));
            } else {
                out = resolver(std::move(iter->r));
            }
            auto last = _results.end();
            --last;
            if (iter != last) std::swap(*iter, *last);
            _results.pop_back();
        }
        return out;
    }

    ///kicks out awaiting coroutine sending out an exception
    /**
     * @param id identification
     * @param e exception
     * @return prepared coroutine for resumption or empty, if none
     */
    prepared_coro kick_out(ident id, std::exception_ptr e) {
        return kick_out(id, [e = std::move(e)](result_object obj) mutable {
            return obj.set_exception(std::move(e));
        });
    }

    ///kicks out awaiting coroutine setting result to "no-value"
    /**
     * @param id identification
     * @return prepared coroutine
     */
    prepared_coro kick_out(ident id) {
        return kick_out(id, [](result_object obj) mutable {
            return obj = std::nullopt;
        });
    }

    ///send alert to prevent a coroutine to receive broadcast
    /**
     * @param alert_flag shared flag. This flag is set to true to prevent registration
     * @param id ident of coroutine, it causes that if coroutine is already registered, it
     * is removed
     * @return prepared coro object. It is filled when registered coroutine was removed. You
     * need to resume it (just drop the object) to give coroutine chance to process
     * this situation. The coroutine can check shared flag.
     *
     * @note the co_await always throws exception await_canceled_exception.
     */
    prepared_coro alert(alert_flag &alert_flag) {
        prepared_coro out;
        lock_guard _(_mx);
        alert_flag.set();
        auto iter = std::find_if(_results.begin(), _results.end(), [&](const awaiting_info &x){
            return x.i == &alert_flag;
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
