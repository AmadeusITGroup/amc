#include <gtest/gtest.h>
#include <stdint.h>

#include <amc/config.hpp>
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <initializer_list>
#include <list>
#ifdef AMC_SMALLSET
#include <amc/smallset.hpp>
#endif

#include "testtypes.hpp"

namespace amc {

TypeStats TypeStats::_stats;

template <typename T>
class SetListTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

typedef ::testing::Types<
// clang-format off
#ifdef AMC_SMALLSET
                         SmallSet<char, 2>,
                         SmallSet<char, 3>,
                         SmallSet<char, 10>,
                         SmallSet<uint32_t, 4, std::greater<uint32_t>>,
                         SmallSet<char, 5, std::less<char>, amc::allocator<char>>,
                         SmallSet<char, 6, std::less<char>, std::allocator<char>>,
                         SmallSet<char, 10, std::less<char>, amc::allocator<char>, FlatSet<char>>,
                         SmallSet<uint32_t, 1, std::greater<uint32_t>, amc::allocator<uint32_t>>,
                         SmallSet<int64_t, 2, std::less<int64_t>, std::allocator<int64_t>, FlatSet<int64_t, std::less<int64_t>, std::allocator<int64_t>>>,
                         SmallSet<uint16_t, 3, std::less<uint16_t>, amc::allocator<uint16_t>>,
                         SmallSet<uint8_t, 10, std::less<uint8_t>, std::allocator<uint8_t>>,
                         SmallSet<int32_t, 4, std::greater<int32_t>, std::allocator<int32_t>>,
                         SmallSet<ComplexTriviallyRelocatableType, 8>, 
                         SmallSet<ComplexTriviallyRelocatableType, 5, std::less<ComplexTriviallyRelocatableType>, amc::allocator<ComplexTriviallyRelocatableType>, FlatSet<ComplexTriviallyRelocatableType>>, 
                         SmallSet<Foo, 7>,
                         SmallSet<Foo, 14, std::less<Foo>, amc::allocator<Foo>, FlatSet<Foo, std::less<Foo>, amc::allocator<Foo>, SmallVector<Foo, 50>>>,
#endif
                         FlatSet<uint32_t, std::less<uint32_t>, FixedCapacityVector<uint32_t, 20>::allocator_type, FixedCapacityVector<uint32_t, 20> >,
                         FlatSet<char>,
                         FlatSet<uint32_t, std::greater<uint32_t>>, 
                         FlatSet<char, std::less<char>, amc::allocator<char>, SmallVector<char, 4>>,
                         FlatSet<char, std::less<char>, std::allocator<char>>,
                         FlatSet<char, std::less<char>, amc::allocator<char>, SmallVector<char, 3>>,
                         FlatSet<uint32_t, std::greater<uint32_t>, amc::allocator<uint32_t>, std::vector<uint32_t, amc::allocator<uint32_t>>>,
                         FlatSet<int64_t, std::less<int64_t>, std::allocator<int64_t>, SmallVector<int64_t, 6, std::allocator<int64_t>>>,
                         FlatSet<uint16_t, std::less<uint16_t>, amc::allocator<uint16_t>, SmallVector<uint16_t, 2>>,
                         FlatSet<uint8_t, std::greater<uint8_t>, std::allocator<uint8_t>, SmallVector<uint8_t, 11, std::allocator<uint8_t>>>,
                         FlatSet<int32_t, std::less<int32_t>, std::allocator<int32_t>>,
                         FlatSet<ComplexTriviallyRelocatableType>,
                         FlatSet<ComplexTriviallyRelocatableType, std::less<ComplexTriviallyRelocatableType>, amc::allocator<ComplexTriviallyRelocatableType>, SmallVector<ComplexTriviallyRelocatableType, 6>>,
                         FlatSet<Foo>, 
                         FlatSet<Foo, std::less<Foo>, std::allocator<Foo>, SmallVector<Foo, 5, std::allocator<Foo>, int16_t>> 
    >
    SetsType;
// clang-format on
TYPED_TEST_SUITE(SetListTest, SetsType, );

TYPED_TEST(SetListTest, DefaultConstructor) {
  TypeParam s;
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(s.size(), 0U);
}

TYPED_TEST(SetListTest, Insert) {
  TypeParam s;
  s.insert(8);
  s.insert(15);
  EXPECT_EQ(s.size(), 2U);
  EXPECT_FALSE(s.empty());
}

TYPED_TEST(SetListTest, Emplace) {
  TypeParam s;
  EXPECT_TRUE(s.emplace(4).second);
  EXPECT_FALSE(s.emplace(4).second);
  s.emplace(32);
  s.emplace(31);
  EXPECT_EQ(s.size(), 3U);
  EXPECT_FALSE(s.empty());
  EXPECT_EQ(s, TypeParam({4, 32, 31}));
}

TYPED_TEST(SetListTest, NoDuplicates) {
  TypeParam s{1, 2, 3};
  EXPECT_EQ(s, TypeParam({3, 2, 1}));
  s.insert(4);
  EXPECT_EQ(s, TypeParam({2, 1, 3, 4}));
  s.insert(4);
  EXPECT_EQ(s, TypeParam({3, 1, 4, 2}));
  EXPECT_FALSE(s.insert(3).second);
}

TYPED_TEST(SetListTest, FindAndContains) {
  TypeParam s{1, 2, 4};
  EXPECT_EQ(s.find(3), s.end());
  EXPECT_NE(s.find(1), s.end());
  EXPECT_TRUE(s.contains(2));
  EXPECT_FALSE(s.contains(5));
  std::pair<typename TypeParam::const_iterator, bool> insertedPair = s.insert(3);
  EXPECT_TRUE(insertedPair.second);
  EXPECT_TRUE(s.contains(*insertedPair.first));
  EXPECT_NE(s.find(3), s.end());
  EXPECT_TRUE(s.contains(3));
}

TYPED_TEST(SetListTest, Iteration) {
  TypeParam s{1, 3, 4, 6, 8, 15, 18};
  EXPECT_NE(s.begin(), s.end());
  TypeParam cpy;
  for (auto it = s.begin(); it != s.end(); ++it) {
    EXPECT_TRUE(s.contains(*it));
    cpy.insert(*it);
  }
  EXPECT_EQ(s, cpy);
  for (const auto& v : s) {
    EXPECT_NE(s.find(v), s.end());
  }
}

TYPED_TEST(SetListTest, ReverseIteration) {
  TypeParam s{9, 4, 67, 89, 7};
  EXPECT_NE(s.rbegin(), s.rend());
  TypeParam cpy;
  for (auto it = s.rbegin(); it != s.rend(); ++it) {
    EXPECT_TRUE(s.contains(*it));
    cpy.insert(*it);
  }
  EXPECT_EQ(s, cpy);
}

TYPED_TEST(SetListTest, IteratorOperators) {
  TypeParam s{47, 125, 90, 12, 111};
  auto begIt = s.begin();

  EXPECT_EQ(begIt, s.begin());
  EXPECT_NE(begIt, s.end());
}

TYPED_TEST(SetListTest, SpecialMembers) {
  TypeParam s{1, 2, 126, 7};
  TypeParam cpy(s);
  EXPECT_EQ(cpy, s);
  cpy = TypeParam{1, 2, 3};
  EXPECT_EQ(cpy.size(), 3U);
  TypeParam newCpy = std::move(s);
  EXPECT_EQ(newCpy, TypeParam({1, 2, 126, 7}));
  s = cpy;
  EXPECT_EQ(s, TypeParam({1, 2, 3}));
  EXPECT_EQ(s, cpy);
}

TYPED_TEST(SetListTest, EraseValue) {
  TypeParam s{4, 8, 12, 86, 3, 90, 0};
  EXPECT_EQ(s.erase(4), 1U);
  EXPECT_EQ(s.erase(5), 0U);
  EXPECT_EQ(s.erase(91), 0U);
  EXPECT_EQ(s, TypeParam({8, 12, 86, 3, 90, 0}));
}

TYPED_TEST(SetListTest, EraseIterator) {
  TypeParam s{8, 12, 86, 3, 90, 0};
  typename TypeParam::const_iterator it = s.find(86);
  EXPECT_NE(it, s.end());
  typename TypeParam::iterator eraseIt = s.erase(it);
  EXPECT_NE(eraseIt, s.cend());
  EXPECT_EQ(s.size(), 5U);
  EXPECT_EQ(s, TypeParam({8, 12, 3, 90, 0}));
}

TYPED_TEST(SetListTest, EraseRange) {
  TypeParam s{1, 2, 3, 4, 5, 6, 7, 8};
  auto it1 = s.find(3);
  auto it2 = s.find(8);
  bool it1LessThanIt2 = false;
  for (auto it = it1; it != it2 && it != s.end();) {
    if (++it == it2) {
      it1LessThanIt2 = true;
    }
  }
  if (it1LessThanIt2) {
    s.erase(it1, it2);
  } else {  // To manage std::greater
    s.erase(std::next(it2), std::next(it1));
  }
  EXPECT_EQ(s, TypeParam({1, 2, 8}));
  s.erase(s.begin(), s.end());
  EXPECT_TRUE(s.empty());
}

TYPED_TEST(SetListTest, InsertHint1) {
  TypeParam s{1, 3, 4, 6};
  s.insert(std::next(s.begin()), 2);  // good hint
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6}));
  s.insert(s.end(), 7);    // good hint
  s.insert(s.begin(), 5);  // bad hint
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 5, 6, 7}));
  s.insert(s.begin(), 1);  // good hint, equal
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 5, 6, 7}));
  s.insert(std::next(s.end(), -2), 4);  // bad hint
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 5, 6, 7}));
}

TYPED_TEST(SetListTest, InsertHint2) {
  TypeParam s{1, 6, 8, 19};
  s.insert(std::next(s.end(), -2), 4);  // bad hint
  EXPECT_EQ(s, TypeParam({1, 4, 6, 8, 19}));
  s.insert(std::next(s.end(), -1), 19);  // good hint, equal
  EXPECT_EQ(s, TypeParam({1, 4, 6, 8, 19}));
  s.insert(std::next(s.end(), -2), 9);  // correct hint
  EXPECT_EQ(s, TypeParam({1, 4, 6, 8, 9, 19}));
  s.insert(std::next(s.end(), -2), 9);  // correct hint, equal
  EXPECT_EQ(s, TypeParam({1, 4, 6, 8, 9, 19}));
  s.insert(std::next(s.end(), -3), 9);  // correct hint, equal
  EXPECT_EQ(s, TypeParam({1, 4, 6, 8, 9, 19}));
}

TYPED_TEST(SetListTest, InsertHintBeginWithSTL) {
  TypeParam s{1, 3, 4, 6};
  TypeParam s1;
  std::copy(s.begin(), s.end(), std::inserter(s1, s1.begin()));
  EXPECT_EQ(s, s1);
}

TYPED_TEST(SetListTest, InsertHintEndWithSTL) {
  TypeParam s{1, 3, 4, 6};
  TypeParam s1;
  std::copy(s.begin(), s.end(), std::inserter(s1, s1.end()));
  EXPECT_EQ(s, s1);
}

TYPED_TEST(SetListTest, EmplaceHint) {
  TypeParam s{4, 7, 18, 45};
  EXPECT_EQ(s.emplace_hint(s.end(), 45), s.find(45));  // good hint, equal
  s.emplace_hint(std::next(s.begin(), 2), 15);         // good hint
  EXPECT_EQ(s, TypeParam({4, 7, 15, 18, 45}));
  s.emplace_hint(std::next(s.begin()), 8);  // bad hint
  EXPECT_EQ(s, TypeParam({4, 7, 8, 15, 18, 45}));
  s.emplace_hint(s.end(), 17);  // bad hint
  EXPECT_EQ(s, TypeParam({4, 7, 8, 15, 17, 18, 45}));
}

TYPED_TEST(SetListTest, Swap) {
  TypeParam s1{1, 2, 3, 4};
  TypeParam s2{3, 4, 5};
  s1.swap(s2);
  EXPECT_EQ(s1, TypeParam({3, 4, 5}));
  EXPECT_EQ(s2, TypeParam({1, 2, 3, 4}));
}

TYPED_TEST(SetListTest, ReturnedIteratorsInsertErase) {
  TypeParam s{1, 2, 4};
  auto itInsertedPair = s.insert(3);
  EXPECT_EQ(itInsertedPair.first, s.find(3));
  EXPECT_TRUE(itInsertedPair.second);
  EXPECT_FALSE(s.insert(3).second);
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4}));
  s.erase(itInsertedPair.first);
  EXPECT_EQ(s, TypeParam({1, 2, 4}));
}

TYPED_TEST(SetListTest, InsertRange) {
  using value_type = typename TypeParam::value_type;
  const value_type kNewEls[] = {18, 4, 3, 6, 4};
  TypeParam s{1, 2, 3};
  s.insert(std::begin(kNewEls), std::end(kNewEls));
  EXPECT_NE(s.find(18), s.end());
  EXPECT_FALSE(s.contains(5));
  EXPECT_TRUE(s.contains(2));
  EXPECT_TRUE(s.contains(6));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 18}));
}

TYPED_TEST(SetListTest, RangeConstructor) {
  using value_type = typename TypeParam::value_type;
  const value_type kNewEls[] = {1, 2, 3, 4};
  TypeParam s(std::begin(kNewEls), std::end(kNewEls));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4}));
}

TYPED_TEST(SetListTest, ComparisonOperators) {
  TypeParam s{1, 3, 4};
  if (std::is_same<typename TypeParam::key_compare, std::less<typename TypeParam::value_type>>::value) {
    EXPECT_GT(s, TypeParam({1, 2, 3, 4}));
    EXPECT_LT(s, TypeParam({1, 4}));
    EXPECT_GE(s, TypeParam({4, 1, 3}));
    EXPECT_LE(s, TypeParam({3, 5, 1}));
#ifdef AMC_CXX20
    EXPECT_EQ(s <=> TypeParam({1, 4}), std::strong_ordering::less);
    EXPECT_EQ(s <=> TypeParam({4, 2, 3, 1}), std::strong_ordering::greater);
    EXPECT_EQ(s <=> TypeParam({3, 1, 4}), std::strong_ordering::equal);
#endif
  } else if (std::is_same<typename TypeParam::key_compare, std::greater<typename TypeParam::value_type>>::value) {
    EXPECT_LT(s, TypeParam({1, 2, 3, 4}));
    EXPECT_GT(s, TypeParam({1, 4}));
    EXPECT_LE(s, TypeParam({1, 3, 4}));
    EXPECT_LT(s, TypeParam({1, 3, 5}));
  }
}

#ifdef AMC_SMALLSET
TEST(SetTest, SmallSetSizeTest) {
  using SetType = SmallSet<int, 12>;
  SetType s;
  EXPECT_GT(s.max_size(), 12U);
  constexpr int tab[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
  s.insert(std::begin(tab), std::end(tab));
  EXPECT_EQ(s.size(), 10U);
  constexpr int tab2[] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
  s.insert(std::begin(tab2), std::end(tab2));
  EXPECT_EQ(s.size(), 20U);
  EXPECT_TRUE(s.contains(11));
}
#endif

TEST(SetTest, Relocatibility) {
  using TrivialType = int;
  using NonRelocType = std::list<int>;

  static_assert(amc::is_trivially_relocatable<FlatSet<TrivialType>>::value, "");
  static_assert(amc::is_trivially_relocatable<FlatSet<NonRelocType>>::value, "");
  static_assert(amc::is_trivially_relocatable<FlatSet<TrivialType, std::less<TrivialType>, amc::allocator<TrivialType>,
                                                      SmallVector<TrivialType, 10>>>::value ==
                    amc::is_trivially_relocatable<SmallVector<TrivialType, 10>>::value,
                "");
#ifdef AMC_SMALLSET
  static_assert(!amc::is_trivially_relocatable<SmallSet<TrivialType, 10>>::value);
  static_assert(
      amc::is_trivially_relocatable<
          SmallSet<TrivialType, 10, std::less<TrivialType>, amc::allocator<TrivialType>, FlatSet<TrivialType>>>::value);
  static_assert(!amc::is_trivially_relocatable<SmallSet<NonRelocType, 10, std::less<NonRelocType>,
                                                        amc::allocator<NonRelocType>, FlatSet<NonRelocType>>>::value);
#endif
}

#ifdef AMC_CXX17
template <typename T>
class SetListExtractTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

using SetsExtractType = ::testing::Types<FlatSet<NonCopyableType>, SmallSet<NonCopyableType, 2>,
                                         SmallSet<NonCopyableType, 4>, SmallSet<NonCopyableType, 3>,
                                         SmallSet<NonCopyableType, 2, std::less<NonCopyableType>,
                                                  amc::allocator<NonCopyableType>, FlatSet<NonCopyableType>>>;

TYPED_TEST_SUITE(SetListExtractTest, SetsExtractType, );

TYPED_TEST(SetListExtractTest, Extract) {
  using SetType = TypeParam;
  SetType s;
  s.emplace(3);
  s.emplace(17);
  s.emplace(2);
  auto nh = s.extract(3);
  nh.value() = 16;
  SetType ref;
  ref.emplace(2);
  ref.emplace(17);
  EXPECT_EQ(s, ref);
  s.insert(std::move(nh));
  ref.clear();
  ref.emplace(2);
  ref.emplace(17);
  ref.emplace(16);
  EXPECT_EQ(s, ref);
}
#endif

template <typename T>
class SetListMergeTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};
// clang-format off
typedef ::testing::Types<FlatSet<int>
#ifdef AMC_SMALLSET
                        ,SmallSet<int, 2>, 
                         SmallSet<int, 10>, 
                         SmallSet<char, 3>,
                         SmallSet<int, 2, std::less<int>, amc::allocator<int>, FlatSet<int>>
#endif
                        >
    SetsMergeType;
// clang-format on

TYPED_TEST_SUITE(SetListMergeTest, SetsMergeType, );

TYPED_TEST(SetListMergeTest, MergeWithEmpty) {
  using SetType = TypeParam;

  SetType s1{0, 1};
  SetType s2;
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({0, 1}));
  EXPECT_EQ(s2, SetType());
}

TYPED_TEST(SetListMergeTest, MergeFromEmpty) {
  using SetType = TypeParam;

  SetType s1;
  SetType s2{0, 1};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({0, 1}));
  EXPECT_EQ(s2, SetType());
}

TYPED_TEST(SetListMergeTest, MergeNewElems) {
  using SetType = TypeParam;
  SetType s1{0, 2, 4, 5, 6, 7, 9, 15};
  SetType s2{1, 3, 8, 10, 11, 12};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 15}));
  EXPECT_EQ(s2, SetType());
}

TYPED_TEST(SetListMergeTest, MergeSimilarElems) {
  using SetType = TypeParam;
  SetType s1{0, 2, 4, 5, 6, 7, 9, 15};
  SetType s2{0, 2, 5, 7, 9, 15};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({0, 2, 4, 5, 6, 7, 9, 15}));
  EXPECT_EQ(s2, SetType({0, 2, 5, 7, 9, 15}));
}

TYPED_TEST(SetListMergeTest, MergeMix1) {
  using SetType = TypeParam;

  SetType s1{0, 1, 4, 5, 7};
  SetType s2{0, 2, 3, 4};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({0, 1, 2, 3, 4, 5, 7}));
  EXPECT_EQ(s2, SetType({0, 4}));
}

TYPED_TEST(SetListMergeTest, MergeMix2) {
  using SetType = TypeParam;
  SetType s1{'C', 'B', 'B', 'A'}, s2{'E', 'D', 'E', 'C'};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({'A', 'B', 'C', 'D', 'E'}));
  EXPECT_EQ(s2, SetType({'C'}));
}

TYPED_TEST(SetListMergeTest, MergeMix3) {
  using SetType = TypeParam;
  SetType s1{-2, 0, 2, 3, 4, 6, 19};
  SetType s2{0, 2, 5, 7, 9, 10, 19, 20, 22, 23, 25};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({-2, 0, 2, 3, 4, 5, 6, 7, 9, 10, 19, 20, 22, 23, 25}));
  EXPECT_EQ(s2, SetType({0, 2, 19}));
}

TEST(FlatSetTest, MergeDifferentCompare) {
  using SetType = FlatSet<int, std::less<int>>;
  using RevSetType = FlatSet<int, std::greater<int>>;
  SetType s1{-2, 0, 2, 3, 4, 6, 19};
  RevSetType s2{23, 19, 17, 4, 2, -2};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({-2, 0, 2, 3, 4, 6, 17, 19, 23}));
  EXPECT_EQ(s2, RevSetType({19, 4, 2, -2}));
}

#ifdef AMC_CXX14
template <typename T>
class SetListEquivalentType : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};
// clang-format off
typedef ::testing::Types<FlatSet<int, std::less<>>
#ifdef AMC_SMALLSET
                       ,SmallSet<int, 2, std::less<>>
                       ,SmallSet<int, 5, std::less<>>
#endif
                        >
    SetsEquivalentTypes;
// clang-format on

TYPED_TEST_SUITE(SetListEquivalentType, SetsEquivalentTypes, );

TYPED_TEST(SetListEquivalentType, FindEquivalentType) {
  TypeParam s{1, 2, 4};
  EXPECT_EQ(s.find(3), s.end());
  EXPECT_NE(s.find(1), s.end());
  EXPECT_EQ(s.find(4.5), s.end());
  EXPECT_NE(s.find(1.0), s.end());
}

TYPED_TEST(SetListEquivalentType, ContainsEquivalentType) {
  TypeParam s{-3, 0, 6, 7};
  EXPECT_TRUE(s.contains(false));
  EXPECT_FALSE(s.contains(true));
  EXPECT_TRUE(s.contains(7ULL));
  EXPECT_FALSE(s.contains(static_cast<char>(4)));
}
#endif

#ifdef AMC_SMALLSET
TEST(SmallSetTest, MergeDifferentCompare) {
  using SetType = SmallSet<char, 20, std::less<char>>;
  using RevSetType = SmallSet<char, 10, std::greater<char>>;
  SetType s1{'B', 'D', 'E', 'G'};
  RevSetType s2{'H', 'E', 'C', 'A'};
  s1.merge(s2);
  EXPECT_EQ(s1, SetType({'A', 'B', 'C', 'D', 'E', 'G', 'H'}));
  EXPECT_EQ(s2, RevSetType({'E'}));
}
#endif

#ifdef AMC_NONSTD_FEATURES
TEST(FlatSetTest, SpecificPointerMethods) {
  using SetType = FlatSet<int>;
  SetType s{-2, 0, 2, 3, 4, 6, 19};
  const SetType::value_type* pValue = s.data();  // to ensure return type of data() is a pointer
  EXPECT_EQ(pValue[0], -2);
  EXPECT_EQ(s[1], 0);
  EXPECT_EQ(s.at(2), 2);
  EXPECT_THROW(s.at(7), std::out_of_range);
}

TEST(FlatSetTest, CreateFromVector) {
  using SetType = FlatSet<int>;
  using VecType = SetType::vector_type;
  VecType myVec{5, -1, 6, 8, 0};
  SetType s(std::move(myVec));
  EXPECT_TRUE(myVec.empty());
  EXPECT_EQ(s, SetType({-1, 0, 5, 6, 8}));
}

TEST(FlatSetTest, StealVector) {
  using SetType = FlatSet<int>;
  using VecType = SetType::vector_type;
  SetType s{5, -1, 6, 8, 0};
  auto stolenVec = s.steal_vector();
  EXPECT_TRUE(s.empty());
  EXPECT_EQ(stolenVec, VecType({-1, 0, 5, 6, 8}));
}
#endif
}  // namespace amc
