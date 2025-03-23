#pragma once
#include <coroutine>
#include <memory>
#include <optional>

namespace coro {

#define CRPT_virtual
#define CRPT_override

///A format of generic coroutine frame
/**
 * A valid coroutine frame can be converted to coroutine_handle even if it
 * is not coroutine. It mimics the coroutine. Implementation must inherid this
 * struct and specific self as template argument (CRTP)
 * @tparam FrameImpl name of class implementing frame. The class must
 * declared a function do_resume(), which is called when the handle is
 * used for resumption
 *
 * if someone calls destroy() delete is called
 */
template<typename FrameImpl>
class basic_coro_frame {
protected:
    void (*resume)(std::coroutine_handle<>) = [](std::coroutine_handle<> h) {
        auto me = reinterpret_cast<basic_coro_frame *>(h.address());
        static_cast<FrameImpl *>(me)->do_resume();
    };
    void (*destroy)(std::coroutine_handle<>) = [](std::coroutine_handle<> h) {
        auto *me = reinterpret_cast<basic_coro_frame *>(h.address());
        static_cast<FrameImpl *>(me)->do_destroy();
    };

    ///default implementation. Implement own version with a code to perform
    CRPT_virtual void do_resume()  {}
    ///default implementation. It calls delete. You can overwrite this behaviour
    CRPT_virtual void do_destroy() {
        auto *me = static_cast<FrameImpl *>(this);
        delete me;
    }

public:
    ///create new handle for this coroutine frame
    /** Each created handle should be properly used for resumption.
     * Do not discard the handle value. */
    std::coroutine_handle<> create_handle() {
        return std::coroutine_handle<>::from_address(this);
    }
};

///emmulates coro_frame by a auxularity coroutine
/** 
 * This implementation is intended for compilers, that doesn't implement well known coroutine frame layout
 * Note that there are differences. You cannot set_done() such coroutine and each get_handle returns
 * different handle, which should be used for either triggering action, or must be destroyed
 */
template<typename FrameImpl>
class emulated_coro_frame {

    struct auto_destroy_deleter{
        void operator()(FrameImpl *p) {
            p->do_destroy();
        }
    };

    using autodestroy_ptr = std::unique_ptr<FrameImpl, auto_destroy_deleter>;

    class adhoccoro {
    public:

        struct promise_type {
            std::suspend_always initial_suspend() noexcept {return {};}
            std::suspend_never final_suspend() noexcept {return {};}
            adhoccoro get_return_object() {return this;}
            void unhandled_exception() {}
            void return_void() {}
        };

        adhoccoro(promise_type *p):_p(p) {}
        std::coroutine_handle<> get_handle() const {
            return std::coroutine_handle<promise_type>::from_promise(*_p);
        }
    protected:
        promise_type *_p;
    };

    static adhoccoro coro(autodestroy_ptr p) {
        FrameImpl *x = p.release();
        x->do_resume();
        co_return;
    }

    ///default implementation. Implement own version with a code to perform
    CRPT_virtual void do_resume()  {}
    ///default implementation. It calls delete. You can overwrite this behaviour
    CRPT_virtual void do_destroy() {
        auto *me = static_cast<FrameImpl *>(this);
        delete me;
    }
public:

    ///create new handle for this coroutine frame
    /** Each created handle should be properly used for resumption.
     * Do not discard the handle value. */
    std::coroutine_handle<> create_handle() {
        auto c = coro(autodestroy_ptr(static_cast<FrameImpl *>(this)));
        return c.get_handle();
    }
};

#ifdef BASIC_CORO_USE_COMPATIBLE_CORO_FRAME
template<typename FrameImpl> using coro_frame = emulated_coro_frame<FrameImpl>;
#else
template<typename FrameImpl> using coro_frame = basic_coro_frame<FrameImpl>;
#endif

template<typename CB>
class coro_frame_cb: public coro_frame<coro_frame_cb<CB> > {
public:

    template<std::convertible_to<CB> X>
    coro_frame_cb(X &&x):_cb(std::forward<X>(x)) {}

protected:
    std::optional<CB> _cb;

    auto do_resume() {
        return (*_cb)();
    }
    auto do_destroy() {
        _cb.reset();
    }

    friend coro_frame<coro_frame_cb<CB> >;
};

template<typename CB>
coro_frame_cb(CB) -> coro_frame_cb<CB>;

}


