#pragma once

#include "await_proxy.hpp"
#include "concepts.hpp"
#include "coro_frame.hpp"
#include "awaitable.hpp"
#include "prepared_coro.hpp"
#include "universal_storage.hpp"
#include <cstdint>
#include <exception>
#include "safe_invoke.hpp"
#include <optional>
#include <type_traits>

namespace coro {

namespace _details {

    template<typename T, typename ... Uvs>
    struct CalcSizeHelper {
        T _head;
        CalcSizeHelper<Uvs...> _tail;
    };

    template<typename T>
    struct CalcSizeHelper<T> {
        T _head;
    };


}


///Handles transformation of awaitable to another awaitable with different result type. It can be used to implement operators like then, finally, etc. 
/**
    * @tparam Awt expected awaitable type
    * @tparam Closure types of closure arguments (like pred in then() operator)
    All above types are used to calculate required space. The actual objects can be different during asynchronous call, but must fit in the space.
    @note the object must be alive during asynchronous call, so it is not recommended to create temporary object of this type. It is better to create one and reuse it for multiple calls. 
    The object is not MT safe nor reentrant, so it is not recommended to use it in multiple threads or for recursive calls.
    The best place for this object is as a member of an object offering an asynchronous interface.

    Usage:
    @code
        awaitable_transform<int, some_awaitable, some_closure> tr; //declare as member
        //...
        auto new_awaitable = tr(async_call(), [this](auto result){return transform_result(result);});
    @endcode    



 */
template<is_awaitable Awt, typename ... Closure>
class awaitable_transform;

///Same as `awaitable_transform` but you can specify ResultType template which used instead awaitable_result<T>. 
/**
 @tparam ResultType - template with one argument T of returning type used to carry promise for capture asynchronous result.
 It must act as callback which has compatible API with awaitable_result. This allows to implement special processing, such
 a routing execution through a dispatcher
 @param Awt expected awaiter
 @param Closure expected closure types
*/
template<template <typename> class ResultType, is_awaitable Awt,  typename ... Closure>
class awaitable_transform_r {


public:

    ///default constructible
    awaitable_transform_r() = default;
    ///non copyable
    awaitable_transform_r(const awaitable_transform_r &) = delete;
    ///non copy assignable
    awaitable_transform_r &operator=(const awaitable_transform_r &) = delete;

    ///perform transformation
    /**
     * @param awt awaitable (any suitabable awaiter compatible with co_await expression)
     * @param pred closure to perform transformation. It is called with result of awaitable as argument.
                   It returns transformed value. The transformed value can be different type.
                   It can also return awaitable of transformed value, which causes additional waiting
                   for asynchronous result. It is also possible to chain transformations.
                   Inside of predicate, you can call this operator recursively. Keep in mind, that
                   storage for the closure is still in the frame, so reusing it cause destruction
                   of the closure prematurely
                   
                   The predicate is not called, when awaitable is resolved with nullopt or exception

     * @return awaitable with result of type T, which is the result of transformation
     * @note it is possible to call this object recursively in the predicate. When callback is called, internal
            state is already clean, and ready to process next request
     */
    template<typename _Awt, std::invocable<awaiter_result<_Awt> > _Pred>
    auto operator()(_Awt &&awt, _Pred &&pred) {
        static_assert(!std::is_lvalue_reference_v<_Awt>, "You must pass awaiter as r-value reference (must be movable)");
        //contains result of the predicate
        //this result is returned - but if it is not awaitable, we must wrap it into awaitable
        using PredResult = std::invoke_result_t<_Pred, awaiter_result<_Awt> >;
        //if PredResult is just a value, wrap it into awaitable
        using Result = std::conditional_t<is_awaiter<PredResult>, PredResult, awaitable<PredResult> >;
        //explore state of the awaiter - is it ready?
        if (awt.await_ready()) {
            //synchronous approach
            //note: we need to handle exceptions
            try {
                //check, whether awaiter has value (must have a functio has_value())
                if constexpr(has_has_value<_Awt>) {
                    //if not return nullopt
                    if (!awt.has_value()) return Result(std::nullopt);
                }
                //retrieve result
                decltype(auto) r = awt.await_resume();
                //forward result to predicate. It doesn't matter whether result is awaitable or direct value, this should be converted
                return Result(std::invoke(std::forward<_Pred>(pred), std::forward<decltype(r)>(r)));                

            //this exception appears when awaiter was empty
            } catch (...) {
                //in case of exception, return 
                return Result(std::current_exception());
            }            
            //this branch ends by return            
            throw;
        }
        static_assert(sizeof(_frame._awt) >= sizeof(_Awt), "Awaiter is too large" );
        static_assert(sizeof(_frame._closure) >= sizeof(_Pred), "Closure of transform function is too large" );

        //move arguments to frame
        auto awtptr = universal_storage_access<_Awt>(_frame._awt);
        auto predptr = universal_storage_access<_Pred>(_frame._closure);;

        awtptr.emplace(std::move(awt));
        predptr.emplace(std::move(pred));

        //prepare callback for asynchronous call - called when async operation is initiated
        //frame pointer lives in closure
        //if closure is destroyed, the cleanup is performed
        return Result([this]<typename Promise>(Promise promise) mutable -> prepared_coro {                
            //retrieve frame pointer and disable autocleanup operation - cancel is not possible now
            auto frm = &this->_frame;

            using ResultPromise = ResultType<typename Promise::value_type>;

            static_assert(sizeof(frm->_result) >= sizeof(Promise), "Result of awaitable is unexpectly large");
            //initialize result
            auto resptr = universal_storage_access<ResultPromise>(frm->_result);
            //initialize awaiter
            auto awtptr = universal_storage_access<_Awt>(frm->_awt);
            //store result to frame
            resptr.emplace(std::move(promise));            

            frm->on_complete  = [](frame *frm) -> prepared_coro {
                //retrieve all object - launder to break aggresive optimization
                auto result = std::move(*universal_storage_access<ResultPromise>(frm->_result));
                auto awtptr = universal_storage_access<_Awt>(frm->_awt);
                auto predptr = universal_storage_access<_Pred>(frm->_closure);                    

                try {
                    if constexpr(has_has_value<_Awt>) {
                        //check whether there is an result - if not - just forward nullopt
                        if (!awtptr->has_value()) {
                            //set result and exit (auto cleanup)
                            return invoke_force_result<prepared_coro>(result, std::nullopt);
                        }
                        //now we know, there is a value
                    }
                    //retrieve result - exception can be thrown
                    auto awtresult = awtptr->await_resume();
                    
                    //call the predicate with result
                    auto finres = std::invoke(std::forward<_Pred>(*predptr), std::forward<decltype(awtresult)>(awtresult));                
                    //now the result can be a value, if it is this case, we are done
                    if constexpr(!is_awaiter<PredResult>) {
                            //set result and exit (auto cleanup)
                            return invoke_force_result<prepared_coro>(result, std::move(finres));
                    } else {
                        //forward result to async operation behind returned awaiter and return handle to newly created corotuine                        
                        return finres.forward(std::move(result));
                    }

                } catch (...) {
                    //set exception and exit
                    return invoke_force_result<prepared_coro>(result, std::current_exception());
                }
                
            };

            //everything ready, now initiate the suspend operation on the awaiter
            return call_await_suspend(*awtptr, frm->create_handle());
        });
    }

    static constexpr std::size_t align_up(std::size_t size, std::size_t alignment) {
        return (size + alignment - 1) / alignment * alignment;
    }

    static constexpr std::size_t closure_alignment = std::alignment_of_v<_details::CalcSizeHelper<Closure ...> >;
    static constexpr std::size_t awt_alignment = std::alignment_of_v<Awt >;    
    static constexpr std::size_t awaiter_size = align_up(sizeof(Awt),std::max(closure_alignment, awt_alignment));
    static constexpr std::size_t closure_size = sizeof(_details::CalcSizeHelper<Closure ...>);
    static constexpr std::size_t result_size = sizeof(ResultType<std::uintmax_t>);

    //emulates coroutine frame - act as coroutine, calls resume()
    struct frame: public coro_frame<frame> {
        
        prepared_coro (*on_complete)(frame *me) = nullptr;
        universal_storage<result_size> _result;
        universal_storage<awaiter_size> _awt;
        universal_storage<closure_size> _closure;

        prepared_coro do_resume() {
            return on_complete(this);
        }
        void do_destroy() {do_resume();}
    };



protected:    
    frame _frame;
};

template<is_awaitable Awt, typename ... Closure>
class awaitable_transform: public awaitable_transform_r<awaitable_result,Awt,  Closure...> {};


}