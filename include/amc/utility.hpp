#pragma once

#include <utility>

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
}  // namespace amc
