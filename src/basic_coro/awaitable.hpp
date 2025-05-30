#pragma once
#include "concepts.hpp"
#include "sync_await.hpp"
#include "coroutine.hpp"
#include <optional>
#include <memory>


namespace coro {

template<typename T> class awaitable;
template<typename T> class awaitable_result;


///Implements awaiter proxy, which can be used to convert return value to different return value
/**
 * @tparam Awt type of awaiter
 * @tparam Callback type of callback. It must be callable with awaiter as first argument and return value of awaitable<T> type
 *
 * This class is used to convert awaiter to different type. It is used in the following way:
 *
 * @code
 * auto awt = awaitable_function();
 * awaiter_proxy proxy(awt, [](auto &awt) {
 *      return awt.await_resume()*42;
 * });
 * auto res = co_await proxy;
 * @endcode
 */
template<is_awaiter Awt, std::invocable<Awt &> Callback>
class awaiter_proxy {
public:

    awaiter_proxy(Awt &awt, Callback &&cb):_awaiter(awt), _callback(std::forward<Callback>(cb)) {}
    awaiter_proxy( awaiter_proxy &&) = default;

    bool await_ready() const {return _awaiter.await_ready();}
    auto await_suspend(std::coroutine_handle<> h) {
        return _awaiter.await_suspend(h);
    }
    auto await_resume() {
        return _callback(_awaiter);
    }

    ///synchronous get value
    decltype(auto) get() {
        return sync_await(*this);
    }
    ///synchronous get value();
    decltype(auto) operator *() {
        return get();
    }
    ///wait but don't get value;
    void wait() {
        if (await_ready()) {
            sync_frame fr;
            await_suspend(fr.create_handle()).resume();
            fr.wait();
        }
    }

protected:
    Awt &_awaiter;
    Callback _callback;
};


template<typename T>
class [[nodiscard]] awaitable_result {
public:

    ///construct uninitialized
    /**
     * Uninitialized object can be used, however it will not invoke any
     * action and set result or exception is ignored
     */
    awaitable_result() = default;
    ///construct result using pointer to awaitable (which contains space to result)
    awaitable_result(awaitable<T> *ptr):_ptr(ptr) {}
    ///you can move
    awaitable_result(awaitable_result &&other) = default;
    ///you can move
    awaitable_result &operator=(awaitable_result &&other) = default;

    using const_reference = std::add_lvalue_reference_t<std::add_const_t<std::conditional_t<std::is_void_v<T>, void_type, T> > >;
    using rvalue_reference = std::add_rvalue_reference_t<std::conditional_t<std::is_void_v<T>, void_type, T> > ;

    ///set the result
    /**
     * @param val value to set. The value must be convertible to result. It can be also std::exception_ptr or std::nullopt with their
     * special meaning
     * @return prepared coroutine. If the result is discarded, coroutine
     * is resumed immediately. You can store result and destroy it
     * later to postpone resumption.
     */

    template<typename _Val>
    requires(is_awaitable_valid_result_type<T, _Val>)
    prepared_coro operator=(_Val && val) {
        return set_value(std::forward<_Val>(val));
    }

    prepared_coro operator=(std::nullopt_t) {
        return set_empty();
    }

    prepared_coro operator=(std::exception_ptr e) {
        return set_exception(e);
    }

    ///set result by calling its constructor
    /**
     * @param args arguments required to construct the result
     * @return prepared coroutine. If the result is discarded, coroutine
     * is resumed immediately. You can store result and destroy it
     * later to postpone resumption.
     */
    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    prepared_coro operator()(Args && ... args) {
        return set_value(std::forward<Args>(args)...);
    }

     /// set value
    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    prepared_coro set_value(Args && ... args);

    /// set exception as result
    prepared_coro set_exception(std::exception_ptr e);

    ///sets empty result.
    /**
     * the awaiting coroutine receives exception canceled_exception()
     */
    prepared_coro set_empty();

    ///Release internal pointer to be used elswhere
    /**
     * This is for special purpose, if you need carry the pointer to
     * result by other way, for example you need to cast it as uintptr_t.
     * Remember that you need it convert back later and initialize
     * awaitable_result with it to restore its purpose
     *
     * @return internal pointer.
     *
     * @note the object itself becomes unintialized
     */
    awaitable<T> *release() {return _ptr.release();}

    ///returns true if the result is expected
    /**
     * @retval true result is expected
     * @retval false result is not excpected (detached mode?)
     */
    explicit operator bool() const {return static_cast<bool>(_ptr);}

    awaitable<T> *get_handle() const {return _ptr.get();}

protected:
    struct deleter {
        void operator()(awaitable<T> *ptr) const;
    };
    std::unique_ptr<awaitable<T>, deleter> _ptr;



};

///allows to override reserved space in awaitable class for given T
/**
 * @tparam parameter type of awaitable<T>
 *
 * You need to overload this definition with own value (as a constant value).
 * Ensure that both sides see the same definition. It is best to declare overload with the
 * type itself
 *
 * @note You cannot set this value to be less than sizeof(T). Larger value is always used
 */
template<typename T>
struct awaitable_reserved_space {
    static constexpr std::size_t value = 4*sizeof(void *);
};


///Awatable object. Indicates asynchronous result
/**
 * To access value
 *
 * @code
 * co_await obj
 * obj >> callback(result)
 * type result = obj
 * @endcode
 *
 * To set value
 *
 * @code
 * return coroutine(args)
 * return [=](awaitable_result r) {
 *         //do async stuff
 *         r = result;
 *         // or r(result)
 *  };
 * @endcode
 *
 * @tparam T type of return value - can be void
 */
template<typename T>
class awaitable {
public:
    ///contains actually stored value type. It is T unless for void, it is bool
    using store_type = std::conditional_t<std::is_reference_v<T>,std::reference_wrapper<voidless_type<std::remove_reference_t<T> > >, voidless_type<T> >;
    ///contains alias for result object
    using result = awaitable_result<T>;
    ///allows to use awaitable to write coroutines
    using promise_type = coroutine<T>::promise_type;

    ///table of control methods for generic callback
    struct CBVTable {
        ///call the callback
        /**
         * @param inst pointer to an instance
         * @param result result object
         */
        prepared_coro (*call)(void *inst, result);
        ///destroy callback (end lifetime)
        /**
         * @param inst pointer to an instance to destroy
         */
        void (*destroy)(void *inst);
        ///move callback
        /**
         * @param from pointer to an instance to move from
         * @param to pointer to uninitialized space where to move instance. Function starts lifetime at this place
         *
         * @note original instance is still active (but in moved out state)
         */
        void (*move)(void *from, void *to);
    };

    ///this class is used to hold dynamically allocated callback (on heap), acting as its proxy
    /**
     * @tparam Fn callback function (closure)
     */
    template<typename Fn>
    class DynamicAllocatedCB {
    public:
        DynamicAllocatedCB(Fn &&fn): _ptr(std::make_unique<Fn>(std::forward<Fn>(fn))) {}
        prepared_coro operator()(result r) {return (*_ptr)(std::move(r));}
    protected:
        std::unique_ptr<Fn> _ptr;
    };

    ///declaraton of constexpr method table for function Fn
    /**
     * @tparam Fn function (closure object)
     */
    template<typename Fn>
    static constexpr auto cbvtable = CBVTable {
        [](void *me, result r)->prepared_coro{
            if constexpr(std::is_convertible_v<std::invoke_result_t<Fn, result>, prepared_coro>) {
                return (*reinterpret_cast<Fn *>(me))(std::move(r));
            } else {
                 (*reinterpret_cast<Fn *>(me))(std::move(r));
                 return {};
            }
        },
        [](void *me){std::destroy_at(reinterpret_cast<Fn *>(me));},
        [](void *from, void *to){std::construct_at(reinterpret_cast<Fn *>(to), std::move(*reinterpret_cast<Fn *>(from)));},
    };


    ///construct with no value
    awaitable(std::nullopt_t):_state(State::no_value) {};
    ///destructor
    /**
     * @note if there is prepared asynchronous operation, it is started
     * in detached mode. If you need to cancel such operation, use cancel()
     */
    ~awaitable() {
        dtor();
    }

    ///construct containing result constructed by arguments
    template<typename ... Args>
    requires (std::is_constructible_v<store_type, Args...>  && (sizeof...(Args) != 1 || (
                (!std::is_same_v<std::remove_reference_t<Args>, awaitable> && ...)
                && (!std::is_base_of_v<coroutine_tag, Args> && ...)
            )))
    awaitable(Args &&... args)
        :_state(State::value),_value(std::forward<Args>(args)...) {}

    ///construct by coroutine awaitable for its completion
    template<coro_allocator _Alloc>
    awaitable(coroutine<T, _Alloc> coroutine):_state(State::coro),_coro(std::move(coroutine)) {}

    ///construct containing result constructed by arguments
    template<typename ... Args>
    requires (std::is_constructible_v<store_type, Args...>)
    awaitable(std::in_place_t, Args &&... args)
        :_state(State::value),_value(std::forward<Args>(args)...) {}

    ///construct unresolved containing function which is after suspension of the awaiting coroutine
    template<std::invocable<result> Fn>
    awaitable(Fn &&fn) {
        if constexpr(sizeof(Fn) <= callback_max_size) {
            new(_callback_space) Fn(std::forward<Fn>(fn));
            _vtable = &cbvtable<Fn>;

        } else {
            new(_callback_space) DynamicAllocatedCB<Fn>(std::forward<Fn>(fn));
            _vtable = &cbvtable<DynamicAllocatedCB<Fn> >;
        }
    }

    ///construct containing result - in exception state
    awaitable(std::exception_ptr e):_state(State::exception),_exception(std::move(e)) {}

    ///construct unresolved containing member function which is after suspension of the awaiting coroutine
    /**
     * @param ptr a pointer or any object which defines dereference operator (*) which
     * returns reference to an instance of a class
     * @param fn pointer to member function, which is called on the instance of the class
     */
    template<typename ObjPtr, typename Fn>
    requires(is_member_fn_call_for_result<result, ObjPtr, Fn>)
    awaitable(ObjPtr ptr, Fn fn):awaitable([ptr, fn](result res){
        ((*ptr).*fn)(std::move(res));
    }) {}


    ///awaitable can be moved
    awaitable(awaitable &&other) {
        switch (other._state) {
            case State::no_value:_state = State::no_value; break;
            case State::value: std::construct_at(&_value, std::move(other._value));_state = State::value;break;
            case State::exception: std::construct_at(&_exception, std::move(other._exception));_state = State::exception;break;
            case State::coro: std::construct_at(&_coro, std::move(other._coro));_state = State::coro;break;
            default:
                _vtable = other._vtable;
                _vtable->move(other._callback_space, _callback_space);
        }
        other.destroy_state();
        other._state = State::no_value;
    }

    ///awaitable can be assigned by move
    awaitable &operator=(awaitable &&other) {
        if (this != &other) {
            std::destroy_at(this);
            std::construct_at(this, std::move(other));
        }
        return *this;
    }


    ///returns whether the object has a value
    /**
     * The object must be in resolved state. Use co_await ready() if not
     */
    bool has_value() const {
        return _state == State::value || _state == State::exception;
    }

    explicit operator bool() const {return has_value();}

    ///returns whether there is an exception in the object
    /**
     * The object must be in resolved state. Use co_await ready() if not
     */
    bool has_exception() const {
        return _state == State::exception;
    }


    ///returns true if the awaitable is resolved
    bool is_ready() const {
        return await_ready();
    }


    ///returns value of resolved awaitable
    std::add_rvalue_reference_t<T> await_resume() {
        return std::move(*this).value();
    }

    ///return true, if the awaitable is resolved
    bool await_ready() const noexcept {
        return _state == State::no_value || _state == State::value || _state == State::exception;
    }

    ///handles suspension
    /**
     * @param h coroutine currently suspended
     * @return coroutine being resumed
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _owner = h;
        switch (_state) {
            case State::no_value:
            case State::value:
            case State::exception: return h;
            case State::coro: return _coro.start(result(this)).symmetric_transfer();
            default:
                return _vtable->call(_callback_space, result(this)).symmetric_transfer();
        }
    }

    ///set callback, which is called once the awaitable is resolved
    /**
     * @param cb callback function. The function receives reference
     * to awaitable in resolved state
     * @return prepared coroutine (if there is an involved one), you
     * can postpone its resumption by storing result and release it
     * later
     *
     * @note you can set only one callback or coroutine
     */
    template<std::invocable<awaitable &> _Callback>
    prepared_coro operator >> (_Callback &&cb) {
        objstdalloc a;
        return set_callback_internal(std::forward<_Callback>(cb), a);
    }

    ///set callback, which is called once the awaitable is resolved
    /**
     * @param cb callback function. The function receives reference
     * to awaitable in resolved state
     * @return prepared coroutine (if there is an involved one), you
     * can postpone its resumption by storing result and release it
     * later
     *
     * @note you can set only one callback or coroutine
     */
    template<std::invocable<awaitable &> _Callback>
    prepared_coro set_callback (_Callback &&cb) {
        objstdalloc alloc;
        return set_callback_internal(std::forward<_Callback>(cb), alloc);
    }

    ///set callback, which is called once the awaitable is resolved
    /**
     * @param cb callback function. The function receives reference
     * to awaitable in resolved state
     * @param a allocator instance, allows to allocate callback
     * instance using this allocator
     * @return prepared coroutine (if there is an involved one), you
     * can postpone its resumption by storing result and release it
     * later
     *
     * @note you can set only one callback or coroutine
     */
    template<std::invocable<awaitable &> _Callback, coro_allocator _Allocator>
    prepared_coro set_callback (_Callback &&cb, _Allocator &a) {
        return set_callback_internal(std::forward<_Callback>(cb), a);
    }

    ///Retrieve value of the awaitable
    /** The value must be ready (is_ready() == true).
     * @return value stored in the object, returned as rvalue reference
     * @note if the value is not awailable, throws exeception
     */
    std::add_rvalue_reference_t<T> value() && {
        switch (_state) {
            default: throw await_canceled_exception();
            case State::exception: std::rethrow_exception(_exception);
            case State::value: if constexpr(std::is_void_v<T>) {return;} else {return std::forward<T>(_value);}
        }
    }

    /** The value must be ready (is_ready() == true).
     * @return value stored in the object, returned as lvalue reference
     * @note if the value is not awailable, throws exeception
     */
    std::add_lvalue_reference_t<T> value() & {
        switch (_state) {
            default: throw await_canceled_exception();
            case State::exception: std::rethrow_exception(_exception);
            case State::value: if constexpr(std::is_void_v<T>) {return;} else {return _value;}
        }
    }

    /** The value must be ready (is_ready() == true).
     * @return value stored in the object, returned as const lvalue reference
     * @note if the value is not awailable, throws exeception
     */
    std::add_lvalue_reference_t<std::add_const_t<T> > value() const & {
        switch (_state) {
            default: throw await_canceled_exception();
            case State::exception: std::rethrow_exception(_exception);
            case State::value: if constexpr(std::is_void_v<T>) {return;} else {return _value;}
        }
    }
    /** The value must be ready (is_ready() == true).
     * @return value stored in the object, returned as const rvalue reference
     * @note if the value is not awailable, throws exeception
     */
    std::add_lvalue_reference_t<std::add_const_t<T> > value() const && {
        return value();
    }

    ///access using operator * - @see value();
    std::add_rvalue_reference_t<T> operator *() && {return std::move(*this).value();}
    ///access using operator * - @see value();
    std::add_lvalue_reference_t<T> operator *() & {return this->value();}
    ///access using operator * - @see value();
    std::add_lvalue_reference_t<std::add_const_t<T> > operator *() const && {return this->value();}
    ///access using operator * - @see value();
    std::add_lvalue_reference_t<std::add_const_t<T> > operator *() const & {return this->value();}

    ///synchronous await
    /**
     * Function performs synchronous wait. When value is ready, returns it as rvalue reference
     *
     * @note the function always return rvalue reference
     */
    std::add_rvalue_reference_t<T> get() {wait(); return std::move(*this).value();}


    ///awaitable function which returns true if the result is resolved with a value or exception
    /**
     * This is awaitable version on has_value(). It is useful when you need to wait
     * on the result of the awaitable object, but you don't want to handle exceptions
     * This function doesn't throw exception, it just returns true, if the result is resolved
     * with a value or exception. Otherwise it returns false.
     *
     * @code {c++}
     * bool has_v = co_await obj.ready(); //wait like direct co_await on obj, no exception thrown
     * if (has_v) {                     //if awaitable has value
     *    auto v = obj.await_resume();  //retrieve the value manually (exception can be thrown here)
     *   //do something with the value
     * } else {
     *    //no value is available.
     * }
     * @endcode
     */
    auto /*awaitable<bool>*/  ready() {
        return awaiter_proxy(*this, [](awaitable &awt) {
            return awt.has_value();
        });
    }

    ///awaitable function which returns value as optional
    /**
     * @return optional result. It contains no_value, when co_await
     * operation was canceled.
     */
    auto /*awaitable<std::optional<T> > */ as_optional() {
        return awaiter_proxy(*this, [](awaitable &awt) ->std::optional<store_type> {
            if (awt.has_value()) {
                if (awt._state == State::exception) {
                    std::rethrow_exception(awt._exception);
                } else {
                    return std::move(awt._value);
                }
            } else {
                return std::nullopt;
            }
        });
    }

    ///evaluate asynchronous operation, waiting for result synchronously
    void wait();


    ///copy evaluated awaitable object.
    /**
     * If the object has value or exception, returned object contains copy
     * of the value or exception. Otherwise default constructed object is
     * returned
     *
     * @return evaluated awaitable
     *
     * @note because the object itself cannot be copied by copy constructor, this
     * function can be used to make a copy. However you cannot copy value
     * if the value is not resolved yet
     */
    awaitable copy_value() const  {
        switch (_state) {
            default:
            case State::no_value: return std::nullopt;
            case State::value:return awaitable(std::in_place, _value);
            case State::exception:return awaitable(_exception);

        }

    }
    ///return if there is someone awaiting on object
    /**
     * @retval true someone is awaiting, do not destroy the object
     * @retval false nobody awaiting
     */
    bool is_awaiting() const {
        return _owner != std::coroutine_handle<>();
    }

    ///Create result object from existing awaitable object
    /**
     * This function is used by awaiting_callback to create result object
     * on its internal awaiter. However it allows you to create result from
     * coroutine handle which is awaiting on result and awaitable object itself
     * where the coroutine expects the result. The awaitable object is set
     * to is_awaiting() state
     *
     * @param h handle of coroutine which is resumed once the value is set
     * @return result object
     */
    result create_result(std::coroutine_handle<> h) {
        if (_owner) throw invalid_state();
        _owner = h;
        return result(this);
    }


    ///forward result of this awaiter to existing result object
    /**
     * This function causes that result of this awaiter is forwarded to result
     * object regardless on state of this object. If the
     * state is pending, then result object is passed to the async lambda
     * for completion
     *
     * @param r result object
     *
     * @return prepared coroutine handle. The operation can resume a coroutine
     * its handle is returned there
     *
     * @note after forward operation, current awaiter is in uninicialized state
     */
    prepared_coro forward(result &r) {
        prepared_coro out;
        if (r) {
            switch (_state) {
                case State::no_value: out = r.set_empty();break;
                case State::value: out = r.set_value(std::move(_value));break;
                case State::exception: out = r.set_exception(std::move(_exception));break;
                case State::coro:out = _coro.start(std::move(r)).symmetric_transfer();break;
                default: {
                    auto h = r.get_handle();
                    h->destroy_state();
                    h->_vtable = _vtable;
                    _vtable->move(_callback_space, h->_callback_space);
                    out = _vtable->call(h->_callback_space,std::move(r));
                }
            }
        }
        destroy_state();
        _state = State::no_value;
        return out;
    }

    ///forward result of this awaiter to existing result object
    /**
     * This function causes that result of this awaiter is forwarded to result
     * object regardless on state of this object. If the
     * state is pending, then result object is passed to the async lambda
     * for completion
     *
     * @param r result object
     *
     * @return prepared coroutine handle. The operation can resume a coroutine
     * its handle is returned there
     *
     * @note after forward operation, current awaiter is in uninicialized state
     *
     */
    void forward(result &&r) {
        forward(r);
    }

protected:

    enum class State : std::uintptr_t {
        ///awaitable is resolved with no value
        no_value = 0,
        ///awaitable is resolved with a value
        value = 1,
        ///awaitable is resolved with exception
        exception = 2,
        ///awaitable is not resolved, a coroutine is ready to generate result once awaited
        coro = 3
    };


    static constexpr auto callback_max_size = std::max(awaitable_reserved_space<T>::value, sizeof(store_type));

    ///current state of object
    union {
        State _state;
        const CBVTable *_vtable;
    };
    ///handle of owning coroutine. If not set, no coroutine owns, nobody awaiting
    /**@note the handle must not be destroyed in destructor. The awaitable instance
     * is always inside of coroutine's frame, so it will be only destroyed with the owner
     */
    std::coroutine_handle<> _owner;
    union {
        ///holds current value
        store_type _value;
        ///holds current exception
        std::exception_ptr _exception;
        ///holds coroutine registration (to start coroutine when awaited)
        coroutine<T> _coro;
        ///holds reserved space for local callback
        /**@see get_local_callback() */
        char _callback_space[callback_max_size];

    };

    void dtor() {
        if (is_awaiting()) throw invalid_state();
        destroy_state();
    }

    void destroy_state() {
        switch (_state) {
            case State::no_value:break;
            case State::value: std::destroy_at(&_value);break;
            case State::exception: std::destroy_at(&_exception);break;
            case State::coro: _coro.cancel();std::destroy_at(&_coro);break;
            default:
               _vtable->destroy(_callback_space);break;
        }
    }

    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    void set_value(Args && ... args) {
        destroy_state();
        try {
            _state = State::value;
            void *trg = const_cast<std::remove_const_t<store_type> *>(&_value);
            if constexpr (sizeof...(Args) == 1 && (std::is_invocable_r_v<store_type, Args>  && ...)) {
                new(trg) store_type((...,args()));
            } else if constexpr (sizeof...(Args) == 1 && (std::is_same_v<std::nullopt_t, std::decay_t<Args> > &&...)) {
                _state = State::no_value;
            } else {
                new(trg) store_type(std::forward<Args>(args)...);
            }
        } catch (...) {
            _state = State::exception;
            std::construct_at(&_exception, std::current_exception());
        }
    }

    void set_exception(std::exception_ptr e) {
        destroy_state();
        _state = State::exception;
        std::construct_at(&_exception, std::move(e));
    }

    void drop() {
        destroy_state();
        _state = State::no_value;
    }

    prepared_coro wakeup() {
        if (!is_ready()) drop();
        return prepared_coro(std::exchange(_owner, {}));
    }

    template<typename _Callback, typename _Allocator>
    prepared_coro set_callback_internal(_Callback &&cb, _Allocator &a);


    friend class awaitable_result<T>;
    friend struct details::promise_type_base<T>;
    friend struct details::promise_type_base_generic<T>;

};


template <typename T>
template <typename... Args>
requires(is_awaitable_valid_result_type<T, Args...>)
inline prepared_coro awaitable_result<T>::set_value(Args &&...args)
{
    awaitable<T> *p = _ptr.release();
    if (p) {
        p->set_value(std::forward<Args>(args)...);
        return p->wakeup();
    }
    return {};
}


template <typename T>
template <typename _Callback, typename _Allocator>
inline prepared_coro awaitable<T>::set_callback_internal(_Callback &&cb, _Allocator &a)
{
    auto cb_coro = [](_Allocator &, _Callback cb, awaitable awt) -> coroutine<void> {
        co_await awt.ready();
        cb(awt);
    };

    prepared_coro out = {};

    if (await_ready()) {
        cb(*this);
    } else {
        out = cb_coro(a, std::forward<_Callback>(cb), std::move(*this)).start({});
    }
    return out;
}


template <typename T>
inline void awaitable<T>::wait()
{
    if (!await_ready()) {
        sync_frame sync;
        await_suspend(sync.create_handle()).resume();
        sync.wait();
    }
}

template<typename T>
inline void awaitable_result<T>::deleter::operator()(awaitable<T> *ptr) const {
    ptr->wakeup();
}

template <typename T>
inline prepared_coro awaitable_result<T>::set_exception(std::exception_ptr e)
{
    awaitable<T> *p = _ptr.release();
    if (p) {
        p->set_exception(e);
        return p->wakeup();
    }
    return {};
}

template <typename T>
inline prepared_coro awaitable_result<T>::set_empty()
{
    awaitable<T> *p = _ptr.release();
    if (p) {
        p->drop();
        return p->wakeup();
    }
    return {};
}
}
