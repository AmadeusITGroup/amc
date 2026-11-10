#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <memory>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>

#include "allocator.hpp"
#include "config.hpp"
#include "fixedcapacityvector.hpp"
#include "istransparent.hpp"
#include "type_traits.hpp"

#ifdef AMC_CXX17
#include "setnode.hpp"

#ifdef AMC_CXX20
#include "algorithm.hpp"

#ifdef AMC_CXX23
#include <ranges>
#endif
#endif
#endif

namespace amc {

namespace smallset_details {

/// Tells whether a set of type 'Set' merges a set of type 'Set2' with its 'merge' method: a std::set merges the
/// std::sets of other comparators (moving their nodes), a FlatSet the FlatSets of the same vector type.
template <class Set, class Set2, class = void>
struct CanMerge : std::false_type {};

template <class Set, class Set2>
struct CanMerge<Set, Set2, std::void_t<decltype(std::declval<Set&>().merge(std::declval<Set2&>()))>> : std::true_type {
};

#ifdef AMC_CXX20
/// Tells whether 'Compare' orders values of type 'T' with their operator< or operator>: std::less and std::greater, of
/// 'T' or transparent, and their std::ranges versions.
template <class Compare, class T>
struct OrdersByRelationalOperators : std::false_type {};

template <class T>
struct OrdersByRelationalOperators<std::less<T>, T> : std::true_type {};

template <class T>
struct OrdersByRelationalOperators<std::greater<T>, T> : std::true_type {};

template <class T>
struct OrdersByRelationalOperators<std::less<>, T> : std::true_type {};

template <class T>
struct OrdersByRelationalOperators<std::greater<>, T> : std::true_type {};

#ifdef __cpp_lib_ranges
template <class T>
struct OrdersByRelationalOperators<std::ranges::less, T> : std::true_type {};

template <class T>
struct OrdersByRelationalOperators<std::ranges::greater, T> : std::true_type {};
#endif
#endif

}  // namespace smallset_details

/**
 * Bidirectional iterator of a SmallSet whose set iterator is not a pointer. It holds either an iterator of the inline
 * vector (small state) or an iterator of the set (large state).
 * Its reverse iterator is a std::reverse_iterator of it, like the one of std::set.
 * Note: std::iterator is deprecated in c++17 so let's not derive from it.
 */
template <class T, class SetItType>
class SmallSetIterator {
 private:
  using VecItType = const T*;

 public:
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = const T*;
  using reference = const T&;

  // From C++20, an iterator must be default constructible to comply with ranges concepts.
  // See https://stackoverflow.com/questions/72590345/using-stdranges-algorithms-with-custom-containers-and-iterators
  SmallSetIterator() noexcept = default;

  SmallSetIterator(SetItType it) noexcept : _iter(it) {}

  SmallSetIterator(VecItType it) noexcept : _iter(it) {}

  SmallSetIterator& operator++() noexcept {  // Prefix increment
    std::visit([](auto& it) { ++it; }, _iter);
    return *this;
  }

  SmallSetIterator& operator--() noexcept {  // Prefix decrement
    std::visit([](auto& it) { --it; }, _iter);
    return *this;
  }

  SmallSetIterator operator++(int) noexcept {  // Postfix increment
    SmallSetIterator oldSelf = *this;
    ++*this;
    return oldSelf;
  }

  SmallSetIterator operator--(int) noexcept {  // Postfix decrement
    SmallSetIterator oldSelf = *this;
    --*this;
    return oldSelf;
  }

  reference operator*() const noexcept {
    return std::visit([](const auto& it) -> reference { return *it; }, _iter);
  }
  pointer operator->() const noexcept { return std::addressof(**this); }

  // For a set, only equality and un-equality operators are defined
#ifdef AMC_CXX20
  bool operator==(const SmallSetIterator&) const noexcept = default;
#else
  bool operator==(const SmallSetIterator& o) const noexcept { return _iter == o._iter; }
  bool operator!=(const SmallSetIterator& o) const noexcept { return !(*this == o); }
#endif

 private:
  template <class, std::uintmax_t, class, class, class>
  friend class SmallSet;

  SetItType toSetIt() const noexcept { return std::get<SetItType>(_iter); }
  VecItType toVecIt() const noexcept { return std::get<VecItType>(_iter); }

  std::variant<SetItType, VecItType> _iter;
};

template <class T, class Compare, class Alloc, class VecType>
class FlatSet;

/**
 * Set which is optimized for the case when number of elements is small (<= N).
 * The behavior of the set can be split in two steps:
 *  - Set is small     : elements are stored unordered in a vector-like container.
 *                       Find complexity is linear with number of elements hence N should stay small.
 *                       In its small state SmallSet will not allocate memory.
 *  - Set is not small : We have exceed the 'small' capacity of the underlying vector-like container.
 *                       We now use the provided 'SetType' container for all common set operations.
 *
 * Note that SetType can be configured by template parameter, it does not need to be a std::set.
 * For instance, it could be a FlatSet, in this case, the iterator type is optimized into a RandomAccessIterator.
 *
 * It does not allow duplicated elements.
 * Elements a, b are equivalent if !Compare(a, b) && !Compare(b, a)
 *
 * Differences with std::set, by design (see docs/compatibility.md for details):
 *  - In the small state, the elements are iterated in insertion order, not sorted: begin() is not the smallest
 *    element, and the algorithms requiring sorted ranges (std::includes, std::set_union...) do not apply. The
 *    comparison operators sort the elements, they compare like the ones of std::set.
 *  - No lower_bound, upper_bound nor equal_range, which would require sorted elements.
 *  - Iterators, references and pointers are invalidated like the ones of a vector in the small state, and like the
 *    ones of SetType in the large state, except that end() is not stable:
 *     - an insertion in the small state invalidates end(), and the one making the set grow invalidates all of them,
 *       including end() and the iterators to pass as hints,
 *     - an erasure in the small state (erase, extract, clear) invalidates the iterators to the erased elements and to
 *       all the following ones, including end(),
 *     - an erasure emptying the large set (erase, extract, clear) makes it small again, which invalidates end(),
 *     - a swap involving the small state invalidates the iterators to the elements of the small state.
 */
template <class T, std::uintmax_t N, class Compare = std::less<T>, class Alloc = amc::allocator<T>,
          class SetType = typename std::set<T, Compare, Alloc>>
class SmallSet {
 private:
  using VecType = FixedCapacityVector<T, N, vec::UncheckedGrowingPolicy>;
  using VecIt = typename VecType::const_iterator;
  using SetIt = typename SetType::const_iterator;
  // The exception specification of the defaulted move constructor must be the one it would have if it were implicit.
  using IsNothrowMoveConstructible =
      std::integral_constant<bool, std::is_nothrow_move_constructible<VecType>::value &&
                                       std::is_nothrow_move_constructible<SetType>::value>;
  using IsNothrowMoveAssignable = std::integral_constant<bool, std::is_nothrow_move_assignable<VecType>::value &&
                                                                   std::is_nothrow_move_assignable<SetType>::value>;

  static_assert(N <= 64, "N should be small as search has linear complexity for small sets");

  static_assert(std::is_same<T, typename SetType::value_type>::value, "Set value type should be T");
  static_assert(std::is_same<Compare, typename SetType::key_compare>::value, "Set compare type should be Compare");
  static_assert(std::is_same<Alloc, typename SetType::allocator_type>::value, "Allocator should match set's");

 public:
  using key_type = T;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using allocator_type = Alloc;

  // Optim: If Set iterator type is a pointer, do not use SmallSetIterator but a pointer instead.
  // This is possible for SetType = FlatSet for instance.
  using iterator =
      typename std::conditional<std::is_same<SetIt, const T*>::value, const T*, SmallSetIterator<T, SetIt>>::type;
  using reverse_iterator = std::reverse_iterator<iterator>;
  using const_iterator = iterator;
  using const_reverse_iterator = reverse_iterator;
  // Like the ones of std::set, 'reference' and 'pointer' are not const, but the elements are only accessible through
  // const ones (iterators included): modifying them could break the order (of the large state).
  using reference = value_type&;
  using const_reference = const value_type&;
  using size_type = typename std::conditional<sizeof(typename VecType::size_type) < sizeof(typename SetType::size_type),
                                              typename SetType::size_type, typename VecType::size_type>::type;

  using key_compare = Compare;
  using value_compare = Compare;
  using pointer = value_type*;
  using const_pointer = const value_type*;

  /// Shared with the FlatSets and SmallSets of the same value and allocator types (see set_details::NodeHandle).
  using node_type = set_details::NodeHandle<T, Alloc>;
  using insert_return_type = set_details::InsertReturnType<iterator, node_type>;

  using trivially_relocatable =
      typename std::conditional<is_trivially_relocatable<VecType>::value && is_trivially_relocatable<SetType>::value,
                                std::true_type, std::false_type>::type;

  key_compare key_comp() const { return _set.key_comp(); }
  value_compare value_comp() const { return _set.value_comp(); }

  allocator_type get_allocator() const { return _set.get_allocator(); }

  SmallSet() noexcept(std::is_nothrow_default_constructible<VecType>::value &&
                      std::is_nothrow_default_constructible<SetType>::value) = default;

  explicit SmallSet(const Compare& comp, const Alloc& alloc = Alloc()) : _set(comp, alloc) {}

  explicit SmallSet(const Alloc& alloc) : _set(alloc) {}

  template <class InputIt>
  SmallSet(InputIt first, InputIt last, const Compare& comp = Compare(), const Alloc& alloc = Alloc())
      : _set(comp, alloc) {
    insert(first, last);
  }

  template <class InputIt>
  SmallSet(InputIt first, InputIt last, const Alloc& alloc) : SmallSet(first, last, Compare(), alloc) {}

  SmallSet(const SmallSet& o, const Alloc& alloc) : _vec(o._vec), _set(o._set, alloc) {}

  /// If 'alloc' is not equal to the allocator of 'o', the elements of the large state are moved one by one (by its
  /// set). 'o' is left empty in both cases.
  SmallSet(SmallSet&& o,
           const Alloc& alloc) noexcept(std::is_nothrow_move_constructible<VecType>::value &&
                                        std::is_nothrow_constructible<SetType, SetType&&, const Alloc&>::value)
      : _vec(std::move(o._vec)), _set(std::move(o._set), alloc) {
    o._set.clear();
  }

  SmallSet(std::initializer_list<T> list, const Compare& comp = Compare(), const Alloc& alloc = Alloc())
      : SmallSet(list.begin(), list.end(), comp, alloc) {}

  SmallSet(std::initializer_list<T> list, const Alloc& alloc) : SmallSet(list, Compare(), alloc) {}

  SmallSet(const SmallSet&) = default;

  SmallSet(SmallSet&&) noexcept(IsNothrowMoveConstructible::value) = default;

  /// If an exception is thrown, the set is emptied: assigning the elements of the small state one by one could
  /// otherwise leave a mix of the previous and new elements, with duplicates.
  SmallSet& operator=(const SmallSet& o) {
    if (this != &o) {
      AMC_TRY {
        _vec = o._vec;
        _set = o._set;
      } AMC_CATCH_ALL {
        clearBoth();
        AMC_RETHROW;
      }
    }
    return *this;
  }

  /// If a move of an element throws, both sets are emptied, as they could otherwise keep moved from elements, or a mix
  /// of the previous and new elements.
  SmallSet& operator=(SmallSet&& o) noexcept(IsNothrowMoveAssignable::value) {
    if (this != &o) {
      moveAssign(std::move(o), IsNothrowMoveAssignable());
    }
    return *this;
  }

  /// If an exception is thrown, the set is emptied.
  SmallSet& operator=(std::initializer_list<T> list) {
    clear();
    AMC_TRY {
      insert(list.begin(), list.end());
    } AMC_CATCH_ALL {
      clearBoth();
      AMC_RETHROW;
    }
    return *this;
  }

  const_iterator begin() const noexcept {
    return isSmall() ? const_iterator(_vec.begin()) : const_iterator(_set.begin());
  }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator end() const noexcept { return isSmall() ? const_iterator(_vec.end()) : const_iterator(_set.end()); }
  const_iterator cend() const noexcept { return end(); }

  const_reverse_iterator rbegin() const noexcept { return const_reverse_iterator(end()); }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  const_reverse_iterator rend() const noexcept { return const_reverse_iterator(begin()); }
  const_reverse_iterator crend() const noexcept { return rend(); }

  bool empty() const noexcept { return _vec.empty() && _set.empty(); }

  size_type size() const noexcept { return isSmall() ? _vec.size() : _set.size(); }

  size_type max_size() const noexcept {
    return std::max(static_cast<size_type>(_vec.max_size()), static_cast<size_type>(_set.max_size()));
  }

  void clear() noexcept {
    if (isSmall()) {
      _vec.clear();
    } else {
      _set.clear();
    }
  }

  std::pair<iterator, bool> insert(const T& v) { return isSmall() ? insert_small(v) : insert_set(v); }

  std::pair<iterator, bool> insert(T&& v) { return isSmall() ? insert_small(std::move(v)) : insert_set(std::move(v)); }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    // Insert elements in vector as long as we stay small
    while (isSmall() && first != last) {
      insert_small(vec::AsValueType<T>(*first));
      ++first;
    }
    // Insert remaining elements (if any) in the standard set if we became large
    if (!isSmall() && first != last) {
      _set.insert(first, last);
    }
  }

  // We keep elements unsorted in the vector, we cannot have any value from hint

  iterator insert(const_iterator hint, const T& v) {
    return isSmall() ? insert_small(v).first : iterator(_set.insert(ToSetIt(hint), v));
  }

  iterator insert(const_iterator hint, T&& v) {
    return isSmall() ? insert_small(std::move(v)).first : _set.insert(ToSetIt(hint), std::move(v));
  }

  void insert(std::initializer_list<value_type> ilist) { insert(ilist.begin(), ilist.end()); }

  /// 'nh' is left empty. If the insertion fails (equivalent element already present), the returned node owns its value.
  /// If an exception is thrown (by the comparator, or by an allocation when growing), 'nh' keeps its value.
  insert_return_type insert(node_type&& nh) { return node_type::InsertInto(*this, std::move(nh)); }

  /// If the insertion fails (equivalent element already present), 'nh' is unchanged.
  iterator insert(const_iterator hint, node_type&& nh) { return node_type::InsertInto(*this, hint, std::move(nh)); }

#ifdef AMC_CXX23
  /// 'rg' may not be common (its end may be a sentinel of another type than its iterator).
  template <class R>
  void insert_range(R&& rg) {
    if constexpr (std::ranges::common_range<R> && std::ranges::forward_range<R>) {
      insert(std::ranges::begin(rg), std::ranges::end(rg));
    } else {
      const auto last = std::ranges::end(rg);
      for (auto first = std::ranges::begin(rg); first != last; ++first) {
        emplace(*first);
      }
    }
  }
#endif

  template <class... Args>
  std::pair<iterator, bool> emplace(Args&&... args) {
    if (!isSmall()) {
      return _set.emplace(std::forward<Args&&>(args)...);
    }
    if (isSmallContFull()) {
      return insert(T(std::forward<Args&&>(args)...));
    }
    // The element is constructed in place before being compared, to avoid a temporary.
    miterator elIt = std::addressof(_vec.emplace_back(std::forward<Args&&>(args)...));
    miterator it;
    AMC_TRY {
      it = std::find_if(_vec.begin(), elIt, FindFunctor<T>(key_comp(), *elIt));
    } AMC_CATCH_ALL {
      // the new element may be a duplicate: it is removed if the comparator (or its copy) throws
      _vec.pop_back();
      AMC_RETHROW;
    }
    const bool isNew = it == elIt;
    if (!isNew) {
      _vec.pop_back();
      elIt = it;
    }
    return std::pair<iterator, bool>(elIt, isNew);
  }

  template <class... Args>
  iterator emplace_hint(const_iterator hint, Args&&... args) {
    if (!isSmall()) {
      return _set.emplace_hint(ToSetIt(hint), std::forward<Args&&>(args)...);
    }
    // We cannot use any hint when we are small
    return emplace(std::forward<Args&&>(args)...).first;
  }

  const_iterator find(const_reference k) const {
    return isSmall() ? const_iterator(find_small(k)) : const_iterator(_set.find(k));
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  const_iterator find(const K& k) const {
    return isSmall() ? const_iterator(find_small(k)) : const_iterator(_set.find(k));
  }

  bool contains(const_reference k) const { return isSmall() ? find_small(k) != _vec.end() : _set.count(k); }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  bool contains(const K& k) const {
    return isSmall() ? find_small(k) != _vec.end() : _set.count(k);
  }

  size_type count(const_reference v) const { return contains(v); }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  size_type count(const K& k) const {
    return contains(k);
  }

  size_type erase(const_reference v) {
    if (!isSmall()) {
      return _set.erase(v);
    }
    miterator it = mfind_small(v);
    if (it == _vec.end()) {
      return 0U;
    }
    eraseFromVec(it);
    return 1U;
  }

  /// In the small state, if moving an element throws, the set is emptied (see eraseFromVec).
  iterator erase(const_iterator pos) {
    return isSmall() ? iterator(eraseFromVec(ToVecIt(pos))) : afterSetErase(_set.erase(ToSetIt(pos)));
  }

  iterator erase(const_iterator first, const_iterator last) {
    return isSmall() ? iterator(eraseFromVec(ToVecIt(first), ToVecIt(last)))
                     : afterSetErase(_set.erase(ToSetIt(first), ToSetIt(last)));
  }

  void swap(SmallSet& o) noexcept(noexcept(std::declval<VecType>().swap(std::declval<VecType&>())) &&
                                  noexcept(std::declval<SetType>().swap(std::declval<SetType&>()))) {
    _vec.swap(o._vec);
    _set.swap(o._set);
  }

  node_type extract(const_iterator position) {
    if (isSmall()) {
      auto vecIt = ToVecIt(position);
      node_type nt(std::move(*const_cast<miterator>(vecIt)), get_allocator());
      eraseFromVec(vecIt);
      return nt;
    }
    if constexpr (std::is_same_v<typename SetType::node_type, node_type>) {
      return _set.extract(ToSetIt(position));  // the node of a FlatSet is ours: returned as is
    } else {
      return FromSetNode(_set.extract(ToSetIt(position)));
    }
  }

  node_type extract(const key_type& key) {
    if (isSmall()) {
      auto vecIt = mfind_small(key);
      node_type nh(get_allocator());
      if (vecIt != _vec.end()) {
        nh._optV = std::move(*vecIt);
        eraseFromVec(vecIt);
      }
      return nh;
    }
    if constexpr (std::is_same_v<typename SetType::node_type, node_type>) {
      return _set.extract(key);  // the node of a FlatSet is ours: returned as is
    } else {
      return FromSetNode(_set.extract(key));
    }
  }

  /// Merge elements from 'o' set into 'this'.
  /// If the merge can occur without grow, 'this' will stay small.
  /// No grow is performed on 'o'.
  /// The sets of the large states may be of different types (a std::set and a FlatSet): if ours cannot merge the one of
  /// 'o', its elements are moved one by one.
  template <std::uintmax_t N2, class C2, class SetType2>
  void merge(SmallSet<T, N2, C2, Alloc, SetType2>& o) {
    if (!o.isSmall()) {
      if (isSmall()) {
        grow();
      }
      mergeSet(o._set);
      return;
    }
    bool smallState = isSmall();
    for (auto oit = o._vec.begin(); oit != o._vec.end();) {
      FindFunctor<T> fFunc(key_comp(), *oit);
      if (smallState) {
        if (std::none_of(_vec.begin(), _vec.end(), fFunc)) {
          if (isSmallContFull()) {
            growAndInsert(std::move(*oit));
            smallState = false;
          } else {
            _vec.push_back(std::move(*oit));
          }
          oit = o.eraseFromVec(oit);
        } else {
          ++oit;
        }
      } else {
        if (_set.insert(std::move(*oit)).second) {
          oit = o.eraseFromVec(oit);
        } else {
          ++oit;
        }
      }
    }
  }

  /// Like std::set, a temporary can be merged. Its elements which are already present are destroyed with it.
  template <std::uintmax_t N2, class C2, class SetType2>
  void merge(SmallSet<T, N2, C2, Alloc, SetType2>&& o) {
    merge(o);
  }

  bool operator==(const SmallSet& o) const {
    if (size() != o.size()) {
      return false;
    }
    if (!isSmall() && !o.isSmall()) {
      return _set == o._set;
    }
    // We have at least one set that is unsorted. Use is_permutation
    // We use equality here, not equivalence (ie using == operator instead of <)
    return std::is_permutation(begin(), end(), o.begin(), o.end());
  }

  bool operator!=(const SmallSet& o) const { return !(*this == o); }

#ifdef AMC_CXX20
  /// Like the one of std::set, only defined for elements which 'synth_three_way' can compare.
  auto operator<=>(const SmallSet& o) const
    requires algorithm_details::SynthThreeWayComparable<T>
  {
    if (!isSmall() && !o.isSmall()) {
      return _set <=> o._set;
    }
    return compareSorted(o, [](auto first1, auto last1, auto first2, auto last2) {
      return amc::lexicographical_compare_three_way(first1, last1, first2, last2, DerefThreeWay());
    });
  }

  /// Linear: the elements are not erased one by one, which would shift the next ones at each erase in the vector of the
  /// small state, or in the one of a FlatSet.
  template <class Pred>
  friend size_type erase_if(SmallSet& c, Pred pred) {
    // like the iterators of a set, the predicate only has a const access to the elements
    auto constPred = [&pred](const_reference v) { return pred(v); };
    if (c.isSmall()) {
      return static_cast<size_type>(algorithm_details::EraseIfFromSetVector(c._vec, constPred));
    }
    return EraseIfFromSet(c._set, constPred);
  }

#else

  // Comparison operators are provided for compliance with std::set but could be painful for performances
  // as we keep elements unsorted in the 'small' container.
  bool operator<(const SmallSet& o) const {
    if (!isSmall() && !o.isSmall()) {
      return _set < o._set;
    }
    return compareSorted(o, [](auto first1, auto last1, auto first2, auto last2) {
      return std::lexicographical_compare(first1, last1, first2, last2, DerefLess());
    });
  }

  bool operator<=(const SmallSet& o) const { return !(o < *this); }
  bool operator>(const SmallSet& o) const { return o < *this; }
  bool operator>=(const SmallSet& o) const { return !(*this < o); }
#endif

 private:
  using miterator = typename VecType::iterator;  // Custom iterator to allow modification of content (when small)
  using PtrVec = FixedCapacityVector<const_pointer, N, vec::UncheckedGrowingPolicy>;

  template <class I>
  static inline SetIt ToSetIt(I it, typename std::enable_if<std::is_same<I, const T*>::value>::type* = 0) {
    return it;
  }
  template <class I>
  static inline SetIt ToSetIt(I it, typename std::enable_if<!std::is_same<I, const T*>::value>::type* = 0) {
    return it.toSetIt();
  }

  template <class I>
  static inline VecIt ToVecIt(I it, typename std::enable_if<std::is_same<I, const T*>::value>::type* = 0) {
    return it;
  }
  template <class I>
  static inline VecIt ToVecIt(I it, typename std::enable_if<!std::is_same<I, const T*>::value>::type* = 0) {
    return it.toVecIt();
  }

  /// Converts the iterator returned by an erase in the set. If the set has become empty, 'this' is small again (see
  /// 'isSmall'): the returned iterator is then the end of the vector, to stay comparable with 'end()'.
  iterator afterSetErase(SetIt it) const noexcept { return _set.empty() ? end() : iterator(it); }

  /// Our node holding the value of 'setNode', a node of another type extracted from the set (of a std::set).
  /// Our allocator is the one of the set, the allocator of an empty std::set node cannot be read.
  template <class SetNode>
  node_type FromSetNode(SetNode setNode) const {
    node_type nh(get_allocator());
    if (setNode) {
      nh._optV = std::move(setNode.value());
    }
    return nh;
  }

  template <class V>
  std::pair<iterator, bool> insert_small(V&& v) {
    miterator insertIt = mfind_small(v);
    bool newValue = insertIt == _vec.end();
    if (newValue) {
      if (isSmallContFull()) {
        return growAndInsert(std::forward<V>(v));
      }
      _vec.push_back(std::forward<V>(v));
    }
    return std::pair<iterator, bool>(insertIt, newValue);
  }

  template <class V>
  std::pair<iterator, bool> insert_set(V&& v) {
    return _set.insert(std::forward<V>(v));
  }

  /// Merges 'set', the set of a large SmallSet, into ours (empty or in the large state). A set that ours cannot merge
  /// with its 'merge' method (a FlatSet into a std::set for instance) is merged element by element: each new element is
  /// moved to our set, then erased from 'set'. Elements whose move may throw are copied instead: if it throws, 'set'
  /// is unchanged, instead of keeping a moved from element among its sorted ones.
  template <class S>
  void mergeSet(S& set) {
    if constexpr (smallset_details::CanMerge<SetType, S>::value) {
      _set.merge(set);
    } else {
      for (auto it = set.begin(); it != set.end();) {
        // the elements of a set are const, but they are only moved from just before their erasure, which does not
        // compare them. Like std::set, our set does not move from an element which it does not insert.
        if (_set.insert(std::move_if_noexcept(const_cast<T&>(*it))).second) {
          it = set.erase(it);
        } else {
          ++it;
        }
      }
    }
  }

  template <class K>
  struct FindFunctor {
    FindFunctor(Compare&& comp, const K& k) : _comp(std::move(comp)), _k(k) {}

    /// Tells whether 'o' is equivalent to the searched key: neither of them is less than the other for 'Compare', which
    /// is called twice. From C++20, if 'Compare' calls operator< or operator> of a key three-way comparable with 'o'
    /// (strings, pairs, tuples, containers of them, a string view looked up among strings...), a single operator<=>
    /// tells it, as these operators are then rewritten from it: a lookup among 16 strings is twice as fast. Keys
    /// declaring their own operator< or operator> are compared with them, as they may define another ordering.
    /// operator== is not used, as it may compare more than the ordering does.
    bool operator()(const_reference o) const {
#ifdef AMC_CXX20
      if constexpr (smallset_details::OrdersByRelationalOperators<Compare, T>::value &&
                    algorithm_details::ThreeWayComparableWith<std::decay_t<K>, T> &&  // decays a string literal
                    !algorithm_details::DeclaresRelationalOperator<std::decay_t<K>, T>) {
        const auto order = _k <=> o;
        return !(order < 0) && !(order > 0);  // like with 'Compare', unordered values are equivalent
      } else
#endif
      {
        return !_comp(_k, o) && !_comp(o, _k);
      }
    }

    Compare _comp;
    const K& _k;
  };

  template <class K>
  miterator mfind_small(const K& k) {
    return std::find_if(_vec.begin(), _vec.end(), FindFunctor<K>(key_comp(), k));
  }

  template <class K>
  typename VecType::const_iterator find_small(const K& k) const {
    return std::find_if(_vec.begin(), _vec.end(), FindFunctor<K>(key_comp(), k));
  }

  /// Moves the elements of the small state to the set (see moveToEmptySet).
  void grow() {
    const PtrVec sortedPtrs = ComputeSortedPtrVec(_vec, key_comp());
    moveToEmptySet(_set, sortedPtrs, 0U);
    _vec.clear();
  }

  /// Makes the full small state grow by inserting 'v', which is not equivalent to any of its elements. If it throws,
  /// the set stays small and unchanged, like when 'grow' throws, unless the failed insertion emptied the set (a FlatSet
  /// of elements whose moves may throw), which then loses all its elements.
  template <class V>
  std::pair<iterator, bool> growAndInsert(V&& v) {
    const PtrVec sortedPtrs = ComputeSortedPtrVec(_vec, key_comp());
    moveToEmptySet(_set, sortedPtrs, 1U);
    AMC_TRY {
      std::pair<iterator, bool> ret = _set.insert(std::forward<V>(v));
      _vec.clear();
      return ret;
    } AMC_CATCH_ALL {
      if (_set.size() == sortedPtrs.size()) {
        restoreSmallState(_set, sortedPtrs);
      } else {
        clearBoth();  // the elements left in the vector are moved from
      }
      AMC_RETHROW;
    }
  }

  /// Moves the elements of the vector, which 'sortedPtrs' points to in increasing order, to the empty 'set', in this
  /// order, which is the cheapest one to insert them in (each one at the end). Sorting the pointers beforehand only
  /// compares the elements: nothing to undo if it throws. If an insertion throws, the elements already moved to 'set'
  /// are moved back (see restoreSmallState). 'nbExtraElems' is the number of elements to be inserted just after.
  template <class S>
  void moveToEmptySet(S& set, const PtrVec& sortedPtrs, std::size_t) {
    AMC_TRY {
      for (const_pointer ptr : sortedPtrs) {
        set.insert(set.end(), std::move(const_cast<T&>(*ptr)));
      }
    } AMC_CATCH_ALL {
      restoreSmallState(set, sortedPtrs);
      AMC_RETHROW;
    }
  }

  /// The insertion of a range in a FlatSet moves all the elements before comparing them: instead, the sorted elements
  /// are appended to its vector without comparing them again, after a single allocation, which leaves room for the
  /// extra elements: their insertion then does not reallocate the vector right away.
  template <class V>
  void moveToEmptySet(FlatSet<T, Compare, Alloc, V>& set, const PtrVec& sortedPtrs, std::size_t nbExtraElems) {
    V& setVec = set._sortedVector;
    AMC_TRY {
      setVec.reserve(static_cast<typename V::size_type>(sortedPtrs.size() + nbExtraElems));
      for (const_pointer ptr : sortedPtrs) {
        setVec.emplace_back(std::move(const_cast<T&>(*ptr)));
      }
    } AMC_CATCH_ALL {
      restoreSmallState(set, sortedPtrs);
      AMC_RETHROW;
    }
  }

  /// Moves back the elements of 'set' to the vector of the small state, from which they were moved in the order of
  /// 'sortedPtrs', which is their order in 'set': each one gets back to its position, which keeps the insertion order.
  /// 'set' is then cleared, otherwise the moved from elements would reappear once the set becomes empty again. If
  /// moving an element back throws, the vector is cleared too: the set is emptied, instead of keeping moved from
  /// elements.
  template <class S>
  void restoreSmallState(S& set, const PtrVec& sortedPtrs) noexcept {
    AMC_TRY {
      auto ptrIt = sortedPtrs.begin();
      for (auto setIt = set.begin(); setIt != set.end(); ++setIt) {
        // the set is cleared just after, it does not need to stay sorted
        const_cast<T&>(**ptrIt++) = std::move(const_cast<T&>(*setIt));
      }
    } AMC_CATCH_ALL {
      _vec.clear();
    }
    set.clear();
  }

#ifdef AMC_CXX20
  /// Erases one by one the elements satisfying 'pred', like std::erase_if for the node based sets such as std::set.
  template <class S, class Pred>
  static size_type EraseIfFromSet(S& set, Pred& pred) {
    const auto oldSize = set.size();
    for (auto it = set.begin(); it != set.end();) {
      if (pred(*it)) {
        it = set.erase(it);
      } else {
        ++it;
      }
    }
    return static_cast<size_type>(oldSize - set.size());
  }

  /// A FlatSet removes the elements satisfying 'pred' in a single pass over its vector.
  template <class V, class Pred>
  static size_type EraseIfFromSet(FlatSet<T, Compare, Alloc, V>& set, Pred& pred) {
    return static_cast<size_type>(erase_if(set, pred));
  }
#endif

  bool isSmall() const noexcept { return _set.empty(); }
  bool isSmallContFull() const noexcept { return _vec.size() == N; }

  /// Unlike 'clear', clears both containers, as a failed assignment may have filled both.
  void clearBoth() noexcept {
    _vec.clear();
    _set.clear();
  }

  void moveAssign(SmallSet&& o, std::true_type) noexcept {
    _vec = std::move(o._vec);
    _set = std::move(o._set);
    o._set.clear();  // the elements are moved one by one between unequal allocators: 'o' could keep them
  }

  void moveAssign(SmallSet&& o, std::false_type) {
    AMC_TRY {
      _vec = std::move(o._vec);
      _set = std::move(o._set);
    } AMC_CATCH_ALL {
      clearBoth();
      o.clearBoth();
      AMC_RETHROW;
    }
    o._set.clear();  // the elements are moved one by one between unequal allocators: 'o' could keep them
  }

  /// Erases the element at 'pos', or the elements of ['pos', 'last'), from the vector of the small state, which shifts
  /// the next elements. If moving an element throws, the vector is cleared: it could otherwise keep a moved from
  /// element, which may not be unique.
  template <class... Last>
  miterator eraseFromVec(VecIt pos, Last... last) {
    if constexpr (std::is_nothrow_move_assignable_v<T>) {
      return _vec.erase(pos, last...);
    } else {
      AMC_TRY {
        return _vec.erase(pos, last...);
      } AMC_CATCH_ALL {
        _vec.clear();
        AMC_RETHROW;
      }
    }
  }

  /// Pointers to the elements of 'c', sorted with comparator object 'comp' (which may hold a state).
  static PtrVec ComputeSortedPtrVec(const VecType& c, const Compare& comp) {
    PtrVec sortedPtrs;
    std::transform(c.begin(), c.end(), std::back_inserter(sortedPtrs),
                   [](const_reference r) { return std::addressof(r); });
    std::sort(sortedPtrs.begin(), sortedPtrs.end(),
              [&comp](const_pointer p1, const_pointer p2) { return comp(*p1, *p2); });
    return sortedPtrs;
  }

  /// Returns 'lexCompare(first1, last1, first2, last2)' for the elements of 'this' and 'o' in increasing order, at
  /// least one of the sets being small: as the elements of the small state are not ordered, the ones of a small set are
  /// given by pointers to them, sorted by 'ComputeSortedPtrVec' (see DerefLess and DerefThreeWay).
  template <class LexCompare>
  auto compareSorted(const SmallSet& o, LexCompare lexCompare) const {
    if (!isSmall()) {
      const PtrVec oSortedPtrs = ComputeSortedPtrVec(o._vec, o.key_comp());
      return lexCompare(_set.begin(), _set.end(), oSortedPtrs.begin(), oSortedPtrs.end());
    }
    const PtrVec sortedPtrs = ComputeSortedPtrVec(_vec, key_comp());
    if (!o.isSmall()) {
      return lexCompare(sortedPtrs.begin(), sortedPtrs.end(), o._set.begin(), o._set.end());
    }
    const PtrVec oSortedPtrs = ComputeSortedPtrVec(o._vec, o.key_comp());
    return lexCompare(sortedPtrs.begin(), sortedPtrs.end(), oSortedPtrs.begin(), oSortedPtrs.end());
  }

  /// Element of a range compared by 'compareSorted': a pointer to an element of a small set, or an element of a large
  /// one.
  static const T& Deref(const_pointer p) noexcept { return *p; }
  static const T& Deref(const_reference v) noexcept { return v; }

#ifdef AMC_CXX20
  struct DerefThreeWay {
    template <class L, class R>
    auto operator()(const L& lhs, const R& rhs) const {
      return amc::synth_three_way()(Deref(lhs), Deref(rhs));
    }
  };
#else
  struct DerefLess {
    template <class L, class R>
    bool operator()(const L& lhs, const R& rhs) const {
      return Deref(lhs) < Deref(rhs);
    }
  };
#endif

  template <class, std::uintmax_t, class, class, class>
  friend class SmallSet;

  VecType _vec;
  SetType _set;
};

/// Like the one of std::set, has the exception specification of the member swap.
template <class T, std::uintmax_t N, class Compare, class Alloc, class SetType>
inline void swap(SmallSet<T, N, Compare, Alloc, SetType>& lhs,
                 SmallSet<T, N, Compare, Alloc, SetType>& rhs) noexcept(noexcept(lhs.swap(rhs))) {
  lhs.swap(rhs);
}

}  // namespace amc
