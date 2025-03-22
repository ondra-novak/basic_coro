#pragma once
#include "concepts.hpp"
#include "sync_await.hpp"
#include "coroutine.hpp"
#include <optional>


namespace coro {

///replaces simple void everywhere valid type is required
struct void_type {};


template<typename T> class awaitable;
template<typename T> class awaitable_result;
template<typename T> using voidless_type = std::conditional_t<std::is_void_v<T>, void_type, T>;


template<typename T>
class awaitable_result {
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
    
    ///virtual interface to execute callback for resolution
    class ICallback {
    public:
        virtual ~ICallback() = default;
        ///start resolution, call the callback
        virtual prepared_coro call(result) = 0;
        ///move support
        virtual void move_to(void *address) = 0;
    };


    ///object which implements lambda callback
    /** This symbol is public to allow calculation of the size in bytes of this object */
    template<std::invocable<result> Fn>
    class CallbackImpl: public ICallback {
    public:
        CallbackImpl(Fn &&fn):_fn(std::forward<Fn>(fn)) {}
        virtual prepared_coro call(result r) {
            if constexpr(std::convertible_to<std::invoke_result_t<Fn, result>, prepared_coro>) {
                return prepared_coro(_fn(std::move(r)));
            } else {
                _fn(std::move(r));
                return {};
            }
        }
        virtual void move_to(void *address) {
            new(address) CallbackImpl(std::move(_fn));
        }


    protected:
        Fn _fn;
    };


    ///construct with no value
    awaitable(std::nullopt_t) {};
    ///destructor
    /**
     * @note if there is prepared asynchronous operation, it is started
     * in detached mode. If you need to cancel such operation, use cancel()
     */
    ~awaitable() {
        dtor();
    }
    ///construct containing result constructed by default constructor
    /**
     * @note if the result cannot be constructed by default constructor,
     * it is initialized with no value
     */
    awaitable() {
        if constexpr(std::is_default_constructible_v<store_type>) {
            std::construct_at(&_value);
            _state = value;
        } else {
            _state = no_value;
        }
    }

    ///construct containing result constructed by arguments
    template<typename ... Args>
    requires (std::is_constructible_v<store_type, Args...>  && (sizeof...(Args) != 1 || (
                (!std::is_same_v<std::remove_reference_t<Args>, awaitable> && ...)
                && (!std::is_base_of_v<coroutine_tag, Args> && ...)                
            )))
    awaitable(Args &&... args)
        :_state(value),_value(std::forward<Args>(args)...) {}

    ///construct by coroutine awaitable for its completion
    template<coro_allocator _Alloc>
    awaitable(coroutine<T, _Alloc> coroutine):_state(coro),_coro(std::move(coroutine)) {}

    ///construct containing result constructed by arguments
    template<typename ... Args>
    requires (std::is_constructible_v<store_type, Args...>)
    awaitable(std::in_place_t, Args &&... args)
        :_state(value),_value(std::forward<Args>(args)...) {}

    ///construct unresolved containing function which is after suspension of the awaiting coroutine
    template<std::invocable<result> Fn>
    awaitable(Fn &&fn) {
        if constexpr(sizeof(CallbackImpl<Fn>) <= callback_max_size) {
            new (_callback_space) CallbackImpl<Fn>(std::forward<Fn>(fn));
            _state = callback;
        } else {
            std::construct_at(&_callback_ptr, std::make_unique<CallbackImpl<Fn> >(std::forward<Fn>(fn)));
            _state = callback_ptr;
        }
    }

    ///construct containing result - in exception state
    awaitable(std::exception_ptr e):_state(exception),_exception(std::move(e)) {}

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
    awaitable(awaitable &&other):_state(other._state) {
        switch (_state) {
            default: break;
            case value: std::construct_at(&_value, std::move(other._value));break;
            case exception: std::construct_at(&_exception, std::move(other._exception));break;
            case coro: std::construct_at(&_coro, std::move(other._coro));break;
            case callback: other.get_local_callback()->move_to(_callback_space);break;
            case callback_ptr: std::construct_at(&_callback_ptr, std::move(other._callback_ptr));break;
        }
        other.destroy_state();
        other._state = no_value;
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
     * @return the result is awaitable, so you can co_await to retrieve
     * whether the awaitable receives a value (without throwing an exception)
     */
    awaitable<bool> has_value();

    ///returns whether the object has no value
    /**
     * @return the result is awaitable, so you can co_await to retrieve
     * whether the awaitable receives a value (without throwing an exception)
     */
    awaitable<bool> operator!() ;

    ///returns true if the awaitable is resolved
    bool is_ready() const {
        return await_ready();
    }

    ///returns iterator a value
    /**
     * @return iterator to value if value is stored there, otherwise returns end
     * @note the function is awaitable, you can co_await if the value is not yet available.
     */
    awaitable<store_type *> begin();

    ///returns iterator to end
    /**
     * @return iterator to end
     * @note this function is not awaitable it always return valid end()
     */
    store_type *end()  {
        return &_value+1;
    }


    ///returns value of resolved awaitable
    std::add_rvalue_reference_t<T> await_resume() {
        if (_state == value) {
            if constexpr(std::is_void_v<T>) {
                return;
            } else if constexpr(std::is_reference_v<T>) {
                return _value.get();
            } else {
                return std::move(_value);
            }
        } else if (_state == exception) {
            std::rethrow_exception(_exception);
        }
        throw await_canceled_exception();
    }


    ///return true, if the awaitable is resolved
    bool await_ready() const noexcept {
        return _state == no_value || _state == value || _state == exception;
    }

    ///handles suspension
    /**
     * @param h coroutine currently suspended
     * @return coroutine being resumed
     */
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> h) {
        _owner = h;
        if (_state == coro) {
            return _coro.start(result(this)).symmetric_transfer();
        } else if (_state == callback) {
            return get_local_callback()->call(result(this)).symmetric_transfer();
        } else if (_state == callback_ptr) {
            auto cb = std::move(_callback_ptr);
            return cb->call(result(this)).symmetric_transfer();
        } else {
            return h;
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


    ///synchronous await
    decltype(auto) await() {
        wait();
        return await_resume();
    }

    ///synchronous await
    operator voidless_type<T> &&() {
        wait();
        return await_resume();
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
     */
    awaitable copy_value() const  {
        switch (_state) {
            default: return {};
            case value:return awaitable(std::in_place, _value);
            case exception:return awaitable(_exception);
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

    ///cancel futher execution
    /** This prevents to execute prepared asynchronous operation. You need
     * to invoke this function if you requested only non-blocking part of
     * operation and don't want to contine asynchronously
     */
    void cancel() {
        if (_owner) throw invalid_state();
        destroy_state();
        _state = no_value;
    }

    ///determines whether coroutine is running in detached mode
    /**
     * This can optimize processing when coroutine knows, that no result
     * is requested, so it can skip certain parts of its code. It still
     * needs to generate result, but it can return inaccurate result or
     * complete invalid result
     *
     * to use this function, you need call it inside of coroutine body
     * with co_await
     *
     * @code
     * awaitable<int> foo() {
     *      bool detached = co_await awaitable<int>::is_detached();
     *      std::cout << detached?"detached":"not detached" << std::endl;
     *      co_return 42;
     * }
     *
     * @return awaitable which returns true - detached, false - not detached
     */
    static typename coroutine<T>::detached_test_awaitable is_detached() {return {};}


    ///Retrieve pointer to temporary state
    /**
     * Temporary state is a user defined object which is allocated inside of awaitable
     *  during performing an asynchronous operation. It can be allocated
     * at the beginning of the asynchronous operation and must be released before the
     * result is set (or exception);
     *
     * This function returns pointer to such temporary state
     *
     * @tparam X cast the memory to given type
     * @param result valid result object
     * @return if the result variable is not set, return is nullptr. This can happen
     * if the asynchronous operation is run in detached mode. Otherwise it
     * returns valid pointer to X.
     *
     * @note When called for the first time, returned pointer points to
     * uninitialized memory. Accessing this object is UB. You need
     * to start lifetime of this object  by calling std::cosntruct_at.
     * Don't also forget to destroy this object by calling
     * std::destroy_at before you set the result.
     *
     * @note When called for the first time, it destroys a closure
     * of the callback function started to initiated asynchronous
     * function. The destruction is performed by calling
     * destructor of the closure. Ensure, that your function
     * no longer need the closure before you call this function.
     *
     * @note space reserved for the state is equal to
     * size of T (result), but never less than
     * 4x size of pointer. The function checks in compile
     * time whether the type X fits to the buffer
     */
    template<typename X>
    static X * get_temp_state(awaitable_result<T> &result) {
        auto me = result.get_handle();
        if (!me) return nullptr;
        static_assert(sizeof(X) <= callback_max_size);
        if (me->_state != no_value) {
            me->destroy_state();
            me->_state = no_value;
        }
        return reinterpret_cast<X *>(me->_callback_space);
    }

protected:

    enum State {
        ///awaitable is resolved with no value
        no_value,
        ///awaitable is resolved with a value
        value,
        ///awaitable is resolved with exception
        exception,
        ///awaitable is not resolved, a coroutine is ready to generate result once awaited
        coro,
        ///awaitable is not resolved, locally constructed callback is ready to generate result once awaited
        callback,
        ///awaitable is not resolved, dynamically constructed callback is ready to generate result once awaited
        callback_ptr
    };


    static constexpr auto callback_max_size = std::max(sizeof(void *) * 4, sizeof(store_type));

    ///current state of object
    State _state = no_value;
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
        ///holds pointer to virtual interface of callback
        std::unique_ptr<ICallback> _callback_ptr;
        ///holds reserved space for local callback
        /**@see get_local_callback() */
        char _callback_space[callback_max_size];

    };

    ///retrieves pointer to local callback (instance in _callback_space)
    /**
     * @return pointer to instance
     * @note pointer is only valid when _state == callback
     */
    ICallback *get_local_callback() {
        return reinterpret_cast<ICallback *>(_callback_space);
    }

    void dtor() {
        if (is_awaiting()) throw invalid_state();
        switch (_state) {
            default:break;
            case value: std::destroy_at(&_value);break;
            case exception: std::destroy_at(&_exception);break;
            case coro: std::destroy_at(&_coro);break;
            case callback:
                get_local_callback()->call({});
                std::destroy_at(get_local_callback());
                break;
            case callback_ptr:
                _callback_ptr->call({});
                std::destroy_at(&_callback_ptr);
                break;
        }
    }

    void destroy_state() {
        switch (_state) {
            default:break;
            case value: std::destroy_at(&_value);break;
            case exception: std::destroy_at(&_exception);break;
            case coro: _coro.cancel();std::destroy_at(&_coro);break;
            case callback: std::destroy_at(get_local_callback());break;
            case callback_ptr: std::destroy_at(&_callback_ptr);break;
        }
    }

    template<typename ... Args>
    requires(is_awaitable_valid_result_type<T, Args...>)
    void set_value(Args && ... args) {
        destroy_state();
        try {
            _state = value;
            void *trg = const_cast<std::remove_const_t<store_type> *>(&_value);
            if constexpr (sizeof...(Args) == 1 && (std::is_invocable_r_v<store_type, Args>  && ...)) {
                new(trg) store_type((...,args()));
            } else {
                new(trg) store_type(std::forward<Args>(args)...);
            }
        } catch (...) {
            _state = exception;
            std::construct_at(&_exception, std::current_exception());
        }
    }

    void set_exception(std::exception_ptr e) {
        destroy_state();
        _state = exception;
        std::construct_at(&_exception, std::move(e));
    }

    void drop() {
        destroy_state();
        _state = no_value;
    }

    prepared_coro wakeup() {
        if (!is_ready()) drop();
        return prepared_coro(std::exchange(_owner, {}));
    }

    template<typename _Callback, typename _Allocator>
    prepared_coro set_callback_internal(_Callback &&cb, _Allocator &a);



    template<bool test>
    struct read_state_frame: coro_frame<read_state_frame<test> >{
        awaitable *src;
        awaitable<bool> *result = {};

        read_state_frame(awaitable *src):src(src) {}

        void do_resume();
    };

    struct read_ptr_frame: coro_frame<read_ptr_frame>{
        awaitable *src;
        awaitable<store_type *> *result = {};

        read_ptr_frame(awaitable *src):src(src) {}

        void do_resume();
    };

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

namespace details {

///Contains function which can be called through awaitable<T>::result object
/**
 * The function works as a coroutine associated with existing awaitable, but
 * the awaitable instance is not visible for the user. You can only
 * call this function through the result object
 *
 *
 * @tparam T type of return value (can be void)
 * @tparam _CB callback function. It must accept reference to internal awaitable
 * object, where it can retrieve value
 * @tparam _Allocator can specify allocator used to allocate the function (similar to coroutine)
 *
 * @note main benefit of this class is that you can calculate size of the occupied
 * memory during compile time. This is not possible for standard coroutines. Knowing
 * the occupied size allows to reserve buffers for its allocation.
 *
 */
template<typename T, std::invocable<awaitable<T> &> _CB, coro_allocator _Allocator >
class awaiting_callback : public coro_frame<awaiting_callback<T, _CB, _Allocator> >
                        , public _Allocator::overrides
{
public:
    ///Create result object to call specified callback function
    /**
     * @param cb callback function
     * @return result object
     */
    static typename awaitable<T>::result create(_CB &&cb) {
        awaiting_callback *n = new awaiting_callback(std::forward<_CB>(cb));
        return n->_awt.create_result(n->get_handle());
    }

    ///Create result object to call specified callback function
    /**
     * @param cb callback function
     * @param alloc reference allocator instance which is used to allocate this object
     * @return result object
     */
    static typename awaitable<T>::result create(_CB &&cb, _Allocator &alloc) {
        awaiting_callback *n = new(alloc) awaiting_callback(std::forward<_CB>(cb));
        return n->_awt.create_result(n->get_handle());
    }
protected:
    ///constructor is not visible on the API
    awaiting_callback(_CB &&cb):_cb(std::forward<_CB>(cb)) {}

    ///callback function itself
    _CB _cb;
    ///awaitable object associated with the function
    awaitable<T> _awt = {nullptr};

    ///called when resume is triggered
    void do_resume() {
        try {
            _cb(_awt);
        } catch (...) {
            async_unhandled_exception();
        }
        do_destroy();
    }
    void do_destroy() {
        delete this;
    }

    friend coro_frame<awaiting_callback<T, _CB, _Allocator> >;
};

}

template <typename T>
template <typename _Callback, typename _Allocator>
inline prepared_coro awaitable<T>::set_callback_internal(_Callback &&cb, _Allocator &a)
{
    prepared_coro out = {};


    if (await_ready()) {
        cb(*this);
    } else {

        auto res = details::awaiting_callback<T, _Callback, _Allocator>::create(std::forward<_Callback>(cb), a);
        if (_state == callback) {
            out = get_local_callback()->call(std::move(res));
        } else if (_state == callback_ptr) {
            out =_callback_ptr->call(std::move(res));
        } else if (_state == coro) {
            out = _coro.start(std::move(res));
        }
        cancel();
    }
    return out;
}


template<typename T>
template<bool test>
void awaitable<T>::read_state_frame<test>::do_resume() {
           static_assert(std::is_trivially_destructible_v<read_state_frame>);
           bool n = src->_state != no_value;
           awaitable<bool>::result(this->result)(n == test);
}

template<typename T>
void awaitable<T>::read_ptr_frame::do_resume() {
    static_assert(std::is_trivially_destructible_v<read_ptr_frame>);
    typename awaitable<store_type *>::result r(this->result);
    if (src->_state == value) {
        r(&src->_value);
    } else if (src->_state == exception) {
        r = src->_exception;
    } else {
        r(&src->_value+1);
    }
}

template<typename T>
awaitable<bool> awaitable<T>::operator!()  {
    if (await_ready()) {return _state == no_value;}
    return [this](awaitable<bool>::result r) mutable -> prepared_coro {
        auto frm =awaitable<bool>::get_temp_state<read_state_frame<false> >(r);
        if (!frm) return {};
        std::construct_at(frm, this);
        frm->result = r.release();
        return frm->src->await_suspend(frm->get_handle());
    };

}
template<typename T>
awaitable<bool> awaitable<T>::has_value() {
    if (await_ready()) {return _state != no_value;}
    return [this](awaitable<bool>::result r) mutable -> prepared_coro {
        auto frm =awaitable<bool>::get_temp_state<read_state_frame<true> >(r);
        if (!frm) return {};
        std::construct_at(frm, this);
        frm->result = r.release();
        return frm->src->await_suspend(frm->get_handle());
    };
}

template<typename T>
awaitable<typename awaitable<T>::store_type *> awaitable<T>::begin() {
    if (await_ready()) {
        if (_state == exception) std::rethrow_exception(_exception);
        return &_value;
    }
    return [this](awaitable<store_type *>::result r) mutable -> prepared_coro{
        auto frm = awaitable<store_type* >::template get_temp_state<read_ptr_frame>(r);
        if (!frm) return {};
        std::construct_at(frm, this);
        frm->result = r.release();
        return frm->src->await_suspend(frm->get_handle());
    };
}


template<typename T>
inline void awaitable<T>::wait() {
    if (!await_ready()) {
        sync_frame sync;
        await_suspend(sync.get_handle()).resume();
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
