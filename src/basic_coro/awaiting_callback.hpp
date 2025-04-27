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
        auto operator()() {
            return _cb(_awt);
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
    ///dtor
    ~awaiting_callback() {
        clear_callback();
        clear_awaiter();
    }
    ///move constructor moves the stored awaiter, but it cannot move callback
    /**
     * moves just stored awaiter. The callback itself is left unmoved and it is eventually destroyed
     * in original object.
     */
    awaiting_callback(awaiting_callback &&other):_frame_charged(false), _awaiter_charged(other._awaiter_charged) {
        if (_awaiter_charged) {
            std::construct_at(&_awt, std::move(other._awt));
            other.clear_awaiter();
        }
    }

    ///Sets awaiter
    /**
     * @param awt an awaiter instance ready to be initiated for await_suspend. The awaiter must
     * be movable.
     *
     * @note any previously set awaiter is destroyed
     */
    void set_awaiter(Awaiter &awt) {
        clear_awaiter();
        std::construct_at(&_awt,std::move(awt));
        _awaiter_charged = true;
    }

    ///Sets awaiter
    /**
     * @param awt an awaiter instance ready to be initiated for await_suspend. The awaiter must
     * be movable.
     *
     * @note any previously set awaiter is destroyed
     */
    void set_awaiter(Awaiter &&awt) {
        set_awaiter(awt);
    }

    template<typename X>
    requires(has_co_await<X> && std::is_same_v<temporary_awaiter_type<X>, Awaiter>)
    void set_awaiter(X &&awt) {
        clear_awaiter();
        new(&_awt) Awaiter(awt.operator co_await());
    }

    template<typename X>
    requires(has_global_co_await<X> && std::is_same_v<temporary_awaiter_type<X>, Awaiter>)
    void set_awaiter(X &&awt) {
        clear_awaiter();
        new(&_awt) Awaiter(operator co_await(awt));
    }

    ///sets callback
    /**
     * @param cb callback instance, its closure must be small enough to fit into
     * reserved space in the object (specified by template Args )
     * @note callback must be movable
     *
     */
    template<std::invocable<Awaiter &> CB>
    requires(sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    void set_callback(CB &&cb) {
        clear_callback();
        new(_space) coro_frame_cb<CallCB<CB> >(CallCB<CB>(_awt, std::forward<CB>(cb)));
        _frame_charged = true;
    }

    ///Fire await operation on already set awaiter and callback
    /**
     * Causes that await_suspend is called on the prepared awaiter. If the
     * awaiter is marked ready, it immediately initiates the callack. If the
     * awaiter is not ready, await_suspend is called with internal handle, which
     * causes that callback will be called once the operation is complete
     *
     * @return returns prepared coroutine handle. This could be internal handle
     * if awaiter has been marked ready, or any result returned from await_suspend.
     * Return value can be empty.
     *
     * @note in case of awaiter is marked ready, the callback is not executed now. It
     * is returned as internal handle from the function. You need to resume the
     * handle to finish completion. The optimal way is to move return value outside
     * any held lock and resume it there
     *
     */
    coro::prepared_coro await() {
        if (!_frame_charged) throw std::logic_error("Callback was not set");
        if (!_awaiter_charged) throw std::logic_error("Awaiter was not set");
        if (_awt.await_ready()) {
            return _frame.create_handle();
        } else {
            return call_await_suspend(_awt, _frame.create_handle());
        }
    }

    /// await in set awaiter with specified callback
    /**
     * Just combines set_callback() and await().
     * @return see await()
     */
    template<std::invocable<Awaiter &> CB>
    requires(sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    coro::prepared_coro await(CB &&cb) {
        set_callback(std::forward<CB>(cb));
        return await();
    }

    ///Continue in await operation with different awaiter
    /**
     * This can be used to repeat asynchronous operation if previous
     * operation was not fully completed
     * @param awt compatible awaiter, see set_awaiter()
     * @return see await()
     */
    template<typename AWT>
    coro::prepared_coro await_cont(AWT &&awt) {
        set_awaiter(std::forward<AWT>(awt));
        return await();
    }


    ///Full await on awaiter with callback
    /**
     * @param awt awaiter. See set_awaiter()
     * @param cb callback. See set_callback()
     * @return see await()
     */
    template<std::invocable<Awaiter &> CB>
    requires(sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    coro::prepared_coro await(Awaiter &awt, CB &&cb) {
        set_callback(std::forward<CB>(cb));
        return await_cont(awt);
    }

    ///Full await on awaiter with callback
    /**
     * @param awt awaiter. See set_awaiter()
     * @param cb callback. See set_callback()
     * @return see await()
     */
    template<std::invocable<Awaiter &> CB>
    requires(sizeof(coro_frame_cb<CallCB<CB> >) <= required_space)
    coro::prepared_coro await(Awaiter &&awt, CB &&cb) {
        set_callback(std::forward<CB>(cb));
        return await_cont(awt);
    }

    ///Retrieves awaiter instance
    /**
     * @return reference to internal awaiter instance. You can call additional methods
     * on the awaiter, such a cancel()
     * @note The awaiter must be set previously, otherwise UB
     */
    Awaiter &get_awaiter() {
        return _awt;
    }


    ///clear a coroutine frame, call destructor on the callback
    /**
     * If the instance was previously used for awaiting,
     * this function destroys stored callback function (calls its destructor)
     * You can call this function only if operation is already complete
     */
    void clear_callback() {
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
        clear_callback();
        clear_awaiter();
    }

    struct awaiter_guard_deleter {
        void operator()(awaiting_callback *me){
            me->clear_awaiter();
        }
    };

    using awaiter_guard = std::unique_ptr<awaiting_callback, awaiter_guard_deleter>;

    ///Sets awaiter and returns guard
    /**
     * @param awt awaiter
     * @return a guard object which automatically clears awaiter when guard left the scope
     */
    awaiter_guard set_awaiter_guard(Awaiter &awt) {
        set_awaiter(awt);
        return awaiter_guard(this);
    }

    ///Sets awaiter and returns guard
    /**
     * @param awt awaiter
     * @return a guard object which automatically clears awaiter when guard left the scope
     */
    awaiter_guard set_awaiter_guard(Awaiter &&awt) {
        return set_awaiter_guard(awt);
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
