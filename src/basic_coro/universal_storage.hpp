#pragma once

#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>


template<typename T>
class universal_storage_access;

class universal_storage_base {
protected:
    void (*deleter)(void *) = nullptr;

    bool has_value() const {
        return deleter != nullptr;
    }
    void clear(void *where) {
        if (deleter) deleter(where);
        deleter = nullptr;
    }

    template<typename T>
    friend class universal_storage_access;
};

template<std::size_t N>
class universal_storage : public universal_storage_base{
public:
    universal_storage() = default;
    ~universal_storage() {clear();}
    void clear() {
        if (this->deleter) this->deleter(space);
        deleter = nullptr;
    }

    static constexpr std::size_t size() {return N;}

    template<typename T>
    std::add_pointer_t<std::add_const_t<T> > as() const {
        return std::launder(reinterpret_cast<std::add_pointer_t<std::add_const_t<T> > >(space));
    }

    template<typename T>
    std::add_pointer_t<T> as()  {
        return std::launder(reinterpret_cast<std::add_pointer_t<T> >(space));
    }

protected:
    alignas(std::max_align_t) char space[N];

    universal_storage(const universal_storage &) = delete;
    universal_storage &operator=(const universal_storage &) = delete;

    template<typename T>
    friend class universal_storage_access;
};

template<typename T>
class universal_storage_access {
public:

    template<std::size_t sz>
    universal_storage_access(universal_storage<sz> &stor)
        :stor_base(&stor),ptr(stor.template as<T>()) {}
    

    template<typename ... Args>
    requires(std::is_constructible_v<T, Args...>)
    T *emplace(Args && ... args) {
        stor_base->clear(ptr);
        std::construct_at(ptr, std::forward<Args>(args)...);
        stor_base->deleter = [](void *p){
            T *t = std::launder(reinterpret_cast<T *>(p));
            std::destroy_at(t);
        };
        return ptr;
    }

    T &operator *() const {return *ptr;}
    T *operator ->() const {return ptr;}
    
protected:
    universal_storage_base *stor_base;
    T *ptr;   
};