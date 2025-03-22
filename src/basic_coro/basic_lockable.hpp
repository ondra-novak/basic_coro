#pragma once
#include "concepts.hpp"

namespace coro {

template<typename T>
concept basic_lockable = requires(T v) {
    {v.lock()};
    {v.unlock()};
};

class empty_lockable {
public:
    void lock() {};
    void unlock() {};
    bool try_lock() {return true;}
};

template<basic_lockable _LK>
class lock_guard {
public:
    lock_guard(_LK &lk):_lk(lk) {_lk.lock();}
    ~lock_guard() {_lk.unlock();}
    lock_guard(const lock_guard &) = delete;
    lock_guard &operator=(const lock_guard &) = delete;

protected:
    _LK &_lk;

};

}