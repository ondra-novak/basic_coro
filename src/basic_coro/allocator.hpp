#pragma once

#include "concepts.hpp"
#include "exceptions.hpp"

namespace coro {

///represents standard allocator
/**
 * Some templates uses this class as placeholder for standard allocation, however it can be used
 * as any other allocator
 */
class objstdalloc {
public:
    struct overrides {
        template<typename ... Args>
        void *operator new(std::size_t sz, Args && ...) {
            return ::operator new(sz);
        }
        template<typename ... Args>
        void operator delete(void *ptr, Args && ...) {
            ::operator delete(ptr);
        }
        void operator delete(void *ptr, std::size_t) {
            ::operator delete(ptr);
        }
    };
};


static_assert(coro_allocator<objstdalloc>);


///holds a memory which can be reused for coroutine frame
/**
 * The allocator can hold one coroutine at time. You should avoid
 * multiple active coroutines. This is not checked, so result of
 * such action is undefined behaviour
 *
 * Main purpose of this function is to keep memory allocated if
 * the coroutines are called in a cycle. Allocation is costly, so
 * reusing existing memory increases performance
 *
 * To use this allocator, coroutine must be declared with  this allocator
 *
 * @code
 * coroutine<T, reusable_allocator> my_coro(reusable_allocator&, addition_args...)
 * @endcode
 *
 * The instance of the allocator MUST be included in argument list even
 * if the actuall instance is not used in the code of the coroutine. You
 * can freely choose argument position, but there must be exactly
 * one reference to the allocator. The reference points to
 * actuall instance of the allocator which holds the preallocated memory
 *
 *
 */
class reusable_allocator {
public:

    reusable_allocator() = default;
    reusable_allocator(const reusable_allocator &) = delete;
    reusable_allocator &operator=(const reusable_allocator &) = delete;
    ~reusable_allocator() {::operator delete(_buffer);}

   struct overrides {

        template<typename ... Args>
        requires((std::is_same_v<reusable_allocator &, Args> ||...))
        void *operator new(std::size_t sz, Args && ... args) {

            reusable_allocator *me;
            auto finder = [&](auto &&k) {
                if constexpr(std::is_same_v<decltype(k),reusable_allocator &>) me = &k;
            };
            (finder(args),...);
            if (me) {
                if (me->_buffer_size < sz) {
                    ::operator delete(me->_buffer);
                    me->_buffer = ::operator new(sz);
                    me->_buffer_size = sz;
                }
                return me->_buffer;
            } else {
                throw invalid_state();
            }
        }

        void operator delete (void *, std::size_t) {}
    };

protected:
    void *_buffer = {};
    std::size_t _buffer_size = 0;
};

static_assert(coro_allocator<reusable_allocator>);


}