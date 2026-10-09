// Tests of the emulations of standard library features that amc provides before C++17.
// GoogleTest requires C++17, so this test is a plain executable compiled in C++11 (C++14 for MSVC), reporting failures
// with its exit code. Its checks hold for the standard library features as well, from C++17.

#include <amc/memory.hpp>
#include <amc/type_traits.hpp>
#include <cstdio>
#include <type_traits>

namespace {

int gNbFailures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", description);
    ++gNbFailures;
  }
}

struct NonSwappable {
  NonSwappable() = default;
  NonSwappable(NonSwappable&&) = delete;
  NonSwappable& operator=(NonSwappable&&) = delete;
};

struct ThrowingMove {
  ThrowingMove() = default;
  ThrowingMove(ThrowingMove&&) noexcept(false) {}
  ThrowingMove& operator=(ThrowingMove&&) noexcept(false) { return *this; }
};

struct DefaultConstructible {
  int _i = 42;
};

static_assert(!std::is_trivially_default_constructible<DefaultConstructible>::value, "");

/// Default constructor throwing once 'sNbConstructionsBeforeThrow' objects have been constructed, counting live objects
struct ThrowingDefaultConstructor {
  static int sNbConstructionsBeforeThrow;
  static int sNbLive;

  ThrowingDefaultConstructor() {
    if (sNbConstructionsBeforeThrow-- == 0) {
      throw 42;
    }
    ++sNbLive;
  }

  ~ThrowingDefaultConstructor() { --sNbLive; }
};

int ThrowingDefaultConstructor::sNbConstructionsBeforeThrow = 0;
int ThrowingDefaultConstructor::sNbLive = 0;

/// Checks that when the construction of an element throws, 'constructN' destroys the already constructed ones before
/// propagating the exception
template <class ConstructN>
void CheckConstructionThrows(ConstructN constructN, const char* description) {
  alignas(ThrowingDefaultConstructor) unsigned char storage[4 * sizeof(ThrowingDefaultConstructor)];
  ThrowingDefaultConstructor::sNbConstructionsBeforeThrow = 2;
  bool thrown = false;
  try {
    constructN(reinterpret_cast<ThrowingDefaultConstructor*>(storage), 4);
  } catch (int) {
    thrown = true;
  }
  Check(thrown && ThrowingDefaultConstructor::sNbLive == 0, description);
}

}  // namespace

static_assert(amc::is_nothrow_swappable<int>::value, "");
static_assert(!amc::is_nothrow_swappable<ThrowingMove>::value, "");
static_assert(!amc::is_nothrow_swappable<NonSwappable>::value, "");

#ifndef AMC_CXX17
// Before C++17, std::swap is not required to be constrained on movable types (it is not with MSVC STL): whether
// NonSwappable is swappable depends on the standard library.
static_assert(amc::is_swappable<int>::value, "");
static_assert(amc::is_swappable<ThrowingMove>::value, "");
#endif

int main() {
  {
    // trivially default constructible: nothing to construct, but the end of the range is returned
    int buf[4];
    Check(amc::uninitialized_default_construct_n(buf, 4) == buf + 4, "uninitialized_default_construct_n trivial");
  }
  {
    alignas(DefaultConstructible) unsigned char storage[3 * sizeof(DefaultConstructible)];
    DefaultConstructible* first = reinterpret_cast<DefaultConstructible*>(storage);
    DefaultConstructible* last = amc::uninitialized_default_construct_n(first, 3);
    Check(last == first + 3, "uninitialized_default_construct_n non trivial end");
    Check(first[0]._i == 42 && first[2]._i == 42, "uninitialized_default_construct_n non trivial values");
    amc::destroy(first, last);
  }
  {
    int buf[4] = {1, 2, 3, 4};
    Check(amc::uninitialized_value_construct_n(buf, 4) == buf + 4, "uninitialized_value_construct_n trivial end");
    Check(buf[0] == 0 && buf[3] == 0, "uninitialized_value_construct_n trivial values");
  }
  {
    alignas(DefaultConstructible) unsigned char storage[2 * sizeof(DefaultConstructible)];
    DefaultConstructible* first = reinterpret_cast<DefaultConstructible*>(storage);
    DefaultConstructible* last = amc::uninitialized_value_construct_n(first, 2);
    Check(last == first + 2, "uninitialized_value_construct_n non trivial end");
    Check(first[1]._i == 42, "uninitialized_value_construct_n non trivial values");
    amc::destroy(first, last);
  }
  CheckConstructionThrows(
      [](ThrowingDefaultConstructor* first, int n) { amc::uninitialized_default_construct_n(first, n); },
      "uninitialized_default_construct_n throwing construction");
  CheckConstructionThrows(
      [](ThrowingDefaultConstructor* first, int n) { amc::uninitialized_value_construct_n(first, n); },
      "uninitialized_value_construct_n throwing construction");

  if (gNbFailures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", gNbFailures);
    return 1;
  }
  std::puts("All checks passed");
  return 0;
}
