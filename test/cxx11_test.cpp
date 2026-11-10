// Tests of the emulations of standard library features that amc provides before C++17, and of the containers available
// in C++11 (instantiating their templates in C++11).
// GoogleTest requires C++17, so this test is a plain executable compiled in C++11 (C++14 for MSVC, and in the builds
// whose standard is C++14), reporting failures with its exit code. Its checks hold for the standard library features
// as well, from C++17.

#include <algorithm>
#include <amc/allocator.hpp>
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/memory.hpp>
#include <amc/numeric.hpp>
#include <amc/smallvector.hpp>
#include <amc/type_traits.hpp>
#include <amc/vector.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iterator>
#include <limits>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

/// Deleter of a std::unique_ptr which is not trivially copyable
struct NonTriviallyCopyableDeleter {
  NonTriviallyCopyableDeleter() = default;
  NonTriviallyCopyableDeleter(const NonTriviallyCopyableDeleter&) {}
  void operator()(int* p) const { delete p; }
};

// A program can specialize amc::is_trivially_relocatable for the standard library types that amc declares trivially
// relocatable (as it may have done before amc declared them): its specializations take precedence.
namespace amc {
template <class T, class Deleter>
struct is_trivially_relocatable<std::unique_ptr<T, Deleter>> : std::true_type {};

template <>
struct is_trivially_relocatable<std::shared_ptr<int>> : std::false_type {};
}  // namespace amc

static_assert(amc::is_trivially_relocatable<std::unique_ptr<int, NonTriviallyCopyableDeleter>>::value, "");
static_assert(!amc::is_trivially_relocatable<std::shared_ptr<int>>::value, "");
// a cv-qualified type follows the specialization of its unqualified type
static_assert(amc::is_trivially_relocatable<const std::unique_ptr<int, NonTriviallyCopyableDeleter>>::value, "");
static_assert(!amc::is_trivially_relocatable<std::pair<const std::shared_ptr<int>, int>>::value, "");
#ifndef AMC_NO_STD_TRIVIALLY_RELOCATABLE
static_assert(amc::is_trivially_relocatable<std::shared_ptr<char>>::value, "");
static_assert(amc::is_trivially_relocatable<std::tuple<int, std::unique_ptr<int>>>::value, "");
#endif

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

/// Constructors (default, and from an int) throwing once 'sNbConstructionsBeforeThrow' objects have been constructed,
/// counting live objects
struct ThrowingConstructor {
  static int sNbConstructionsBeforeThrow;
  static int sNbLive;

  ThrowingConstructor() {
    if (sNbConstructionsBeforeThrow-- == 0) {
      throw 42;
    }
    ++sNbLive;
  }

  explicit ThrowingConstructor(int) : ThrowingConstructor() {}

  ~ThrowingConstructor() { --sNbLive; }
};

int ThrowingConstructor::sNbConstructionsBeforeThrow = 0;
int ThrowingConstructor::sNbLive = 0;

/// Checks that when the construction of an element throws, 'constructN' destroys the already constructed ones before
/// propagating the exception
template <class ConstructN>
void CheckConstructionThrows(ConstructN constructN, const char* description) {
  alignas(ThrowingConstructor) unsigned char storage[4 * sizeof(ThrowingConstructor)];
  ThrowingConstructor::sNbConstructionsBeforeThrow = 2;
  bool thrown = false;
  try {
    constructN(reinterpret_cast<ThrowingConstructor*>(storage), 4);
  } catch (int) {
    thrown = true;
  }
  Check(thrown && ThrowingConstructor::sNbLive == 0, description);
}

/// Unscoped enumeration: its values convert implicitly to the size types and to the element types.
enum SmallCount { kTwo = 2, kFortyTwo = 42 };

/// Basic operations of a vector, growing it beyond its inline storage if it has one
template <class VectorType>
void CheckVector(const char* description) {
  using Type = typename VectorType::value_type;
  VectorType v;
  for (int i = 0; i < 10; ++i) {
    v.push_back(static_cast<Type>(i));
  }
  v.insert(v.begin() + 2, 3U, static_cast<Type>(42));
  v.erase(v.begin());
  VectorType copy(v);
  VectorType moved(std::move(copy));
  Check(moved == v && v.size() == 12U && v[1] == 42 && v[4] == 2 && v.back() == 9, description);
  const VectorType empty;
  const VectorType emptyCopy(empty);  // copy of no element (from a null pointer for vector)
  Check(emptyCopy.empty(), description);
  // two values of the same type which is not an iterator are a count and a value, like for std::vector
  const VectorType counted(kTwo, kFortyTwo);
  Check(counted.size() == 2U && counted[0] == 42 && counted[1] == 42, description);
}

/// Before C++20, the emulation of std::construct_at must not take an rvalue of the element type followed by other
/// arguments for a move (std::string(std::string&&, pos) constructs the substring from 'pos').
template <class VectorType>
void CheckEmplaceRvalueWithMoreArguments(const char* description) {
  VectorType v;
  v.emplace_back(std::string("hello"), std::string::size_type{1});
  v.emplace(v.begin(), std::string("world"), std::string::size_type{2});
  v.emplace(v.begin() + 1, std::string("amc"), std::string::size_type{1}, std::string::size_type{1});
  Check(v.size() == 3U && v[0] == "rld" && v[1] == "m" && v[2] == "ello", description);
}

/// Forward iterator generating consecutive integers: its operator* returns a prvalue, like the const_iterator of
/// std::vector<bool> (whose iterator returns a proxy)
struct CountingIterator {
  using iterator_category = std::forward_iterator_tag;
  using value_type = int;
  using difference_type = std::ptrdiff_t;
  using pointer = const int*;
  using reference = int;

  int operator*() const { return _value; }

  CountingIterator& operator++() {
    ++_value;
    return *this;
  }
  CountingIterator operator++(int) {
    CountingIterator ret = *this;
    ++_value;
    return ret;
  }

  bool operator==(CountingIterator other) const { return _value == other._value; }
  bool operator!=(CountingIterator other) const { return _value != other._value; }

  int _value;
};

/// Before C++17, the copies of a range of trivially copyable elements must not take the address of the elements when
/// the iterators return them by value or by proxy: all the operations taking a range compile and copy the values.
template <class VectorType>
void CheckRangeOfValues(const char* description) {
  // more elements than the inline capacities, to reach the heap
  VectorType v(CountingIterator{0}, CountingIterator{6});
  v.insert(v.begin() + 1, CountingIterator{10}, CountingIterator{12});
  v.insert(v.end(), CountingIterator{20}, CountingIterator{21});
  Check(v == VectorType{0, 10, 11, 1, 2, 3, 4, 5, 20}, description);
#ifdef AMC_NONSTD_FEATURES
  v.append(CountingIterator{30}, CountingIterator{32});
  Check(v.size() == 11U && v[9] == 30 && v[10] == 31, description);
#endif
  v.assign(CountingIterator{3}, CountingIterator{5});
  Check(v == VectorType{3, 4}, description);
  v.assign(CountingIterator{1}, CountingIterator{4});  // more elements than the vector, fewer than its capacity
  Check(v == VectorType{1, 2, 3}, description);
  v.shrink_to_fit();
  v.assign(CountingIterator{0}, CountingIterator{7});  // more elements than its capacity
  Check(v.size() == 7U && v.front() == 0 && v.back() == 6, description);

  const std::vector<bool> bits{true, false, true};
  amc::vector<bool> fromProxies(bits.begin(), bits.end());
  amc::SmallVector<bool, 2> fromValues(bits.cbegin(), bits.cend());
  fromValues.insert(fromValues.begin(), bits.cbegin(), bits.cend());
  Check(fromProxies.size() == 3U && std::equal(bits.begin(), bits.end(), fromProxies.begin()), description);
  Check(fromValues.size() == 6U && std::equal(bits.begin(), bits.end(), fromValues.begin() + 3), description);
}

bool GreaterInt(int lhs, int rhs) { return lhs > rhs; }

/// Comparator which cannot be derived from: std::is_final is emulated before C++14
struct FinalLess final {
  bool operator()(int lhs, int rhs) const { return lhs < rhs; }
};

/// Compares saturating_add to the clamped sum computed in a wider type, for all the pairs of values of a 8 bits type
template <class T>
void CheckSaturatingAdd(const char* description) {
  const int minValue = std::numeric_limits<T>::min();
  const int maxValue = std::numeric_limits<T>::max();
  bool ok = true;
  for (int x = minValue; x <= maxValue; ++x) {
    for (int y = minValue; y <= maxValue; ++y) {
      const int expected = (std::min)((std::max)(x + y, minValue), maxValue);
      ok = ok && static_cast<int>(amc::saturating_add(static_cast<T>(x), static_cast<T>(y))) == expected;
    }
  }
  Check(ok, description);
}

template <class T, class = void>
struct HasSaturatingAdd : std::false_type {};

template <class T>
struct HasSaturatingAdd<T, decltype(void(amc::saturating_add(T{}, T{})))> : std::true_type {};

constexpr std::uintmax_t kUintMax = std::numeric_limits<std::uintmax_t>::max();
constexpr std::intmax_t kIntMax = std::numeric_limits<std::intmax_t>::max();
constexpr std::intmax_t kIntMin = std::numeric_limits<std::intmax_t>::min();

}  // namespace

// saturating_add is a noexcept constant expression returning a T, even for the types promoted to int by the addition
static_assert(noexcept(amc::saturating_add(1, 2)), "");
static_assert(std::is_same<decltype(amc::saturating_add(std::int8_t{1}, std::int8_t{2})), std::int8_t>::value, "");
static_assert(amc::saturating_add(std::uintmax_t{1}, std::uintmax_t{2}) == 3U, "");
static_assert(amc::saturating_add(kUintMax - 2U, std::uintmax_t{2}) == kUintMax, "");
static_assert(amc::saturating_add(kUintMax, std::uintmax_t{1}) == kUintMax, "");
static_assert(amc::saturating_add(kUintMax, kUintMax) == kUintMax, "");
static_assert(amc::saturating_add(kIntMax, std::intmax_t{1}) == kIntMax, "");
static_assert(amc::saturating_add(kIntMin, std::intmax_t{-1}) == kIntMin, "");
static_assert(amc::saturating_add(kIntMin, kIntMin) == kIntMin, "");
static_assert(amc::saturating_add(kIntMin, kIntMax) == -1, "");

// like the standard function, saturating_add only takes the signed and unsigned integer types
static_assert(HasSaturatingAdd<signed char>::value && HasSaturatingAdd<unsigned char>::value, "");
static_assert(!HasSaturatingAdd<bool>::value && !HasSaturatingAdd<char>::value && !HasSaturatingAdd<wchar_t>::value,
              "");
static_assert(!HasSaturatingAdd<char16_t>::value && !HasSaturatingAdd<char32_t>::value, "");
static_assert(!HasSaturatingAdd<double>::value, "");

static_assert(amc::is_nothrow_swappable<int>::value, "");
static_assert(!amc::is_nothrow_swappable<ThrowingMove>::value, "");
static_assert(!amc::is_nothrow_swappable<NonSwappable>::value, "");

// the free swap of FlatSet has the exception specification of its member swap, and its member types are the ones of
// std::set
static_assert(amc::is_nothrow_swappable<amc::FlatSet<int>>::value, "");
static_assert(
    !amc::is_nothrow_swappable<amc::FlatSet<ThrowingMove, std::less<ThrowingMove>, amc::allocator<ThrowingMove>,
                                            amc::SmallVector<ThrowingMove, 4>>>::value,
    "");
static_assert(std::is_same<amc::FlatSet<int>::reference, int&>::value, "");
static_assert(std::is_same<amc::FlatSet<int>::pointer, int*>::value, "");

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
  CheckConstructionThrows([](ThrowingConstructor* first, int n) { amc::uninitialized_default_construct_n(first, n); },
                          "uninitialized_default_construct_n throwing construction");
  CheckConstructionThrows([](ThrowingConstructor* first, int n) { amc::uninitialized_value_construct_n(first, n); },
                          "uninitialized_value_construct_n throwing construction");
  // from the values of another type: each element is constructed, the already constructed ones destroyed on a throw
  CheckConstructionThrows(
      [](ThrowingConstructor* first, int n) { amc::uninitialized_copy_n(CountingIterator{0}, n, first); },
      "uninitialized_copy_n throwing construction");

  // inline elements smaller than a pointer share its storage
  CheckVector<amc::SmallVector<char, 8>>("SmallVector<char, 8>");
  CheckVector<amc::SmallVector<int, 4>>("SmallVector<int, 4>");
  CheckVector<amc::vector<int>>("vector<int>");
  CheckVector<amc::FixedCapacityVector<int, 16>>("FixedCapacityVector<int, 16>");
  {
    amc::FlatSet<int> s{4, 1, 3, 1};
    s.insert(2);
    Check(s == amc::FlatSet<int>{1, 2, 3, 4}, "FlatSet<int>");
  }

  CheckEmplaceRvalueWithMoreArguments<amc::vector<std::string>>("vector<string> emplace of an rvalue and a position");
  CheckEmplaceRvalueWithMoreArguments<amc::SmallVector<std::string, 2>>(
      "SmallVector<string, 2> emplace of an rvalue and a position");
  CheckEmplaceRvalueWithMoreArguments<amc::FixedCapacityVector<std::string, 4>>(
      "FixedCapacityVector<string, 4> emplace of an rvalue and a position");
  {
    // Before C++17, a static constexpr data member which is odr-used (here, whose address is taken) must be defined
    // outside of its class: this links
    using SmallVectorType = amc::SmallVector<int, 4>;
    const SmallVectorType::size_type* volatile inlineCapacity = &SmallVectorType::kInlineCapacity;
    const std::size_t* volatile maxCopiedSize = &amc::SimpleAllocator::kMaxCopiedSize;
    Check(*inlineCapacity == 4U && *maxCopiedSize == 1024U, "odr-used static constexpr data members");
    const SmallVectorType::size_type count = 10;
    Check(std::min(count, SmallVectorType::kInlineCapacity) == 4U, "kInlineCapacity bound to a reference");
  }
  CheckRangeOfValues<amc::vector<int>>("vector<int> from iterators returning values");
  CheckRangeOfValues<amc::SmallVector<int, 4>>("SmallVector<int, 4> from iterators returning values");
  CheckRangeOfValues<amc::FixedCapacityVector<int, 16>>("FixedCapacityVector<int, 16> from iterators returning values");
  {
    amc::FlatSet<int> s(CountingIterator{2}, CountingIterator{5});
    s.insert(CountingIterator{0}, CountingIterator{4});
    Check(s == amc::FlatSet<int>{0, 1, 2, 3, 4}, "FlatSet<int> from iterators returning values");
  }
  {
    amc::FlatSet<int, bool (*)(int, int)> greater({1, 3, 2}, &GreaterInt);
    amc::FlatSet<int, FinalLess> less{3, 1, 2};
    Check(*greater.begin() == 3 && *less.begin() == 1, "FlatSet of a function pointer or a final comparator");
  }
  static_assert(std::is_trivially_destructible<amc::FixedCapacityVector<int, 0>>::value, "");
  {
    // vectors of standard library types, relocated with memcpy (std::vector: except in the debug modes)
    amc::vector<std::vector<int>> vectors;
    amc::SmallVector<std::tuple<int, std::unique_ptr<int>>, 2> tuples;
    for (int i = 0; i < 20; ++i) {
      vectors.push_back(std::vector<int>(static_cast<std::size_t>(i), i));
      tuples.emplace_back(i, std::unique_ptr<int>(new int(i)));
    }
    vectors.erase(vectors.begin());
    tuples.erase(tuples.begin());
    Check(vectors.size() == 19U && vectors[0] == std::vector<int>(1, 1) && vectors.back() == std::vector<int>(19, 19),
          "vector of std::vector");
    Check(tuples.size() == 19U && std::get<0>(tuples.front()) == 1 && *std::get<1>(tuples.back()) == 19,
          "SmallVector of std::tuple");
  }
  CheckSaturatingAdd<std::uint8_t>("saturating_add of all the uint8_t");
  CheckSaturatingAdd<std::int8_t>("saturating_add of all the int8_t");

  if (gNbFailures != 0) {
    std::fprintf(stderr, "%d failure(s)\n", gNbFailures);
    return 1;
  }
  std::puts("All checks passed");
  return 0;
}
