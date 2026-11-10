#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
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
AMC_INLINE_VARIABLE constexpr std::size_t kMaxDoublingBytes = 4096;

/// Capacity below which a vector of T doubles its capacity when growing (see kMaxDoublingBytes).
template <class T>
constexpr std::uintmax_t MaxDoublingCapacity() {
  return (kMaxDoublingBytes + sizeof(T) - 1U) / sizeof(T);
}

// New capacity for at least 'newSize' elements, not exceeding 'maxCapa' (see MaxCapacity).
// Capacities below 'maxDoublingCapa' double, larger ones grow by a factor of 1.5.
// 'exact' is only set by 'reserve' and the assignments, which allocate exactly the requested capacity.
template <class SizeType>
inline SizeType SafeNextCapacity(SizeType oldCapa, std::uintmax_t newSize, std::uintmax_t maxCapa,
                                 std::uintmax_t maxDoublingCapa, bool exact) {
  if (AMC_UNLIKELY(maxCapa < newSize)) {
    AMC_THROW(
        std::overflow_error("Attempt to use more elements than max_size(). Use a larger size_type if it is the limit"));
  }
  if (exact) {
    return static_cast<SizeType>(newSize);
  }
  const std::uintmax_t capa = static_cast<std::uintmax_t>(oldCapa);
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
  AMC_TRY {
    (void)amc::uninitialized_relocate_n(p, size, newPtr);
  } AMC_CATCH_ALL {
    alloc.deallocate(newPtr, static_cast<std::size_t>(newCapa));
    AMC_RETHROW;
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
  AMC_TRY {
    (void)amc::uninitialized_move_n(first + pos, n - pos, dest + pos + 1);
  } AMC_CATCH_ALL {
    amc::destroy_n(dest, pos);
    AMC_RETHROW;
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
  AMC_TRY {
    amc::construct_at(newPtr + pos, std::forward<Args>(args)...);
    AMC_TRY {
      uninitialized_relocate_around(p, size, pos, newPtr);
    } AMC_CATCH_ALL {
      amc::destroy_at(newPtr + pos);
      AMC_RETHROW;
    }
  } AMC_CATCH_ALL {
    alloc.deallocate(newPtr, static_cast<std::size_t>(newCapa));
    AMC_RETHROW;
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
    SizeType oldCapa = capacity();
    newCapa = SafeNextCapacity(oldCapa, minSize, maxCapa, MaxDoublingCapacity<T>(), exact);
    T* dynStorage = this->allocate(static_cast<std::size_t>(newCapa));
    AMC_TRY {
      (void)amc::uninitialized_relocate_n(_storage.ptr(), _capa, dynStorage);
    } AMC_CATCH_ALL {
      this->deallocate(dynStorage, static_cast<std::size_t>(newCapa));
      AMC_RETHROW;
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

// Replaces the elements by the 'n' ones that 'construct(storage)' constructs in a new storage of exactly 'n' elements,
// 'n' exceeding the capacity, for the assignments. The elements would all be overwritten: they are not relocated to
// the new storage (which is not reallocated either: 'reallocate' copies them when it cannot extend it in place), but
// destroyed after the new ones are constructed, like libstdc++ does. Hence the source of the new elements may refer
// to them (or to objects owned by them, 'assign(count, v)' needs no copy of 'v'), and if anything throws (capacity
// exceeding the maximum, allocation, construction of an element), the vector is unchanged.
// 'construct' destroys the elements it constructed if it throws, like std::uninitialized_fill_n.
template <class T, class Alloc, class SizeType>
template <class Construct>
void SmallVectorBase<T, Alloc, SizeType>::assignInNewStorage(std::uintmax_t n, Construct construct) {
  // larger than the capacity, hence than the inline capacity, as required in the large state
  assert(static_cast<std::uintmax_t>(capacity()) < n);
  const SizeType capa = SafeNextCapacity(capacity(), n, MaxCapacity<SizeType>(static_cast<const Alloc&>(*this)),
                                         MaxDoublingCapacity<T>(), true);
  T* dynStorage = this->allocate(static_cast<std::size_t>(capa));
  AMC_TRY {
    construct(dynStorage);
  } AMC_CATCH_ALL {
    this->deallocate(dynStorage, static_cast<std::size_t>(capa));
    AMC_RETHROW;
  }
  destroyFreeStorage();
  _storage.setDyn(dynStorage);
  _capa = capa;
  _size = capa;
}

template <class T, class Alloc, class SizeType>
template <class Construct>
void StdVectorBase<T, Alloc, SizeType>::assignInNewStorage(std::uintmax_t n, Construct construct) {
  assert(static_cast<std::uintmax_t>(_capa) < n);
  const SizeType capa = SafeNextCapacity(_capa, n, MaxCapacity<SizeType>(static_cast<const Alloc&>(*this)),
                                         MaxDoublingCapacity<T>(), true);
  T* storage = this->allocate(static_cast<std::size_t>(capa));
  AMC_TRY {
    construct(storage);
  } AMC_CATCH_ALL {
    this->deallocate(storage, static_cast<std::size_t>(capa));
    AMC_RETHROW;
  }
  if (_capa != 0U) {
    amc::destroy_n(_storage, _size);
    freeStorage();
  }
  _storage = storage;
  _capa = capa;
  _size = capa;
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
  AMC_TRY {
    (void)amc::uninitialized_relocate_n(dynStorage, _size, _storage.ptr());
  } AMC_CATCH_ALL {
    // the inline storage shares its first bytes with the pointer to the dynamic storage
    _storage.setDyn(dynStorage);
    AMC_RETHROW;
  }
  this->deallocate(dynStorage, static_cast<std::size_t>(_capa));
  _capa = _size;
  _size = SmallSizeMember(inplaceCapa);
}

template <class T, class Alloc, class SizeType>
void SmallVectorBase<T, Alloc, SizeType>::freeStorage() noexcept {
  this->deallocate(_storage.dyn(), static_cast<std::size_t>(_capa));
}
}  // namespace vec

/**
 * Vector optimized for *trivially relocatable types* and in 'Small' state (when its capacity is its inline capacity).
 *
 * In 'Small' state, container does not allocate memory and store elements inline. When it is possible,
 * dynamic pointer and inline storage are shared to optimize SmallVector's size.
 * The inline capacity, kInlineCapacity, is at least N: all the elements fitting in the inline storage, which may have
 * room for more than N of them (in the bytes of the pointer, or in the padding after the last element).
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
 * its inline capacity, otherwise it relocates its elements inline. Indeed, a 'Large' SmallVector always has a capacity
 * larger than its inline capacity.
 *
 * In addition, size_type can be configured and is uint32_t by default. Like with std::vector, the counts taken by the
 * methods (constructors, assign, insert, resize, reserve...) are of type size_type: a larger count is converted to it
 * by the call (wrapping around for an unsigned type), before the vector can check it against max_size(). For
 * instance, SmallVector<int, 4, amc::allocator<int>, uint8_t>(300) has 44 elements.
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
 *   If Object movement can throw (and T is not trivially relocatable), growth moves the elements to the new storage
 *   instead of copying them like std::vector: 'push_back' and 'emplace_back' then provide strong exception safety only
 *   when the vector is not full, and basic exception safety when it grows.
 */
template <class T, std::uintmax_t N, class Alloc = amc::allocator<T>, class SizeType = std::uint32_t>
using SmallVector = Vector<T, Alloc, SizeType, vec::DynamicGrowingPolicy, vec::SanitizeInlineSize<N, SizeType>::value>;
}  // namespace amc
