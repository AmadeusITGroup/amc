#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

#include "config.hpp"
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

/// Node handle of FlatSet and SmallSet ('node_type'), owning at most one element extracted from a set. Like the node
/// handles of the standard sets, it only depends on the value and allocator types: nodes can be moved between FlatSets
/// and SmallSets of different comparators and underlying containers. It holds an allocator only when it holds an
/// element, so the allocator type does not need to be default constructible nor assignable
/// (std::pmr::polymorphic_allocator, for instance). Its element is built and destroyed with this allocator
/// (uses-allocator construction for std::pmr).
template <class T, class Alloc>
class NodeHandle {
 private:
  using AllocTraits = std::allocator_traits<Alloc>;

  /// The element of a node moves with the allocator of the node (equal to the one it was built with): like a move
  /// constructor, the construction is assumed not to throw if the move constructor of the element does not.
  static constexpr bool kNothrowMove = std::is_nothrow_move_constructible<T>::value;

 public:
  using value_type = T;
  using allocator_type = Alloc;

  NodeHandle() noexcept {}

  NodeHandle(const NodeHandle&) = delete;

  /// Like the standard node handles, a moved from node is empty.
  NodeHandle(NodeHandle&& o) noexcept(kNothrowMove) {
    if (o._alloc) {
      emplace(*o._alloc, std::move(o._s.v));
      o.reset();
    }
  }

  NodeHandle& operator=(const NodeHandle&) = delete;

  /// Like the standard node handles, the allocator is replaced only if this node is empty or if the allocators
  /// propagate on move assignment (otherwise they should be equal). If the move of the element throws, this node is
  /// left empty and 'o' is unchanged.
  NodeHandle& operator=(NodeHandle&& o) noexcept(kNothrowMove) {
    if (this != &o) {
      if (o._alloc) {
        if (_alloc) {
          AllocTraits::destroy(*_alloc, std::addressof(_s.v));
          if constexpr (AllocTraits::propagate_on_container_move_assignment::value) {
            *_alloc = *o._alloc;
          }
        } else {
          _alloc.emplace(*o._alloc);
        }
        if constexpr (kNothrowMove) {
          AllocTraits::construct(*_alloc, std::addressof(_s.v), std::move(o._s.v));
        } else {
          AMC_TRY {
            AllocTraits::construct(*_alloc, std::addressof(_s.v), std::move(o._s.v));
          } AMC_CATCH_ALL {
            _alloc.reset();
            AMC_RETHROW;
          }
        }
        o.reset();
      } else {
        reset();
      }
    }
    return *this;
  }

  ~NodeHandle() { reset(); }

  bool empty() const noexcept { return !_alloc.has_value(); }

  explicit operator bool() const noexcept { return _alloc.has_value(); }

  /// The node must not be empty.
  allocator_type get_allocator() const { return *_alloc; }

  /// Like the standard node handles, the value of a const node can be modified. The node must not be empty.
  value_type& value() const { return _s.v; }

  /// Like the standard node handles, the allocators are exchanged only if one of the nodes is empty or if they
  /// propagate on swap (otherwise they should be equal).
  void swap(NodeHandle& o) noexcept(kNothrowMove && amc::is_nothrow_swappable<T>::value) {
    if (!o._alloc) {
      o = std::move(*this);
    } else if (!_alloc) {
      *this = std::move(o);
    } else {
      using std::swap;
      swap(_s.v, o._s.v);
      if constexpr (AllocTraits::propagate_on_container_swap::value) {
        swap(*_alloc, *o._alloc);
      }
    }
  }

  friend void swap(NodeHandle& lhs, NodeHandle& rhs) noexcept(noexcept(lhs.swap(rhs))) { lhs.swap(rhs); }

 private:
  template <class, class, class, class>
  friend class amc::FlatSet;

  template <class, std::uintmax_t, class, class, class>
  friend class amc::SmallSet;

  /// Node holding 'v', extracted from a container of allocator 'alloc'.
  NodeHandle(value_type&& v, const Alloc& alloc) { emplace(alloc, std::move(v)); }

  /// Builds the element of this empty node from 'args' with (a copy of) 'alloc'. If this throws, the node stays empty.
  template <class... Args>
  void emplace(const Alloc& alloc, Args&&... args) {
    _alloc.emplace(alloc);
    AMC_TRY {
      AllocTraits::construct(*_alloc, std::addressof(_s.v), std::forward<Args>(args)...);
    } AMC_CATCH_ALL {
      _alloc.reset();
      AMC_RETHROW;
    }
  }

  void reset() noexcept {
    if (_alloc) {
      AllocTraits::destroy(*_alloc, std::addressof(_s.v));
      _alloc.reset();
    }
  }

  /// Implementation of 'insert(node_type&& nh)' of 'set', a FlatSet or a SmallSet.
  template <class Set>
  static typename Set::insert_return_type InsertInto(Set& set, NodeHandle&& nh) {
    using InsertReturnType = typename Set::insert_return_type;
    if (!nh) {
      return InsertReturnType{set.end(), false, NodeHandle()};
    }
    // the value is only moved from if it is inserted: it stays in 'nh' until the insertion succeeds or fails
    const auto ret = set.insert(std::move(nh._s.v));
    if (ret.second) {
      nh.reset();
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
    const typename Set::iterator it = set.insert(hint, std::move(nh._s.v));
    if (set.size() != oldSize) {
      nh.reset();
    }
    return it;
  }

  /// Storage of the element, alive if and only if the node holds an allocator.
  union Storage {
    Storage() noexcept {}

    Storage(const Storage&) = delete;
    Storage(Storage&&) noexcept = delete;
    Storage& operator=(const Storage&) = delete;
    Storage& operator=(Storage&&) noexcept = delete;

    ~Storage() {}

    T v;
  };

  /// Mutable, as the value of a const node can be modified (see 'value').
  mutable Storage _s;
  std::optional<Alloc> _alloc;
};

}  // namespace set_details
}  // namespace amc
