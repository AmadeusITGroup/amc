#pragma once

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <type_traits>
#include <utility>

#include "allocator.hpp"
#include "config.hpp"
#include "memory.hpp"
#include "type_traits.hpp"
#include "vector.hpp"

#ifdef AMC_CXX14
#include "istransparent.hpp"
#ifdef AMC_CXX17
#include "setnode.hpp"
#ifdef AMC_CXX20
#include "algorithm.hpp"
#endif
#ifdef AMC_CXX23
#include <ranges>
#endif
#endif
#endif

namespace amc {

namespace flatset_details {

/// std::less and std::greater, transparent or of a scalar type: they compare scalar values with the built-in operators.
template <class Compare>
struct IsBuiltinComparator : std::false_type {};

template <class U>
struct IsBuiltinComparator<std::less<U>>
    : std::integral_constant<bool, std::is_void<U>::value || std::is_scalar<U>::value> {};

template <class U>
struct IsBuiltinComparator<std::greater<U>>
    : std::integral_constant<bool, std::is_void<U>::value || std::is_scalar<U>::value> {};

/// Tells whether the binary searches of keys of type 'K' among elements of type 'T' ordered by 'Compare' use
/// BranchlessPartitionPoint. It is only faster than the standard binary searches when comparisons are cheap: scalar
/// values (arithmetic, enumeration, pointer) compared with the built-in operators. It is slower for std::string, or
/// with a comparator dereferencing pointers.
template <class T, class K, class Compare>
struct UseBranchlessSearch : std::integral_constant<bool, std::is_scalar<T>::value && std::is_scalar<K>::value &&
                                                              IsBuiltinComparator<Compare>::value> {};

/// Ranges smaller than this size are searched without prefetching: they likely fit in the L1 data cache, where the
/// prefetches would only cost instructions.
AMC_INLINE_VARIABLE constexpr std::size_t kPrefetchMinRangeBytes = 64U * 1024U;

/// The prefetching stops when the remaining range is smaller than this size: its few cache lines are already being
/// loaded.
AMC_INLINE_VARIABLE constexpr std::size_t kPrefetchStopRangeBytes = 256U;

/// Unsorted input starting with a sorted prefix of at least two thirds of its elements is not sorted at once: the other
/// elements are sorted, then merged with the prefix. Smaller input is always sorted at once: std::sort is then faster
/// than the allocation of the buffer of std::inplace_merge.
AMC_INLINE_VARIABLE constexpr std::size_t kMinSizeToMergeSortedPrefix = 32U;

/// Returns 'it + offset' if 'cond', 'it' otherwise, without a branch, which would be mispredicted half of the time on a
/// random condition.
template <class RandomIt>
RandomIt AdvanceIf(RandomIt it, typename std::iterator_traits<RandomIt>::difference_type offset, bool cond) {
#ifdef AMC_UNPREDICTABLE
  // Clang compiles this 'if' into a conditional move, but turns the arithmetic below into a branch
  if (AMC_UNPREDICTABLE(cond)) {
    it += offset;
  }
  return it;
#else
  // GCC compiles the 'if' above into a branch (even with __builtin_expect_with_probability(cond, 1, 0.5))
  return it + (offset & -static_cast<typename std::iterator_traits<RandomIt>::difference_type>(cond));
#endif
}

/// Returns the first iterator 'it' of [first, first + n) for which 'pred(*it)' is false, the range being partitioned by
/// 'pred' (the elements for which it is true come first), like std::partition_point.
/// The next half of the range is selected with a conditional move instead of a branch, which would be mispredicted half
/// of the time. As the next element to compare then depends on the result of the current comparison, in large ranges
/// the elements of the next two steps (4 candidates) are prefetched, so that the latencies of their loads overlap.
/// 'pred' is called ceil(log2(n + 1)) times, as many times as std::partition_point in its worst case.
template <class RandomIt, class Pred>
RandomIt BranchlessPartitionPoint(RandomIt first, std::size_t n, Pred pred) {
  using diff_type = typename std::iterator_traits<RandomIt>::difference_type;
  using value_type = typename std::iterator_traits<RandomIt>::value_type;
  static_assert(kPrefetchStopRangeBytes / sizeof(value_type) >= 4U,
                "the prefetching loop needs at least 5 positions for the prefetched elements to be in the range");
  // Invariant: the partition point is one of the 'len' positions [first, first + len - 1]. Each step compares the
  // element before first + half (half < len) and keeps either [first, first + half - 1] or [first + half, first + len
  // - 1]: both fit in 'len - half' positions from their first one, so 'len' does not depend on the comparison either.
  std::size_t len = n + 1U;
  if (len > kPrefetchMinRangeBytes / sizeof(value_type)) {
    do {
      const std::size_t half = len / 2;
      len -= half;
      // The step after the next one compares the element before first + half2, plus 0 or half1 (next step), plus 0 or
      // half: as len >= 3 here, 1 <= half2 and half1 + half2 < len.
      const std::size_t half1 = len / 2;
      const std::size_t half2 = (len - half1) / 2;
      AMC_PREFETCH(std::addressof(first[static_cast<diff_type>(half2 - 1U)]));
      AMC_PREFETCH(std::addressof(first[static_cast<diff_type>(half1 + half2 - 1U)]));
      AMC_PREFETCH(std::addressof(first[static_cast<diff_type>(half + half2 - 1U)]));
      AMC_PREFETCH(std::addressof(first[static_cast<diff_type>(half + half1 + half2 - 1U)]));
      first = AdvanceIf(first, static_cast<diff_type>(half), pred(first[static_cast<diff_type>(half - 1U)]));
    } while (len > kPrefetchStopRangeBytes / sizeof(value_type));
  }
  while (len > 1U) {
    const std::size_t half = len / 2;
    len -= half;
    first = AdvanceIf(first, static_cast<diff_type>(half), pred(first[static_cast<diff_type>(half - 1U)]));
  }
  return first;
}

/// Tells whether 'VecType' is one of the amc vectors (amc::vector, SmallVector, FixedCapacityVector), whose storage
/// FlatSet can fill itself (see Vector).
template <class VecType>
struct IsAmcVector : std::false_type {};

template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N>
struct IsAmcVector<Vector<T, Alloc, SizeType, GrowingPolicy, N>> : std::true_type {};

#ifdef AMC_CXX14
template <class T>
using IsFinal = std::is_final<T>;
#else
/// std::is_final is C++14: in C++11, its compiler intrinsic is used (GCC and Clang, MSVC has no C++11 mode).
template <class T>
struct IsFinal : std::integral_constant<bool, __is_final(T)> {};
#endif

/// Holds the comparator of a FlatSet. Classes are derived from, so that empty ones (the usual case) take no space
/// (empty base optimization). Like with std::set, the comparator may also be a function pointer or a final class,
/// which cannot be derived from: it is then held as a member.
/// Like the one of std::set, the comparator is value initialized by default: a function pointer is then null.
template <class Compare, bool = std::is_class<Compare>::value && !IsFinal<Compare>::value>
class CompareHolder : private Compare {
 public:
  template <class C = Compare, typename std::enable_if<std::is_default_constructible<C>::value, bool>::type = true>
  CompareHolder() noexcept(std::is_nothrow_default_constructible<C>::value) : Compare() {}

  explicit CompareHolder(const Compare& comp) : Compare(comp) {}

  Compare& get() noexcept { return *this; }
  const Compare& get() const noexcept { return *this; }
};

template <class Compare>
class CompareHolder<Compare, false> {
 public:
  template <class C = Compare, typename std::enable_if<std::is_default_constructible<C>::value, bool>::type = true>
  CompareHolder() noexcept(std::is_nothrow_default_constructible<C>::value) : _comp() {}

  explicit CompareHolder(const Compare& comp) : _comp(comp) {}

  Compare& get() noexcept { return _comp; }
  const Compare& get() const noexcept { return _comp; }

 private:
  Compare _comp;
};

}  // namespace flatset_details

/**
 * Set which keeps its elements into a sorted vector container.
 * The disadvantages of a sorted vector are linear-time insertions and linear-time deletions.
 * In exchange, a FlatSet offers a significant performance boost for all other operations,
 * with a much smaller memory impact.
 *
 * Detailed features and characteristics:
 * - Faster lookup than standard associative containers. The binary searches of scalar types ordered by std::less or
 * std::greater are branchless, and prefetch their next steps in large sets.
 * - Much faster iteration than standard associative containers. Random-access iterators instead of bidirectional
 * iterators.
 * - Less memory consumption
 * - Improved cache performance (data is stored in contiguous memory)
 * - Non-stable iterators (iterators are invalidated when inserting and erasing elements)
 * - Non-copyable and non-movable values types can't be stored
 * - Weaker exception safety than standard associative containers (copy/move constructors can throw when shifting values
 * in erasures and insertions)
 * - Slower insertion and erasure than standard associative containers (specially for non-movable types)
 *
 * Consider usage of a FlatSet when read-only operations are favored against insertions / deletions.
 *
 * It does not allow duplicated elements.
 * Elements a, b are considered the same if !Compare(a, b) && !Compare(b, a)
 */
template <class T, class Compare = std::less<T>, class Alloc = amc::allocator<T>, class VecType = amc::vector<T, Alloc>>
class FlatSet : private flatset_details::CompareHolder<Compare> {
 private:
  using CompareBase = flatset_details::CompareHolder<Compare>;

 public:
  using key_type = T;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using size_type = typename VecType::size_type;
  using iterator = typename VecType::const_iterator;
  using const_iterator = typename VecType::const_iterator;
  // Like the ones of std::set, 'reference' and 'pointer' are not const, but the elements are only accessible through
  // const ones (iterators included): modifying them could break the order. Raw pointers, as 'Alloc' may be the empty
  // allocator of a FixedCapacityVector.
  using reference = value_type&;
  using const_reference = const value_type&;
  using pointer = value_type*;
  using const_pointer = const value_type*;
  using key_compare = Compare;
  using value_compare = Compare;
  using allocator_type = Alloc;

  static_assert(std::is_same<T, typename VecType::value_type>::value, "Vector value type should be T");
  static_assert(std::is_same<Alloc, typename VecType::allocator_type>::value, "Allocator should match vector's");

#ifdef AMC_CXX17
  /// Shared with the FlatSets and SmallSets of the same value and allocator types (see set_details::NodeHandle).
  using node_type = set_details::NodeHandle<T, Alloc>;
  using insert_return_type = set_details::InsertReturnType<iterator, node_type>;
#endif

  using trivially_relocatable =
      typename std::conditional<is_trivially_relocatable<Compare>::value && is_trivially_relocatable<VecType>::value,
                                std::true_type, std::false_type>::type;

  key_compare key_comp() const { return compRef(); }
  value_compare value_comp() const { return compRef(); }
  allocator_type get_allocator() const { return _sortedVector.get_allocator(); }

  FlatSet() noexcept(std::is_nothrow_default_constructible<Compare>::value &&
                     std::is_nothrow_default_constructible<VecType>::value) = default;

  explicit FlatSet(const Compare& comp, const Alloc& alloc = Alloc()) : CompareBase(comp), _sortedVector(alloc) {}

  explicit FlatSet(const Alloc& alloc) : _sortedVector(alloc) {}

  /// Linear time if [first, last) is already sorted by 'comp': it is not sorted again.
  template <class InputIt>
  FlatSet(InputIt first, InputIt last, const Compare& comp = Compare(), const Alloc& alloc = Alloc())
      : CompareBase(comp), _sortedVector(first, last, alloc) {
    eraseDuplicates(sortIfUnsorted(mbegin()));
  }

  template <class InputIt>
  FlatSet(InputIt first, InputIt last, const Alloc& alloc) : FlatSet(first, last, Compare(), alloc) {}

  FlatSet(const FlatSet& o, const Alloc& alloc) : CompareBase(o.compRef()), _sortedVector(o._sortedVector, alloc) {}

  /// Like the move constructor, moves the comparator. If 'alloc' is not equal to the allocator of 'o', the elements are
  /// moved one by one (by the vector).
  FlatSet(FlatSet&& o,
          const Alloc& alloc) noexcept(std::is_nothrow_move_constructible<Compare>::value &&
                                       std::is_nothrow_constructible<VecType, VecType&&, const Alloc&>::value)
      : CompareBase(std::move(static_cast<CompareBase&>(o))), _sortedVector(std::move(o._sortedVector), alloc) {}

  FlatSet(std::initializer_list<value_type> list, const Compare& comp = Compare(), const Alloc& alloc = Alloc())
      : CompareBase(comp), _sortedVector(alloc) {
    insert(list.begin(), list.end());
  }

  FlatSet(std::initializer_list<value_type> list, const Alloc& alloc) : FlatSet(list, Compare(), alloc) {}

 private:
  // The exception specifications of the defaulted moves must be the ones they would have if they were implicit.
  using IsNothrowMoveConstructible =
      std::integral_constant<bool, std::is_nothrow_move_constructible<Compare>::value &&
                                       std::is_nothrow_move_constructible<VecType>::value>;
  using IsNothrowMoveAssignable = std::integral_constant<bool, std::is_nothrow_move_assignable<Compare>::value &&
                                                                   std::is_nothrow_move_assignable<VecType>::value>;

 public:
  FlatSet(const FlatSet&) = default;

  FlatSet(FlatSet&&) noexcept(IsNothrowMoveConstructible::value) = default;

  /// If an exception is thrown, the set is emptied: assigning the elements one by one could otherwise leave a mix of
  /// the previous and new elements, unsorted or with duplicates.
  FlatSet& operator=(const FlatSet& o) {
    if (this != &o) {
      try {
        compRef() = o.compRef();
        _sortedVector = o._sortedVector;
      } catch (...) {
        _sortedVector.clear();
        throw;
      }
    }
    return *this;
  }

  /// If a move of an element throws, both sets are emptied, as they could otherwise keep moved from elements, or a mix
  /// of the previous and new elements.
  FlatSet& operator=(FlatSet&& o) noexcept(IsNothrowMoveAssignable::value) {
    if (this != &o) {
      moveAssign(o, IsNothrowMoveAssignable());
    }
    return *this;
  }

#ifdef AMC_NONSTD_FEATURES
  using vector_type = VecType;

  /// Non standard constructor of a FlatSet from a Vector, stealing its dynamic memory.
  /// Linear time if 'v' is already sorted by 'comp': it is not sorted again.
  explicit FlatSet(vector_type&& v, const Compare& comp = Compare(), const Alloc& alloc = Alloc())
      : CompareBase(comp), _sortedVector(std::move(v), alloc) {
    eraseDuplicates(sortIfUnsorted(mbegin()));
  }

  /// Linear time if 'v' is already sorted: it is not sorted again.
  /// If the comparator or a move of an element throws, the set is emptied.
  FlatSet& operator=(vector_type&& v) {
    _sortedVector = std::move(v);
    try {
      eraseDuplicates(sortIfUnsorted(mbegin()));
    } catch (...) {
      _sortedVector.clear();
      throw;
    }
    return *this;
  }
#endif

  /// If an exception is thrown, the set is emptied.
  FlatSet& operator=(std::initializer_list<value_type> list) {
    _sortedVector.clear();
    insert(list.begin(), list.end());
    return *this;
  }

  const_iterator begin() const noexcept { return _sortedVector.begin(); }
  const_iterator end() const noexcept { return _sortedVector.end(); }
  const_iterator cbegin() const noexcept { return _sortedVector.begin(); }
  const_iterator cend() const noexcept { return _sortedVector.end(); }

  const_reference front() const { return _sortedVector.front(); }
  const_reference back() const { return _sortedVector.back(); }

  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_reverse_iterator = std::reverse_iterator<const_iterator>;

  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  const_reverse_iterator crbegin() const noexcept { return const_reverse_iterator(cend()); }
  const_reverse_iterator crend() const noexcept { return const_reverse_iterator(cbegin()); }

#ifdef AMC_NONSTD_FEATURES
  const_pointer data() const noexcept { return _sortedVector.data(); }

  const_reference operator[](size_type idx) const { return _sortedVector[idx]; }

  const_reference at(size_type idx) const { return _sortedVector.at(idx); }

  size_type capacity() const noexcept { return _sortedVector.capacity(); }
  void reserve(size_type size) { _sortedVector.reserve(size); }

  void shrink_to_fit() { _sortedVector.shrink_to_fit(); }
#endif

  bool empty() const noexcept { return _sortedVector.empty(); }
  size_type size() const noexcept { return _sortedVector.size(); }
  size_type max_size() const noexcept { return _sortedVector.max_size(); }

  void clear() noexcept { _sortedVector.clear(); }

  std::pair<iterator, bool> insert(const T& v) { return insert_val(v); }

  std::pair<iterator, bool> insert(T&& v) { return insert_val(std::move(v)); }

  iterator insert(const_iterator hint, const T& v) { return insert_hint(hint, v); }

  iterator insert(const_iterator hint, T&& v) { return insert_hint(hint, std::move(v)); }

  /// The inserted elements are not sorted if they already are.
  /// If an exception is thrown (by the comparator, or by a copy or a move of an element), the set either keeps only its
  /// previous elements, or is emptied.
  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    appendAndMerge([&] { _sortedVector.insert(_sortedVector.end(), first, last); });
  }

  /// Same exception guarantee as the insertion of a range.
  void insert(std::initializer_list<value_type> ilist) { insert(ilist.begin(), ilist.end()); }

#ifdef AMC_CXX17
  /// 'nh' is left empty. If the insertion fails (equivalent element already present), the returned node owns its value.
  /// If an exception is thrown (by the comparator, or by an allocation), 'nh' keeps its value.
  insert_return_type insert(node_type&& nh) { return node_type::InsertInto(*this, std::move(nh)); }

  /// If the insertion fails (equivalent element already present), 'nh' is unchanged.
  iterator insert(const_iterator hint, node_type&& nh) { return node_type::InsertInto(*this, hint, std::move(nh)); }
#endif

#ifdef AMC_CXX23
  /// 'rg' may not be common (its end may be a sentinel of another type than its iterator).
  /// Same exception guarantee as the insertion of a range of iterators.
  template <class R>
  void insert_range(R&& rg) {
    if constexpr (std::ranges::common_range<R> && std::ranges::forward_range<R>) {
      insert(std::ranges::begin(rg), std::ranges::end(rg));
    } else {
      appendAndMerge([&] {
        const auto last = std::ranges::end(rg);
        for (auto first = std::ranges::begin(rg); first != last; ++first) {
          _sortedVector.emplace_back(*first);
        }
      });
    }
  }
#endif

  template <class... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    return insert(T(std::forward<Args&&>(args)...));
  }

  template <class... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    return insert(hint, T(std::forward<Args&&>(args)...));
  }

  const_iterator find(const_reference k) const {
    const_iterator lbIt = lower_bound(k);
    return lbIt == cend() || compRef()(k, *lbIt) ? cend() : lbIt;
  }

  bool contains(const_reference k) const { return find(k) != end(); }

  size_type count(const_reference k) const { return contains(k); }

#ifdef AMC_CXX14
  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  const_iterator find(const K& k) const {
    const_iterator lbIt = lower_bound(k);
    return lbIt == cend() || compRef()(k, *lbIt) ? cend() : lbIt;
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  bool contains(const K& k) const {
    return find(k) != end();
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  size_type count(const K& k) const {
    return contains(k);
  }
#endif

  size_type erase(const_reference v) {
    const_iterator it = find(v);
    if (it == end()) {
      return 0U;
    }
    _sortedVector.erase(it);
    return 1U;
  }

  iterator erase(const_iterator position) { return _sortedVector.erase(position); }
  iterator erase(const_iterator first, const_iterator last) { return _sortedVector.erase(first, last); }

  bool operator==(const FlatSet& o) const { return _sortedVector == o._sortedVector; }
  bool operator!=(const FlatSet& o) const { return !(*this == o); }

#ifdef AMC_CXX20
  /// Like the one of std::set, only defined for elements which 'synth_three_way' can compare.
  auto operator<=>(const FlatSet& o) const
    requires algorithm_details::SynthThreeWayComparable<T>
  {
    return _sortedVector <=> o._sortedVector;
  }
#else
  bool operator<(const FlatSet& o) const { return _sortedVector < o._sortedVector; }

  bool operator<=(const FlatSet& o) const { return !(o < *this); }
  bool operator>(const FlatSet& o) const { return o < *this; }
  bool operator>=(const FlatSet& o) const { return !(*this < o); }
#endif

  /// Like the one of std::set, the comparators are swapped with their swap found by argument dependent lookup (or
  /// std::swap), which is the one whose exception specification 'amc::is_nothrow_swappable' checks.
  void swap(FlatSet& o) noexcept(noexcept(std::declval<VecType>().swap(std::declval<VecType&>())) &&
                                 amc::is_nothrow_swappable<Compare>::value) {
    using std::swap;
    swap(compRef(), o.compRef());
    _sortedVector.swap(o._sortedVector);
  }

#ifdef AMC_CXX17
  node_type extract(const_iterator position) {
    // the iterators of the vector may not be pointers (std::vector): no const_cast
    node_type nt(std::move(*(mbegin() + (position - begin()))), get_allocator());
    _sortedVector.erase(position);
    return nt;
  }

  node_type extract(const key_type& key) {
    miterator it = mfind(key);
    node_type nh(get_allocator());
    if (it != _sortedVector.end()) {
      nh._optV = std::move(*it);
      _sortedVector.erase(it);
    }
    return nh;
  }
#endif

  template <class C2, typename std::enable_if<!std::is_same<Compare, C2>::value, bool>::type = true>
  void merge(FlatSet<T, C2, Alloc, VecType>& o) {
    mergeOneByOne(o);
  }

  /// Moves the elements of 'o' which are not already present in 'this': like with std::set, the ones equivalent to an
  /// element of 'this' stay in 'o'.
  /// If 'o' is ordered by our comparator (always the case for a stateless one) and the sum of both sizes does not
  /// exceed max_size(), the merge of trivially relocatable elements stored in an amc vector is linear, and an
  /// allocation failure leaves both sets unchanged. If the comparator throws, the elements already moved stay in 'this'
  /// and the other ones in 'o': both sets are valid.
  void merge(FlatSet& o) {
    if (this == &o) {
      return;  // all the elements are already present
    }
    // The merges of sorted sets require 'o' to be ordered by our comparator. This is always the case for stateless
    // comparators, but stateful ones of the same type may define different orderings (and equivalences).
    if (!o.isStrictlySortedBy(compRef(), std::is_empty<Compare>())) {
      mergeOneByOne(o);
      return;
    }
    mergeSorted(o, RelocatingMerge());
  }

  /// Like std::set, a temporary can be merged. Its elements which are already present are destroyed with it.
  template <class C2, typename std::enable_if<!std::is_same<Compare, C2>::value, bool>::type = true>
  void merge(FlatSet<T, C2, Alloc, VecType>&& o) {
    mergeOneByOne(o);
  }

  /// Like std::set, a temporary can be merged. Its elements which are already present are destroyed with it.
  void merge(FlatSet&& o) { merge(o); }

  /// Like std::set, the range of an absent key is empty and positioned where the key would be inserted.
  std::pair<const_iterator, const_iterator> equal_range(const key_type& key) const {
    const_iterator first = lower_bound(key);
    // keys are unique: the range holds at most the element found by lower_bound
    const_iterator second = first == end() || compRef()(key, *first) ? first : std::next(first);
    return std::pair<const_iterator, const_iterator>(first, second);
  }

  const_iterator lower_bound(const_reference v) const { return lowerBound(begin(), end(), v); }
  const_iterator upper_bound(const_reference v) const { return upperBound(v); }

#ifdef AMC_CXX14
  /// Unlike a key_type, a key of another type may be equivalent to several elements (like with std::set).
  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  std::pair<const_iterator, const_iterator> equal_range(const K& k) const {
    const const_iterator first = lower_bound(k);
    return std::pair<const_iterator, const_iterator>(first, std::upper_bound(first, end(), k, compRef()));
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  const_iterator lower_bound(const K& k) const {
    return lowerBound(begin(), end(), k);
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  const_iterator upper_bound(const K& k) const {
    return upperBound(k);
  }
#endif

#ifdef AMC_NONSTD_FEATURES
  /// Get the underlying vector of elements of this FlatSet, by returning a newly move constructed vector.
  /// After this call, the FlatSet will be empty.
  vector_type steal_vector() {
    vector_type ret(std::move(_sortedVector));
    // Make sure our sorted vector is cleared to avoid unspecified state (not necessarily in order)
    _sortedVector.clear();
    return ret;
  }
#endif

#ifdef AMC_CXX20
  /// Like the iterators of a set, the predicate only has a const access to the elements.
  template <class Pred>
  friend size_type erase_if(FlatSet& c, Pred pred) {
    return erase_if(c._sortedVector, [&pred](const_reference v) { return pred(v); });
  }
#endif

 private:
  template <class, class, class, class>
  friend class FlatSet;

  // SmallSet moves its elements, already sorted and unique, directly to our vector when it grows
  template <class, std::uintmax_t, class, class, class>
  friend class SmallSet;

  using miterator = typename VecType::iterator;  // Custom non const iterator to allow modification of content

  void moveAssign(FlatSet& o, std::true_type) noexcept {
    compRef() = std::move(o.compRef());
    _sortedVector = std::move(o._sortedVector);
  }

  void moveAssign(FlatSet& o, std::false_type) {
    try {
      compRef() = std::move(o.compRef());
      _sortedVector = std::move(o._sortedVector);
    } catch (...) {
      _sortedVector.clear();
      o._sortedVector.clear();
      throw;
    }
  }

  miterator mbegin() noexcept { return _sortedVector.begin(); }
  miterator mend() noexcept { return _sortedVector.end(); }

  miterator mfind(const_reference v) { return mbegin() + (find(v) - begin()); }

  /// First element of [first, last) which is not less than 'k' (std::lower_bound).
  template <class K>
  const_iterator lowerBound(const_iterator first, const_iterator last, const K& k) const {
    return lowerBound(first, last, k, flatset_details::UseBranchlessSearch<T, K, Compare>());
  }

  template <class K>
  const_iterator lowerBound(const_iterator first, const_iterator last, const K& k, std::true_type) const {
    const Compare& comp = compRef();
    return flatset_details::BranchlessPartitionPoint(first, static_cast<std::size_t>(last - first),
                                                     [&comp, &k](const_reference v) { return comp(v, k); });
  }

  template <class K>
  const_iterator lowerBound(const_iterator first, const_iterator last, const K& k, std::false_type) const {
    return std::lower_bound(first, last, k, compRef());
  }

  /// First element which is greater than 'k' (std::upper_bound).
  template <class K>
  const_iterator upperBound(const K& k) const {
    return upperBound(k, flatset_details::UseBranchlessSearch<T, K, Compare>());
  }

  template <class K>
  const_iterator upperBound(const K& k, std::true_type) const {
    const Compare& comp = compRef();
    return flatset_details::BranchlessPartitionPoint(begin(), static_cast<std::size_t>(size()),
                                                     [&comp, &k](const_reference v) { return !comp(k, v); });
  }

  template <class K>
  const_iterator upperBound(const K& k, std::false_type) const {
    return std::upper_bound(begin(), end(), k, compRef());
  }

  template <class V>
  std::pair<iterator, bool> insert_val(V&& v) {
    const_iterator insertIt = lowerBound(begin(), end(), v);
    bool newValue = insertIt == end() || compRef()(v, *insertIt);
    if (newValue) {
      insertIt = _sortedVector.insert(insertIt, std::forward<V>(v));
    }
    return std::pair<iterator, bool>(insertIt, newValue);
  }

  template <class V>
  iterator insert_hint(const_iterator hint, V&& v) {
    const_iterator b = begin();
    const_iterator e = end();
    assert(hint >= b && hint <= e);
    if (hint == e || !compRef()(*hint, v)) {  // [v, right side) is sorted
      // only used if hint != b: there is no iterator before b
      const_iterator prevIt = hint == b ? b : std::prev(hint);
      if (hint == b || !compRef()(v, *prevIt)) {  // (left side, v] is sorted
        // hint is correct, but we need to check if equal
        if (hint != e && !compRef()(v, *hint)) {
          // *hint == v, do not insert v, return iterator pointing to value that prevented the insert
          return hint;
        }
        if (hint != b && !compRef()(*prevIt, v)) {
          // *prevIt == v, do not insert v, return iterator pointing to value that prevented the insert
          return prevIt;
        }
        // hint is correct and element not already present, insert
        return _sortedVector.insert(hint, std::forward<V>(v));
      } else {
        const_iterator insertIt = lowerBound(b, prevIt, v);
        if (insertIt == prevIt || compRef()(v, *insertIt)) {
          return _sortedVector.insert(insertIt, std::forward<V>(v));
        }
        return insertIt;  // no insert as equal elements
      }
    } else {
      const_iterator nextIt = std::next(hint);
      if (nextIt == e || !compRef()(*nextIt, v)) {  // [*nextIt, right side) is sorted
        if (nextIt != e && !compRef()(v, *nextIt)) {
          return nextIt;
        }
        return _sortedVector.insert(nextIt, std::forward<V>(v));
      }
    }
    // hint does not bring any valuable information, use standard insert
    return insert(std::forward<V>(v)).first;
  }

  /// Appends elements to the sorted vector with 'append', then sorts them (unless they already are) and merges them
  /// with the previous ones. Only the previous elements which are not less than the smallest appended one are merged
  /// and checked for duplicates: appending k elements greater than all the n previous ones costs O(log(n) + k log(k)),
  /// and O(log(n) + k) if they are already sorted.
  /// If 'append', the sort of the appended elements or the search of their merge position throws, the appended elements
  /// are erased: the previous ones are kept. If the merge or the erasure of duplicates throws, the elements may not be
  /// sorted anymore: the set is cleared.
  template <class Append>
  void appendAndMerge(Append append) {
    const difference_type oldSize = static_cast<difference_type>(size());
    miterator first{};
    miterator middle{};
    miterator firstDuplicateCandidate{};
    try {
      append();
      middle = mbegin() + oldSize;
      if (middle == mend()) {
        return;
      }
      // sort appended elements only (beginning is already sorted)
      firstDuplicateCandidate = sortIfUnsorted(middle);
      // The previous elements before 'first' are less than all the appended ones: they keep their positions, and none
      // is equivalent to its successor. The merge being stable, a previous element equivalent to an appended one stays
      // before it, and is the one kept by the erasure of duplicates.
      first = mbegin() + (lowerBound(begin(), middle, *middle) - begin());
    } catch (...) {
      _sortedVector.erase(mbegin() + oldSize, mend());
      throw;
    }
    try {
      std::inplace_merge(first, middle, mend(), compRef());
      // If the appended elements are all greater than the previous ones, the merge leaves them in place: only those
      // from 'firstDuplicateCandidate' may be equivalent to their successor.
      eraseDuplicates(first == middle ? firstDuplicateCandidate : first);
    } catch (...) {
      _sortedVector.clear();
      throw;
    }
  }

  Compare& compRef() noexcept { return CompareBase::get(); }
  const Compare& compRef() const noexcept { return CompareBase::get(); }

  /// Moves each element of 'o' which is not already present in 'this' at its position according to our comparator,
  /// whatever the order of 'o'.
  template <class C2>
  void mergeOneByOne(FlatSet<T, C2, Alloc, VecType>& o) {
    for (miterator oit = o.mbegin(); oit != o.mend();) {
      const_iterator lbIt = lowerBound(begin(), end(), *oit);
      if (lbIt == end()) {
        _sortedVector.push_back(std::move(*oit));
        oit = o._sortedVector.erase(oit);
      } else if (compRef()(*oit, *lbIt)) {
        _sortedVector.insert(lbIt, std::move(*oit));
        oit = o._sortedVector.erase(oit);
      } else {
        // equal
        ++oit;
      }
    }
  }

  /// Whether the merge of a sorted set relocates the elements itself, with memcpy, in the storages of the vectors.
  using RelocatingMerge =
      std::integral_constant<bool, is_trivially_relocatable<T>::value && flatset_details::IsAmcVector<VecType>::value>;

  /// Merges 'o', ordered by our comparator, by inserting each of its new elements in our vector and erasing it from
  /// 'o': both shift the next elements.
  void mergeSorted(FlatSet& o, std::false_type) {
    // Do not use std::inplace_merge to avoid allocating memory if not needed
    miterator first1 = mbegin(), last1 = mend();
    miterator first2 = o.mbegin(), last2 = o.mend();
    while (first2 != last2) {
      if (first1 == last1) {
        _sortedVector.insert(last1, std::make_move_iterator(first2), std::make_move_iterator(last2));
        o._sortedVector.erase(first2, last2);
        break;
      }
      if (compRef()(*first1, *first2)) {
        ++first1;
      } else if (compRef()(*first2, *first1)) {
        first1 = std::next(_sortedVector.insert(first1, std::move(*first2)));
        last1 = mend();
        first2 = o._sortedVector.erase(first2);
        last2 = o.mend();  // the erasure invalidates the end iterator
      } else {
        // equal
        ++first1;
        ++first2;
      }
    }
  }

  /// Merges 'o', ordered by our comparator, in linear time and in place. Our vector first grows to the sum of both
  /// sizes (if it throws, nothing changes). Then, from the ends of both sets, the greatest remaining element is
  /// relocated before the merged ones, at the end of our storage, and an element of 'o' equivalent to one of ours
  /// before the kept ones, at the end of the storage of 'o'. The merged elements are finally relocated after our
  /// remaining ones (if there are equivalent elements), and the kept ones after the remaining ones of 'o'. Relocations
  /// cannot throw: if the comparator throws, this last step leaves both sets valid.
  void mergeSorted(FlatSet& o, std::true_type) {
    const size_type size1 = size();
    const size_type size2 = o.size();
    const std::uintmax_t maxMergedSize = static_cast<std::uintmax_t>(size1) + static_cast<std::uintmax_t>(size2);
    if (maxMergedSize > static_cast<std::uintmax_t>(max_size())) {
      // Both sets do not fit in our storage, but their union may
      mergeSorted(o, std::false_type());
      return;
    }
    _sortedVector.adjustCapacity(maxMergedSize);
    pointer first1 = _sortedVector.data();
    pointer first2 = o._sortedVector.data();
    pointer last1 = first1 + size1;  // end of our elements not merged yet
    pointer end1 = last1 + size2;
    pointer end2 = first2 + size2;
    pointer last2 = end2;   // end of the elements of 'o' not merged yet
    pointer merged = end1;  // merged elements are [merged, end1)
    pointer kept = end2;    // elements kept in 'o' are [kept, end2)
    auto commit = [&]() noexcept {
      if (merged != last1) {
        amc::uninitialized_relocate(merged, end1, last1);
      }
      if (kept != last2) {
        amc::uninitialized_relocate(kept, end2, last2);
      }
      _sortedVector.setSize(static_cast<size_type>((last1 - first1) + (end1 - merged)));
      o._sortedVector.setSize(static_cast<size_type>((last2 - first2) + (end2 - kept)));
    };
    const Compare& comp = compRef();
    try {
      while (last1 != first1 && last2 != first2) {
        if (comp(last1[-1], last2[-1])) {
          amc::relocate_at(--last2, --merged);
        } else {
          if (!comp(last2[-1], last1[-1])) {
            amc::relocate_at(--last2, --kept);  // equivalent elements
          }
          amc::relocate_at(--last1, --merged);
        }
      }
    } catch (...) {
      commit();
      throw;
    }
    // the remaining elements of 'o' are less than all the merged ones
    merged -= last2 - first2;
    amc::uninitialized_relocate(first2, last2, merged);
    last2 = first2;
    commit();
  }

  /// Tells whether each element is strictly less than its successor according to 'comp'.
  bool isStrictlySortedBy(const Compare& comp) const { return findNotLessThanNext(begin(), end(), comp) == end(); }

  /// Tells whether 'this' is strictly sorted by 'comp', another instance of our comparator type.
  /// Stateless comparators all define the same ordering, so no check is needed.
  /// Tag dispatch (instead of a constant condition) keeps C++11 compatibility without triggering MSVC C4127.
  bool isStrictlySortedBy(const Compare&, std::true_type /*isEmptyCompare*/) const { return true; }
  bool isStrictlySortedBy(const Compare& comp, std::false_type /*isEmptyCompare*/) const {
    return isStrictlySortedBy(comp);
  }

  /// First element of [first, last) which is not less than its successor according to 'comp', either equivalent to it
  /// or out of order (last if there is none).
  template <class It>
  static It findNotLessThanNext(It first, It last, const Compare& comp) {
    return std::adjacent_find(first, last, [&comp](const_reference v1, const_reference v2) { return !comp(v1, v2); });
  }

  /// Sorts [first, end()) by our comparator, unless it is already sorted, and returns the element from which its
  /// duplicates must be erased (see eraseDuplicates): the elements before it are strictly increasing.
  /// The first element which is not less than its successor is searched first: if there is none (sorted input without
  /// duplicates), a single pass is enough. Otherwise, the end of the sorted prefix is searched from it. Both searches
  /// stop at the first pair out of order: on random input, they only cost a few comparisons.
  /// If the sorted prefix holds at least two thirds of the elements, only the other ones are sorted, then merged with
  /// the prefix elements which are not less than their smallest one (see kMinSizeToMergeSortedPrefix).
  miterator sortIfUnsorted(miterator first) {
    const Compare& comp = compRef();
    const miterator last = mend();
    const miterator notLess = findNotLessThanNext(first, last, comp);
    const miterator sortedLast = std::is_sorted_until(notLess, last, comp);
    if (sortedLast == last) {
      return notLess;
    }
    const std::size_t nbSorted = static_cast<std::size_t>(sortedLast - first);
    const std::size_t nbUnsorted = static_cast<std::size_t>(last - sortedLast);
    if (nbSorted + nbUnsorted < flatset_details::kMinSizeToMergeSortedPrefix || nbSorted < 2U * nbUnsorted) {
      std::sort(first, last, comp);
      return first;
    }
    std::sort(sortedLast, last, comp);
    // the prefix elements before 'mergeFirst' are less than all the other elements: they keep their positions
    const miterator mergeFirst = mbegin() + (lowerBound(first, sortedLast, *sortedLast) - begin());
    std::inplace_merge(mergeFirst, sortedLast, last, comp);
    return std::min(notLess, mergeFirst);
  }

  /// Erase consecutive equivalent elements of [first, end()), keeping the first one of each group, according to our
  /// comparator object (which may hold a state). The elements before 'first' must be less than it.
  /// As elements are sorted, an element is equivalent to its predecessor 'p' if and only if it is not greater than 'p':
  /// a single comparison per element is needed (std::unique would require a symmetric equivalence predicate).
  void eraseDuplicates(miterator first) {
    const Compare& comp = compRef();
    const miterator last = mend();
    miterator dest = findNotLessThanNext(first, last, comp);
    if (dest != last) {
      for (miterator it = std::next(dest); ++it != last;) {
        if (comp(*dest, *it)) {
          *++dest = std::move(*it);
        }
      }
      _sortedVector.erase(std::next(dest), end());
    }
  }

  VecType _sortedVector;
};

/// Like the one of std::set, has the exception specification of the member swap.
template <class T, class Compare, class Alloc, class VecType>
inline void swap(FlatSet<T, Compare, Alloc, VecType>& lhs,
                 FlatSet<T, Compare, Alloc, VecType>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

}  // namespace amc
