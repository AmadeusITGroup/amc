#pragma once

#include <memory>
#include <type_traits>
#include <utility>

#include "config.hpp"

// Standard library types declared trivially relocatable (see default_trivially_relocatable below). Their headers are
// only included if the types are declared.
#ifndef AMC_NO_STD_TRIVIALLY_RELOCATABLE
#include <array>
#include <exception>
#include <forward_list>
#include <tuple>
#include <vector>
#ifdef AMC_CXX17
#include <optional>
#include <variant>
#endif
#if defined(AMC_CXX23) && defined(__has_include)
#if __has_include(<expected>)
#include <expected>
#endif
#endif
#endif

#ifdef AMC_STD_STRING_TRIVIALLY_RELOCATABLE
#include <string>
#endif

#ifdef AMC_STD_DEQUE_TRIVIALLY_RELOCATABLE
#include <deque>
#endif

// AddressSanitizer, which may poison bytes inside the objects of some standard library types (undefined at the end)
#if defined(__SANITIZE_ADDRESS__)
#define AMC_TYPETRAITS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define AMC_TYPETRAITS_ASAN 1
#endif
#endif

// Debug modes of the standard libraries checking iterators, in which the containers point back to themselves
// (undefined at the end): libstdc++ with _GLIBCXX_DEBUG, the MSVC STL with an _ITERATOR_DEBUG_LEVEL of 1 or 2 (2 by
// default in Debug builds), libc++ before LLVM 17 with its debug mode.
#if defined(_GLIBCXX_DEBUG) || defined(_LIBCPP_ENABLE_DEBUG_MODE) || defined(_LIBCPP_DEBUG) || \
    (defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0)
#define AMC_TYPETRAITS_DEBUG_ITERATORS 1
#endif

namespace amc {

// is_nothrow_swappable
#ifdef AMC_CXX17
template <class T>
using is_nothrow_swappable = std::is_nothrow_swappable<T>;
#else
namespace typetraits_details {
using std::swap;

struct do_is_swappable_impl {
  template <typename T, typename = decltype(swap(std::declval<T&>(), std::declval<T&>()))>
  static std::true_type test(int);

  template <typename>
  static std::false_type test(...);
};

struct do_is_nothrow_swappable_impl {
  template <typename T>
  static std::integral_constant<bool, noexcept(swap(std::declval<T&>(), std::declval<T&>()))> test(int);

  template <typename>
  static std::false_type test(...);
};
}  // namespace typetraits_details

template <typename T>
struct is_swappable_impl : public typetraits_details::do_is_swappable_impl {
  using type = decltype(test<T>(0));
};

template <typename T>
struct is_nothrow_swappable_impl : public typetraits_details::do_is_nothrow_swappable_impl {
  using type = decltype(test<T>(0));
};

template <typename T>
struct is_swappable : public is_swappable_impl<T>::type {};

template <typename T>
struct is_nothrow_swappable : public is_nothrow_swappable_impl<T>::type {};
#endif

namespace typetraits_details {

template <typename... Ts>
struct make_void {
  using type = void;
};

template <typename T, typename = void>
struct has_trivially_relocatable : std::false_type {};

template <typename T>
struct has_trivially_relocatable<T, typename make_void<typename T::trivially_relocatable>::type> : std::true_type {};

/// is_trivially_relocatable<T> of the types without a 'trivially_relocatable' member type: the trivially copyable
/// types, and the standard library types of the specializations below. These are not specializations of
/// is_trivially_relocatable, which a program can thus still specialize for these types, overriding them.
template <typename T>
struct default_trivially_relocatable : public std::is_trivially_copyable<T> {};

template <typename T, bool>
struct is_trivially_relocatable_impl : public default_trivially_relocatable<T> {};

template <typename T>
struct is_trivially_relocatable_impl<T, true> : public std::is_same<typename T::trivially_relocatable, std::true_type> {
};
}  // namespace typetraits_details

template <typename T>
struct is_trivially_relocatable;

namespace typetraits_details {
/// A cv-qualified type is trivially relocatable if its unqualified type is, including through a specialization of
/// is_trivially_relocatable for it: the values of map-like containers, std::pair<const K, V>, need nothing more.
template <typename T, typename U = typename std::remove_cv<T>::type>
struct is_trivially_relocatable_dispatch : is_trivially_relocatable<U> {};

template <typename T>
struct is_trivially_relocatable_dispatch<T, T> : is_trivially_relocatable_impl<T, has_trivially_relocatable<T>::value> {
};
}  // namespace typetraits_details

/**
 * is_trivially_relocatable<T>::value describes the ability of moving around
 * memory a value of type T by using memcpy (as opposed to the
 * conservative approach of calling the copy constructor and then
 * destroying the old temporary. Essentially for a relocatable type,
 * the following two sequences of code should be semantically
 * equivalent:
 *
 * void move1(T * from, T * to) {
 *   new(to) T(from);
 *   (*from).~T();
 * }
 *
 * void move2(T * from, T * to) {
 *   memcpy(to, from, sizeof(T));
 * }
 *
 * The default conservatively assumes the type is relocatable if it is trivially copyable, and knows some standard
 * library types (see default_trivially_relocatable). A cv-qualified type is relocatable if its unqualified type is.
 * You may want to add your own specializations, also for standard library types (they take precedence).
 * Do so in namespace amc and make sure you keep the specialization of
 * is_trivially_relocatable<SomeStruct> in the same header as SomeStruct.
 *
 * You may also declare a type to be relocatable by defining
 *    `using trivially_relocatable = std::true_type;`
 * in the class declaration.
 * */
template <typename T>
struct is_trivially_relocatable : typetraits_details::is_trivially_relocatable_dispatch<T> {};

namespace typetraits_details {
template <class... Ts>
struct all_trivially_relocatable : std::true_type {};

template <class T, class... Ts>
struct all_trivially_relocatable<T, Ts...>
    : std::integral_constant<bool, is_trivially_relocatable<T>::value && all_trivially_relocatable<Ts...>::value> {};

#ifdef AMC_CXX17
template <class Alloc>
struct allocator_is_always_equal : std::allocator_traits<Alloc>::is_always_equal {};
#else
/// std::allocator_traits<Alloc>::is_always_equal is only standard from C++17: Alloc::is_always_equal if it exists,
/// std::is_empty<Alloc> otherwise.
template <class Alloc, class = void>
struct allocator_is_always_equal : std::is_empty<Alloc> {};

template <class Alloc>
struct allocator_is_always_equal<Alloc, typename make_void<typename Alloc::is_always_equal>::type>
    : std::integral_constant<bool, Alloc::is_always_equal::value> {};
#endif

/// Containers holding an allocator and pointers of this allocator (possibly fancy pointers) to their heap storage.
/// The vectors relocate their elements instead of move assigning them when they insert or erase: the allocator must
/// also move along with the elements in a move assignment, or be always equal. A container whose allocator does not
/// propagate and may differ (std::pmr::polymorphic_allocator) keeps its allocator when move assigned, it would get the
/// one of the relocated container instead.
template <class Alloc>
struct allocator_trivially_relocatable
    : std::integral_constant<bool,
                             all_trivially_relocatable<Alloc, typename std::allocator_traits<Alloc>::pointer>::value &&
                                 (std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value ||
                                  allocator_is_always_equal<Alloc>::value)> {};

// The standard library types below hold no pointer to themselves in libstdc++, libc++ and the MSVC STL, which do not
// make them trivially copyable: they are trivially relocatable if what they store is.

/// std::pair only holds its two members.
template <class T, class U>
struct default_trivially_relocatable<std::pair<T, U> > : all_trivially_relocatable<T, U> {};

/// std::allocator is stateless, but not trivially copyable in all implementations (user-provided copy constructor).
template <class T>
struct default_trivially_relocatable<std::allocator<T> > : std::true_type {};

// The other ones can be removed by defining AMC_NO_STD_TRIVIALLY_RELOCATABLE (CMake option
// AMC_STD_TRIVIALLY_RELOCATABLE=OFF), for a standard library implementation in which they would not be.
#ifndef AMC_NO_STD_TRIVIALLY_RELOCATABLE
template <class... Ts>
struct default_trivially_relocatable<std::tuple<Ts...> > : all_trivially_relocatable<Ts...> {};

/// A std::array<T, 0> holds no element with libstdc++ and libc++, and one with the MSVC STL (if T is default
/// constructible).
template <class T, std::size_t N>
struct default_trivially_relocatable<std::array<T, N> > : is_trivially_relocatable<T> {};

/// A std::unique_ptr holds its pointer (which may be a fancy pointer of the deleter) and its deleter.
template <class T, class Deleter>
struct default_trivially_relocatable<std::unique_ptr<T, Deleter> >
    : all_trivially_relocatable<typename std::unique_ptr<T, Deleter>::pointer, Deleter> {};

/// std::shared_ptr, std::weak_ptr and std::exception_ptr point to reference counted objects.
template <class T>
struct default_trivially_relocatable<std::shared_ptr<T> > : std::true_type {};

template <class T>
struct default_trivially_relocatable<std::weak_ptr<T> > : std::true_type {};

template <>
struct default_trivially_relocatable<std::exception_ptr> : std::true_type {};

// A std::vector (and std::vector<bool>) holds its allocator and pointers to its heap storage, and a std::forward_list
// its allocator and a pointer to its first node, except in the debug modes checking the iterators, in which the
// containers point back to themselves. The other containers are not here: std::deque is only declared on request
// (below), and libstdc++ and libc++ store in std::list, std::map, std::set and the unordered containers a node to which
// the other nodes point.
#ifndef AMC_TYPETRAITS_DEBUG_ITERATORS
template <class T, class Alloc>
struct default_trivially_relocatable<std::vector<T, Alloc> > : allocator_trivially_relocatable<Alloc> {};

template <class T, class Alloc>
struct default_trivially_relocatable<std::forward_list<T, Alloc> > : allocator_trivially_relocatable<Alloc> {};
#endif

#ifdef AMC_CXX17
// With AddressSanitizer, the MSVC STL poisons the storage of an empty std::optional (of a type which is not trivially
// destructible) inside the object: copying its bytes would be reported.
#if !defined(_MSVC_STL_VERSION) || \
    !(defined(AMC_TYPETRAITS_ASAN) || defined(_ANNOTATE_OPTIONAL) || defined(_ANNOTATE_STL))
template <class T>
struct default_trivially_relocatable<std::optional<T> > : is_trivially_relocatable<T> {};
#endif

template <class... Ts>
struct default_trivially_relocatable<std::variant<Ts...> > : all_trivially_relocatable<Ts...> {};
#endif

#ifdef __cpp_lib_expected
/// A std::expected<cv void, E> only holds an E.
template <class T, class E>
struct default_trivially_relocatable<std::expected<T, E> >
    : all_trivially_relocatable<typename std::conditional<std::is_void<T>::value, int, T>::type, E> {};
#endif
#endif  // AMC_NO_STD_TRIVIALLY_RELOCATABLE

// std::basic_string is only declared trivially relocatable on request (AMC_STD_STRING_TRIVIALLY_RELOCATABLE), as in
// the default ABI of libstdc++, the pointer to the characters of a short string points to its inline buffer. It is
// then declared for the implementations whose strings never point to themselves: libc++ (not with the debug mode of
// LLVM 16 and older, which registers each string in its debug database, like std::vector), the MSVC STL (with no
// iterator debugging), and the copy-on-write strings of the old ABI of libstdc++ (_GLIBCXX_USE_CXX11_ABI=0). Not with
// AddressSanitizer either, as libc++ may poison the unused part of the inline buffer.
#if defined(AMC_STD_STRING_TRIVIALLY_RELOCATABLE) && !defined(AMC_TYPETRAITS_ASAN) &&                \
    ((defined(_LIBCPP_VERSION) && !defined(_LIBCPP_ENABLE_DEBUG_MODE) && !defined(_LIBCPP_DEBUG)) || \
     (defined(_MSVC_STL_VERSION) && defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL == 0) || \
     (defined(__GLIBCXX__) && defined(_GLIBCXX_USE_CXX11_ABI) && _GLIBCXX_USE_CXX11_ABI == 0))
template <class CharT, class Traits, class Alloc>
struct default_trivially_relocatable<std::basic_string<CharT, Traits, Alloc> >
    : allocator_trivially_relocatable<Alloc> {};
#endif

// std::deque is only declared trivially relocatable on request (AMC_STD_DEQUE_TRIVIALLY_RELOCATABLE), as <deque> makes
// every file including amc compile 2 to 5 % slower, for vectors which rarely hold deques. It is then declared like
// std::vector for libstdc++ and libc++, whose deques hold their allocator, a pointer to their map of pointers to the
// blocks of elements and positions in them. Not for the MSVC STL, nor for the other implementations: in all modes, the
// MSVC STL allocates a proxy pointing back to the deque, through which its iterators reach it.
#if defined(AMC_STD_DEQUE_TRIVIALLY_RELOCATABLE) && !defined(AMC_TYPETRAITS_DEBUG_ITERATORS) && \
    (defined(__GLIBCXX__) || defined(_LIBCPP_VERSION))
template <class T, class Alloc>
struct default_trivially_relocatable<std::deque<T, Alloc> > : allocator_trivially_relocatable<Alloc> {};
#endif
}  // namespace typetraits_details

#ifdef AMC_CXX17
template <typename T>
inline constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;
#endif
}  // namespace amc

#undef AMC_TYPETRAITS_ASAN
#undef AMC_TYPETRAITS_DEBUG_ITERATORS
