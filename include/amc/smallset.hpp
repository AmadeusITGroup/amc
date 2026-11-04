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
#include "fixedcapacityvector.hpp"
#include "istransparent.hpp"
#include "setnode.hpp"
#include "type_traits.hpp"

#ifdef AMC_CXX20
#include "algorithm.hpp"
#endif

#ifdef AMC_CXX23
#include <ranges>
#endif

namespace amc {

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
 * Differences with std::set, by design (see the README for details):
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
  using reference = const T&;
  using const_reference = reference;
  using size_type = typename std::conditional<sizeof(typename VecType::size_type) < sizeof(typename SetType::size_type),
                                              typename SetType::size_type, typename VecType::size_type>::type;

  using key_compare = Compare;
  using value_compare = Compare;
  using pointer = const T*;
  using const_pointer = const T*;

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
  /// set).
  SmallSet(SmallSet&& o,
           const Alloc& alloc) noexcept(std::is_nothrow_move_constructible<VecType>::value &&
                                        std::is_nothrow_constructible<SetType, SetType&&, const Alloc&>::value)
      : _vec(std::move(o._vec)), _set(std::move(o._set), alloc) {}

  SmallSet(std::initializer_list<T> list, const Compare& comp = Compare(), const Alloc& alloc = Alloc())
      : SmallSet(list.begin(), list.end(), comp, alloc) {}

  SmallSet(std::initializer_list<T> list, const Alloc& alloc) : SmallSet(list, Compare(), alloc) {}

  SmallSet(const SmallSet&) = default;

  SmallSet(SmallSet&&) noexcept(IsNothrowMoveConstructible::value) = default;

  /// If an exception is thrown, the set is emptied: assigning the elements of the small state one by one could
  /// otherwise leave a mix of the previous and new elements, with duplicates.
  SmallSet& operator=(const SmallSet& o) {
    if (this != &o) {
      try {
        _vec = o._vec;
        _set = o._set;
      } catch (...) {
        clearBoth();
        throw;
      }
    }
    return *this;
  }

  /// If a move of an element throws, both sets are emptied, as they could otherwise keep moved from elements, or a mix
  /// of the previous and new elements.
  SmallSet& operator=(SmallSet&& o) noexcept(IsNothrowMoveAssignable::value) {
    if (this != &o) {
      moveAssign(o, IsNothrowMoveAssignable());
    }
    return *this;
  }

  SmallSet& operator=(std::initializer_list<T> list) {
    clear();
    insert(list.begin(), list.end());
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
    try {
      it = std::find_if(_vec.begin(), elIt, FindFunctor<T>(key_comp(), *elIt));
    } catch (...) {
      // the new element may be a duplicate: it is removed if the comparator (or its copy) throws
      _vec.pop_back();
      throw;
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
    _vec.erase(it);
    return 1U;
  }

  iterator erase(const_iterator pos) {
    return isSmall() ? iterator(_vec.erase(ToVecIt(pos))) : afterSetErase(_set.erase(ToSetIt(pos)));
  }

  iterator erase(const_iterator first, const_iterator last) {
    return isSmall() ? iterator(_vec.erase(ToVecIt(first), ToVecIt(last)))
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
      _vec.erase(vecIt);
      return nt;
    }
    auto setNt = _set.extract(ToSetIt(position));
    node_type nt(std::move(setNt.value()), setNt.get_allocator());
    return nt;
  }

  node_type extract(const key_type& key) {
    if (isSmall()) {
      auto vecIt = mfind_small(key);
      node_type nh(get_allocator());
      if (vecIt != _vec.end()) {
        nh._optV = std::move(*vecIt);
        _vec.erase(vecIt);
      }
      return nh;
    }
    auto setNt = _set.extract(key);
    node_type nh(get_allocator());
    if (setNt) {
      nh._optV = std::move(setNt.value());
    }
    return nh;
  }

  /// Merge elements from 'o' set into 'this'.
  /// If the merge can occur without grow, 'this' will stay small.
  /// No grow is performed on 'o'.
  template <std::uintmax_t N2, class C2, class SetType2>
  void merge(SmallSet<T, N2, C2, Alloc, SetType2>& o) {
    if (!o.isSmall()) {
      if (isSmall()) {
        grow();
      }
      _set.merge(o._set);
      return;
    }
    bool small = isSmall();
    for (auto oit = o._vec.begin(); oit != o._vec.end();) {
      FindFunctor<T> fFunc(key_comp(), *oit);
      if (small) {
        if (std::none_of(_vec.begin(), _vec.end(), fFunc)) {
          if (isSmallContFull()) {
            grow();
            small = false;
            _set.insert(std::move(*oit));
          } else {
            _vec.push_back(std::move(*oit));
          }
          oit = o._vec.erase(oit);
        } else {
          ++oit;
        }
      } else {
        if (_set.insert(std::move(*oit)).second) {
          oit = o._vec.erase(oit);
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
  auto operator<=>(const SmallSet& o) const {
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
      return static_cast<size_type>(erase_if(c._vec, constPred));
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

  template <class V>
  std::pair<iterator, bool> insert_small(V&& v) {
    miterator insertIt = mfind_small(v);
    bool newValue = insertIt == _vec.end();
    if (newValue) {
      if (isSmallContFull()) {
        grow();
        return _set.insert(std::forward<V>(v));
      }
      _vec.push_back(std::forward<V>(v));
    }
    return std::pair<iterator, bool>(insertIt, newValue);
  }

  template <class V>
  std::pair<iterator, bool> insert_set(V&& v) {
    return _set.insert(std::forward<V>(v));
  }

  template <class K>
  struct FindFunctor {
    FindFunctor(Compare&& comp, const K& k) : _comp(std::move(comp)), _k(k) {}

    bool operator()(const_reference o) const { return !_comp(_k, o) && !_comp(o, _k); }

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

  /// Moves the elements of the vector to the set.
  /// If it throws, the elements already moved to the set are moved back to the vector, which then keeps all the
  /// elements (strong guarantee if moving T does not throw), and the set is cleared: otherwise, the moved from elements
  /// left in the vector would reappear once the set becomes empty again.
  void grow() {
    moveToEmptySet(_set);
    _vec.clear();
  }

  /// The insertion of a range in a set such as std::set inserts the elements one by one.
  template <class S>
  void moveToEmptySet(S& set) {
    try {
      set.insert(std::make_move_iterator(_vec.begin()), std::make_move_iterator(_vec.end()));
    } catch (...) {
      // The set contains exactly the elements of the vector inserted before the exception (they are all unique), which
      // are moved from in the vector. As the vector is unordered, they can be moved back in any order.
      miterator vecIt = _vec.begin();
      for (auto setIt = set.begin(); setIt != set.end(); ++setIt, (void)++vecIt) {
        *vecIt = std::move(const_cast<T&>(*setIt));  // the set is cleared just after, it does not need to stay sorted
      }
      set.clear();
      throw;
    }
  }

  /// The insertion of a range in a FlatSet moves all the elements before comparing them, so they could not be moved
  /// back if a comparison throws. Instead, they are sorted beforehand, by sorting pointers to them (which only compares
  /// them: nothing to undo if it throws), then appended in this order to the vector of the FlatSet without comparing
  /// them again, after a single allocation: then, only a move of an element can throw.
  template <class V>
  void moveToEmptySet(FlatSet<T, Compare, Alloc, V>& set) {
    const PtrVec sortedPtrs = ComputeSortedPtrVec(_vec, key_comp());
    V& setVec = set._sortedVector;
    try {
      setVec.reserve(static_cast<typename V::size_type>(sortedPtrs.size()));
      for (const_pointer ptr : sortedPtrs) {
        setVec.emplace_back(std::move(const_cast<T&>(*ptr)));
      }
    } catch (...) {
      // The vector of the set contains the elements pointed to by the first pointers of 'sortedPtrs', in this order.
      auto ptrIt = sortedPtrs.begin();
      for (T& v : setVec) {
        const_cast<T&>(**ptrIt++) = std::move(v);
      }
      setVec.clear();
      throw;
    }
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

  void moveAssign(SmallSet& o, std::true_type) noexcept {
    _vec = std::move(o._vec);
    _set = std::move(o._set);
  }

  void moveAssign(SmallSet& o, std::false_type) {
    try {
      _vec = std::move(o._vec);
      _set = std::move(o._set);
    } catch (...) {
      clearBoth();
      o.clearBoth();
      throw;
    }
  }

  using PtrVec = FixedCapacityVector<const_pointer, N, vec::UncheckedGrowingPolicy>;

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

template <class T, std::uintmax_t N, class Compare, class Alloc, class SetType>
inline void swap(SmallSet<T, N, Compare, Alloc, SetType>& lhs, SmallSet<T, N, Compare, Alloc, SetType>& rhs) {
  lhs.swap(rhs);
}

}  // namespace amc
