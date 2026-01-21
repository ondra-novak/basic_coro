#include "coro_frame.hpp"

namespace coro {

///Defer execution of the function
/**
 * Defers the execution of the given callable until the end of the current defer context.
 * If no defer context is active, one is created and the function is executed immediately.
 * Any additional defers invoked during the execution of the function will be properly deferred.
 *
 * @param fn A callable with no arguments to be deferred. For trivially copy-constructible callables,
 *           no heap allocation is performed; otherwise, the callable is stored on the heap.
 *
 * @note The defer context is thread-local and is also shared with coroutine's lazy_resume(),
 *       allowing coroutines to be enqueued in the defer context.
 *
 * @see is_defer_context_active, enter_defer_context
 */
template<std::invocable<> Fn>
void defer(Fn &&fn) {
    using DFn = std::decay_t<Fn>;
    auto &ctx = defer_context::get_instance();

    if constexpr(std::is_trivially_copy_constructible_v<Fn>) {
        constexpr std::size_t map_size =  (sizeof(DFn)+sizeof(void *)-1)/sizeof(void *);
        alignas(std::max_align_t) void *  vmap[map_size];
        std::copy(reinterpret_cast<const char *>(&fn), 
                  reinterpret_cast<const char *>(&fn)+sizeof(DFn), 
                  reinterpret_cast<char *>(vmap));
        static constexpr coro_frame_cb frame([]{
            constexpr std::size_t map_size =  (sizeof(DFn)+sizeof(void *)-1)/sizeof(void *);
            alignas(std::max_align_t) void *vmap[map_size];
            auto &ctx = defer_context::get_instance();
            for (auto &m: vmap) m = ctx.pop_in_flush();
            Fn &fn = *reinterpret_cast<Fn *>(vmap);
            fn();
            fn.~Fn();
        });

        bool is_active = ctx.is_active();

        ctx.push(const_cast<std::decay_t<decltype(frame)> &>(frame).create_handle());
        for (const auto &m: vmap) ctx.push(m);

        if (!is_active) {
            defer_context::get_instance().flush();
        }
    
   } else {
        return defer([ptr = new DFn(std::forward<Fn>(fn))]{
            (*ptr)();
            delete ptr;
        });
   }

}

///Determines whether there is active defer context
/**
 * Each thread has its own defer context
 * 
 * @retval true defer context is active
 * @retval false no defer context is active
 */
inline bool is_defer_context_active() {
    return defer_context::get_instance().is_active();
}

///Creates defer context and calls function
/**
 * The function creates defer context and calls function into it. If the defer
 * context is already active, it just calls the function
 * 
 * 
 * @param fn function (a callable) to be called
 * 
 * 
 */
template<std::invocable<> Fn>
void enter_defer_context(Fn &&fn) {
    auto &ctx = defer_context::get_instance();
    if (ctx.is_active()) fn();
    else defer(std::forward<Fn>(fn));

}



}