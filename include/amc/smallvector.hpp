#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "allocator.hpp"
#include "config.hpp"
#include "memory.hpp"
#include "vectorcommon.hpp"

namespace amc {
namespace vec {

/// Capacities of fewer bytes double when a vector grows, larger ones grow by a factor of 1.5: reallocating a small
/// block costs more than the memory left unused, and the smaller factor saves memory for large blocks.
constexpr std::size_t kMaxDoublingBytes = 4096;

/// Capacity below which a vector of T doubles its capacity when growing (see kMaxDoublingBytes).
template <class T>
constexpr std::uintmax_t MaxDoublingCapacity() {
  return (kMaxDoublingBytes + sizeof(T) - 1U) / sizeof(T);
}

// New capacity for at least 'newSize' elements, not exceeding 'maxCapa' (see MaxCapacity).
// Capacities below 'maxDoublingCapa' double, larger ones grow by a factor of 1.5.
// 'exact' is only set by 'reserve', which allocates exactly the requested capacity.
template <class SizeType>
inline SizeType SafeNextCapacity(SizeType oldCapa, std::uintmax_t newSize, std::uintmax_t maxCapa,
                                 std::uintmax_t maxDoublingCapa, bool exact) {
  if (AMC_UNLIKELY(maxCapa < newSize)) {
    throw std::overflow_error(
        "Attempt to use more elements than max_size(). Use a larger size_type if it is the limit");
  }
  if (exact) {
    return static_cast<SizeType>(newSize);
  }
  const auto capa = static_cast<std::uintmax_t>(oldCapa);
  const std::uintmax_t grownCapa = capa < maxDoublingCapa ? 2U * capa : (3U * capa + 1U) / 2U;
  // Except if the minimum requested size is larger (choose it in this case)
  return static_cast<SizeType>(std::min(std::max(grownCapa, newSize), maxCapa));
}

template <class Alloc, class T, class SizeType, typename std::enable_if<CanReallocate<Alloc>::value, bool>::type = true>
inline T* Reallocate(Alloc& alloc, T* p, SizeType oldCapa, SizeType newCapa, SizeType size) {
  // capacities do not exceed the max_size() of the allocator (see MaxCapacity)
  using AllocSizeType = typename Alloc::size_type;
  return alloc.reallocate(p, static_cast<AllocSizeType>(oldCapa), static_cast<AllocSizeType>(newCapa),
                          static_cast<AllocSizeType>(size));
}

// Relocates the 'size' elements of 'p' to a newly allocated memory, then deallocates 'p'.
// If a move throws, the new memory is deallocated and the elements stay in 'p'.
template <class Alloc, class T, class SizeType,
          typename std::enable_if<!CanReallocate<Alloc>::value, bool>::type = true>
inline T* Reallocate(Alloc& alloc, T* p, SizeType oldCapa, SizeType newCapa, SizeType size) {
  T* newPtr = alloc.allocate(static_cast<std::size_t>(newCapa));
  try {
    (void)amc::uninitialized_relocate_n(p, size, newPtr);
  } catch (...) {
    alloc.deallocate(newPtr, static_cast<std::size_t>(newCapa));
    throw;
  }
  alloc.deallocate(p, static_cast<std::size_t>(oldCapa));
  return newPtr;
}

// Relocates the 'n' elements starting at 'first' to the raw memory starting at 'dest', the ones from index 'pos' one
// slot further: the slot 'dest + pos' is left to the inserted element.
template <class T, class SizeType, typename std::enable_if<is_move_construct_nothrow<T>::value, bool>::type = true>
inline void uninitialized_relocate_around(T* first, SizeType n, SizeType pos, T* dest) noexcept {
  (void)amc::uninitialized_relocate_n(first, pos, dest);
  (void)amc::uninitialized_relocate_n(first + pos, n - pos, dest + pos + 1);
}

// If a move throws, the elements constructed at 'dest' are destroyed and all the source elements stay alive (some of
// them moved from), like with 'amc::uninitialized_relocate_n': all of them are moved before any is destroyed.
template <class T, class SizeType, typename std::enable_if<!is_move_construct_nothrow<T>::value, bool>::type = true>
inline void uninitialized_relocate_around(T* first, SizeType n, SizeType pos, T* dest) {
  (void)amc::uninitialized_move_n(first, pos, dest);
  try {
    (void)amc::uninitialized_move_n(first + pos, n - pos, dest + pos + 1);
  } catch (...) {
    amc::destroy_n(dest, pos);
    throw;
  }
  amc::destroy_n(first, n);
}

// Allocates a storage of 'newCapa' elements and constructs in it the element at index 'pos' from 'args', then
// relocates the 'size' elements of 'p' around it. 'args' may reference elements of 'p': they are still in place when
// the new element is constructed.
// If anything throws, the new storage is deallocated and the elements stay in 'p'.
template <class Alloc, class T, class SizeType, class... Args>
inline T* AllocateEmplace(Alloc& alloc, T* p, SizeType size, SizeType newCapa, SizeType pos, Args&&... args) {
  T* newPtr = alloc.allocate(static_cast<std::size_t>(newCapa));
  try {
    amc::construct_at(newPtr + pos, std::forward<Args>(args)...);
    try {
      uninitialized_relocate_around(p, size, pos, newPtr);
    } catch (...) {
      amc::destroy_at(newPtr + pos);
      throw;
    }
  } catch (...) {
    alloc.deallocate(newPtr, static_cast<std::size_t>(newCapa));
    throw;
  }
  return newPtr;
}

// 'grow' is declared inline: GCC otherwise stops inlining it into the modifiers, and the vector then stays in memory
// instead of registers in the loops that fill it (up to 20 % slower on small vectors).
template <class T, class Alloc, class SizeType>
inline void SmallVectorBase<T, Alloc, SizeType>::grow(std::uintmax_t minSize, bool exact) {
  SizeType newCapa;
  const std::uintmax_t maxCapa = MaxCapacity<SizeType>(static_cast<const Alloc&>(*this));
  if (isSmall()) {
    SizeType oldCapa = _size == std::numeric_limits<SizeType>::max() ? _capa : _size;
    newCapa = SafeNextCapacity(oldCapa, minSize, maxCapa, MaxDoublingCapacity<T>(), exact);
    T* dynStorage = this->allocate(static_cast<std::size_t>(newCapa));
    try {
      (void)amc::uninitialized_relocate_n(_storage.ptr(), _capa, dynStorage);
    } catch (...) {
      this->deallocate(dynStorage, static_cast<std::size_t>(newCapa));
      throw;
    }
    _storage.setDyn(dynStorage);
    _size = _capa;
  } else {
    newCapa = SafeNextCapacity(_capa, minSize, maxCapa, MaxDoublingCapacity<T>(), exact);
    _storage.setDyn(vec::Reallocate(static_cast<Alloc&>(*this), _storage.dyn(), _capa, newCapa, _size));
  }
  _capa = newCapa;
}

template <class T, class Alloc, class SizeType>
inline void StdVectorBase<T, Alloc, SizeType>::grow(std::uintmax_t minSize, bool exact) {
  SizeType newCapa = SafeNextCapacity(_capa, minSize, MaxCapacity<SizeType>(static_cast<const Alloc&>(*this)),
                                      MaxDoublingCapacity<T>(), exact);
  // Without capacity there is no storage: allocate it rather than reallocating a null pointer, which is slower
  _storage = _capa == 0U ? this->allocate(static_cast<std::size_t>(newCapa))
                         : vec::Reallocate(static_cast<Alloc&>(*this), _storage, _capa, newCapa, _size);
  _capa = newCapa;
}

// Grows to a new dynamic storage, in which the element at index 'pos' is constructed from 'args' before the elements
// are relocated around it (see AllocateEmplace). Returns the new element, without incrementing the size.
template <class T, class Alloc, class SizeType>
template <class... Args>
inline T* SmallVectorBase<T, Alloc, SizeType>::growEmplace(SizeType pos, Args&&... args) {
  const SizeType size = this->size();
  const SizeType newCapa =
      SafeNextCapacity(capacity(), static_cast<std::uintmax_t>(size) + 1U,
                       MaxCapacity<SizeType>(static_cast<const Alloc&>(*this)), MaxDoublingCapacity<T>(), false);
  T* dynStorage = AllocateEmplace(static_cast<Alloc&>(*this), begin(), size, newCapa, pos, std::forward<Args>(args)...);
  if (!isSmall()) {
    freeStorage();
  }
  _storage.setDyn(dynStorage);
  _capa = newCapa;
  _size = size;
  return dynStorage + pos;
}

template <class T, class Alloc, class SizeType>
template <class... Args>
inline T* StdVectorBase<T, Alloc, SizeType>::growEmplace(SizeType pos, Args&&... args) {
  const SizeType newCapa =
      SafeNextCapacity(_capa, static_cast<std::uintmax_t>(_size) + 1U,
                       MaxCapacity<SizeType>(static_cast<const Alloc&>(*this)), MaxDoublingCapacity<T>(), false);
  T* storage = AllocateEmplace(static_cast<Alloc&>(*this), _storage, _size, newCapa, pos, std::forward<Args>(args)...);
  if (_capa != 0U) {
    freeStorage();
  }
  _storage = storage;
  _capa = newCapa;
  return storage + pos;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::shrink() {
  _storage.setDyn(vec::Reallocate(static_cast<Alloc&>(*this), _storage.dyn(), _capa, _size, _size));
  _capa = _size;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::shrink() {
  if (_size == 0U) {
    this->deallocate(_storage, static_cast<std::size_t>(_capa));
    _storage = nullptr;
  } else {
    _storage = vec::Reallocate(static_cast<Alloc&>(*this), _storage, _capa, _size, _size);
  }
  _capa = _size;
}

template <class T, class Alloc, class SizeType>
void StdVectorBase<T, Alloc, SizeType>::freeStorage() noexcept {
  this->deallocate(_storage, static_cast<std::size_t>(_capa));
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
  this->deallocate(dynStorage, static_cast<std::size_t>(_capa));
  _capa = _size;
  _size = _size == inplaceCapa ? std::numeric_limits<SizeType>::max() : inplaceCapa;
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::freeStorage() noexcept {
  this->deallocate(_storage.dyn(), static_cast<std::size_t>(_capa));
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
 * A SmallVector can be built from a rvalue amc::vector: it steals its dynamic storage if its capacity is larger than
 * N, otherwise it relocates its elements inline. Indeed, a 'Large' SmallVector always has a capacity larger than N.
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
template <class T, std::uintmax_t N, class Alloc = amc::allocator<T>, class SizeType = std::uint32_t>
using SmallVector = Vector<T, Alloc, SizeType, vec::DynamicGrowingPolicy, vec::SanitizeInlineSize<N, SizeType>::value>;
}  // namespace amc
