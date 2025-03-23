#pragma once

#include "awaitable.hpp"
#include "basic_lockable.hpp"


namespace coro {

///limited queue - helper class for coro_basic_queue
/**
 * @tparam T type of item in queue
 * @tparam count max count of items in queue

 */
template<typename T, unsigned int count>
struct limited_queue{
public:

    using value_type = T;

    ///determine whether queue is full
    constexpr bool is_full() const {
        return _front - _back >= count;
    }

    ///determine whether queue is empty
    constexpr bool is_empty() const {
        return _front - _back == 0;
    }

    ///push item
    /**
     * @param args arguments to construct item
     *
     * @note it doesn't check fullness, use is_full() before you call this function
     *
     */
    template<typename ... Args>
    constexpr void push(Args && ... args) {
        item &x = _items[_front % count];
        std::construct_at(&x.val, std::forward<Args>(args)...);
        ++_front;
    }

    ///pop item
    /**
     * @return item removed from queue
     *
     * @note it doesn't check for emptyness, use is_empty() before calling of this function
     */
    constexpr T pop() {
        item &x = _items[_back % count];
        T r = std::move(x.val);
        std::destroy_at(&x.val);
        ++_back;
        return r;
    }


protected:

    struct item {
        T val;
        item() {}
        ~item() {}
    };

    item _items[count];
    unsigned int _front = 0;
    unsigned int _back = 0;
};



///basic coroutine queue
/**
 *
 * @tparam Queue_Impl implementation of the queue = example limited_queue
 * @tparam Lock object to lock internals
 */
template<typename Queue_Impl, basic_lockable Lock = empty_lockable>
class coro_basic_queue {
public:

    using value_type = typename Queue_Impl::value_type;

    ///Push to queue
    /**
     * @param args arguments to construct item
     * @return awaitable (co_await). If there is a space in the queue, the operation finishes
     * immediately and new item is pushed to the queue. If the queue is full,
     * returned object is set to pending state. Push operation continues
     * when someone removes an item from the queue
     *
     * @note if you doesn't co_await on result, the item is pushed only if it
     * can be pushed without blocking. You can check this state by testing
     * is_ready()
     *
     */
    template<typename ... Args >
    requires(std::is_constructible_v<value_type, Args...>)
    awaitable<void> push(Args && ... args) {
        prepared_coro resm;
        lock_guard _(_mx);
        if (_queue.is_full()) {
            return push_async_cb(this, std::forward<Args>(args)...);
        } else {
            resm = push2(std::forward<Args>(args)...);
            return {};
        }
    }

    ///pop from queue
    /**
     * @return awaitable object which eventually receives the item. You
     * need co_await on result.
     */
    awaitable<value_type> pop() {
        prepared_coro resm;
        lock_guard _(_mx);
        if (_queue.is_empty()) {
            return pop_async_cb(this);
        } else {
            return pop2(resm);
        }
    }


    ///clear whole queue. The function also resumes all stuck producers
    void clear() {
        while (pop().is_ready());
    }

    ///closes the queue
    /**
     * When queue is closed, coroutines can't co_await on the queue. Any
     * attempt of co_await is reported as nullopt value, which leads
     * to the execption await_canceled_exception, if not tested
     */
    void close() {
        slot<typename awaitable<value_type>::result> *slots;
        {
            lock_guard _(_mx);
            _closed = true;
            slots = _pop_queue.first;
            _pop_queue.first = _pop_queue.last = nullptr;
        }
        while (slots) {
            auto s = slots;
            slots = s->next;
            s->payload = std::nullopt;
        }
    }



protected:

    template<typename _Payload>
    struct slot {
        _Payload payload = {};
        slot *next = nullptr;

        slot(_Payload p):payload(std::move(p)) {}

        template<typename ... Args>
        requires(std::is_constructible_v<_Payload, Args...>)
        slot(Args && ... args):payload(std::forward<Args>(args)...) {}
    };


    struct push_async_payload {
        union {
            awaitable<void> *r;
            coro_basic_queue *q;
        };
        value_type val;

        template<typename ... Args>
        requires(std::is_constructible_v<value_type, Args...>)
        push_async_payload(coro_basic_queue *me, Args && ... args):q(me),val(std::forward<Args>(args)...) {}
    };

    struct push_async_cb : slot<push_async_payload> {
        template<typename ... Args>
        requires(std::is_constructible_v<value_type, Args...>)
        push_async_cb (coro_basic_queue *me, Args && ... args):slot<push_async_payload>(me, std::forward<Args>(args)...) {}
        prepared_coro operator()(awaitable<void>::result r) {
            if (!r) return {};
            coro_basic_queue *me = this->payload.q;
            lock_guard _(me->_mx);
            if (me->_queue.is_full()) {
                this->payload.r = r.release();
                me->_push_queue.push(this);
                return {};
            } else {
                return me->push2(std::move(this->payload.val));
            }
        }    
    };

    friend struct push_async_cb;

    struct pop_async_cb: slot<typename awaitable<value_type>::result> {
        coro_basic_queue *me;
        pop_async_cb(coro_basic_queue *me):me(me) {}
        prepared_coro operator() (typename awaitable<value_type>::result r) {
            prepared_coro resm;
            lock_guard _(me->_mx);
            if (me->_queue.is_empty()) {
                if (!r) return {};
                if (me->_closed) {                    
                    return (r =  std::nullopt);
                }

                this->payload = std::move(r);
                me->_pop_queue.push(this);
                return {};
            } else {
                return r(me->pop2(resm).await());
            }
        }
    };


    awaitable<value_type> pop2(prepared_coro &resm) {
        awaitable<value_type> r ( _queue.pop());
        slot<push_async_payload> *s = _push_queue.pop();
        if (s) {
            _queue.push(std::move(s->payload.val));
            awaitable<void>::result r2( s->payload.r);
            resm = r2();
        }
        return r;
    }
    template<typename ... Args>
    prepared_coro push2(Args && ... args) {
         if (_queue.is_empty()) {
            auto slot = _pop_queue.pop();
            if (slot) {
                return slot->payload(std::forward<Args>(args)...);
            } else {
                _queue.push(std::forward<Args>(args)...);
            }
        } else {
            _queue.push(std::forward<Args>(args)...);
        }
        return {};
    }
    

    template<typename X>
    struct link_list_queue {
        slot<X> *first = {};
        slot<X> *last = {};

        void push(slot<X> *s) {
            if (last) {
                last->next = s;
                last = s;
            } else {
                first = last = s;
            }
        }

        slot<X> *pop() {
            auto r = first;
            if (r == last) {
                last = first = nullptr;
            } else {
                first = first->next;
            }
            return r;
        }

    };

    Lock _mx;
    Queue_Impl _queue;
    link_list_queue<typename awaitable<value_type>::result> _pop_queue;
    link_list_queue<push_async_payload> _push_queue;
    bool _closed = false;


};


template<typename T, unsigned int count, typename Lock = empty_lockable>
class coro_queue : public coro_basic_queue<limited_queue<T, count>, Lock > {};

}
