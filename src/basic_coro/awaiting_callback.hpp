#pragma once

#include "prepared_coro.hpp"
#include "coro_frame.hpp"
#include "awaitable.hpp"

namespace coro {

/// Constructs a class which can be used to store a callback function while awaiting on an awaitable
/** In contrast to standard coroutine, this class has known size during compile time and its instance
 * can be created as member variable of another class.
 *
 * @param Awaiter type of standard C++ awaiter. It must be direct awaiter, you need to call
 *      operator co_await() manually if necessery. Note that Awaiter must be move constructible
 *      because it is moved into the instance.
 * @param ClosureArgsExample an example list of closure arguments for the callback. This helps
 *      to calculate required space. The actual closure can be differnt as long as its
 *      size is less or equal to calculated space
 *
 */
template<is_awaiter Awaiter, typename ... ClosureArgsExample>
requires (std::is_move_constructible_v<Awaiter>)
class awaiting_callback {

    ///Extended callback function
    template<typename CB>
    struct CallCB {
        ///the closure contains reference to awaiter
        Awaiter &_awt;
        ///and the actuall callback
        CB _cb;
        ///actual callback is called with argumenr awaiter
        void operator()() {
            _cb(_awt);
        }

        ///construct extended callback function
        /**
         * @param awt awaiter reference
         * @param cb callback function
         */
        CallCB(Awaiter &awt, CB &&cb):_awt(awt), _cb(std::forward<CB>(cb)) {}
    };

    ///contains example callback function with specified closure
    struct ExampleCB {
        std::tuple<ClosureArgsExample...> _closure;
        void operator()(Awaiter &) {
            throw invalid_state();
        }
    };

    ///contains example coro frame with callback
    using ExampleFrame = coro_frame_cb<CallCB<ExampleCB> >;
    ///contains required space
    static constexpr std::size_t required_space = sizeof(ExampleFrame);

public:

    ///construct the callback space
    awaiting_callback() {};
    ///object can be copied, but because it is frame, no content is copied
    awaiting_callback(const awaiting_callback &)
        :_frame_charged(false),_awaiter_charged(false) {}
    ///assigment has no effect, cannot copy internal state
    awaiting_callback &operator=(const awaiting_callback &) {
        return *this;
    }
    ///dtor
    ~awaiting_callback() {
        clear_frame();
        clear_awaiter();
    }

    ///continue in await operation
    /**
     * Starts await operation which is contination of previous await operation.
     * This function expects that callback has been set by function await(). This
     * function can be used inside of the callback to repeat await operation (the
     * callback is called again when await operation is complete)
     *
     * @param awt awaiter to await on
     * @return prepared coro holding result of await_suspend operation
     *  if a handle is returned. It can return handle to callback if
     *  the operation is already complete. You should resume the
     *  prepared_coro in order to finish asynchronous operation
     *
     * @exception invalid_state await_cont() called without initial await()
     *
     */
    prepared_coro await_cont(Awaiter &awt) {
        prepare_await(awt);
        return await_on_prepared();
    }

    ///continue in await operation
    /**
     * Starts await operation which is contination of previous await operation.
     * This function expects that callback has been set by function await(). This
     * function can be used inside of the callback to repeat await operation (the
     * callback is called again when await operation is complete)
     *
     * @param awt awaiter to await on
     * @return prepared coro holding result of await_suspend operation
     *  if a handle is returned. It can return handle to callback if
     *  the operation is already complete. You should resume the
     *  prepared_coro in order to finish asynchronous operation
     * @exception invalid_state await_cont() called without initial await()
     *
     */
    prepared_coro await_cont(Awaiter &&awt) {
        return await_cont(awt);
    }

    ///initiate await operation
    /**
     * @param awt awaiter to await on
     * @param cb callback with a closure. The closure must occupy less or equal calculated space
     * @return prepared coro holding result of await_suspend operation
     *  if a handle is returned. It can return handle to callback if
     *  the operation is already complete. You should resume the
     *  prepared_coro in order to finish asynchronous operation
     *
     * @note it is allowed to reuse this object to another await operation. You only
     * need to prevent to initiate await operation while other is still pending
     */
    template<std::invocable<Awaiter &> CB>
    requires (sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    prepared_coro await(Awaiter &awt, CB &&cb) {
        clear_frame();
        new(_space) coro_frame_cb<CallCB<CB> >(CallCB<CB>(_awt, std::forward<CB>(cb)));
        _frame_charged = true;
        return await_cont(awt);
    }

    ///initiate await operation
    /**
     * @param awt awaiter to await on
     * @param cb callback with a closure. The closure must occupy less or equal calculated space
     * @return prepared coro holding result of await_suspend operation
     *  if a handle is returned. It can return handle to callback if
     *  the operation is already complete. You should resume the
     *  prepared_coro in order to finish asynchronous operation
     *
     * @note it is allowed to reuse this object to another await operation. You only
     * need to prevent to initiate await operation while other is still pending

     */
    template<std::invocable<Awaiter &> CB>
    requires (sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    prepared_coro await(Awaiter &&awt, CB &&cb) {
        return await(awt, std::forward<CB>(cb));
    }

    ///clear a coroutine frame, call destructor on the callback
    /**
     * If the instance was previously used for awaiting,
     * this function destroys stored callback function (calls its destructor)
     * You can call this function only if operation is already complete
     */
    void clear_frame() {
        if (_frame_charged) {
            _frame.create_handle().destroy();
            _frame_charged = false;
        }
    }
    ///clear stored awaiter
    /**
     * Stored awaiter is clear, its destructor is called. This can be important
     * for some types of awaiters
     * You can call this function only if operation is already complete
     */
    void clear_awaiter() {
        if (_awaiter_charged) {
            std::destroy_at(&_awt);
            _awaiter_charged = false;
        }
    }

    ///clear object state
    /**
     * Clear both awaiter and frame, call their destructors.
     *
     * In normal usage, you don't need to clear state explicitly.
     * Functions await and await_cont handles clear automatically
     */
    void clear() {
        clear_frame();
        clear_awaiter();
    }

    ///prepares await operation by moving awaiter into internal state
    /** This function can be called before the  frame is initialized.
     * The frame can be initialized by run_await later
     *
     * @param awt awaiter to move in
     *
     * @note this function allows to check for non-blocking result. If
     * result is not ready, it can move awaiter to internal state to
     * be prepared for execution of asynchronous operation
     *
     */
    void prepare_await(Awaiter &awt) {
        clear_awaiter();
        std::construct_at(&_awt,std::move(awt));
        _awaiter_charged = true;
    }
    ///prepares await operation by moving awaiter into internal state
    /** This function can be called before the  frame is initialized.
     * The frame can be initialized by run_await later
     *
     * @param awt awaiter to move in
     *
     * @note this function allows to check for non-blocking result. If
     * result is not ready, it can move awaiter to internal state to
     * be prepared for execution of asynchronous operation
     *
     */
    void prepare_await(Awaiter &&awt) {
        prepare_await(awt);
    }

    ///await on prepared awaiter
    prepared_coro await_on_prepared() {
        if (!_frame_charged) throw std::logic_error("no callback has been defined");
        if (_awt.await_ready()) {
            return _frame.create_handle();
        } else {
            return call_await_suspend(_awt,_frame.create_handle());
        }
    }

    ///await on prepared awaiter, define callback
    template<std::invocable<Awaiter &> CB>
    requires (sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    prepared_coro await_on_prepared(CB &&cb) {
        clear_frame();
        new(_space) coro_frame_cb<CallCB<CB> >(CallCB<CB>(_awt, std::forward<CB>(cb)));
        _frame_charged = true;
        return await_on_prepared();
    }

    ///retrieves internal awaiter
    Awaiter *get_awaiter() {
        if (_awaiter_charged) return &_awt;
        else return nullptr;
    }

    class AutoCancelDeleter {
    public:
        template<typename X>
        void operator()(X *p) {
            auto a = p->get_awaiter();
            if (a) a->cancel();
        }
    };

    ///for awaitable<X> awaiter allows to create an object which
    /// causes calling of cancel on awaitable if the async lambda function is not executed
    std::unique_ptr<awaiting_callback, AutoCancelDeleter> get_auto_cancel() {
        return {this,{}};
    }

protected:
    union {
        Awaiter _awt;
    };
    union {
        ExampleFrame _frame;
        char _space[required_space];
    };
    bool _frame_charged = false;
    bool _awaiter_charged = false;


};

}
