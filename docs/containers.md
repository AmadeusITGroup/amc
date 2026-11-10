# Containers guide

- [Choosing a container](#choosing-a-container)
- [Trivially relocatable types](#trivially-relocatable-types)
- [amc::vector](#amcvector)
- [SmallVector](#smallvector)
- [FixedCapacityVector and inplace_vector](#fixedcapacityvector-and-inplace_vector)
- [Size type of the vectors](#size-type-of-the-vectors)
- [FlatSet](#flatset)
- [SmallSet](#smallset)
- [Node handles](#node-handles)
- [Non-standard extensions](#non-standard-extensions)

Each container has its own header in `include/amc/`, and from C++20 the module `amc` provides all of them (`import amc;`, see the [README](../README.md#c20-module)). All the vectors share a single implementation, most of which does not depend on `N`, which limits the code generated for each inline capacity. The containers are not designed to be base classes: deriving from them is undefined behavior. They also compile with exceptions disabled (`-fno-exceptions`): the errors which would throw then abort the program, like with libstdc++ and libc++. See [compatibility.md](compatibility.md) for their differences with the standard containers, and [benchmarks.md](benchmarks.md) for their performance.

## Choosing a container

| Need                                                                                   | Container                                                                                       |
| -------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- |
| A `std::vector`, faster for trivially relocatable elements                             | `amc::vector<T>`                                                                                |
| Many vectors, most of them with at most `N` elements                                   | `amc::SmallVector<T, N>`, with `N` covering most of them (90 % for instance)                    |
| A bound known at compile time, never any allocation                                    | `amc::FixedCapacityVector<T, N>`, or `amc::inplace_vector<T, N>` for the C++26 interface        |
| A set mostly read (lookups, iteration), of elements cheap to move                      | `amc::FlatSet<T>`                                                                               |
| Many sets, most of them with at most `N` elements cheap to compare                     | `amc::SmallSet<T, N>`, with an `amc::FlatSet<T>` as set type for trivially relocatable elements |
| Large sets of strings, many insertions and erasures of non trivially relocatable types | `std::set` or `std::unordered_set`                                                              |

## Trivially relocatable types

A type is trivially relocatable if moving an object to another address and destroying the source is equivalent to copying its bytes with `memcpy` (and forgetting the source). The amc containers then move their elements in bulk, with `memcpy`, `memmove` or `realloc`, instead of calling a move constructor and a destructor per element: when they grow, insert, erase, swap or merge. The trait is not (yet) in the standard (see [P1144](https://quuxplusone.github.io/blog/2018/07/18/announcing-trivially-relocatable/)), but no special compiler is needed.

Most types are trivially relocatable, including types owning memory through a pointer, such as `std::unique_ptr` or `std::vector` in the major implementations, although amc only knows it for the trivially copyable ones (see below). Types holding pointers to parts of themselves are not, as moving them must update these pointers: `std::list`, `std::set` and `std::map` in some implementations, `std::string` in libstdc++ (which points to its inline buffer). More information [here](https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable/).

`amc::is_trivially_relocatable<T>` (`<amc/type_traits.hpp>`) conservatively defaults to `std::is_trivially_copyable<T>`: trivially copyable types need nothing. Other types opt in, either by declaring a public member type:

```cpp
#include <cstddef>
#include <memory>
#include <type_traits>

#include <amc/vector.hpp>

struct Message {
  std::unique_ptr<char[]> payload;  // not trivially copyable...
  std::size_t size = 0;

  using trivially_relocatable = std::true_type;  // ...but movable with memcpy
};

static_assert(amc::is_trivially_relocatable<Message>::value, "");
```

or, for a type which cannot be modified, by specializing the trait next to its definition:

```cpp
namespace amc {
template <>
struct is_trivially_relocatable<thirdparty::Buffer> : std::true_type {};
}  // namespace amc
```

Declaring a type trivially relocatable when it is not is undefined behavior.

The trait is also defined for `std::pair` of trivially relocatable types, for `std::allocator`, and for the amc containers, so that containers of containers are relocated with `memcpy` too: `amc::vector` is trivially relocatable if its allocator is (`amc::allocator` and `std::allocator` are), `SmallVector` if `T` and its allocator are, `FixedCapacityVector` if `T` is, `FlatSet` if its comparator and vector are, `SmallSet` if its inline vector and set type are. A custom allocator which is not trivially copyable (with a user-provided copy constructor, for instance) has to be marked like any other type. `amc::BasicAllocatorWrapper` is trivially relocatable if its basic allocator is.

## amc::vector

`amc::vector<T, Alloc = amc::allocator<T>, SizeType = std::uint32_t>` (`<amc/vector.hpp>`) is a `std::vector` optimized for trivially relocatable types, and 16 bytes instead of 24 on 64-bit platforms (its size and capacity are of type `SizeType`, see [Size type of the vectors](#size-type-of-the-vectors)). For other types, it uses the same algorithms as `std::vector`.

Growth: the capacity doubles while the storage takes less than 4 KiB, then grows by a factor of 1.5. For trivially relocatable types, the storage grows with the `reallocate` method of the allocator if it has one, which `amc::allocator` implements with `realloc`: large blocks are often extended in place, without copying the elements. `reserve` allocates exactly the requested capacity.

Allocators:

- `amc::allocator<T>` is `amc::BasicAllocatorWrapper<T, amc::SimpleAllocator>`, which allocates with `malloc`, `realloc` and `free`. It supports over-aligned types (without `realloc`).
- `amc::BasicAllocatorWrapper<T, B>` turns a basic allocator `B`, providing `void* allocate(std::size_t)`, `void* reallocate(void* p, std::size_t oldSz, std::size_t newSz)` and `void deallocate(void* p, std::size_t)`, into a standard allocator. A basic allocator with a state must provide an `operator==` telling whether the memory allocated by one can be deallocated by the other.
- Any standard allocator can be used, `std::allocator<T>` included. To grow trivially relocatable vectors in place, an allocator provides `T* reallocate(T* p, size_type oldCapacity, size_type newCapacity, size_type nConstructedElems)`.

## SmallVector

`amc::SmallVector<T, N, Alloc = amc::allocator<T>, SizeType = std::uint32_t>` (`<amc/smallvector.hpp>`) stores up to `N` elements in the object itself, without allocating memory. Beyond `N`, it moves them to the heap and behaves like an `amc::vector`. Once on the heap, it stays there with its capacity, even when its size goes back under `N`, until `shrink_to_fit` is called.

Choose `N` so that most of the vectors (90 % of them, for instance) never exceed it. The pointer to the heap storage shares its bytes with the first inline elements: a `SmallVector<char, 8>` takes 16 bytes, like an `amc::vector`.

```cpp
#include <amc/smallvector.hpp>

using Path = amc::SmallVector<int, 8>;  // no allocation up to 8 elements
```

A `SmallVector` can be constructed from an rvalue `amc::vector` of the same allocator and size type: it takes its storage if its capacity is larger than `N`, otherwise it moves its elements inline.

Unlike for `std::vector`, moving or swapping a `SmallVector` in its small state moves its elements, so iterators do not follow them (see [compatibility.md](compatibility.md#differences-with-stdvector)).

## FixedCapacityVector and inplace_vector

`amc::FixedCapacityVector<T, N, GrowingPolicy = amc::vec::ExceptionGrowingPolicy, SizeType>` (`<amc/fixedcapacityvector.hpp>`) stores its elements in the object itself and never allocates: its capacity is `N`. Use it when the application has a compile-time bound on the size.

```cpp
#include <amc/fixedcapacityvector.hpp>

using SoldUnitsPerDayInMonth = amc::FixedCapacityVector<int, 31>;
```

`GrowingPolicy` tells what happens when the vector would exceed `N` elements:

| Growing policy                     | Behavior                                                                    |
| ---------------------------------- | --------------------------------------------------------------------------- |
| `amc::vec::ExceptionGrowingPolicy` | throws `std::out_of_range` (default)                                        |
| `amc::vec::BadAllocGrowingPolicy`  | throws `std::bad_alloc`, like `std::inplace_vector`                         |
| `amc::vec::UncheckedGrowingPolicy` | `assert` only: aborts in debug builds, undefined behavior in release builds |

Compared to a `SmallVector` which would never grow, a `FixedCapacityVector` makes fewer checks and states the intent. Without reallocation, `begin()` is never invalidated, and neither are the iterators before an insertion or erasure. Its `size_type` is the smallest unsigned type holding `N` (`std::uint8_t` up to 255), which keeps it small, and it is trivially destructible if `T` is.

`amc::inplace_vector<T, N>` is a `FixedCapacityVector<T, N, amc::vec::BadAllocGrowingPolicy, std::size_t>`, with the interface of C++26 `std::inplace_vector`. Its `try_push_back`, `try_emplace_back` (which return `nullptr` when the vector is full), `unchecked_push_back`, `unchecked_emplace_back` and `try_append_range` (C++23) are available on any `FixedCapacityVector`.

## Size type of the vectors

The vectors take their `size_type` as a template parameter: `std::uint32_t` by default for `amc::vector` and `SmallVector`, and the smallest unsigned type able to hold `N` for `FixedCapacityVector`. A smaller `size_type` makes a smaller vector, but bounds its `max_size()`.

Like with `std::vector`, the counts given to the member functions (constructors, `assign`, `insert`, `resize`, `reserve`...) are of type `size_type`: a larger count is converted by the call, before the vector can check it. For instance, `amc::SmallVector<int, 4, amc::allocator<int>, std::uint8_t>(300)` has 44 elements, and `amc::FixedCapacityVector<int, 10>::reserve(260)` reserves 4 elements without throwing. GCC and Clang warn about these conversions by default for constant counts, and with `-Wconversion` for the other ones. With a signed `size_type`, a negative count is checked like a count exceeding `max_size()`: the vector throws (according to its `GrowingPolicy` for `FixedCapacityVector`), like `std::vector` does with the huge `size_t` a negative count converts to. Use `std::size_t` as `size_type` to remove the limit of the size type: `max_size()` is then the one of the allocator (`SIZE_MAX / sizeof(T)` for `amc::allocator`, while `std::vector` also caps it at `PTRDIFF_MAX / sizeof(T)`).

## FlatSet

`amc::FlatSet<T, Compare = std::less<T>, Alloc = amc::allocator<T>, VecType = amc::vector<T, Alloc>>` (`<amc/flatset.hpp>`), also called a sorted vector, keeps its elements sorted in a vector. It is cache friendly and compact (4 bytes per `uint32_t` element, against 40 to 50 for `std::set`), with fast lookups and iteration. Insertions and erasures shift the next elements, which is cheap for trivially relocatable types (a single `memmove`), but linear in the size of the set. Use it for sets which are mostly read.

- Lookups (`find`, `lower_bound`, `insert`...) of scalar types (arithmetic, enumerations, pointers) ordered by `std::less` or `std::greater` use a branchless binary search, which prefetches its next steps in large sets.
- The construction from a range, the construction from and the assignment of a vector (see [Non-standard extensions](#non-standard-extensions)), and the insertion of a range only sort the new elements if they are not already sorted: the check is linear, and stops at the first pair out of order. Constructing a `FlatSet<uint32_t>` from 1 000 000 sorted values is about 10 times faster than sorting them. If the new elements (at least 32) start with a sorted prefix of at least two thirds of them, only the other ones are sorted, then merged with the prefix: from 1 000 to 1 000 000 `uint32_t` values whose first 90 % are sorted, the construction is 4 to 7 times faster.
- The insertion of a range merges the new elements only with the present elements which are not less than the smallest new one: inserting `k` elements greater than all the `n` present ones (like increasing keys) costs `O(log(n) + k log(k))`, and `O(log(n) + k)` if they are sorted, independently of the size of the set.
- `merge` of trivially relocatable types stored in amc vectors is linear: the elements are relocated from the ends of both sets to the storage of the destination set, instead of being inserted one by one.

The vector type can be any amc vector, or `std::vector`. `Alloc` must be its allocator type, which is an empty tag for a `FixedCapacityVector`:

```cpp
#include <functional>

#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>

using CapitalLetters = amc::FixedCapacityVector<char, 26>;
using CapitalLettersSet = amc::FlatSet<char, std::less<char>, CapitalLetters::allocator_type, CapitalLetters>;
```

Like with `std::set`, the comparator may be a function pointer or a `final` class. Empty comparators which are not `final` take no space.

## SmallSet

`amc::SmallSet<T, N, Compare = std::less<T>, Alloc = amc::allocator<T>, SetType = std::set<T, Compare, Alloc>>` (`<amc/smallset.hpp>`, C++17) has two states:

- **Small**: up to `N` elements (`N <= 64`), stored unsorted in an inline vector, without allocation. Lookups and insertions are linear searches, followed by a `push_back` for an insertion.
- **Large**: beyond `N` elements, they are moved to a `SetType`, until the set is emptied. `SetType` is `std::set` by default, and may be any set with the same value, comparator and allocator types, such as an `amc::FlatSet`. With a `FlatSet` over an amc vector, the iterators of the `SmallSet` are pointers.

```cpp
#include <cstdint>
#include <functional>
#include <string>

#include <amc/flatset.hpp>
#include <amc/smallset.hpp>

using VisitedCountries = amc::SmallSet<std::string, 8>;
using SeenIds = amc::SmallSet<std::uint32_t, 16, std::less<std::uint32_t>, amc::allocator<std::uint32_t>,
                              amc::FlatSet<std::uint32_t>>;
```

From C++20, when the comparator calls `operator<` or `operator>` (`std::less`, `std::greater`, transparent or not, or their `std::ranges` versions) and the looked up key is three-way comparable with the elements without declaring its own `operator<` or `operator>` (which `operator<=>` might not agree with), the linear search of the small state compares it once with each element, with `operator<=>`, instead of twice with the comparator: lookups among strings are about twice as fast.

A `SmallSet` with an `amc::FlatSet` is the fastest set for integers, enumerations, pointers or small trivially relocatable types, when most sets have at most `N` elements. For strings, the default `SmallSet` (with a `std::set`) pays off when most sets are small (see [benchmarks.md](benchmarks.md#smallset)).

The small state is not sorted: iteration follows the insertion order, `lower_bound`, `upper_bound` and `equal_range` are not provided, and iterators are invalidated like the ones of a vector (see [compatibility.md](compatibility.md#smallset-iterator-invalidation)).

## Node handles

Like the node handles of `std::set`, the `node_type` of `FlatSet` and `SmallSet` only depends on the value and allocator types: `extract` and `insert` move elements between sets of different comparators and underlying containers, from a `FlatSet` to a `SmallSet` and back. A node holds its element by value, and is not compatible with the nodes of `std::set`. A `FlatSet` over a `FixedCapacityVector` has another allocator type (an empty tag), so its nodes are of another type.

## Non-standard extensions

These members are only declared if `AMC_NONSTD_FEATURES` is defined before including the amc headers. As it changes the definition of the classes, define it in all the translation units of a program, for instance with `target_compile_definitions(my_target PUBLIC AMC_NONSTD_FEATURES)`. The `AMC_PEDANTIC` CMake option only applies to the unit tests and benchmarks of amc: they are built with the extensions, unless it is `ON`. The C++20 module is built with the extensions if the CMake option `AMC_MODULE_NONSTD_FEATURES` is `ON`, which also defines `AMC_NONSTD_FEATURES` for the targets linking `amc::module`.

All vectors:

| Member                                                                   | Description                                                                                                           |
| ------------------------------------------------------------------------ | --------------------------------------------------------------------------------------------------------------------- |
| `T pop_back_val()`                                                       | `pop_back`, returning the popped element                                                                              |
| `append(first, last)`, `append(count)`, `append(count, v)`, `append(il)` | same as `insert(end(), ...)`                                                                                          |
| `swap2(other)`                                                           | swaps with any amc vector of the same `T` (other flavor, `N`, allocator or size type). May throw, as it may grow them |

`FlatSet`:

| Member                                                                 | Description                                                                          |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------------------------ |
| `vector_type`                                                          | `VecType`                                                                            |
| `explicit FlatSet(vector_type&& v, comp = Compare(), alloc = Alloc())` | takes the storage of `v`, sorts it (unless it already is) and removes the duplicates |
| `operator=(vector_type&& v)`                                           | same, for an assignment                                                              |
| `vector_type steal_vector()`                                           | moves the sorted vector out, leaving the set empty                                   |
| `data()`, `operator[](i)`, `at(i)`                                     | `const` access by position (`at` throws `std::out_of_range`)                         |
| `capacity()`, `reserve(n)`, `shrink_to_fit()`                          | the ones of the vector                                                               |

Always available, as they do not conflict with the standard interface: the construction of a `SmallVector` from an `amc::vector` (see [SmallVector](#smallvector)), `front()` and `back()` of `FlatSet`, the `std::inplace_vector` members of `FixedCapacityVector`, and the `trivially_relocatable` member type of all containers.
