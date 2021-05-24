#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "amc_allocator.hpp"
#include "amc_isdetected.hpp"
#include "amc_vectorcommon.hpp"

namespace amc {
namespace vec {

/// Implement the member detection idiom to know if allocator provides 'reallocate' method.
template <class T>
using has_reallocate_t = decltype(
    std::declval<T>().reallocate(std::declval<typename T::pointer>(), std::declval<typename T::size_type>(),
                                 std::declval<typename T::size_type>(), std::declval<typename T::size_type>()));

template <class T>
using has_reallocate = is_detected<has_reallocate_t, T>;

template <class SizeType>
inline SizeType SafeNextCapacity(SizeType oldCapa, uintmax_t newSize, bool exact) {
  if (exact) {
    // Only for reserve method. It takes a SizeType as input parameter, so it can obviously fit into a SizeType.
    return static_cast<SizeType>(newSize);
  }
  // Realloc * 1.5 except if minimum requested size is larger (choose it in this case).
  // Also make sure the new capacity can fit in SizeType
  const uintmax_t newCapa =
      std::min(std::max(static_cast<uintmax_t>((3U * static_cast<uintmax_t>(oldCapa) + 1U) / 2U), newSize),
               static_cast<uintmax_t>(std::numeric_limits<SizeType>::max()));
  if (AMC_UNLIKELY(newCapa < newSize)) {
    throw std::overflow_error("Attempt to use more elements that size_type can support. Use a larger size_type");
  }
  return static_cast<SizeType>(newCapa);
}

template <class Alloc>
struct CanReallocate
    : public std::integral_constant<bool, is_trivially_relocatable<typename Alloc::value_type>::value &&
                                              has_reallocate<Alloc>::value> {};

template <class Alloc, class T, class SizeType, typename std::enable_if<CanReallocate<Alloc>::value, bool>::type = true>
inline T *Reallocate(Alloc &alloc, T *p, SizeType oldCapa, SizeType newCapa, SizeType size) {
  return alloc.reallocate(p, oldCapa, newCapa, size);
}

template <class Alloc, class T, class SizeType,
          typename std::enable_if<!CanReallocate<Alloc>::value, bool>::type = true>
inline T *Reallocate(Alloc &alloc, T *p, SizeType oldCapa, SizeType newCapa, SizeType size) {
  T *newPtr = alloc.allocate(newCapa);
  (void)amc::uninitialized_relocate_n(p, size, newPtr);
  alloc.deallocate(p, oldCapa);
  return newPtr;
}
}  // namespace vec

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::grow(uintmax_t minSize, bool exact) {
  SizeType newCapa;
  if (isSmall()) {
    SizeType oldCapa = _size == std::numeric_limits<SizeType>::max() ? _capa : _size;
    newCapa = vec::SafeNextCapacity(oldCapa, minSize, exact);
    T *dynStorage = this->allocate(newCapa);
    (void)amc::uninitialized_relocate_n(_storage.ptr(), _capa, dynStorage);
    _storage.setDyn(dynStorage);
    _size = _capa;
  } else {
    newCapa = vec::SafeNextCapacity(_capa, minSize, exact);
    _storage.setDyn(vec::Reallocate(static_cast<Alloc &>(*this), _storage.dyn(), _capa, newCapa, _size));
  }
  _capa = newCapa;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::grow(uintmax_t minSize, bool exact) {
  SizeType newCapa = vec::SafeNextCapacity(_capa, minSize, exact);
  _storage = vec::Reallocate(static_cast<Alloc &>(*this), _storage, _capa, newCapa, _size);
  _capa = newCapa;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::shrink() {
  _storage.setDyn(vec::Reallocate(static_cast<Alloc &>(*this), _storage.dyn(), _capa, _size, _size));
  _capa = _size;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::shrink() {
  if (_size == 0U) {
    this->deallocate(_storage, _capa);
    _storage = nullptr;
  } else {
    _storage = vec::Reallocate(static_cast<Alloc &>(*this), _storage, _capa, _size, _size);
  }
  _capa = _size;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::freeStorage() {
  this->deallocate(_storage, _capa);
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::resetToSmall(SizeType inplaceCapa) {
  T *dynStorage = _storage.dyn();
  (void)amc::uninitialized_relocate_n(dynStorage, _size, _storage.ptr());
  this->deallocate(dynStorage, _capa);
  _capa = _size;
  _size = _size == inplaceCapa ? std::numeric_limits<SizeType>::max() : inplaceCapa;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::freeStorage() {
  this->deallocate(_storage.dyn(), _capa);
}

/**
 * Vector optimized for *trivially relocatable types* and in 'Small' state (when its capacity is <= N).
 *
 * In 'Small' state, container does not allocate memory and store elements inline. When it is possible,
 * dynamic pointer and inline storage are shared to optimize SmallVector's size.
 *
 * API is compliant to C++17 std::vector, with the following extra methods:
 *  - append: shortcut for myVec.insert(myVec.end(), ..., ...)
 *  - pop_back_val: pop_back and return the popped value.
 *  - swap2: generalized version of swap usable for all vectors of this library.
 *           you could swap values from a amc::vector with a FixedCapacityVector for instance,
 *           all combinations are possible between the 3 types of vectors.
 *           Note that implementation is not noexcept, adjust capacity needs to be called for both operands.
 *
 * In addition, size_type can be configured and is uint32_t by default.
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
 *   If Object movement can throw, only 'push_back' and 'emplace_back' modifiers provide strong exception warranty
 */
template <class T, uintmax_t N, class Alloc = amc::allocator<T>, class SizeType = uint32_t>
using SmallVector = Vector<T, Alloc, SizeType, vec::DynamicGrowingPolicy, vec::SanitizeInlineSize<N, SizeType>()>;
}  // namespace amc
