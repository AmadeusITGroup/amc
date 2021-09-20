#pragma once

#include <cstddef>
#include <cstdlib>
#include <limits>
#include <new>

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
  void *allocate(size_t n) { return Alloc::instance().allocate(n); }
  void *reallocate(void *p, size_t oldSz, size_t newSz) { return Alloc::instance().reallocate(p, oldSz, newSz); }
  void deallocate(void *p, size_t n) { Alloc::instance().deallocate(p, n); }
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
 */
template <class T, class BasicAllocator>
class BasicAllocatorWrapper : private BasicAllocator {
 public:
  using value_type = T;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
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

  pointer address(reference r) const noexcept { return std::addressof(r); }
  const_pointer address(const_reference r) const noexcept { return std::addressof(r); }

  pointer allocate(size_type n, const_pointer = 0) {
    return static_cast<pointer>(BasicAllocator::allocate(n * sizeof(value_type)));
  }

  pointer reallocate(pointer p, size_type oldCapacity, size_type newCapacity, size_type nConstructedElems) {
    return Reallocate(*this, p, oldCapacity, newCapacity, nConstructedElems);
  }

  void deallocate(pointer p, size_type s) { BasicAllocator::deallocate(p, s * sizeof(T)); }

  constexpr size_type max_size() const { return static_cast<size_type>(-1) / sizeof(value_type); }

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
  constexpr bool operator==(const BasicAllocatorWrapper<U, BasicAllocator> &) const {
    return std::is_empty<BasicAllocator>::value;
  }

  template <typename U>
  constexpr bool operator!=(const BasicAllocatorWrapper<U, BasicAllocator> &rhs) const {
    return !(*this == rhs);
  }

 private:
  template <class, class>
  friend class BasicAllocatorWrapper;

  template <class V = T>
  static typename std::enable_if<!amc::is_trivially_relocatable<V>::value, T *>::type Reallocate(
      BasicAllocator &basicAlloc, V *p, size_t oldCapacity, size_t newCapacity, size_t nConstructedElems) {
    T *newPtr = static_cast<T *>(basicAlloc.allocate(newCapacity * sizeof(T)));
    amc::uninitialized_relocate_n(p, nConstructedElems, newPtr);
    basicAlloc.deallocate(p, oldCapacity * sizeof(T));
    return newPtr;
  }

  template <class V = T>
  static typename std::enable_if<amc::is_trivially_relocatable<V>::value, T *>::type Reallocate(
      BasicAllocator &basicAlloc, V *p, size_t oldCapacity, size_t newCapacity, size_t) {
    return static_cast<T *>(basicAlloc.reallocate(p, oldCapacity * sizeof(T), newCapacity * sizeof(T)));
  }
};

template <class BasicAllocator>
struct BasicAllocatorWrapper<void, BasicAllocator> {
  using value_type = void;
  using size_type = size_t;
  using difference_type = ptrdiff_t;
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
  void *allocate(size_t n) {
    void *ptr = malloc(n);
    if (AMC_UNLIKELY(!ptr)) {
      throw std::bad_alloc();
    }
    return ptr;
  }

  void *reallocate(void *p, size_t, size_t newSz) {
    p = realloc(p, newSz);
    if (AMC_UNLIKELY(!p)) {
      throw std::bad_alloc();
    }
    return p;
  }

  void deallocate(void *p, size_t) { free(p); }
};

/**
 * Standard (STL conformant) allocator using std::allocator providing an extra 'reallocate' method.
 *
 * If type T is trivially relocatable, 'reallocate' will be optimized into a call to 'realloc',
 * otherwise it will simply allocate the new block and relocate all elements into it.
 */
template <class T>
using allocator = BasicAllocatorWrapper<T, SimpleAllocator>;
}  // namespace amc
