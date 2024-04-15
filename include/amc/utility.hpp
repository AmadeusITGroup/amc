#pragma once

#include <utility>

#include "config.hpp"

#if AMC_CXX20
#include <algorithm>
#include <compare>
#endif

namespace amc {

// Provides emulation of std::exchange if C++14 is not supported.
#ifdef __cpp_lib_exchange_function
using std::exchange;
#else
template <class T, class U = T>
T exchange(T &obj, U &&new_value) {
  T old_value = std::move(obj);
  obj = std::forward<U>(new_value);
  return old_value;
}
#endif

#if AMC_CXX20
#if !defined(_LIBCPP_VERSION) || _LIBCPP_VERSION >= 170000
using std::lexicographical_compare_three_way;
#else
// Adjusted from https://en.cppreference.com/w/cpp/algorithm/lexicographical_compare_three_way
template <class I1, class I2, class Cmp>
constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2, Cmp comp) -> decltype(comp(*f1, *f2)) {
  using ret_t = decltype(comp(*f1, *f2));
  static_assert(std::disjunction_v<std::is_same<ret_t, std::strong_ordering>, std::is_same<ret_t, std::weak_ordering>,
                                   std::is_same<ret_t, std::partial_ordering> >,
                "The return type must be a comparison category type.");

  for (; f1 != l1; ++f1, ++f2) {
    if (f2 == l2) return std::strong_ordering::greater;
    if (auto c = comp(*f1, *f2); c != 0) return c;
  }
  return (f2 == l2) <=> true;
}
#if _LIBCPP_VERSION >= 14000
using std::compare_three_way;
#else
struct compare_three_way {
  using is_transparent = void;
  template <class T, class U>
  constexpr auto operator()(T&& t, U&& u) const noexcept(noexcept(std::declval<T>() <=> std::declval<U>())) {
    return static_cast<T&&>(t) <=> static_cast<U&&>(u);
  }
};
#endif
template <class I1, class I2>
constexpr auto lexicographical_compare_three_way(I1 f1, I1 l1, I2 f2, I2 l2) {
  return lexicographical_compare_three_way(f1, l1, f2, l2, amc::compare_three_way());
}
#endif
#endif

}  // namespace amc
