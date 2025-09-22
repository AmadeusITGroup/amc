#pragma once

#include <cstring>
#include <memory>
#include <utility>

#include "config.hpp"
#include "type_traits.hpp"

#ifndef AMC_CXX17
#include <iterator>

#include "utility.hpp"
#endif

namespace amc {

// destroy
#ifdef AMC_CXX17
using std::destroy;
using std::destroy_at;
using std::destroy_n;
#else
template <class T>
void destroy_at(T *p, typename std::enable_if<!std::is_array<T>::value>::type * = nullptr) {
  p->~T();
}

template <class T>
void destroy_at(T *p, typename std::enable_if<std::is_array<T>::value>::type * = nullptr) {
  for (auto &elem : *p) {
    destroy_at(std::addressof(elem));
  }
}

template <class ForwardIt>
void destroy(ForwardIt first, ForwardIt last) {
  for (; first != last; ++first) {
    destroy_at(std::addressof(*first));
  }
}

template <class ForwardIt, class Size>
ForwardIt destroy_n(ForwardIt first, Size n) {
  for (; n > 0; (void)++first, --n) {
    destroy_at(std::addressof(*first));
  }
  return first;
}
#endif

// construct_at
#ifdef AMC_CXX20
using std::construct_at;
#else
namespace memory_details {
struct TriviallyCopyable {};
struct NonTriviallyCopyable {};
struct NonTriviallyCopyableArray {};

template <class T>
inline void construct_at_impl(T *pos, const T &v, NonTriviallyCopyable) {
  ::new (const_cast<void *>(static_cast<const volatile void *>(pos))) T(v);
}

template <class T>
inline void construct_at_impl(T *pos, const T &v, NonTriviallyCopyableArray) {
  int i = 0;
  try {
    using ElemT = typename std::remove_reference<decltype(**pos)>::type;
    typedef
        typename std::conditional<std::is_array<ElemT>::value, NonTriviallyCopyableArray, NonTriviallyCopyable>::type
            TypeTraits;
    for (ElemT &elem : *pos) {
      construct_at_impl(&elem, v[i++], TypeTraits());
    }
  } catch (...) {
    amc::destroy(*pos, *pos + --i);
    throw;
  }
}

template <class T>
inline void construct_at_impl(T *pos, T &&v, NonTriviallyCopyable) {
  ::new (const_cast<void *>(static_cast<const volatile void *>(pos))) T(::std::move(v));
}

template <class T>
inline void construct_at_impl(T *pos, T &&v, NonTriviallyCopyableArray) {
  int i = 0;
  try {
    using ElemT = typename std::remove_reference<decltype(**pos)>::type;
    typedef
        typename std::conditional<std::is_array<ElemT>::value, NonTriviallyCopyableArray, NonTriviallyCopyable>::type
            TypeTraits;
    for (ElemT &elem : *pos) {
      construct_at_impl(&elem, ::std::move(v[i++]), TypeTraits());
    }
  } catch (...) {
    amc::destroy(*pos, *pos + --i);
    throw;
  }
}

template <class T>
inline void construct_at_impl(T *pos, const T &v, TriviallyCopyable) {
  std::memcpy(const_cast<void *>(static_cast<const volatile void *>(pos)), std::addressof(v), sizeof(T));
}

template <typename, class T, class... Args>
struct construct_at {
  inline T *operator()(T *pos, Args &&...args) {
    return ::new (const_cast<void *>(static_cast<const volatile void *>(pos))) T(std::forward<Args>(args)...);
  }
};

template <class T, class B, class... Args>
struct construct_at<typename std::enable_if<std::is_same<B, T>::value>::type, T, B, Args...> {
  typedef typename std::conditional<std::is_trivially_copyable<T>::value, TriviallyCopyable,
                                    typename std::conditional<std::is_array<T>::value, NonTriviallyCopyableArray,
                                                              NonTriviallyCopyable>::type>::type TypeTraits;

  inline T *operator()(T *pos, const T &v) const {
    construct_at_impl(pos, v, TypeTraits());
    return pos;
  }

  inline T *operator()(T *pos, T &&v) const {
    construct_at_impl(pos, std::move(v), TypeTraits());
    return pos;
  }
};
}  // namespace memory_details

template <class T, class... Args>
T *construct_at(T *pos, Args &&...args) {
  return memory_details::construct_at<void, T, Args...>()(pos, std::forward<Args>(args)...);
}
#endif

// default / value construct
#ifdef AMC_CXX17
using std::uninitialized_default_construct;
using std::uninitialized_default_construct_n;
using std::uninitialized_value_construct;
using std::uninitialized_value_construct_n;
#else
template <class ForwardIt>
void uninitialized_default_construct(ForwardIt first, ForwardIt last,
                                     typename std::enable_if<!std::is_trivially_default_constructible<
                                         typename std::iterator_traits<ForwardIt>::value_type>::value>::type * = 0) {
  using Value = typename std::iterator_traits<ForwardIt>::value_type;
  ForwardIt current = first;
  try {
    for (; current != last; ++current) {
      ::new (static_cast<void *>(std::addressof(*current))) Value;
    }
  } catch (...) {
    amc::destroy(first, current);
    throw;
  }
}
template <class ForwardIt>
void uninitialized_default_construct(ForwardIt, ForwardIt,
                                     typename std::enable_if<std::is_trivially_default_constructible<
                                         typename std::iterator_traits<ForwardIt>::value_type>::value>::type * = 0) {}

template <class ForwardIt, class Size>
ForwardIt uninitialized_default_construct_n(
    ForwardIt first, Size n,
    typename std::enable_if<
        !std::is_trivially_default_constructible<typename std::iterator_traits<ForwardIt>::value_type>::value>::type * =
        0) {
  using Value = typename std::iterator_traits<ForwardIt>::value_type;
  ForwardIt current = first;
  try {
    for (; n > 0; (void)++current, --n) {
      ::new (static_cast<void *>(std::addressof(*current))) Value;
    }
    return current;
  } catch (...) {
    amc::destroy(first, current);
    throw;
  }
}
template <class ForwardIt, class Size>
ForwardIt uninitialized_default_construct_n(
    ForwardIt, Size,
    typename std::enable_if<
        std::is_trivially_default_constructible<typename std::iterator_traits<ForwardIt>::value_type>::value>::type * =
        0) {}

template <class ForwardIt>
void uninitialized_value_construct(
    ForwardIt first, ForwardIt last,
    typename std::enable_if<!std::is_trivial<typename std::iterator_traits<ForwardIt>::value_type>::value>::type * =
        0) {
  ForwardIt current = first;
  try {
    for (; current != last; ++current) {
      amc::construct_at(std::addressof(*current));
    }
  } catch (...) {
    amc::destroy(first, current);
    throw;
  }
}
template <class ForwardIt>
void uninitialized_value_construct(
    ForwardIt first, ForwardIt last,
    typename std::enable_if<std::is_trivial<typename std::iterator_traits<ForwardIt>::value_type>::value>::type * = 0) {
  return std::fill(first, last, typename std::iterator_traits<ForwardIt>::value_type());
}

template <class ForwardIt, class Size>
ForwardIt uninitialized_value_construct_n(
    ForwardIt first, Size n,
    typename std::enable_if<!std::is_trivial<typename std::iterator_traits<ForwardIt>::value_type>::value>::type * =
        0) {
  ForwardIt current = first;
  try {
    for (; n > 0; (void)++current, --n) {
      amc::construct_at(std::addressof(*current));
    }
    return current;
  } catch (...) {
    amc::destroy(first, current);
    throw;
  }
}
template <class ForwardIt, class Size>
ForwardIt uninitialized_value_construct_n(
    ForwardIt first, Size n,
    typename std::enable_if<std::is_trivial<typename std::iterator_traits<ForwardIt>::value_type>::value>::type * = 0) {
  return std::fill_n(first, n, typename std::iterator_traits<ForwardIt>::value_type());
}

#endif

/// uninitialized_copy with optimization for trivially copyable types.
/// In earlier C++ versions std::uninitialized_copy was optimized only for trivial types, but we can optimize it for
/// trivially copyable types.
namespace memory_details {
struct Default {};
struct MemMoveInALoop {};
struct MemMove {};

// Type factory deducing the optimization mode from the 3 defined above.
template <class InputIt, class OutputIt, bool IsMemMovePossible>
struct ImplModeFactory {
  using InputType = typename std::iterator_traits<InputIt>::value_type;
  using OutputType = typename std::iterator_traits<OutputIt>::value_type;
  typedef typename std::conditional<std::is_pointer<InputIt>::value && std::is_pointer<OutputIt>::value, MemMove,
                                    MemMoveInALoop>::type MemMoveType;
  typedef
      typename std::conditional<IsMemMovePossible && std::is_same<InputType, OutputType>::value &&
                                    !std::is_rvalue_reference<typename std::iterator_traits<InputIt>::reference>::value,
                                MemMoveType, Default>::type type;
};
}  // namespace memory_details

#ifdef AMC_CXX17
using std::uninitialized_copy;
using std::uninitialized_copy_n;
using std::uninitialized_move;
using std::uninitialized_move_n;
#else
namespace memory_details {

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_copy_impl(InputIt first, InputIt last, OutputIt dest, Default) {
  return std::uninitialized_copy(first, last, dest);
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_copy_impl(InputIt first, InputIt last, OutputIt dest, MemMoveInALoop) {
  for (; first != last; ++dest, void(), ++first) {
    using ValueType = typename std::iterator_traits<InputIt>::value_type;
    ::std::memcpy(static_cast<void *>(std::addressof(*dest)), static_cast<const void *>(std::addressof(*first)),
                  sizeof(ValueType));
  }
  return dest;
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_copy_impl(InputIt first, InputIt last, OutputIt dest, MemMove) {
  using ValueType = typename std::iterator_traits<InputIt>::value_type;
  typename std::iterator_traits<InputIt>::difference_type count = last - first;
  if (count > 0) {
    // We can use memcpy here as for uninitialized_copy, we know that ranges do not overlap
    ::std::memcpy(static_cast<void *>(dest), static_cast<const void *>(first), count * sizeof(ValueType));
  }
  return dest + count;
}

template <class InputIt, class Size, class OutputIt>
inline OutputIt uninitialized_copy_n_impl(InputIt first, Size count, OutputIt dest, Default) {
  OutputIt current = dest;
  try {
    for (; count > 0; ++first, (void)++current, --count) {
      amc::construct_at(std::addressof(*current), *first);
    }
  } catch (...) {
    amc::destroy(dest, current);
    throw;
  }
  return current;
}

template <class InputIt, class Size, class OutputIt>
inline OutputIt uninitialized_copy_n_impl(InputIt first, Size count, OutputIt dest, MemMoveInALoop) {
  for (; count > 0; ++dest, void(), ++first, --count) {
    using ValueType = typename std::iterator_traits<InputIt>::value_type;
    ::std::memcpy(static_cast<void *>(std::addressof(*dest)), static_cast<const void *>(std::addressof(*first)),
                  sizeof(ValueType));
  }
  return dest;
}

template <class InputIt, class Size, class OutputIt>
inline OutputIt uninitialized_copy_n_impl(InputIt first, Size count, OutputIt dest, MemMove) {
  using ValueType = typename std::iterator_traits<InputIt>::value_type;
  // We can use memcpy here as for uninitialized_copy, we know that ranges do not overlap
  if (count > 0) {
    ::std::memcpy(static_cast<void *>(dest), static_cast<const void *>(first), count * sizeof(ValueType));
  }
  return dest + count;
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_move_impl(InputIt first, InputIt last, OutputIt dest, Default) {
  return std::uninitialized_copy(std::make_move_iterator(first), std::make_move_iterator(last), dest);
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_move_impl(InputIt first, InputIt last, OutputIt dest, MemMoveInALoop) {
  return uninitialized_copy_impl(first, last, dest, MemMoveInALoop());
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_move_impl(InputIt first, InputIt last, OutputIt dest, MemMove) {
  return uninitialized_copy_impl(first, last, dest, MemMove());
}

template <class InputIt, class Size, class OutputIt>
std::pair<InputIt, OutputIt> uninitialized_move_n_impl(InputIt first, Size count, OutputIt dest, Default) {
  OutputIt current = dest;
  try {
    for (; count > 0; ++first, (void)++current, --count) {
      amc::construct_at(std::addressof(*current), ::std::move(*first));
    }
  } catch (...) {
    amc::destroy(dest, current);
    throw;
  }
  return std::pair<InputIt, OutputIt>(first, current);
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_move_n_impl(InputIt first, Size count, OutputIt dest,
                                                              MemMoveInALoop) {
  for (; count > 0; ++dest, void(), ++first, --count) {
    using ValueType = typename std::iterator_traits<InputIt>::value_type;
    ::std::memcpy(static_cast<void *>(std::addressof(*dest)), static_cast<const void *>(std::addressof(*first)),
                  sizeof(ValueType));
  }
  return std::pair<InputIt, OutputIt>(first, dest);
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_move_n_impl(InputIt first, Size count, OutputIt dest, MemMove) {
  return std::pair<InputIt, OutputIt>(first + count, uninitialized_copy_n_impl(first, count, dest, MemMove()));
}

}  // namespace memory_details

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_copy(InputIt first, InputIt last, OutputIt dest) {
  typedef typename memory_details::ImplModeFactory<
      InputIt, OutputIt, std::is_trivially_copyable<typename std::iterator_traits<InputIt>::value_type>::value>::type
      ImplMode;
  return memory_details::uninitialized_copy_impl(first, last, dest, ImplMode());
}

template <class InputIt, class Size, class OutputIt>
inline OutputIt uninitialized_copy_n(InputIt first, Size count, OutputIt dest) {
  typedef typename memory_details::ImplModeFactory<
      InputIt, OutputIt, std::is_trivially_copyable<typename std::iterator_traits<InputIt>::value_type>::value>::type
      ImplMode;
  return memory_details::uninitialized_copy_n_impl(first, count, dest, ImplMode());
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_move(InputIt first, InputIt last, OutputIt dest) {
  typedef typename memory_details::ImplModeFactory<
      InputIt, OutputIt, std::is_trivially_copyable<typename std::iterator_traits<InputIt>::value_type>::value>::type
      ImplMode;
  return memory_details::uninitialized_move_impl(first, last, dest, ImplMode());
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_move_n(InputIt first, Size count, OutputIt dest) {
  typedef typename memory_details::ImplModeFactory<
      InputIt, OutputIt, std::is_trivially_copyable<typename std::iterator_traits<InputIt>::value_type>::value>::type
      ImplMode;
  return memory_details::uninitialized_move_n_impl(first, count, dest, ImplMode());
}
#endif

/// Useful helping functions taking advantage of the properties of trivially relocatable types.
/// Relocate elements in [first, last) to another raw memory starting at dest, according to the traits of T.

namespace memory_details {
/// Relocate element pointed by 'elem' at a raw memory location 'dest'
template <class T>
inline T *relocate_at_impl(T *elem, T *dest, Default) {
  dest = amc::construct_at(dest, ::std::move(*elem));
  amc::destroy_at(elem);
  return dest;
}

template <class T>
inline T *relocate_at_impl(T *elem, T *dest, MemMove) {
#if __GNUC__ >= 8
  // Do not raise class-memaccess warning for trivially relocatable types
  AMC_PUSH_WARNING
  AMC_DISABLE_WARNING("-Wclass-memaccess")
#endif
  ::std::memmove(static_cast<void *>(dest), static_cast<const void *>(elem), sizeof(T));
#if __GNUC__ >= 8
  AMC_POP_WARNING
#endif
  return dest;
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_relocate_impl(InputIt first, InputIt last, OutputIt dest, Default) {
  dest = amc::uninitialized_move(first, last, dest);
  amc::destroy(first, last);
  return dest;
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_relocate_impl(InputIt first, InputIt last, OutputIt dest, MemMoveInALoop) {
  for (; first != last; ++dest, void(), ++first) {
    relocate_at_impl(std::addressof(*first), std::addressof(*dest), MemMove());
  }
  return dest;
}

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_relocate_impl(InputIt first, InputIt last, OutputIt dest, MemMove) {
  using ValueType = typename std::iterator_traits<InputIt>::value_type;
  typename std::iterator_traits<InputIt>::difference_type count = last - first;
  if (count > 0) {
#if __GNUC__ >= 8
    // Do not raise class-memaccess warning for trivially relocatable types
    AMC_PUSH_WARNING
    AMC_DISABLE_WARNING("-Wclass-memaccess")
#endif
    std::memmove(dest, first, count * sizeof(ValueType));
#if __GNUC__ >= 8
    AMC_POP_WARNING
#endif
  }
  return dest + count;
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_relocate_n_impl(InputIt first, Size count, OutputIt dest, Default) {
  std::pair<InputIt, OutputIt> p = amc::uninitialized_move_n(first, count, dest);
  amc::destroy_n(first, count);
  return p;
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_relocate_n_impl(InputIt first, Size count, OutputIt dest,
                                                                  MemMoveInALoop) {
  for (; count > 0; ++dest, void(), ++first, --count) {
    relocate_at_impl(std::addressof(*first), std::addressof(*dest), MemMove());
  }
  return std::pair<InputIt, OutputIt>(first, dest);
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_relocate_n_impl(InputIt first, Size count, OutputIt dest, MemMove) {
  using ValueType = typename std::iterator_traits<InputIt>::value_type;
  if (count > 0) {
#if __GNUC__ >= 8
    // Do not raise class-memaccess warning for trivially relocatable types
    AMC_PUSH_WARNING
    AMC_DISABLE_WARNING("-Wclass-memaccess")
#endif
    ::std::memmove(static_cast<void *>(dest), static_cast<const void *>(first),
                   static_cast<std::size_t>(count) * sizeof(ValueType));
#if __GNUC__ >= 8
    AMC_POP_WARNING
#endif
  }
  return std::pair<InputIt, OutputIt>(first + count, dest + count);
}
}  // namespace memory_details

template <class InputIt, class OutputIt>
inline OutputIt uninitialized_relocate(InputIt first, InputIt last, OutputIt dest) {
  typedef typename memory_details::ImplModeFactory<
      InputIt, OutputIt, amc::is_trivially_relocatable<typename std::iterator_traits<InputIt>::value_type>::value>::type
      ImplMode;
  return memory_details::uninitialized_relocate_impl(first, last, dest, ImplMode());
}

template <class InputIt, class Size, class OutputIt>
inline std::pair<InputIt, OutputIt> uninitialized_relocate_n(InputIt first, Size count, OutputIt dest) {
  typedef typename memory_details::ImplModeFactory<
      InputIt, OutputIt, amc::is_trivially_relocatable<typename std::iterator_traits<InputIt>::value_type>::value>::type
      ImplMode;
  return memory_details::uninitialized_relocate_n_impl(first, count, dest, ImplMode());
}

/// Relocate element pointed by 'elem' at a raw memory location 'dest'
template <class T>
inline T *relocate_at(T *elem, T *dest) {
  typedef typename std::conditional<amc::is_trivially_relocatable<T>::value, memory_details::MemMove,
                                    memory_details::Default>::type ImplMode;
  return memory_details::relocate_at_impl(elem, dest, ImplMode());
}

}  // namespace amc
