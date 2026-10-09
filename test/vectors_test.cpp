#include <gtest/gtest.h>

#include <algorithm>
#include <amc/fixedcapacityvector.hpp>
#include <amc/smallvector.hpp>
#include <amc/vector.hpp>
#include <array>
#include <initializer_list>
#include <iterator>
#include <list>
#include <memory>
#include <numeric>
#include <sstream>
#include <string>
#include <vector>

#ifdef AMC_CXX23
#include <ranges>
#endif

#include "testhelpers.hpp"
#include "testmallocfailure.hpp"
#include "testtypes.hpp"

namespace amc {

TypeStats TypeStats::_stats;

template <typename T>
class VectorTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

// Each type exercises a distinct combination of what the implementation dispatches on, as every type multiplies the
// compilation time of the typed tests:
//  - the vector flavor: FixedCapacityVector, SmallVector with inline elements, vector (no inline element)
//  - the element type: trivially copyable (and trivially default constructible or not), trivially relocatable, non
//    trivially relocatable, unaligned, over-aligned
//  - for growable vectors, the allocator: with 'reallocate' (amc::allocator, default) or without (std::allocator)
//  - small and signed size types
typedef ::testing::Types<
    FixedCapacityVector<char, 23>, FixedCapacityVector<TriviallyCopyableType, 18>,
    FixedCapacityVector<ComplexNonTriviallyRelocatableType, 17>,
    FixedCapacityVector<ComplexTriviallyRelocatableType, 29>,

    SmallVector<char, 5>, SmallVector<uint32_t, 4, std::allocator<uint32_t>, int32_t>,
    SmallVector<TriviallyCopyableType, 8>, SmallVector<ComplexNonTriviallyRelocatableType, 6>,
    SmallVector<ComplexTriviallyRelocatableType, 8>,
    SmallVector<NonTriviallyRelocatableType, 1, std::allocator<NonTriviallyRelocatableType>, int16_t>,
    SmallVector<UnalignedToPtr<3>, 4>, SmallVector<OverAlignedType, 3>,

    vector<int32_t, std::allocator<int32_t>, uint64_t>, vector<uint32_t, std::allocator<uint32_t>, signed char>,
    vector<TriviallyCopyableType>, vector<ComplexNonTriviallyRelocatableType>, vector<ComplexTriviallyRelocatableType>,
    vector<ComplexTriviallyRelocatableType, std::allocator<ComplexTriviallyRelocatableType>>, vector<OverAlignedType>>
    MyTypes;
TYPED_TEST_SUITE(VectorTest, MyTypes, );

template <class VecType>
void ChecksAgainstTab(const VecType& cont, std::initializer_list<typename VecType::value_type> expectedValues) {
  using ValueType = typename VecType::value_type;
  using SzType = typename VecType::size_type;
  using ConstIt = typename VecType::const_iterator;
  using RefVec = std::vector<ValueType>;

  EXPECT_FALSE(cont.empty());
  EXPECT_EQ(static_cast<uint32_t>(cont.size()), expectedValues.size());
  auto it = expectedValues.begin();
  EXPECT_EQ(cont.front(), *it);
  for (const ValueType& v : cont) {
    EXPECT_EQ(*it, v);
    ++it;
  }
  EXPECT_EQ(cont.back(), *(it - 1));
  for (int v = 0; v < 9; ++v) {
    VecType cpy = cont;
    EXPECT_EQ(cpy, cont);
    static uint32_t gTabPos;
    if (gTabPos >= expectedValues.size()) {
      gTabPos = 0;
    }
    ConstIt randIt = cpy.begin() + gTabPos;
    const ValueType& randValue = expectedValues.begin()[gTabPos++];
    cpy.erase(randIt);
    VecType cpy2 = cont;
    RefVec refTab(cont.begin(), cont.end());
    EXPECT_EQ(cpy2, VecType(refTab.begin(), refTab.end()));
    cpy2.swap(cpy);
    EXPECT_LT(cpy2.size(), cont.size());
    cpy2.swap(cpy);
    EXPECT_EQ(cpy2, cont);
    for (int i = std::max(0, v - 2);
         i < v + 3 && i <= static_cast<int>(cpy2.size()) && cpy2.size() + v <= cpy2.max_size(); ++i) {
      refTab.insert(refTab.begin() + i, v, static_cast<ValueType>(randValue + i));
      cpy2.insert(cpy2.begin() + i, static_cast<SzType>(v), static_cast<ValueType>(randValue + i));
      EXPECT_EQ(cpy2, VecType(refTab.begin(), refTab.end()));
    }

    EXPECT_LT(cpy.size(), cont.size());
    EXPECT_NE(cpy, cont);
    EXPECT_NE(std::find(cont.begin(), cont.end(), randValue), cont.end());
    if (gTabPos != expectedValues.size()) {
      EXPECT_EQ(cpy[static_cast<typename VecType::size_type>(gTabPos - 1)],
                cont[static_cast<typename VecType::size_type>(gTabPos)]);
    }
    cpy.emplace(cpy.begin() + cpy.size() / 2, static_cast<ValueType>(v / 2));
    cpy2 = cpy;
    EXPECT_EQ(cpy2, cpy);
    cpy.shrink_to_fit();
    EXPECT_EQ(cpy2, cpy);
    cpy.clear();
    EXPECT_TRUE(cpy.empty());
    cpy.resize(static_cast<SzType>(v + 1), ValueType(42));
    refTab.assign(v + 1, ValueType(42));
    EXPECT_EQ(cpy, VecType(refTab.begin(), refTab.end()));

#ifdef AMC_NONSTD_FEATURES
    ValueType lastEl = cpy.pop_back_val();
#else
    ValueType lastEl = std::move(cpy.back());
    cpy.pop_back();
#endif
    EXPECT_EQ(lastEl, ValueType(42));
    EXPECT_EQ(static_cast<int>(cpy.size()), v);
    EXPECT_TRUE(cpy.empty() || cpy.back() == ValueType(42));
    for (int copyType = 0; copyType < 2; ++copyType) {
      // Test Copy constructor
      cpy2.assign(static_cast<SzType>(v), static_cast<ValueType>(9 + v));
      VecType newCont = cpy2;
      refTab.assign(v, static_cast<ValueType>(9 + v));
      EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      for (int i = std::max(0, v - 1); i < v + 7; ++i) {
        // Test Copy assignment
        if (copyType == 0) {
          VecType c;
          newCont.swap(c);
        }
        VecType cpy3(static_cast<SzType>(i), static_cast<ValueType>(42 + v));
        newCont = cpy3;
        refTab.assign(i, static_cast<ValueType>(42 + v));
        EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      }
    }

    for (int moveType = 0; moveType < 2; ++moveType) {
      // Test Move constructor
      cpy2.assign(static_cast<SzType>(v), static_cast<ValueType>(9 + v));
      VecType newCont = std::move(cpy2);
      refTab.assign(v, static_cast<ValueType>(9 + v));
      EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      for (int i = std::max(0, v - 1); i < v + 7; ++i) {
        // Test Move assignment
        if (moveType == 0) {
          VecType c;
          newCont.swap(c);
        }
        newCont = VecType(static_cast<SzType>(i), static_cast<ValueType>(42 + v));
        refTab.assign(i, static_cast<ValueType>(42 + v));
        EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      }
    }
  }
}

TYPED_TEST(VectorTest, Main) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  VectorType s;
  EXPECT_TRUE(s.empty());
  s.push_back(Type(8));
  s.emplace_back(Type(15));
  s.insert(s.begin(), Type(3));
  s.insert(s.end(), Type(8));
  EXPECT_EQ(static_cast<unsigned int>(s.size()), 4U);
  EXPECT_EQ(s[1], Type(8));
  EXPECT_EQ(s.front(), Type(3));
  EXPECT_EQ(s.back(), Type(8));

  EXPECT_EQ(s, VectorType(s));

  {
    const Type kNewEls[] = {18, 4, 3, 6, 4};
    s.insert(s.end(), kNewEls, kNewEls + 5);
    ChecksAgainstTab(s, {3, 8, 15, 8, 18, 4, 3, 6, 4});

    s.erase(s.begin());

    ChecksAgainstTab(VectorType(kNewEls, kNewEls + 5), {18, 4, 3, 6, 4});
  }
}

// Single pass input iterators (here, reading from a stream) can be traversed only once.
// Elements must be aligned according to their type, even when its alignment is larger than the one of malloc.
TYPED_TEST(VectorTest, ElementsAlignment) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  const auto isAligned = [](const VectorType& v) {
    return reinterpret_cast<std::uintptr_t>(v.data()) % alignof(Type) == 0U;
  };
  VectorType v;
  for (int i = 0; i < 16; ++i) {  // several reallocations
    v.push_back(Type(i));
    EXPECT_TRUE(isAligned(v));
  }
  v.erase(v.begin() + 2, v.end());
  v.shrink_to_fit();
  EXPECT_TRUE(isAligned(v));
  v.reserve(13U);
  EXPECT_TRUE(isAligned(v));
  VectorType copy(v);
  EXPECT_TRUE(isAligned(copy));
}

TYPED_TEST(VectorTest, InputIterators) {
  using VectorType = TypeParam;
  using InputIt = std::istream_iterator<int>;

  std::istringstream constructSs("1 2 3 4 5");
  InputIt first(constructSs);
  InputIt last;
  VectorType v(first, last);
  EXPECT_EQ(v, VectorType({1, 2, 3, 4, 5}));

  std::istringstream assignLessSs("6 7");
  v.assign(InputIt(assignLessSs), InputIt());
  EXPECT_EQ(v, VectorType({6, 7}));

  std::istringstream assignMoreSs("8 9 10 11");
  v.assign(InputIt(assignMoreSs), InputIt());
  EXPECT_EQ(v, VectorType({8, 9, 10, 11}));

  std::istringstream insertMiddleSs("12 13");
  typename VectorType::iterator it = v.insert(v.begin() + 1, InputIt(insertMiddleSs), InputIt());
  EXPECT_EQ(it, v.begin() + 1);
  EXPECT_EQ(v, VectorType({8, 12, 13, 9, 10, 11}));

  std::istringstream insertEndSs("14");
  it = v.insert(v.end(), InputIt(insertEndSs), InputIt());
  EXPECT_EQ(it, v.begin() + 6);
  EXPECT_EQ(v, VectorType({8, 12, 13, 9, 10, 11, 14}));

  std::istringstream insertEmptySs("");
  it = v.insert(v.begin(), InputIt(insertEmptySs), InputIt());
  EXPECT_EQ(it, v.begin());
  EXPECT_EQ(v, VectorType({8, 12, 13, 9, 10, 11, 14}));

#ifdef AMC_NONSTD_FEATURES
  std::istringstream appendSs("15 16");
  v.append(InputIt(appendSs), InputIt());
  EXPECT_EQ(v, VectorType({8, 12, 13, 9, 10, 11, 14, 15, 16}));
#endif
}

// Number of elements putting all tested SmallVectors in their dynamic storage state, while fitting in the capacity of
// all tested FixedCapacityVectors
constexpr int kNbElemsLargeState = 12;

template <class VectorType>
VectorType CreateLargeVector() {
  using Type = typename VectorType::value_type;
  VectorType v;
  for (int i = 1; i <= kNbElemsLargeState; ++i) {
    v.push_back(Type(i));
  }
  return v;
}

TYPED_TEST(VectorTest, Shrink) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  VectorType v = CreateLargeVector<VectorType>();
  v.resize(3, Type(42));
  EXPECT_EQ(v, VectorType({1, 2, 3}));

  // An empty vector releases its dynamic storage, if any
  v.clear();
  v.shrink_to_fit();
  EXPECT_EQ(v.capacity(), VectorType().capacity());
  v.push_back(Type(4));
  EXPECT_EQ(v, VectorType{Type(4)});
}

TYPED_TEST(VectorTest, SelfAssignment) {
  using VectorType = TypeParam;
  VectorType v{1, 2, 3};
  VectorType& self = v;  // assignment through a reference, avoiding self assignment warnings
  v = self;
  EXPECT_EQ(v, VectorType({1, 2, 3}));
  v = std::move(self);
  EXPECT_EQ(v, VectorType({1, 2, 3}));
}

TYPED_TEST(VectorTest, InsertEmptyRange) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  VectorType v{1, 2, 3};
  const Type kElems[] = {4};
  for (int pos = 0; pos <= 3; ++pos) {
    typename VectorType::iterator it = v.insert(v.begin() + pos, std::begin(kElems), std::begin(kElems));
    EXPECT_EQ(it, v.begin() + pos);
    EXPECT_EQ(v, VectorType({1, 2, 3}));
  }
}

// Swaps between all storage states of SmallVectors (inline and dynamic), in both directions
TYPED_TEST(VectorTest, SwapSmallAndLarge) {
  using VectorType = TypeParam;
  const VectorType kSmall{1, 2};
  const VectorType kLarge = CreateLargeVector<VectorType>();
  VectorType v1 = kSmall;
  VectorType v2 = kLarge;
  v1.swap(v2);
  EXPECT_EQ(v1, kLarge);
  EXPECT_EQ(v2, kSmall);
  v1.swap(v2);
  EXPECT_EQ(v1, kSmall);
  EXPECT_EQ(v2, kLarge);
}

TEST(VectorTest, Operators) {
  using VectorType = vector<int>;

  VectorType v1{1, 2, 3};
  VectorType v2{1, 4, 3};
  VectorType v3{1, 4, 3, -4};

  VectorType v4{1, 2, 3};

  EXPECT_EQ(v1, v4);
  EXPECT_NE(v1, v2);

  EXPECT_LT(v1, v2);
  EXPECT_LE(v1, v2);
  EXPECT_LE(v1, v1);
  EXPECT_GT(v3, v1);
  EXPECT_GE(v3, v1);
  EXPECT_GE(v3, v3);

#ifdef AMC_CXX20
  EXPECT_EQ(v1 <=> v1, std::strong_ordering::equal);
  EXPECT_EQ(v1 <=> v2, std::strong_ordering::less);
  EXPECT_EQ(v1 <=> v3, std::strong_ordering::less);
  EXPECT_EQ(v3 <=> v1, std::strong_ordering::greater);
#endif
}

template <typename T>
class VectorLessComparableTest : public ::testing::Test {};

using VectorsOfLessComparableTypes = ::testing::Types<vector<LessComparableType>, SmallVector<LessComparableType, 2>,
                                                      FixedCapacityVector<LessComparableType, 4>>;
TYPED_TEST_SUITE(VectorLessComparableTest, VectorsOfLessComparableTypes, );

// Vectors of elements only providing operator< are comparable (from C++20, with an ordering synthesized from it)
TYPED_TEST(VectorLessComparableTest, ComparisonOperators) {
  using VectorType = TypeParam;
  const VectorType v1{1, 2, 3};
  const VectorType v2{1, 3};
  const VectorType v3{1, 2};
  EXPECT_EQ(v1, VectorType({1, 2, 3}));
  EXPECT_NE(v1, v2);
  EXPECT_LT(v1, v2);
  EXPECT_LE(v3, v1);
  EXPECT_GT(v2, v3);
  EXPECT_GE(v1, v1);
#ifdef AMC_CXX20
  static_assert(std::is_same_v<decltype(v1 <=> v2), std::weak_ordering>, "ordering synthesized from operator<");
  EXPECT_EQ(v1 <=> v2, std::weak_ordering::less);
  EXPECT_EQ(v1 <=> v1, std::weak_ordering::equivalent);
  EXPECT_EQ(v2 <=> v3, std::weak_ordering::greater);
#endif
}

#ifdef AMC_CXX20
TEST(VectorTest, Erase) {
  using VectorType = vector<int>;

  VectorType v1{1, 2, 3};
  EXPECT_EQ(erase(v1, 2), 1U);
  EXPECT_EQ(v1, VectorType({1, 3}));

  VectorType v2{1, 4, 3};
  EXPECT_EQ(erase(v2, 2), 0U);
  EXPECT_EQ(v2, VectorType({1, 4, 3}));

  VectorType v3{1, 4, 3, -4};
  EXPECT_EQ(erase(v3, 1), 1U);
  EXPECT_EQ(v3, VectorType({4, 3, -4}));

  VectorType v4{1, 2, 3, 1, 2, 3, 4};
  EXPECT_EQ(erase(v4, 3), 2U);
  EXPECT_EQ(v4, VectorType({1, 2, 1, 2, 4}));
}

TEST(VectorTest, EraseIf) {
  using VectorType = vector<int>;

  VectorType v1{1, 2, 3};
  EXPECT_EQ(erase_if(v1, [](int v) { return v > 2; }), 1U);
  EXPECT_EQ(v1, VectorType({1, 2}));

  VectorType v2{1, 4, 3};
  EXPECT_EQ(erase_if(v2, [](int v) { return (v % 2) == 0; }), 1U);
  EXPECT_EQ(v2, VectorType({1, 3}));

  VectorType v3{1, 4, 3, -4};
  EXPECT_EQ(erase_if(v3, [](int v) { return v < 2; }), 2U);
  EXPECT_EQ(v3, VectorType({4, 3}));

  VectorType v4{1, 2, 3, 1, 2, 3, 4};
  EXPECT_EQ(erase_if(v4, [](int v) { return v > 2; }), 3U);
  EXPECT_EQ(v4, VectorType({1, 2, 1, 2}));
}
#endif

// Erasing an empty range must not modify any element, even for types not supporting self move assignment.
template <class VectorType>
void CheckEraseEmptyRange() {
  VectorType v{1, 2, 3};
  for (int pos = 0; pos <= 3; ++pos) {
    typename VectorType::iterator it = v.erase(v.begin() + pos, v.begin() + pos);
    EXPECT_EQ(it, v.begin() + pos);
    EXPECT_EQ(v, VectorType({1, 2, 3}));
  }
}

TEST(VectorTest, EraseEmptyRange) {
  CheckEraseEmptyRange<vector<SelfMoveUnsafeType>>();
  CheckEraseEmptyRange<SmallVector<SelfMoveUnsafeType, 4>>();
  CheckEraseEmptyRange<FixedCapacityVector<SelfMoveUnsafeType, 4>>();
  CheckEraseEmptyRange<vector<ComplexTriviallyRelocatableType>>();
}

template <typename T>
class VectorRefTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

// Same selection principle as 'MyTypes', for larger vectors (no value construction here, hence no need to distinguish
// trivially default constructible types)
typedef ::testing::Types<
    FixedCapacityVector<int32_t, 1000>, FixedCapacityVector<ComplexNonTriviallyRelocatableType, 1000>,
    FixedCapacityVector<ComplexTriviallyRelocatableType, 1000>,

    SmallVector<int32_t, 80>, SmallVector<int32_t, 100, std::allocator<int32_t>>,
    SmallVector<ComplexNonTriviallyRelocatableType, 120>, SmallVector<ComplexTriviallyRelocatableType, 130>,

    vector<int32_t>, vector<ComplexNonTriviallyRelocatableType>,
    vector<ComplexTriviallyRelocatableType, std::allocator<ComplexTriviallyRelocatableType>>,
    vector<NonTriviallyRelocatableType, std::allocator<NonTriviallyRelocatableType>, uint64_t>>
    MyTypesForRef;
TYPED_TEST_SUITE(VectorRefTest, MyTypesForRef, );

template <class T, class A, class S, class G, S N>
inline bool operator==(const Vector<T, A, S, G, N>& lhs, const typename std::vector<T>& rhs) {
  return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

TYPED_TEST(VectorRefTest, CompareToRefVector) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  using SzType = typename VectorType::size_type;
  using RefVecType = typename std::vector<Type>;

  VectorType v(100U, 2);
  RefVecType r(100U, 2);
  EXPECT_EQ(v, r);

  std::array<Type, 200U> kTab;
  std::iota(kTab.begin(), kTab.end(), 0);
  for (int i = 10, s = 19; i < 100; i += 7, s += 3) {
    v.insert(std::min(v.begin() + i, v.end()), static_cast<SzType>(s), static_cast<Type>(s));
    r.insert(std::min(r.begin() + i, r.end()), static_cast<typename RefVecType::size_type>(s), static_cast<Type>(s));
    EXPECT_EQ(v, r);
    v.erase(v.begin(), v.begin() + s);
    r.erase(r.begin(), r.begin() + s);
    EXPECT_EQ(v, r);

    if (i % 4 == 0) {
      v.shrink_to_fit();
      r.shrink_to_fit();
      EXPECT_EQ(v, r);
    }

    v.insert(std::max(v.begin(), v.end() - i), kTab.begin() + s, kTab.end() - s);
    r.insert(std::max(r.begin(), r.end() - i), kTab.begin() + s, kTab.end() - s);
    EXPECT_EQ(v, r);
    v.erase(v.begin() + 3, v.begin() + 3 + (kTab.size() - 2 * s));
    r.erase(r.begin() + 3, r.begin() + 3 + (kTab.size() - 2 * s));
    EXPECT_EQ(v, r);
  }
}

TEST(VectorTest, CustomOperations) {
  constexpr uint32_t kStaticSize = 15;
  using IntVectorType = FixedCapacityVector<int, kStaticSize>;
  using OtherVectorType = FixedCapacityVector<NonTriviallyRelocatableType, 42>;

  static_assert(std::is_trivially_destructible<IntVectorType>::value,
                "FixedCapacityVector of a trivially copyable type should be trivially destructible");
  static_assert(!std::is_trivially_destructible<OtherVectorType>::value,
                "FixedCapacityVector of a non trivially copyable type should not be trivially destructible");
  static_assert(!std::is_trivially_destructible<SmallVector<int, 10>>::value,
                "SmallVector should not be trivially destructible");

  static_assert(is_trivially_relocatable<IntVectorType>::value, "FixedCapacityVector should be trivially relocatable");
  static_assert(is_trivially_relocatable<SmallVector<int, 0>>::value,
                "SmallVector without inline storage should be trivially relocatable");
  static_assert(is_trivially_relocatable<vector<NonTriviallyRelocatableType>>::value,
                "vector should be trivially relocatable");
  static_assert(is_trivially_relocatable<SmallVector<int, 10>>::value,
                "SmallVector with inline storage should be trivially relocatable if T is");

  EXPECT_EQ(IntVectorType().capacity(), kStaticSize);
  EXPECT_EQ(OtherVectorType().max_size(), 42U);
  IntVectorType v;
  v = {3, 3, 3, 3, 3};
  ChecksAgainstTab(v, {3, 3, 3, 3, 3});

  v = IntVectorType({2, 2, 2});
  ChecksAgainstTab(v, {2, 2, 2});

  IntVectorType v1 = v;
  v1[1] = 3;
  EXPECT_LT(v, v1);
  EXPECT_GT(v1, v);
  v1[1] = 2;
  EXPECT_LE(v, v1);
  EXPECT_GE(v1, v);

  EXPECT_THROW(v.assign(kStaticSize + 1, 0), std::out_of_range);

  FixedCapacityVector<int, 10, vec::UncheckedGrowingPolicy> v2(6U);
#ifndef NDEBUG
  EXPECT_DEATH(v2.insert(v2.begin() + 3, 6U, 10), "");
#endif

  IntVectorType throwingV(kStaticSize);
  EXPECT_EQ(std::accumulate(throwingV.begin(), throwingV.end(), 0U), 0U);

  EXPECT_THROW(throwingV.assign(kStaticSize + 1, 0), std::out_of_range);

  EXPECT_EQ(v.insert(v.begin() + 2, {18, 18, 18}), v.begin() + 2);
  {
    ChecksAgainstTab(v, {2, 2, 18, 18, 18, 2});
    const int kTab[] = {2, 2, 18, 17, 18, 2};
    EXPECT_EQ(v.insert(v.begin() + 4, kTab + 1, kTab + 4), v.begin() + 4);
    ChecksAgainstTab(v, {2, 2, 18, 18, 2, 18, 17, 18, 2});
    const int kTab2[] = {2, 18, 18, 2, 18, 17, 18};
    EXPECT_THROW(v.insert(v.begin(), kTab2, kTab2 + 7), std::out_of_range);
    v.erase(v.begin() + 1, v.begin() + 2);
    v.insert(v.begin(), kTab2 + 2, kTab2 + 4);
    ChecksAgainstTab(v, {18, 2, 2, 18, 18, 2, 18, 17, 18, 2});
  }
}

#ifdef AMC_CXX23
TEST(VectorTest, AppendRange) {
  using IntVectorType = SmallVector<int, 4>;
  IntVectorType v{1, 2, 3};
  std::list<int> l{4, 5, 6};
  v.append_range(l);
  EXPECT_EQ(v, IntVectorType({1, 2, 3, 4, 5, 6}));
  v.append_range(std::vector<int>{7, 8, 9, 10, 11});
  EXPECT_EQ(v, IntVectorType({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
}

TEST(VectorTest, InsertRange) {
  using IntVectorType = SmallVector<int, 4>;
  IntVectorType v{1, 2, 3};
  std::list<int> l{4, 5, 6};
  v.insert_range(v.begin() + 1, l);
  EXPECT_EQ(v, IntVectorType({1, 4, 5, 6, 2, 3}));
  v.insert_range(v.end(), std::vector<int>{7, 8, 9, 10, 11});
  EXPECT_EQ(v, IntVectorType({1, 4, 5, 6, 2, 3, 7, 8, 9, 10, 11}));
}

TEST(VectorTest, TryAppendRangeInputIterator) {
  using IntVectorType = FixedCapacityVector<int, 5>;
  IntVectorType v{1, 2, 3};
  std::list<int> rg{4, 5, 6};
  EXPECT_EQ(v.try_append_range(rg), std::next(rg.begin(), 2));
}

TEST(VectorTest, TryAppendRangeRandomAccessIterator) {
  using IntVectorType = FixedCapacityVector<int, 5>;
  IntVectorType v{1, 2, 3};
  std::vector<int> rg{4, 5, 6, 7};
  EXPECT_EQ(v.try_append_range(rg), rg.begin() + 2);
}

TEST(VectorTest, TryAppendRangeInputIteratorConstructElements) {
  using ValueType = ComplexNonTriviallyRelocatableType;
  using VectorType = FixedCapacityVector<ValueType, 3>;
  std::list<ValueType> rg{1, 2, 3, 4};
  VectorType v{0};

  TypeStats& stats = TypeStats::_stats;
  stats = TypeStats();
  stats.start();
  EXPECT_EQ(v.try_append_range(rg), std::next(rg.begin(), 2));
  stats.end();

  EXPECT_EQ(stats._nbCopyAssignments, 0U);
  EXPECT_EQ(stats._nbMoveAssignments, 0U);
  EXPECT_EQ(stats._nbCopyConstructs, 2U);
  EXPECT_EQ(v, VectorType({0, 1, 2}));
}

// try_append_range appends elements up to the capacity, even if the size of the range does not fit in size_type, or if
// the range is not sized, not common or a non borrowed rvalue.
TYPED_TEST(VectorTest, TryAppendRangeUpToCapacity) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  using SizeType = typename VectorType::size_type;
  VectorType v;
  v.push_back(Type(1));
  v.reserve(10U);
  const std::size_t capacity = static_cast<std::size_t>(v.capacity());

  // 256 elements do not fit in an 8 bits size_type
  const std::vector<int> rg(256, 7);
  std::vector<int>::const_iterator it = v.try_append_range(rg);
  EXPECT_EQ(static_cast<std::size_t>(it - rg.begin()), capacity - 1U);
  EXPECT_EQ(static_cast<std::size_t>(v.size()), capacity);
  EXPECT_TRUE(std::all_of(v.begin() + 1, v.end(), [](const Type& e) { return e == Type(7); }));

  // unbounded, hence not sized and not common, random access range
  v.clear();
  auto iotaIt = v.try_append_range(std::views::iota(0));
  EXPECT_EQ(static_cast<std::size_t>(*iotaIt), capacity);
  EXPECT_EQ(static_cast<std::size_t>(v.size()), capacity);
  for (std::size_t i = 0; i < capacity; ++i) {
    EXPECT_EQ(v[static_cast<SizeType>(i)], Type(static_cast<int>(i)));
  }

  // non borrowed rvalue range: returns std::ranges::dangling
  v.clear();
  std::ranges::dangling dangling = v.try_append_range(std::vector<int>{3, 4});
  (void)dangling;
  EXPECT_EQ(v, VectorType({3, 4}));
}

// append_range, assign_range and insert_range accept ranges that are not common (whose end is a sentinel of another
// type than their iterator), forward or single pass input ones.
template <class VectorType>
void CheckRangesMethodsNonCommonRanges() {
  using Type = typename VectorType::value_type;
  const auto toType = std::views::transform([](int i) { return Type(i); });
  auto upTo3 = std::views::iota(1) | std::views::take_while([](int i) { return i <= 3; }) | toType;
  static_assert(std::ranges::forward_range<decltype(upTo3)> && !std::ranges::common_range<decltype(upTo3)>);
  VectorType v;
  v.append_range(upTo3);
  EXPECT_EQ(v, VectorType({1, 2, 3}));
  v.insert_range(v.begin() + 1, upTo3);
  EXPECT_EQ(v, VectorType({1, 1, 2, 3, 2, 3}));
  v.assign_range(upTo3);
  EXPECT_EQ(v, VectorType({1, 2, 3}));

  std::istringstream ss1("4 5");
  v.append_range(std::views::istream<int>(ss1) | toType);
  EXPECT_EQ(v, VectorType({1, 2, 3, 4, 5}));
  std::istringstream ss2("6 7");
  v.insert_range(v.begin() + 1, std::views::istream<int>(ss2) | toType);
  EXPECT_EQ(v, VectorType({1, 6, 7, 2, 3, 4, 5}));
  std::istringstream ss3("8 9");
  v.assign_range(std::views::istream<int>(ss3) | toType);
  EXPECT_EQ(v, VectorType({8, 9}));
}

TEST(VectorTest, RangesMethodsNonCommonRanges) {
  CheckRangesMethodsNonCommonRanges<FixedCapacityVector<int, 10>>();
  CheckRangesMethodsNonCommonRanges<SmallVector<int, 4>>();
  CheckRangesMethodsNonCommonRanges<vector<ComplexNonTriviallyRelocatableType>>();
}

#ifdef __cpp_lib_ranges_as_rvalue
// The elements of a range of rvalues are moved, not copied.
TEST(VectorTest, RangesMethodsMoveRvalues) {
  using VectorType = vector<std::unique_ptr<int>>;
  auto makePtrs = [](int first) {
    std::vector<std::unique_ptr<int>> ptrs;
    ptrs.push_back(std::make_unique<int>(first));
    ptrs.push_back(std::make_unique<int>(first + 1));
    return ptrs;
  };
  VectorType v;
  auto ptrs = makePtrs(1);
  v.append_range(ptrs | std::views::as_rvalue);
  EXPECT_FALSE(ptrs.front());
  ptrs = makePtrs(3);
  v.insert_range(v.begin() + 1, ptrs | std::views::as_rvalue);
  ptrs = makePtrs(5);
  v.assign_range(ptrs | std::views::as_rvalue);
  ASSERT_EQ(v.size(), 2U);
  EXPECT_EQ(*v[0], 5);
  EXPECT_EQ(*v[1], 6);
  EXPECT_FALSE(ptrs.back());
}
#endif

#endif

TEST(VectorTest, TryEmplacePushBack) {
  using VectorType = FixedCapacityVector<Foo, 4>;
  VectorType v;
  for (int i = 0; i < 10; ++i) {
    VectorType::pointer p;
    if (i % 2 == 0) {
      if (i == 0) {
        p = v.unchecked_push_back(i);
      } else {
        p = v.try_emplace_back(i);
      }

    } else {
      p = v.try_push_back(i);
    }
    if (i < 4) {
      EXPECT_NE(p, nullptr);
      EXPECT_EQ(*p, i);
    } else {
      EXPECT_EQ(p, nullptr);
    }
    EXPECT_EQ(v.size(), static_cast<uint32_t>(std::min(i + 1, 4)));
  }
}

TEST(VectorTest, TryUncheckedPushBackConstructElements) {
  using ValueType = ComplexNonTriviallyRelocatableType;
  using VectorType = FixedCapacityVector<ValueType, 4>;
  const ValueType lvalue(42);
  VectorType v;

  TypeStats& stats = TypeStats::_stats;
  stats = TypeStats();
  stats.start();
  EXPECT_NE(v.try_push_back(lvalue), nullptr);
  EXPECT_NE(v.try_push_back(ValueType(43)), nullptr);
  EXPECT_NE(v.unchecked_push_back(lvalue), nullptr);
  EXPECT_NE(v.unchecked_push_back(ValueType(44)), nullptr);
  EXPECT_EQ(v.try_push_back(lvalue), nullptr);
  stats.end();

  EXPECT_EQ(stats._nbCopyAssignments, 0U);
  EXPECT_EQ(stats._nbMoveAssignments, 0U);
  EXPECT_EQ(stats._nbCopyConstructs, 2U);
  EXPECT_EQ(stats._nbMoveConstructs, 2U);
  EXPECT_EQ(v, VectorType({42, 43, 42, 44}));
}

TEST(VectorTest, NonCopyableType) {
  using NonCopyableTypeVectorType = SmallVector<NonCopyableType, 6>;
  NonCopyableTypeVectorType v(6);
  EXPECT_EQ(v.front(), NonCopyableType());
  EXPECT_EQ(v.back(), NonCopyableType());
  v.resize(7);
  EXPECT_EQ(v[6], NonCopyableType());
  v.resize(2);
  EXPECT_EQ(v.size(), 2U);

  v.emplace_back(1);
  EXPECT_EQ(v.back(), NonCopyableType(1));
}

#ifdef AMC_NONSTD_FEATURES
TEST(VectorTest, CustomSwap) {
  using ObjType = NonTriviallyRelocatableType;
  using Bar7Vector = FixedCapacityVector<ObjType, 7>;
  using Bar10Vector = FixedCapacityVector<ObjType, 10>;
  using Bar6Vector = SmallVector<ObjType, 6>;
  using BarVector = vector<ObjType>;
  Bar7Vector bar7(3U);
  Bar10Vector bar10(7U);
  EXPECT_EQ(bar7.capacity(), 7U);
  bar7.swap2(bar10);
  EXPECT_EQ(bar7.size(), 7U);
  bar10.shrink_to_fit();
  EXPECT_EQ(bar10.size(), 3U);
  EXPECT_EQ(bar10.capacity(), 10U);

  Bar6Vector bar6;
  EXPECT_EQ(bar6.capacity(), 6U);
  bar6.swap2(bar7);
  EXPECT_GE(bar6.capacity(), 7U);
  EXPECT_EQ(bar6.size(), 7U);
  EXPECT_TRUE(bar7.empty());
  bar7.swap2(bar6);
  EXPECT_EQ(bar7.size(), 7U);
  EXPECT_TRUE(bar6.empty());
  bar6.shrink_to_fit();
  EXPECT_EQ(bar6.capacity(), 6U);

  BarVector bar(5, 37);
  // to force compilation of all possible template combinations
  bar.swap2(bar6);
  EXPECT_EQ(bar6, Bar6Vector(5, 37));
  EXPECT_TRUE(bar.empty());
  bar6.swap2(bar);
  EXPECT_EQ(bar, BarVector(5, 37));
  EXPECT_TRUE(bar6.empty());

  bar.swap2(bar7);

  bar7.swap2(bar);

  vector<ObjType, std::allocator<ObjType>, int32_t> barvec2(4, 56);

  bar.swap2(barvec2);
  barvec2.swap2(bar);
  bar7.swap2(barvec2);
  barvec2.swap2(bar7);
  bar6.swap2(barvec2);
  barvec2.swap2(bar6);

  SmallVector<ObjType, 6, std::allocator<ObjType>, int16_t> bar62(2, 62);
  bar.swap2(bar62);
  bar62.swap2(bar);
  bar7.swap2(bar62);
  bar62.swap2(bar7);
  bar6.swap2(bar62);
  bar62.swap2(bar6);

  bar.swap2(barvec2);
  barvec2.swap2(bar);
  bar7.swap2(barvec2);
  barvec2.swap2(bar7);
  bar6.swap2(barvec2);
  barvec2.swap2(bar6);
}

// swap2 of vectors with different size types: dynamic storages can be exchanged only if each capacity fits in the size
// type of the other vector. Otherwise, elements are swapped one by one if sizes fit, or swap2 throws.
template <class SmallSizeTypeVec, class LargeSizeTypeVec>
void CheckSwap2DifferentSizeTypes() {
  static_assert(std::is_same<typename SmallSizeTypeVec::size_type, uint8_t>::value, "");
  std::vector<int> values(10);
  std::iota(values.begin(), values.end(), 0);
  {
    // capacity of 'large' does not fit in the size type of 'small'
    SmallSizeTypeVec small;
    LargeSizeTypeVec large(values.begin(), values.end());
    large.reserve(1000);
    small.swap2(large);
    EXPECT_EQ(small, SmallSizeTypeVec(values.begin(), values.end()));
    EXPECT_TRUE(large.empty());
    EXPECT_EQ(large.capacity(), 1000U);
    large.swap2(small);
    EXPECT_EQ(large, LargeSizeTypeVec(values.begin(), values.end()));
    EXPECT_TRUE(small.empty());
  }
  {
    // size of 'large' does not fit in the size type of 'small': vectors should not be modified
    SmallSizeTypeVec small(values.begin(), values.end());
    LargeSizeTypeVec large(300, 42);
    EXPECT_THROW(small.swap2(large), std::overflow_error);
    EXPECT_THROW(large.swap2(small), std::overflow_error);
    EXPECT_EQ(small, SmallSizeTypeVec(values.begin(), values.end()));
    EXPECT_EQ(large, LargeSizeTypeVec(300, 42));
  }
  {
    // capacities fit in the size type of each other: dynamic storages are exchanged
    SmallSizeTypeVec small(values.begin(), values.end());
    small.reserve(100);
    LargeSizeTypeVec large(5, 42);
    large.reserve(50);
    const int* smallData = small.data();
    const int* largeData = large.data();
    small.swap2(large);
    EXPECT_EQ(small.data(), largeData);
    EXPECT_EQ(large.data(), smallData);
    EXPECT_EQ(small, SmallSizeTypeVec(5, 42));
    EXPECT_EQ(large, LargeSizeTypeVec(values.begin(), values.end()));
    EXPECT_EQ(small.capacity(), 50U);
    EXPECT_EQ(large.capacity(), 100U);
  }
}

TEST(VectorTest, Swap2DifferentSizeTypes) {
  using Alloc = amc::allocator<int>;
  CheckSwap2DifferentSizeTypes<vector<int, Alloc, uint8_t>, vector<int, Alloc, uint32_t>>();
  CheckSwap2DifferentSizeTypes<SmallVector<int, 4, Alloc, uint8_t>, vector<int, Alloc, uint32_t>>();
  CheckSwap2DifferentSizeTypes<vector<int, Alloc, uint8_t>, SmallVector<int, 4, Alloc, uint32_t>>();
  CheckSwap2DifferentSizeTypes<SmallVector<int, 4, Alloc, uint8_t>, SmallVector<int, 8, Alloc, uint32_t>>();
}
#endif

TEST(VectorTest, TrickyEmplace) {
  using VectorType = vector<ComplexTriviallyRelocatableType>;
  VectorType v;
  v.emplace_back(2);
  v.emplace(v.begin(), 3);
  VectorType::const_iterator it = v.begin() + 1;
  v.emplace(v.begin() + 1, it);

  const ComplexTriviallyRelocatableType expectedRes[] = {
      ComplexTriviallyRelocatableType(3), ComplexTriviallyRelocatableType(2), ComplexTriviallyRelocatableType(2)};

  EXPECT_EQ(v.size(), sizeof(expectedRes) / sizeof(expectedRes[0]));
  EXPECT_TRUE(std::equal(v.begin(), v.end(), expectedRes));
}

TEST(VectorTest, TrickyPushBack) {
  using VectorType = SmallVector<SimpleNonTriviallyCopyableType, 1U>;
  VectorType v(1, 42);
  v.push_back(v.front());
  --v.front()._i;
  v.push_back(v.front());
  --v.front()._i;
  v.push_back(v.front());
  --v.front()._i;
  v.push_back(v.front());
  --v.front()._i;
  const SimpleNonTriviallyCopyableType expectedRes[] = {38, 42, 41, 40, 39};
  EXPECT_EQ(v.size(), sizeof(expectedRes) / sizeof(expectedRes[0]));
  EXPECT_TRUE(std::equal(v.begin(), v.end(), expectedRes));
  v.insert(v.begin() + 2, 3U, v.front());
  const SimpleNonTriviallyCopyableType expectedRes2[] = {38, 42, 38, 38, 38, 41, 40, 39};
  EXPECT_EQ(v.size(), sizeof(expectedRes2) / sizeof(expectedRes2[0]));
  EXPECT_TRUE(std::equal(v.begin(), v.end(), expectedRes2));
}

// Inserting a 'const_reference' that aliases an element located at or after the insertion point must be
// well-defined (see GitHub issue #63): 'shift_right' moves the source element out of the way, so a naive
// implementation ends up inserting a neighbouring value instead of the intended one.
TYPED_TEST(VectorTest, InsertSelfReferenceAfterPosition) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  for (bool inplace : {true, false}) {
    VectorType vec;
    for (int i = 0; i < 6; ++i) {
      vec.push_back(Type(i + 1));  // {1, 2, 3, 4, 5, 6}
    }
    if (inplace) {
      vec.reserve(vec.size() + 1U);  // no reallocation will happen during insert
    } else {
      vec.shrink_to_fit();  // force a reallocation during insert for growable vectors
    }
    // Insert element #4 (value 5) at index 2. Its source is *after* the insertion point, hence among the
    // elements shifted to the right by insert.
    typename VectorType::iterator p = vec.insert(vec.begin() + 2, vec[4]);
    EXPECT_EQ(*p, Type(5));
    EXPECT_EQ(static_cast<uint32_t>(vec.size()), 7U);
    const Type kExpected[] = {Type(1), Type(2), Type(5), Type(3), Type(4), Type(5), Type(6)};
    EXPECT_TRUE(std::equal(vec.begin(), vec.end(), kExpected));
  }
}

// Same aliasing concern as above, but for the 'insert(pos, count, const_reference)' overload: 'fill_after_shift'
// must read the source value from its post-shift location when it aliases a shifted element.
TYPED_TEST(VectorTest, InsertCountSelfReferenceAfterPosition) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  using SzType = typename VectorType::size_type;
  for (bool inplace : {true, false}) {
    VectorType vec;
    for (int i = 0; i < 6; ++i) {
      vec.push_back(Type(i + 1));  // {1, 2, 3, 4, 5, 6}
    }
    if (inplace) {
      vec.reserve(vec.size() + 3U);  // no reallocation will happen during insert
    } else {
      vec.shrink_to_fit();  // force a reallocation during insert for growable vectors
    }
    // Insert 3 copies of element #4 (value 5) at index 1. The source is among the shifted elements.
    typename VectorType::iterator p = vec.insert(vec.begin() + 1, static_cast<SzType>(3), vec[4]);
    EXPECT_EQ(*p, Type(5));
    EXPECT_EQ(static_cast<uint32_t>(vec.size()), 9U);
    const Type kExpected[] = {Type(1), Type(5), Type(5), Type(5), Type(2), Type(3), Type(4), Type(5), Type(6)};
    EXPECT_TRUE(std::equal(vec.begin(), vec.end(), kExpected));
  }
}

// Same aliasing concern for 'emplace', whose arguments may reference an element of the vector (see LWG 2164).
TYPED_TEST(VectorTest, EmplaceSelfReference) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  for (bool inplace : {true, false}) {
    VectorType vec;
    for (int i = 0; i < 6; ++i) {
      vec.push_back(Type(i + 1));  // {1, 2, 3, 4, 5, 6}
    }
    if (inplace) {
      vec.reserve(vec.size() + 3U);  // no reallocation will happen during emplace
    } else {
      vec.shrink_to_fit();  // force a reallocation during first emplace for growable vectors
    }
    // Emplace a copy of element #4 (value 5) at index 2. The source is among the shifted elements.
    typename VectorType::iterator p = vec.emplace(vec.begin() + 2, vec[4]);
    EXPECT_EQ(*p, Type(5));
    // Emplace a copy of element #1 (value 2) at index 1. The source is the element at the emplace position.
    p = vec.emplace(vec.begin() + 1, vec[1]);
    EXPECT_EQ(*p, Type(2));
    if (!inplace) {
      vec.shrink_to_fit();  // force a reallocation during next emplace for growable vectors
    }
    // Emplace a copy of element #0 (value 1) at the end, without any element to shift
    p = vec.emplace(vec.end(), vec[0]);
    EXPECT_EQ(*p, Type(1));
    EXPECT_EQ(static_cast<uint32_t>(vec.size()), 9U);
    const Type kExpected[] = {Type(1), Type(2), Type(2), Type(5), Type(3), Type(4), Type(5), Type(6), Type(1)};
    EXPECT_TRUE(std::equal(vec.begin(), vec.end(), kExpected));
  }
}

// 'emplace' arguments may also reference an element of the vector indirectly, here through a pointer.
template <class VectorType>
void CheckEmplaceFromPointerToShiftedElement() {
  using Type = typename VectorType::value_type;
  VectorType vec{3, 2, 1};
  vec.reserve(4U);                               // no reallocation will happen during emplace
  vec.emplace(vec.begin() + 1, vec.data() + 2);  // Type(const Type *) copies the pointed element
  const Type kExpected[] = {Type(3), Type(1), Type(2), Type(1)};
  EXPECT_EQ(static_cast<uint32_t>(vec.size()), 4U);
  EXPECT_TRUE(std::equal(vec.begin(), vec.end(), kExpected));
}

TEST(VectorTest, EmplaceFromPointerToShiftedElement) {
  CheckEmplaceFromPointerToShiftedElement<vector<ComplexTriviallyRelocatableType>>();
  CheckEmplaceFromPointerToShiftedElement<vector<ComplexNonTriviallyRelocatableType>>();
  CheckEmplaceFromPointerToShiftedElement<SmallVector<ComplexNonTriviallyRelocatableType, 4>>();
  CheckEmplaceFromPointerToShiftedElement<FixedCapacityVector<ComplexTriviallyRelocatableType, 4>>();
  CheckEmplaceFromPointerToShiftedElement<FixedCapacityVector<ComplexNonTriviallyRelocatableType, 4>>();
}

// When the vector is full, emplace methods construct the new element in a temporary storage before growing (as
// arguments may reference elements of the vector). This element must be destroyed if the growth throws.
template <class VectorType>
void CheckEmplaceGrowFailureDoesNotLeak() {
  static_assert(std::is_same<typename VectorType::size_type, uint8_t>::value, "growing beyond 255 elements throws");
  VectorType v(255);
  EXPECT_EQ(v.capacity(), 255U);

  TypeStats& stats = TypeStats::_stats;
  stats = TypeStats();
  stats.start();
  EXPECT_THROW(v.emplace_back(42), std::overflow_error);
  EXPECT_THROW(v.emplace(v.begin() + 1, 42), std::overflow_error);
  EXPECT_THROW(v.emplace(v.end(), 42), std::overflow_error);
  stats.end();

  EXPECT_EQ(stats._nbConstructs + stats._nbCopyConstructs + stats._nbMoveConstructs, stats._nbDestructs);
  EXPECT_EQ(v.size(), 255U);
}

TEST(VectorTest, EmplaceGrowFailureDoesNotLeak) {
  using NonTrivRelocType = ComplexNonTriviallyRelocatableType;
  using TrivRelocType = ComplexTriviallyRelocatableType;
  CheckEmplaceGrowFailureDoesNotLeak<vector<NonTrivRelocType, std::allocator<NonTrivRelocType>, uint8_t>>();
  CheckEmplaceGrowFailureDoesNotLeak<vector<TrivRelocType, std::allocator<TrivRelocType>, uint8_t>>();
  CheckEmplaceGrowFailureDoesNotLeak<SmallVector<NonTrivRelocType, 4, std::allocator<NonTrivRelocType>, uint8_t>>();
  CheckEmplaceGrowFailureDoesNotLeak<SmallVector<TrivRelocType, 4, std::allocator<TrivRelocType>, uint8_t>>();
}

// When copying the inserted element throws, the elements shifted to make room for it are shifted back: the vector keeps
// its elements, without any leak.
template <class VectorType>
void CheckInsertCopyThrowsKeepsElements() {
  using Type = typename VectorType::value_type;
  const Type value(5);
  TypeStats& stats = TypeStats::_stats;
  for (bool inplace : {true, false}) {
    for (int pos = 0; pos <= 4; ++pos) {
      VectorType v{1, 2, 3, 4};
      if (inplace) {
        v.reserve(5U);  // no reallocation: the elements are shifted in place
      }
      stats = TypeStats();
      stats.start();
      stats._nbCopiesBeforeThrow = 0;
      EXPECT_THROW(v.insert(v.begin() + pos, value), CopyException);
      stats._nbCopiesBeforeThrow = 0;
      EXPECT_THROW(v.insert(v.begin() + pos, v[3]), CopyException);  // source among the shifted elements
      stats._nbCopiesBeforeThrow = -1;
      stats.end();
      EXPECT_EQ(v, VectorType({1, 2, 3, 4}));
      EXPECT_EQ(stats._nbConstructs + stats._nbCopyConstructs + stats._nbMoveConstructs, stats._nbDestructs);
    }
  }
}

TEST(VectorTest, InsertCopyThrowsKeepsElements) {
  CheckInsertCopyThrowsKeepsElements<vector<ComplexTriviallyRelocatableType>>();
  CheckInsertCopyThrowsKeepsElements<vector<ComplexNonTriviallyRelocatableType>>();
  CheckInsertCopyThrowsKeepsElements<SmallVector<ComplexTriviallyRelocatableType, 2>>();
  CheckInsertCopyThrowsKeepsElements<SmallVector<ComplexNonTriviallyRelocatableType, 8>>();
  CheckInsertCopyThrowsKeepsElements<FixedCapacityVector<ComplexTriviallyRelocatableType, 5>>();
  CheckInsertCopyThrowsKeepsElements<FixedCapacityVector<ComplexNonTriviallyRelocatableType, 5>>();
}

// When the allocation needed to grow fails (out of memory), std::bad_alloc is thrown and the vector keeps its elements
// and its capacity.
template <class VectorType>
void CheckGrowAllocationFailureKeepsElements() {
  using Type = typename VectorType::value_type;
  for (int nbElems : {0, 2, 3, 8}) {
    VectorType v;
    for (int i = 0; i < nbElems; ++i) {
      v.push_back(Type(i));
    }
    v.shrink_to_fit();
    if (v.size() != v.capacity()) {
      continue;  // inline storage not full: inserting an element does not allocate
    }
    const VectorType expected = v;
    const auto capacity = v.capacity();
    const Type value(42);
    EXPECT_THROW(CallWithFailingMalloc([&] { v.push_back(value); }), std::bad_alloc);
    EXPECT_THROW(CallWithFailingMalloc([&] { v.emplace(v.begin(), 42); }), std::bad_alloc);
    EXPECT_THROW(CallWithFailingMalloc([&] { v.insert(v.begin(), 3U, value); }), std::bad_alloc);
    EXPECT_THROW(CallWithFailingMalloc([&] { v.reserve(capacity + 10U); }), std::bad_alloc);
    EXPECT_EQ(v, expected);
    EXPECT_EQ(v.capacity(), capacity);
  }
}

TEST(VectorTest, GrowAllocationFailureKeepsElements) {
  if (!kMallocFailureInjection) {
    GTEST_SKIP() << "allocation failures cannot be injected in this build (needs glibc, without sanitizers)";
  }
  // Growth with 'realloc' for trivially relocatable types, with 'malloc' and relocation of the elements otherwise
  CheckGrowAllocationFailureKeepsElements<vector<int>>();
  CheckGrowAllocationFailureKeepsElements<vector<SimpleNonTriviallyCopyableType>>();
  // Including the transition from the inline storage to the dynamic one
  CheckGrowAllocationFailureKeepsElements<SmallVector<int, 2>>();
  CheckGrowAllocationFailureKeepsElements<SmallVector<SimpleNonTriviallyCopyableType, 2>>();
  // operator new
  CheckGrowAllocationFailureKeepsElements<vector<int, std::allocator<int>>>();
}

// When copying one of several inserted elements throws, the elements constructed past the end of the vector are
// destroyed (no leak) and its size is unchanged. Trivially relocatable elements are even shifted back: the vector keeps
// its elements. Same for assign of more copies of a value than the size of the vector.
template <class VectorType>
void CheckInsertSeveralCopiesThrowsDoesNotLeak() {
  using Type = typename VectorType::value_type;
  const Type value(5);
  const std::vector<Type> values{5, 6, 7, 8, 9, 10};
  TypeStats& stats = TypeStats::_stats;
  for (int method = 0; method < 3; ++method) {
    for (int pos = 0; pos <= 4; ++pos) {
      for (int count : {1, 3, 6}) {  // fewer or more inserted elements than shifted ones
        for (int nbCopies = 0; nbCopies < count; ++nbCopies) {
          stats = TypeStats();
          stats.start();
          {
            VectorType v{1, 2, 3, 4};
            stats._nbCopiesBeforeThrow = nbCopies;
            if (method == 0) {
              EXPECT_THROW(v.insert(v.begin() + pos, static_cast<typename VectorType::size_type>(count), value),
                           CopyException);
            } else if (method == 1) {
              EXPECT_THROW(v.insert(v.begin() + pos, values.begin(), values.begin() + count), CopyException);
            } else if (count > 4) {
              EXPECT_THROW(v.assign(static_cast<typename VectorType::size_type>(count), value), CopyException);
            } else {
              stats._nbCopiesBeforeThrow = -1;
              continue;
            }
            stats._nbCopiesBeforeThrow = -1;
            EXPECT_EQ(v.size(), 4U);
            if (method != 2 && is_trivially_relocatable<Type>::value) {
              EXPECT_EQ(v, VectorType({1, 2, 3, 4}));
            }
          }
          stats.end();
          EXPECT_EQ(stats._nbConstructs + stats._nbCopyConstructs + stats._nbMoveConstructs, stats._nbDestructs);
        }
      }
    }
  }
}

TEST(VectorTest, InsertSeveralCopiesThrowsDoesNotLeak) {
  CheckInsertSeveralCopiesThrowsDoesNotLeak<vector<ComplexTriviallyRelocatableType>>();
  CheckInsertSeveralCopiesThrowsDoesNotLeak<vector<ComplexNonTriviallyRelocatableType>>();
  CheckInsertSeveralCopiesThrowsDoesNotLeak<SmallVector<ComplexTriviallyRelocatableType, 2>>();
  CheckInsertSeveralCopiesThrowsDoesNotLeak<SmallVector<ComplexNonTriviallyRelocatableType, 8>>();
  CheckInsertSeveralCopiesThrowsDoesNotLeak<FixedCapacityVector<ComplexTriviallyRelocatableType, 10>>();
  CheckInsertSeveralCopiesThrowsDoesNotLeak<FixedCapacityVector<ComplexNonTriviallyRelocatableType, 10>>();
}

// When moving an element throws, the vector keeps its elements and its memory without any leak, whatever the operation
// relocating them: growth from the inline storage, reallocation, shrink to the inline storage, swap of a large and a
// small SmallVector (the inline storage shares its first bytes with the pointer to the dynamic storage), shift.
TEST(VectorTest, ThrowingMoveDoesNotLeak) {
  using Type = ThrowingMoveType;
  using SV = SmallVector<Type, 2, BasicAllocatorWrapper<Type, CountingAllocator>>;
  static_assert(SV::kInlineCapacity == 2U, "the first inline element shares its storage with the pointer");
  int& nbMovesBeforeThrow = Type::NbMovesBeforeThrow();
  const int nbLive = Type::NbLive();
  const int64_t nbLiveAllocations = CountingAllocator::NbLiveAllocations();
  {
    SV v{1, 2};
    nbMovesBeforeThrow = 0;
    EXPECT_THROW(v.push_back(3), MoveForbiddenException);  // growth from the inline storage
    nbMovesBeforeThrow = -1;
    EXPECT_EQ(v, SV({1, 2}));
    EXPECT_EQ(v.capacity(), 2U);
    EXPECT_EQ(CountingAllocator::NbLiveAllocations(), nbLiveAllocations);

    v.reserve(4U);
    v.push_back(3);
    v.push_back(4);
    nbMovesBeforeThrow = 1;
    EXPECT_THROW(v.push_back(5), MoveForbiddenException);  // reallocation
    nbMovesBeforeThrow = -1;
    EXPECT_EQ(v, SV({1, 2, 3, 4}));
    EXPECT_EQ(v.capacity(), 4U);
    EXPECT_EQ(CountingAllocator::NbLiveAllocations(), nbLiveAllocations + 1);

    v.resize(2U);
    nbMovesBeforeThrow = 1;
    EXPECT_THROW(v.shrink_to_fit(), MoveForbiddenException);  // relocation to the inline storage
    nbMovesBeforeThrow = -1;
    EXPECT_EQ(v, SV({1, 2}));
    EXPECT_EQ(v.capacity(), 4U);

    SV small{Type(7)};
    nbMovesBeforeThrow = 0;
    EXPECT_THROW(v.swap(small), MoveForbiddenException);  // relocation of 'small' to the inline storage of 'v'
    nbMovesBeforeThrow = -1;
    EXPECT_EQ(v, SV({1, 2}));
    EXPECT_EQ(small, SV{Type(7)});
  }
  {
    // reallocation by the allocator itself, of elements which are not trivially relocatable
    BasicAllocatorWrapper<Type, CountingAllocator> alloc;
    Type* p = alloc.allocate(2U);
    amc::construct_at(p, 1);
    amc::construct_at(p + 1, 2);
    nbMovesBeforeThrow = 1;
    EXPECT_THROW(p = alloc.reallocate(p, 2U, 4U, 2U), MoveForbiddenException);
    nbMovesBeforeThrow = -1;
    EXPECT_EQ(static_cast<int32_t>(p[0]), 1);
    EXPECT_EQ(static_cast<int32_t>(p[1]), 2);
    amc::destroy_n(p, 2);
    alloc.deallocate(p, 2U);
  }
  {
    vector<Type> v{1, 2, 3};
    v.reserve(10U);
    nbMovesBeforeThrow = 1;  // move construction past the end succeeds, then a move assignment throws
    EXPECT_THROW(v.insert(v.begin(), Type(0)), MoveForbiddenException);
    EXPECT_EQ(v.size(), 3U);
    nbMovesBeforeThrow = 2;
    EXPECT_THROW(v.insert(v.begin(), 2U, Type(0)), MoveForbiddenException);
    nbMovesBeforeThrow = -1;
    EXPECT_EQ(v.size(), 3U);
  }
  EXPECT_EQ(Type::NbLive(), nbLive);
  EXPECT_EQ(CountingAllocator::NbLiveAllocations(), nbLiveAllocations);
}

// Same for the rvalue overload when moving the inserted element throws (elements are shifted with memmove here)
TEST(VectorTest, InsertMoveThrowsKeepsElements) {
  using VectorType = vector<MoveForbidden<true>>;
  VectorType v(3);
  v.reserve(4U);
  const MoveForbidden<true>* data = v.data();
  EXPECT_THROW(v.insert(v.begin() + 1, MoveForbidden<true>()), MoveForbiddenException);
  EXPECT_EQ(v.size(), 3U);
  EXPECT_EQ(v.data(), data);
}

// Exact reproduction of the first scenario reported in GitHub issue #63.
TEST(VectorTest, InsertSelfReferenceString) {
  for (bool inplace : {true, false}) {
    vector<std::string> vec;
    const std::string str("very long string much longer than SSO");
    vec.resize(6, str);
    vec[3] = "a";
    vec[5] = "b";
    ASSERT_EQ(vec[4], str);
    if (inplace) {
      vec.reserve(vec.size() + 1U);
    }
    vector<std::string>::iterator p = vec.insert(vec.begin() + 3, vec[4]);
    EXPECT_EQ(*p, str);
    EXPECT_EQ(vec[3], str);
    EXPECT_EQ(vec[4], "a");
    EXPECT_EQ(vec[5], str);
    EXPECT_EQ(vec[6], "b");
  }
}

// Exact reproduction of the second scenario reported in GitHub issue #63: the rvalue overload of push_back
// must handle an argument that aliases the vector, even when a reallocation is required to grow the container.
TEST(VectorTest, PushBackRvalueSelfReference) {
  vector<std::unique_ptr<int>> vec;
  vec.reserve(5);
  for (int i = 0; i < 5; ++i) {
    vec.push_back(std::make_unique<int>(i));
  }
  ASSERT_EQ(vec.capacity(), vec.size());  // the next push_back must reallocate
  int* d = vec.front().get();
  vec.push_back(std::move(vec.front()));
  EXPECT_EQ(vec.back().get(), d);  // the moved-from pointer landed at the back untouched
  EXPECT_FALSE(vec.front());       // the source element has been moved from
  EXPECT_NO_THROW(vec.clear());
}

template <class VectorType>
void PushBackRvalueSelfReferenceGrowImpl() {
  using Type = typename VectorType::value_type;
  VectorType vec;
  vec.reserve(5);
  for (uint32_t i = 1; i <= 5; ++i) {
    vec.push_back(Type(i));  // {1, 2, 3, 4, 5}
  }
  ASSERT_EQ(static_cast<uint32_t>(vec.capacity()), static_cast<uint32_t>(vec.size()));  // force a reallocation
  const Type frontValue = vec.front();                                                  // == Type(1)
  vec.push_back(std::move(vec.front()));
  EXPECT_EQ(static_cast<uint32_t>(vec.size()), 6U);
  EXPECT_EQ(vec.back(), frontValue);  // the front's value is preserved at the back despite the reallocation
  const Type kMiddle[] = {Type(2), Type(3), Type(4), Type(5)};
  EXPECT_TRUE(std::equal(vec.begin() + 1, vec.end() - 1, kMiddle));
}

// Same as above but forcing a reallocation for both the trivially and non trivially relocatable code paths.
TEST(VectorTest, PushBackRvalueSelfReferenceGrow) {
  PushBackRvalueSelfReferenceGrowImpl<vector<ComplexNonTriviallyRelocatableType>>();
  PushBackRvalueSelfReferenceGrowImpl<vector<ComplexTriviallyRelocatableType>>();
  PushBackRvalueSelfReferenceGrowImpl<SmallVector<ComplexNonTriviallyRelocatableType, 2>>();
  PushBackRvalueSelfReferenceGrowImpl<SmallVector<ComplexTriviallyRelocatableType, 2>>();
}

// Growing beyond max_size throws ExceptionType, whatever the method adding elements, without modifying the vector.
// With 8 bits size types, the size would overflow.
template <class IntVector, class ExceptionType>
void CheckGrowBeyondMaxSizeThrows() {
  using SizeType = typename IntVector::size_type;
  const SizeType maxSize = IntVector().max_size();
  IntVector v(static_cast<SizeType>(maxSize - 5U));
  const int kTab[] = {1, 2, 3, 4, 5, 6};
  EXPECT_THROW(v.insert(v.begin() + 1, kTab, kTab + 6), ExceptionType);
  EXPECT_EQ(v.size(), static_cast<SizeType>(maxSize - 5U));
  v.resize(maxSize);
#ifdef AMC_NONSTD_FEATURES
  EXPECT_THROW(v.append(1U, 0), ExceptionType);
#endif
  const int i = 4;
  EXPECT_THROW(v.push_back(0), ExceptionType);
  EXPECT_THROW(v.push_back(i), ExceptionType);
  EXPECT_THROW(v.emplace_back(0), ExceptionType);
  EXPECT_THROW(v.insert(v.begin(), 0), ExceptionType);
  EXPECT_EQ(v.size(), maxSize);
}

TEST(VectorTest, GrowBeyondMaxSizeThrows) {
  static_assert(sizeof(FixedCapacityVector<int, 255U>::size_type) == 1U, "");
  CheckGrowBeyondMaxSizeThrows<FixedCapacityVector<int, 255U>, std::out_of_range>();
  CheckGrowBeyondMaxSizeThrows<SmallVector<int, 32, std::allocator<int>, uint8_t>, std::overflow_error>();
  CheckGrowBeyondMaxSizeThrows<inplace_vector<int, 10>, std::bad_alloc>();
}

TEST(VectorTest, RelocatabilityAvoidsMoveOperations) {
  vector<MoveForbidden<true>, BasicAllocatorWrapper<MoveForbidden<true>, TestReallocateAllocator>> v(10);
  EXPECT_EQ(v.capacity(), v.size());
  // Should use TestAllocator::reallocate: no forbidden move / new allocate operation
  EXPECT_NO_THROW(v.emplace_back());

  vector<MoveForbidden<false>, BasicAllocatorWrapper<MoveForbidden<false>, TestReallocateAllocator>> v2(10);
  EXPECT_EQ(v2.capacity(), v2.size());
  // Cannot use reallocate as type is not trivially relocatable: attempt to use bigger allocate should fail
  EXPECT_THROW(v2.emplace_back(), BiggerAllocateException);

  vector<MoveForbidden<false>, BasicAllocatorWrapper<MoveForbidden<false>, TestAllocator>> v3(10);
  EXPECT_EQ(v3.capacity(), v3.size());
  // Cannot use reallocate as type is not trivially relocatable: attempt to use forbidden move operations
  EXPECT_THROW(v3.emplace_back(), MoveForbiddenException);
}

TEST(VectorTest, BasicAllocatorWrapperOfStatelessAllocatorsAreEqual) {
  using IntAlloc = BasicAllocatorWrapper<int, SimpleAllocator>;
  using CharAlloc = BasicAllocatorWrapper<char, SimpleAllocator>;
  EXPECT_TRUE(IntAlloc() == CharAlloc());
  EXPECT_FALSE(IntAlloc() != CharAlloc());
}

// Wrappers of basic allocators with a state compare them: an allocator is equal to itself and to its copies.
TEST(VectorTest, BasicAllocatorWrapperOfStatefulAllocators) {
  const ArenaAllocatorOf<int> alloc;
  EXPECT_TRUE(alloc == alloc);
  EXPECT_TRUE(alloc == ArenaAllocatorOf<int>(alloc));
  EXPECT_TRUE(alloc == ArenaAllocatorOf<char>(alloc));
  EXPECT_FALSE(alloc != ArenaAllocatorOf<char>(alloc));
  EXPECT_FALSE(alloc == ArenaAllocatorOf<int>(ArenaAllocator(1)));
  EXPECT_TRUE(alloc != ArenaAllocatorOf<char>(ArenaAllocator(1)));
}

// The number of bytes to allocate cannot wrap around: allocators throw beyond their max_size(), which also limits the
// max_size() of the vectors, whose size type may count more elements than size_t can count bytes.
TEST(VectorTest, AllocationSizeOverflow) {
  amc::allocator<int> alloc;
  EXPECT_THROW(alloc.allocate(alloc.max_size() + 1U), std::bad_alloc);
  amc::allocator<OverAlignedType> overAlignedAlloc;
  EXPECT_THROW(overAlignedAlloc.allocate(overAlignedAlloc.max_size() + 1U), std::bad_alloc);

  using VectorType = vector<int, amc::allocator<int>, uint64_t>;
  VectorType v{1, 2};
  EXPECT_EQ(v.max_size(), alloc.max_size());
  EXPECT_THROW(v.reserve(alloc.max_size() + 1U), std::overflow_error);
  EXPECT_EQ(v, VectorType({1, 2}));
}

// Allocators with a state are handled like in the standard containers: memory is only deallocated by an allocator
// equal to the one that allocated it, and allocators propagate according to their traits.
template <class VectorType>
void CheckStatefulAllocator() {
  using Alloc = typename VectorType::allocator_type;
  constexpr bool kPropagate = std::allocator_traits<Alloc>::propagate_on_container_move_assignment::value;
  const Alloc alloc1(ArenaAllocator(1));
  const Alloc alloc2(ArenaAllocator(2));
  const int64_t nbLiveAllocations = ArenaAllocator::NbLiveAllocations();
  const int64_t nbArenaMismatches = ArenaAllocator::NbArenaMismatches();
  const std::vector<int> smallValues{1};
  const std::vector<int> largeValues{1, 2, 3, 4, 5};
  for (const std::vector<int>* values : {&smallValues, &largeValues}) {
    const VectorType v(values->begin(), values->end(), alloc1);

    VectorType copy(v);
    EXPECT_EQ(copy.get_allocator(), alloc1);  // select_on_container_copy_construction: a copy by default
    VectorType moved(std::move(copy));
    EXPECT_EQ(moved.get_allocator(), alloc1);
    EXPECT_EQ(moved, v);
    VectorType movedWithAlloc(std::move(moved), alloc2);  // elements moved one by one to the memory of 'alloc2'
    EXPECT_EQ(movedWithAlloc.get_allocator(), alloc2);
    EXPECT_EQ(movedWithAlloc, v);
    const int* data = movedWithAlloc.data();
    VectorType movedWithEqualAlloc(std::move(movedWithAlloc), alloc2);  // equal allocators: memory is stolen
    EXPECT_EQ(movedWithEqualAlloc, v);
    if (values == &largeValues) {
      EXPECT_EQ(movedWithEqualAlloc.data(), data);
    }

    for (const std::vector<int>* lhsValues : {&smallValues, &largeValues}) {
      VectorType copyAssigned(lhsValues->begin(), lhsValues->end(), alloc2);
      copyAssigned = v;
      EXPECT_EQ(copyAssigned, v);
      EXPECT_EQ(copyAssigned.get_allocator(), kPropagate ? alloc1 : alloc2);

      VectorType moveAssigned(lhsValues->begin(), lhsValues->end(), alloc2);
      VectorType rhs(v);
      moveAssigned = std::move(rhs);
      EXPECT_EQ(moveAssigned, v);
      EXPECT_EQ(moveAssigned.get_allocator(), kPropagate ? alloc1 : alloc2);

      VectorType moveAssignedEqualAlloc(lhsValues->begin(), lhsValues->end(), alloc1);
      VectorType rhsEqualAlloc(v);
      const int* rhsData = rhsEqualAlloc.data();
      moveAssignedEqualAlloc = std::move(rhsEqualAlloc);  // equal allocators: memory is stolen
      EXPECT_EQ(moveAssignedEqualAlloc, v);
      if (values == &largeValues) {
        EXPECT_EQ(moveAssignedEqualAlloc.data(), rhsData);
      }

      // swapping containers of unequal allocators which do not propagate is undefined behavior
      const Alloc& lhsAlloc = kPropagate ? alloc2 : alloc1;
      VectorType lhs(lhsValues->begin(), lhsValues->end(), lhsAlloc);
      VectorType other(v);
      lhs.swap(other);
      EXPECT_EQ(lhs, v);
      EXPECT_EQ(lhs.get_allocator(), alloc1);
      EXPECT_EQ(other, VectorType(lhsValues->begin(), lhsValues->end()));
      EXPECT_EQ(other.get_allocator(), lhsAlloc);
    }
  }
  EXPECT_EQ(ArenaAllocator::NbArenaMismatches(), nbArenaMismatches);
  EXPECT_EQ(ArenaAllocator::NbLiveAllocations(), nbLiveAllocations);
}

TEST(VectorTest, StatefulAllocator) {
  CheckStatefulAllocator<vector<int, ArenaAllocatorOf<int>>>();
  CheckStatefulAllocator<vector<int, PropagatingArenaAllocator<int>>>();
  CheckStatefulAllocator<SmallVector<int, 2, ArenaAllocatorOf<int>>>();
  CheckStatefulAllocator<SmallVector<int, 2, PropagatingArenaAllocator<int>>>();
}

// Vectors only exchange their dynamic storages if their allocators are equal.
TEST(VectorTest, StatefulAllocatorDynamicStorageExchange) {
  using Alloc = ArenaAllocatorOf<int>;
  const Alloc alloc1(ArenaAllocator(1));
  const int64_t nbLiveAllocations = ArenaAllocator::NbLiveAllocations();
  const int64_t nbArenaMismatches = ArenaAllocator::NbArenaMismatches();
  {
    // SmallVector stealing the dynamic storage of a vector takes its allocator
    vector<int, Alloc> v({1, 2, 3}, alloc1);
    SmallVector<int, 2, Alloc> sv(std::move(v));
    EXPECT_EQ(sv.get_allocator(), alloc1);
    EXPECT_EQ(sv, (SmallVector<int, 2, Alloc>{1, 2, 3}));
#ifdef AMC_NONSTD_FEATURES
    // deep swaps, between all the flavors of vectors with a dynamic storage
    vector<int, Alloc> other({4, 5, 6, 7}, Alloc(ArenaAllocator(2)));
    other.swap2(sv);
    EXPECT_EQ(other, (vector<int, Alloc>{1, 2, 3}));
    EXPECT_EQ(sv, (SmallVector<int, 2, Alloc>{4, 5, 6, 7}));
    EXPECT_EQ(sv.get_allocator(), alloc1);
    sv.swap2(other);
    EXPECT_EQ(other, (vector<int, Alloc>{4, 5, 6, 7}));
    EXPECT_EQ(sv, (SmallVector<int, 2, Alloc>{1, 2, 3}));
    SmallVector<int, 3, Alloc> otherSmallVector({8, 9, 10, 11}, Alloc(ArenaAllocator(2)));
    sv.swap2(otherSmallVector);
    EXPECT_EQ(otherSmallVector, (SmallVector<int, 3, Alloc>{1, 2, 3}));
    EXPECT_EQ(sv, (SmallVector<int, 2, Alloc>{8, 9, 10, 11}));
    EXPECT_EQ(sv.get_allocator(), alloc1);
#endif
  }
  EXPECT_EQ(ArenaAllocator::NbArenaMismatches(), nbArenaMismatches);
  EXPECT_EQ(ArenaAllocator::NbLiveAllocations(), nbLiveAllocations);
}

TEST(VectorTest, RelocatabilityAgainstRefVector) {
  using Vec1 = vector<SmallVector<int, 3>>;
  using Vec2 = vector<FixedCapacityVector<int, 8>>;
  using VecRef = std::vector<std::vector<int>>;

  int s = 0;
  Vec1 v1;
  Vec2 v2;
  VecRef vRef;
  for (int i = 0; i < 1000; ++i) {
    if (i % 8 == 0) {
      v1.emplace_back();
      v2.emplace_back();
      vRef.emplace_back();
    } else {
      int h = static_cast<int>(HashValue64(++s));
      v1.back().push_back(h);
      v2.back().push_back(h);
      vRef.back().push_back(h);
    }
    EXPECT_EQ(v1.size(), v2.size());
    EXPECT_EQ(v2.size(), vRef.size());
    int sz = v1.size();
    for (int vPos = 0; vPos < sz; ++vPos) {
      EXPECT_EQ(v1[vPos].size(), v2[vPos].size());
      EXPECT_EQ(v2[vPos].size(), vRef[vPos].size());
      EXPECT_TRUE(std::equal(v1[vPos].begin(), v1[vPos].end(), v2[vPos].begin()));
      EXPECT_TRUE(std::equal(v2[vPos].begin(), v2[vPos].end(), vRef[vPos].begin()));
    }
  }
}

TEST(VectorTest, SmallVectorSizeOptimization) {
  constexpr auto kPtrNbBytes = sizeof(char*);
  static_assert(sizeof(SmallVector<char, kPtrNbBytes>) == sizeof(SmallVector<char, 1>),
                "SmallVector size should be optimized");
  static_assert(kPtrNbBytes != 4 || sizeof(SmallVector<int16_t, 2>) == sizeof(SmallVector<int16_t, 1>),
                "SmallVector size should be optimized");
  static_assert(kPtrNbBytes != 8 || sizeof(SmallVector<int16_t, 4>) == sizeof(SmallVector<int16_t, 1>),
                "SmallVector size should be optimized");
  static_assert(
      kPtrNbBytes != 8 || sizeof(SmallVector<std::array<char, 3>, 2>) == sizeof(SmallVector<std::array<char, 3>, 1>),
      "SmallVector size should be optimized");
}

TEST(VectorTest, SmallVectorOptimizedSizeBool) {
  using SmallBoolSmallVector = SmallVector<bool, 8>;
  SmallBoolSmallVector bools(5, false);
  bools.push_back(true);
  EXPECT_EQ(bools.size(), 6U);
  bools.insert(bools.begin(), 2, true);
  EXPECT_EQ(bools, SmallBoolSmallVector({true, true, false, false, false, false, false, true}));
  bools.push_back(false);
  EXPECT_EQ(bools, SmallBoolSmallVector({true, true, false, false, false, false, false, true, false}));
}

TEST(VectorTest, SmallVectorOptimizedSizeInt) {
  using SmallInt16SmallVector = SmallVector<int16_t, 5, amc::allocator<int16_t>, uint32_t>;
  SmallInt16SmallVector ints(3, 42);
  ints.push_back(37);
  EXPECT_EQ(ints.size(), 4U);
  EXPECT_EQ(ints, SmallInt16SmallVector({42, 42, 42, 37}));
  ints.insert(ints.begin() + 1, 2, -56);
  EXPECT_EQ(ints, SmallInt16SmallVector({42, -56, -56, 42, 42, 37}));
  ints.push_back(7567);
  EXPECT_EQ(ints, SmallInt16SmallVector({42, -56, -56, 42, 42, 37, 7567}));
}

TEST(VectorTest, SmallVectorMoveConstructFromVector) {
  using SV = SmallVector<ComplexNonTriviallyRelocatableType, 5>;
  using V = vector<ComplexNonTriviallyRelocatableType>;
  V v;
  EXPECT_EQ(SV(std::move(v)).capacity(), 5U);
  EXPECT_EQ(v.capacity(), 0U);
  v = {1, 2, 3, 4};
  EXPECT_EQ(SV(std::move(v)).capacity(), 4U);
  EXPECT_EQ(v.capacity(), 0U);
  v = {1, 2, 3, 4, 5, 6};
  EXPECT_EQ(SV(std::move(v)).capacity(), 6U);
  EXPECT_EQ(v.capacity(), 0U);
  v = {1, 2, 3, 4, 5, 6, 7, 8};
  EXPECT_EQ(SV(std::move(v)).capacity(), 8U);
  EXPECT_EQ(v.capacity(), 0U);
}

TEST(VectorTest, SmallVectorMoveAssignFromVector) {
  using SV = SmallVector<ComplexNonTriviallyRelocatableType, 5>;
  using V = vector<ComplexNonTriviallyRelocatableType>;
  V v;
  SV sv;
  sv = std::move(v);
  EXPECT_EQ(sv.capacity(), 5U);
  EXPECT_EQ(v.capacity(), 0U);
  v = {1, 2, 3, 4};
  sv = std::move(v);
  EXPECT_EQ(sv.capacity(), 4U);
  EXPECT_EQ(v.capacity(), 0U);
  v = {1, 2, 3, 4, 5, 6};
  sv = std::move(v);
  EXPECT_EQ(sv.capacity(), 6U);
  EXPECT_EQ(v.capacity(), 0U);
  v = {1, 2, 3, 4, 5, 6, 7, 8};
  sv = std::move(v);
  EXPECT_EQ(sv.capacity(), 8U);
  EXPECT_EQ(v.capacity(), 0U);
}

TEST(VectorTest, SmallVectorMoveAssignFromLargeDoesNotLeak) {
  using SV = SmallVector<int, 2, BasicAllocatorWrapper<int, CountingAllocator>>;
  // Move assign a large SmallVector to large ones of all sizes, including the empty one
  for (int size = 0; size < 4; ++size) {
    const int64_t nbLiveAllocations = CountingAllocator::NbLiveAllocations();
    {
      SV sv;
      sv.reserve(10);
      for (int i = 0; i < size; ++i) {
        sv.push_back(i);
      }
      EXPECT_EQ(sv.capacity(), 10U);
      SV o{5, 6, 7, 8};
      sv = std::move(o);
      EXPECT_EQ(sv, SV({5, 6, 7, 8}));
    }
    EXPECT_EQ(CountingAllocator::NbLiveAllocations(), nbLiveAllocations);
  }
}

// Move assigning a small SmallVector should keep the capacity of both SmallVectors, whatever their sizes and states.
template <class SV>
void CheckSmallVectorMoveAssignFromSmall() {
  static_assert(SV::kInlineCapacity == 4U, "values below are designed for 4 inline elements");
  using Values = std::vector<typename SV::value_type>;
  const std::vector<Values> smallValues{{}, {1}, {1, 2, 3}, {1, 2, 3, 4}};
  std::vector<Values> lhsValuesList = smallValues;
  lhsValuesList.push_back({1, 2, 3, 4, 5});  // large state
  for (const Values& lhsValues : lhsValuesList) {
    for (const Values& rhsValues : smallValues) {
      SV lhs(lhsValues.begin(), lhsValues.end());
      SV rhs(rhsValues.begin(), rhsValues.end());
      const auto lhsCapacity = lhs.capacity();
      lhs = std::move(rhs);
      EXPECT_EQ(lhs, SV(rhsValues.begin(), rhsValues.end()));
      EXPECT_EQ(lhs.capacity(), lhsCapacity);
      EXPECT_TRUE(rhs.empty());
      EXPECT_EQ(rhs.capacity(), 4U);
      // Filling the remaining capacity should not grow
      while (lhs.size() < lhsCapacity) {
        lhs.push_back(0);
      }
      EXPECT_EQ(lhs.capacity(), lhsCapacity);
    }
  }
}

TEST(VectorTest, SmallVectorMoveAssignFromSmall) {
  CheckSmallVectorMoveAssignFromSmall<SmallVector<int, 4>>();
  CheckSmallVectorMoveAssignFromSmall<SmallVector<ComplexTriviallyRelocatableType, 4>>();
  CheckSmallVectorMoveAssignFromSmall<SmallVector<ComplexNonTriviallyRelocatableType, 4>>();
}

template <typename T>
class VectorTestUnalignedStorage : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

typedef ::testing::Types<SmallVector<UnalignedToPtr<3>, 5>, SmallVector<UnalignedToPtr<7>, 4>,
                         SmallVector<UnalignedToPtr<5>, 3>, SmallVector<UnalignedToPtr2<3, uint16_t>, 4>,
                         SmallVector<UnalignedToPtr2<7, uint16_t>, 3>, SmallVector<UnalignedToPtr2<5, uint16_t>, 2>,
                         SmallVector<UnalignedToPtr2<3, uint32_t>, 3>, SmallVector<UnalignedToPtr2<7, uint32_t>, 2>,
                         SmallVector<UnalignedToPtr2<5, uint32_t>, 1>>
    UnalignedStorageTypes;
TYPED_TEST_SUITE(VectorTestUnalignedStorage, UnalignedStorageTypes, );

TYPED_TEST(VectorTestUnalignedStorage, SmallVectorUnalignedInlineStorage) {
  using SmallVectorUnalignedType = TypeParam;
  using VecOfVec = vector<SmallVectorUnalignedType>;
  SmallVectorUnalignedType v;
  VecOfVec vecOfVec;
  std::vector<unsigned int> expectedValues;
  for (unsigned int i = 0; i < 10U; ++i) {
    v.push_back(i);
    expectedValues.push_back(i);
    vecOfVec.push_back(v);
    EXPECT_EQ(v.size(), i + 1U);
    EXPECT_GE(v.capacity(), v.size());
    EXPECT_EQ(v, SmallVectorUnalignedType(expectedValues.begin(), expectedValues.end()));
  }
}

#if defined(AMC_CXX20) || (!defined(_MSC_VER) && defined(AMC_CXX17))
// Some earlier compilers may not be able to compile this
// Indeed, it is only specified from C++17 that std::vector may compile with incomplete types
// More information here: https://en.cppreference.com/w/cpp/container/vector
// For some reason, MSVC 2017 is not able to compile this, even with CXX17 enabled. Activate only in CXX20 for MSVC
TEST(VectorTest, IncompleteType) {
  struct Foo {
    amc::vector<Foo> v;
  };

  Foo f;
  EXPECT_TRUE(f.v.empty());
}

#endif

}  // namespace amc
