#include "amc_isdetected.hpp"

#include "amc_allocator.hpp"
#include "amc_smallvector.hpp"

namespace amc {
static_assert(vec::has_reallocate<amc::allocator<int>>::value, "amc::allocator should provide reallocate method");
static_assert(!vec::has_reallocate<std::allocator<int>>::value, "std::allocator does not provide reallocate method");

struct Meow {
  using pointer = const char *;
  using size_type = int;

  pointer reallocate(pointer);
  pointer reallocate(pointer, size_type);
  pointer reallocate(pointer, size_type, size_type);
};

struct Purr {
  using pointer = const char *;
  using size_type = int;

  Purr &operator=(const Purr &) = delete;

  pointer reallocate(pointer, size_type, size_type, size_type);
};

static_assert(is_detected<copy_assign_t, Meow>::value, "Meow should be copy assignable!");
static_assert(!is_detected<copy_assign_t, Purr>::value, "Purr should not be copy assignable!");
static_assert(is_detected_exact<Meow &, copy_assign_t, Meow>::value, "Copy assignment of Meow should return Meow&!");

static_assert(!vec::has_reallocate<Meow>::value, "Meow should not provide reallocate method");
static_assert(vec::has_reallocate<Purr>::value, "Purr should provide reallocate method");
}  // namespace amc