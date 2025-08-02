#pragma once

#include "awaitable.hpp"
#include "defer.hpp"
#include <queue>

namespace coro {


struct reactive_default_policy {
    constexpr bool operator()(const auto &) const {return true;}
};

template<typename T, typename Hasher = std::hash<T> >
struct reactive_hash_compare_policy {
    [[no_unique_address]] Hasher hasher;
    using HashResult = std::invoke_result_t<Hasher, T>;    
    HashResult last = {};
    
    constexpr bool operator()(const T &val) {
        HashResult cur_val = hasher(val);
        bool out = cur_val != last;
        last = cur_val;
        return out;
    }
};



///Declares variable, which can be watched for changes by coroutines
/**
 * @tparam T variable which can be watched
 * @tparam Policy defines how value is compared. Default policy causes that any non-const access is reported as change. You can
 * define differeny policy, for example reactive_hash_compare_policy causes that change is detected by calculating different hash 
 * 
 * @note variable is not thread safe, use propery synchronization
 * 
 * 
 */
template<typename T, typename Policy = reactive_default_policy>
requires(std::is_invocable_r_v<bool, Policy, const T &>)
class reactive {
public: 
    using Result = awaitable<reactive *>::result;

    ///watch variable, returns awaitable
    /**
     * Function block until the variable is changed. It returns following status
     * 
     * @retval true variable changed
     * @retval false variable has been destroyed
     */
    friend awaitable<reactive *> watch(reactive &var) {        
        return [&var](Result res){
            if (!var._state) var._state.reset(new WatchState(&var));
            var._state->reg_watchers.push(std::move(res));
        };
    }


    ///construct reactive variable
    template<typename ... Args>
    requires(std::is_constructible_v<T, Args...>)
    reactive(Args &&... args)
        :_value(std::forward<Args>(args)...) {
            _policy(_value);
    }

    ///create copy - note watchers are not copied
    reactive(const reactive &other):_value(other.value) {
        _policy(_value);
    }
    
    ///assign different value
    /**
     * notifies watchers, if value changed
     */
    reactive &operator=(const reactive &other) {
        if (&other != this) {
            _value = other._value;
            check_change();
        }
        return *this;
    }

    ///move constructor     
    /** When variable is moved, so watchers too */
    reactive(reactive &&other) = default;
    ///move value 
    /**
     * Moves reactive value replacing existing value. This is considered
     * as change of current variable. If moved value has watchers, they are
     * moved to new variable. It is not recommended to merge two variables with watchers.
     * In such case, target variable is considered destroyed
     * 
     */
    reactive &operator=(reactive &&other) {
        if (this != &other) {
            _value = std::move(other._value);
            if (other._state) {
                _state = std::move(other._state);
            } else {
                check_change();
            }
        }
        return *this;
    }

    struct PtrProxy {
        reactive *_owner;
        PtrProxy(reactive *owner):_owner(owner) {};
        ~PtrProxy() {
            _owner->check_change();            
        }
        T *operator->() {return &_owner->_value;}
    };


    ///access to member
    PtrProxy operator->() {return {this};}
    ///access to member
    const T * operator->() const {return &_value;}

    ///access to value
    operator const T &() const {return _value;}



protected:

    struct WatchState {
        std::queue<Result> reg_watchers = {};
        std::size_t pending = 0;
        reactive *owner = nullptr;
        WatchState(reactive *o):owner(o) {}
        void run_defer() {
            defer([this]{
                while (pending) {
                    reg_watchers.front()(owner);
                    reg_watchers.pop();
                    --pending;                    
                }
                if (!owner) {
                    delete this;
                }
            });

        }
    };

    struct WatchStateDeleter {
        void operator()(WatchState *st) {
            st->owner = nullptr;
            if (st->reg_watchers.empty()) {
                delete st;
            } else {
                bool run = st->pending == 0;
                st->pending = st->reg_watchers.size();
                if (run) st->run_defer();
            }
        }
    };

    T _value;
    [[no_unique_address]] Policy _policy;
    std::unique_ptr<WatchState, WatchStateDeleter> _state;
    
    void notify() {
        if (_state &&  _state->reg_watchers.size()) {
            _state->owner = this;
            bool run = _state->pending == 0;
            _state->pending = _state->reg_watchers.size();
            if (run) _state->run_defer();
        }
    }
    void check_change() {
        if (_policy(_value)) notify();
    }
};


}