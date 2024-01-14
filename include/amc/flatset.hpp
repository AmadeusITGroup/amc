#pragma once

#include <algorithm>
#include <cassert>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <utility>

#include "allocator.hpp"
#include "config.hpp"
#include "type_traits.hpp"
#include "vector.hpp"

#ifdef AMC_CXX14
#include "istransparent.hpp"
#ifdef AMC_CXX17
#include <optional>
#ifdef AMC_CXX20
#include <compare>
#endif
#endif
#endif
namespace amc {

/**
 * Set which keeps its elements into a sorted vector container.
 * The disadvantages of a sorted vector are linear-time insertions and linear-time deletions.
 * In exchange, a FlatSet offers a significant performance boost for all other operations,
 * with a much smaller memory impact.
 *
 * Detailed features and characteristics:
 * - Faster lookup than standard associative containers
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
class FlatSet : private Compare {
 public:
  using key_type = T;
  using value_type = T;
  using difference_type = ptrdiff_t;
  using size_type = typename VecType::size_type;
  using iterator = typename VecType::const_iterator;
  using const_iterator = typename VecType::const_iterator;
  using const_reference = typename VecType::const_reference;
  using pointer = typename VecType::const_pointer;
  using const_pointer = typename VecType::const_pointer;
  using key_compare = Compare;
  using value_compare = Compare;
  using allocator_type = Alloc;

  static_assert(std::is_same<T, typename VecType::value_type>::value, "Vector value type should be T");
  static_assert(std::is_same<Alloc, typename VecType::allocator_type>::value, "Allocator should match vector's");

#ifdef AMC_CXX17
  class node_type : private Alloc {
   public:
    using value_type = T;
    using allocator_type = Alloc;

    node_type() noexcept = default;

    node_type(const node_type &) = delete;
    node_type(node_type &&o) noexcept(std::is_nothrow_move_constructible<value_type>::value) = default;
    node_type &operator=(const node_type &) = delete;
    node_type &operator=(node_type &&o) noexcept(std::is_nothrow_move_assignable<value_type>::value) = default;

    ~node_type() = default;

    bool empty() const noexcept { return !_optV.has_value(); }
    explicit operator bool() const noexcept { return _optV.has_value(); }
    allocator_type get_allocator() const { return static_cast<allocator_type>(*this); }

    value_type &value() { return _optV.value(); }
    const value_type &value() const { return _optV.value(); }

    void swap(node_type &o) noexcept(
        std::is_nothrow_move_constructible<T>::value &&amc::is_nothrow_swappable<T>::value) {
      _optV.swap(o._optV);
    }

   private:
    template <class, class, class, class>
    friend class FlatSet;

    explicit node_type(Alloc alloc) : Alloc(alloc) {}
    explicit node_type(value_type &&v, Alloc alloc = Alloc()) : Alloc(alloc), _optV(std::move(v)) {}

    std::optional<value_type> _optV;
  };

  struct insert_return_type {
    iterator position;
    bool inserted;
    node_type node;
  };
#endif

  using trivially_relocatable =
      typename std::conditional<is_trivially_relocatable<Compare>::value && is_trivially_relocatable<VecType>::value,
                                std::true_type, std::false_type>::type;

  key_compare key_comp() const { return *this; }
  value_compare value_comp() const { return *this; }
  allocator_type get_allocator() const { return _sortedVector.get_allocator(); }

  FlatSet() noexcept(std::is_nothrow_default_constructible<Compare>::value
                         &&std::is_nothrow_default_constructible<VecType>::value) = default;

  explicit FlatSet(const Compare &comp, const Alloc &alloc = Alloc()) : Compare(comp), _sortedVector(alloc) {}

  explicit FlatSet(const Alloc &alloc) : _sortedVector(alloc) {}

  template <class InputIt>
  FlatSet(InputIt first, InputIt last, const Compare &comp = Compare(), const Alloc &alloc = Alloc())
      : Compare(comp), _sortedVector(first, last, alloc) {
    std::sort(_sortedVector.begin(), _sortedVector.end(), comp);
    eraseDuplicates();
  }

  template <class InputIt>
  FlatSet(InputIt first, InputIt last, const Alloc &alloc) : FlatSet(first, last, Compare(), alloc) {}

  FlatSet(const FlatSet &o, const Alloc &alloc) : Compare(o.key_comp()), _sortedVector(o._sortedVector, alloc) {}

  FlatSet(FlatSet &&o, const Alloc &alloc) : Compare(o.key_comp()), _sortedVector(std::move(o._sortedVector), alloc) {}

  FlatSet(std::initializer_list<value_type> list, const Compare &comp = Compare(), const Alloc &alloc = Alloc())
      : Compare(comp), _sortedVector(alloc) {
    insert(list.begin(), list.end());
  }

  FlatSet(std::initializer_list<value_type> list, const Alloc &alloc) : FlatSet(list, Compare(), alloc) {}

#ifdef AMC_NONSTD_FEATURES
  using vector_type = VecType;

  /// Non standard constructor of a FlatSet from a Vector, stealing its dynamic memory.
  explicit FlatSet(vector_type &&v, const Compare &comp = Compare(), const Alloc &alloc = Alloc())
      : Compare(comp), _sortedVector(std::move(v), alloc) {
    std::sort(_sortedVector.begin(), _sortedVector.end(), comp);
    eraseDuplicates();
  }

  FlatSet &operator=(vector_type &&v) {
    _sortedVector = std::move(v);
    std::sort(_sortedVector.begin(), _sortedVector.end(), compRef());
    eraseDuplicates();
    return *this;
  }
#endif

  FlatSet &operator=(std::initializer_list<value_type> list) {
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

  std::pair<iterator, bool> insert(const T &v) { return insert_val(v); }

  std::pair<iterator, bool> insert(T &&v) { return insert_val(std::move(v)); }

  iterator insert(const_iterator hint, const T &v) { return insert_hint(hint, v); }

  iterator insert(const_iterator hint, T &&v) { return insert_hint(hint, std::move(v)); }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    miterator insertIt = _sortedVector.insert(_sortedVector.end(), first, last);
    // sort appended elements only (beginning is already sorted)
    std::sort(insertIt, mend(), compRef());
    std::inplace_merge(mbegin(), insertIt, mend(), compRef());
    eraseDuplicates();
  }

  void insert(std::initializer_list<value_type> ilist) { insert(ilist.begin(), ilist.end()); }

#ifdef AMC_CXX17
  insert_return_type insert(node_type &&nh) {
    insert_return_type irt{end(), false, std::move(nh)};
    if (irt.node) {
      std::tie(irt.position, irt.inserted) = insert(std::move(*irt.node._optV));
      irt.node._optV = std::nullopt;
    }
    return irt;
  }

  iterator insert(const_iterator hint, node_type &&nh) {
    if (nh) {
      iterator retIt = insert(hint, std::move(*nh._optV));
      nh._optV = std::nullopt;
      return retIt;
    }
    return end();
  }
#endif

  template <class... Args>
  std::pair<iterator, bool> emplace(Args &&...args) {
    return insert(T(std::forward<Args &&>(args)...));
  }

  template <class... Args>
  iterator emplace_hint(const_iterator hint, Args &&...args) {
    return insert(hint, T(std::forward<Args &&>(args)...));
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
  const_iterator find(const K &k) const {
    const_iterator lbIt = lower_bound(k);
    return lbIt == cend() || compRef()(k, *lbIt) ? cend() : lbIt;
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  bool contains(const K &k) const {
    return find(k) != end();
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  size_type count(const K &k) const {
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

  bool operator==(const FlatSet &o) const { return _sortedVector == o._sortedVector; }
  bool operator!=(const FlatSet &o) const { return !(*this == o); }

#ifdef AMC_CXX20
  auto operator<=>(const FlatSet &o) const { return _sortedVector <=> o._sortedVector; }
#else
  bool operator<(const FlatSet &o) const { return _sortedVector < o._sortedVector; }

  bool operator<=(const FlatSet &o) const { return !(o < *this); }
  bool operator>(const FlatSet &o) const { return o < *this; }
  bool operator>=(const FlatSet &o) const { return !(*this < o); }
#endif

  void swap(FlatSet &o) noexcept(noexcept(std::declval<VecType>().swap(std::declval<VecType &>())) &&
                                 amc::is_nothrow_swappable<Compare>::value) {
    std::swap(static_cast<Compare &>(*this), static_cast<Compare &>(o));
    _sortedVector.swap(o._sortedVector);
  }

#ifdef AMC_CXX17
  node_type extract(const_iterator position) {
    node_type nt(std::move(*const_cast<miterator>(position)), get_allocator());
    _sortedVector.erase(position);
    return nt;
  }

  node_type extract(const key_type &key) {
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
  void merge(FlatSet<T, C2, Alloc, VecType> &o) {
    for (miterator oit = o.mbegin(); oit != o.mend();) {
      miterator lbIt = std::lower_bound(mbegin(), mend(), *oit, compRef());
      if (lbIt == mend()) {
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

  void merge(FlatSet &o) {
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
        --last2;
      } else {
        // equal
        ++first1;
        ++first2;
      }
    }
  }

  std::pair<const_iterator, const_iterator> equal_range(const key_type &key) const {
    const_iterator first = find(key);
    const_iterator second = first != end() ? std::next(first) : end();
    return std::pair<const_iterator, const_iterator>(first, second);
  }

  const_iterator lower_bound(const_reference v) const { return std::lower_bound(begin(), end(), v, compRef()); }
  const_iterator upper_bound(const_reference v) const { return std::upper_bound(begin(), end(), v, compRef()); }

#ifdef AMC_CXX14
  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  const_iterator lower_bound(const K &k) const {
    return std::lower_bound(begin(), end(), k, compRef());
  }

  template <class K, typename std::enable_if<!std::is_same<T, K>::value && has_is_transparent<Compare>::value,
                                             bool>::type = true>
  const_iterator upper_bound(const K &k) const {
    return std::upper_bound(begin(), end(), k, compRef());
  }
#endif

#ifdef AMC_NONSTD_FEATURES
  /// Get the underlying vector of elements of this FlatSet, by returning a newly move constructed vector.
  vector_type steal_vector() {
    vector_type ret(std::move(_sortedVector));
    // Make sure our sorted vector is cleared to avoid unspecified state (not necessarily in order)
    _sortedVector.clear();
    return ret;
  }
#endif

#ifdef AMC_CXX20
  template <class Pred>
  friend size_type erase_if(FlatSet &c, Pred pred) {
    return erase_if(c._sortedVector, pred);
  }
#endif

 private:
  template <class, class, class, class>
  friend class FlatSet;

  using miterator = typename VecType::iterator;  // Custom non const iterator to allow modification of content

  miterator mbegin() noexcept { return _sortedVector.begin(); }
  miterator mend() noexcept { return _sortedVector.end(); }

  miterator mfind(const_reference v) {
    miterator lbIt = std::lower_bound(mbegin(), mend(), v, compRef());
    return lbIt == mend() || compRef()(v, *lbIt) ? mend() : lbIt;
  }

  template <class V>
  std::pair<iterator, bool> insert_val(V &&v) {
    miterator insertIt = std::lower_bound(mbegin(), mend(), v, compRef());
    bool newValue = insertIt == mend() || compRef()(v, *insertIt);
    if (newValue) {
      insertIt = _sortedVector.insert(insertIt, std::forward<V>(v));
    }
    return std::pair<iterator, bool>(insertIt, newValue);
  }

  template <class V>
  iterator insert_hint(const_iterator hint, V &&v) {
    const_iterator b = begin();
    const_iterator e = end();
    assert(hint >= b && hint <= e);
    if (hint == e || !compRef()(*hint, v)) {  // [v, right side) is sorted
      const_iterator prevIt = b == e ? e : std::next(hint, -1);
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
        const_iterator insertIt = std::lower_bound(b, prevIt, v, compRef());
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

  static bool value_equi(const_reference v1, const_reference v2) {
    return !value_compare()(v1, v2) && !value_compare()(v2, v1);
  }

  Compare &compRef() { return static_cast<Compare &>(*this); }
  const Compare &compRef() const { return static_cast<const Compare &>(*this); }

  void eraseDuplicates() { _sortedVector.erase(std::unique(mbegin(), mend(), value_equi), end()); }

  VecType _sortedVector;
};

template <class T, class Compare, class Alloc, class VecType>
inline void swap(FlatSet<T, Compare, Alloc, VecType> &lhs, FlatSet<T, Compare, Alloc, VecType> &rhs) {
  lhs.swap(rhs);
}

}  // namespace amc
