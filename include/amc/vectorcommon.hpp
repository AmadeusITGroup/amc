#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <stdexcept>

#include "config.hpp"
#include "memory.hpp"
#include "type_traits.hpp"
#include "utility.hpp"

#ifdef AMC_CXX14
#include <functional>
#endif

namespace amc {
namespace vec {
template <class T>
struct is_swap_noexcept : std::integral_constant<bool, std::is_nothrow_move_constructible<T>::value &&
                                                           amc::is_nothrow_swappable<T>::value> {};
template <class T>
struct is_shift_nothrow : std::integral_constant<bool, amc::is_trivially_relocatable<T>::value ||
                                                           (std::is_nothrow_move_constructible<T>::value &&
                                                            std::is_nothrow_move_assignable<T>::value)> {};
template <class T>
struct is_move_construct_nothrow : std::integral_constant<bool, amc::is_trivially_relocatable<T>::value ||
                                                                    std::is_nothrow_move_constructible<T>::value> {};

/// Shift 'n' elements starting at 'first' one slot to the right
/// Requirements: n != 0, with uninitialized memory starting at 'first + n'
/// Warning: no destroy is called for elements which has been moved from.
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void shift_right(T *first, SizeType n) noexcept(is_shift_nothrow<T>::value) {
  T *last = first + n;
  amc::construct_at(last, std::move(*(last - 1)));
  std::move_backward(first, last - 1, last);
}

/// Specialization for trivially relocatable types. Just use memmove here.
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void shift_right(T *first, SizeType n) noexcept {
  (void)amc::uninitialized_relocate_n(first, n, first + 1);
}

/// Shift 'n' elements starting at 'first' 'count' slots to the right
/// Requirements: uninitialized memory starting at 'first + n'
/// Warning: no destroy is called for elements which has been moved from.
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
void shift_right(T *first, SizeType n, SizeType count) noexcept(is_shift_nothrow<T>::value) {
  if (count < n) {
    T *last = first + n;
    amc::uninitialized_move_n(last - count, count, last);  // move last 'count' elems to uninitialized storage
    std::move_backward(first, last - count, last);         // move remaining 'n - count' elems to initialized storage
  } else {
    // no overlap, we shift all elements to uninitialized memory
    amc::uninitialized_move_n(first, n, first + count);
  }
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void shift_right(T *first, SizeType n, SizeType count) noexcept {
  (void)amc::uninitialized_relocate_n(first, n, first + count);
}

/// Fill 'count' 'v' values at memory starting at 'first', with first 'n' slots on inintialized memory,
/// and next 'count - n' slots on uninitialized memory if there is overlap
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void fill_after_shift(T *first, SizeType n, SizeType count, const T &v) {
  if (n < count) {
    std::uninitialized_fill_n(first + n, count - n, v);
    std::fill_n(first, n, v);
  } else {
    std::fill_n(first, count, v);
  }
}

/// shift_right leaves only uninitialized memory for trivially relocatable type
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void fill_after_shift(T *first, SizeType, SizeType count, const T &v) {
  std::uninitialized_fill_n(first, count, v);
}

/// copy from a range to available location divided in two parts: one on initialized memory, other one on raw memory
template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<!std::is_trivially_copyable<T>::value, bool>::type = true>
inline void assign_n(ForwardIt first, SizeType count, T *d_first, SizeType d_n) {
  if (d_n > 0) {
    *d_first++ = *first;  // rewrite copy_n to avoid double iteration on the input elements
    for (SizeType i = 1; i < d_n; ++i) {
      *d_first++ = *++first;
    }
    (void)++first;
  }
  if (d_n < count) {
    amc::uninitialized_copy_n(first, count - d_n, d_first);
  }
}

template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<std::is_trivially_copyable<T>::value, bool>::type = true>
inline void assign_n(ForwardIt first, SizeType count, T *d_first, SizeType) {
  amc::uninitialized_copy_n(first, count, d_first);
}

/// Copy 'count' elements starting at 'first' to 'pos' location
/// To be used in conjonction with 'shift_right'
template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void copy_after_shift(ForwardIt first, SizeType n, SizeType count, T *pos) {
  if (n < count) {
    if (n > 0) {
      *pos++ = *first;  // rewrite copy_n to avoid double iteration on the input elements
      for (SizeType i = 1; i < n; ++i) {
        *pos++ = *++first;
      }
      (void)++first;
    }
    amc::uninitialized_copy_n(first, count - n, pos);
  } else {
    std::copy_n(first, count, pos);
  }
}

template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void copy_after_shift(ForwardIt first, SizeType, SizeType count, T *pos) {
  amc::uninitialized_copy_n(first, count, pos);
}

/// Call destroy from a memory that has been moved from only for non trivially relocatable types
template <class T, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void destroy_after_shift(T *pos) {
  amc::destroy_at(pos);
}

template <class T, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void destroy_after_shift(T *) {}

/// Shift 'n' elements starting at 'first' one slot back to the left
/// Requirements: n != 0 with one slot of initialized memory at first - 1
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
void shift_left(T *first, SizeType n) noexcept(is_shift_nothrow<T>::value) {
  *(first - 1) = std::move(*first);  // move first element to initialized memory slot 'first - 1'
  // move next 'n - 1' elements one slot to the left and destroy last moved element
  amc::destroy_at(std::move(first + 1, first + n, first));
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
void shift_left(T *first, SizeType n) noexcept {
  (void)amc::uninitialized_relocate_n(first, n, first - 1);
}

/// Erase 'n' elements starting at 'first', shifting the next 'count' elements to memory starting at 'first'
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_n(T *first, SizeType n, SizeType count) {
  amc::destroy_n(std::move(first + n, first + n + count, first), n);
}
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_n(T *first, SizeType n, SizeType count) {
  amc::destroy_n(first, n);
  (void)amc::uninitialized_relocate_n(first + n, count, first);
}

/// Erase one element starting at 'first', shifting the next 'count' elements to memory starting at 'first'
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_at(T *first, SizeType count) {
  amc::destroy_at(std::move(first + 1, first + 1 + count, first));
}
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_at(T *first, SizeType count) {
  amc::destroy_at(first);
  (void)amc::uninitialized_relocate_n(first + 1, count, first);
}

/// Assign 'v' to a memory starting at 'first', with 'n' slots on initialized memory, 'count - n'
/// slots on uninitialized memory.
/// Requirements: n < count
template <class T, class SizeType, typename std::enable_if<!std::is_trivially_copyable<T>::value, bool>::type = true>
inline void fill(T *first, SizeType n, SizeType count, const T &v) {
  // uninitialize fill first for slightly better exception safety
  std::uninitialized_fill_n(first + n, count - n, v);
  std::fill_n(first, n, v);
}

template <class T, class SizeType, typename std::enable_if<std::is_trivially_copyable<T>::value, bool>::type = true>
inline void fill(T *first, SizeType, SizeType count, const T &v) {
  std::uninitialized_fill_n(first, count, v);
}

template <class T, class SizeType1, class SizeType2>
void swap_deep(T *first1, SizeType1 count1, T *first2, SizeType2 count2) noexcept(is_swap_noexcept<T>::value) {
  // swap element by element in common (initialized) storage
  using SizeType = typename std::conditional<sizeof(SizeType1) < sizeof(SizeType2), SizeType2, SizeType1>::type;
  std::swap_ranges(first1, first1 + std::min(static_cast<SizeType>(count1), static_cast<SizeType>(count2)), first2);
  if (static_cast<SizeType>(count1) < static_cast<SizeType>(count2)) {
    // Move their next elements to our storage
    (void)amc::uninitialized_relocate_n(first2 + count1, count2 - count1, first1 + count1);
  } else {
    // Move our next elements to their storage
    (void)amc::uninitialized_relocate_n(first1 + count2, count1 - count2, first2 + count2);
  }
}

template <class SizeType1, class SizeType2>
inline void swap_sizetype(SizeType1 &lhs, SizeType2 &rhs) {
  // Simple swap for different SizeType
  // We need to check if their values can exchange in each other size type
#ifdef AMC_CXX17
  if constexpr (sizeof(SizeType1) < sizeof(SizeType2)) {
    if (AMC_UNLIKELY(static_cast<SizeType2>(std::numeric_limits<SizeType1>::max()) < rhs)) {
      throw std::overflow_error("Cannot cast size to each other");
    }
  } else if constexpr (sizeof(SizeType2) < sizeof(SizeType1)) {
    if (AMC_UNLIKELY(static_cast<SizeType1>(std::numeric_limits<SizeType2>::max()) < lhs)) {
      throw std::overflow_error("Cannot cast size to each other");
    }
  }
#else
  if (AMC_UNLIKELY((sizeof(SizeType1) < sizeof(SizeType2) &&
                    static_cast<SizeType2>(std::numeric_limits<SizeType1>::max()) < rhs) ||
                   (sizeof(SizeType2) < sizeof(SizeType1) &&
                    static_cast<SizeType1>(std::numeric_limits<SizeType2>::max()) < lhs))) {
    throw std::overflow_error("Cannot cast size to each other");
  }
#endif
  SizeType1 tmp = lhs;
  lhs = static_cast<SizeType1>(rhs);
  rhs = static_cast<SizeType2>(tmp);
}

template <class SizeType>
inline void swap_sizetype(SizeType &lhs, SizeType &rhs) noexcept {
  std::swap(lhs, rhs);
}

/// Move 'n' objects starting at 'first' to a range starting at 'd_first' containing already 'd_n' instantiated objects
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void move_n(T *first, SizeType n, T *d_first, SizeType d_n) {
  std::move(first, first + std::min(n, d_n), d_first);
  if (d_n < n) {
    amc::uninitialized_move_n(first + d_n, n - d_n, d_first + d_n);
  } else {
    amc::destroy_n(d_first + n, d_n - n);
  }
  amc::destroy_n(first, n);
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void move_n(T *first, SizeType n, T *d_first, SizeType d_n) {
  amc::destroy_n(d_first, d_n);
  (void)amc::uninitialized_relocate_n(first, n, d_first);
}

// Shift 'n' elements starting at 'first' one slot back to the left
// Requirements: 'n' != 0 with one slot of uninitialized memory at first - 1
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
void uninitialized_shift_left(T *first, SizeType n) noexcept(is_shift_nothrow<T>::value) {
  amc::construct_at(first - 1, std::move(*first));  // move first element to uninitialized memory slot 'first - 1'
  // move next 'n - 1' elements one slot to the left and destroy last moved element
  amc::destroy_at(std::move(first + 1, first + n, first));
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
void uninitialized_shift_left(T *first, SizeType n) noexcept {
  amc::uninitialized_relocate_n(first, n, first - 1);
}

/// Construct at 'pos' the T from 'args' parameters, shifting 'n' elements starting at 'pos' to the right
template <class T, class SizeType, class... Args>
inline void emplace_n(T *pos, SizeType n, Args &&...args) {
  if (n == 0) {
    amc::construct_at(pos, std::forward<Args>(args)...);
  } else {
    shift_right(pos, n);
    destroy_after_shift(pos);
    try {
      amc::construct_at(pos, std::forward<Args>(args)...);
    } catch (...) {
      uninitialized_shift_left(pos + 1, n);
      throw;
    }
  }
}

template <class T, class V, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void assign_after_shift(T *pos, V &&v) {
  *pos = std::forward<V>(v);
}
template <class T, class V, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void assign_after_shift(T *pos, V &&v) {
  amc::construct_at(pos, std::forward<V>(v));
}

/// Insert 'v' at 'pos', shifting 'n' elements starting at 'pos' to the right
template <class T, class SizeType, class V>
inline void insert_n(T *pos, SizeType n, V &&v) {
  if (n == 0) {
    amc::construct_at(pos, std::forward<V>(v));
  } else {
    shift_right(pos, n);
    try {
      assign_after_shift(pos, std::forward<V>(v));
    } catch (...) {
      shift_left(pos + 1, n);
      throw;
    }
  }
}

template <class T, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void relocate_after_shift(T *e, T *dest) {
  *dest = std::move(*e);
  amc::destroy_at(e);
}
template <class T, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void relocate_after_shift(T *e, T *dest) {
  amc::relocate_at(e, dest);
}

template <class T>
class ElemStorage {
 public:
  T *ptr() noexcept { return reinterpret_cast<T *>(this); }
  const T *ptr() const noexcept { return reinterpret_cast<const T *>(this); }

 private:
  typename std::aligned_storage<sizeof(T), std::alignment_of<T>::value>::type _el;
};

/// This class represents a merge of a pointer and some inline storage elements.
/// Thanks to this optimization, SmallVector behaves like a string type with SSO
/// Example : for a system with pointer size of 8 bytes,
///           sizeof(SmallVector<char, 8>) == sizeof(vector<char>)
///           because 8 chars can be stored in a pointer.
template <class T>
class ElemWithPtrStorage {
 public:
  using pointer = T *;
  using const_pointer = const T *;

#ifdef AMC_CXX14
  // Use std::divides instead of '/' to avoid potential harmless warning occurring for instance in GCC:
  // warning: division 'sizeof (...) / sizeof (...)' does not compute the number of array elements
  // [-Wsizeof-pointer-div]
  // Activated only in C++14 as we need it to be constexpr
  static constexpr auto kNbSlots =
      std::max(std::divides<std::size_t>()(sizeof(pointer), sizeof(T)), static_cast<std::size_t>(1));
#else
  static constexpr auto kNbSlots =
      sizeof(pointer) < sizeof(T) ? static_cast<std::size_t>(1) : (sizeof(pointer) / sizeof(T));
#endif

  // Get a pointer to its underlying storage
  pointer ptr() noexcept { return reinterpret_cast<pointer>(this); }
  const_pointer ptr() const noexcept { return reinterpret_cast<const_pointer>(this); }

  void setDyn(pointer p) noexcept { std::memcpy(std::addressof(_el), std::addressof(p), sizeof(pointer)); }

  // Return the pointer stored in the first bytes of this object to the dynamic storage.
  pointer dyn() const noexcept {
    // use memcpy to avoid breaking strict aliasing rule, will be optimized away by the compiler.
    // (confirmed with clang and gcc from O2)
    pointer p;
    std::memcpy(std::addressof(p), std::addressof(_el), sizeof(pointer));
    return p;
  }

 private:
  // Use aligned storage able to store at least one pointer or a T, with alignment of T as next inline elements will be
  // appended to this one.
  static constexpr auto kTAlign = std::alignment_of<T>::value;
  static constexpr auto kPtrAlign = std::alignment_of<T *>::value;

#ifdef AMC_CXX14
  // std::max is constexpr from C++14
  typename std::aligned_storage<std::max(sizeof(T), sizeof(T *)), std::max(kTAlign, kPtrAlign)>::type _el;
#else
  typename std::aligned_storage < sizeof(T *) < sizeof(T) ? sizeof(T) : sizeof(T *),
      kPtrAlign<kTAlign ? kTAlign : kPtrAlign>::type _el;
#endif
};

template <class T>
void SwapDynStorage(ElemWithPtrStorage<T> &lhs, ElemWithPtrStorage<T> &rhs) {
  T *pTemp = lhs.dyn();
  lhs.setDyn(rhs.dyn());
  rhs.setDyn(pTemp);
}

template <class T>
void SwapDynStorage(ElemWithPtrStorage<T> &lhs, T *&rhs) {
  T *pTemp = lhs.dyn();
  lhs.setDyn(rhs);
  rhs = pTemp;
}
template <class T>
void SwapDynStorage(T *&lhs, ElemWithPtrStorage<T> &rhs) {
  T *pTemp = lhs;
  lhs = rhs.dyn();
  rhs.setDyn(pTemp);
}
template <class T>
void SwapDynStorage(T *&lhs, T *&rhs) {
  std::swap(lhs, rhs);
}

struct EmptyAlloc {};

template <class T, class SizeType>
class StaticVectorBase {
 public:
  using iterator = T *;
  using const_iterator = const T *;
  using allocator_type = EmptyAlloc;

  allocator_type get_allocator() const noexcept { return allocator_type(); }

  /// FixedCapacityVector is trivially relocatable if T is
  using trivially_relocatable = typename is_trivially_relocatable<T>::type;

  iterator begin() noexcept { return _firstEl.ptr(); }
  const_iterator begin() const noexcept { return _firstEl.ptr(); }
  const_iterator cbegin() const noexcept { return begin(); }

  SizeType size() const noexcept { return _size; }
  SizeType capacity() const noexcept { return _capa; }

 protected:
  explicit StaticVectorBase(SizeType inplaceCapa) noexcept : _capa(inplaceCapa), _size(0) {}

  StaticVectorBase(SizeType inplaceCapa, const EmptyAlloc &) noexcept : _capa(inplaceCapa), _size(0) {}

  void swap_impl(StaticVectorBase &o) noexcept(is_swap_noexcept<T>::value) {
    swap_deep(begin(), _size, o.begin(), o._size);
    std::swap(_size, o._size);
  }

  void move_construct(StaticVectorBase &o, SizeType) noexcept(is_move_construct_nothrow<T>::value) {
    amc::uninitialized_relocate_n(o.begin(), o._size, begin());
    _size = amc::exchange(o._size, 0);
  }

  void move_assign(StaticVectorBase &o, SizeType) noexcept(is_shift_nothrow<T>::value) {
    move_n(o.begin(), o._size, begin(), _size);
    _size = amc::exchange(o._size, 0);
  }

  void shrink_impl(SizeType) noexcept {}

  void incrSize() noexcept { ++_size; }
  void decrSize() noexcept { --_size; }
  SizeType &msize() noexcept { return _size; }
  void setSize(SizeType s) noexcept { _size = s; }

 private:
  // Capacity is added as member of the object to decrease code generation.
  // It's usually not a concern as SizeType is mostly small for FixedCapacityVector (upper bound is known at compile
  // time).
  const SizeType _capa;
  SizeType _size;
  ElemStorage<T> _firstEl;  // Inplace elements will be appended to this first one, do not add fields in between
};

template <class, class, class>
class SmallVectorBase;

/// Specialization for SmallVectors without some inline elements (like std::vector)
template <class T, class Alloc, class SizeType>
class StdVectorBase : private Alloc {
 public:
  using iterator = T *;
  using const_iterator = const T *;
  using allocator_type = Alloc;

  allocator_type get_allocator() const noexcept { return *this; }

  /// vector is always trivially relocatable
  using trivially_relocatable = std::true_type;

  iterator begin() noexcept { return _storage; }
  const_iterator begin() const noexcept { return _storage; }
  const_iterator cbegin() const noexcept { return _storage; }

  SizeType size() const noexcept { return _size; }
  SizeType capacity() const noexcept { return _capa; }

  ~StdVectorBase() {
    if (_storage) {
      freeStorage();
    }
  }

 protected:
  explicit StdVectorBase(SizeType) noexcept {}

  StdVectorBase(SizeType, const Alloc &alloc) noexcept : Alloc(alloc) {}

  void swap_impl(StdVectorBase &o) noexcept {
    std::swap(_storage, o._storage);
    std::swap(_capa, o._capa);
    std::swap(_size, o._size);
  }

  void move_construct(StdVectorBase &o, SizeType) noexcept {
    _storage = amc::exchange(o._storage, nullptr);
    _capa = amc::exchange(o._capa, 0);
    _size = amc::exchange(o._size, 0);
  }

  void move_assign(StdVectorBase &o, SizeType) noexcept {
    if (_storage) {
      amc::destroy_n(_storage, _size);
      freeStorage();  // Compared to swap, we can free memory directly for move assigment
    }
    _storage = amc::exchange(o._storage, nullptr);
    _capa = amc::exchange(o._capa, 0);
    _size = amc::exchange(o._size, 0);
  }

  void grow(uintmax_t minSize, bool exact = false);

  void shrink_impl(SizeType) noexcept {
    if (_size != _capa) {
      shrink();
    }
  }

  template <class, class, class>
  friend class SmallVectorBase;

  template <class, class, class>
  friend class StdVectorBase;

  template <class OSizeType>
  bool canSwapDynStorage(StaticVectorBase<T, OSizeType> &) const noexcept {
    return false;
  }
  template <class OSizeType, class OAlloc>
  bool canSwapDynStorage(SmallVectorBase<T, OAlloc, OSizeType> &o) const noexcept;

  template <class OSizeType, class OAlloc>
  bool canSwapDynStorage(StdVectorBase<T, OAlloc, OSizeType> &) const noexcept {
    return std::is_same<OAlloc, Alloc>::value;
  }

  template <class VectorType>
  void swapDynStorage(VectorType &o) noexcept {
    SwapDynStorage(_storage, o._storage);
  }
  template <class OSizeType>
  void swapDynStorage(StaticVectorBase<T, OSizeType> &) noexcept {}

  void incrSize() noexcept { ++_size; }
  void decrSize() noexcept { --_size; }
  SizeType &msize() noexcept { return _size; }
  SizeType &mcapacity() noexcept { return _capa; }
  void setSize(SizeType s) noexcept { _size = s; }

  iterator dynStorage() const noexcept { return _storage; }

 private:
  SizeType _capa = 0, _size = 0;
  T *_storage = nullptr;

  void shrink();
  void freeStorage() noexcept;
};

template <class T, class Alloc, class SizeType>
class SmallVectorBase : private Alloc {
 private:
  void destroyFreeStorage() noexcept {
    if (isSmall()) {
      amc::destroy_n(_storage.ptr(), _capa);
    } else if (_size != 0) {
      amc::destroy_n(_storage.dyn(), _size);
      freeStorage();
    }
  }

 public:
  using iterator = T *;
  using const_iterator = const T *;
  using allocator_type = Alloc;

  allocator_type get_allocator() const noexcept { return *this; }

  /// SmallVector is trivially relocatable if T is
  using trivially_relocatable = typename is_trivially_relocatable<T>::type;

  iterator begin() noexcept { return isSmall() ? _storage.ptr() : _storage.dyn(); }
  const_iterator begin() const noexcept { return isSmall() ? _storage.ptr() : _storage.dyn(); }
  const_iterator cbegin() const noexcept { return begin(); }

  SizeType size() const noexcept { return isSmall() ? _capa : _size; }
  SizeType capacity() const noexcept {
    return isSmall() && _size != std::numeric_limits<SizeType>::max() ? _size : _capa;
  }

  ~SmallVectorBase() {
    if (!isSmall()) {
      freeStorage();
    }
  }

 protected:
  /// Optim: To know if the SmallVector is small, we need one additional bool.
  /// The idea here, instead of storing an additional bool is to use a normally 'invalid' configuration of the two
  /// size and capacity values for small states: we inverse the capacity and the size in this case.
  /// This way, we are able to detect if the SmallVector is small or not comparing the two. There is one ambiguity
  /// though: when size == capacity. This could happen in both states of the SmallVector, so we need something else
  /// for this special configuration: we will set size to MAX in this case.
  /// Maximum size and capacity would make no sense in a SmallVector for a small state, because it could not grow
  /// (it could be transformed into a FixedCapacityVector, or, if larger size is needed, SizeType could be upgraded
  /// to a larger type). This invalid configuration is catched in a static_assert in SmallVector class.
  explicit SmallVectorBase(SizeType inplaceCapa) noexcept : _capa(0), _size(inplaceCapa) {}

  SmallVectorBase(SizeType inplaceCapa, const Alloc &alloc) noexcept : Alloc(alloc), _capa(0), _size(inplaceCapa) {}

  /// As explained above, if _capa == _size == SizeType::max() then it's necessarily in a large state.
  bool isSmall() const noexcept { return _capa < _size; }

  /// swap_impl is called by public method 'swap' for same SmallVector (same number of inplace elements).
  /// No need to check / adjust capacity for small states then (no throw guaranteed).
  void swap_impl(SmallVectorBase &o) noexcept(is_swap_noexcept<T>::value) {
    if (isSmall()) {
      if (o.isSmall()) {
        swap_deep(_storage.ptr(), _capa, o._storage.ptr(), o._capa);
      } else {
        SwapDynamicBuffer(o, *this);
      }
    } else {
      if (o.isSmall()) {
        SwapDynamicBuffer(*this, o);
      } else {
        SwapDynStorage(_storage, o._storage);
      }
    }
    std::swap(_capa, o._capa);
    std::swap(_size, o._size);
  }

  void move_construct(SmallVectorBase &o, SizeType inplaceCapa) noexcept(is_move_construct_nothrow<T>::value) {
    if (o.isSmall()) {
      amc::uninitialized_relocate_n(o._storage.ptr(), o._capa, _storage.ptr());
    } else {
      _storage.setDyn(o._storage.dyn());
    }
    _capa = amc::exchange(o._capa, 0);
    _size = amc::exchange(o._size, inplaceCapa);
  }

  void move_construct(StdVectorBase<T, Alloc, SizeType> &o) noexcept {
    if (o._capa != 0) {
      // Always steal dynamic buffer in this case, to make move construct faster
      _storage.setDyn(o._storage);
      o._storage = nullptr;
      _capa = amc::exchange(o._capa, 0);
      _size = amc::exchange(o._size, 0);
    }
  }

  void move_assign(SmallVectorBase &o, SizeType inplaceCapa) noexcept(is_shift_nothrow<T>::value) {
    if (o.isSmall()) {
      // No need to check 'this' capacity. If 'this' is small, then 'this' capacity is same as 'o'.
      // If 'this' is large, then 'this' capacity is larger by design.
      // Indeed, capacity cannot shrink, except for shrink_to_fit which resets to small state if possible.
      // Besides, if 'this' is large, let's not shrink to small size and keep our dynamic memory for now.
      // To sum-up, in this context, we do not touch our capacity, only move and relocates o's elements
      move_n(o._storage.ptr(), o._capa, begin(), size());
      if (o._size == kMaxSize) {
        if (isSmall()) {
          _size = kMaxSize;
        }
        o._size = inplaceCapa;
      }
      msize() = amc::exchange(o._capa, 0);
    } else {
      // Clear our stuff before stealing o's guts
      destroyFreeStorage();
      _storage.setDyn(o._storage.dyn());
      _capa = amc::exchange(o._capa, 0);
      _size = amc::exchange(o._size, inplaceCapa);
    }
  }

  void grow(uintmax_t minSize, bool exact = false);

  void shrink_impl(SizeType inplaceCapa) {
    if (!isSmall()) {
      if (_size <= inplaceCapa) {
        resetToSmall(inplaceCapa);
      } else if (_size != _capa) {
        shrink();
      }
    }
  }

  template <class, class, class>
  friend class SmallVectorBase;

  template <class, class, class>
  friend class StdVectorBase;

  template <class OAlloc, class OSizeType>
  bool canSwapDynStorage(StdVectorBase<T, OAlloc, OSizeType> &) const noexcept {
    return std::is_same<OAlloc, Alloc>::value && !isSmall();
  }
  template <class OSizeType>
  bool canSwapDynStorage(StaticVectorBase<T, OSizeType> &) const noexcept {
    return false;
  }
  template <class OSizeType, class OAlloc>
  bool canSwapDynStorage(SmallVectorBase<T, OAlloc, OSizeType> &o) const noexcept {
    return std::is_same<OAlloc, Alloc>::value && !isSmall() && !o.isSmall();
  }

  template <class VectorType>
  void swapDynStorage(VectorType &o) noexcept {
    SwapDynStorage(_storage, o._storage);
  }
  template <class OSizeType>
  void swapDynStorage(StaticVectorBase<T, OSizeType> &) noexcept {}

  static constexpr SizeType kMaxSize = std::numeric_limits<SizeType>::max();

  void incrSize() noexcept {
    if (isSmall()) {
      if (++_capa == _size) {
        _size = kMaxSize;
      }
    } else {
      (void)++_size;
    }
  }
  void decrSize() noexcept {
    if (isSmall()) {
      if (_size == kMaxSize) {
        _size = _capa;
      }
      (void)--_capa;
    } else {
      (void)--_size;
    }
  }
  void setSize(SizeType s) noexcept {
    if (isSmall()) {
      if (_size == kMaxSize) {
        if (s != _capa) {
          _size = _capa;
        }
      } else if (s == _size) {
        _size = kMaxSize;
      }
      _capa = s;
    } else {
      _size = s;
    }
  }

  /// Access to 'real' size member reference.
  SizeType &msize() noexcept { return isSmall() ? _capa : _size; }
  /// Access to 'real' capacity member reference. No need to check for small state here, this method is only called
  /// for large state vectors.
  SizeType &mcapacity() noexcept { return _capa; }

  iterator dynStorage() const noexcept { return _storage.dyn(); }

 private:
  SizeType _capa, _size;
  ElemWithPtrStorage<T> _storage;

  static inline void SwapDynamicBuffer(SmallVectorBase &vDynBuf,
                                       SmallVectorBase &vSmall) noexcept(is_swap_noexcept<T>::value) {
    T *oDynStorage = vDynBuf._storage.dyn();
    (void)amc::uninitialized_relocate_n(vSmall._storage.ptr(), vSmall._capa, vDynBuf._storage.ptr());
    vSmall._storage.setDyn(oDynStorage);
  }

  void shrink();
  void resetToSmall(SizeType);
  void freeStorage() noexcept;
};

template <class T, class Alloc, class SizeType>
template <class OSizeType, class OAlloc>
bool StdVectorBase<T, Alloc, SizeType>::canSwapDynStorage(SmallVectorBase<T, OAlloc, OSizeType> &o) const noexcept {
  return std::is_same<OAlloc, Alloc>::value && !o.isSmall();
}

template <class T, class SizeType, class GrowingPolicy>
class StaticVector : public StaticVectorBase<T, SizeType> {
 public:
  using reference = T &;
  using iterator = T *;
  using const_iterator = const T *;
  using size_type = SizeType;

  size_type max_size() const noexcept { return this->capacity(); }

  void reserve(size_type capacity) { GrowingPolicy::Check(capacity, this->capacity()); }

  template <class... Args>
  iterator emplace(const_iterator position, Args &&...args) {
    assert(position >= this->cbegin() && position <= this->cbegin() + this->size());
    GrowingPolicy::Check(this->size() + 1U, this->capacity());
    iterator pos = const_cast<iterator>(position);
    emplace_n(pos, this->size() - (pos - this->begin()), std::forward<Args>(args)...);
    this->incrSize();
    return pos;
  }

  template <class... Args>
  reference emplace_back(Args &&...args) {
    GrowingPolicy::Check(this->size() + 1U, this->capacity());
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, std::forward<Args &&>(args)...);
    this->incrSize();
    return *endIt;
  }

 protected:
  template <class... Args>
  explicit StaticVector(Args &&...args) noexcept : StaticVectorBase<T, SizeType>(std::forward<Args &&>(args)...) {}

  template <class, class, class>
  friend class StaticVector;

  template <class, class, class, bool>
  friend class DynamicVector;

  template <class VectorType>
  void swap2_impl(VectorType &o) noexcept(is_swap_noexcept<T>::value) {
    swap_deep(this->begin(), this->size(), o.begin(), o.size());
    swap_sizetype(this->msize(), o.msize());
  }

  // Adjust capacity methods take uintmax_t as parameter to check for size_type overflow
  void adjustCapacity(uintmax_t neededCapacity) const { GrowingPolicy::Check(neededCapacity, this->capacity()); }

  T *adjustCapacity(uintmax_t neededCapacity, const T *position) const {
    adjustCapacity(neededCapacity);
    return const_cast<T *>(position);
  }

  const T &adjustCapacity(uintmax_t neededCapacity, const T &v) const {
    adjustCapacity(neededCapacity);
    return v;
  }

  const T &adjustCapacity(uintmax_t neededCapacity, const T &v, const T **) const {
    adjustCapacity(neededCapacity);
    return v;
  }

  template <class VectorType>
  void adjustEachOtherCapacity(VectorType &o) const {
    adjustCapacity(o.size());
    o.adjustCapacity(this->size());
  }
};

template <class T, class Alloc, class SizeType, bool WithInlineElements>
struct DynamicVectorBaseTypeDispatcher {
  using type = typename std::conditional<WithInlineElements, SmallVectorBase<T, Alloc, SizeType>,
                                         StdVectorBase<T, Alloc, SizeType> >::type;
};

template <class T, class Alloc, class SizeType, bool WithInlineElements>
class DynamicVector : public DynamicVectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements>::type {
 public:
  using reference = T &;
  using iterator = T *;
  using const_iterator = const T *;
  using size_type = SizeType;
  using allocator_type = Alloc;

  size_type max_size() const noexcept { return std::numeric_limits<size_type>::max(); }

  void reserve(size_type capacity) {
    if (this->capacity() < capacity) {  // No condition hint, for reserve capacity is more likely to be adjusted
      this->grow(capacity, true);       // Reserve with exact capacity
    }
  }

  template <class... Args>
  iterator emplace(const_iterator position, Args &&...args) {
    assert(position >= this->cbegin() && position <= this->cbegin() + this->size());
    SizeType nElemsToShift = static_cast<SizeType>(this->size() - (position - this->begin()));
    iterator pos;
    if (AMC_UNLIKELY(this->size() == this->capacity())) {
      // construct before possible iterator invalidation from grow in constructor arguments
      ElemStorage<T> e;
      amc::construct_at(e.ptr(), std::forward<Args &&>(args)...);
      SizeType idx = static_cast<SizeType>(position - this->begin());
      this->grow(this->size() + 1U);
      pos = this->begin() + idx;
      if (nElemsToShift == 0) {
        amc::relocate_at(e.ptr(), pos);
      } else {
        shift_right(pos, nElemsToShift);
        try {
          relocate_after_shift(e.ptr(), pos);
        } catch (...) {
          shift_left(pos + 1, nElemsToShift);
          throw;
        }
      }
    } else {
      pos = const_cast<iterator>(position);
      emplace_n(pos, nElemsToShift, std::forward<Args>(args)...);
    }
    this->incrSize();
    return pos;
  }

  template <class... Args>
  reference emplace_back(Args &&...args) {
    iterator endIt;
    if (AMC_UNLIKELY(this->size() == this->capacity())) {
      // construct before possible iterator invalidation from grow in constructor arguments
      ElemStorage<T> e;
      amc::construct_at(e.ptr(), std::forward<Args &&>(args)...);
      this->grow(this->size() + 1U);
      endIt = this->dynStorage() + this->size();
      amc::relocate_at(e.ptr(), endIt);
    } else {
      endIt = this->begin() + this->size();
      amc::construct_at(endIt, std::forward<Args &&>(args)...);
    }
    this->incrSize();
    return *endIt;
  }

 protected:
  template <class... Args>
  explicit DynamicVector(Args &&...args) noexcept
      : DynamicVectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements>::type(std::forward<Args &&>(args)...) {}

  template <class, class, class>
  friend class StaticVector;

  template <class, class, class, bool>
  friend class DynamicVector;

  template <class OSizeType, class OGrowingPolicy>
  void swap2_impl(StaticVector<T, OSizeType, OGrowingPolicy> &o) noexcept(is_swap_noexcept<T>::value) {
    // Here 'o' cannot grow so we cannot swap any dynamic storage. Deeply swap all elements
    swap_deep(this->begin(), this->size(), o.begin(), o.size());
    swap_sizetype(this->msize(), o.msize());
  }

  template <class OAlloc, class OSizeType, bool OWithInlineElems>
  void swap2_impl(DynamicVector<T, OAlloc, OSizeType, OWithInlineElems> &o) noexcept(is_swap_noexcept<T>::value) {
    if (this->canSwapDynStorage(o)) {
      this->swapDynStorage(o);
      swap_sizetype(this->mcapacity(), o.mcapacity());
    } else {
      swap_deep(this->begin(), this->size(), o.begin(), o.size());
    }
    swap_sizetype(this->msize(), o.msize());
  }

  // Adjust capacity methods take uintmax_t as parameter to check for size_type overflow
  inline void adjustCapacity(uintmax_t neededCapacity) {
    if (AMC_UNLIKELY(static_cast<uintmax_t>(this->capacity()) < neededCapacity)) {
      this->grow(neededCapacity);
    }
  }

  inline T *adjustCapacity(uintmax_t neededCapacity, const T *position) {
    if (AMC_UNLIKELY(static_cast<uintmax_t>(this->capacity()) < neededCapacity)) {
      SizeType idx = static_cast<SizeType>(position - this->begin());  // pos will be invalidated
      this->grow(neededCapacity);
      return this->begin() + idx;
    }
    return const_cast<T *>(position);
  }

  inline const T &adjustCapacity(uintmax_t neededCapacity, const T &v) {
    if (AMC_UNLIKELY(static_cast<uintmax_t>(this->capacity()) < neededCapacity)) {
      const T *ptr = std::addressof(v);
      ptrdiff_t idx = ptr >= this->begin() && ptr < this->begin() + this->size() ? ptr - this->begin() : -1;
      this->grow(neededCapacity);
      if (idx != -1) {
        return this->begin()[idx];
      }
    }
    return v;
  }

  inline const T &adjustCapacity(uintmax_t neededCapacity, const T &v, const T **position) {
    if (AMC_UNLIKELY(static_cast<uintmax_t>(this->capacity()) < neededCapacity)) {
      const T *ptr = std::addressof(v);
      ptrdiff_t idx = ptr >= this->begin() && ptr < this->begin() + this->size() ? ptr - this->begin() : -1;
      SizeType itIdx = static_cast<SizeType>(*position - this->begin());  // pos will be invalidated
      this->grow(neededCapacity);
      *position = this->begin() + itIdx;
      if (idx != -1) {
        return this->begin()[idx];
      }
    }
    return v;
  }

  /// Adjust each other capacity for swap2 method
  /// Optim: For two 'Large' SmallVectors, no need to reserve as we can swap directly the dynamic storage
  /// Do not use public method reserve as it takes size_type argument instead of LargestSizeType
  /// (as the two size types may differ we should use LargestSizeType to avoid overflows)
  template <class VectorType>
  void adjustEachOtherCapacity(VectorType &o) {
    if (!this->canSwapDynStorage(o)) {
      adjustCapacity(o.size());
      o.adjustCapacity(this->size());
    }
  }
};

/// Standard growing policy which allows SmallVector to use dynamic memory
struct DynamicGrowingPolicy {};

template <class T, class Alloc, class SizeType, bool WithInlineElements, class GrowingPolicy>
struct VectorBaseTypeDispatcher {
  using type = typename std::conditional<std::is_same<GrowingPolicy, DynamicGrowingPolicy>::value,
                                         DynamicVector<T, Alloc, SizeType, WithInlineElements>,
                                         StaticVector<T, SizeType, GrowingPolicy> >::type;
};

/// VectorDestr simply defines a destructor for non trivially destructible T's, and stays trivially destructible for
/// trivially destructible T's.
/// There is no way to SFINAE the destructor so we use a derived class here.
template <class T, class Alloc, class SizeType, bool WithInlineElements, class GrowingPolicy, bool DefineDestructor>
class VectorDestr : public VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type {
 public:
  ~VectorDestr() { amc::destroy_n(this->begin(), this->size()); }

 protected:
  template <class... Args>
  explicit VectorDestr(Args &&...args) noexcept
      : VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type(
            std::forward<Args &&>(args)...) {}
};

template <class T, class Alloc, class SizeType, bool WithInlineElements, class GrowingPolicy>
class VectorDestr<T, Alloc, SizeType, WithInlineElements, GrowingPolicy, false>
    : public VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type {
 protected:
  template <class... Args>
  explicit VectorDestr(Args &&...args) noexcept
      : VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type(
            std::forward<Args &&>(args)...) {}
};

/// This macro allows usage of incomplete type for amc::vector, while keeping possibility for a FixedCapacityVector of a
/// trivially destructible type to stay trivially destructible
template <class T, bool WithInlineElements>
struct DefineDestructor : std::integral_constant<bool, !std::is_trivially_destructible<T>::value> {};

template <class T>
struct DefineDestructor<T, false> : std::integral_constant<bool, true> {};

/// Implementation class with definitions independent from the traits of type T and number of elements
template <class T, class Alloc, class SizeType, bool WithInlineElements, class GrowingPolicy>
class VectorImpl : public VectorDestr<T, Alloc, SizeType, WithInlineElements, GrowingPolicy,
                                      DefineDestructor<T, WithInlineElements>::value> {
 public:
  using value_type = T;
  using iterator = T *;
  using const_iterator = const T *;
  using pointer = T *;
  using const_pointer = const T *;
  using difference_type = ptrdiff_t;
  using reference = T &;
  using const_reference = const T &;
  using size_type = SizeType;
  using allocator_type = Alloc;

  VectorImpl &operator=(std::initializer_list<T> list) {
    assign(list.begin(), list.end());
    return *this;
  }

  bool empty() const noexcept { return this->size() == 0; }

  iterator end() noexcept { return this->begin() + this->size(); }
  const_iterator end() const noexcept { return this->begin() + this->size(); }
  const_iterator cend() const noexcept { return end(); }

  // reverse iterator support
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  reverse_iterator rbegin() noexcept { return reverse_iterator(end()); }
  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(end()); }

  reverse_iterator rend() noexcept { return reverse_iterator(this->begin()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(this->begin()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(this->begin()); }

  pointer data() noexcept { return this->begin(); }
  const_pointer data() const noexcept { return this->begin(); }

  reference operator[](size_type idx) {
    assert(idx < this->size());
    return this->begin()[idx];
  }
  const_reference operator[](size_type idx) const {
    assert(idx < this->size());
    return this->begin()[idx];
  }

  reference at(size_type idx) {
    if (idx >= this->size()) throw std::out_of_range("Out of Range access");
    return this->begin()[idx];
  }
  const_reference at(size_type idx) const {
    if (idx >= this->size()) throw std::out_of_range("Out of Range access");
    return this->begin()[idx];
  }

  reference front() {
    assert(!empty());
    return *this->begin();
  }
  const_reference front() const {
    assert(!empty());
    return *this->begin();
  }

  reference back() {
    assert(!empty());
    return *(end() - 1);
  }
  const_reference back() const {
    assert(!empty());
    return *(end() - 1);
  }

  bool operator==(const VectorImpl &o) const {
    return this->size() == o.size() && std::equal(this->begin(), end(), o.begin());
  }
  bool operator!=(const VectorImpl &o) const { return !(*this == o); }

  bool operator<(const VectorImpl &o) const {
    return std::lexicographical_compare(this->begin(), end(), o.begin(), o.end());
  }
  bool operator<=(const VectorImpl &o) const { return !(o < *this); }
  bool operator>(const VectorImpl &o) const { return o < *this; }
  bool operator>=(const VectorImpl &o) const { return !(*this < o); }

  void pop_back() {
    assert(!empty());
    amc::destroy_at(this->end() - 1);
    this->decrSize();
  }

  T pop_back_val() {
    T lastEl = std::move(back());
    pop_back();
    return lastEl;
  }

  void clear() noexcept {
    amc::destroy_n(this->begin(), this->size());
    this->setSize(0);
  }

  void assign(size_type count, const_reference v) {
    if (this->size() < count) {
      const_reference newV = this->adjustCapacity(count, v);
      fill(this->begin(), this->size(), count, newV);
    } else {
      // copy to already existing elements and destroy remaining ones
      std::fill_n(this->begin(), count, v);
      amc::destroy_n(this->begin() + count, this->size() - count);
    }
    this->setSize(count);
  }

  /// Replaces the contents of the container.
  /// The behavior is undefined if either argument is an iterator into *this.
  template <class InputIt, typename std::enable_if<!std::is_integral<InputIt>::value, bool>::type = true>
  void assign(InputIt first, InputIt last) {
    uintmax_t count = std::distance(first, last);
    if (static_cast<uintmax_t>(this->size()) < count) {
      this->adjustCapacity(count);
      assign_n(first, static_cast<SizeType>(count), this->begin(), this->size());
    } else {
      // copy to already existing elements and destroy remaining ones
      amc::destroy(std::copy(first, last, this->begin()), end());
    }
    this->setSize(static_cast<SizeType>(count));
  }

  void assign(std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); }

  iterator insert(const_iterator position, const_reference v) {
    assert(position >= this->cbegin() && position <= cend());
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U, v, &position);
    iterator pos = const_cast<iterator>(position);
    insert_n(pos, this->size() - (pos - this->begin()), newV);
    this->incrSize();
    return pos;
  }

  iterator insert(const_iterator position, T &&v) {
    assert(position >= this->cbegin() && position <= cend());
    iterator pos = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U, position);
    insert_n(pos, this->size() - (pos - this->begin()), std::move(v));
    this->incrSize();
    return pos;
  }

  iterator insert(const_iterator position, size_type count, const_reference v) {
    assert(position >= this->cbegin() && position <= cend());
    iterator pos;
    if (count > 0) {
      const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count, v, &position);
      pos = const_cast<iterator>(position);
      SizeType nElemsToShift = static_cast<SizeType>(this->size() - (pos - this->begin()));
      if (nElemsToShift == 0) {
        std::uninitialized_fill_n(pos, count, newV);
      } else {
        shift_right(pos, nElemsToShift, count);
        fill_after_shift(pos, nElemsToShift, count, newV);
      }
      this->setSize(this->size() + count);
    } else {
      pos = const_cast<iterator>(position);
    }
    return pos;
  }

  /// Inserts elements at the specified location in the container.
  /// The behavior is undefined if first and last are iterators into *this
  template <class InputIt, typename std::enable_if<!std::is_integral<InputIt>::value, bool>::type = true>
  iterator insert(const_iterator position, InputIt first, InputIt last) {
    assert(position >= this->cbegin() && position <= cend());
    typename std::iterator_traits<InputIt>::difference_type count = std::distance(first, last);
    iterator pos;
    if (count > 0) {
      pos = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count, position);
      SizeType nElemsToShift = static_cast<SizeType>(this->size() - (pos - this->begin()));
      if (nElemsToShift == 0) {
        amc::uninitialized_copy_n(first, count, pos);
      } else {
        shift_right(pos, nElemsToShift, static_cast<SizeType>(count));
        copy_after_shift(first, nElemsToShift, static_cast<SizeType>(count), pos);
      }
      this->setSize(static_cast<SizeType>(this->size() + count));
    } else {
      pos = const_cast<iterator>(position);
    }
    return pos;
  }

  iterator insert(const_iterator pos, std::initializer_list<T> list) { return insert(pos, list.begin(), list.end()); }

  iterator erase(const_iterator position) {
    assert(position >= this->cbegin() && position < cend());
    iterator it = const_cast<iterator>(position);
    erase_at(it, this->size() - (position - this->begin()) - 1);
    this->decrSize();
    return it;
  }

  iterator erase(const_iterator first, const_iterator last) {
    assert(first <= last && first >= this->cbegin() && last <= cend());
    iterator mfirst = const_cast<iterator>(first);
    SizeType n = static_cast<SizeType>(last - first);
    erase_n(mfirst, n, static_cast<SizeType>(this->size() - (last - this->begin())));
    this->setSize(this->size() - n);
    return mfirst;
  }

  void push_back(const_reference v) {
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U, v);
    amc::construct_at(end(), newV);
    this->incrSize();
  }

  void push_back(T &&v) {
    this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U);
    amc::construct_at(end(), std::move(v));
    this->incrSize();
  }

  void resize(size_type count) {
    if (this->size() < count) {
      this->adjustCapacity(count);
      amc::uninitialized_value_construct_n(end(), count - this->size());
    } else {
      amc::destroy_n(this->begin() + count, this->size() - count);
    }
    this->setSize(count);
  }

  void resize(size_type count, const_reference v) {
    if (this->size() < count) {
      const_reference newV = this->adjustCapacity(count, v);
      std::uninitialized_fill_n(end(), count - this->size(), newV);
    } else {
      amc::destroy_n(this->begin() + count, this->size() - count);
    }
    this->setSize(count);
  }

  // Additional convenient methods not present in std::vector

  /// Like swap, but can take other vectors of same type with different inline number of elements
  /// It has one drawback though: it can throw (as it can make SmallVectors grow)
  template <class OAlloc, class OSizeType, bool OWithInlineElements, class OGrowingPolicy>
  void swap2(VectorImpl<T, OAlloc, OSizeType, OWithInlineElements, OGrowingPolicy> &o) {
    this->adjustEachOtherCapacity(o);
    this->swap2_impl(o);
  }

  /// Append elements to the end of the vector. Similar to: vec.insert(vec.end(), ...).
  /// The behavior is undefined if first and last are iterators into *this
  template <class InputIt, typename std::enable_if<!std::is_integral<InputIt>::value, bool>::type = true>
  void append(InputIt first, InputIt last) {
    this->adjustCapacity(static_cast<uintmax_t>(this->size()) + std::distance(first, last));
    this->setSize(static_cast<SizeType>(amc::uninitialized_copy(first, last, end()) - this->begin()));
  }

  void append(size_type count) {
    this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count);
    amc::uninitialized_value_construct_n(end(), count);
    this->setSize(this->size() + count);
  }

  void append(size_type count, const_reference v) {
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count, v);
    std::uninitialized_fill_n(end(), count, newV);
    this->setSize(this->size() + count);
  }

  void append(std::initializer_list<T> list) { append(list.begin(), list.end()); }

 protected:
  template <class... Args>
  explicit VectorImpl(Args &&...args) noexcept
      : VectorDestr<T, Alloc, SizeType, WithInlineElements, GrowingPolicy,
                    DefineDestructor<T, WithInlineElements>::value>(std::forward<Args &&>(args)...) {}
};

template <class T, class A, class S, bool I, class G>
void swap(VectorImpl<T, A, S, I, G> &lhs, VectorImpl<T, A, S, I, G> &rhs) {
  lhs.swap2(rhs);
}

/// Add inplace storage when needed
template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N, class Enable = void>
class VectorWithInplaceStorage : public VectorImpl<T, Alloc, SizeType, true, GrowingPolicy> {
 protected:
  template <class... Args>
  explicit VectorWithInplaceStorage(Args &&...args) noexcept
      : VectorImpl<T, Alloc, SizeType, true, GrowingPolicy>(std::forward<Args &&>(args)...) {}

 private:
  ElemStorage<T>
      _elems[N - (std::is_same<GrowingPolicy, DynamicGrowingPolicy>::value ? ElemWithPtrStorage<T>::kNbSlots : 1)];
};

template <class T, class GrowingPolicy, uintmax_t N>
struct NoInlineStorage : std::integral_constant<bool, std::is_same<GrowingPolicy, DynamicGrowingPolicy>::value &&
                                                          (N <= ElemWithPtrStorage<T>::kNbSlots)> {};

template <class T, class GrowingPolicy>
struct NoInlineStorage<T, GrowingPolicy, 0> : std::integral_constant<bool, true> {};

template <class T, class GrowingPolicy>
struct NoInlineStorage<T, GrowingPolicy, 1> : std::integral_constant<bool, true> {};

template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N>
class VectorWithInplaceStorage<T, Alloc, SizeType, GrowingPolicy, N,
                               typename std::enable_if<NoInlineStorage<T, GrowingPolicy, N>::value>::type>
    : public VectorImpl<T, Alloc, SizeType, (N != 0), GrowingPolicy> {
 protected:
  template <class... Args>
  explicit VectorWithInplaceStorage(Args &&...args) noexcept
      : VectorImpl<T, Alloc, SizeType, (N != 0), GrowingPolicy>(std::forward<Args &&>(args)...) {}
};

template <uintmax_t N, class SizeType>
constexpr SizeType SanitizeInlineSize() {
  static_assert(N <= static_cast<uintmax_t>(std::numeric_limits<SizeType>::max()),
                "Inline storage too large for SizeType");
  return static_cast<SizeType>(N);
}
}  // namespace vec

template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N>
class Vector : public vec::VectorWithInplaceStorage<T, Alloc, SizeType, GrowingPolicy, N> {
 private:
  using Base = vec::VectorWithInplaceStorage<T, Alloc, SizeType, GrowingPolicy, N>;

  /// Static checks to make sure of correct usage of this class
  static_assert(!std::is_same<GrowingPolicy, vec::DynamicGrowingPolicy>::value ||
                    N < std::numeric_limits<SizeType>::max(),
                "Invalid Vector: cannot grow, could be FixedCapacityVector. Use larger size_type or decrease "
                "number of inline elements.");

  static_assert(std::is_same<GrowingPolicy, vec::DynamicGrowingPolicy>::value ==
                    !std::is_same<Alloc, vec::EmptyAlloc>::value,
                "FixedCapacityVector should use EmptyAlloc");

 public:
  using const_reference = typename Base::const_reference;
  using size_type = SizeType;

  static constexpr size_type kInlineCapacity = N;

  Vector() noexcept : Base(N) {}

  explicit Vector(const Alloc &alloc) noexcept : Base(N, alloc) {}

  template <class InputIt, typename std::enable_if<!std::is_integral<InputIt>::value, bool>::type = true>
  Vector(InputIt first, InputIt last, const Alloc &alloc = Alloc()) : Base(N, alloc) {
    this->append(first, last);
  }

  explicit Vector(size_type count, const Alloc &alloc = Alloc()) : Base(N, alloc) { this->append(count); }

  Vector(size_type count, const_reference v, const Alloc &alloc = Alloc()) : Base(N, alloc) { this->append(count, v); }

  Vector(const Vector &o) : Base(N) { this->append(o.begin(), o.end()); }

  Vector(const Vector &o, const Alloc &alloc) : Base(N, alloc) { this->append(o.begin(), o.end()); }

  Vector(Vector &&o) noexcept(N == 0 || vec::is_move_construct_nothrow<T>::value) : Base(N) {
    this->move_construct(o, N);
  }

  /// Build a SmallVector from a vector, stealing its dynamic storage.
  template <SizeType ON = N, class OGrowingPolicy = GrowingPolicy>
  Vector(
      Vector<T, Alloc, SizeType, OGrowingPolicy, 0> &&o,
      typename std::enable_if<std::is_same<OGrowingPolicy, vec::DynamicGrowingPolicy>::value && (ON > 0)>::type * = 0)
      : Base(N) {
    this->move_construct(o);
  }

  Vector(Vector &&o, const Alloc &alloc) noexcept(N == 0 || vec::is_move_construct_nothrow<T>::value) : Base(N, alloc) {
    this->move_construct(o, N);
  }

  Vector(std::initializer_list<T> init, const Alloc &alloc = Alloc()) : Base(N, alloc) {
    this->append(init.begin(), init.end());
  }

  Vector &operator=(const Vector &o) {
    if (AMC_LIKELY(this != &o)) {
      this->assign(o.begin(), o.end());
    }
    return *this;
  }

  // Move assignment operator only defined here as it requires same N
  Vector &operator=(Vector &&o) noexcept(N == 0 || vec::is_shift_nothrow<T>::value) {
    if (AMC_LIKELY(this != &o)) {
      this->move_assign(o, N);
    }
    return *this;
  }

  // Define swap here instead of VectorImpl as noexcept swap is possible only for same inplace capacity
  void swap(Vector &o) noexcept(N == 0 || vec::is_swap_noexcept<T>::value) { this->swap_impl(o); }

  void shrink_to_fit() { this->shrink_impl(N); }
};

template <class T, class A, class S, class G, S N>
void swap(Vector<T, A, S, G, N> &lhs, Vector<T, A, S, G, N> &rhs) noexcept(N == 0 || vec::is_swap_noexcept<T>::value) {
  lhs.swap(rhs);
}
}  // namespace amc
