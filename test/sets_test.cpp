#include <gtest/gtest.h>
#include <stdint.h>

#include <algorithm>
#include <amc/config.hpp>
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <cstddef>
#include <iterator>
#include <limits>
#include <list>
#include <set>
#include <sstream>
#include <vector>
#ifdef AMC_SMALLSET
#include <amc/smallset.hpp>
#endif
#ifdef AMC_CXX23
#include <ranges>
#endif

#include "testmallocfailure.hpp"
#include "testtypes.hpp"

namespace amc {

TypeStats TypeStats::_stats;

template <typename T>
class SetListTest : public ::testing::Test {
 public:
  using List = typename std::list<T>;
};

// Each type exercises a distinct combination of what the sets depend on, as every type multiplies the compilation time
// of the typed tests:
//  - SmallSet: the container of the large state (std::set or FlatSet), the inline capacity (with a small capacity, most
//    tests run in the large state), the comparator, the element type. The allocator is only used by the container of
//    the large state.
//  - FlatSet: the underlying vector (amc::vector, SmallVector, FixedCapacityVector, std::vector) and its allocator, the
//    comparator, the element type.
typedef ::testing::Types<
#ifdef AMC_SMALLSET
    SmallSet<char, 2>, SmallSet<char, 10>, SmallSet<uint32_t, 4, std::greater<uint32_t>>,
    SmallSet<uint32_t, 1, std::greater<uint32_t>>,
    SmallSet<char, 10, std::less<char>, amc::allocator<char>, FlatSet<char>>,
    SmallSet<int64_t, 2, std::less<int64_t>, std::allocator<int64_t>,
             FlatSet<int64_t, std::less<int64_t>, std::allocator<int64_t>>>,
    SmallSet<ComplexTriviallyRelocatableType, 8>,
    SmallSet<ComplexTriviallyRelocatableType, 5, std::less<ComplexTriviallyRelocatableType>,
             amc::allocator<ComplexTriviallyRelocatableType>, FlatSet<ComplexTriviallyRelocatableType>>,
    SmallSet<Foo, 7>,
    SmallSet<Foo, 14, std::less<Foo>, amc::allocator<Foo>,
             FlatSet<Foo, std::less<Foo>, amc::allocator<Foo>, SmallVector<Foo, 50>>>,
#endif
    FlatSet<char>, FlatSet<uint32_t, std::greater<uint32_t>>,
    FlatSet<uint32_t, std::less<uint32_t>, FixedCapacityVector<uint32_t, 20>::allocator_type,
            FixedCapacityVector<uint32_t, 20>>,
    FlatSet<char, std::less<char>, amc::allocator<char>, SmallVector<char, 4>>,
    FlatSet<int64_t, std::less<int64_t>, std::allocator<int64_t>, SmallVector<int64_t, 6, std::allocator<int64_t>>>,
    FlatSet<uint32_t, std::greater<uint32_t>, amc::allocator<uint32_t>,
            std::vector<uint32_t, amc::allocator<uint32_t>>>,
    FlatSet<ComplexTriviallyRelocatableType>,
    FlatSet<Foo, std::less<Foo>, std::allocator<Foo>, SmallVector<Foo, 5, std::allocator<Foo>, int16_t>>>
    SetsType;
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
  s.insert(8);
  EXPECT_EQ(s.size(), 2U);
  EXPECT_FALSE(s.empty());
  EXPECT_TRUE(s.contains(15));
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
  EXPECT_EQ(*std::prev(s.rend()), *s.begin());
  EXPECT_EQ(*std::prev(s.end()), *s.rbegin());
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

// Erasing all elements of a large SmallSet makes it small again: the returned iterators must still compare equal to
// end(), so that the usual erase loops terminate. Same for clear.
TYPED_TEST(SetListTest, EraseAllElementsReturnsEnd) {
  using value_type = typename TypeParam::value_type;
  constexpr int kNbElems = 20;  // larger than the inline capacity of all tested SmallSets
  TypeParam s;
  for (int i = 1; i <= kNbElems; ++i) {
    s.insert(value_type(i));
  }

  TypeParam s1 = s;
  typename TypeParam::iterator it = s1.begin();
  for (int i = 0; i < kNbElems; ++i) {
    it = s1.erase(it);
  }
  EXPECT_TRUE(s1.empty());
  EXPECT_EQ(it, s1.end());

  TypeParam s2 = s;
  it = s2.erase(s2.begin(), s2.end());
  EXPECT_TRUE(s2.empty());
  EXPECT_EQ(it, s2.end());

  TypeParam s3 = s;
  s3.erase(std::next(s3.begin()), s3.end());
  EXPECT_EQ(s3.size(), 1U);
  it = s3.erase(s3.begin());
  EXPECT_TRUE(s3.empty());
  EXPECT_EQ(it, s3.end());

#ifdef AMC_CXX20
  TypeParam s4 = s;
  EXPECT_EQ(erase_if(s4, [](const value_type&) { return true; }), static_cast<typename TypeParam::size_type>(kNbElems));
  EXPECT_TRUE(s4.empty());
#endif

  TypeParam s5 = s;
  s5.clear();
  EXPECT_TRUE(s5.empty());
  EXPECT_EQ(s5.begin(), s5.end());
  s5.insert(value_type(4));
  EXPECT_EQ(s5, TypeParam{value_type(4)});
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
  s.insert(std::prev(s.end()), 8);  // hint on the last element, value after it (with std::less)
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 5, 6, 7, 8}));
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

// Like std::set, merge moves the elements which are not already present, the other ones stay in the source set.
// The elements of the merged sets have all the relative positions (interleaved, equivalent, before or after all the
// other ones), in vectors which stay inline or grow. The sizes of the last merged sets add up to more than the capacity
// of the FixedCapacityVector (20), but not their union.
TYPED_TEST(SetListTest, Merge) {
  using SetType = TypeParam;
  using ValueType = typename SetType::value_type;
  using RefSetType = std::set<ValueType, typename SetType::key_compare>;
  using Values = std::vector<ValueType>;
  const std::pair<Values, Values> merges[] = {
      {{2, 4, 5, 8, 10, 11, 14}, {0, 1, 4, 6, 8, 9, 11, 15, 16}},
      {{0, 1, 4, 6, 8, 9, 11, 15, 16}, {2, 4, 5, 8, 10, 11, 14}},
      {{1, 3}, {2, 3}},
      {{10, 11, 12}, {1, 2}},
      {{10, 11, 12}, {13, 15}},
      {{1, 2, 3}, {1, 2, 3}},
      {{}, {1, 2, 3}},
      {{1, 2, 3}, {}},
      {{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14}, {5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19}}};
  for (const std::pair<Values, Values>& values : merges) {
    SetType s1(values.first.begin(), values.first.end());
    SetType s2(values.second.begin(), values.second.end());
    RefSetType ref1(values.first.begin(), values.first.end());
    RefSetType ref2(values.second.begin(), values.second.end());
    s1.merge(s2);
    ref1.merge(ref2);
    EXPECT_EQ(s1, SetType(ref1.begin(), ref1.end()));
    EXPECT_EQ(s2, SetType(ref2.begin(), ref2.end()));
  }

  SetType s{1, 2, 3};
  s.merge(s);
  EXPECT_EQ(s, SetType({1, 2, 3}));
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

/// Values 1 to 'n' (at most 127, the maximum of a char), in the order of the comparator of 'SetType'.
template <class SetType>
std::vector<typename SetType::value_type> SortedValues(std::size_t n) {
  using ValueType = typename SetType::value_type;
  std::vector<ValueType> values;
  for (std::size_t i = 1; i <= n; ++i) {
    // an unsigned char converts to all the element types without sign or narrowing conversion
    values.push_back(static_cast<ValueType>(static_cast<unsigned char>(i)));
  }
  std::sort(values.begin(), values.end(), typename SetType::key_compare());
  return values;
}

using Indexes = std::vector<std::size_t>;

/// Elements of 'values' at 'indexes'.
template <class ValueType>
std::vector<ValueType> ValuesAt(const std::vector<ValueType>& values, const Indexes& indexes) {
  std::vector<ValueType> ret;
  for (std::size_t i : indexes) {
    ret.push_back(values[i]);
  }
  return ret;
}

TYPED_TEST(SetListTest, InsertIterators) {
  using value_type = typename TypeParam::value_type;
  const value_type kNewEls[] = {18, 4, 3, 6, 4};
  TypeParam s{1, 2, 3};
  s.insert(std::begin(kNewEls), std::end(kNewEls));
  EXPECT_NE(s.find(18), s.end());
  EXPECT_FALSE(s.contains(5));
  EXPECT_TRUE(s.contains(2));
  EXPECT_TRUE(s.contains(6));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 18}));

  // empty range, then elements all greater, and all less than the present ones
  s.insert(std::begin(kNewEls), std::begin(kNewEls));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 18}));
  const value_type kGreaterEls[] = {20, 19, 20};
  s.insert(std::begin(kGreaterEls), std::end(kGreaterEls));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 18, 19, 20}));
  const value_type kLessEls[] = {0, 0};
  s.insert(std::begin(kLessEls), std::end(kLessEls));
  EXPECT_EQ(s, TypeParam({0, 1, 2, 3, 4, 6, 18, 19, 20}));

  // Ranges sorted by the comparator of the set, which are not sorted again: after all the elements of the set (in an
  // empty set, strictly sorted, with a duplicate) and before or equivalent to some of them.
  const std::vector<value_type> sorted = SortedValues<TypeParam>(8);
  TypeParam s2;
  TypeParam expected;
  for (const Indexes& indexes : {Indexes{1, 3}, Indexes{4, 5}, Indexes{6, 7, 7}, Indexes{0, 2, 2, 3}}) {
    const std::vector<value_type> inserted = ValuesAt(sorted, indexes);
    s2.insert(inserted.begin(), inserted.end());
    for (const value_type& v : inserted) {
      expected.insert(v);
    }
    EXPECT_EQ(s2, expected);
  }
}

#ifdef AMC_CXX23

TYPED_TEST(SetListTest, InsertRange) {
  using value_type = typename TypeParam::value_type;
  const value_type kNewEls[] = {18, 4, 3, 6, 4};
  TypeParam s{1, 2, 3};
  s.insert_range(kNewEls);
  EXPECT_NE(s.find(18), s.end());
  EXPECT_FALSE(s.contains(5));
  EXPECT_TRUE(s.contains(2));
  EXPECT_TRUE(s.contains(6));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 18}));

  // ranges that are not common (whose end is a sentinel of another type than their iterator), forward or input ones
  s.insert_range(std::views::iota(17) | std::views::take_while([](int i) { return i < 20; }));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 17, 18, 19}));
  std::istringstream ss("5 20 1");
  s.insert_range(std::views::istream<int>(ss));
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 5, 6, 17, 18, 19, 20}));
}

#endif

// Input in the order of the comparator of the set: sorted (strictly, or with duplicates), which is not sorted again,
// sorted up to a duplicate or to its last element, unsorted from its beginning, or reversed.
TYPED_TEST(SetListTest, RangeConstructor) {
  using value_type = typename TypeParam::value_type;
  const std::vector<value_type> sorted = SortedValues<TypeParam>(4);
  TypeParam expected;
  for (const value_type& v : sorted) {
    expected.insert(v);
  }
  for (const Indexes& indexes :
       {Indexes{0, 1, 2, 3}, Indexes{0, 0, 1, 2, 3}, Indexes{0, 1, 2, 2, 3, 3}, Indexes{0, 1, 1, 3, 2},
        Indexes{0, 1, 3, 2}, Indexes{1, 0, 2, 3}, Indexes{3, 2, 1, 0}}) {
    const std::vector<value_type> input = ValuesAt(sorted, indexes);
    EXPECT_EQ(TypeParam(input.begin(), input.end()), expected);
  }
  EXPECT_TRUE(TypeParam(sorted.begin(), sorted.begin()).empty());

  // Input of at least 32 elements (kMinSizeToMergeSortedPrefix), starting with a sorted prefix of at least two thirds
  // of them: only the other ones are sorted, then merged with the end of the prefix. Duplicates are erased from the
  // first duplicate of the prefix, or from the first merged element. The last input has a too short sorted prefix.
  const std::vector<value_type> sorted48 = SortedValues<TypeParam>(48);
  Indexes evens;  // 0, 2, ..., 46
  for (std::size_t i = 0; i < 48; i += 2) {
    evens.push_back(i);
  }
  const auto concat = [](Indexes lhs, const Indexes& rhs) {
    lhs.insert(lhs.end(), rhs.begin(), rhs.end());
    return lhs;
  };
  // with duplicates, and an element equivalent to one of the prefix
  const Indexes upperTail{45, 31, 47, 31, 46, 33, 39, 41};
  const Indexes inputs[] = {
      concat(evens, upperTail),               // strictly sorted prefix
      concat(concat({0}, evens), upperTail),  // duplicate in the prefix
      concat(concat(Indexes(evens.begin() + 1, evens.end()), {47}), {1, 0, 13, 2, 25, 25, 3, 0}),  // tail before all
      concat(Indexes(evens.begin(), evens.begin() + 21), {39, 47, 45, 43, 41, 37, 35, 33, 31, 29, 27})};  // 21 < 2 * 11
  for (const Indexes& indexes : inputs) {
    if (static_cast<std::size_t>(TypeParam().max_size()) < indexes.size() + 2U) {
      break;  // FixedCapacityVector
    }
    const std::vector<value_type> input = ValuesAt(sorted48, indexes);
    TypeParam expectedLong;
    for (const value_type& v : input) {
      expectedLong.insert(v);
    }
    EXPECT_EQ(TypeParam(input.begin(), input.end()), expectedLong);

    // insertion in a set whose elements are interleaved with the inserted ones
    TypeParam s{sorted48[5], sorted48[30]};
    s.insert(input.begin(), input.end());
    expectedLong.insert(sorted48[5]);
    expectedLong.insert(sorted48[30]);
    EXPECT_EQ(s, expectedLong);
  }
}

// Single pass input iterators (here, reading from a stream) can be traversed only once.
TYPED_TEST(SetListTest, InsertInputIterators) {
  std::istringstream ss("18 4 3 6 4");
  TypeParam s{1, 2, 3};
  s.insert(std::istream_iterator<int>(ss), std::istream_iterator<int>());
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4, 6, 18}));
}

TYPED_TEST(SetListTest, RangeConstructorInputIterators) {
  std::istringstream ss("4 1 3 2 1");
  std::istream_iterator<int> first(ss);
  std::istream_iterator<int> last;
  TypeParam s(first, last);
  EXPECT_EQ(s, TypeParam({1, 2, 3, 4}));
}

TYPED_TEST(SetListTest, ComparisonOperators) {
  using value_type = typename TypeParam::value_type;
  TypeParam s{1, 3, 4};
  TypeParam large;  // large state for all tested SmallSets
  for (int i = 1; i <= 20; ++i) {
    large.insert(value_type(i));
  }
  EXPECT_NE(s, TypeParam({1, 3}));
  EXPECT_NE(s, large);
  if (std::is_same<typename TypeParam::key_compare, std::less<typename TypeParam::value_type>>::value) {
    EXPECT_GT(s, large);
    EXPECT_LT(large, s);
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
    EXPECT_LT(s, large);
    EXPECT_GT(large, s);
    EXPECT_LT(s, TypeParam({1, 2, 3, 4}));
    EXPECT_GT(s, TypeParam({1, 4}));
    EXPECT_LE(s, TypeParam({1, 3, 4}));
    EXPECT_LT(s, TypeParam({1, 3, 5}));
  }
}

#ifdef AMC_CXX20
TYPED_TEST(SetListTest, EraseIf) {
  using ValueType = typename TypeParam::value_type;
  using SizeType = typename TypeParam::size_type;
  TypeParam s1{1, 2, 3};
  EXPECT_EQ(erase_if(s1, [](const ValueType& v) { return v > ValueType{2}; }), SizeType{1});
  EXPECT_EQ(s1, TypeParam({1, 2}));

  TypeParam s2{1, 4, 3};
  EXPECT_EQ(erase_if(s2, [](const ValueType& v) { return (v % 2) == 0; }), SizeType{1});
  EXPECT_EQ(s2, TypeParam({1, 3}));

  TypeParam s3{1, 4, 3, 7};
  EXPECT_EQ(erase_if(s3, [](const ValueType& v) { return v < ValueType{4}; }), SizeType{2});
  EXPECT_EQ(s3, TypeParam({4, 7}));

  TypeParam s4{1, 2, 3, 1, 2, 3, 4};
  EXPECT_EQ(erase_if(s4, [](const ValueType& v) { return v > ValueType{2}; }), SizeType{2});
  EXPECT_EQ(s4, TypeParam({1, 2, 1, 2}));
}
#endif

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

#ifdef AMC_SMALLSET
// If moving the elements of a SmallSet to its set fails when it grows, it keeps all its elements, and they do not
// reappear once its set becomes empty again. 'insertFailing(s, n)' inserts 5 in 's', making an operation fail after 'n'
// successful ones, and tells whether it failed.
template <class SetType, class InsertFailing>
void ExpectGrowFailureKeepsElements(InsertFailing insertFailing) {
  for (int nbSuccesses = 0;; ++nbSuccesses) {
    SetType s{4, 1, 3, 2};  // unsorted: the elements are moved to a FlatSet in another order
    if (!insertFailing(s, nbSuccesses)) {
      EXPECT_EQ(s, SetType({1, 2, 3, 4, 5}));
      break;
    }
    EXPECT_EQ(s, SetType({1, 2, 3, 4})) << "failure after " << nbSuccesses << " successes";
    s.insert(5);
    EXPECT_EQ(s, SetType({1, 2, 3, 4, 5}));
    for (int i = 1; i <= 5; ++i) {
      s.erase(i);
    }
    EXPECT_TRUE(s.empty());
  }
}

TEST(SetTest, SmallSetGrowAllocationFailureKeepsElements) {
  if (!kMallocFailureInjection) {
    GTEST_SKIP() << "allocation failures cannot be injected in this build (needs glibc, without sanitizers)";
  }
  auto insertFailing = [](auto& s, int nbSuccessfulAllocations) {
    try {
      CallWithFailingMalloc([&] { s.insert(5); }, nbSuccessfulAllocations);
    } catch (const std::bad_alloc&) {
      return true;
    }
    return false;
  };
  ExpectGrowFailureKeepsElements<SmallSet<int, 4>>(insertFailing);
  using T = ThrowingAssignmentType;  // loses its value when moved
  ExpectGrowFailureKeepsElements<SmallSet<T, 4, std::less<T>, amc::allocator<T>, FlatSet<T>>>(insertFailing);
}

TEST(SetTest, SmallSetGrowThrowingMoveKeepsElements) {
  using T = ThrowingMoveType;
  const int nbLive = T::NbLive();
  auto insertFailing = [](auto& s, int nbMoves) {
    T::NbMovesBeforeThrow() = nbMoves;
    bool thrown = false;
    try {
      s.insert(5);
    } catch (const MoveForbiddenException&) {
      thrown = true;
    }
    T::NbMovesBeforeThrow() = -1;
    return thrown;
  };
  ExpectGrowFailureKeepsElements<SmallSet<T, 4>>(insertFailing);
  ExpectGrowFailureKeepsElements<SmallSet<T, 4, std::less<T>, amc::allocator<T>, FlatSet<T>>>(insertFailing);
  EXPECT_EQ(T::NbLive(), nbLive);
}
#endif

// If appending elements to the vector of a FlatSet fails, the appended ones are erased: it stays sorted.
TEST(SetTest, FlatSetInsertAllocationFailureKeepsElements) {
  if (!kMallocFailureInjection) {
    GTEST_SKIP() << "allocation failures cannot be injected in this build (needs glibc, without sanitizers)";
  }
  using SetType = FlatSet<int>;
  SetType s{1, 2, 3, 4, 5};
  s.erase(4);
  s.erase(5);  // room for two more elements before a reallocation
  std::istringstream ss("0 10 7");
  EXPECT_THROW(CallWithFailingMalloc([&] { s.insert(std::istream_iterator<int>(ss), std::istream_iterator<int>()); }),
               std::bad_alloc);
  EXPECT_EQ(s, SetType({1, 2, 3}));
}

// If the vector of the destination set cannot grow, merge leaves both sets unchanged: it grows it before moving any
// element.
template <class SetType>
void ExpectMergeAllocationFailureKeepsSets() {
  const SetType init1{1, 3, 5};
  const SetType init2{2, 3, 4};
  SetType s1(init1);
  SetType s2(init2);
  EXPECT_THROW(CallWithFailingMalloc([&] { s1.merge(s2); }), std::bad_alloc);
  EXPECT_EQ(s1, init1);
  EXPECT_EQ(s2, init2);
}

TEST(SetTest, FlatSetMergeAllocationFailureKeepsSets) {
  if (!kMallocFailureInjection) {
    GTEST_SKIP() << "allocation failures cannot be injected in this build (needs glibc, without sanitizers)";
  }
  using T = ComplexTriviallyRelocatableType;
  // full vector, reallocated with realloc
  ExpectMergeAllocationFailureKeepsSets<FlatSet<T>>();
  // inline vector, which has room for the first new element only: merging elements one by one would move it
  ExpectMergeAllocationFailureKeepsSets<FlatSet<T, std::less<T>, amc::allocator<T>, SmallVector<T, 4>>>();
}

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
  using ValueType = typename SetType::value_type;
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
  auto irt = s.insert(std::move(nh));
  EXPECT_TRUE(irt.inserted);
  EXPECT_TRUE(irt.node.empty());
  EXPECT_TRUE(nh.empty());  // a successful insertion consumes the node
  ref.clear();
  ref.emplace(2);
  ref.emplace(17);
  ref.emplace(16);
  EXPECT_EQ(s, ref);
  EXPECT_TRUE(s.extract(5).empty());  // absent key, smaller than some present ones
  EXPECT_EQ(s, ref);

  // extraction by iterator until the set is empty (a large SmallSet becomes small again)
  nh = s.extract(s.find(16));
  ASSERT_FALSE(nh.empty());
  EXPECT_EQ(nh.value(), ValueType(16));
  ref.erase(16);
  EXPECT_EQ(s, ref);
  while (!s.empty()) {
    const int first = *s.begin();
    nh = s.extract(s.begin());
    ASSERT_FALSE(nh.empty());
    EXPECT_EQ(nh.value(), ValueType(first));
    EXPECT_FALSE(s.contains(first));
  }
  EXPECT_EQ(s.begin(), s.end());

  // extraction by iterator in the small state of all SmallSets
  s.emplace(4);
  s.emplace(1);
  nh = s.extract(s.find(4));
  ASSERT_FALSE(nh.empty());
  EXPECT_EQ(nh.value(), ValueType(4));
  ref.clear();
  ref.emplace(1);
  EXPECT_EQ(s, ref);

  // moving a node handle empties the source
  auto nh2 = std::move(nh);
  EXPECT_TRUE(nh.empty());
  EXPECT_FALSE(nh);
  ASSERT_FALSE(nh2.empty());
  EXPECT_EQ(nh2.value(), ValueType(4));
  nh = s.extract(1);
  nh = std::move(nh2);  // move assignment to a non empty node
  EXPECT_TRUE(nh2.empty());
  ASSERT_FALSE(nh.empty());
  EXPECT_EQ(nh.value(), ValueType(4));
  nh2 = std::move(nh);  // move assignment to an empty node
  EXPECT_TRUE(nh.empty());
  ASSERT_FALSE(nh2.empty());
  EXPECT_EQ(nh2.value(), ValueType(4));
  nh2 = std::move(nh);  // move assignment from an empty node
  EXPECT_TRUE(nh.empty());
  EXPECT_TRUE(nh2.empty());
  nh = s.extract(42);  // move assignment from an empty node, to an empty node
  EXPECT_TRUE(nh.empty());

  s.emplace(5);
  nh2 = s.extract(5);
  auto& self = nh2;  // assignment through a reference, avoiding self move warnings
  nh2 = std::move(self);
  ASSERT_FALSE(nh2.empty());
  EXPECT_EQ(nh2.value(), ValueType(5));
}

// Inserting a node whose value is already present fails and leaves the node untouched: it keeps its value.
TYPED_TEST(SetListExtractTest, InsertNodeAlreadyPresent) {
  using SetType = TypeParam;
  using ValueType = typename SetType::value_type;
  using SizeType = typename SetType::size_type;
  for (int nbElems : {2, 6}) {  // small and large states of SmallSets
    SetType s;
    for (int i = 0; i < nbElems; ++i) {
      s.emplace(i);
    }
    SetType o;
    o.emplace(1);

    auto nh = o.extract(1);
    auto irt = s.insert(std::move(nh));
    EXPECT_TRUE(nh.empty());  // the value has been transferred to the returned node
    EXPECT_FALSE(irt.inserted);
    ASSERT_FALSE(irt.node.empty());
    EXPECT_EQ(irt.node.value(), ValueType(1));
    EXPECT_EQ(*irt.position, ValueType(1));
    EXPECT_EQ(s.size(), static_cast<SizeType>(nbElems));

    auto it = s.insert(s.end(), std::move(irt.node));
    ASSERT_FALSE(irt.node.empty());
    EXPECT_EQ(irt.node.value(), ValueType(1));
    EXPECT_EQ(*it, ValueType(1));
    EXPECT_EQ(s.size(), static_cast<SizeType>(nbElems));

    // a successful insertion empties the node
    irt.node.value() = ValueType(nbElems + 10);
    it = s.insert(s.end(), std::move(irt.node));
    EXPECT_TRUE(irt.node.empty());
    EXPECT_EQ(*it, ValueType(nbElems + 10));
    EXPECT_EQ(s.size(), static_cast<SizeType>(nbElems + 1));

    // extracting a missing key gives an empty node, inserting an empty node does nothing
    irt = s.insert(s.extract(42));
    EXPECT_FALSE(irt.inserted);
    EXPECT_TRUE(irt.node.empty());
    EXPECT_EQ(irt.position, s.end());
    EXPECT_EQ(s.insert(s.begin(), s.extract(42)), s.end());
    EXPECT_EQ(s.size(), static_cast<SizeType>(nbElems + 1));
  }
}
#endif

template <typename T>
class SetStatefulCompareTest : public ::testing::Test {};

// clang-format off
typedef ::testing::Types<FlatSet<int32_t, ModuloCompare>,
                         FlatSet<int32_t, ModuloCompare, amc::allocator<int32_t>, SmallVector<int32_t, 4>>
#ifdef AMC_SMALLSET
                        ,SmallSet<int32_t, 2, ModuloCompare>,
                         SmallSet<int32_t, 2, ModuloCompare, amc::allocator<int32_t>, FlatSet<int32_t, ModuloCompare>>
#endif
                        >
    SetsStatefulCompareTypes;
// clang-format on

TYPED_TEST_SUITE(SetStatefulCompareTest, SetsStatefulCompareTypes, );

// Sets must use their comparator object, which may hold a state, and never a default constructed one.
TYPED_TEST(SetStatefulCompareTest, UseComparatorObject) {
  using SetType = TypeParam;
  const ModuloCompare comp(10);
  // 11, 21 and 12 are equivalent to 1, 1 and 2. 3 makes small SmallSets grow before they are inserted.
  const int32_t values[] = {1, 2, 3, 11, 21, 12};
  // sorted by value, but not by the comparator object, which must sort them
  const int32_t valuesSortedByValue[] = {2, 3, 11, 21};

  SetType rangeConstructed(std::begin(values), std::end(values), comp);
  SetType sortedRangeConstructed(std::begin(valuesSortedByValue), std::end(valuesSortedByValue), comp);
  SetType rangeInserted(comp);
  rangeInserted.insert(std::begin(values), std::end(values));
  SetType oneByOne(comp);
  for (int32_t v : values) {
    oneByOne.insert(v);
  }
  for (const SetType* s : {&rangeConstructed, &sortedRangeConstructed, &rangeInserted, &oneByOne}) {
    EXPECT_EQ(s->size(), 3U);
    EXPECT_TRUE(s->contains(31));
    EXPECT_FALSE(s->contains(4));
  }

  // Sets are compared lexicographically, in their iteration order which is defined by the comparator object
  SetType lhs(comp);  // iteration order: 11, 2
  lhs.insert(11);
  lhs.insert(2);
  SetType rhs(comp);  // iteration order: 10, 3
  rhs.insert(10);
  rhs.insert(3);
  EXPECT_TRUE(rhs < lhs);
  EXPECT_FALSE(lhs < rhs);
}

// Like std::set, the insertion of a range keeps the present elements rather than the equivalent inserted ones, wherever
// the least inserted element is: before all the present ones, equivalent to the first, to a middle or to the last one,
// or after all of them.
TYPED_TEST(SetStatefulCompareTest, InsertRangeKeepsPresentElements) {
  using SetType = TypeParam;
  using Values = std::vector<int32_t>;
  const ModuloCompare moduloTen(10);
  const Values present{2, 4, 6};
  // inserted elements equivalent to each other are also equivalent to a present one, which is kept
  for (const Values& inserted :
       {Values{11, 15, 22}, Values{12, 17, 22, 13}, Values{15, 26, 14, 34}, Values{16, 19, 36}, Values{19, 17}}) {
    SetType s(moduloTen);
    s.insert(present.begin(), present.end());
    s.insert(inserted.begin(), inserted.end());
    std::set<int32_t, ModuloCompare> expected(present.begin(), present.end(), moduloTen);
    expected.insert(inserted.begin(), inserted.end());
    EXPECT_EQ(Values(s.begin(), s.end()), Values(expected.begin(), expected.end()));
  }
}

// Comparator objects of the same type may define different orderings: merged elements must be ordered and deduplicated
// according to the comparator object of the destination set, whatever the order of the source set.
TYPED_TEST(SetStatefulCompareTest, MergeWithDifferentComparatorStates) {
  using SetType = TypeParam;
  using Values = std::vector<int32_t>;
  const ModuloCompare moduloTen(10);

  // destination compares remainders, source compares the integers themselves.
  // 11 is equivalent to 1 for the destination only, and is kept in the source.
  SetType dst(moduloTen);
  dst.insert(1);
  SetType src{3, 4, 11, 12, 25};
  dst.merge(src);
  EXPECT_EQ(Values(dst.begin(), dst.end()), Values({1, 12, 3, 4, 25}));
  EXPECT_EQ(Values(src.begin(), src.end()), Values({11}));

  // destination compares the integers themselves, source compares remainders (iteration order: 10, 21, 2, 13)
  SetType dst2{5};
  SetType src2(moduloTen);
  for (int32_t v : {13, 2, 21, 10}) {
    src2.insert(v);
  }
  dst2.merge(src2);
  EXPECT_EQ(Values(dst2.begin(), dst2.end()), Values({2, 5, 10, 13, 21}));
  EXPECT_TRUE(src2.empty());

  // same orderings
  SetType dst3(moduloTen);
  dst3.insert(1);
  dst3.insert(5);
  SetType src3(moduloTen);
  for (int32_t v : {13, 2, 21, 15, 10}) {
    src3.insert(v);
  }
  dst3.merge(src3);
  EXPECT_EQ(Values(dst3.begin(), dst3.end()), Values({10, 1, 2, 13, 5}));
  EXPECT_EQ(Values(src3.begin(), src3.end()), Values({21, 15}));
}

template <typename T>
class SetThrowingCompareTest : public ::testing::Test {};

// SmallSets of ThrowingAssignmentType, whose moved from objects lose their value, grow from the small state to their
// set (std::set or FlatSet) during the insertion.
// clang-format off
typedef ::testing::Types<FlatSet<int32_t, ThrowingCompare>,
                         FlatSet<int32_t, ThrowingCompare, amc::allocator<int32_t>, SmallVector<int32_t, 4>>
#ifdef AMC_SMALLSET
                        ,SmallSet<ThrowingAssignmentType, 2, ThrowingCompare>,
                         SmallSet<ThrowingAssignmentType, 2, ThrowingCompare, amc::allocator<ThrowingAssignmentType>,
                                  FlatSet<ThrowingAssignmentType, ThrowingCompare>>
#endif
                        >
    SetsThrowingCompareTypes;
// clang-format on

TYPED_TEST_SUITE(SetThrowingCompareTest, SetsThrowingCompareTypes, );

/// Tells whether the merge of two large SmallSets merges their std::set.
template <class SetType>
struct MergesStdSets : std::false_type {};

#ifdef AMC_SMALLSET
template <class T, std::uintmax_t N, class Compare, class Alloc>
struct MergesStdSets<SmallSet<T, N, Compare, Alloc, std::set<T, Compare, Alloc>>> : std::true_type {};
#endif

// If a comparison throws during the insertion of a range, the set stays valid (sorted, without duplicates, without
// moved from elements): it keeps all its previous elements, or it is emptied.
TYPED_TEST(SetThrowingCompareTest, InsertRangeComparisonThrowKeepsValidSet) {
  using SetType = TypeParam;
  using Values = std::vector<int32_t>;
  int& nbComparisonsBeforeThrow = ThrowingCompare::NbComparisonsBeforeThrow();
  // unsorted, interleaved with the present elements and with a duplicate: each step of the insertion can throw
  const Values inserted{3, 2, 1, 5};
  const SetType expected{1, 2, 3, 4, 5};
  for (int nbComparisons = 0;; ++nbComparisons) {
    SetType s{4, 1};  // unsorted in the small state of SmallSets
    nbComparisonsBeforeThrow = nbComparisons;
    bool thrown = false;
    try {
      s.insert(inserted.begin(), inserted.end());
    } catch (const CompareException&) {
      thrown = true;
    }
    nbComparisonsBeforeThrow = -1;
    if (!thrown) {
      EXPECT_EQ(s, expected);
      break;
    }
    const Values elems(s.begin(), s.end());
    EXPECT_EQ(s, SetType(elems.begin(), elems.end())) << "throw at comparison " << nbComparisons;
    EXPECT_TRUE(s.empty() || (s.contains(1) && s.contains(4))) << "throw at comparison " << nbComparisons;
    for (int32_t v : elems) {
      EXPECT_TRUE(expected.contains(v)) << "throw at comparison " << nbComparisons;
    }
  }
}

// If a comparison throws during a merge, both sets stay valid (sorted, without duplicates, without moved from
// elements), and each element is in exactly one of them.
TYPED_TEST(SetThrowingCompareTest, MergeComparisonThrowKeepsValidSets) {
  using SetType = TypeParam;
  using Values = std::vector<int32_t>;
  if (MergesStdSets<SetType>::value) {
    GTEST_SKIP() << "std::set::merge is noexcept in libstdc++: a throwing comparator terminates the program";
  }
  int& nbComparisonsBeforeThrow = ThrowingCompare::NbComparisonsBeforeThrow();
  // interleaved, with equivalent elements, and elements before and after all those of the other set
  const Values values1{2, 4, 5, 8, 10};
  const Values values2{0, 1, 4, 6, 8, 9, 11};
  Values allValues(values1);
  allValues.insert(allValues.end(), values2.begin(), values2.end());
  std::sort(allValues.begin(), allValues.end());
  for (int nbComparisons = 0;; ++nbComparisons) {
    SetType s1(values1.begin(), values1.end());
    SetType s2(values2.begin(), values2.end());
    nbComparisonsBeforeThrow = nbComparisons;
    bool thrown = false;
    try {
      s1.merge(s2);
    } catch (const CompareException&) {
      thrown = true;
    }
    nbComparisonsBeforeThrow = -1;
    const Values elems1(s1.begin(), s1.end());
    const Values elems2(s2.begin(), s2.end());
    EXPECT_EQ(s1, SetType(elems1.begin(), elems1.end())) << "throw at comparison " << nbComparisons;
    EXPECT_EQ(s2, SetType(elems2.begin(), elems2.end())) << "throw at comparison " << nbComparisons;
    Values elems(elems1);
    elems.insert(elems.end(), elems2.begin(), elems2.end());
    std::sort(elems.begin(), elems.end());
    EXPECT_EQ(elems, allValues) << "throw at comparison " << nbComparisons;
    if (!thrown) {
      EXPECT_EQ(s1, SetType({0, 1, 2, 4, 5, 6, 8, 9, 10, 11}));
      EXPECT_EQ(s2, SetType({4, 8}));
      break;
    }
  }
}

// If a comparison throws during an emplace, the set keeps exactly its previous elements: in particular, a SmallSet
// which constructs the new element in its small vector before comparing it does not keep it, even if it is a duplicate.
TYPED_TEST(SetThrowingCompareTest, EmplaceComparisonThrowKeepsElements) {
  using SetType = TypeParam;
  using Values = std::vector<int32_t>;
  int& nbComparisonsBeforeThrow = ThrowingCompare::NbComparisonsBeforeThrow();
  // small, small and full, and large states of SmallSets
  for (const Values& initValues : {Values{4}, Values{4, 1}, Values{4, 1, 6}}) {
    const SetType init(initValues.begin(), initValues.end());
    for (int32_t v : {1, 4, 2, 7}) {  // present or not, before or after the first element
      for (bool withHint : {false, true}) {
        for (int nbComparisons = 0;; ++nbComparisons) {
          SetType s(init);
          nbComparisonsBeforeThrow = nbComparisons;
          bool thrown = false;
          try {
            if (withHint) {
              s.emplace_hint(s.begin(), v);
            } else {
              s.emplace(v);
            }
          } catch (const CompareException&) {
            thrown = true;
          }
          nbComparisonsBeforeThrow = -1;
          if (!thrown) {
            SetType expected(init);
            expected.insert(v);
            EXPECT_EQ(s, expected) << "emplace " << v << (withHint ? " with hint" : "");
            break;
          }
          EXPECT_EQ(s, init) << "emplace " << v << (withHint ? " with hint" : "") << ", throw at comparison "
                             << nbComparisons;
        }
      }
    }
  }
}

template <typename T>
class SetLessComparableTest : public ::testing::Test {};

// clang-format off
typedef ::testing::Types<FlatSet<LessComparableType>
#ifdef AMC_SMALLSET
                        ,SmallSet<LessComparableType, 2>,
                         SmallSet<LessComparableType, 2, std::less<LessComparableType>,
                                  amc::allocator<LessComparableType>, FlatSet<LessComparableType>>
#endif
                        >
    SetsOfLessComparableTypes;
// clang-format on

TYPED_TEST_SUITE(SetLessComparableTest, SetsOfLessComparableTypes, );

// Sets of elements only providing operator< are comparable (from C++20, with an ordering synthesized from it), in the
// small and large states of SmallSets
TYPED_TEST(SetLessComparableTest, ComparisonOperators) {
  using SetType = TypeParam;
  const SetType s{4, 1, 3};
  SetType large;  // large state for all tested SmallSets
  for (int32_t i = 1; i <= 10; ++i) {
    large.insert(i);
  }
  EXPECT_EQ(s, SetType({1, 3, 4}));
  EXPECT_LT(s, SetType({1, 4}));
  EXPECT_GT(s, SetType({1, 2, 3, 4}));
  EXPECT_GT(s, large);
  EXPECT_LT(large, s);
#ifdef AMC_CXX20
  static_assert(std::is_same_v<decltype(s <=> s), std::weak_ordering>, "ordering synthesized from operator<");
  EXPECT_EQ(s <=> SetType({1, 4}), std::weak_ordering::less);
  EXPECT_EQ(s <=> SetType({3, 4, 1}), std::weak_ordering::equivalent);
  EXPECT_EQ(s <=> large, std::weak_ordering::greater);
  EXPECT_EQ(large <=> s, std::weak_ordering::less);
#endif
}

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
  EXPECT_TRUE(s.contains(0LL));
  EXPECT_FALSE(s.contains(1LL));
  EXPECT_TRUE(s.contains(7ULL));
  EXPECT_FALSE(s.contains(static_cast<char>(4)));
  EXPECT_EQ(s.count(0LL), 1U);
  EXPECT_EQ(s.count(1LL), 0U);
}

TEST(FlatSetTest, BoundsEquivalentType) {
  const FlatSet<int, std::less<>> s{1, 2, 4};
  EXPECT_EQ(s.lower_bound(1.5), s.begin() + 1);
  EXPECT_EQ(s.upper_bound(2.0), s.begin() + 2);
  EXPECT_EQ(s.lower_bound(SelfMoveUnsafeType(2)), s.begin() + 1);
  EXPECT_EQ(s.upper_bound(SelfMoveUnsafeType(2)), s.begin() + 2);
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

template <typename T>
class FlatSetBoundsTest : public ::testing::Test {};

// The bounds depend on the ordering of the comparator, on whether the search is branchless (scalar types ordered by
// std::less or std::greater), and on the iterators of the vector. Only uint32_t has enough distinct values to reach
// the size from which the branchless search prefetches.
typedef ::testing::Types<FlatSet<char>, FlatSet<char, ModuloCompare>,
                         FlatSet<uint32_t, std::greater<uint32_t>, amc::allocator<uint32_t>,
                                 std::vector<uint32_t, amc::allocator<uint32_t>>>>
    FlatSetsBoundsType;
TYPED_TEST_SUITE(FlatSetBoundsTest, FlatSetsBoundsType, );

// Same positions as std::set, for present and absent keys: the range of an absent key is empty, at the position where
// it would be inserted (and not at end(), like in {1, 3, 5}.equal_range(2))
TYPED_TEST(FlatSetBoundsTest, EqualRangeAndBounds) {
  using SetType = TypeParam;
  using ValueType = typename SetType::value_type;
  using RefSetType = std::set<ValueType, typename SetType::key_compare>;
  for (const SetType& s : {SetType(), SetType{1, 3, 5}}) {
    const RefSetType ref(s.begin(), s.end());
    for (int k = 0; k <= 6; ++k) {
      const ValueType key = static_cast<ValueType>(k);
      const auto range = s.equal_range(key);
      const auto refRange = ref.equal_range(key);
      EXPECT_EQ(std::distance(s.begin(), range.first), std::distance(ref.begin(), refRange.first)) << "key " << k;
      EXPECT_EQ(std::distance(s.begin(), range.second), std::distance(ref.begin(), refRange.second)) << "key " << k;
      EXPECT_TRUE(range.first == s.lower_bound(key)) << "key " << k;
      EXPECT_TRUE(range.second == s.upper_bound(key)) << "key " << k;
    }
  }
}

// Same positions as the standard binary searches, for all the sizes up to 70 (each number of steps of the binary search
// without prefetching, from 0 to 7), and for sizes on both sides of the one from which it prefetches, for all the keys:
// present, absent between two elements, before the first and after the last one.
TYPED_TEST(FlatSetBoundsTest, LookupsOfAllSizes) {
  using SetType = TypeParam;
  using ValueType = typename SetType::value_type;
  using SizeType = typename SetType::size_type;
  const typename SetType::key_compare comp;
  const std::size_t prefetchMinSize = flatset_details::kPrefetchMinRangeBytes / sizeof(ValueType);
  std::vector<std::size_t> sizes;
  for (std::size_t n = 0; n <= 70; ++n) {
    sizes.push_back(n);
  }
  for (std::size_t n : {prefetchMinSize - 1, prefetchMinSize, 2 * prefetchMinSize, 3 * prefetchMinSize + 7}) {
    sizes.push_back(n);
  }
  for (std::size_t n : sizes) {
    // Elements 1, 3, ..., 2n - 1, so that keys 0, 2, ..., 2n are absent
    if (2 * n > static_cast<std::size_t>(std::numeric_limits<ValueType>::max())) {
      continue;
    }
    std::vector<ValueType> sorted;
    for (std::size_t i = 0; i < n; ++i) {
      sorted.push_back(static_cast<ValueType>(2 * i + 1));
    }
    std::sort(sorted.begin(), sorted.end(), comp);
    const SetType s(sorted.begin(), sorted.end());
    ASSERT_EQ(s.size(), static_cast<SizeType>(n));
    for (std::size_t k = 0; k <= 2 * n; ++k) {
      const ValueType key = static_cast<ValueType>(k);
      const bool present = k % 2 == 1;
      const std::ptrdiff_t lb = std::lower_bound(sorted.begin(), sorted.end(), key, comp) - sorted.begin();
      const std::ptrdiff_t ub = std::upper_bound(sorted.begin(), sorted.end(), key, comp) - sorted.begin();
      ASSERT_EQ(s.lower_bound(key) - s.begin(), lb) << "size " << n << " key " << k;
      ASSERT_EQ(s.upper_bound(key) - s.begin(), ub) << "size " << n << " key " << k;
      ASSERT_EQ(s.find(key) - s.begin(), present ? lb : static_cast<std::ptrdiff_t>(n)) << "size " << n << " key " << k;
      ASSERT_EQ(s.count(key), static_cast<SizeType>(present)) << "size " << n << " key " << k;
      const auto range = s.equal_range(key);
      ASSERT_EQ(range.first - s.begin(), lb) << "size " << n << " key " << k;
      ASSERT_EQ(range.second - s.begin(), ub) << "size " << n << " key " << k;
    }
  }
}

// Sorted input is not sorted again: each element is compared with its successor, once if the elements are unique (it
// is the minimum to check that they are sorted), twice from the first duplicate on, and the insertion of greater
// elements adds the binary search of their position. Only the unsorted elements after a long sorted prefix are sorted.
TEST(FlatSetTest, SortedInputIsNotSortedAgain) {
  using SetType = FlatSet<int32_t, ThrowingCompare>;
  using Values = std::vector<int32_t>;
  constexpr int kNbElems = 1000;
  constexpr int kMaxNbBinarySearchComparisons = 11;  // ceil(log2(kNbElems + 1)), plus one for the implementation
  Values sorted;
  Values greater;
  for (int32_t i = 0; i < kNbElems; ++i) {
    sorted.push_back(i);
    greater.push_back(kNbElems + i);
  }
  Values sortedWithDuplicate(sorted);
  sortedWithDuplicate.insert(sortedWithDuplicate.begin(), 0);

  // ThrowingCompare counts down the comparisons (and never throws here)
  int& nbComparisonsLeft = ThrowingCompare::NbComparisonsBeforeThrow();
  const auto startCounting = [&nbComparisonsLeft] { nbComparisonsLeft = std::numeric_limits<int>::max(); };
  const auto nbComparisons = [&nbComparisonsLeft] { return std::numeric_limits<int>::max() - nbComparisonsLeft; };

  startCounting();
  SetType s(sorted.begin(), sorted.end());
  EXPECT_EQ(nbComparisons(), kNbElems - 1);

  startCounting();
  SetType s2(sortedWithDuplicate.begin(), sortedWithDuplicate.end());
  EXPECT_LT(nbComparisons(), 2 * (kNbElems + 1));
  EXPECT_EQ(s2, s);

  startCounting();
  s.insert(greater.begin(), greater.end());
  EXPECT_LE(nbComparisons(), kNbElems - 1 + kMaxNbBinarySearchComparisons);
  EXPECT_EQ(s.size(), static_cast<SetType::size_type>(2 * kNbElems));

  // A few unsorted elements after the sorted ones: only they are sorted, then merged with the end of the sorted ones
  Values sortedPrefix(sorted);
  const Values tail{998, 1500, 990, 1200, 995, 1500};
  sortedPrefix.insert(sortedPrefix.end(), tail.begin(), tail.end());
  startCounting();
  SetType s4(sortedPrefix.begin(), sortedPrefix.end());
  EXPECT_LT(nbComparisons(), kNbElems + 100);
  SetType expected(sorted.begin(), sorted.end());
  expected.insert(1200);
  expected.insert(1500);
  EXPECT_EQ(s4, expected);

#ifdef AMC_NONSTD_FEATURES
  using VecType = SetType::vector_type;
  startCounting();
  SetType s3(VecType(sorted.begin(), sorted.end()));
  EXPECT_EQ(nbComparisons(), kNbElems - 1);

  startCounting();
  s3 = VecType(greater.begin(), greater.end());
  EXPECT_EQ(nbComparisons(), kNbElems - 1);
  EXPECT_EQ(s3, SetType(greater.begin(), greater.end()));
#endif
  nbComparisonsLeft = -1;
}

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

// If a comparison throws during the assignment from a vector, whose elements may then be unsorted, the set is emptied.
TEST(FlatSetTest, AssignFromVectorComparisonThrowEmptiesSet) {
  using SetType = FlatSet<int32_t, ThrowingCompare>;
  using VecType = SetType::vector_type;
  int& nbComparisonsBeforeThrow = ThrowingCompare::NbComparisonsBeforeThrow();
  for (int nbComparisons = 0;; ++nbComparisons) {
    SetType s{1, 4};
    nbComparisonsBeforeThrow = nbComparisons;
    bool thrown = false;
    try {
      s = VecType{3, 2, 1, 5, 2};
    } catch (const CompareException&) {
      thrown = true;
    }
    nbComparisonsBeforeThrow = -1;
    if (!thrown) {
      EXPECT_EQ(s, SetType({1, 2, 3, 5}));
      break;
    }
    EXPECT_TRUE(s.empty()) << "throw at comparison " << nbComparisons;
  }
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
