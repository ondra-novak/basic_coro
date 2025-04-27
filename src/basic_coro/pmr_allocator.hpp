#pragma once

#include "allocator.hpp"
#include <memory_resource>

namespace coro {

    ///enables pmr (memory_resource) to be used for coroutine allocation
    /**
     * You need to include pmr_allocator & (reference) as an argument of a coroutine
     * Ensure that instance of specified memory resource is valid whole time when
     * it is used by coroutines
     * 
     * @tparam PtrType specified how the pointer to memory resource is held. You 
     * can change it std::shared_ptr<std::pmr::memory_resource> is you wish
     * better lifetime control. The memory resource is destroyed when 
     * last coroutine is deallocated
     */
    template<typename PtrType = std::pmr::memory_resource *>
    class pmr_allocator {
    public:
        pmr_allocator():_mem_res(std::pmr::get_default_resource()) {}
        pmr_allocator(PtrType res):_mem_res(res) {}

        struct overrides {
            template<typename ... Args>
            requires((std::is_same_v<pmr_allocator, std::decay_t<Args> > ||...))
            void *operator new(std::size_t sz, Args && ... args) {
                pmr_allocator *me = get_first_arg_of_type<pmr_allocator>(args...);
                return me->allocate(sz);
            }
            void operator delete(void *ptr, std::size_t sz) {
                pmr_allocator *me = reinterpret_cast<pmr_allocator *>(ptr_plus_bytes(ptr, sz));
                me->deallocate(ptr, sz);
            }
        };



    protected:
        PtrType _mem_res;

        void *allocate(std::size_t sz) {
            void *r = _mem_res->allocate(sz+sizeof(pmr_allocator));
            new(ptr_plus_bytes(r, sz)) pmr_allocator(*this);
            return r;
        }

        void deallocate(void *ptr, std::size_t sz) {
            auto m = std::move(_mem_res);
            std::destroy_at(this);
            m->deallocate(ptr, sz+sizeof(pmr_allocator));
        }

    };


}