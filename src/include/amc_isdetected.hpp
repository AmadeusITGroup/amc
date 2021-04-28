#pragma once

#include <type_traits>

#include "amc_config.hpp"

/// Emulation of std::experimental::is_detected feature.
/// Sources:
///    https://people.eecs.berkeley.edu/~brock/blog/detection_idiom.php
///       - Excellent blog page explaining the evolution of C++ detection idiom implementations)
///    https://en.cppreference.com/w/cpp/experimental/is_detected
///       - Code taken from above page
namespace amc {

#ifdef AMC_CXX17
using std::void_t;
#else
template <class...>
using void_t = void;
#endif

namespace detail {
template <class Default, class AlwaysVoid, template <class...> class Op, class... Args>
struct detector {
  using value_t = std::false_type;
  using type = Default;
};

template <class Default, template <class...> class Op, class... Args>
struct detector<Default, void_t<Op<Args...>>, Op, Args...> {
  using value_t = std::true_type;
  using type = Op<Args...>;
};

}  // namespace detail

struct nonesuch {
  ~nonesuch() = delete;
  nonesuch(nonesuch const&) = delete;
  void operator=(nonesuch const&) = delete;
};

template <template <class...> class Op, class... Args>
using is_detected = typename detail::detector<nonesuch, void, Op, Args...>::value_t;

template <template <class...> class Op, class... Args>
using detected_t = typename detail::detector<nonesuch, void, Op, Args...>::type;

template <class Default, template <class...> class Op, class... Args>
using detected_or = detail::detector<Default, void, Op, Args...>;

template <class T>
using copy_assign_t = decltype(std::declval<T&>() = std::declval<const T&>());

template <class Expected, template <class...> class Op, class... Args>
using is_detected_exact = std::is_same<Expected, detected_t<Op, Args...>>;

template <class Default, template <class...> class Op, class... Args>
using detected_or_t = typename detected_or<Default, Op, Args...>::type;
}  // namespace amc