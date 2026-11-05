#pragma once

#include <amc/config.hpp>
#include <cstdint>

#ifdef AMC_CXX20
#include <compare>
#include <type_traits>
#endif

namespace amc {

inline uint64_t HashValue64(uint64_t x) {
  // Murmur-inspired hashing.
  constexpr uint64_t kMul = 0x9ddfea08eb382d69ULL;
  uint64_t b = x * kMul;
  b ^= (b >> 44);
  b *= kMul;
  b ^= (b >> 41);
  b *= kMul;
  return b;
}

#ifdef AMC_CXX20
/// Tells whether 'Container' has the three-way comparison of the standard container 'RefContainer': none (operator<=>
/// does not participate in overload resolution for elements which are not ordered), or one of the same result type.
template <class Container, class RefContainer>
constexpr bool HasThreeWayComparisonOf() {
  if constexpr (!std::three_way_comparable<RefContainer>) {
    return !std::three_way_comparable<Container>;
  } else if constexpr (std::three_way_comparable<Container>) {
    return std::is_same_v<std::compare_three_way_result_t<Container>, std::compare_three_way_result_t<RefContainer>>;
  } else {
    return false;
  }
}
#endif
}  // namespace amc
