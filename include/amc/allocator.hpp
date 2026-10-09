#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include "config.hpp"
#include "memory.hpp"
#include "type_traits.hpp"

namespace amc {

/**
 * Adaptor that wraps a singleton class Alloc into a "basic" allocator.
 * Alloc is expected to model the "basic" allocator concept and to provide
 * a static instance() method to access singleton.
 *
 * A basic allocator is a class that provides methods:
 * void *allocate(size_t n)
 * void *reallocate(void *p, size_t oldSz, size_t newSz)
 * void deallocate(void *p, size_t n)
 */
template <typename Alloc>
class BasicSingletonAllocatorAdaptor {
 public:
  void *allocate(std::size_t n) { return Alloc::instance().allocate(n); }
  void *reallocate(void *p, std::size_t oldSz, std::size_t newSz) {
    return Alloc::instance().reallocate(p, oldSz, newSz);
  }
  void deallocate(void *p, std::size_t n) { Alloc::instance().deallocate(p, n); }
};

/**
 * Creates a standard (STL conformant) allocator from a 'basic allocator' providing an extra 'reallocate' method.
 *
 * A basic allocator is a class that provides methods:
 * void *allocate(size_t n)
 * void *reallocate(void *p, size_t oldSz, size_t newSz)
 * void deallocate(void *p, size_t n)
 *
 * If type T is trivially relocatable, 'reallocate' will be optimized into a call to 'realloc',
 * otherwise it will simply allocate the new block and relocate all elements into it.
 *
 * Over-aligned types (with an alignment larger than 'alignof(std::max_align_t)', the only one guaranteed by malloc) are
 * supported: memory is then over-allocated to align the elements, and 'reallocate' does not call 'realloc' for them.
 *
 * Wrappers of an empty basic allocator are always equal. A basic allocator with a state must provide an 'operator=='
 * telling whether the memory allocated by one of them can be deallocated by the other one (copies must be equal).
 */
template <class T, class BasicAllocator>
class BasicAllocatorWrapper : private BasicAllocator {
 public:
  using value_type = T;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = T *;
  using const_pointer = const T *;
  using reference = T &;
  using const_reference = const T &;

  BasicAllocatorWrapper() = default;

  BasicAllocatorWrapper(const BasicAllocatorWrapper &) = default;
  BasicAllocatorWrapper(BasicAllocatorWrapper &&) = default;
  BasicAllocatorWrapper &operator=(const BasicAllocatorWrapper &) noexcept = default;
  BasicAllocatorWrapper &operator=(BasicAllocatorWrapper &&) noexcept = default;

  template <class U>
  BasicAllocatorWrapper(const BasicAllocatorWrapper<U, BasicAllocator> &o) : BasicAllocator(o) {}

  /// Wraps a copy of 'basicAllocator' (useful for basic allocators with a state).
  explicit BasicAllocatorWrapper(const BasicAllocator &basicAllocator) : BasicAllocator(basicAllocator) {}

  pointer address(reference r) const noexcept { return std::addressof(r); }
  const_pointer address(const_reference r) const noexcept { return std::addressof(r); }

  /// Throws std::bad_array_new_length (like std::allocator) if the number of bytes for 'n' elements exceeds size_t.
  pointer allocate(size_type n, const_pointer = 0) {
    CheckSize(n);
    return Allocate(n, IsOverAligned<>());
  }

  pointer reallocate(pointer p, size_type oldCapacity, size_type newCapacity, size_type nConstructedElems) {
    return Reallocate(p, oldCapacity, newCapacity, nConstructedElems, CanUseBasicReallocate<>());
  }

  void deallocate(pointer p, size_type s) { Deallocate(p, s, IsOverAligned<>()); }

  /// Largest number of elements whose number of bytes (including the extra ones of over-aligned types) fits in size_t.
  constexpr size_type max_size() const noexcept {
    return (static_cast<size_type>(-1) - (IsOverAligned<>::value ? OverAlignedExtraBytes() : 0U)) / sizeof(value_type);
  }

  template <class U, class... Args>
  void construct(U *p, Args &&...args) {
    amc::construct_at(p, std::forward<Args>(args)...);
  }

  template <class U>
  void destroy(U *p) {
    amc::destroy_at(p);
  }

  template <class U>
  struct rebind {
    using other = BasicAllocatorWrapper<U, BasicAllocator>;
  };

  template <typename U>
  constexpr bool operator==(const BasicAllocatorWrapper<U, BasicAllocator> &o) const {
    return IsEqual(o, std::is_empty<BasicAllocator>());
  }

  template <typename U>
  constexpr bool operator!=(const BasicAllocatorWrapper<U, BasicAllocator> &rhs) const {
    return !(*this == rhs);
  }

 private:
  template <class, class>
  friend class BasicAllocatorWrapper;

  // The following traits are alias templates to evaluate 'alignof(T)' only when used, as T may be incomplete when this
  // class is instantiated (vector of an incomplete type).

  /// Over-aligned types have an alignment larger than the one guaranteed by the basic allocator (like malloc).
  template <class V = T>
  using IsOverAligned = std::integral_constant<bool, (alignof(V) > alignof(std::max_align_t))>;

  /// Basic allocator 'reallocate' (like realloc) moves elements with memcpy and does not keep over-alignment.
  template <class V = T>
  using CanUseBasicReallocate =
      std::integral_constant<bool, amc::is_trivially_relocatable<V>::value && !IsOverAligned<V>::value>;

  /// Over-aligned allocations have extra bytes to align the elements, with room before them to store their offset to
  /// the start of the allocated block.
  template <class V = T>
  static constexpr std::size_t OverAlignedExtraBytes() {
    return sizeof(std::size_t) + alignof(V) - 1U;
  }

  template <class U>
  constexpr bool IsEqual(const BasicAllocatorWrapper<U, BasicAllocator> &, std::true_type) const {
    return true;
  }

  template <class U>
  constexpr bool IsEqual(const BasicAllocatorWrapper<U, BasicAllocator> &o, std::false_type) const {
    return static_cast<const BasicAllocator &>(*this) == static_cast<const BasicAllocator &>(o);
  }

  void CheckSize(size_type n) const {
    if (AMC_UNLIKELY(max_size() < n)) {
      throw std::bad_array_new_length();
    }
  }

  pointer Allocate(size_type n, std::false_type) {
    return static_cast<pointer>(BasicAllocator::allocate(n * sizeof(T)));
  }

  pointer Allocate(size_type n, std::true_type) {
    const std::size_t nbBytes = n * sizeof(T);
    char *block = static_cast<char *>(BasicAllocator::allocate(nbBytes + OverAlignedExtraBytes()));
    void *elems = block + sizeof(std::size_t);
    std::size_t space = nbBytes + alignof(T) - 1U;
    std::align(alignof(T), nbBytes, elems, space);  // cannot fail thanks to the extra bytes
    const std::size_t offset = static_cast<std::size_t>(static_cast<char *>(elems) - block);
    std::memcpy(static_cast<char *>(elems) - sizeof(std::size_t), &offset, sizeof(std::size_t));
    return static_cast<pointer>(elems);
  }

  void Deallocate(pointer p, size_type n, std::false_type) { BasicAllocator::deallocate(p, n * sizeof(T)); }

  void Deallocate(pointer p, size_type n, std::true_type) {
    if (p != nullptr) {
      char *elems = reinterpret_cast<char *>(p);
      std::size_t offset;
      std::memcpy(&offset, elems - sizeof(std::size_t), sizeof(std::size_t));
      BasicAllocator::deallocate(elems - offset, n * sizeof(T) + OverAlignedExtraBytes());
    }
  }

  /// If a move throws, the new memory is deallocated and the elements stay in 'p'.
  pointer Reallocate(pointer p, size_type oldCapacity, size_type newCapacity, size_type nConstructedElems,
                     std::false_type) {
    pointer newPtr = allocate(newCapacity);
    try {
      amc::uninitialized_relocate_n(p, nConstructedElems, newPtr);
    } catch (...) {
      deallocate(newPtr, newCapacity);
      throw;
    }
    deallocate(p, oldCapacity);
    return newPtr;
  }

  pointer Reallocate(pointer p, size_type oldCapacity, size_type newCapacity, size_type, std::true_type) {
    CheckSize(newCapacity);
    return static_cast<pointer>(BasicAllocator::reallocate(p, oldCapacity * sizeof(T), newCapacity * sizeof(T)));
  }
};

template <class BasicAllocator>
struct BasicAllocatorWrapper<void, BasicAllocator> {
  using value_type = void;
  using size_type = std::size_t;
  using difference_type = std::ptrdiff_t;
  using pointer = void *;
  using const_pointer = const void *;

  template <class U>
  struct rebind {
    using other = BasicAllocatorWrapper<U, BasicAllocator>;
  };
};

/**
 * Wrapper around std::allocator that models the "basic" allocator concept.
 * A basic allocator is a class that provides methods:
 * void * allocate(size_t n)
 * void *reallocate(void *p, size_t oldSz, size_t newSz)
 * void deallocate(void *p, size_t n)
 */
struct SimpleAllocator {
  void *allocate(std::size_t n) {
    void *ptr = std::malloc(n);
    if (AMC_UNLIKELY(!ptr)) {
      throw std::bad_alloc();
    }
    return ptr;
  }

  void *reallocate(void *p, std::size_t, std::size_t newSz) {
    p = std::realloc(p, newSz);
    if (AMC_UNLIKELY(!p)) {
      throw std::bad_alloc();
    }
    return p;
  }

  void deallocate(void *p, std::size_t) { std::free(p); }
};

/**
 * Standard (STL conformant) allocator using std::allocator providing an extra 'reallocate' method.
 *
 * If type T is trivially relocatable (and not over-aligned), 'reallocate' will be optimized into a call to 'realloc',
 * otherwise it will simply allocate the new block and relocate all elements into it.
 */
template <class T>
using allocator = BasicAllocatorWrapper<T, SimpleAllocator>;
}  // namespace amc
