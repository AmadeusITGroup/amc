#pragma once

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <memory>
#include <stdexcept>

#include "config.hpp"
#include "memory.hpp"
#include "type_traits.hpp"
#include "utility.hpp"

#ifndef AMC_CXX17
#include "isdetected.hpp"
#endif

#ifdef AMC_CXX23
#include <ranges>
#endif

#ifdef AMC_CXX20
#include "algorithm.hpp"
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

/// Tells whether 'p' points to one of the 'n' elements starting at 'first'.
/// 'p' may point to an object unrelated to this range: the result of the built-in relational operators is then
/// unspecified, whereas std::less provides a strict total order over pointers.
template <class T, class SizeType>
inline bool IsInRange(const T* p, const T* first, SizeType n) noexcept {
  return !std::less<const T*>()(p, first) && std::less<const T*>()(p, first + n);
}

/// Returns 'v' as is if it is a T, otherwise explicitly converted to a T: elements of a range of another type are
/// converted once and explicitly, instead of implicitly (MSVC warns about the narrowing ones).
template <class T, class U,
          typename std::enable_if<std::is_same<typename std::decay<U>::type, T>::value, bool>::type = true>
inline U&& AsValueType(U&& v) noexcept {
  return std::forward<U>(v);
}

template <class T, class U,
          typename std::enable_if<!std::is_same<typename std::decay<U>::type, T>::value, bool>::type = true>
inline T AsValueType(U&& v) {
  return static_cast<T>(std::forward<U>(v));
}

/// Shift 'n' elements starting at 'first' one slot to the right
/// Requirements: n != 0, with uninitialized memory starting at 'first + n'
/// Warning: no destroy is called for elements which has been moved from.
/// If a move throws, the element constructed in the uninitialized memory is destroyed.
/// (not declared noexcept(is_shift_nothrow<T>::value), as GCC warns about the rethrow when it is true)
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void shift_right(T* first, SizeType n) {
  T* last = first + n;
  amc::construct_at(last, std::move(*(last - 1)));
  try {
    std::move_backward(first, last - 1, last);
  } catch (...) {
    amc::destroy_at(last);
    throw;
  }
}

/// Specialization for trivially relocatable types. Just use memmove here.
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void shift_right(T* first, SizeType n) noexcept {
  (void)amc::uninitialized_relocate_n(first, n, first + 1);
}

/// Shift 'n' elements starting at 'first' 'count' slots to the right
/// Requirements: uninitialized memory starting at 'first + n'
/// Warning: no destroy is called for elements which has been moved from.
/// If a move throws, the elements constructed in the uninitialized memory are destroyed.
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
void shift_right(T* first, SizeType n, SizeType count) {
  if (count < n) {
    T* last = first + n;
    amc::uninitialized_move_n(last - count, count, last);  // move last 'count' elems to uninitialized storage
    try {
      std::move_backward(first, last - count, last);  // move remaining 'n - count' elems to initialized storage
    } catch (...) {
      amc::destroy_n(last, count);
      throw;
    }
  } else {
    // no overlap, we shift all elements to uninitialized memory
    amc::uninitialized_move_n(first, n, first + count);
  }
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void shift_right(T* first, SizeType n, SizeType count) noexcept {
  (void)amc::uninitialized_relocate_n(first, n, first + count);
}

/// Undo a 'shift_right(first, n, count)' when copying the elements to insert to the 'count' slots starting at 'first'
/// throws, the copy having destroyed the elements it constructed in uninitialized memory.
/// For non trivially relocatable types, the shifted elements constructed past the old end ('first + n') are destroyed,
/// the elements before it keep valid but unspecified values (basic guarantee).
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void undo_shift_right(T* first, SizeType n, SizeType count) noexcept {
  amc::destroy_n(first + std::max(n, count), std::min(n, count));
}

/// For trivially relocatable types, the slots of the elements to insert only contain uninitialized memory: the shifted
/// elements are relocated back to their original location (strong guarantee).
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void undo_shift_right(T* first, SizeType n, SizeType count) noexcept {
  (void)amc::uninitialized_relocate_n(first + count, n, first);
}

/// Fill 'count' 'v' values at memory starting at 'first', with first 'n' slots on initialized memory,
/// and next 'count - n' slots on uninitialized memory if there is overlap.
/// Initialized memory is filled first: if a copy throws, there is no element constructed in uninitialized memory.
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void fill_after_shift(T* first, SizeType n, SizeType count, const T& v) {
  if (n < count) {
    std::fill_n(first, n, v);
    std::uninitialized_fill_n(first + n, count - n, v);
  } else {
    std::fill_n(first, count, v);
  }
}

/// shift_right leaves only uninitialized memory for trivially relocatable type
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void fill_after_shift(T* first, SizeType, SizeType count, const T& v) {
  std::uninitialized_fill_n(first, count, v);
}

/// copy from a range to available location divided in two parts: one on initialized memory, other one on raw memory
/// Requirements: d_n < count
template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<!std::is_trivially_copyable<T>::value, bool>::type = true>
inline void assign_n(ForwardIt first, SizeType count, T* d_first, SizeType d_n) {
  assert(d_n < count);
  if (d_n > 0) {
    *d_first++ = AsValueType<T>(*first);  // rewrite copy_n to avoid double iteration on the input elements
    for (SizeType i = 1; i < d_n; ++i) {
      *d_first++ = AsValueType<T>(*++first);
    }
    (void)++first;
  }
  amc::uninitialized_copy_n(first, count - d_n, d_first);
}

template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<std::is_trivially_copyable<T>::value, bool>::type = true>
inline void assign_n(ForwardIt first, SizeType count, T* d_first, SizeType) {
  amc::uninitialized_copy_n(first, count, d_first);
}

/// Copy 'count' elements starting at 'first' to 'pos' location
/// To be used in conjunction with 'shift_right'
/// Requirements: n > 0 ('shift_right' shifted elements)
template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void copy_after_shift(ForwardIt first, SizeType n, SizeType count, T* pos) {
  assert(n > 0);
  if (n < count) {
    *pos++ = AsValueType<T>(*first);  // rewrite copy_n to avoid double iteration on the input elements
    for (SizeType i = 1; i < n; ++i) {
      *pos++ = AsValueType<T>(*++first);
    }
    (void)++first;
    amc::uninitialized_copy_n(first, count - n, pos);
  } else {
    std::copy_n(first, count, pos);
  }
}

template <class ForwardIt, class SizeType, class T,
          typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void copy_after_shift(ForwardIt first, SizeType, SizeType count, T* pos) {
  amc::uninitialized_copy_n(first, count, pos);
}

/// Shift 'n' elements starting at 'first' one slot back to the left
/// Requirements: n != 0 with one slot of initialized memory at first - 1
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
void shift_left(T* first, SizeType n) noexcept(is_shift_nothrow<T>::value) {
  *(first - 1) = std::move(*first);  // move first element to initialized memory slot 'first - 1'
  // move next 'n - 1' elements one slot to the left and destroy last moved element
  amc::destroy_at(std::move(first + 1, first + n, first));
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
void shift_left(T* first, SizeType n) noexcept {
  (void)amc::uninitialized_relocate_n(first, n, first - 1);
}

/// Erase 'n' elements starting at 'first', shifting the next 'count' elements to memory starting at 'first'
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_n(T* first, SizeType n, SizeType count) {
  amc::destroy_n(std::move(first + n, first + n + count, first), n);
}
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_n(T* first, SizeType n, SizeType count) {
  amc::destroy_n(first, n);
  (void)amc::uninitialized_relocate_n(first + n, count, first);
}

/// Erase one element starting at 'first', shifting the next 'count' elements to memory starting at 'first'
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_at(T* first, SizeType count) {
  amc::destroy_at(std::move(first + 1, first + 1 + count, first));
}
template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void erase_at(T* first, SizeType count) {
  amc::destroy_at(first);
  (void)amc::uninitialized_relocate_n(first + 1, count, first);
}

/// Assign 'v' to a memory starting at 'first', with 'n' slots on initialized memory, 'count - n'
/// slots on uninitialized memory.
/// Requirements: n < count
template <class T, class SizeType, typename std::enable_if<!std::is_trivially_copyable<T>::value, bool>::type = true>
inline void fill(T* first, SizeType n, SizeType count, const T& v) {
  // initialized memory first: if a copy throws, there is no element constructed in uninitialized memory to destroy
  std::fill_n(first, n, v);
  std::uninitialized_fill_n(first + n, count - n, v);
}

template <class T, class SizeType, typename std::enable_if<std::is_trivially_copyable<T>::value, bool>::type = true>
inline void fill(T* first, SizeType, SizeType count, const T& v) {
  std::uninitialized_fill_n(first, count, v);
}

template <class T, class SizeType1, class SizeType2>
void swap_deep(T* first1, SizeType1 count1, T* first2, SizeType2 count2) noexcept(is_swap_noexcept<T>::value) {
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

/// Tells whether two vectors of given capacities (and of potentially different size types) can exchange their dynamic
/// storages, which is possible only if each capacity (and thus size) fits in the size type of the other vector.
template <class SizeType1, class SizeType2>
inline bool CapacitiesFitEachOther(SizeType1 capacity1, SizeType2 capacity2) noexcept {
  return static_cast<uintmax_t>(capacity1) <= static_cast<uintmax_t>(std::numeric_limits<SizeType2>::max()) &&
         static_cast<uintmax_t>(capacity2) <= static_cast<uintmax_t>(std::numeric_limits<SizeType1>::max());
}

/// Swap of sizes (or capacities) of different size types.
/// Requirements: each value fits in the size type of the other one. Callers ensure it: swap2 grows each vector to the
/// size of the other one first (which throws if it does not fit), and only exchanges dynamic storages whose capacities
/// fit in the size type of each other.
template <class SizeType1, class SizeType2>
inline void swap_sizetype(SizeType1& lhs, SizeType2& rhs) noexcept {
  assert(CapacitiesFitEachOther(lhs, rhs));
  SizeType1 tmp = lhs;
  lhs = static_cast<SizeType1>(rhs);
  rhs = static_cast<SizeType2>(tmp);
}

template <class SizeType>
inline void swap_sizetype(SizeType& lhs, SizeType& rhs) noexcept {
  std::swap(lhs, rhs);
}

/// Move 'n' objects starting at 'first' to a range starting at 'd_first' containing already 'd_n' instantiated objects
template <class T, class SizeType, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void move_n(T* first, SizeType n, T* d_first, SizeType d_n) {
  std::move(first, first + std::min(n, d_n), d_first);
  if (d_n < n) {
    amc::uninitialized_move_n(first + d_n, n - d_n, d_first + d_n);
  } else {
    amc::destroy_n(d_first + n, d_n - n);
  }
  amc::destroy_n(first, n);
}

template <class T, class SizeType, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void move_n(T* first, SizeType n, T* d_first, SizeType d_n) {
  amc::destroy_n(d_first, d_n);
  (void)amc::uninitialized_relocate_n(first, n, d_first);
}

template <class T, class V, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void assign_after_shift(T* pos, V&& v) {
  *pos = std::forward<V>(v);
}
template <class T, class V, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void assign_after_shift(T* pos, V&& v) {
  amc::construct_at(pos, std::forward<V>(v));
}

/// Insert an lvalue 'v' at 'pos', shifting the 'n' elements starting at 'pos' one slot to the right.
///
/// 'v' is allowed to be a reference to an element already stored in the vector: this is the case exercised by the
/// 'const_reference' overload of 'vector::insert' and the C++ Standard requires it to be well-defined, even when
/// the referenced element is located at or after 'pos' - i.e. among the elements that this function shifts.
/// Nothing in [vector.modifiers] forbids 'v' from aliasing the container for this overload, contrary to the range
/// and 'InputIt' overloads, which explicitly document that their arguments must not be iterators into '*this'.
///
/// 'shift_right' moves the elements of '[pos, pos + n)' one slot to the right, so if 'v' aliases one of them the
/// object it designates ends up one slot further, at 'std::addressof(v) + 1'. We therefore detect this situation
/// *before* shifting and bind our source reference accordingly; reading it back *after* the shift then yields the
/// intended value. Only a copy is performed, leaving the (shifted) source element untouched, exactly as mandated.
template <class T, class SizeType>
inline void insert_n(T* pos, SizeType n, const T& v) {
  if (n == 0) {
    amc::construct_at(pos, v);
  } else {
    // If 'v' aliases one of the elements about to be shifted, 'shift_right' relocates it one slot to the right.
    // Bind the source to its post-shift location so the correct value is read once the shift has been performed.
    const T* pv = std::addressof(v);
    const T& src = IsInRange(pv, pos, n) ? *(pv + 1) : v;
    shift_right(pos, n);
    try {
      assign_after_shift(pos, src);
    } catch (...) {
      shift_left(pos + 1, n);
      throw;
    }
  }
}

/// Insert an rvalue 'v' at 'pos', shifting the 'n' elements starting at 'pos' one slot to the right.
///
/// This overload is selected by the 'T&&' overload of 'vector::insert'. As permitted by [res.on.arguments], an
/// rvalue argument is treated as a temporary: 'v' is assumed *not* to alias the vector, so - unlike the 'const T&'
/// overload above - no aliasing check is performed and the element is moved into place.
template <class T, class SizeType>
inline void insert_n(T* pos, SizeType n, T&& v) {
  if (n == 0) {
    amc::construct_at(pos, std::move(v));
  } else {
    shift_right(pos, n);
    try {
      assign_after_shift(pos, std::move(v));
    } catch (...) {
      shift_left(pos + 1, n);
      throw;
    }
  }
}

template <class T, typename std::enable_if<!amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void relocate_after_shift(T* e, T* dest) {
  *dest = std::move(*e);
  amc::destroy_at(e);
}
template <class T, typename std::enable_if<amc::is_trivially_relocatable<T>::value, bool>::type = true>
inline void relocate_after_shift(T* e, T* dest) {
  amc::relocate_at(e, dest);
}

template <class T>
class ElemStorage {
 public:
  T* ptr() noexcept { return reinterpret_cast<T*>(this); }
  const T* ptr() const noexcept { return reinterpret_cast<const T*>(this); }

 private:
  alignas(T) std::uint8_t _el[sizeof(T)];
};

/// Element constructed outside of the vector, to be relocated into it afterwards.
/// It is destroyed at the end of its scope, unless 'release' has been called after a successful relocation.
/// Relocation functions provide this guarantee: if they throw, the element has not been relocated (and is still alive).
template <class T>
class TemporaryElem {
 public:
  template <class... Args>
  explicit TemporaryElem(Args&&... args) {
    amc::construct_at(_storage.ptr(), std::forward<Args>(args)...);
  }

  TemporaryElem(const TemporaryElem&) = delete;
  TemporaryElem(TemporaryElem&& other) noexcept = delete;
  TemporaryElem& operator=(const TemporaryElem&) = delete;
  TemporaryElem& operator=(TemporaryElem&&) noexcept = delete;

  ~TemporaryElem() {
    if (_owned) {
      amc::destroy_at(_storage.ptr());
    }
  }

  T* ptr() noexcept { return _storage.ptr(); }

  void release() noexcept { _owned = false; }

 private:
  ElemStorage<T> _storage;
  bool _owned = true;
};

/// Relocate the element 'e', constructed outside of the vector, at 'pos', shifting the 'n' elements starting at 'pos'
/// one slot to the right.
template <class T, class SizeType>
inline void relocate_insert_n(T* pos, SizeType n, T* e) {
  if (n == 0) {
    amc::relocate_at(e, pos);
  } else {
    shift_right(pos, n);
    try {
      relocate_after_shift(e, pos);
    } catch (...) {
      shift_left(pos + 1, n);
      throw;
    }
  }
}

/// Construct at 'pos' the T from 'args' parameters, shifting 'n' elements starting at 'pos' to the right.
///
/// 'args' may reference, directly or indirectly, an element of the vector, including one of those about to be shifted
/// (the C++ Standard requires 'emplace' to support it, see LWG 2164). Therefore, like standard library
/// implementations, the new element is constructed in a temporary storage before shifting any element.
template <class T, class SizeType, class... Args>
inline void emplace_n(T* pos, SizeType n, Args&&... args) {
  if (n == 0) {
    amc::construct_at(pos, std::forward<Args>(args)...);
  } else {
    TemporaryElem<T> e(std::forward<Args>(args)...);
    relocate_insert_n(pos, n, e.ptr());
    e.release();
  }
}

/// Number of elements of 'ElemSize' bytes fitting in 'NbBytes' bytes, at least 1.
/// Template parameters, as GCC warns that 'sizeof(pointer) / sizeof(T)' does not compute the number of elements of an
/// array (-Wsizeof-pointer-div).
template <std::size_t NbBytes, std::size_t ElemSize>
struct NbSlots : std::integral_constant<std::size_t, (ElemSize < NbBytes ? NbBytes / ElemSize : 1U)> {};

/// This class represents a merge of a pointer and some inline storage elements.
/// Thanks to this optimization, SmallVector behaves like a string type with SSO
/// Example : for a system with pointer size of 8 bytes,
///           sizeof(SmallVector<char, 8>) == sizeof(vector<char>)
///           because 8 chars can be stored in a pointer.
template <class T>
class ElemWithPtrStorage {
 public:
  using pointer = T*;
  using const_pointer = const T*;

  static constexpr std::size_t kNbSlots = NbSlots<sizeof(pointer), sizeof(T)>::value;

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
  static constexpr auto kPtrAlign = std::alignment_of<T*>::value;

#ifdef AMC_CXX14
  // std::max is constexpr from C++14
  alignas(std::max(kTAlign, kPtrAlign)) std::uint8_t _el[std::max(sizeof(T), sizeof(T*))];
#else
  alignas(kPtrAlign < kTAlign ? kTAlign : kPtrAlign) std::uint8_t _el[sizeof(T*) < sizeof(T) ? sizeof(T) : sizeof(T*)];
#endif
};

template <class T>
void SwapDynStorage(ElemWithPtrStorage<T>& lhs, ElemWithPtrStorage<T>& rhs) {
  T* pTemp = lhs.dyn();
  lhs.setDyn(rhs.dyn());
  rhs.setDyn(pTemp);
}

template <class T>
void SwapDynStorage(ElemWithPtrStorage<T>& lhs, T*& rhs) {
  T* pTemp = lhs.dyn();
  lhs.setDyn(rhs);
  rhs = pTemp;
}
template <class T>
void SwapDynStorage(T*& lhs, ElemWithPtrStorage<T>& rhs) {
  T* pTemp = lhs;
  lhs = rhs.dyn();
  rhs.setDyn(pTemp);
}
template <class T>
void SwapDynStorage(T*& lhs, T*& rhs) {
  std::swap(lhs, rhs);
}

struct EmptyAlloc {};

#ifndef AMC_CXX17
template <class Alloc>
using alloc_is_always_equal_t = typename Alloc::is_always_equal;
#endif

/// Allocator traits used by the vectors, also defined for the EmptyAlloc of FixedCapacityVector (which never
/// allocates). Operations depending on the equality of the allocators are dispatched at compile time on
/// 'is_always_equal', so that the containers of always equal allocators (the most common ones) do not instantiate the
/// code for unequal ones.
template <class Alloc>
struct AllocTraits {
#ifdef AMC_CXX17
  using is_always_equal = typename std::allocator_traits<Alloc>::is_always_equal;
#else
  // std::allocator_traits<Alloc>::is_always_equal is only standard from C++17
  using is_always_equal = detected_or_t<typename std::is_empty<Alloc>::type, alloc_is_always_equal_t, Alloc>;
#endif
  using propagate_on_copy_assignment = typename std::allocator_traits<Alloc>::propagate_on_container_copy_assignment;
  using propagate_on_move_assignment = typename std::allocator_traits<Alloc>::propagate_on_container_move_assignment;
  using propagate_on_swap = typename std::allocator_traits<Alloc>::propagate_on_container_swap;

  static Alloc SelectOnCopy(const Alloc& alloc) {
    return std::allocator_traits<Alloc>::select_on_container_copy_construction(alloc);
  }

  /// Tells whether the memory allocated by one of the allocators can be deallocated by the other one.
  static bool Equal(const Alloc& lhs, const Alloc& rhs) noexcept { return Equal(lhs, rhs, is_always_equal()); }

 private:
  static bool Equal(const Alloc&, const Alloc&, std::true_type) noexcept { return true; }
  static bool Equal(const Alloc& lhs, const Alloc& rhs, std::false_type) noexcept { return lhs == rhs; }
};

template <>
struct AllocTraits<EmptyAlloc> {
  using is_always_equal = std::true_type;
  using propagate_on_copy_assignment = std::false_type;
  using propagate_on_move_assignment = std::false_type;
  using propagate_on_swap = std::false_type;

  static EmptyAlloc SelectOnCopy(const EmptyAlloc&) noexcept { return EmptyAlloc(); }
};

/// Tells whether two vectors may exchange their dynamic storages, as far as their allocators are concerned: they need
/// to be of the same type and equal.
template <class Alloc, class OAlloc>
inline bool AllocatorsAreEqual(const Alloc&, const OAlloc&) noexcept {
  return false;
}

template <class Alloc>
inline bool AllocatorsAreEqual(const Alloc& lhs, const Alloc& rhs) noexcept {
  return AllocTraits<Alloc>::Equal(lhs, rhs);
}

/// Maximum capacity of a vector: its size type and its allocator (whose number of bytes to allocate must fit in a
/// size_t) both limit it.
template <class SizeType, class Alloc>
inline uintmax_t MaxCapacity(const Alloc& alloc) noexcept {
  return std::min(static_cast<uintmax_t>(std::numeric_limits<SizeType>::max()),
                  static_cast<uintmax_t>(std::allocator_traits<Alloc>::max_size(alloc)));
}

template <class T, class SizeType>
class StaticVectorBase {
 public:
  using iterator = T*;
  using const_iterator = const T*;
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

  StaticVectorBase(SizeType inplaceCapa, const EmptyAlloc&) noexcept : _capa(inplaceCapa), _size(0) {}

  void swap_impl(StaticVectorBase& o) noexcept(is_swap_noexcept<T>::value) {
    swap_deep(begin(), _size, o.begin(), o._size);
    std::swap(_size, o._size);
  }

  void move_construct(StaticVectorBase& o, SizeType) noexcept(is_move_construct_nothrow<T>::value) {
    amc::uninitialized_relocate_n(o.begin(), o._size, begin());
    _size = amc::exchange(o._size, SizeType{0});
  }

  void move_assign(StaticVectorBase& o, SizeType) noexcept(is_shift_nothrow<T>::value) {
    move_n(o.begin(), o._size, begin(), _size);
    _size = amc::exchange(o._size, SizeType{0});
  }

  void shrink_impl(SizeType) noexcept {}

  void incrSize() noexcept { ++_size; }
  void decrSize() noexcept { --_size; }
  SizeType& msize() noexcept { return _size; }
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
  using iterator = T*;
  using const_iterator = const T*;
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

  StdVectorBase(SizeType, const Alloc& alloc) noexcept : Alloc(alloc) {}

  void swap_impl(StdVectorBase& o) noexcept {
    std::swap(_storage, o._storage);
    std::swap(_capa, o._capa);
    std::swap(_size, o._size);
  }

  void move_construct(StdVectorBase& o, SizeType) noexcept {
    _storage = amc::exchange(o._storage, nullptr);
    _capa = amc::exchange(o._capa, SizeType{0});
    _size = amc::exchange(o._size, SizeType{0});
  }

  void move_assign(StdVectorBase& o, SizeType) noexcept {
    if (_storage) {
      amc::destroy_n(_storage, _size);
      freeStorage();  // Compared to swap, we can free memory directly for move assignment
    }
    _storage = amc::exchange(o._storage, nullptr);
    _capa = amc::exchange(o._capa, SizeType{0});
    _size = amc::exchange(o._size, SizeType{0});
  }

  void grow(uintmax_t minSize, bool exact = false);

  void shrink_impl(SizeType) noexcept {
    if (_size != _capa) {
      shrink();
    }
  }

  void setAllocator(const Alloc& alloc) noexcept { static_cast<Alloc&>(*this) = alloc; }
  void swapAllocator(StdVectorBase& o) noexcept {
    using std::swap;
    swap(static_cast<Alloc&>(*this), static_cast<Alloc&>(o));
  }

  template <class, class, class>
  friend class SmallVectorBase;

  template <class, class, class>
  friend class StdVectorBase;

  template <class OSizeType>
  bool canSwapDynStorage(StaticVectorBase<T, OSizeType>&) const noexcept {
    return false;
  }
  template <class OSizeType, class OAlloc>
  bool canSwapDynStorage(SmallVectorBase<T, OAlloc, OSizeType>& o) const noexcept;

  template <class OSizeType, class OAlloc>
  bool canSwapDynStorage(StdVectorBase<T, OAlloc, OSizeType>& o) const noexcept {
    return AllocatorsAreEqual(get_allocator(), o.get_allocator()) && CapacitiesFitEachOther(_capa, o._capa);
  }

  template <class VectorType>
  void swapDynStorage(VectorType& o) noexcept {
    SwapDynStorage(_storage, o._storage);
  }
  template <class OSizeType>
  void swapDynStorage(StaticVectorBase<T, OSizeType>&) noexcept {}

  void incrSize() noexcept { ++_size; }
  void decrSize() noexcept { --_size; }
  SizeType& msize() noexcept { return _size; }
  SizeType& mcapacity() noexcept { return _capa; }
  void setSize(SizeType s) noexcept { _size = s; }

  iterator dynStorage() const noexcept { return _storage; }

 private:
  void shrink();
  void freeStorage() noexcept;

  SizeType _capa = 0;
  SizeType _size = 0;
  T* _storage = nullptr;
};

template <class T, class Alloc, class SizeType>
class SmallVectorBase : private Alloc {
 private:
  void destroyFreeStorage() noexcept {
    if (isSmall()) {
      amc::destroy_n(_storage.ptr(), _capa);
    } else {
      amc::destroy_n(_storage.dyn(), _size);
      freeStorage();
    }
  }

 public:
  using iterator = T*;
  using const_iterator = const T*;
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
  /// to a larger type). This invalid configuration is caught in a static_assert in SmallVector class.
  explicit SmallVectorBase(SizeType inplaceCapa) noexcept : _capa(0), _size(inplaceCapa) {}

  SmallVectorBase(SizeType inplaceCapa, const Alloc& alloc) noexcept : Alloc(alloc), _capa(0), _size(inplaceCapa) {}

  /// As explained above, if _capa == _size == SizeType::max() then it's necessarily in a large state.
  bool isSmall() const noexcept { return _capa < _size; }

  /// swap_impl is called by public method 'swap' for same SmallVector (same number of inplace elements).
  /// No need to check / adjust capacity for small states then (no throw guaranteed).
  void swap_impl(SmallVectorBase& o) noexcept(is_swap_noexcept<T>::value) {
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

  void move_construct(SmallVectorBase& o, SizeType inplaceCapa) noexcept(is_move_construct_nothrow<T>::value) {
    if (o.isSmall()) {
      amc::uninitialized_relocate_n(o._storage.ptr(), o._capa, _storage.ptr());
    } else {
      _storage.setDyn(o._storage.dyn());
    }
    _capa = amc::exchange(o._capa, SizeType{0});
    _size = amc::exchange(o._size, inplaceCapa);
  }

  void move_construct(StdVectorBase<T, Alloc, SizeType>& o) noexcept {
    if (o._capa != 0) {
      // Always steal dynamic buffer in this case, to make move construct faster
      _storage.setDyn(o._storage);
      o._storage = nullptr;
      _capa = amc::exchange(o._capa, SizeType{0});
      _size = amc::exchange(o._size, SizeType{0});
    }
  }

  void move_assign(SmallVectorBase& o, SizeType inplaceCapa) noexcept(is_shift_nothrow<T>::value) {
    if (o.isSmall()) {
      // No need to check 'this' capacity. If 'this' is small, then 'this' capacity is same as 'o'.
      // If 'this' is large, then 'this' capacity is larger by design.
      // Indeed, capacity cannot shrink, except for shrink_to_fit which resets to small state if possible.
      // Besides, if 'this' is large, let's not shrink to small size and keep our dynamic memory for now.
      // To sum-up, in this context, we do not touch our capacity, only move and relocates o's elements
      move_n(o._storage.ptr(), o._capa, begin(), size());
      // Use setSize for both as it correctly handles the encoding of the full small state
      setSize(o._capa);
      o.setSize(0);
    } else {
      // Clear our stuff before stealing o's guts
      destroyFreeStorage();
      _storage.setDyn(o._storage.dyn());
      _capa = amc::exchange(o._capa, SizeType{0});
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

  void setAllocator(const Alloc& alloc) noexcept { static_cast<Alloc&>(*this) = alloc; }
  void swapAllocator(SmallVectorBase& o) noexcept {
    using std::swap;
    swap(static_cast<Alloc&>(*this), static_cast<Alloc&>(o));
  }

  template <class, class, class>
  friend class SmallVectorBase;

  template <class, class, class>
  friend class StdVectorBase;

  template <class OAlloc, class OSizeType>
  bool canSwapDynStorage(StdVectorBase<T, OAlloc, OSizeType>& o) const noexcept {
    return !isSmall() && AllocatorsAreEqual(get_allocator(), o.get_allocator()) &&
           CapacitiesFitEachOther(_capa, o._capa);
  }
  template <class OSizeType>
  bool canSwapDynStorage(StaticVectorBase<T, OSizeType>&) const noexcept {
    return false;
  }
  template <class OSizeType, class OAlloc>
  bool canSwapDynStorage(SmallVectorBase<T, OAlloc, OSizeType>& o) const noexcept {
    return !isSmall() && !o.isSmall() && AllocatorsAreEqual(get_allocator(), o.get_allocator()) &&
           CapacitiesFitEachOther(_capa, o._capa);
  }

  template <class VectorType>
  void swapDynStorage(VectorType& o) noexcept {
    SwapDynStorage(_storage, o._storage);
  }
  template <class OSizeType>
  void swapDynStorage(StaticVectorBase<T, OSizeType>&) noexcept {}

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
  SizeType& msize() noexcept { return isSmall() ? _capa : _size; }
  /// Access to 'real' capacity member reference. No need to check for small state here, this method is only called
  /// for large state vectors.
  SizeType& mcapacity() noexcept { return _capa; }

  iterator dynStorage() const noexcept { return _storage.dyn(); }

 private:
  /// The inline elements of 'vSmall' are relocated to the inline storage of 'vDynBuf', which shares its first bytes
  /// with the pointer to its dynamic storage: if a move throws, this pointer is restored.
  /// (not declared noexcept(is_swap_noexcept<T>::value), as GCC warns about the rethrow when it is true)
  static inline void SwapDynamicBuffer(SmallVectorBase& vDynBuf, SmallVectorBase& vSmall) {
    T* oDynStorage = vDynBuf._storage.dyn();
    try {
      (void)amc::uninitialized_relocate_n(vSmall._storage.ptr(), vSmall._capa, vDynBuf._storage.ptr());
    } catch (...) {
      vDynBuf._storage.setDyn(oDynStorage);
      throw;
    }
    vSmall._storage.setDyn(oDynStorage);
  }

  void shrink();
  void resetToSmall(SizeType);
  void freeStorage() noexcept;

  SizeType _capa, _size;
  ElemWithPtrStorage<T> _storage;
};

template <class T, class Alloc, class SizeType>
template <class OSizeType, class OAlloc>
bool StdVectorBase<T, Alloc, SizeType>::canSwapDynStorage(SmallVectorBase<T, OAlloc, OSizeType>& o) const noexcept {
  return !o.isSmall() && AllocatorsAreEqual(get_allocator(), o.get_allocator()) &&
         CapacitiesFitEachOther(_capa, o._capa);
}

template <class T, class SizeType, class GrowingPolicy>
class StaticVector : public StaticVectorBase<T, SizeType> {
 public:
  using reference = T&;
  using iterator = T*;
  using pointer = T*;
  using const_iterator = const T*;
  using size_type = SizeType;

  size_type max_size() const noexcept { return this->capacity(); }

  void reserve(size_type capacity) { GrowingPolicy::Check(capacity, this->capacity()); }

  template <class... Args>
  iterator emplace(const_iterator position, Args&&... args) {
    assert(position >= this->cbegin() && position <= this->cbegin() + this->size());
    GrowingPolicy::Check(this->size() + 1U, this->capacity());
    iterator pos = const_cast<iterator>(position);
    emplace_n(pos, this->size() - (pos - this->begin()), std::forward<Args>(args)...);
    this->incrSize();
    return pos;
  }

  template <class... Args>
  reference emplace_back(Args&&... args) {
    GrowingPolicy::Check(this->size() + 1U, this->capacity());
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, std::forward<Args&&>(args)...);
    this->incrSize();
    return *endIt;
  }

  template <class... Args>
  pointer try_emplace_back(Args&&... args) {
    if (this->size() == this->capacity()) {
      return nullptr;
    }
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, std::forward<Args&&>(args)...);
    this->incrSize();
    return endIt;
  }

  template <class... Args>
  reference unchecked_emplace_back(Args&&... args) {
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, std::forward<Args&&>(args)...);
    this->incrSize();
    return *endIt;
  }

  pointer try_push_back(const T& value) {
    if (this->size() == this->capacity()) {
      return nullptr;
    }
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, value);
    this->incrSize();
    return endIt;
  }

  pointer try_push_back(T&& value) {
    if (this->size() == this->capacity()) {
      return nullptr;
    }
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, std::move(value));
    this->incrSize();
    return endIt;
  }

  pointer unchecked_push_back(const T& value) {
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, value);
    this->incrSize();
    return endIt;
  }

  pointer unchecked_push_back(T&& value) {
    iterator endIt = this->begin() + this->size();
    amc::construct_at(endIt, std::move(value));
    this->incrSize();
    return endIt;
  }

 protected:
  template <class... Args>
  explicit StaticVector(Args&&... args) noexcept : StaticVectorBase<T, SizeType>(std::forward<Args&&>(args)...) {}

  template <class, class, class>
  friend class StaticVector;

  template <class, class, class, bool>
  friend class DynamicVector;

  template <class VectorType>
  void swap2_impl(VectorType& o) noexcept(is_swap_noexcept<T>::value) {
    swap_deep(this->begin(), this->size(), o.begin(), o.size());
    swap_sizetype(this->msize(), o.msize());
  }

  // Adjust capacity methods take uintmax_t as parameter to check for size_type overflow
  void adjustCapacity(uintmax_t neededCapacity) const { GrowingPolicy::Check(neededCapacity, this->capacity()); }

  T* adjustCapacity(uintmax_t neededCapacity, const T* position) const {
    adjustCapacity(neededCapacity);
    return const_cast<T*>(position);
  }

  const T& adjustCapacity(uintmax_t neededCapacity, const T& v) const {
    adjustCapacity(neededCapacity);
    return v;
  }

  const T& adjustCapacity(uintmax_t neededCapacity, const T& v, const T**) const {
    adjustCapacity(neededCapacity);
    return v;
  }

  template <class VectorType>
  void adjustEachOtherCapacity(VectorType& o) const {
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
  using reference = T&;
  using iterator = T*;
  using const_iterator = const T*;
  using size_type = SizeType;
  using allocator_type = Alloc;

  size_type max_size() const noexcept { return static_cast<size_type>(MaxCapacity<SizeType>(this->get_allocator())); }

  void reserve(size_type capacity) {
    if (this->capacity() < capacity) {
      this->grow(capacity, true);  // Reserve with exact capacity
    }
  }

  template <class... Args>
  iterator emplace(const_iterator position, Args&&... args) {
    assert(position >= this->cbegin() && position <= this->cbegin() + this->size());
    SizeType nElemsToShift = static_cast<SizeType>(this->size() - (position - this->begin()));
    iterator pos;
    if (this->size() == this->capacity()) {
      // construct before possible iterator invalidation from grow in constructor arguments
      TemporaryElem<T> e(std::forward<Args>(args)...);
      SizeType idx = static_cast<SizeType>(position - this->begin());
      this->grow(this->size() + 1U);
      pos = this->begin() + idx;
      relocate_insert_n(pos, nElemsToShift, e.ptr());
      e.release();
    } else {
      pos = const_cast<iterator>(position);
      emplace_n(pos, nElemsToShift, std::forward<Args>(args)...);
    }
    this->incrSize();
    return pos;
  }

  template <class... Args>
  reference emplace_back(Args&&... args) {
    iterator endIt;
    if (this->size() == this->capacity()) {
      // construct before possible iterator invalidation from grow in constructor arguments
      TemporaryElem<T> e(std::forward<Args>(args)...);
      this->grow(this->size() + 1U);
      endIt = this->dynStorage() + this->size();
      amc::relocate_at(e.ptr(), endIt);
      e.release();
    } else {
      endIt = this->begin() + this->size();
      amc::construct_at(endIt, std::forward<Args&&>(args)...);
    }
    this->incrSize();
    return *endIt;
  }

 protected:
  template <class... Args>
  explicit DynamicVector(Args&&... args) noexcept
      : DynamicVectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements>::type(std::forward<Args&&>(args)...) {}

  template <class, class, class>
  friend class StaticVector;

  template <class, class, class, bool>
  friend class DynamicVector;

  template <class OSizeType, class OGrowingPolicy>
  void swap2_impl(StaticVector<T, OSizeType, OGrowingPolicy>& o) noexcept(is_swap_noexcept<T>::value) {
    // Here 'o' cannot grow so we cannot swap any dynamic storage. Deeply swap all elements
    swap_deep(this->begin(), this->size(), o.begin(), o.size());
    swap_sizetype(this->msize(), o.msize());
  }

  template <class OAlloc, class OSizeType, bool OWithInlineElems>
  void swap2_impl(DynamicVector<T, OAlloc, OSizeType, OWithInlineElems>& o) noexcept(is_swap_noexcept<T>::value) {
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
    if (static_cast<uintmax_t>(this->capacity()) < neededCapacity) {
      this->grow(neededCapacity);
    }
  }

  inline T* adjustCapacity(uintmax_t neededCapacity, const T* position) {
    if (static_cast<uintmax_t>(this->capacity()) < neededCapacity) {
      SizeType idx = static_cast<SizeType>(position - this->begin());  // pos will be invalidated
      this->grow(neededCapacity);
      return this->begin() + idx;
    }
    return const_cast<T*>(position);
  }

  inline const T& adjustCapacity(uintmax_t neededCapacity, const T& v) {
    if (static_cast<uintmax_t>(this->capacity()) < neededCapacity) {
      const T* ptr = std::addressof(v);
      ptrdiff_t idx = IsInRange(ptr, this->begin(), this->size()) ? ptr - this->begin() : -1;
      this->grow(neededCapacity);
      if (idx != -1) {
        return this->begin()[idx];
      }
    }
    return v;
  }

  inline const T& adjustCapacity(uintmax_t neededCapacity, const T& v, const T** position) {
    if (static_cast<uintmax_t>(this->capacity()) < neededCapacity) {
      const T* ptr = std::addressof(v);
      ptrdiff_t idx = IsInRange(ptr, this->begin(), this->size()) ? ptr - this->begin() : -1;
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
  void adjustEachOtherCapacity(VectorType& o) {
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
  explicit VectorDestr(Args&&... args) noexcept
      : VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type(
            std::forward<Args&&>(args)...) {}
};

template <class T, class Alloc, class SizeType, bool WithInlineElements, class GrowingPolicy>
class VectorDestr<T, Alloc, SizeType, WithInlineElements, GrowingPolicy, false>
    : public VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type {
 protected:
  template <class... Args>
  explicit VectorDestr(Args&&... args) noexcept
      : VectorBaseTypeDispatcher<T, Alloc, SizeType, WithInlineElements, GrowingPolicy>::type(
            std::forward<Args&&>(args)...) {}
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
  using iterator = T*;
  using const_iterator = const T*;
  using pointer = T*;
  using const_pointer = const T*;
  using difference_type = ptrdiff_t;
  using reference = T&;
  using const_reference = const T&;
  using size_type = SizeType;
  using allocator_type = Alloc;

  VectorImpl& operator=(std::initializer_list<T> list) {
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

  bool operator==(const VectorImpl& o) const {
    return this->size() == o.size() && std::equal(this->begin(), end(), o.begin());
  }
  bool operator!=(const VectorImpl& o) const { return !(*this == o); }

#ifdef AMC_CXX20
  auto operator<=>(const VectorImpl& o) const {
    return amc::lexicographical_compare_three_way(this->begin(), end(), o.begin(), o.end(), amc::synth_three_way());
  }
#else
  bool operator<(const VectorImpl& o) const {
    return std::lexicographical_compare(this->begin(), end(), o.begin(), o.end());
  }
  bool operator<=(const VectorImpl& o) const { return !(o < *this); }
  bool operator>(const VectorImpl& o) const { return o < *this; }
  bool operator>=(const VectorImpl& o) const { return !(*this < o); }
#endif

  void pop_back() {
    assert(!empty());
    amc::destroy_at(this->end() - 1);
    this->decrSize();
  }

#ifdef AMC_NONSTD_FEATURES
  T pop_back_val() {
    T lastEl = std::move(back());
    pop_back();
    return lastEl;
  }
#endif

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
    assignImpl(first, last, typename std::iterator_traits<InputIt>::iterator_category());
  }

  void assign(std::initializer_list<T> ilist) { assign(ilist.begin(), ilist.end()); }

#ifdef AMC_CXX23
  /// Replaces the contents of the container with the elements of 'rg', which may not be common (its end may be a
  /// sentinel of another type than its iterator). 'rg' must not overlap the container.
  template <class R>
  void assign_range(R&& rg) {
    if constexpr (std::ranges::forward_range<R>) {
      assignN(std::ranges::begin(rg), static_cast<uintmax_t>(std::ranges::distance(rg)));
    } else {
      assignInput(std::ranges::begin(rg), std::ranges::end(rg));
    }
  }
#endif

  iterator insert(const_iterator position, const_reference v) {
    assert(position >= this->cbegin() && position <= cend());
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U, v, &position);
    iterator pos = const_cast<iterator>(position);
    insert_n(pos, this->size() - (pos - this->begin()), newV);
    this->incrSize();
    return pos;
  }

  iterator insert(const_iterator position, T&& v) {
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
        // 'newV' may alias one of the elements about to be shifted (e.g. inserting several copies of an element
        // located at or after 'pos'). 'shift_right' relocates that element 'count' slots to the right, so bind the
        // source to its post-shift location to keep reading the intended value - same rationale as 'insert_n'.
        const T* pv = std::addressof(newV);
        const_reference src = IsInRange(pv, pos, nElemsToShift) ? *(pv + count) : newV;
        shift_right(pos, nElemsToShift, count);
        try {
          fill_after_shift(pos, nElemsToShift, count, src);
        } catch (...) {
          undo_shift_right(pos, nElemsToShift, count);
          throw;
        }
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
    return insertImpl(position, first, last, typename std::iterator_traits<InputIt>::iterator_category());
  }

  iterator insert(const_iterator pos, std::initializer_list<T> list) { return insert(pos, list.begin(), list.end()); }

#ifdef AMC_CXX23
  /// Inserts the elements of 'rg' before 'pos'. 'rg' may not be common, and must not overlap the container.
  template <class R>
  iterator insert_range(const_iterator pos, R&& rg) {
    assert(pos >= this->cbegin() && pos <= cend());
    if constexpr (std::ranges::forward_range<R>) {
      return insertN(pos, std::ranges::begin(rg), static_cast<uintmax_t>(std::ranges::distance(rg)));
    } else {
      return insertInput(pos, std::ranges::begin(rg), std::ranges::end(rg));
    }
  }
#endif

  /// @brief Erases element at position in the vector. A 'large' SmallVector will not become small even if the size can
  /// fit in its inline storage.
  /// @param position iterator in a valid range of the vector, that is >= begin and < end
  /// @return iterator to the element immediately after the removed one
  iterator erase(const_iterator position) {
    assert(position >= this->cbegin() && position < cend());
    const iterator it = const_cast<iterator>(position);
    erase_at(it, this->size() - (position - this->begin()) - 1);
    this->decrSize();
    return it;
  }

  iterator erase(const_iterator first, const_iterator last) {
    assert(first <= last && first >= this->cbegin() && last <= cend());
    const iterator mfirst = const_cast<iterator>(first);
    if (first == last) {
      // Nothing to erase. Return early, as 'erase_n' would self move assign all next elements, which may change them.
      return mfirst;
    }
    const SizeType n = static_cast<SizeType>(last - first);
    erase_n(mfirst, n, static_cast<SizeType>(this->size() - (last - this->begin())));
    this->setSize(this->size() - n);
    return mfirst;
  }

  void push_back(const_reference v) {
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U, v);
    amc::construct_at(end(), newV);
    this->incrSize();
  }

  /// Appends the given element value to the end of the container, moving from 'v'.
  ///
  /// 'v' may be an rvalue reference to an element already stored in this vector (for instance
  /// 'v.push_back(std::move(v.front()))'). If a reallocation is needed to grow the container, that element is
  /// relocated into the freshly allocated storage and the original reference would be left dangling. We therefore
  /// route 'v' through the same 'adjustCapacity' helper as the 'const_reference' overload: it returns a reference
  /// to 'v' at its (possibly relocated) location, which we only read from once the growth has taken place. The
  /// 'const_cast' is safe here because 'v' always binds to a non-const 'T' object, whether it lives inside or
  /// outside the vector.
  void push_back(T&& v) {
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + 1U, v);
    amc::construct_at(end(), std::move(const_cast<reference>(newV)));
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

#ifndef AMC_NONSTD_FEATURES
 protected:
#endif

  // Additional convenient methods activated when non standard features are enabled

  /// Like swap, but can take other vectors of same type with different inline number of elements
  /// It has one drawback though: it can throw (as it can make SmallVectors grow)
  template <class OAlloc, class OSizeType, bool OWithInlineElements, class OGrowingPolicy>
  void swap2(VectorImpl<T, OAlloc, OSizeType, OWithInlineElements, OGrowingPolicy>& o) {
    this->adjustEachOtherCapacity(o);
    this->swap2_impl(o);
  }

  /// Append elements to the end of the vector. Similar to: vec.insert(vec.end(), ...).
  /// The behavior is undefined if first and last are iterators into *this
  template <class InputIt, typename std::enable_if<!std::is_integral<InputIt>::value, bool>::type = true>
  void append(InputIt first, InputIt last) {
    appendImpl(first, last, typename std::iterator_traits<InputIt>::iterator_category());
  }

  void append(size_type count) {
    this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count);
    amc::uninitialized_value_construct_n(end(), count);
    this->setSize(static_cast<SizeType>(this->size() + count));
  }

  void append(size_type count, const_reference v) {
    const_reference newV = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count, v);
    std::uninitialized_fill_n(end(), count, newV);
    this->setSize(static_cast<SizeType>(this->size() + count));
  }

  void append(std::initializer_list<T> list) { append(list.begin(), list.end()); }

#ifdef AMC_CXX23
 public:
  /// Appends the elements of 'rg', which may not be common. 'rg' must not overlap the container.
  template <class R>
  void append_range(R&& rg) {
    if constexpr (std::ranges::forward_range<R>) {
      appendN(std::ranges::begin(rg), static_cast<uintmax_t>(std::ranges::distance(rg)));
    } else {
      appendInput(std::ranges::begin(rg), std::ranges::end(rg));
    }
  }

  /// Appends elements of 'rg' until the vector is full.
  /// Returns an iterator to the first non appended element ('std::ranges::dangling' if 'rg' is a non borrowed rvalue).
  template <class R>
  std::ranges::borrowed_iterator_t<R> try_append_range(R&& rg) {
    auto first = std::ranges::begin(rg);
    if constexpr (std::ranges::sized_range<R> && std::ranges::random_access_range<R>) {
      // compute in uintmax_t as the size of the range may not fit in SizeType
      const uintmax_t count = std::min(static_cast<uintmax_t>(std::ranges::size(rg)),
                                       static_cast<uintmax_t>(this->capacity() - this->size()));
      amc::uninitialized_copy_n(first, static_cast<SizeType>(count), this->end());
      this->setSize(static_cast<SizeType>(this->size() + count));
      return first + static_cast<std::ranges::range_difference_t<R> >(count);
    } else {
      // the range may not be common (its end may be a sentinel of a different type than its iterator)
      auto last = std::ranges::end(rg);
      for (; first != last && this->size() < this->capacity(); ++first) {
        amc::construct_at(this->end(), *first);
        this->incrSize();
      }
      return first;
    }
  }
#endif

 private:
  // Range methods implementations, dispatched on the iterator category.
  // Forward iterators can be traversed several times: we compute the number of elements first to reserve once.
  // Single pass input iterators can be traversed only once, so we cannot know their number of elements in advance.
  // The ranges methods call the same implementations: the '*N' ones take a forward iterator and the number of elements,
  // the '*Input' ones an input iterator and a sentinel, which may be of another type.

  template <class ForwardIt>
  void assignImpl(ForwardIt first, ForwardIt last, std::forward_iterator_tag) {
    assignN(first, static_cast<uintmax_t>(std::distance(first, last)));
  }

  template <class InputIt>
  void assignImpl(InputIt first, InputIt last, std::input_iterator_tag) {
    assignInput(first, last);
  }

  template <class ForwardIt>
  iterator insertImpl(const_iterator position, ForwardIt first, ForwardIt last, std::forward_iterator_tag) {
    return insertN(position, first, static_cast<uintmax_t>(std::distance(first, last)));
  }

  template <class InputIt>
  iterator insertImpl(const_iterator position, InputIt first, InputIt last, std::input_iterator_tag) {
    return insertInput(position, first, last);
  }

  template <class ForwardIt>
  void appendImpl(ForwardIt first, ForwardIt last, std::forward_iterator_tag) {
    appendN(first, static_cast<uintmax_t>(std::distance(first, last)));
  }

  template <class InputIt>
  void appendImpl(InputIt first, InputIt last, std::input_iterator_tag) {
    appendInput(first, last);
  }

  template <class ForwardIt>
  void assignN(ForwardIt first, uintmax_t count) {
    if (static_cast<uintmax_t>(this->size()) < count) {
      this->adjustCapacity(count);
      assign_n(first, static_cast<SizeType>(count), this->begin(), this->size());
    } else {
      // copy to already existing elements and destroy remaining ones
      amc::destroy(std::copy_n(first, static_cast<SizeType>(count), this->begin()), end());
    }
    this->setSize(static_cast<SizeType>(count));
  }

  template <class InputIt, class Sentinel>
  void assignInput(InputIt first, Sentinel last) {
    // copy to already existing elements, then destroy remaining ones or append remaining input elements
    const iterator endIt = end();
    iterator it = this->begin();
    for (; it != endIt && first != last; ++it, (void)++first) {
      *it = AsValueType<T>(*first);
    }
    if (it != endIt) {
      amc::destroy(it, endIt);
      this->setSize(static_cast<SizeType>(it - this->begin()));
    } else {
      appendInput(std::move(first), last);
    }
  }

  template <class ForwardIt>
  iterator insertN(const_iterator position, ForwardIt first, uintmax_t count) {
    iterator pos;
    if (count > 0) {
      pos = this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count, position);
      const SizeType n = static_cast<SizeType>(count);
      const SizeType nElemsToShift = static_cast<SizeType>(this->size() - (pos - this->begin()));
      if (nElemsToShift == 0) {
        amc::uninitialized_copy_n(first, n, pos);
      } else {
        shift_right(pos, nElemsToShift, n);
        try {
          copy_after_shift(first, nElemsToShift, n, pos);
        } catch (...) {
          undo_shift_right(pos, nElemsToShift, n);
          throw;
        }
      }
      this->setSize(static_cast<SizeType>(this->size() + count));
    } else {
      pos = const_cast<iterator>(position);
    }
    return pos;
  }

  template <class InputIt, class Sentinel>
  iterator insertInput(const_iterator position, InputIt first, Sentinel last) {
    // append new elements at the end, then rotate them to their final position
    const auto idx = position - this->cbegin();  // position may be invalidated by append
    const auto oldSize = this->size();
    appendInput(std::move(first), last);
    const iterator pos = this->begin() + idx;
    std::rotate(pos, this->begin() + oldSize, end());
    return pos;
  }

  template <class ForwardIt>
  void appendN(ForwardIt first, uintmax_t count) {
    this->adjustCapacity(static_cast<uintmax_t>(this->size()) + count);
    amc::uninitialized_copy_n(first, static_cast<SizeType>(count), end());
    this->setSize(static_cast<SizeType>(this->size() + count));
  }

  template <class InputIt, class Sentinel>
  void appendInput(InputIt first, Sentinel last) {
    for (; first != last; ++first) {
      this->emplace_back(*first);
    }
  }

 protected:
  template <class... Args>
  explicit VectorImpl(Args&&... args) noexcept
      : VectorDestr<T, Alloc, SizeType, WithInlineElements, GrowingPolicy,
                    DefineDestructor<T, WithInlineElements>::value>(std::forward<Args&&>(args)...) {}
};

template <class T, class A, class S, bool I, class G>
void swap(VectorImpl<T, A, S, I, G>& lhs, VectorImpl<T, A, S, I, G>& rhs) {
  lhs.swap2(rhs);
}

/// Add inplace storage when needed
template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N, class Enable = void>
class VectorWithInplaceStorage : public VectorImpl<T, Alloc, SizeType, true, GrowingPolicy> {
 protected:
  template <class... Args>
  explicit VectorWithInplaceStorage(Args&&... args) noexcept
      : VectorImpl<T, Alloc, SizeType, true, GrowingPolicy>(std::forward<Args&&>(args)...) {}

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
  explicit VectorWithInplaceStorage(Args&&... args) noexcept
      : VectorImpl<T, Alloc, SizeType, (N != 0), GrowingPolicy>(std::forward<Args&&>(args)...) {}
};

/// Number of inline elements 'N' as a SizeType, checked to fit in it.
template <uintmax_t N, class SizeType>
struct SanitizeInlineSize : std::integral_constant<SizeType, static_cast<SizeType>(N)> {
  static_assert(N <= static_cast<uintmax_t>(std::numeric_limits<SizeType>::max()),
                "Inline storage too large for SizeType");
};
}  // namespace vec

/// Allocators are handled like in the standard containers: copy construction calls
/// 'select_on_container_copy_construction', move construction moves the allocator, and assignments and swap propagate
/// it according to the 'propagate_on_container_*' traits. Memory is only exchanged between containers of equal
/// allocators, otherwise elements are moved one by one.
template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N>
class Vector : public vec::VectorWithInplaceStorage<T, Alloc, SizeType, GrowingPolicy, N> {
 private:
  using Base = vec::VectorWithInplaceStorage<T, Alloc, SizeType, GrowingPolicy, N>;
  using AllocTraits = vec::AllocTraits<Alloc>;
  using IsAlwaysEqual = typename AllocTraits::is_always_equal;
  using PropagateOnMove = typename AllocTraits::propagate_on_move_assignment;
  /// Whether the memory of a move assigned container can be stolen (after having propagated its allocator if needed).
  using CanStealOnMoveAssign = std::integral_constant<bool, IsAlwaysEqual::value || PropagateOnMove::value>;

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

  explicit Vector(const Alloc& alloc) noexcept : Base(N, alloc) {}

  template <class InputIt, typename std::enable_if<!std::is_integral<InputIt>::value, bool>::type = true>
  Vector(InputIt first, InputIt last, const Alloc& alloc = Alloc()) : Base(N, alloc) {
    this->append(first, last);
  }

  explicit Vector(size_type count, const Alloc& alloc = Alloc()) : Base(N, alloc) { this->append(count); }

  Vector(size_type count, const_reference v, const Alloc& alloc = Alloc()) : Base(N, alloc) { this->append(count, v); }

  Vector(const Vector& o) : Base(N, AllocTraits::SelectOnCopy(o.get_allocator())) { this->append(o.begin(), o.end()); }

  Vector(const Vector& o, const Alloc& alloc) : Base(N, alloc) { this->append(o.begin(), o.end()); }

  Vector(Vector&& o) noexcept(N == 0 || vec::is_move_construct_nothrow<T>::value) : Base(N, o.get_allocator()) {
    this->move_construct(o, N);
  }

  /// Build a SmallVector from a vector, stealing its dynamic storage.
  template <SizeType ON = N, class OGrowingPolicy = GrowingPolicy>
  Vector(Vector<T, Alloc, SizeType, OGrowingPolicy, 0>&& o,
         typename std::enable_if<std::is_same<OGrowingPolicy, vec::DynamicGrowingPolicy>::value && (ON > 0)>::type* = 0)
      : Base(N, o.get_allocator()) {
    this->move_construct(o);
  }

  /// If 'alloc' is not equal to the allocator of 'o', its elements are moved one by one.
  Vector(Vector&& o,
         const Alloc& alloc) noexcept((N == 0 || vec::is_move_construct_nothrow<T>::value) && IsAlwaysEqual::value)
      : Base(N, alloc) {
    moveConstruct(o, IsAlwaysEqual());
  }

  Vector(std::initializer_list<T> init, const Alloc& alloc = Alloc()) : Base(N, alloc) {
    this->append(init.begin(), init.end());
  }

  Vector& operator=(const Vector& o) {
    if (AMC_LIKELY(this != &o)) {
      propagateAllocator(o, typename AllocTraits::propagate_on_copy_assignment());
      this->assign(o.begin(), o.end());
    }
    return *this;
  }

  // Move assignment operator only defined here as it requires same N
  Vector& operator=(Vector&& o) noexcept((N == 0 || vec::is_shift_nothrow<T>::value) && CanStealOnMoveAssign::value) {
    if (AMC_LIKELY(this != &o)) {
      moveAssign(o, CanStealOnMoveAssign());
    }
    return *this;
  }

  // Define swap here instead of VectorImpl as noexcept swap is possible only for same inplace capacity
  void swap(Vector& o) noexcept(N == 0 || vec::is_swap_noexcept<T>::value) {
    this->swap_impl(o);
    swapAllocators(o, typename AllocTraits::propagate_on_swap());
  }

  void shrink_to_fit() { this->shrink_impl(N); }

#ifdef AMC_CXX20
  template <class V>
  friend size_type erase(Vector& c, const V& value) {
    const auto it = std::remove(c.begin(), c.end(), value);
    const auto r = std::distance(it, c.end());
    c.erase(it, c.end());
    return static_cast<size_type>(r);
  }

  template <class Pred>
  friend size_type erase_if(Vector& c, Pred pred) {
    const auto it = std::remove_if(c.begin(), c.end(), pred);
    const auto r = std::distance(it, c.end());
    c.erase(it, c.end());
    return static_cast<size_type>(r);
  }
#endif

 private:
  void moveConstruct(Vector& o, std::true_type) noexcept(N == 0 || vec::is_move_construct_nothrow<T>::value) {
    this->move_construct(o, N);
  }

  void moveConstruct(Vector& o, std::false_type) {
    if (AllocTraits::Equal(this->get_allocator(), o.get_allocator())) {
      this->move_construct(o, N);
    } else {
      this->append(std::make_move_iterator(o.begin()), std::make_move_iterator(o.end()));
    }
  }

  /// Replaces our allocator by the one of 'o', after having released our memory if it cannot deallocate it.
  void propagateAllocator(const Vector& o, std::true_type) noexcept {
    if (!AllocTraits::Equal(this->get_allocator(), o.get_allocator())) {
      this->clear();
      this->shrink_to_fit();  // no element to relocate: cannot throw
    }
    this->setAllocator(o.get_allocator());
  }

  void propagateAllocator(const Vector&, std::false_type) noexcept {}

  void swapAllocators(Vector& o, std::true_type) noexcept { this->swapAllocator(o); }

  void swapAllocators(Vector& o, std::false_type) noexcept { checkEqualAllocators(o, IsAlwaysEqual()); }

  void checkEqualAllocators(Vector&, std::true_type) noexcept {}

  /// Like for the standard containers, swapping containers with unequal allocators that do not propagate is undefined
  void checkEqualAllocators(Vector& o, std::false_type) noexcept {
    assert(this->get_allocator() == o.get_allocator());
    (void)o;
  }

  /// Our allocator is equal to the one of 'o', or it propagates: we can steal the memory of 'o'.
  void moveAssign(Vector& o, std::true_type) noexcept(N == 0 || vec::is_shift_nothrow<T>::value) {
    propagateAllocator(o, PropagateOnMove());
    this->move_assign(o, N);
  }

  /// Our allocator does not propagate: we can steal the memory of 'o' only if our allocators are equal.
  void moveAssign(Vector& o, std::false_type) {
    if (AllocTraits::Equal(this->get_allocator(), o.get_allocator())) {
      this->move_assign(o, N);
    } else {
      this->assign(std::make_move_iterator(o.begin()), std::make_move_iterator(o.end()));
    }
  }
};

template <class T, class A, class S, class G, S N>
inline void swap(Vector<T, A, S, G, N>& lhs,
                 Vector<T, A, S, G, N>& rhs) noexcept(N == 0 || vec::is_swap_noexcept<T>::value) {
  lhs.swap(rhs);
}

}  // namespace amc
