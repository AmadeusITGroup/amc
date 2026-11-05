// Tests of the amc C++20 module: its public names, used through 'import amc;' only. No amc header is included, except
// config.hpp for the AMC_* macros (a module does not export macros), which declares no name.
#include <gtest/gtest.h>

#include <algorithm>
#include <amc/config.hpp>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <new>
#include <ranges>
#include <set>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

import amc;

// Each exported name, before any other use of the module: Clang makes a name visible once a class declaring it as a
// friend is instantiated (the vectors declare FlatSet for instance), which would hide its missing export from the code
// below.
namespace exported_names {
using amc::allocator;
using amc::BasicAllocatorWrapper;
using amc::BasicSingletonAllocatorAdaptor;
using amc::FixedCapacityVector;
using amc::FlatSet;
using amc::inplace_vector;
using amc::is_trivially_relocatable;
using amc::is_trivially_relocatable_v;
using amc::relocate_at;
using amc::SimpleAllocator;
using amc::SmallSet;
using amc::SmallVector;
using amc::swap;
using amc::uninitialized_relocate;
using amc::uninitialized_relocate_n;
using amc::vector;
using amc::Vector;
using amc::vec::BadAllocGrowingPolicy;
using amc::vec::DynamicGrowingPolicy;
using amc::vec::ExceptionGrowingPolicy;
using amc::vec::UncheckedGrowingPolicy;
}  // namespace exported_names

// Defined in module_test_headers.cpp, which includes the amc headers
amc::SmallVector<std::string, 2> MakeStringsWithHeaders(int n);
std::size_t SizeWithHeaders(const amc::FlatSet<int>& s);

namespace {
/// Copyable, not trivially copyable, and declared trivially relocatable by a specialization of the trait below, like
/// a type which cannot be modified.
class Handle {
 public:
  explicit Handle(int i = 0) : _p(std::make_shared<int>(i)) {}

  bool operator==(const Handle& o) const { return *_p == *o._p; }
  std::strong_ordering operator<=>(const Handle& o) const { return *_p <=> *o._p; }

 private:
  std::shared_ptr<int> _p;
};

/// Move only, declared trivially relocatable by its member type.
struct Message {
  explicit Message(int i = 0) : payload(std::make_unique<int>(i)) {}

  int value() const { return *payload; }

  std::unique_ptr<int> payload;

  using trivially_relocatable = std::true_type;
};

template <class T>
T Make(int i);

template <>
int Make<int>(int i) {
  return i;
}

// Longer than the inline buffer of the strings: each one owns memory
template <>
std::string Make<std::string>(int i) {
  return std::string(32, static_cast<char>('a' + i));
}

template <>
Handle Make<Handle>(int i) {
  return Handle(i);
}

template <class T, class A, class S, class G, S N>
std::uintmax_t InlineCapacity(const amc::Vector<T, A, S, G, N>&) {
  return N;
}
}  // namespace

// The specialization documented for the types which cannot declare their member type
namespace amc {
template <>
struct is_trivially_relocatable<Handle> : std::true_type {};
}  // namespace amc

namespace {
static_assert(amc::is_trivially_relocatable<Message>::value);
static_assert(amc::is_trivially_relocatable_v<Handle>);
static_assert(!amc::is_trivially_relocatable_v<std::string>);
// Specializations of the module for std types, and for its containers
static_assert(amc::is_trivially_relocatable_v<std::pair<Message, Handle>>);
static_assert(!amc::is_trivially_relocatable_v<std::pair<Message, std::string>>);
static_assert(amc::is_trivially_relocatable_v<std::allocator<std::string>>);
static_assert(amc::is_trivially_relocatable_v<amc::vector<std::string>>);
static_assert(amc::is_trivially_relocatable_v<amc::SmallVector<Handle, 4>>);
static_assert(!amc::is_trivially_relocatable_v<amc::SmallVector<std::string, 4>>);
static_assert(amc::is_trivially_relocatable_v<amc::FixedCapacityVector<Message, 4>>);
static_assert(amc::is_trivially_relocatable_v<amc::FlatSet<int>>);
static_assert(
    amc::is_trivially_relocatable_v<amc::SmallSet<int, 4, std::less<int>, amc::allocator<int>, amc::FlatSet<int>>>);
static_assert(std::is_trivially_destructible_v<amc::FixedCapacityVector<int, 4>>);

static_assert(std::is_same_v<amc::vector<int>,
                             amc::Vector<int, amc::allocator<int>, std::uint32_t, amc::vec::DynamicGrowingPolicy, 0>>);
static_assert(std::is_same_v<amc::inplace_vector<int, 4>,
                             amc::FixedCapacityVector<int, 4, amc::vec::BadAllocGrowingPolicy, std::size_t>>);
static_assert(std::ranges::contiguous_range<amc::SmallVector<std::string, 4>>);
static_assert(std::ranges::random_access_range<amc::FlatSet<int>>);

template <class T>
class ModuleVectorTest : public ::testing::Test {};

// Each flavor with elements of each relocation path (trivially copyable, trivially relocatable, neither)
using VectorTypes =
    ::testing::Types<amc::vector<int>, amc::vector<std::string>, amc::vector<Handle>, amc::SmallVector<int, 2>,
                     amc::SmallVector<std::string, 2>, amc::SmallVector<Handle, 2>,
                     amc::FixedCapacityVector<std::string, 16>, amc::inplace_vector<Handle, 16>>;
TYPED_TEST_SUITE(ModuleVectorTest, VectorTypes, );

TYPED_TEST(ModuleVectorTest, StandardInterface) {
  using VectorType = TypeParam;
  using T = typename VectorType::value_type;

  VectorType v{Make<T>(1), Make<T>(2), Make<T>(3)};
  v.push_back(Make<T>(4));
  v.emplace_back(Make<T>(5));
  v.insert(v.begin(), Make<T>(0));
  v.push_back(v.front());
  v.erase(v.end() - 1);
  ASSERT_EQ(v.size(), 6U);
  for (int i = 0; i < 6; ++i) {
    EXPECT_EQ(v[static_cast<typename VectorType::size_type>(i)], Make<T>(i));
  }
  EXPECT_EQ(v.at(5), Make<T>(5));
  EXPECT_THROW(static_cast<void>(v.at(6)), std::out_of_range);
  EXPECT_EQ(*v.rbegin(), v.back());
  EXPECT_EQ(v.data(), &v.front());

  VectorType copy(v);
  EXPECT_EQ(copy, v);
  VectorType moved(std::move(copy));
  EXPECT_EQ(moved, v);
  moved.resize(3);
  EXPECT_LT(moved, v);
  EXPECT_TRUE((moved <=> v) < 0);
  moved.assign(2, Make<T>(7));
  EXPECT_EQ(moved, (VectorType{Make<T>(7), Make<T>(7)}));
  moved.insert(moved.begin() + 1, v.begin(), v.begin() + 2);
  EXPECT_EQ(moved, (VectorType{Make<T>(7), Make<T>(0), Make<T>(1), Make<T>(7)}));

  swap(moved, v);  // amc::swap, found by argument dependent lookup
  EXPECT_EQ(moved.size(), 6U);
  std::swap(moved, v);
  EXPECT_EQ(v.size(), 6U);
  v.swap(moved);
  moved.swap(v);
  EXPECT_EQ(v.size(), 6U);

  // Hidden friends, found by argument dependent lookup
  EXPECT_EQ(erase(v, Make<T>(0)), 1U);
  EXPECT_EQ(erase_if(v, [](const T& e) { return e == Make<T>(1) || e == Make<T>(2); }), 2U);
  EXPECT_EQ(v, (VectorType{Make<T>(3), Make<T>(4), Make<T>(5)}));

  v.reserve(10);
  EXPECT_GE(v.capacity(), 10U);
  v.shrink_to_fit();
  v.clear();
  EXPECT_TRUE(v.empty());
}

#ifdef AMC_CXX23
TYPED_TEST(ModuleVectorTest, RangeMembers) {
  using VectorType = TypeParam;
  using T = typename VectorType::value_type;

  const std::vector<T> values{Make<T>(1), Make<T>(2)};
  VectorType v;
  v.append_range(values);
  v.insert_range(v.begin(), values | std::views::reverse);
  EXPECT_EQ(v, (VectorType{Make<T>(2), Make<T>(1), Make<T>(1), Make<T>(2)}));
  v.assign_range(values);
  EXPECT_EQ(v, (VectorType{Make<T>(1), Make<T>(2)}));
}
#endif

#ifdef AMC_NONSTD_FEATURES
TYPED_TEST(ModuleVectorTest, NonStandardExtensions) {
  using VectorType = TypeParam;
  using T = typename VectorType::value_type;

  VectorType v{Make<T>(1)};
  v.append({Make<T>(2), Make<T>(3)});
  EXPECT_EQ(v.pop_back_val(), Make<T>(3));

  amc::SmallVector<T, 4> other{Make<T>(4)};
  v.swap2(other);
  EXPECT_EQ(v, VectorType{Make<T>(4)});
  EXPECT_EQ(other.size(), 2U);
}
#endif

TEST(ModuleVector, MoveOnlyElements) {
  amc::vector<Message> v;
  for (int i = 0; i < 10; ++i) {
    v.emplace_back(i);
  }
  v.insert(v.begin(), Message(-1));
  v.erase(v.begin() + 1, v.begin() + 3);
  ASSERT_EQ(v.size(), 9U);
  EXPECT_EQ(v.front().value(), -1);
  EXPECT_EQ(v.back().value(), 9);

  amc::SmallVector<Message, 4> small(std::move(v));  // takes the storage of the amc::vector
  EXPECT_EQ(small.size(), 9U);
  EXPECT_EQ(small[1].value(), 2);
}

TEST(ModuleVector, VectorClassTemplate) {
  EXPECT_EQ(InlineCapacity(amc::vector<int>()), 0U);
  EXPECT_EQ(InlineCapacity(amc::SmallVector<int, 5>()), 5U);
  EXPECT_EQ(InlineCapacity(amc::FixedCapacityVector<std::string, 7>()), 7U);
}

TEST(ModuleVector, GrowingPolicies) {
  amc::FixedCapacityVector<int, 2> exceptionVector{1, 2};
  EXPECT_THROW(exceptionVector.push_back(3), std::out_of_range);

  amc::FixedCapacityVector<int, 2, amc::vec::ExceptionGrowingPolicy> explicitExceptionVector(2);
  EXPECT_THROW(explicitExceptionVector.emplace_back(3), std::out_of_range);

  amc::FixedCapacityVector<int, 2, amc::vec::BadAllocGrowingPolicy> badAllocVector{1, 2};
  EXPECT_THROW(badAllocVector.push_back(3), std::bad_alloc);

  amc::FixedCapacityVector<int, 2, amc::vec::UncheckedGrowingPolicy> uncheckedVector;
  uncheckedVector.push_back(1);
  EXPECT_EQ(uncheckedVector.size(), 1U);

  amc::inplace_vector<std::string, 1> inplaceVector;
  EXPECT_NE(inplaceVector.try_push_back(Make<std::string>(1)), nullptr);
  EXPECT_EQ(inplaceVector.try_push_back(Make<std::string>(2)), nullptr);
  EXPECT_THROW(inplaceVector.push_back(Make<std::string>(2)), std::bad_alloc);
  EXPECT_EQ(inplaceVector.size(), 1U);
}

template <class T>
class ModuleSetTest : public ::testing::Test {};

using SetTypes = ::testing::Types<amc::FlatSet<int>, amc::FlatSet<std::string>,
                                  amc::FlatSet<int, std::greater<int>, amc::allocator<int>, amc::SmallVector<int, 4>>,
                                  amc::SmallSet<int, 4>, amc::SmallSet<std::string, 4>,
                                  amc::SmallSet<int, 4, std::less<int>, amc::allocator<int>, amc::FlatSet<int>>>;
TYPED_TEST_SUITE(ModuleSetTest, SetTypes, );

TYPED_TEST(ModuleSetTest, StandardInterface) {
  using SetType = TypeParam;
  using T = typename SetType::value_type;
  using RefSet = std::set<T, typename SetType::key_compare>;

  // A SmallSet of 4 elements becomes large
  SetType s;
  for (int i : {5, 1, 4, 1, 3, 9, 2, 6}) {
    s.insert(Make<T>(i));
  }
  s.emplace(Make<T>(8));
  ASSERT_EQ(s.size(), 8U);
  EXPECT_TRUE(s.contains(Make<T>(4)));
  EXPECT_EQ(s.count(Make<T>(7)), 0U);
  EXPECT_NE(s.find(Make<T>(9)), s.end());
  EXPECT_EQ(s.erase(Make<T>(1)), 1U);
  s.erase(s.find(Make<T>(2)));

  auto node = s.extract(Make<T>(3));
  ASSERT_FALSE(node.empty());
  SetType other;
  EXPECT_TRUE(other.insert(std::move(node)).inserted);
  other.insert(Make<T>(5));
  s.merge(other);
  EXPECT_EQ(other, SetType{Make<T>(5)});
  EXPECT_EQ(RefSet(s.begin(), s.end()),
            (RefSet{Make<T>(3), Make<T>(4), Make<T>(5), Make<T>(6), Make<T>(8), Make<T>(9)}));

  SetType copy(s);
  EXPECT_EQ(copy, s);
  EXPECT_EQ(erase_if(copy, [](const T& e) { return e == Make<T>(4) || e == Make<T>(8); }), 2U);  // hidden friend
  EXPECT_NE(copy, s);
  EXPECT_TRUE((copy <=> s) != 0);
  EXPECT_EQ(copy < s, std::lexicographical_compare(copy.begin(), copy.end(), s.begin(), s.end()));

  swap(copy, other);  // amc::swap, found by argument dependent lookup
  EXPECT_EQ(copy, SetType{Make<T>(5)});
  std::swap(copy, other);
  copy.swap(other);
  EXPECT_EQ(other.size(), 4U);
  copy.clear();
  EXPECT_TRUE(copy.empty());
}

TEST(ModuleSet, NodeHandlesBetweenSets) {
  // The node type only depends on the value and allocator types
  amc::FlatSet<int> flatSet{1, 2, 3};
  amc::SmallSet<int, 4, std::greater<int>, amc::allocator<int>, amc::FlatSet<int, std::greater<int>>> smallSet;
  smallSet.insert(flatSet.extract(2));
  smallSet.insert(flatSet.extract(flatSet.begin()));
  EXPECT_EQ(flatSet, amc::FlatSet<int>{3});
  EXPECT_EQ(std::set<int>(smallSet.begin(), smallSet.end()), (std::set<int>{1, 2}));
}

#ifdef AMC_CXX23
TEST(ModuleSet, RangeMembers) {
  amc::FlatSet<int> s;
  s.insert_range(std::vector<int>{3, 1, 2, 3});
  EXPECT_EQ(s, (amc::FlatSet<int>{1, 2, 3}));
}
#endif

#ifdef AMC_NONSTD_FEATURES
TEST(ModuleSet, NonStandardExtensions) {
  amc::FlatSet<int> s(amc::vector<int>{3, 1, 2, 3});
  ASSERT_EQ(s.size(), 3U);
  EXPECT_EQ(s[0], 1);
  EXPECT_EQ(s.at(2), 3);
  EXPECT_EQ(s.data()[1], 2);
  const amc::vector<int> stolen = s.steal_vector();
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(stolen, (amc::vector<int>{1, 2, 3}));
}
#endif

/// Basic allocator counting its allocations.
struct CountingBasicAllocator {
  static inline int nbAllocations = 0;

  void* allocate(std::size_t n) {
    ++nbAllocations;
    return amc::SimpleAllocator().allocate(n);
  }
  void* reallocate(void* p, std::size_t oldSz, std::size_t newSz) {
    return amc::SimpleAllocator().reallocate(p, oldSz, newSz);
  }
  void deallocate(void* p, std::size_t n) { amc::SimpleAllocator().deallocate(p, n); }
};

/// Basic allocator accessed through a singleton.
struct SingletonBasicAllocator : CountingBasicAllocator {
  static SingletonBasicAllocator& instance() {
    static SingletonBasicAllocator singleton;
    return singleton;
  }
};

TEST(ModuleAllocator, Allocators) {
  EXPECT_EQ(amc::allocator<int>(), amc::allocator<int>());

  CountingBasicAllocator::nbAllocations = 0;
  amc::vector<std::string, amc::BasicAllocatorWrapper<std::string, CountingBasicAllocator>> v;
  v.push_back(Make<std::string>(1));
  EXPECT_EQ(CountingBasicAllocator::nbAllocations, 1);

  using SingletonAllocator =
      amc::BasicAllocatorWrapper<int, amc::BasicSingletonAllocatorAdaptor<SingletonBasicAllocator>>;
  amc::SmallVector<int, 2, SingletonAllocator> small{1, 2, 3};
  EXPECT_EQ(CountingBasicAllocator::nbAllocations, 2);
  EXPECT_EQ(small.back(), 3);
}

TEST(ModuleHeaders, SameEntitiesAsTheHeaders) {
  amc::SmallVector<std::string, 2> v = MakeStringsWithHeaders(3);
  ASSERT_EQ(v.size(), 3U);
  EXPECT_EQ(v[2], Make<std::string>(2));
  v.push_back(Make<std::string>(3));
  EXPECT_EQ(v.back(), Make<std::string>(3));
  EXPECT_EQ(SizeWithHeaders(amc::FlatSet<int>{3, 1, 2, 1}), 3U);
}

TEST(ModuleRelocation, RelocationHelpers) {
  std::allocator<std::string> alloc;
  std::string* first = alloc.allocate(3);
  std::string* dest = alloc.allocate(3);
  for (int i = 0; i < 3; ++i) {
    std::construct_at(first + i, Make<std::string>(i));
  }

  // The source elements are destroyed
  EXPECT_EQ(amc::uninitialized_relocate_n(first, 2, dest).second, dest + 2);
  EXPECT_EQ(amc::uninitialized_relocate(first + 2, first + 3, dest + 2), dest + 3);
  EXPECT_EQ(dest[0], Make<std::string>(0));
  EXPECT_EQ(dest[2], Make<std::string>(2));
  amc::relocate_at(dest + 1, first);
  EXPECT_EQ(*first, Make<std::string>(1));

  std::destroy_at(first);
  std::destroy_at(dest);
  std::destroy_at(dest + 2);
  alloc.deallocate(dest, 3);
  alloc.deallocate(first, 3);
}
}  // namespace
