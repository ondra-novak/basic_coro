#pragma once
#include <memory_resource>
#include "allocator.hpp"

namespace coro {

/** flat_stack_memory_resource is a custom memory resource based on std::pmr::memory_resource.
 * It implements a flat stack-like memory allocation strategy, optimized for coroutine usage.
 * 
 * A large memory block is preallocated upfront. Allocations within this block are handled 
 * by sequentially advancing a top pointer. 
 *
 * - When memory is allocated, it is taken from the current top, and the top is moved forward.
 * - When memory is deallocated from the top of the stack, the top pointer simply moves back, 
 *   reclaiming the memory immediately.
 * - If memory is deallocated from the middle of the stack (not the current top), the block is 
 *   marked as free but the top pointer is not moved.
 * - When subsequent deallocations remove all allocations above a previously freed block, 
 *   the top pointer moves backward over the freed space as well, effectively coalescing free space.
 *
 * Example sequence:
 *   - alloc a, alloc b, alloc c, alloc d
 *   - dealloc d → d is freed and the top pointer moves back
 *   - dealloc b → b is marked as free, but the top remains the same
 *   - dealloc c → c is freed, and since b was already free, both c and b are reclaimed 
 *     and the top pointer moves back accordingly.
 *
 * This memory resource is particularly efficient when allocations and deallocations happen
 * in mostly stack-like (LIFO) order, but it can also handle slight deviations gracefully.
 */

class flat_stack_memory_resource_extendable: public std::pmr::memory_resource {
public:

    static constexpr std::size_t block_size = sizeof(std::size_t);

    static constexpr std::size_t to_blocks(std::size_t bytes) {
        return (bytes+block_size-1)/block_size;
    }

    static constexpr std::size_t to_bytes(std::size_t blocks) {
        return (blocks * block_size);
    }
    
    /// construct object
    /**
     * @param size size of reserver memory for stack - in bytes. Note the actual size is extended to 
     * largest basic align, which is sizeof(std::size_t)
     * 
     * @param res memory resource used to allocate this memory
     */
    flat_stack_memory_resource_extendable(std::size_t size, std::pmr::memory_resource *res):_mres(res) {
        auto blks = to_blocks(size);
        _mem = reinterpret_cast<size_t *>(_mres->allocate(blks*block_size));
        _count = blks;
        _top = 0;
    }

    /// construct object which uses existing preallocated block    
    /**
     * @param preallocated_memory_ptr pointer to already preallocated memory block
     * @param size_in_bytes size in bytes of preallocated memory block. Note the size is rouned down
     * to near align of type std::size_t 
     * 
     * @note preallocated block is not freed while destruction
     */
    flat_stack_memory_resource_extendable(void *preallocated_memory_ptr, std::size_t size_in_bytes):_mres(nullptr) {
        _mem = reinterpret_cast<size_t *>(preallocated_memory_ptr);
        _count = size_in_bytes/sizeof(std::size_t);
        _top = 0;
    }

    /// construct object allocates memory
    /**
     * @param size size in bytes
     */
    explicit flat_stack_memory_resource_extendable(std::size_t size):flat_stack_memory_resource_extendable(size, std::pmr::get_default_resource()) {}


    ~flat_stack_memory_resource_extendable() {
        if (_mres) _mres->deallocate(_mem, _count*block_size);
    }

    flat_stack_memory_resource_extendable(const flat_stack_memory_resource_extendable &) = delete;
    flat_stack_memory_resource_extendable &operator=(const flat_stack_memory_resource_extendable &) = delete;


protected:
    virtual void* do_allocate(size_t bytes, size_t alignment) {
        std::size_t align_in_blocks = to_blocks(alignment);
        std::size_t aextra = (align_in_blocks - (_top % align_in_blocks)) % align_in_blocks;
        std::size_t needsz = to_blocks(bytes)+aextra+1;
        auto curtop = _top;
        if (curtop + needsz > _count) throw std::bad_alloc();

        void *r = _mem+curtop+aextra;
        std::size_t *m = _mem+curtop+needsz-1;
        *m = needsz << 1;
        _top += needsz;
        return r;
    }

    virtual void do_deallocate(void* p, size_t bytes, size_t ) {
        std::size_t pos = to_blocks(reinterpret_cast<char *>(p) - reinterpret_cast<char *>(_mem));
        std::size_t sep = pos + to_blocks(bytes);
        _mem[sep] |= 1;
        cleanup();
    }

    void cleanup() {
        while (_top > 0) {
            std::size_t sep = _mem[_top-1];
            std::size_t sz = sep >> 1;
            if (sep & 0x1) {
                _top -= sz;
            } else {
                break;
            }
        }
    }

    virtual bool do_is_equal(const memory_resource& other) const noexcept {
        return this == &other;
    }

    std::pmr::memory_resource *_mres = nullptr;
    size_t *_mem = nullptr;
    size_t _count = 0;
    size_t _top = 0;
};

/** flat_stack_memory_resource is a custom memory resource based on std::pmr::memory_resource.
 * It implements a flat stack-like memory allocation strategy, optimized for coroutine usage.
 * 
 * A large memory block is preallocated upfront. Allocations within this block are handled 
 * by sequentially advancing a top pointer. 
 *
 * - When memory is allocated, it is taken from the current top, and the top is moved forward.
 * - When memory is deallocated from the top of the stack, the top pointer simply moves back, 
 *   reclaiming the memory immediately.
 * - If memory is deallocated from the middle of the stack (not the current top), the block is 
 *   marked as free but the top pointer is not moved.
 * - When subsequent deallocations remove all allocations above a previously freed block, 
 *   the top pointer moves backward over the freed space as well, effectively coalescing free space.
 *
 * Example sequence:
 *   - alloc a, alloc b, alloc c, alloc d
 *   - dealloc d → d is freed and the top pointer moves back
 *   - dealloc b → b is marked as free, but the top remains the same
 *   - dealloc c → c is freed, and since b was already free, both c and b are reclaimed 
 *     and the top pointer moves back accordingly.
 *
 * This memory resource is particularly efficient when allocations and deallocations happen
 * in mostly stack-like (LIFO) order, but it can also handle slight deviations gracefully.
 * 
 * @note this class is final, which helps to pmr_allocator to avoid usage of virtual functions.
 */

class flat_stack_memory_resource final: public flat_stack_memory_resource_extendable {
public:
    using flat_stack_memory_resource_extendable::flat_stack_memory_resource_extendable;
    
    void* allocate(size_t __bytes, size_t __alignment  = alignof(std::max_align_t)){ 
        return ::operator new(__bytes, flat_stack_memory_resource_extendable::do_allocate(__bytes, __alignment)); 
    }

    void deallocate(void* __p, size_t __bytes, size_t __alignment = alignof(std::max_align_t))  { 
        return flat_stack_memory_resource_extendable::do_deallocate(__p, __bytes, __alignment); 
    }
};

}