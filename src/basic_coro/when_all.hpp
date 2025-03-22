#include "prepared_coro.hpp"
#include "coro_frame.hpp"
#include "concepts.hpp"
#include "sync_await.hpp"
#include <atomic>
namespace coro {


///start and wait for one or multiple tasks
/**
 * This class can be used to wait for multiple awaitables
 *
 * @code
 * co_await when_all(awt1,awt2,awt3);
 * @endcode
 *
 * Note that all arguments are passed as reference. The co_await doesn't
 * return value, you need to extract results from
 * each awaitable after wait is complete
 *
 * The class can be used to run concurrently with a background coroutine, and
 * to join its awaitable at the end of the section.
 *
 * @code
 * auto awt1 = coro1();
 * when_all c(awt1); //coroutine is started here
 *  //any code here
 * co_await c; //join
 * @endcode
 *
 */
class when_all {
    public:
    
        ///construct empty
        when_all() = default;
    
        ///no copy
        when_all(const when_all &) = delete;
        ///no copy
        when_all &operator=(const when_all &) = delete;
    
        ///construc using multiple awaiters
        /**
         * @param other list of awaiters. This can be awaitable<> but it
         * also supports any direct awaiters. If the awaiter needs co_await
         * operator, you need to create instance of its result before it
         * can be used here
         *
         * @code
         * auto need_op1 = async_op1();
         * auto need_op2 = async_op2();
         * auto awt1 = operator co_await(need_op1);
         * auto awt2 = operator co_await(need_op2);
         * co_await when_all(awt1,awt2);
         * @endcode
         */
        template<is_awaiter... Awts>
        when_all(Awts & ... other) {
            prepared_coro ps[sizeof...(Awts)] = {add(other)...};
            for (auto &p: ps) p();
        }
    
        ///construct from iteratable container
        template<range_for_iterable X>
        when_all(X &list) {
            for (auto &x: list) add(x);
        }
    
        ///construct from array
        template<is_awaiter Awt, int n>
        when_all(Awt (&list)[n]) {
            for (auto &x: list) add(x);
        }
    
        ~when_all() {
            wait();
        }
    
        ///Add awaiter to list of awaiting awaiters.
        /**  
         * The function must be called before co_await or wait(). This allows programatically
         * add new awaiters before waiting on them
         * @param awt awaiter to add
         */
        template<is_awaiter X>
        prepared_coro add(X &awt) {
            if (!awt.await_ready()) {
                _cnt.count.fetch_add(1, std::memory_order_relaxed);
                return call_await_suspend(awt, _cnt.get_handle());
            }
            return {};
        }
    
        ///implements co_await
        bool await_ready() const noexcept {
            return (_cnt.count.load(std::memory_order_acquire) <= 1);
        }
    
        ///implements co_await
        std::coroutine_handle<> await_suspend(std::coroutine_handle<> me) noexcept {
            _cnt.r  = me;
            return _cnt.do_resume().symmetric_transfer();
        }
    
        ///doesn't return anything
        static void await_resume() noexcept {} 
    
        ///perform sync await
        void wait() {
            sync_await(*this);
        }
    
        ///reset state
        /** You can only reset state after successful co_await. This allows to reuse the object */
        bool reset() {
            unsigned int need = 0;
            return _cnt.count.compare_exchange_strong(need,1);
        }
    
    protected:
        struct counter: coro_frame<counter> {
            std::atomic<unsigned int> count = {1};
            prepared_coro r = {};
            prepared_coro do_resume() {
                //test whether we reached zero
                if (count.fetch_sub(1, std::memory_order_relaxed)  == 1) {
                    //ensure that all results are visible
                    count.load(std::memory_order_acquire);
                    //resume awaiting coroutine
                    return std::move(r);
                }
                //still pending
                return {};
            }
    
        };
        counter _cnt;
    
    };
}