#pragma once

#include "basic_coro/awaitable.hpp"
#include "basic_coro/concepts.hpp"
#include "basic_coro/pending.hpp"
#include "basic_coro/prepared_coro.hpp"
#include "basic_coro/result_proxy.hpp"
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>

namespace coro {

///implements dispatching thread
/**
 The dispatching thread enables to run and resume coroutines in the
 same thread. When coroutine is resume, it can be resumed in dispatch thread
 in which has been created. 

 To create new dispatch_thread, you need to create it with function create();
 The instance is managed by shared_ptr. It keeps instance alive while there
 is at least one reference, or when there are task in the queue. 
*/
class dispatch_thread:  public std::enable_shared_from_this<dispatch_thread> {
public:

    ///enqueue a coroutine for execution
    void enqueue(prepared_coro coro) {
        bool empty = false;
        {
            std::scoped_lock _(_mx);
            empty = _queue.empty();
            if (empty) _instance_lock = shared_from_this();
            _queue.push(std::move(coro));
        }
        if (empty) _cv.notify_one();        
    }
    
    ///Launch asynchronous operation in dispatcher thread
    /**
      This function is intended for coroutines, which are started in current thread. If you need
       to start the coroutine in disptacher thread. 
       @param awt awaiter / coroutine object which is would be otherwise co_awaited 
       @return pending object contains pending operation, which must be finally co_waited to join/synchronize
     */
    template<is_awaiter Awt>
    pending<Awt> launch(Awt awt) {
        return pending<Awt>(std::move(awt), [this](prepared_coro coro){
            enqueue(std::move(coro));
        });
    }

    ///destructor - safely ends thread even if the thread itself is currently doing destruction
    /**
        by default destructor is called only when there are no references and no tasks in the queue.
        The destructor can be called by the thread itself, and in this case, the thread is detached. 
        In other cases, the destructor is called by other thread, and its thread is joined.
     */
    ~dispatch_thread() {
        _thr.request_stop();
        if (_thr.get_id() == std::this_thread::get_id()) {
            _thr.detach();
        } else {
            _thr.join();
        }
        _join();
    }

    ///create new dispatch thread
     /**
      * @return shared pointer to created dispatch thread. The thread is kept alive while there is at least one reference,
         or when there are task in the queue. 
      */
     static std::shared_ptr<dispatch_thread> create() {
        auto p = std::make_shared<dispatch_thread>();
        p->start();
        return p;
    }

    static std::shared_ptr<dispatch_thread> current() {
        return cur_instance.lock();
    }

    ///join current thread
    /**
      blocks execution (co_await blocking) until current thread is finished. 
      @param ptr shared pointer moved into call, it is destroyed during process to
      ensure that this reference is reset. You need to destroy all other reference to
      successfuly join

      @return awaitable object. coroutine is resumed when dispatcher thread is finished. 
      @note the coroutine is resumed in thread which calls the destructor. This also can be
      the dispatcher's thread itself. Do not perform blocking operations in the coroutine

      @note there can be only one awaitable at time
     */
    static awaitable<void> join(std::shared_ptr<dispatch_thread> &&ptr) {
        return [ptr = std::move(ptr)](awaitable_result<void> p)mutable  {            
            if (ptr) {
                std::scoped_lock _(ptr->_mx);
                ptr->_join = std::move(p);
            } else {
                p();
            }
            ptr.reset();
        };
    }

protected:
    mutable std::mutex _mx;
    std::jthread _thr;
    std::queue<prepared_coro> _queue;
    std::condition_variable _cv;
    std::shared_ptr<void> _instance_lock;
    awaitable_result<void> _join;

    static thread_local std::weak_ptr<dispatch_thread> cur_instance;



    void start() {
        _thr = std::jthread([this](std::stop_token tkn){
            cur_instance = weak_from_this();
            worker(tkn);
        });
    }

    void worker(std::stop_token tkn) {
        std::stop_callback stopper(tkn, [this]{
            std::lock_guard _(_mx);
            _cv.notify_one();
        });
        while (!tkn.stop_requested()) {
            std::shared_ptr<void> ilock;
            std::unique_lock lk(_mx);
            if (_queue.empty()) {
                if (tkn.stop_requested()) break;
                _cv.wait(lk);
            } else {
                auto p = std::move(_queue.front());
                _queue.pop();
                if (_queue.empty()) {
                    ilock = std::move(_instance_lock);               
                }
                lk.unlock();
                p.lazy_resume();
                ilock.reset();
            }
        }
    }

};

inline thread_local std::weak_ptr<dispatch_thread> dispatch_thread::cur_instance = {};

class dispatch_proxy_callaback_type {
public:
    void operator()(prepared_coro coro) {
        if (_disp) _disp->enqueue(std::move(coro));
    }    
    dispatch_proxy_callaback_type():_disp(dispatch_thread::current()) {
        if (!_disp) throw std::runtime_error("Operation requires dispatch thread context");    
    }
protected:
    std::shared_ptr<dispatch_thread> _disp;
};

///converts awaitable result to dispatchable result. This causes that when result is set, the coroutine is resumed in associated dispatcher
/**
to use this class, it must be created in thread with dispatcher, otherwise exception is thrown;
 */
template<is_awaitable_result_compatible T>
class dispatch_result : public result_proxy<T,  dispatch_proxy_callaback_type> {
public:
    dispatch_result(T &&result_object)
        :result_proxy<T,  dispatch_proxy_callaback_type>(std::move(result_object), dispatch_proxy_callaback_type{}) {}
    dispatch_result(T &result_object)
        :result_proxy<T,  dispatch_proxy_callaback_type>(std::move(result_object), dispatch_proxy_callaback_type{}) {}
};

}

