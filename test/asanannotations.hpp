#pragma once

/// Checks of the AddressSanitizer annotations of the containers (see AMC_ASAN_ANNOTATIONS), and the reads of their
/// death tests, shared by the tests of the vectors and of the hash tables. Without the annotations, the checks hold.

#include <amc/config.hpp>
#include <amc/fixedcapacityvector.hpp>
#include <amc/smallvector.hpp>
#include <amc/vector.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#if __has_include(<memory_resource>)
#include <memory_resource>
#endif

namespace amc {
namespace annotations_test {

/// Whether the objects of type T may hold poisoned bytes: those of the inline storage of an amc container they hold.
/// The checks of the containers do not look for poisoned bytes in such elements. Specialized for the hash tables in
/// hashtablemodels.hpp.
template <class T>
struct HoldsAnnotations : std::false_type {};

template <class T, class Alloc, class SizeType, class GrowingPolicy, SizeType N>
struct HoldsAnnotations<Vector<T, Alloc, SizeType, GrowingPolicy, N>> : std::bool_constant<N != 0> {};

template <class T>
struct HoldsAnnotations<const T> : HoldsAnnotations<T> {};

template <class T1, class T2>
struct HoldsAnnotations<std::pair<T1, T2>> : std::disjunction<HoldsAnnotations<T1>, HoldsAnnotations<T2>> {};

template <class... T>
struct HoldsAnnotations<std::tuple<T...>> : std::disjunction<HoldsAnnotations<T>...> {};

template <class T, std::size_t N>
struct HoldsAnnotations<std::array<T, N>> : HoldsAnnotations<T> {};

/// Whether none of the 'n' bytes at 'p' is poisoned, printing the first one otherwise ('what' describes them).
inline bool NoBytePoisoned([[maybe_unused]] const void* p, [[maybe_unused]] std::size_t n,
                           [[maybe_unused]] const char* what) {
#ifdef AMC_ASAN_ANNOTATIONS
  const void* poisoned = n == 0 ? nullptr : __asan_region_is_poisoned(const_cast<void*>(p), n);
  if (poisoned != nullptr) {
    std::fprintf(stderr, "%s: byte %zu of %zu poisoned\n", what,
                 static_cast<std::size_t>(static_cast<const char*>(poisoned) - static_cast<const char*>(p)), n);
    return false;
  }
#endif
  return true;
}

/// Whether the bytes from 'first' to 'last' are poisoned, as far as the shadow memory of AddressSanitizer can tell it:
/// it describes each granule of 8 bytes by the number of its first bytes which may be accessed, so that only the bytes
/// whose granule ends in the range are checked (the poisoned bytes of a granule being its last ones, the first one
/// tells for the next ones). Prints the first accessible byte ('what' describes the range).
inline bool BytesPoisoned([[maybe_unused]] const void* first, [[maybe_unused]] const void* last,
                          [[maybe_unused]] const char* what) {
#ifdef AMC_ASAN_ANNOTATIONS
  const std::uintptr_t lo = reinterpret_cast<std::uintptr_t>(first);
  const std::uintptr_t hi = reinterpret_cast<std::uintptr_t>(last) & ~std::uintptr_t{7};
  for (std::uintptr_t p = lo; p < hi; p = (p & ~std::uintptr_t{7}) + 8U) {
    if (__asan_address_is_poisoned(reinterpret_cast<const void*>(p)) == 0) {
      std::fprintf(stderr, "%s: byte %zu of %zu accessible\n", what, static_cast<std::size_t>(p - lo),
                   static_cast<std::size_t>(reinterpret_cast<std::uintptr_t>(last) - lo));
      return false;
    }
  }
#endif
  return true;
}

/// Whether the AddressSanitizer annotations of the vector 'v' are the ones of its state (see PoisonSlots): its elements
/// accessible (unless they may hold poisoned bytes themselves), its slots without element poisoned, the inline storage
/// of a large SmallVector poisoned after the pointer to its dynamic storage, and its members accessible.
template <class V>
bool VectorAnnotationsMatch([[maybe_unused]] const V& v) {
#ifdef AMC_ASAN_ANNOTATIONS
  using T = typename V::value_type;
  const char* object = reinterpret_cast<const char*>(std::addressof(v));
  const char* data = reinterpret_cast<const char*>(v.data());
  const std::size_t size = static_cast<std::size_t>(v.size()) * sizeof(T);
  const std::size_t capacity = static_cast<std::size_t>(v.capacity()) * sizeof(T);
  if (!HoldsAnnotations<T>::value && !NoBytePoisoned(data, size, "elements")) {
    return false;
  }
  if (!BytesPoisoned(data + size, data + capacity, "slots without element")) {
    return false;
  }
  if constexpr (V::kInlineCapacity == 0) {
    return NoBytePoisoned(object, sizeof(V), "members");
  } else {
    // offset of the inline storage: the one of the elements of an empty vector
    const V empty(v.get_allocator());
    const std::size_t offset = static_cast<std::size_t>(reinterpret_cast<const char*>(empty.data()) -
                                                        reinterpret_cast<const char*>(std::addressof(empty)));
    const char* inlineFirst = object + offset;
    if (!NoBytePoisoned(object, offset, "members")) {
      return false;
    }
    if (data != inlineFirst) {
      // large SmallVector
      const char* inlineLast = inlineFirst + static_cast<std::size_t>(V::kInlineCapacity) * sizeof(T);
      const char* afterPointer = inlineFirst + sizeof(T*);
      if (!NoBytePoisoned(inlineFirst, sizeof(T*), "pointer to the dynamic storage")) {
        return false;
      }
      if (afterPointer < inlineLast && !BytesPoisoned(afterPointer, inlineLast, "unused inline storage")) {
        return false;
      }
    }
  }
#endif
  return true;
}

/// Whether the vector 'v' and each of its elements, vectors too, have the annotations of their state.
template <class V>
bool NestedVectorAnnotationsMatch(const V& v) {
  if (!VectorAnnotationsMatch(v)) {
    return false;
  }
  for (const auto& elem : v) {
    if (!VectorAnnotationsMatch(elem)) {
      return false;
    }
  }
  return true;
}

/// Whether the byte at 'p' is poisoned (false without the annotations).
inline bool Poisoned([[maybe_unused]] const void* p) {
#ifdef AMC_ASAN_ANNOTATIONS
  return __asan_address_is_poisoned(p) != 0;
#else
  return false;
#endif
}

/// Reads the bytes of 'v', like an access through a pointer, a reference or an iterator kept to an element: reported by
/// AddressSanitizer if one of them is poisoned. The address goes through a volatile variable: with optimizations,
/// Clang does not check the accesses that its stack safety analysis proves within the bounds of a local variable,
/// poisoned bytes or not, which a stale access to the inline storage of a local container is once inlined.
template <class T>
void ReadBytes(const T& v) {
  const volatile unsigned char* volatile address = reinterpret_cast<const volatile unsigned char*>(std::addressof(v));
  const volatile unsigned char* const bytes = address;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    static_cast<void>(bytes[i]);
  }
}

#ifdef __cpp_lib_memory_resource
/// Memory resource giving back a block of the same size and alignment deallocated before, after writing all its bytes:
/// AddressSanitizer reports a block given back with poisoned bytes. A container which leaves poisoned bytes in the
/// storage it deallocates is only reported with such an allocator, the memory freed by 'operator delete' being
/// poisoned anyway.
class RecyclingResource : public std::pmr::memory_resource {
 public:
  RecyclingResource() = default;

  RecyclingResource(const RecyclingResource&) = delete;
  RecyclingResource(RecyclingResource&&) noexcept = delete;
  RecyclingResource& operator=(const RecyclingResource&) = delete;
  RecyclingResource& operator=(RecyclingResource&&) noexcept = delete;

  ~RecyclingResource() override {
    for (const Block& b : _blocks) {
      std::pmr::new_delete_resource()->deallocate(b.p, b.bytes, b.alignment);
    }
  }

  int nbReused() const noexcept { return _nbReused; }

 private:
  struct Block {
    void* p;
    std::size_t bytes;
    std::size_t alignment;
  };

  void* do_allocate(std::size_t bytes, std::size_t alignment) override {
    for (auto it = _blocks.begin(); it != _blocks.end(); ++it) {
      if (it->bytes == bytes && it->alignment == alignment) {
        unsigned char* p = static_cast<unsigned char*>(it->p);
        _blocks.erase(it);
        for (std::size_t i = 0; i < bytes; ++i) {
          p[i] = 0xAB;
        }
        ++_nbReused;
        return p;
      }
    }
    return std::pmr::new_delete_resource()->allocate(bytes, alignment);
  }

  void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
    _blocks.push_back(Block{p, bytes, alignment});
  }

  bool do_is_equal(const std::pmr::memory_resource& o) const noexcept override { return this == &o; }

  std::vector<Block> _blocks;
  int _nbReused = 0;
};
#endif

}  // namespace annotations_test
}  // namespace amc
