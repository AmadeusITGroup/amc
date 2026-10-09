#pragma once

#include <amc/allocator.hpp>
#include <amc/type_traits.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>

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
  auto operator<=>(const NonCopyableType &o) const = default;
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
  auto operator<=>(const SimpleNonTriviallyCopyableType &o) const = default;
#endif

  int _i;
};

struct NonTrivialType {
  NonTrivialType(uint32_t i = 0U) : _i(i) {}

  operator uint32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const NonTrivialType &o) const = default;
#endif

  uint32_t _i;
};

/// Over-aligned type: its alignment is larger than the one guaranteed by malloc (alignof(std::max_align_t)).
struct alignas(64) OverAlignedType {
  OverAlignedType(int32_t i = 0) : _i(i) {}

  operator int32_t() const { return _i; }

#ifdef AMC_CXX20
  auto operator<=>(const OverAlignedType &o) const = default;
#endif

  int32_t _i;
  char _padding[60]{};  // explicit padding to its alignment, as MSVC warns about implicit padding (C4324)
};

static_assert(!std::is_trivial<NonTrivialType>::value, "");
static_assert(std::is_trivially_copyable<NonTrivialType>::value, "");
static_assert(!std::is_trivially_copyable<SimpleNonTriviallyCopyableType>::value, "");
static_assert(alignof(OverAlignedType) > alignof(std::max_align_t), "");
static_assert(sizeof(OverAlignedType) == alignof(OverAlignedType), "");

/// Type whose move assignment does not preserve its value when self move assigned, which is allowed by the standard
/// (Cpp17MoveAssignable specifies the value of the assigned object only when it is not the moved-from object).
struct SelfMoveUnsafeType {
  static constexpr int32_t kMovedFromValue = -1;

  SelfMoveUnsafeType(int32_t i = 0) : _i(i) {}

  SelfMoveUnsafeType(const SelfMoveUnsafeType &) = default;
  SelfMoveUnsafeType &operator=(const SelfMoveUnsafeType &) = default;

  SelfMoveUnsafeType(SelfMoveUnsafeType &&o) noexcept : _i(o._i) { o._i = kMovedFromValue; }

  SelfMoveUnsafeType &operator=(SelfMoveUnsafeType &&o) noexcept {
    _i = o._i;
    o._i = kMovedFromValue;
    return *this;
  }

  operator int32_t() const { return _i; }

  int32_t _i;
};

static_assert(!amc::is_trivially_relocatable<SelfMoveUnsafeType>::value, "");

/// Stateful comparator of integers, comparing their remainders of the division by the modulo given at construction.
/// A default constructed one compares the integers themselves (if they are non negative).
struct ModuloCompare {
  ModuloCompare() = default;
  explicit ModuloCompare(int32_t modulo) : _modulo(modulo) {}

  bool operator()(int32_t lhs, int32_t rhs) const { return lhs % _modulo < rhs % _modulo; }

  int32_t _modulo = std::numeric_limits<int32_t>::max();
};

/// Type only providing operator== and operator< (no operator<=>, no conversion to an arithmetic type): from C++20,
/// containers of this type should still be comparable, like the standard ones which use 'synth-three-way'.
struct LessComparableType {
  LessComparableType(int32_t i = 0) : _i(i) {}

  bool operator==(const LessComparableType &o) const { return _i == o._i; }
  bool operator<(const LessComparableType &o) const { return _i < o._i; }

  int32_t _i;
};

struct CopyException {};

struct TypeStats {
  static TypeStats _stats;

  /// Called by each copy (construction or assignment) of a ComplexType, before it modifies anything.
  /// Throws CopyException when the number of copies set in '_nbCopiesBeforeThrow' have been done, never if negative.
  void copy() {
    if (_nbCopiesBeforeThrow >= 0 && _nbCopiesBeforeThrow-- == 0) {
      throw CopyException();
    }
  }

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

  int _nbCopiesBeforeThrow{-1};

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

  ComplexType(const ComplexType &o) : _ptr(MallocCopy(o)), _c(o._c), _i(o._i) {
    TypeStats::_stats.copyConstruct();
    if (_ptr) {
      TypeStats::_stats.malloc();
    }
  }

  ComplexType &operator=(const ComplexType &o) {
    if (this != &o) {
      TypeStats::_stats.copy();
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

 private:
  static void *MallocCopy(const ComplexType &o) {
    TypeStats::_stats.copy();
    return malloc(o._i % kMaxMallocSize);
  }
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

/// Non trivially relocatable type with a value, whose moves throw MoveForbiddenException once 'NbMovesBeforeThrow()'
/// moves have been done (never if negative). A throwing move has already written its value: relocating elements to
/// the inline storage of a SmallVector, which shares its first bytes with the pointer to its dynamic storage, then
/// overwrites this pointer. Counts its live objects, to detect leaks.
struct ThrowingMoveType {
  static int &NbMovesBeforeThrow() {
    static int nbMovesBeforeThrow = -1;
    return nbMovesBeforeThrow;
  }
  static int &NbLive() {
    static int nbLive = 0;
    return nbLive;
  }

  ThrowingMoveType(int32_t i = 0) : _i(i) { ++NbLive(); }
  ThrowingMoveType(const ThrowingMoveType &o) : _i(o._i) { ++NbLive(); }
  ThrowingMoveType(ThrowingMoveType &&o) : _i(o._i) {
    Move();
    ++NbLive();
  }
  ThrowingMoveType &operator=(const ThrowingMoveType &o) = default;
  ThrowingMoveType &operator=(ThrowingMoveType &&o) {
    _i = o._i;
    Move();
    return *this;
  }
  ~ThrowingMoveType() { --NbLive(); }

  operator int32_t() const { return _i; }

  static void Move() {
    int &nbMovesBeforeThrow = NbMovesBeforeThrow();
    if (nbMovesBeforeThrow >= 0 && nbMovesBeforeThrow-- == 0) {
      throw MoveForbiddenException();
    }
  }

  int32_t _i;
};

static_assert(!amc::is_trivially_relocatable<ThrowingMoveType>::value, "");

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

/// Basic allocator counting its live allocations, to detect memory leaks without relying on sanitizers.
class CountingAllocator {
 public:
  static int64_t &NbLiveAllocations() {
    static int64_t nbLiveAllocations = 0;
    return nbLiveAllocations;
  }

  void *allocate(size_t n) {
    void *p = malloc(n);
    if (!p) {
      throw std::bad_alloc();
    }
    ++NbLiveAllocations();
    return p;
  }

  void *reallocate(void *p, size_t, size_t newSz) {
    void *newPtr = realloc(p, newSz);
    if (!newPtr) {
      throw std::bad_alloc();
    }
    if (!p) {
      ++NbLiveAllocations();
    }
    return newPtr;
  }

  void deallocate(void *p, size_t) {
    if (p) {
      --NbLiveAllocations();
    }
    free(p);
  }
};

/// Basic allocator with a state, its arena: allocators of the same arena are equal. Each allocated block records the
/// arena of its allocator, to count the blocks deallocated by an allocator of another arena, which must never happen.
class ArenaAllocator {
 public:
  static int64_t &NbLiveAllocations() {
    static int64_t nbLiveAllocations = 0;
    return nbLiveAllocations;
  }
  static int64_t &NbArenaMismatches() {
    static int64_t nbArenaMismatches = 0;
    return nbArenaMismatches;
  }

  explicit ArenaAllocator(int arena = 0) noexcept : _arena(arena) {}

  void *allocate(size_t n) {
    char *block = static_cast<char *>(malloc(n + kHeaderSize));
    if (!block) {
      throw std::bad_alloc();
    }
    std::memcpy(block, &_arena, sizeof(_arena));
    ++NbLiveAllocations();
    return block + kHeaderSize;
  }

  void *reallocate(void *p, size_t, size_t newSz) {
    if (!p) {
      return allocate(newSz);
    }
    char *block = checkArena(p);
    block = static_cast<char *>(realloc(block, newSz + kHeaderSize));
    if (!block) {
      throw std::bad_alloc();
    }
    return block + kHeaderSize;
  }

  void deallocate(void *p, size_t) {
    if (p) {
      free(checkArena(p));
      --NbLiveAllocations();
    }
  }

  bool operator==(const ArenaAllocator &o) const { return _arena == o._arena; }

 private:
  static constexpr size_t kHeaderSize = alignof(std::max_align_t);

  char *checkArena(void *p) const {
    char *block = static_cast<char *>(p) - kHeaderSize;
    int arena;
    std::memcpy(&arena, block, sizeof(arena));
    if (arena != _arena) {
      ++NbArenaMismatches();
    }
    return block;
  }

  int _arena;
};

/// Standard allocator of an arena, which does not propagate (default of std::allocator_traits).
template <class T>
using ArenaAllocatorOf = BasicAllocatorWrapper<T, ArenaAllocator>;

/// Standard allocator of an arena, propagating on container copy assignment, move assignment and swap.
template <class T>
class PropagatingArenaAllocator : public BasicAllocatorWrapper<T, ArenaAllocator> {
 public:
  using propagate_on_container_copy_assignment = std::true_type;
  using propagate_on_container_move_assignment = std::true_type;
  using propagate_on_container_swap = std::true_type;

  template <class U>
  struct rebind {
    using other = PropagatingArenaAllocator<U>;
  };

  PropagatingArenaAllocator() = default;

  // ::amc::ArenaAllocator, as MSVC would find the inaccessible name of the private base of BasicAllocatorWrapper
  explicit PropagatingArenaAllocator(const ::amc::ArenaAllocator &arenaAllocator)
      : BasicAllocatorWrapper<T, ::amc::ArenaAllocator>(arenaAllocator) {}

  template <class U>
  PropagatingArenaAllocator(const PropagatingArenaAllocator<U> &o)
      : BasicAllocatorWrapper<T, ::amc::ArenaAllocator>(o) {}
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
