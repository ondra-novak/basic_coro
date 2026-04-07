#pragma once

#include "awaitable.hpp"
#include "basic_coro/coro_frame.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "cancel_signal.hpp"

#include <algorithm>
#include <mutex>
#include <optional>
#include <condition_variable>
#include <queue>
#include <stop_token>
#include <vector>
#include <thread>
namespace coro {


template<typename T, typename _TP, typename _Ident = const void *>
class generic_scheduler {
public:

    void schedule_at(T x, _TP timestamp, _Ident ident) {
        _heap.push_back({timestamp, std::move(x), ident});
        std::push_heap(_heap.begin(), _heap.end(), compare);
    }

    std::optional<_TP> get_first_scheduled_time() const {
        if (_heap.empty()) return {};
        return _heap.front().timestamp;
    }

    T remove_first() {
        T out;
        if (!_heap.empty()) {
            std::pop_heap(_heap.begin(), _heap.end(), compare);
            out = std::move(_heap.back().res);
            _heap.pop_back();
        }
        return out;
    }
    T remove_by_ident(_Ident ident) {
        T r;
        for (std::size_t i = 0, cnt = _heap.size();i<cnt; ++i) {
            if (_heap[i].ident == ident) {
                r = std::move(_heap[i].res);
                if (i == 0) {
                    r = remove_first();
                } else if (i == cnt-1) {
                    _heap.pop_back();
                } else {
                    update_heap_element(i, std::move(_heap.back()));
                    _heap.pop_back();
                }
                break;
            }
        }
        return r;
    }

    ///set task time, update its position in the heap
    bool set_time(_Ident ident, _TP new_tp) {
        bool ok = false;
        for (std::size_t i = 0, cnt = _heap.size();i<cnt; ++i) {
            if (_heap[i].ident == ident) {
                auto r=std::move(_heap[i].res);
                update_heap_element(i, {new_tp, std::move(r), ident});
                ok = true;
            }
        }
        return ok;
    }

    bool empty() const {
        return _heap.empty();
    }

protected:

    struct HeapItem {
        _TP timestamp;
        T res;
        _Ident ident;
    };

    static bool compare(const HeapItem &a,const HeapItem &b) {
        return a.timestamp > b.timestamp;
    }

    std::vector<HeapItem> _heap;


    void update_heap_element(std::size_t pos, HeapItem &&new_value) {
        bool shift_up = compare(_heap[pos], new_value);
        _heap[pos] = std::move(new_value);

        if (shift_up) {
            while (pos > 0) {
                size_t parent = (pos - 1) / 2;
                if (compare(_heap[parent], _heap[pos])) {
                    std::swap(_heap[parent], _heap[pos]);
                    pos = parent;
                } else {
                    break;
                }
            }
        }
        else {
            size_t n = _heap.size();
            while (true) {
                size_t left = 2 * pos + 1;
                size_t right = 2 * pos + 2;
                size_t largest = pos;

                if (left < n && compare(_heap[largest], _heap[left])) {
                    largest = left;
                }
                if (right < n && compare(_heap[largest], _heap[right])) {
                    largest = right;
                }
                if (largest != pos) {
                    std::swap(_heap[pos], _heap[largest]);
                    pos = largest;
                } else {
                    break;
                }
            }
        }
    }
};


///manual scheduler
/** manually advances time, and executes scheduled coroutines.
 *  This scheduler is useful for testing, or for simmulation purposes
 *
 * @note no lock is used in this scheduler, so it is not thread safe.
 *
 * @tparam _TP time point type. Must support addition with duration and comparison operators
 * @tparam result_object type of the result object returned by the scheduler    
 */
template<typename _TP = std::chrono::system_clock::time_point>
class manual_scheduler {
public:

    using result_object = typename awaitable<bool>::result;

    awaitable<bool> sleep_until(_TP tp, cancel_signal *cflag = nullptr) {
        return [this,tp=std::move(tp),cflag](result_object r) mutable -> prepared_coro {
            if (cflag && *cflag) return r(false);
            _sch.schedule_at(std::move(r), std::move(tp), cflag);
            return prepared_coro();
        };
    }

    template<typename Dur>
    requires(requires(_TP tp, Dur dur){{tp+dur}->std::convertible_to<_TP>;})
    awaitable<bool> sleep_for(Dur dur, cancel_signal *csignal = nullptr) {
        return sleep_until(get_current_time()+dur, csignal);
    }
    ///retrive first scheduled time
    std::optional<std::chrono::system_clock::time_point> get_first_scheduled_time() const {
        return _sch.get_first_scheduled_time();
    }
    ///remove first scheduled coroutine
    /**
     * @return result object of this coroutine, or empty if none
     */
    result_object remove_first() {
        return _sch.remove_first();
    }
    
    ///cancel sleep and wake up coroutine by pointer to cancel signal
     /**
      * @param cflag pointer to cancel signal. If this pointer is null, nothing is canceled. 
           If this pointer is not null, the function sets this flag to true and wakes up sleeping coroutine 
           which has this pointer as identity (if there is such coroutine). If there is no such sleeping coroutine, nothing happens.
      * @return result object of canceled coroutine, or empty if nothing has been canceled.
         You need to call result object with result value to resume coroutine.
      */
    prepared_coro cancel(cancel_signal *cflag) {
        if (!cflag) return {};
        cflag->request_cancel();
        return _sch.remove_by_ident(cflag)(false);
     }

     ///retrieves current time
     /**
      * @return current simmulation time
      */
     _TP get_current_time() const {return _current_time;}

     ///advances time until givne time point
     /** If there is scheduled coroutine, it removes it and returns it
      * to be resumed. The current time is set to time where coroutine was scheduled
      * If there is no coroutine before given target_time, returns empty object
      * @return result object of the first scheduled coroutine, or empty if there is no scheduled coroutine before target_time.
      * You need to call result object with result value to resume coroutine. 
      */
     prepared_coro advance_time_until(_TP target_time) {
        auto n = _sch.get_first_scheduled_time();
        if (!n || *n>target_time) {
            _current_time = target_time;
            return {};
        }
        _current_time = std::max(target_time,*n);
        result_object r = _sch.remove_first();
        return r(true);
     }

protected:
    _TP _current_time = {};
    generic_scheduler<result_object, std::chrono::system_clock::time_point, cancel_signal *> _sch;
};


///scheduler with thread and real time.
/**
    co_await operation on the scheduler return true if the sleep was waken up by timeout, and false if sleep was interrupted.
    You can also cancel sleep by identity, or send alert to alertable sleeping coroutine.
*/
class scheduler {
public:

    using result_object = typename awaitable<bool>::result;

    ///sleep until given time point, with optional cancel signal
     /**
      * @param tp time point to sleep until
      * @param cflag optional pointer to cancel signal. True value of this flag means that sleep was canceled. 
            This solves race condition between checking cancel signal and going to sleep. 
            If this pointer is null, sleep cannot be canceled.
        @return awaitable object, which can be co_awaited. When co_awaited, it returns true if sleep was waken up by timeout,
            and false if sleep was interrupted by cancel signal.
      */
    awaitable<bool> sleep_until(std::chrono::system_clock::time_point tp, cancel_signal *cflag = nullptr) {
        return [this,tp=std::move(tp),cflag](result_object r) mutable -> prepared_coro {
            std::scoped_lock _(_mx);            
            if (cflag && *cflag) return r(false);
            _sch.schedule_at(std::move(r), std::move(tp), cflag);
            _cv.notify_one();   
            return prepared_coro();
        };
    }

    ///sleep for given duration, with optional cancel signal
     /**
      * @param dur duration to sleep
      * @param cflag optional pointer to cancel signal. True value of this flag means that sleep was canceled. 
            This solves race condition between checking cancel signal and going to sleep. 
            If this pointer is null, sleep cannot be canceled.
        @return awaitable object, which can be co_awaited. When co_awaited, it returns true if sleep was waken up by timeout,
            and false if sleep was interrupted by cancel signal.
      */
    template<typename A, typename B>
    awaitable<bool> sleep_for(std::chrono::duration<A, B> dur, cancel_signal *cflag = nullptr) {
        return sleep_until(std::chrono::system_clock::now() + dur, cflag);
    }   

    ///run thread and execute scheduled coroutines in this thread
     /**
      * @param executor executor to execute scheduled coroutines. Executor is a callable object, 
           which takes result object of scheduled coroutine as an argument and executes it.  It allows
           to forward execution of scheduled coroutine to other thread, or to execute it in the same thread.
           If you want to execute scheduled coroutines in the same thread, you can use run_thread() without arguments, 
           which is equivalent to run_thread([](auto &&){})
      * @param tkn stop token to stop thread. When stop is requested, thread is stopped and all scheduled coroutines are not executed. 
      */
    template<std::invocable<prepared_coro> Executor>
    void run_thread(Executor &&executor, std::stop_token tkn) {
        std::stop_callback __(tkn,[this]{
            _cv.notify_all();
        });
        while (!tkn.stop_requested()) {
            std::unique_lock lk(_mx);
            auto tm = _sch.get_first_scheduled_time();
            if (tm) {
                auto now =std::chrono::system_clock::now();
                if (now > *tm) {
                    auto r = _sch.remove_first();
                    lk.unlock();
                    executor(r(true));
                } else{
                    _cv.wait_until(lk, *tm);
                }
            } else {
                _cv.wait(lk);
            }
        }
    }
    ///run thread and execute scheduled coroutines in this thread
     /**
      * @param executor executor to execute scheduled coroutines. Executor is a callable object, 
           which takes result object of scheduled coroutine as an argument and executes it.  It allows
           to forward execution of scheduled coroutine to other thread, or to execute it in the same thread.
           If you want to execute scheduled coroutines in the same thread, you can use run_thread() without arguments, 
           which is equivalent to run_thread([](auto &&){})
      */
    void run_thread(std::stop_token tkn) {
        run_thread([](auto &&){}, std::move(tkn));
    }

    ///run scheduler in current thread until specified awaiter is ready
     /**
      * @param awt awaitable object to wait for. Scheduler runs in current thread until this object is ready. 
      * @return result of awaiting this object
      * emulates co_await outside of coroutine allowing to postpone execution of current code 
      *  until some condition is satisfied. This function is intended to be used in non-coroutine code,
      *  but it can also be used in coroutine code, allowing to wait for some condition without blocking the thread 
      *  and without using additional coroutines.
      */
    template<is_awaiter Awt>
    auto await(Awt &&awt) {

        if (!awt.await_ready()) {
            std::stop_source stpsrc;
            coro_frame_cb frm([&]{stpsrc.request_stop();});
            call_await_suspend(awt, frm.create_handle());
            run_thread(stpsrc.get_token());
        }
        return awt.await_resume();
    }

    ///await operation with custom co_await operator
    template<has_co_await Awt>
    auto await(Awt &&awt) {
        return await(awt.operator co_await());
    }
    ///await operation with custom co_await operator
    template<has_global_co_await Awt>
    auto await(Awt &&awt) {
        return await(operator co_await(awt));
    }


    ///create thread and run scheduler
    /**
     * @param executor executor (see run_thread)
     * @return running thread. Ensure that you destroy thread before destuction of scheduler
     */
    template<std::invocable<result_object> Executor>
    std::jthread create_thread(Executor executor) {
        return std::jthread([this,executor = std::move(executor)]
                             (std::stop_token tkn)mutable{
            run_thread(std::move(executor), std::move(tkn));
        });
    }
    ///create thread and run scheduler
    /**
     * @return running thread. Ensure that you destroy thread before destuction of scheduler
     */
    std::jthread create_thread() {
        return std::jthread([this](std::stop_token tkn)mutable{
            run_thread(std::move(tkn));
        });
    }


    prepared_coro cancel(cancel_signal *cancel_signal) {
        if (cancel_signal) {
            std::scoped_lock _(_mx);
            cancel_signal->request_cancel();
            auto r = _sch.remove_by_ident(cancel_signal);
            return r(false);            
        } else {
            return prepared_coro();
        }
    }

protected:
    mutable std::mutex _mx;
    std::condition_variable _cv;
    generic_scheduler<result_object, std::chrono::system_clock::time_point,cancel_signal *> _sch;
};


}
