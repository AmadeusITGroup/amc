#pragma once

#include <cstdint>

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
}  // namespace amc
