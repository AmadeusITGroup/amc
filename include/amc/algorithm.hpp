#pragma once

#include "config.hpp"

#ifdef AMC_CXX20
#include <algorithm>
#include <compare>
#include <iterator>
#include <type_traits>
#include <utility>
#endif

namespace amc {

#ifdef AMC_CXX20
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
    if (f2 == l2) {
      return std::strong_ordering::greater;
    }
    if (auto c = comp(*f1, *f2); c != 0) {
      return c;
    }
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

namespace algorithm_details {
#if defined(__cpp_lib_three_way_comparison) && __cpp_lib_three_way_comparison >= 201907L
template <class T, class U>
concept ThreeWayComparableWith = std::three_way_comparable_with<T, U>;
#else
template <class T, class U>
concept ThreeWayComparableWith = requires(const T& t, const U& u) { t <=> u; };
#endif

/// Emulation of the exposition only 'boolean-testable' concept (without std::convertible_to, which needs <concepts>).
template <class B>
concept BooleanTestableImpl = std::is_convertible_v<B, bool>;

template <class B>
concept BooleanTestable = BooleanTestableImpl<B> && requires(B&& b) {
  { !std::forward<B>(b) } -> BooleanTestableImpl;
};

/// Constraint of the exposition only 'synth-three-way': 'T' and 'U' are ordered with operator< (which operator<=>
/// provides too). Like the standard containers, the amc ones only define operator<=> for elements satisfying it, so
/// that type traits such as std::three_way_comparable tell that the containers of other elements are not ordered.
template <class T, class U = T>
concept SynthThreeWayComparable = requires(const T& t, const U& u) {
  { t < u } -> BooleanTestable;
  { u < t } -> BooleanTestable;
};

/// Tells whether operator< or operator> is declared for 'T' and 'U' (in any order), as a member or a non-member
/// function: unlike the expressions 't < u' and 't > u', the explicit calls of the operator functions do not consider
/// the candidates rewritten from operator<=>, nor the built-in operators. Such declared operators may define another
/// ordering than operator<=>.
template <class T, class U>
concept DeclaresRelationalOperator =
    requires(const T& t, const U& u) { operator<(t, u); } || requires(const T& t, const U& u) { operator<(u, t); } ||
    requires(const T& t, const U& u) { operator>(t, u); } || requires(const T& t, const U& u) { operator>(u, t); } ||
    requires(const T& t, const U& u) { t.operator<(u); } || requires(const T& t, const U& u) { u.operator<(t); } ||
    requires(const T& t, const U& u) { t.operator>(u); } || requires(const T& t, const U& u) { u.operator>(t); };

/// Erases the elements of 'v', the vector of a set (whose elements are unique), satisfying 'pred' in a single pass,
/// keeping the order of the other ones. If 'pred' throws, the elements already found are erased and the other ones
/// kept, like with std::erase_if of a std::set. If moving an element may throw, 'v' is cleared instead, as it could
/// keep a moved from element, which may break the order or be a duplicate.
template <class Vec, class Pred>
auto EraseIfFromSetVector(Vec& v, Pred& pred) {
  using T = typename Vec::value_type;
  const auto last = v.end();
  auto dest = std::find_if(v.begin(), last, pred);
  if (dest == last) {
    return typename Vec::size_type{0};
  }
  auto it = std::next(dest);
  try {
    for (; it != last; ++it) {
      if (!pred(std::as_const(*it))) {
        *dest = std::move(*it);
        ++dest;
      }
    }
  } catch (...) {
    if constexpr (std::is_nothrow_move_assignable_v<T>) {
      v.erase(dest, it);
    } else {
      v.clear();
    }
    throw;
  }
  const auto nbErased = static_cast<typename Vec::size_type>(last - dest);
  v.erase(dest, last);
  return nbErased;
}
}  // namespace algorithm_details

/// Emulation of the exposition only 'synth-three-way' used by the comparison operators of the standard containers:
/// elements are compared with operator<=> if they can, otherwise with operator< (giving a std::weak_ordering).
struct synth_three_way {
  template <class T, class U>
    requires algorithm_details::SynthThreeWayComparable<T, U>
  constexpr auto operator()(const T& t, const U& u) const {
    if constexpr (algorithm_details::ThreeWayComparableWith<T, U>) {
      return t <=> u;
    } else {
      if (t < u) return std::weak_ordering::less;
      if (u < t) return std::weak_ordering::greater;
      return std::weak_ordering::equivalent;
    }
  }
};
#endif

}  // namespace amc
