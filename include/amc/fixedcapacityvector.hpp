#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <new>
#include <stdexcept>

#include "config.hpp"
#include "vectorcommon.hpp"

namespace amc {
namespace vec {

/// Throw exception in the case we attempt to exceed inplace capacity (default mode)
struct ExceptionGrowingPolicy {
  static inline void Check(std::uintmax_t capacity, std::uintmax_t maxCapacity) {
    if (AMC_UNLIKELY(maxCapacity < capacity)) {
      AMC_THROW(std::out_of_range("Growing is not possible"));
    }
  }
};

/// Throw std::bad_alloc in the case we attempt to exceed inplace capacity (same as std::inplace_vector)
struct BadAllocGrowingPolicy {
  static inline void Check(std::uintmax_t capacity, std::uintmax_t maxCapacity) {
    if (AMC_UNLIKELY(maxCapacity < capacity)) {
      AMC_THROW(std::bad_alloc());
    }
  }
};

/// Abort the program in the case we attempt to exceed inplace capacity if assertions are enable only
struct UncheckedGrowingPolicy {
  static inline void Check(std::uintmax_t capacity, std::uintmax_t maxCapacity) {
    (void)capacity;
    (void)maxCapacity;
    assert(capacity <= maxCapacity);
  }
};
}  // namespace vec

/**
 * Vector-like container whose maximum number of elements cannot exceed N.
 *
 * It does not make dynamic memory allocations which, in addition of the obvious performance boost,
 * provides the following benefits:
 *  - begin() is never invalidated
 *  - iterators before any insert / erase are never invalidated
 *  - reserve / shrink_to_fit are not needed (calling the first one is a capacity check, second one a no-op)
 *  - vector is *trivially destructible* if T is
 *
 * If new element is about to be inserted in a full Vector, behavior can be controlled thanks to provided
 * 'GrowingPolicy':
 *  - ExceptionGrowingPolicy: throw 'std::out_of_range' exception (default)
 *  - BadAllocGrowingPolicy: throw 'std::bad_alloc' exception (like std::inplace_vector)
 *  - UncheckedGrowingPolicy: assert check (nothing is done in Release, invoking undefined behavior, abort will be
 *                            called in Debug).
 *
 * API is compliant to C++17 std::vector, with the following extra methods:
 *  - append: shortcut for myVec.insert(myVec.end(), ..., ...)
 *  - pop_back_val: pop_back and return the popped value.
 *  - swap2: generalized version of swap usable for all vectors of this library.
 *           you could swap values from a amc::vector with a FixedCapacityVector for instance,
 *           all combinations are possible between the 3 types of vectors.
 *           Note that implementation is not noexcept, adjust capacity needs to be called for both operands.
 *
 * In addition, size_type can be configured and is the smallest unsigned integral type able to hold N elements (with a
 * larger one, like the std::size_t of inplace_vector, the size and capacity are still stored in this smallest type, but
 * at least in a uint16_t, which does not alias the elements).
 * Like with std::vector, the counts taken by the methods (constructors, assign, insert, resize, reserve...) are of type
 * size_type: a larger count is converted to it by the call (wrapping around for an unsigned type), before the vector
 * can check it against its capacity. For instance, FixedCapacityVector<int, 10>::reserve(260) reserves 4 elements
 * (uint8_t size_type) and does not throw.
 *
 * Exception safety:
 *   It provides at least Basic exception safety.
 *   If Object movement is noexcept, most operations provide strong exception safety, excepted:
 *     - assign
 *     - insert from InputIt
 *     - insert from count elements
 *   If Object movement can throw, only 'push_back' and 'emplace_back' modifiers provide strong exception warranty
 */
template <class T, std::uintmax_t N, class GrowingPolicy = vec::ExceptionGrowingPolicy,
          class SizeType = typename vec::SmallestSizeType<N>::type>
using FixedCapacityVector =
    Vector<T, vec::EmptyAlloc, SizeType, GrowingPolicy, vec::SanitizeInlineSize<N, SizeType>::value>;

/// Compatibility with std::inplace_vector (C++26)
template <class T, std::uintmax_t N>
using inplace_vector = FixedCapacityVector<T, N, vec::BadAllocGrowingPolicy, std::size_t>;

}  // namespace amc
