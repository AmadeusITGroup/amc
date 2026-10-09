#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "allocator.hpp"
#include "hasreallocate.hpp"
#include "vectorcommon.hpp"

namespace amc {
namespace vec {

/// New capacity for at least 'newSize' elements, not exceeding 'maxCapa' (see MaxCapacity).
/// 'exact' is only set by 'reserve', which allocates exactly the requested capacity.
template <class SizeType>
inline SizeType SafeNextCapacity(SizeType oldCapa, uintmax_t newSize, uintmax_t maxCapa, bool exact) {
  if (AMC_UNLIKELY(maxCapa < newSize)) {
    throw std::overflow_error(
        "Attempt to use more elements than max_size(). Use a larger size_type if it is the limit");
  }
  if (exact) {
    return static_cast<SizeType>(newSize);
  }
  // Realloc * 1.5 except if minimum requested size is larger (choose it in this case).
  return static_cast<SizeType>(
      std::min(std::max(static_cast<uintmax_t>((3U * static_cast<uintmax_t>(oldCapa) + 1U) / 2U), newSize), maxCapa));
}

template <class Alloc>
struct CanReallocate
    : public std::integral_constant<bool, is_trivially_relocatable<typename Alloc::value_type>::value &&
                                              has_reallocate<Alloc>::value> {};

template <class Alloc, class T, class SizeType, typename std::enable_if<CanReallocate<Alloc>::value, bool>::type = true>
inline T* Reallocate(Alloc& alloc, T* p, SizeType oldCapa, SizeType newCapa, SizeType size) {
  // capacities do not exceed the max_size() of the allocator (see MaxCapacity)
  using AllocSizeType = typename Alloc::size_type;
  return alloc.reallocate(p, static_cast<AllocSizeType>(oldCapa), static_cast<AllocSizeType>(newCapa),
                          static_cast<AllocSizeType>(size));
}

/// Relocates the 'size' elements of 'p' to a newly allocated memory, then deallocates 'p'.
/// If a move throws, the new memory is deallocated and the elements stay in 'p'.
template <class Alloc, class T, class SizeType,
          typename std::enable_if<!CanReallocate<Alloc>::value, bool>::type = true>
inline T* Reallocate(Alloc& alloc, T* p, SizeType oldCapa, SizeType newCapa, SizeType size) {
  T* newPtr = alloc.allocate(static_cast<size_t>(newCapa));
  try {
    (void)amc::uninitialized_relocate_n(p, size, newPtr);
  } catch (...) {
    alloc.deallocate(newPtr, static_cast<size_t>(newCapa));
    throw;
  }
  alloc.deallocate(p, static_cast<size_t>(oldCapa));
  return newPtr;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::grow(uintmax_t minSize, bool exact) {
  SizeType newCapa;
  const uintmax_t maxCapa = MaxCapacity<SizeType>(static_cast<const Alloc&>(*this));
  if (isSmall()) {
    SizeType oldCapa = _size == std::numeric_limits<SizeType>::max() ? _capa : _size;
    newCapa = SafeNextCapacity(oldCapa, minSize, maxCapa, exact);
    T* dynStorage = this->allocate(static_cast<size_t>(newCapa));
    try {
      (void)amc::uninitialized_relocate_n(_storage.ptr(), _capa, dynStorage);
    } catch (...) {
      this->deallocate(dynStorage, static_cast<size_t>(newCapa));
      throw;
    }
    _storage.setDyn(dynStorage);
    _size = _capa;
  } else {
    newCapa = SafeNextCapacity(_capa, minSize, maxCapa, exact);
    _storage.setDyn(vec::Reallocate(static_cast<Alloc&>(*this), _storage.dyn(), _capa, newCapa, _size));
  }
  _capa = newCapa;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::grow(uintmax_t minSize, bool exact) {
  SizeType newCapa = SafeNextCapacity(_capa, minSize, MaxCapacity<SizeType>(static_cast<const Alloc&>(*this)), exact);
  _storage = vec::Reallocate(static_cast<Alloc&>(*this), _storage, _capa, newCapa, _size);
  _capa = newCapa;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::shrink() {
  _storage.setDyn(vec::Reallocate(static_cast<Alloc&>(*this), _storage.dyn(), _capa, _size, _size));
  _capa = _size;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::shrink() {
  if (_size == 0U) {
    this->deallocate(_storage, static_cast<size_t>(_capa));
    _storage = nullptr;
  } else {
    _storage = vec::Reallocate(static_cast<Alloc&>(*this), _storage, _capa, _size, _size);
  }
  _capa = _size;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::freeStorage() noexcept {
  this->deallocate(_storage, static_cast<size_t>(_capa));
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::resetToSmall(SizeType inplaceCapa) {
  T* dynStorage = _storage.dyn();
  try {
    (void)amc::uninitialized_relocate_n(dynStorage, _size, _storage.ptr());
  } catch (...) {
    // the inline storage shares its first bytes with the pointer to the dynamic storage
    _storage.setDyn(dynStorage);
    throw;
  }
  this->deallocate(dynStorage, static_cast<size_t>(_capa));
  _capa = _size;
  _size = _size == inplaceCapa ? std::numeric_limits<SizeType>::max() : inplaceCapa;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::freeStorage() noexcept {
  this->deallocate(_storage.dyn(), static_cast<size_t>(_capa));
}
}  // namespace vec

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
 *   It provides at least Basic exception safety (no leak, the vector stays valid).
 *   If Object movement is noexcept, most operations provide strong exception safety, excepted:
 *     - assign
 *     - insert from InputIt
 *     - insert from count elements (if T is not trivially relocatable)
 *   If Object movement can throw, only 'push_back' and 'emplace_back' modifiers provide strong exception warranty
 */
template <class T, uintmax_t N, class Alloc = amc::allocator<T>, class SizeType = uint32_t>
using SmallVector = Vector<T, Alloc, SizeType, vec::DynamicGrowingPolicy, vec::SanitizeInlineSize<N, SizeType>::value>;
}  // namespace amc
