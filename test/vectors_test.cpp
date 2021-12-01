#include <gtest/gtest.h>

#include <amc/fixedcapacityvector.hpp>
#include <amc/smallvector.hpp>
#include <amc/vector.hpp>
#include <array>
#include <initializer_list>
#include <list>
#include <numeric>
#include <vector>

#include "testhelpers.hpp"
#include "testtypes.hpp"

namespace amc {

TypeStats TypeStats::_stats;

template <typename T>
class VectorTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

typedef ::testing::Types<
    FixedCapacityVector<char, 23>, FixedCapacityVector<uint32_t, 24>, FixedCapacityVector<TriviallyCopyableType, 18>,
    FixedCapacityVector<ComplexNonTriviallyRelocatableType, 17>,
    FixedCapacityVector<ComplexTriviallyRelocatableType, 29>, FixedCapacityVector<NonTriviallyRelocatableType, 64>,
    SmallVector<char, 5>, SmallVector<uint32_t, 4, std::allocator<uint32_t>, int32_t>,
    SmallVector<TriviallyCopyableType, 8>, SmallVector<ComplexNonTriviallyRelocatableType, 6>,
    SmallVector<ComplexTriviallyRelocatableType, 8>, SmallVector<ComplexTriviallyRelocatableType, 10>,
    SmallVector<NonTriviallyRelocatableType, 1, std::allocator<NonTriviallyRelocatableType>, int16_t>,
    SmallVector<uint32_t, 0, std::allocator<uint32_t>, signed char>,
    SmallVector<NonTriviallyRelocatableType, 3, std::allocator<NonTriviallyRelocatableType>>,
    SmallVector<UnalignedToPtr<3>, 4>, SmallVector<UnalignedToPtr<7>, 3>, SmallVector<UnalignedToPtr<5>, 2>,
    vector<int32_t, std::allocator<int32_t>, uint64_t>, vector<TriviallyCopyableType>,
    vector<ComplexNonTriviallyRelocatableType>, vector<ComplexTriviallyRelocatableType>,
    vector<ComplexNonTriviallyRelocatableType, std::allocator<ComplexNonTriviallyRelocatableType>>,
    vector<ComplexTriviallyRelocatableType, std::allocator<ComplexTriviallyRelocatableType>>,
    vector<NonTriviallyRelocatableType>>
    MyTypes;
TYPED_TEST_SUITE(VectorTest, MyTypes, );

template <class VecType>
void ChecksAgainstTab(const VecType &cont, std::initializer_list<typename VecType::value_type> expectedValues) {
  using ValueType = typename VecType::value_type;
  using ConstIt = typename VecType::const_iterator;
  using RefVec = std::vector<ValueType>;

  EXPECT_FALSE(cont.empty());
  EXPECT_EQ(static_cast<uint32_t>(cont.size()), expectedValues.size());
  auto it = expectedValues.begin();
  EXPECT_EQ(cont.front(), *it);
  for (const ValueType &v : cont) {
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
    const ValueType &randValue = expectedValues.begin()[gTabPos++];
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
      refTab.insert(refTab.begin() + i, v, randValue + i);
      cpy2.insert(cpy2.begin() + i, v, randValue + i);
      EXPECT_EQ(cpy2, VecType(refTab.begin(), refTab.end()));
    }

    EXPECT_LT(cpy.size(), cont.size());
    EXPECT_NE(cpy, cont);
    EXPECT_NE(std::find(cont.begin(), cont.end(), randValue), cont.end());
    if (gTabPos != expectedValues.size()) {
      EXPECT_EQ(cpy[gTabPos - 1], cont[gTabPos]);
    }
    cpy.emplace(cpy.begin() + cpy.size() / 2, v / 2);
    cpy2 = cpy;
    EXPECT_EQ(cpy2, cpy);
    cpy.shrink_to_fit();
    EXPECT_EQ(cpy2, cpy);
    cpy.clear();
    EXPECT_TRUE(cpy.empty());
    cpy.resize(v + 1, 42);
    refTab.assign(v + 1, 42);
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
      cpy2.assign(v, 9 + v);
      VecType newCont = cpy2;
      refTab.assign(v, 9 + v);
      EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      for (int i = std::max(0, v - 1); i < v + 7; ++i) {
        // Test Copy assignment
        if (copyType == 0) {
          VecType c;
          newCont.swap(c);
        }
        VecType cpy3(i, 42 + v);
        newCont = cpy3;
        refTab.assign(i, 42 + v);
        EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      }
    }

    for (int moveType = 0; moveType < 2; ++moveType) {
      // Test Move constructor
      cpy2.assign(v, 9 + v);
      VecType newCont = std::move(cpy2);
      refTab.assign(v, 9 + v);
      EXPECT_EQ(newCont, VecType(refTab.begin(), refTab.end()));
      for (int i = std::max(0, v - 1); i < v + 7; ++i) {
        // Test Move assignment
        if (moveType == 0) {
          VecType c;
          newCont.swap(c);
        }
        newCont = VecType(i, 42 + v);
        refTab.assign(i, 42 + v);
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
  s.push_back(8);
  s.emplace_back(15);
  s.insert(s.begin(), 3);
  s.insert(s.end(), 8);
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

template <typename T>
class VectorRefTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

// clang-format off
typedef ::testing::Types<
    FixedCapacityVector<int32_t, 1000>, 
    FixedCapacityVector<TriviallyCopyableType, 1000>, 
    FixedCapacityVector<ComplexNonTriviallyRelocatableType, 1000>,
    FixedCapacityVector<ComplexTriviallyRelocatableType, 1000>, 
    FixedCapacityVector<NonTriviallyRelocatableType, 1000>,

    SmallVector<int32_t, 80>, 
    SmallVector<TriviallyCopyableType, 90>,
    SmallVector<int32_t, 100, std::allocator<int32_t> >,
    SmallVector<TriviallyCopyableType, 110, std::allocator<TriviallyCopyableType> >,
    SmallVector<ComplexNonTriviallyRelocatableType, 120>, 
    SmallVector<ComplexTriviallyRelocatableType, 130>,
    SmallVector<NonTriviallyRelocatableType, 140>, 
    vector<int32_t>, 
    vector<TriviallyCopyableType>,
    vector<ComplexNonTriviallyRelocatableType>,
    vector<ComplexTriviallyRelocatableType, std::allocator<ComplexTriviallyRelocatableType> >,
    SmallVector<NonTriviallyRelocatableType, 0U, std::allocator<NonTriviallyRelocatableType>, uint64_t> >
    MyTypesForRef;
// clang-format on
TYPED_TEST_SUITE(VectorRefTest, MyTypesForRef, );

template <class T, class A, class S, class G, S N>
inline bool operator==(const Vector<T, A, S, G, N> &lhs, const typename std::vector<T> &rhs) {
  return lhs.size() == rhs.size() && std::equal(lhs.begin(), lhs.end(), rhs.begin());
}

TYPED_TEST(VectorRefTest, CompareToRefVector) {
  using VectorType = TypeParam;
  using Type = typename VectorType::value_type;
  using RefVecType = typename std::vector<Type>;

  VectorType v(100U, 2);
  RefVecType r(100U, 2);
  EXPECT_EQ(v, r);

  std::array<Type, 200U> kTab;
  std::iota(kTab.begin(), kTab.end(), 0);
  for (int i = 10, s = 19; i < 100; i += 7, s += 3) {
    v.insert(std::min(v.begin() + i, v.end()), static_cast<uint32_t>(s), s);
    r.insert(std::min(r.begin() + i, r.end()), static_cast<uint32_t>(s), s);
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

TEST(VectorTest, NonCopyableType) {
  using NonCopyableTypeVectorType = FixedCapacityVector<NonCopyableType, 10>;
  NonCopyableTypeVectorType v(6);
  EXPECT_EQ(v.front(), NonCopyableType());
  EXPECT_EQ(v.back(), NonCopyableType());
  v.resize(7);
  EXPECT_EQ(v[6], NonCopyableType());
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

TEST(VectorTest, SizeTypeNoIntegerOverflowFixedCapacityVector) {
  using IntVector = FixedCapacityVector<int, 255U>;
  using ExceptionType = std::out_of_range;
  static_assert(sizeof(IntVector::size_type) == 1U, "");
  IntVector v(250);
  const int kTab[] = {1, 2, 3, 4, 5, 6};
  EXPECT_THROW(v.insert(v.begin() + 1, kTab, kTab + 6), ExceptionType);
  v.resize(255);
  int i = 4;
#ifdef AMC_NONSTD_FEATURES
  EXPECT_THROW(v.append(1U, 0), ExceptionType);
#endif
  EXPECT_THROW(v.push_back(0), ExceptionType);
  EXPECT_THROW(v.push_back(i), ExceptionType);
  EXPECT_THROW(v.emplace_back(0), ExceptionType);
}

TEST(VectorTest, SizeTypeNoIntegerOverflowSmallVector) {
  using IntVector = SmallVector<int, 32, std::allocator<int>, uint8_t>;
  using ExceptionType = std::overflow_error;
  static_assert(sizeof(IntVector::size_type) == 1U, "");
  IntVector v(250);
  const int kTab[] = {1, 2, 3, 4, 5, 6};
  EXPECT_THROW(v.insert(v.begin() + 1, kTab, kTab + 6), ExceptionType);
  v.resize(255);
#ifdef AMC_NONSTD_FEATURES
  EXPECT_THROW(v.append(1, 0), ExceptionType);
#endif
  EXPECT_THROW(v.push_back(0), ExceptionType);
  EXPECT_THROW(v.push_back(4), ExceptionType);
  EXPECT_THROW(v.emplace_back(0), ExceptionType);
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
  constexpr auto kPtrNbBytes = sizeof(char *);
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
