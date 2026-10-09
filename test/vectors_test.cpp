#include <gtest/gtest.h>

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

typedef ::testing::Types<
    FixedCapacityVector<int32_t, 1000>, FixedCapacityVector<TriviallyCopyableType, 1000>,
    FixedCapacityVector<ComplexNonTriviallyRelocatableType, 1000>,
    FixedCapacityVector<ComplexTriviallyRelocatableType, 1000>, FixedCapacityVector<NonTriviallyRelocatableType, 1000>,

    SmallVector<int32_t, 80>, SmallVector<TriviallyCopyableType, 90>,
    SmallVector<int32_t, 100, std::allocator<int32_t>>,
    SmallVector<TriviallyCopyableType, 110, std::allocator<TriviallyCopyableType>>,
    SmallVector<ComplexNonTriviallyRelocatableType, 120>, SmallVector<ComplexTriviallyRelocatableType, 130>,
    SmallVector<NonTriviallyRelocatableType, 140>, vector<int32_t>, vector<TriviallyCopyableType>,
    vector<ComplexNonTriviallyRelocatableType>,
    vector<ComplexTriviallyRelocatableType, std::allocator<ComplexTriviallyRelocatableType>>,
    SmallVector<NonTriviallyRelocatableType, 0U, std::allocator<NonTriviallyRelocatableType>, uint64_t>>
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
      vec.reserve(vec.size() + 2U);  // no reallocation will happen during emplace
    } else {
      vec.shrink_to_fit();  // force a reallocation during first emplace for growable vectors
    }
    // Emplace a copy of element #4 (value 5) at index 2. The source is among the shifted elements.
    typename VectorType::iterator p = vec.emplace(vec.begin() + 2, vec[4]);
    EXPECT_EQ(*p, Type(5));
    // Emplace a copy of element #1 (value 2) at index 1. The source is the element at the emplace position.
    p = vec.emplace(vec.begin() + 1, vec[1]);
    EXPECT_EQ(*p, Type(2));
    EXPECT_EQ(static_cast<uint32_t>(vec.size()), 8U);
    const Type kExpected[] = {Type(1), Type(2), Type(2), Type(5), Type(3), Type(4), Type(5), Type(6)};
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

TEST(VectorTest, SizeTypeNoIntegerOverflowFixedCapacityVector) {
  using IntVector = FixedCapacityVector<int, 255U>;
  using ExceptionType = std::out_of_range;

  static_assert(sizeof(IntVector::size_type) == 1U, "");
  IntVector v(250);
  constexpr int kTab[] = {1, 2, 3, 4, 5, 6};
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
