#pragma once

#include <functional>
#include <initializer_list>
#include <iterator>
#include <optional>
#include <set>
#include <type_traits>
#include <utility>
#include <variant>

#include "amc_allocator.hpp"
#include "amc_config.hpp"
#include "amc_fixedcapacityvector.hpp"
#include "amc_memory.hpp"
#include "amc_type_traits.hpp"

namespace amc {

/**
 * Common definitions of SmallSetIterator that is independent from direction.
 * Note: std::iterator is deprecated in c++17 so let's not derive from it.
 * It contains a variant to embed either a Vector type iterator or a SetType iterator.
 */
template <class T, class SetItType>
class SmallSetIteratorCommon {
 private:
  using VecItType = const T *;
  using IteratorVariant = std::variant<SetItType, VecItType>;

  IteratorVariant _iter;

  explicit SmallSetIteratorCommon(SetItType it) : _iter(it) {}
  explicit SmallSetIteratorCommon(VecItType it) : _iter(it) {}

  void incr() {
    std::visit([](auto &&it) { ++it; }, _iter);
  }
  void decr() {
    std::visit([](auto &&it) { --it; }, _iter);
  }
  const T &get() const {
    return std::visit([](auto &&it) -> const T & { return *it; }, _iter);
  }
  const T &getPrev() const {
    return std::visit([](auto &&it) -> const T & { return *std::prev(it, 1); }, _iter);
  }

  SetItType toSetIt() const { return std::get<SetItType>(_iter); }
  VecItType toVecIt() const { return std::get<VecItType>(_iter); }

  template <class, class, bool>
  friend class SmallSetIterator;
  template <class, uintmax_t, class, class, class>
  friend class SmallSet;

 public:
  // Needed types for iterators
  using iterator_category = std::bidirectional_iterator_tag;
  using value_type = T;
  using difference_type = std::ptrdiff_t;
  using pointer = const T *;
  using reference = const T &;

  bool operator==(const SmallSetIteratorCommon &o) const { return o._iter == _iter; }
  bool operator!=(const SmallSetIteratorCommon &o) const { return !(*this == o); }
};

/**
 * Definition of the SmallSetIterator, depending on its direction.
 */
template <class T, class SetItType, bool Reversed>
class SmallSetIterator : public SmallSetIteratorCommon<T, SetItType> {
 public:
  SmallSetIterator(SetItType it) : SmallSetIteratorCommon<T, SetItType>(it) {}
  SmallSetIterator(typename SmallSetIteratorCommon<T, SetItType>::VecItType it)
      : SmallSetIteratorCommon<T, SetItType>(it) {}

  SmallSetIterator &operator++() {  // Prefix increment
    Reversed ? this->decr() : this->incr();
    return *this;
  }
  SmallSetIterator &operator--() {  // Prefix decrement
    Reversed ? this->incr() : this->decr();
    return *this;
  }
  SmallSetIterator operator++(int) {  // Postfix increment
    SmallSetIterator oldSelf = *this;
    Reversed ? this->decr() : this->incr();
    return oldSelf;
  }
  SmallSetIterator operator--(int) {  // Postfix decrement
    SmallSetIterator oldSelf = *this;
    Reversed ? this->incr() : this->decr();
    return oldSelf;
  }

  const T &operator*() const { return Reversed ? this->getPrev() : this->get(); }
  const T *operator->() const { return &static_cast<const SmallSetIterator *>(this)->operator*(); }
};

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
 */
template <class T, uintmax_t N, class Compare = std::less<T>, class Alloc = amc::allocator<T>,
          class SetType = typename std::set<T, Compare, Alloc>>
class SmallSet {
 private:
  using VecType = FixedCapacityVector<T, N, vec::UncheckedGrowingPolicy>;
  using SetIt = typename SetType::const_iterator;

  static_assert(N <= 64, "N should be small as search has linear complexity for small sets");

 public:
  using key_type = T;
  using value_type = T;
  using difference_type = ptrdiff_t;
  using allocator_type = Alloc;

  // Optim: If Set iterator type is a pointer, do not use SmallSetIterator but a pointer instead.
  // This is possible for SetType = FlatSet for instance.
  using iterator = typename std::conditional<std::is_same<SetIt, const T *>::value, const T *,
                                             SmallSetIterator<T, SetIt, false>>::type;
  using reverse_iterator =
      typename std::conditional<std::is_same<SetIt, const T *>::value, std::reverse_iterator<const T *>,
                                SmallSetIterator<T, SetIt, true>>::type;
  using const_iterator = iterator;
  using const_reverse_iterator = reverse_iterator;
  using reference = const T &;
  using const_reference = reference;
  using size_type = typename std::conditional<sizeof(typename VecType::size_type) < sizeof(typename SetType::size_type),
                                              typename SetType::size_type, typename VecType::size_type>::type;

  using key_compare = Compare;
  using value_compare = Compare;
  using pointer = const T *;
  using const_pointer = const T *;

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
    template <class, uintmax_t, class, class, class>
    friend class SmallSet;

    explicit node_type(Alloc alloc) : Alloc(alloc) {}
    explicit node_type(value_type &&v, Alloc alloc = Alloc()) : Alloc(alloc), _optV(std::move(v)) {}

    std::optional<value_type> _optV;
  };

  struct insert_return_type {
    iterator position;
    bool inserted;
    node_type node;
  };

  using trivially_relocatable =
      typename std::conditional<is_trivially_relocatable<VecType>::value && is_trivially_relocatable<SetType>::value,
                                std::true_type, std::false_type>::type;

  key_compare key_comp() const { return _set.key_comp(); }
  value_compare value_comp() const { return _set.value_comp(); }

  allocator_type get_allocator() const { return _set.get_allocator(); }

  SmallSet() noexcept(std::is_nothrow_default_constructible<VecType>::value
                          &&std::is_nothrow_default_constructible<SetType>::value) = default;

  explicit SmallSet(const Compare &comp, const Alloc &alloc = Alloc()) : _set(comp, alloc) {}

  explicit SmallSet(const Alloc &alloc) : _set(alloc) {}

  template <class InputIt>
  SmallSet(InputIt first, InputIt last, const Compare &comp = Compare(), const Alloc &alloc = Alloc())
      : _set(comp, alloc) {
    insert(first, last);
  }

  template <class InputIt>
  SmallSet(InputIt first, InputIt last, const Alloc &alloc) : SmallSet(first, last, Compare(), alloc) {}

  SmallSet(const SmallSet &o, const Alloc &alloc) : _vec(o._vec), _set(o._set, alloc) {}

  SmallSet(SmallSet &&o, const Alloc &alloc) : _vec(std::move(o._vec)), _set(std::move(o._set), alloc) {}

  SmallSet(std::initializer_list<T> list, const Compare &comp = Compare(), const Alloc &alloc = Alloc())
      : SmallSet(list.begin(), list.end(), comp, alloc) {}

  SmallSet(std::initializer_list<T> list, const Alloc &alloc) : SmallSet(list, Compare(), alloc) {}

  SmallSet &operator=(std::initializer_list<T> list) {
    clear();
    insert(list.begin(), list.end());
    return *this;
  }

  const_iterator begin() const noexcept { return isSmall() ? iterator(_vec.begin()) : iterator(_set.begin()); }
  const_iterator cbegin() const noexcept { return begin(); }
  const_iterator end() const noexcept { return isSmall() ? iterator(_vec.end()) : iterator(_set.end()); }
  const_iterator cend() const noexcept { return end(); }

  const_reverse_iterator rbegin() const noexcept {
    return isSmall() ? reverse_iterator(_vec.end()) : reverse_iterator(_set.end());
  }
  const_reverse_iterator crbegin() const noexcept { return rbegin(); }

  const_reverse_iterator rend() const noexcept {
    return isSmall() ? reverse_iterator(_vec.begin()) : reverse_iterator(_set.begin());
  }
  const_reverse_iterator crend() const noexcept { return rend(); }

  bool empty() const noexcept { return isSmall() ? _vec.empty() : _set.empty(); }
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

  std::pair<iterator, bool> insert(const T &v) { return isSmall() ? insert_small(v) : insert_set(v); }

  std::pair<iterator, bool> insert(T &&v) { return isSmall() ? insert_small(std::move(v)) : insert_set(std::move(v)); }

  template <class InputIt>
  void insert(InputIt first, InputIt last) {
    // Insert elements in vector as long as we stay small
    while (isSmall() && first != last) {
      insert_small(*first);
      ++first;
    }
    // Insert remaining elements (if any) in the standard set if we became large
    if (!isSmall() && first != last) {
      _set.insert(first, last);
    }
  }

  // We keep elements unsorted in the vector, we cannot have any value from hint

  iterator insert(const_iterator hint, const T &v) {
    return isSmall() ? insert_small(v).first : iterator(_set.insert(ToSetIt(hint), v));
  }

  iterator insert(const_iterator hint, T &&v) {
    return isSmall() ? insert_small(std::move(v)).first : _set.insert(ToSetIt(hint), std::move(v));
  }

  void insert(std::initializer_list<value_type> ilist) { insert(ilist.begin(), ilist.end()); }

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
      auto retIt = insert(hint, std::move(*nh._optV));
      nh._optV = std::nullopt;
      return retIt;
    }
    return end();
  }

  template <class... Args>
  std::pair<iterator, bool> emplace(Args &&...args) {
    if (!isSmall()) {
      return _set.emplace(std::forward<Args &&>(args)...);
    }
    if (isSmallContFull()) {
      return insert(T(std::forward<Args &&>(args)...));
    }
    miterator elIt = std::addressof(_vec.emplace_back(std::forward<Args &&>(args)...));
    miterator it = std::find_if(_vec.begin(), elIt, FindFunctor(key_comp(), *elIt));
    bool isNew = it == elIt;
    if (!isNew) {
      _vec.pop_back();
      elIt = it;
    }
    return std::pair<iterator, bool>(elIt, isNew);
  }

  template <class... Args>
  iterator emplace_hint(const_iterator hint, Args &&...args) {
    if (!isSmall()) {
      return _set.emplace_hint(ToSetIt(hint), std::forward<Args &&>(args)...);
    }
    // We cannot use any hint when we are small
    return emplace(std::forward<Args &&>(args)...).first;
  }

  const_iterator find(const_reference v) const {
    return isSmall() ? const_iterator(std::find_if(_vec.begin(), _vec.end(), FindFunctor(key_comp(), v)))
                     : const_iterator(_set.find(v));
  }

  bool contains(const_reference v) const { return find(v) != end(); }
  size_type count(const_reference v) const { return contains(v); }

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

  template <class I = const_iterator>
  iterator erase(const_iterator pos, typename std::enable_if<std::is_same<I, const T *>::value>::type * = 0) {
    return isSmall() ? _vec.erase(pos) : _set.erase(pos);
  }
  template <class I = const_iterator>
  iterator erase(const_iterator first, const_iterator last,
                 typename std::enable_if<std::is_same<I, const T *>::value>::type * = 0) {
    return isSmall() ? _vec.erase(first, last) : _set.erase(first, last);
  }

  template <class I = const_iterator>
  iterator erase(const_iterator pos, typename std::enable_if<!std::is_same<I, const T *>::value>::type * = 0) {
    return isSmall() ? iterator(_vec.erase(pos.toVecIt())) : iterator(_set.erase(pos.toSetIt()));
  }
  template <class I = const_iterator>
  iterator erase(const_iterator first, const_iterator last,
                 typename std::enable_if<!std::is_same<I, const T *>::value>::type * = 0) {
    return isSmall() ? iterator(_vec.erase(first.toVecIt(), last.toVecIt()))
                     : iterator(_set.erase(first.toSetIt(), last.toSetIt()));
  }

  void swap(SmallSet &o) noexcept(noexcept(std::declval<VecType>().swap(std::declval<VecType &>())) &&noexcept(
      std::declval<SetType>().swap(std::declval<SetType &>()))) {
    _vec.swap(o._vec);
    _set.swap(o._set);
  }

  node_type extract(const_iterator position) {
    if (isSmall()) {
      auto vecIt = position.toVecIt();
      node_type nt(std::move(*const_cast<typename VecType::iterator>(vecIt)), get_allocator());
      _vec.erase(vecIt);
      return nt;
    }
    auto setIt = position.toSetIt();
    auto setNt = _set.extract(setIt);
    node_type nt(std::move(setNt.value()), setNt.get_allocator());
    return nt;
  }

  node_type extract(const key_type &key) {
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
  template <uintmax_t N2, class C2, class SetType2>
  void merge(SmallSet<T, N2, C2, Alloc, SetType2> &o) {
    if (!o.isSmall()) {
      if (isSmall()) {
        grow();
      }
      _set.merge(o._set);
    } else {
      bool small = isSmall();
      for (auto oit = o._vec.begin(); oit != o._vec.end();) {
        FindFunctor fFunc(key_comp(), *oit);
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
  }

  bool operator==(const SmallSet &o) const {
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

  bool operator!=(const SmallSet &o) const { return !(*this == o); }

  // Comparison operators are provided for compliance with std::set but could be painful for performances
  // as we keep elements unsorted in the 'small' container.
  bool operator<(const SmallSet &o) const {
    struct LessPtrFunc {
      bool operator()(const_pointer pLhs, const_pointer pRhs) const { return *pLhs < *pRhs; }
      bool operator()(const_pointer pLhs, const_reference rhs) const { return *pLhs < rhs; }
      bool operator()(const_reference lhs, const_pointer pRhs) const { return lhs < *pRhs; }
    };
    if (isSmall()) {
      PtrVec sortedPtrs = ComputeSortedPtrVec(_vec);
      if (o.isSmall()) {
        // We are both small, we need to sort both containers
        PtrVec oSortedPtrs = ComputeSortedPtrVec(o._vec);
        return std::lexicographical_compare(sortedPtrs.begin(), sortedPtrs.end(), oSortedPtrs.begin(),
                                            oSortedPtrs.end(), LessPtrFunc());
      }
      // we are small: as we do not order elements in the small container, we need to sort them.
      return std::lexicographical_compare(sortedPtrs.begin(), sortedPtrs.end(), o._set.begin(), o._set.end(),
                                          LessPtrFunc());
    }
    if (o.isSmall()) {
      // other is small: as we do not order elements in the small container, we need to sort them.
      PtrVec oSortedPtrs = ComputeSortedPtrVec(o._vec);
      return std::lexicographical_compare(_set.begin(), _set.end(), oSortedPtrs.begin(), oSortedPtrs.end(),
                                          LessPtrFunc());
    }
    return _set < o._set;
  }
  bool operator<=(const SmallSet &o) const { return !(o < *this); }
  bool operator>(const SmallSet &o) const { return o < *this; }
  bool operator>=(const SmallSet &o) const { return !(*this < o); }

 private:
  using miterator = typename VecType::iterator;  // Custom iterator to allow modification of content (when small)

  template <class I>
  static inline SetIt ToSetIt(I it, typename std::enable_if<std::is_same<I, const T *>::value>::type * = 0) {
    return it;
  }
  template <class I>
  static inline SetIt ToSetIt(I it, typename std::enable_if<!std::is_same<I, const T *>::value>::type * = 0) {
    return it.toSetIt();
  }

  template <class V>
  std::pair<iterator, bool> insert_small(V &&v) {
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
  std::pair<iterator, bool> insert_set(V &&v) {
    return _set.insert(std::forward<V>(v));
  }

  struct FindFunctor {
    FindFunctor(Compare comp, const_reference v) : _comp(comp), _v(v) {}
    bool operator()(const_reference o) const { return !_comp(_v, o) && !_comp(o, _v); }
    Compare _comp;
    const_reference _v;
  };

  miterator mfind_small(const_reference v) {
    return std::find_if(_vec.begin(), _vec.end(), FindFunctor(key_comp(), v));
  }

  void grow() {
    _set.insert(std::make_move_iterator(_vec.begin()), std::make_move_iterator(_vec.end()));
    _vec.clear();
  }

  bool isSmall() const noexcept { return _set.empty(); }
  bool isSmallContFull() const noexcept { return _vec.size() == N; }

  using PtrVec = FixedCapacityVector<const_pointer, N, vec::UncheckedGrowingPolicy>;

  static PtrVec ComputeSortedPtrVec(const VecType &c) {
    PtrVec sortedPtrs;
    std::transform(c.begin(), c.end(), std::back_inserter(sortedPtrs),
                   static_cast<const_pointer (*)(const_reference)>(std::addressof));
    std::sort(sortedPtrs.begin(), sortedPtrs.end(),
              [](const_pointer p1, const_pointer p2) { return Compare()(*p1, *p2); });
    return sortedPtrs;
  }

  template <class, uintmax_t, class, class, class>
  friend class SmallSet;

  VecType _vec;
  SetType _set;
};

template <class T, uintmax_t N, class Compare, class Alloc, class SetType>
inline void swap(SmallSet<T, N, Compare, Alloc, SetType> &lhs, SmallSet<T, N, Compare, Alloc, SetType> &rhs) {
  lhs.swap(rhs);
}
}  // namespace amc
