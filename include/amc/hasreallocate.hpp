#pragma once

#include <utility>

#include "isdetected.hpp"

namespace amc {
namespace vec {

/// Implement the member detection idiom to know if allocator provides 'reallocate' method.
template <class T>
using has_reallocate_t = decltype(std::declval<T>().reallocate(
    std::declval<typename T::pointer>(), std::declval<typename T::size_type>(), std::declval<typename T::size_type>(),
    std::declval<typename T::size_type>()));

template <class T>
using has_reallocate = is_detected<has_reallocate_t, T>;
}  // namespace vec
}  // namespace amc