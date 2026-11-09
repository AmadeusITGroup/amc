#pragma once

#include "config.hpp"

#ifdef AMC_CXX20
#include <version>
#endif

// The value 202311L is the one of the first implementations, which name this function std::add_sat: they get the
// emulation.
#if defined(__cpp_lib_saturation_arithmetic) && __cpp_lib_saturation_arithmetic >= 202603L

#include <numeric>

namespace amc {
using std::saturating_add;
}  // namespace amc

#else

#include <limits>
#include <type_traits>

namespace amc {

namespace numeric_details {
template <class T>
struct IsBoolOrCharacter : std::false_type {};
template <>
struct IsBoolOrCharacter<bool> : std::true_type {};
template <>
struct IsBoolOrCharacter<char> : std::true_type {};
template <>
struct IsBoolOrCharacter<wchar_t> : std::true_type {};
template <>
struct IsBoolOrCharacter<char16_t> : std::true_type {};
template <>
struct IsBoolOrCharacter<char32_t> : std::true_type {};
#ifdef __cpp_char8_t
template <>
struct IsBoolOrCharacter<char8_t> : std::true_type {};
#endif

/// The "signed or unsigned integer types" of the standard, which are the integral types except these ones
template <class T>
struct IsInteger : std::integral_constant<bool, std::is_integral<T>::value && !IsBoolOrCharacter<T>::value> {};
}  // namespace numeric_details

/// Emulation of std::saturating_add (C++26): returns x + y, or the limit of T closest to it when T cannot represent it.
template <class T, typename std::enable_if<numeric_details::IsInteger<T>::value && std::is_unsigned<T>::value,
                                           bool>::type = true>
constexpr T saturating_add(T x, T y) noexcept {
  return static_cast<T>(std::numeric_limits<T>::max() - x) < y ? std::numeric_limits<T>::max() : static_cast<T>(x + y);
}

template <class T,
          typename std::enable_if<numeric_details::IsInteger<T>::value && std::is_signed<T>::value, bool>::type = true>
constexpr T saturating_add(T x, T y) noexcept {
  return (y < 0 && x < static_cast<T>(std::numeric_limits<T>::min() - y))   ? std::numeric_limits<T>::min()
         : (y > 0 && x > static_cast<T>(std::numeric_limits<T>::max() - y)) ? std::numeric_limits<T>::max()
                                                                            : static_cast<T>(x + y);
}

}  // namespace amc

#endif
