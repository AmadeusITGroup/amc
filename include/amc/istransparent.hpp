#pragma once

#include <utility>

#include "isdetected.hpp"

namespace amc {

/// Implement the member detection idiom to know if T provides 'is_transparent' type
template <class T>
using has_is_transparent_t = typename T::is_transparent;

template <class T>
using has_is_transparent = is_detected<has_is_transparent_t, T>;
}  // namespace amc