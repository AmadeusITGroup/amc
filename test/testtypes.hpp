#pragma once

#include <amc/type_traits.hpp>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>

#ifdef AMC_CXX20
#include <compare>
#endif

namespace amc {

struct Foo {
  Foo(int32_t i = 0) : _ptr(malloc(i)), _c(0U), _i(static_cast<int16_t>(i)) {}

  Foo(const Foo &foo) : _ptr(malloc(foo._i)), _c(foo._c), _i(foo._i) {}

  Foo &operator=(const Foo &foo) {
    _ptr = realloc(_ptr, foo._i);
    _c = foo._c;
    _i = foo._i;
    return *this;
  }

  Foo(Foo &&foo) noexcept : _ptr(foo._ptr), _c(foo._c), _i(foo._i) { foo._ptr = nullptr; }

  Foo &operator=(Foo &&foo) noexcept {
    std::swap(_ptr, foo._ptr);
    std::swap(_c, foo._c);
    std::swap(_i, foo._i);
    return *this;
  }

  ~Foo() { free(_ptr); }

  operator int32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const Foo &o) const { return _i <=> o._i; }
#endif

  void *_ptr;
  int8_t _c;
  int16_t _i;
};

struct TriviallyCopyableType {
  TriviallyCopyableType(int32_t i = 0) : _c(0U), _i(static_cast<int16_t>(i)) {}

  operator int32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const TriviallyCopyableType &o) const { return _i <=> o._i; }
#endif

  int8_t _c;
  int16_t _i;
};

struct NonCopyableType {
  NonCopyableType(int i = 7) : _i(i) {}

  NonCopyableType(const NonCopyableType &) = delete;
  NonCopyableType &operator=(const NonCopyableType &) = delete;

  NonCopyableType(NonCopyableType &&o) noexcept : _i(o._i) {}

  NonCopyableType &operator=(NonCopyableType &&o) noexcept {
    if (this != &o) {
      _i = o._i;
    }
    return *this;
  }

  bool operator==(const NonCopyableType &o) const { return _i == o._i; }

#ifdef AMC_CXX20
  auto operator<=>(const NonCopyableType &o) const { return _i <=> o._i; }
#endif

  operator int32_t() const { return _i; }

  int _i;
};

struct SimpleNonTriviallyCopyableType {
  SimpleNonTriviallyCopyableType(int i = 7) : _i(i) {}

  ~SimpleNonTriviallyCopyableType() {}

  bool operator==(const SimpleNonTriviallyCopyableType &o) const { return _i == o._i; }

  operator int32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const SimpleNonTriviallyCopyableType &o) const { return _i <=> o._i; }
#endif

  int _i;
};

struct NonTrivialType {
  NonTrivialType(uint32_t i = 0U) : _i(i) {}

  operator uint32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const NonTrivialType &o) const { return _i <=> o._i; }
#endif

  uint32_t _i;
};

static_assert(!std::is_trivial<NonTrivialType>::value, "");
static_assert(std::is_trivially_copyable<NonTrivialType>::value, "");
static_assert(!std::is_trivially_copyable<SimpleNonTriviallyCopyableType>::value, "");

struct TypeStats {
  static TypeStats _stats;

  void start() { _count = true; }
  void end() { _count = false; }

  void construct() {
    if (_count) {
      ++_nbConstructs;
    }
  }
  void copyConstruct() {
    if (_count) {
      ++_nbCopyConstructs;
    }
  }
  void moveConstruct() {
    if (_count) {
      ++_nbMoveConstructs;
    }
  }
  void copyAssign() {
    if (_count) {
      ++_nbCopyAssignments;
    }
  }
  void moveAssign() {
    if (_count) {
      ++_nbMoveAssignments;
    }
  }
  void destruct() {
    if (_count) {
      ++_nbDestructs;
    }
  }
  void malloc() {
    if (_count) {
      ++_nbMallocs;
    }
  }
  void realloc() {
    if (_count) {
      ++_nbReallocs;
    }
  }
  void free() {
    if (_count) {
      ++_nbFree;
    }
  }

  size_t _nbConstructs{};
  size_t _nbCopyConstructs{};
  size_t _nbMoveConstructs{};
  size_t _nbCopyAssignments{};
  size_t _nbMoveAssignments{};
  size_t _nbDestructs{};

  size_t _nbMallocs{};
  size_t _nbReallocs{};
  size_t _nbFree{};

  bool _count{};
};

template <bool IsTriviallyRelocatable>
struct ComplexType {
  static const size_t kMaxMallocSize = 10000;

  ComplexType(uint32_t i = 0) : _ptr(malloc(i % kMaxMallocSize)), _c(0U), _i(i) {
    if (i % kMaxMallocSize != 0 && !_ptr) {
      throw std::bad_alloc();
    }
    TypeStats::_stats.construct();
    if (_ptr) {
      TypeStats::_stats.malloc();
    }
  }

  explicit ComplexType(const ComplexType *p) : _ptr(malloc(p->_i % kMaxMallocSize)), _c(0U), _i(p->_i) {
    if (p->_i % kMaxMallocSize != 0 && !_ptr) {
      throw std::bad_alloc();
    }
    TypeStats::_stats.construct();
    if (_ptr) {
      TypeStats::_stats.malloc();
    }
  }

  ComplexType(const ComplexType &o) : _ptr(malloc(o._i % kMaxMallocSize)), _c(o._c), _i(o._i) {
    TypeStats::_stats.copyConstruct();
    if (_ptr) {
      TypeStats::_stats.malloc();
    }
  }

  ComplexType &operator=(const ComplexType &o) {
    if (this != &o) {
      if (o._i > _i) {
        if (_ptr) {
          TypeStats::_stats.realloc();
        } else {
          TypeStats::_stats.malloc();
        }
        void *newPtr = realloc(_ptr, o._i % kMaxMallocSize);
        if (!newPtr) {
          throw std::bad_alloc();
        }
        _ptr = newPtr;
      }
      _c = o._c;
      _i = o._i;
      TypeStats::_stats.copyAssign();
    }
    return *this;
  }

  ComplexType(ComplexType &&o) noexcept : _ptr(o._ptr), _c(o._c), _i(o._i) {
    o._ptr = nullptr;
    o._i = 0;
    TypeStats::_stats.moveConstruct();
  }

  ComplexType &operator=(ComplexType &&o) noexcept {
    if (this != &o) {
      if (_ptr) {
        TypeStats::_stats.free();
      }
      free(_ptr);
      _ptr = o._ptr;
      _i = o._i;
      o._ptr = nullptr;
      o._i = 0;
      TypeStats::_stats.moveAssign();
    }
    return *this;
  }

  ~ComplexType() {
    if (_ptr) {
      TypeStats::_stats.free();
    }
    free(_ptr);
    TypeStats::_stats.destruct();
  }

  operator uint32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const ComplexType &o) const { return _i <=> o._i; }
#else
  bool operator==(const ComplexType &o) const { return _i == o._i; }
  bool operator<(const ComplexType &o) const { return _i < o._i; }
  bool operator>(const ComplexType &o) const { return _i > o._i; }
#endif

  using trivially_relocatable =
      typename std::conditional<IsTriviallyRelocatable, std::true_type, std::false_type>::type;

  void *_ptr;
  int8_t _c;
  uint32_t _i;
};

using ComplexNonTriviallyRelocatableType = ComplexType<false>;
using ComplexTriviallyRelocatableType = ComplexType<true>;

struct NonTriviallyRelocatableType {
  NonTriviallyRelocatableType(uint32_t i = 0) : _data(i) {}
  operator uint32_t() const { return _data._i; }
  ComplexNonTriviallyRelocatableType _data;
};

static_assert(!std::is_trivially_copyable<ComplexTriviallyRelocatableType>::value, "");
static_assert(amc::is_trivially_relocatable<ComplexTriviallyRelocatableType>::value, "");
static_assert(!amc::is_trivially_relocatable<ComplexNonTriviallyRelocatableType>::value, "");
static_assert(!amc::is_trivially_relocatable<NonTriviallyRelocatableType>::value, "");

struct MoveForbiddenException {};

template <bool IsTriviallyRelocatable>
struct MoveForbidden {
  MoveForbidden() = default;

  MoveForbidden(const MoveForbidden &) = delete;
  MoveForbidden &operator=(const MoveForbidden &) = delete;
  MoveForbidden(MoveForbidden &&) { throw MoveForbiddenException(); }
  MoveForbidden &operator=(MoveForbidden &&) { throw MoveForbiddenException(); }

  using trivially_relocatable =
      typename std::conditional<IsTriviallyRelocatable, std::true_type, std::false_type>::type;
};

template <unsigned int Size>
struct UnalignedToPtr {
  static constexpr size_t kIntSize = Size < sizeof(uint32_t) ? Size : sizeof(uint32_t);

  UnalignedToPtr(uint32_t i) { std::memcpy(c, &i, kIntSize); }

  operator uint32_t() const {
    uint32_t ret{};
    std::memcpy(&ret, c, kIntSize);
    return ret;
  }

  char c[Size];
};

template <unsigned int Size, class T>
struct UnalignedToPtr2 {
  static constexpr size_t kIntSize = Size < sizeof(uint32_t) ? Size : sizeof(uint32_t);

  UnalignedToPtr2(uint32_t i) { std::memcpy(c, &i, kIntSize); }

  operator uint32_t() const {
    uint32_t ret{};
    std::memcpy(&ret, c, kIntSize);
    return ret;
  }

  T e;
  char c[Size];
};

class TestAllocator {
 public:
  void *allocate(size_t n) {
    if (n > 20) {
      throw std::bad_alloc();
    }
    return bytes;
  }

  void deallocate(void *, size_t) {}

 private:
  char bytes[20];
};

struct BiggerAllocateException {};

class TestReallocateAllocator {
 public:
  void *allocate(size_t n) {
    if (n > 10) {
      throw BiggerAllocateException();
    }
    return bytes;
  }

  void *reallocate(void *, size_t, size_t newSz) {
    if (newSz > 20) {
      throw std::bad_alloc();
    }
    return bytes;
  }
  void deallocate(void *, size_t) {}

 private:
  char bytes[20];
};

}  // namespace amc
