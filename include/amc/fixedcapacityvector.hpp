#pragma once

#include <cassert>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "vectorcommon.hpp"

namespace amc {
namespace vec {

/// Wrapper to get the smallest needed type for holding a maximum number of N elements
template <uintmax_t N>
struct SmallestSizeType {
  // clang-format off
#ifdef AMC_CXX14
// this is to avoid potential harmless warning occurring for instance in GCC:
// warning: comparison is always false due to limited range of data type [-Wtype-limits]
// Activated only in C++14 as we need it to be constexpr
  using type = typename std::conditional<std::less_equal<std::size_t>()(N, std::numeric_limits<uint8_t>::max()), uint8_t,
               typename std::conditional<std::less_equal<std::size_t>()(N, std::numeric_limits<uint16_t>::max()), uint16_t,
               typename std::conditional<std::less_equal<std::size_t>()(N, std::numeric_limits<uint32_t>::max()), uint32_t,
          uint64_t>::type>::type>::type;
#else
  using type = typename std::conditional<N <= std::numeric_limits<uint8_t>::max(), uint8_t,
               typename std::conditional<N <= std::numeric_limits<uint16_t>::max(), uint16_t,
               typename std::conditional<N <= std::numeric_limits<uint32_t>::max(), uint32_t,
          uint64_t>::type>::type>::type;
#endif
  // clang-format on
};

/// Throw exception in the case we attempt to exceed inplace capacity (default mode)
struct ExceptionGrowingPolicy {
  static inline void Check(uintmax_t capacity, uintmax_t maxCapacity) {
    if (AMC_UNLIKELY(maxCapacity < capacity)) {
      throw std::out_of_range("Growing is not possible");
    }
  }
};

/// Abort the program in the case we attempt to exceed inplace capacity if assertions are enable only
struct UncheckedGrowingPolicy {
  static inline void Check(uintmax_t capacity, uintmax_t maxCapacity) {
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
 * In addition, size_type can be configured and is the smallest unsigned integral type able to hold N elements.
 *
 * Exception safety:
 *   It provides at least Basic exception safety.
 *   If Object movement is noexcept, most operations provide strong exception safety, excepted:
 *     - assign
 *     - insert from InputIt
 *     - insert from count elements
 *   If Object movement can throw, only 'push_back' and 'emplace_back' modifiers provide strong exception warranty
 */
template <class T, uintmax_t N, class GrowingPolicy = vec::ExceptionGrowingPolicy,
          class SizeType = typename vec::SmallestSizeType<N>::type>
using FixedCapacityVector = Vector<T, vec::EmptyAlloc, SizeType, GrowingPolicy, vec::SanitizeInlineSize<N, SizeType>()>;

}  // namespace amc
