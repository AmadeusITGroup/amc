#pragma once

#include <type_traits>
#include <utility>

#include "amc_config.hpp"

namespace amc {

// is_nothrow_swappable
#ifdef AMC_CXX17
template <class T>
using is_nothrow_swappable = std::is_nothrow_swappable<T>;
#else
namespace typetraits_details {
using std::swap;

struct do_is_swappable_impl {
  template <typename T, typename = decltype(swap(std::declval<T &>(), std::declval<T &>()))>
  static std::true_type test(int);

  template <typename>
  static std::false_type test(...);
};

struct do_is_nothrow_swappable_impl {
  template <typename T>
  static std::integral_constant<bool, noexcept(swap(std::declval<T &>(), std::declval<T &>()))> test(int);

  template <typename>
  static std::false_type __test(...);
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

template <typename T, bool>
struct is_trivially_relocatable_impl : public std::is_trivially_copyable<T> {};

template <typename T>
struct is_trivially_relocatable_impl<T, true> : public std::is_same<typename T::trivially_relocatable, std::true_type> {
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
 * The default conservatively assumes the type is relocatable if it is trivially copyable.
 * You may want to add your own specializations.
 * Do so in namespace amc and make sure you keep the specialization of
 * is_trivially_relocatable<SomeStruct> in the same header as SomeStruct.
 *
 * You may also declare a type to be relocatable by defining
 *    `using trivially_relocatable = std::true_type;`
 * in the class declaration.
 * */
template <typename T>
struct is_trivially_relocatable
    : typetraits_details::is_trivially_relocatable_impl<T, typetraits_details::has_trivially_relocatable<T>::value> {};

template <class T, class U>
struct is_trivially_relocatable<std::pair<T, U> >
    : std::integral_constant<bool, is_trivially_relocatable<T>::value && is_trivially_relocatable<U>::value> {};

#ifdef AMC_CXX17
template <typename T>
inline constexpr bool is_trivially_relocatable_v = is_trivially_relocatable<T>::value;
#endif
}  // namespace amc
