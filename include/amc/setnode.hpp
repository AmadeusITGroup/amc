#pragma once

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

#include "type_traits.hpp"

namespace amc {

template <class T, class Compare, class Alloc, class VecType>
class FlatSet;

template <class T, std::uintmax_t N, class Compare, class Alloc, class SetType>
class SmallSet;

namespace set_details {

/// Result of the insertion of a node handle in a set, like the 'insert_return_type' of the standard sets.
template <class Iterator, class NodeType>
struct InsertReturnType {
  Iterator position;
  bool inserted;
  NodeType node;
};

/// Node handle of FlatSet and SmallSet ('node_type'), owning at most one element extracted from a set.
/// Like the node handles of the standard sets, it only depends on the value and allocator types: nodes can be moved
/// between FlatSets and SmallSets of different comparators and underlying containers.
template <class T, class Alloc>
class NodeHandle : private Alloc {
 public:
  using value_type = T;
  using allocator_type = Alloc;

  NodeHandle() noexcept = default;

  NodeHandle(const NodeHandle&) = delete;
  /// Like the standard node handles, a moved from node is empty.
  NodeHandle(NodeHandle&& o) noexcept(std::is_nothrow_move_constructible<value_type>::value)
      : Alloc(std::move(static_cast<Alloc&>(o))), _optV(std::move(o._optV)) {
    o._optV.reset();
  }
  NodeHandle& operator=(const NodeHandle&) = delete;
  NodeHandle& operator=(NodeHandle&& o) noexcept(std::is_nothrow_move_constructible<value_type>::value &&
                                                 std::is_nothrow_move_assignable<value_type>::value) {
    if (this != &o) {
      static_cast<Alloc&>(*this) = std::move(static_cast<Alloc&>(o));
      _optV = std::move(o._optV);
      o._optV.reset();
    }
    return *this;
  }

  ~NodeHandle() = default;

  bool empty() const noexcept { return !_optV.has_value(); }

  explicit operator bool() const noexcept { return _optV.has_value(); }

  allocator_type get_allocator() const { return static_cast<allocator_type>(*this); }

  value_type& value() { return _optV.value(); }
  const value_type& value() const { return _optV.value(); }

  /// Like the standard node handles, the allocators are exchanged too: an empty node may hold the allocator of
  /// another container, and two nodes holding values should have equal allocators, unless they propagate on swap.
  void swap(NodeHandle& o) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                    amc::is_nothrow_swappable<T>::value) {
    _optV.swap(o._optV);  // first, as it may throw (the allocators cannot)
    using std::swap;
    swap(static_cast<Alloc&>(*this), static_cast<Alloc&>(o));
  }

  friend void swap(NodeHandle& lhs, NodeHandle& rhs) noexcept(std::is_nothrow_move_constructible<T>::value &&
                                                              amc::is_nothrow_swappable<T>::value) {
    lhs.swap(rhs);
  }

 private:
  template <class, class, class, class>
  friend class amc::FlatSet;

  template <class, std::uintmax_t, class, class, class>
  friend class amc::SmallSet;

  explicit NodeHandle(Alloc alloc) : Alloc(alloc) {}
  explicit NodeHandle(value_type&& v, Alloc alloc = Alloc()) : Alloc(alloc), _optV(std::move(v)) {}

  /// Implementation of 'insert(node_type&& nh)' of 'set', a FlatSet or a SmallSet.
  template <class Set>
  static typename Set::insert_return_type InsertInto(Set& set, NodeHandle&& nh) {
    using InsertReturnType = typename Set::insert_return_type;
    if (!nh) {
      return InsertReturnType{set.end(), false, NodeHandle()};
    }
    // the value is only moved from if it is inserted: it stays in 'nh' until the insertion succeeds or fails
    const auto ret = set.insert(std::move(*nh._optV));
    if (ret.second) {
      nh._optV.reset();
      return InsertReturnType{ret.first, true, NodeHandle()};
    }
    return InsertReturnType{ret.first, false, std::move(nh)};
  }

  /// Implementation of 'insert(const_iterator hint, node_type&& nh)' of 'set', a FlatSet or a SmallSet.
  template <class Set>
  static typename Set::iterator InsertInto(Set& set, typename Set::const_iterator hint, NodeHandle&& nh) {
    if (!nh) {
      return set.end();
    }
    // the value is only moved from if it is inserted
    const auto oldSize = set.size();
    const typename Set::iterator it = set.insert(hint, std::move(*nh._optV));
    if (set.size() != oldSize) {
      nh._optV.reset();
    }
    return it;
  }

  std::optional<value_type> _optV;
};

}  // namespace set_details
}  // namespace amc
