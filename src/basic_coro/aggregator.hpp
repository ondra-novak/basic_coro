#pragma once
#include "coro_frame.hpp"
#include "concepts.hpp"
#include "queue.hpp"
#include "async_generator.hpp"
#include <coroutine>
#include <tuple>
#include <mutex>
#include <vector>
#include <stdexcept>
#include <string>


namespace coro {

class generator_exception: public std::exception, public std::nested_exception {
public:
    generator_exception(unsigned int index):_index(index) {}

    virtual const char *what() const noexcept override {
        _msg = "Generator exception at index: " + std::to_string(_index);
        return _msg.c_str();
    } 

protected:
    unsigned int _index;
    mutable std::string _msg;
};
    

namespace details {

    
    class AggregatorHelperFrame: public coro_frame<AggregatorHelperFrame> {
    public:
        AggregatorHelperFrame(queue<unsigned int, 0, std::mutex> &q, unsigned int index)
            :_q(q),_index(index) {}
        void do_resume() {
            _q.push(_index);
        }
        void do_destroy() {

        }
    protected:
        queue<unsigned int, 0, std::mutex> &_q;
        unsigned int _index;
    };

}


template<typename T, typename Param, typename Alloc>
async_generator<T, Param, Alloc> aggregator(Alloc &, std::vector<async_generator<T, Param> > gens) {
    
    //vector of frames (one for each generator)
    std::vector<details::AggregatorHelperFrame> frames;
    //vector of awaitables - results
    std::vector<awaitable<T> > awts;
    //a queue, each frame enqueues self
    queue<unsigned int, 0, std::mutex> queue;
    //total count of running generators
    unsigned int count = gens.size();
    //initialize frames
    frames.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        frames.emplace_back(queue,i);
    }
    //initialize awaiters
    awts.reserve(count);
    for (unsigned int i = 0; i < count; ++i) {
        awts.emplace_back(std::nullopt);
    }
    unsigned int run_count = 0;
    //start each generator
    for (unsigned int i = 0; i < count; ++i) {
        auto &awt = awts[i];
        std::exception_ptr e;
        try {
            awt = gens[i].start();
            if (awt.await_ready()) {
                queue.push(i);
            } else {
                call_await_suspend(awt, frames[i].create_handle());
            }
            ++run_count;
            continue;
        } catch (...) {
            e = std::make_exception_ptr(generator_exception(i));
        }   

        co_yield e;
    }
    

    //define action on_destroy
    on_destroy _= [&]{
        while (run_count) { //wait for finish all pending generators (synchronously)
            queue.pop();
            --run_count;
        }
    };

    //while there are running generators
    while (run_count) {
        //pop index
        unsigned int idx =  co_await queue.pop();
        //retrieve awaiter
        auto &awt = awts[idx];
        //awaiter has no value
        if (!awt) {
            //this generator is done, decrease count
            --run_count;
        } else {
            //we store exception here
            std::exception_ptr e;
            //if argument if void
            if constexpr(std::is_void_v<Param>) {
                try {
                    //yield value
                    co_yield [&]{return awt.await_resume();};
                    //charge generator when returns
                    awt = gens[idx]();
                } catch (...) {
                    //exception can be thrown from awt.await_resume()
                    e = std::make_exception_ptr(generator_exception(idx));
                }
            } else {
                //with argument
                try {
                    //yield value and retrieve argument
                    Param p = co_yield [&]{return awt.await_resume();};
                    //charge generator with argument
                    awt = gens[idx](p);
                } catch (...) {
                    //exception can be thrown from awt.await_resume()
                    e = std::make_exception_ptr(generator_exception(idx));
                }
            }
            //if exception thrown
            if (e) {
                //yield it
                co_yield e;
                //remove this generator               
                --run_count;
            } else {
                //otherwise if new result is ready
                if (awt.await_ready()) {
                    //push index to queue
                    queue.push(idx);
                } else {
                    //otherwise call suspend and wait
                    call_await_suspend(awt, frames[idx].create_handle());
                }
            }
        }
    }
}

template<typename T, typename Param>
async_generator<T, Param> aggregator(std::vector<async_generator<T, Param> > gens) {
    objstdalloc alloc;
    return aggregator<T, Param, objstdalloc>(alloc, std::move(gens));
}

}