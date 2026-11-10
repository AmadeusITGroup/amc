#pragma once

#include <cstdint>

#include "allocator.hpp"
#include "smallvector.hpp"

namespace amc {

/**
 * Vector optimized for *trivially relocatable types* (see amc_type_traits).
 *
 * API is compliant to C++17 std::vector, with the following extra methods:
 *  - append: shortcut for myVec.insert(myVec.end(), ..., ...)
 *  - pop_back_val: pop_back and return the popped value.
 *  - swap2: generalized version of swap usable for all vectors of this library.
 *           you could swap values from a amc::vector with a FixedCapacityVector for instance,
 *           all combinations are possible between the 3 types of vectors.
 *           Note that implementation is not noexcept, adjust capacity needs to be called for both operands.
 *
 * In addition, size_type can be configured and is uint32_t by default. Like with std::vector, the counts taken by the
 * methods (constructors, assign, insert, resize, reserve...) are of type size_type: a larger count is converted to it
 * by the call (wrapping around for an unsigned type), before the vector can check it against max_size().
 *
 * If 'Alloc' provides this additional optional method
 *  * T *reallocate(pointer p, size_type oldCapacity, size_type newCapacity, size_type nConstructedElems);
 *
 * then it will be able use it to optimize the growing of the container when T is trivially relocatable
 * (for amc::allocator, it will use 'realloc').
 *
 * Exception safety:
 *   It provides at least Basic exception safety.
 *   If Object movement is noexcept, most operations provide strong exception safety, excepted:
 *     - assign
 *     - insert from InputIt
 *     - insert from count elements
 *   If Object movement can throw (and T is not trivially relocatable), growth moves the elements to the new storage
 *   instead of copying them like std::vector: 'push_back' and 'emplace_back' then provide strong exception safety only
 *   when the vector is not full, and basic exception safety when it grows.
 */
template <class T, class Alloc = amc::allocator<T>, class SizeType = std::uint32_t>
using vector = SmallVector<T, 0U, Alloc, SizeType>;
}  // namespace amc
