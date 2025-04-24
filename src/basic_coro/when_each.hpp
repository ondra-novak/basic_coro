#pragma once
#include "concepts.hpp"
#include "coro_frame.hpp"


namespace coro {

    ///Wait and iterate over completed results
/**
 * @tparam count count of awaited objects, This value is often deduced
 */
template<unsigned int count>
class when_each {
public:

    ///constructs from an array of awaitable objects
    /**
     * @param awts array of awaitable object
     *
     * @note when constructor finishes, the awaitable objects are
     * evaluated and eventually await_suspend is called if the
     * awaitable is not ready yet. This can cause that
     * evaluation can run on background (in different thread)
     */
    template<is_awaiter Awt>
    when_each(Awt (&awts)[count]):_cnt(count){
        for (std::size_t i = 0; i < count; ++i) {
            add(awts[i], i);
        }
    }
    ///constructs from multiple arguments
    /**
     * @param awts list of awaitable/awaiters, each can be different type
     *
     * @note when constructor finishes, the awaitable objects are
     * evaluated and eventually await_suspend is called if the
     * awaitable is not ready yet. This can cause that
     * evaluation can run on background (in different thread)
     */
    template<is_awaiter ... Awts>
    requires(sizeof...(Awts) <= count)
    when_each(Awts &... awts):_cnt(sizeof...(Awts)) {
        std::size_t idx = 0;
        (add(awts, idx++),...);
    }

    ///construct from the list
    /** This constructor cannot use deduction guide. You need to
     * specify count above expected size of the list. The actual
     * list can be smaller, but not larger than count
     * @param list object which is iteratable by ranged-for
     */
    template<range_for_iterable X>
    when_each(X &list) {
        std::size_t idx = 0;
        for (auto &x: list) {
            add(x, idx++);
            if (idx == count) break;
        }
        _cnt = idx;
    }

    ///cannot copy
    when_each(const when_each &) = delete;
    ///cannot copy
    when_each &operator=(const when_each &) = delete;

    ///destructor ensures that all awaitables are serialized (join)
    /** @note destructor can't use co_await, it joins synchronously
     */
    ~when_each() {
        while (_nx < _cnt) wait();
    }

    bool await_ready() const {
        return _nx >= _cnt || _finished[_nx].load(std::memory_order_relaxed) != 0;
    }

    unsigned int await_resume() {
        if (_nx >= _cnt) return _nx;
        unsigned int r = _finished[_nx].load(std::memory_order_acquire);
        ++_nx;
        return r - 2;
    }

    bool await_suspend(std::coroutine_handle<> h) {
        _r = h;
        unsigned int need = 0;
        return _finished[_nx].compare_exchange_strong(need, 1, std::memory_order_relaxed);
    }

    ///Wait synchronously
    /**
     Useful to wait in non-coroutine function
     @return index of complete awaitable
     */
    unsigned int wait() {
        return sync_await(*this);
    }

    ///determines, whether there are still pending awaitables
    /**
     * @retval true still pending
     * @retval false no more pending, you can destroy this object
     */
    explicit operator bool() const{
        return _nx < _cnt;
    }



protected:


    ///contains fake-coroutine which is called when real coroutine would be resumed
    struct Slot: coro_frame<Slot> { // @suppress("Miss copy constructor or assignment operator")
        when_each *_parent;

        prepared_coro do_resume() {
            return _parent->resumed(this);
        }
    };

    ///list of prepared fake-coroutines to catch resume attempt
    Slot _slots[count];
    ///contains indexes of complete awaitables
    /**
     * The actual value is not index directly, value is increased by 2
     * - value 0 - not complete yet
     * - value 1 - not complete yet but awaitin
     * - other - index of complete + 2
     */
    std::atomic<unsigned int> _finished[count] ={};
    ///contains index of free slot
    std::atomic<unsigned int> _free_slot = {};
    ///contains index of next tested slot
    unsigned int _nx = 0;
    ///contains count of slots
    unsigned int _cnt = 0;
    prepared_coro _r = {};

    ///register to slot
    /**
     * @param awt awaitable
     * @param idx index of slot
     * @return prepared coroutine if resume happens
     */
    template<is_awaiter Awt>
    prepared_coro add(Awt &awt, std::size_t idx) {

        //activate slot
        _slots[idx]._parent = this;
        //test whether awaiter is ready
        if (awt.await_ready()) {
            //if ready, we already resumed
            resumed(&_slots[idx]);
            //nothing to resume
            return {};
        } else {
            //if not - depend of type await_suspend
            return call_await_suspend(awt, _slots[idx].create_handle());            
        }
    }

    ///called when slot is resumed
    /**
     * @param nd pointer to slot
     * @return prepared coroutine if resumption happened
     */
    prepared_coro resumed(Slot *nd) {
        //calculate index
        unsigned int idx = static_cast<unsigned int>(nd - _slots);
        //calculate value
        unsigned int v = idx + 2;
        //retrieve next result slot
        unsigned int wridx = _free_slot.fetch_add(1, std::memory_order_relaxed);
        //exchange value
        unsigned int st = _finished[wridx].exchange(v, std::memory_order_release);
        //if there is 1, somebody already awaiting
        return (st == 1)?std::move(_r):prepared_coro();
    }
};


template<typename Awt, unsigned int N>
when_each(Awt (&)[N]) -> when_each<N>;

template<is_awaiter... Awts>
when_each(Awts&...) -> when_each<sizeof...(Awts)>;


}