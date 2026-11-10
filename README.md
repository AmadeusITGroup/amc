[![Ubuntu](https://github.com/AmadeusITGroup/amc/actions/workflows/ubuntu.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/ubuntu.yml)
[![Windows](https://github.com/AmadeusITGroup/amc/actions/workflows/windows.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/windows.yml)
[![MacOS](https://github.com/AmadeusITGroup/amc/actions/workflows/macos.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/macos.yml)
[![formatted](https://github.com/AmadeusITGroup/amc/actions/workflows/clang-format-check.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/clang-format-check.yml)
[![codecov](https://codecov.io/gh/AmadeusITGroup/amc/branch/main/graph/badge.svg)](https://codecov.io/gh/AmadeusITGroup/amc)
[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![GitHub Releases](https://img.shields.io/github/release/AmadeusITGroup/amc.svg)](https://github.com/AmadeusITGroup/amc/releases)

# amc - AMadeus (C++) Containers

Header-only C++ library of containers with the interface of `std::vector` and `std::set`, used instead of the standard ones in the Amadeus pricing and shopping engines. They are faster in two common cases:

- **Trivially relocatable elements**, which are most types (integers, pointers, structures of them, standard library types such as `std::unique_ptr` and `std::vector`, and the types which [declare it](docs/containers.md#trivially-relocatable-types), such as a structure holding a `std::unique_ptr`): the containers move them in bulk with `memcpy`, `memmove` or `realloc` when they grow, insert or erase, instead of calling a move constructor and a destructor per element.
- **Small sizes**: up to a compile-time capacity `N`, the elements are stored in the container object itself, without any allocation.

The library needs C++11 (C++17 for `SmallSet`), and is tested up to C++23 with GCC, Clang, MSVC, clang-cl and Apple Clang. From C++20, it is also a [module](#c20-module): `import amc;`.

## Containers

| Container                        | Header                        | Instead of                  | Storage                                                                           |
| -------------------------------- | ----------------------------- | --------------------------- | --------------------------------------------------------------------------------- |
| `amc::vector<T>`                 | `amc/vector.hpp`              | `std::vector`               | heap, grown with `realloc` for trivially relocatable types                        |
| `amc::SmallVector<T, N>`         | `amc/smallvector.hpp`         | `std::vector`               | inline up to at least `N` elements (all that fit in its size), then heap          |
| `amc::FixedCapacityVector<T, N>` | `amc/fixedcapacityvector.hpp` | `std::vector`               | inline, at most `N` elements: never allocates                                     |
| `amc::inplace_vector<T, N>`      | `amc/fixedcapacityvector.hpp` | C++26 `std::inplace_vector` | a `FixedCapacityVector` with the interface of `std::inplace_vector`               |
| `amc::FlatSet<T>`                | `amc/flatset.hpp`             | `std::set`                  | sorted vector                                                                     |
| `amc::SmallSet<T, N>` (C++17)    | `amc/smallset.hpp`            | `std::set`                  | unsorted inline vector up to `N` elements, then a `std::set` or an `amc::FlatSet` |

```cpp
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <type_traits>

#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/smallset.hpp>
#include <amc/smallvector.hpp>
#include <amc/vector.hpp>

struct Message {
  std::unique_ptr<char[]> payload;  // not trivially copyable...
  std::size_t size = 0;

  using trivially_relocatable = std::true_type;  // ...but movable with memcpy: declare it
};

amc::vector<Message> messages;                  // grows with realloc, shifts elements with memmove
amc::SmallVector<int, 8> path;                  // no allocation up to 8 elements
amc::FixedCapacityVector<int, 31> salesPerDay;  // never allocates, throws std::out_of_range beyond 31 elements
amc::FlatSet<std::uint32_t> ids;                // sorted vector: compact, fast lookups, linear insertions
amc::SmallSet<std::uint32_t, 16, std::less<std::uint32_t>, amc::allocator<std::uint32_t>,
              amc::FlatSet<std::uint32_t>>
    tags;  // unsorted inline up to 16 elements, then a FlatSet
```

Trivially copyable types are trivially relocatable without any declaration. See the [containers guide](docs/containers.md) for each container, and for [trivially relocatable types](docs/containers.md#trivially-relocatable-types).

## Performance

Some results of the [benchmarks](docs/benchmarks.md), with GCC 13 and Clang 23 (libstdc++ 13), trade-offs included:

| Benchmark                                                                                           | Compared to                      | amc                 |
| --------------------------------------------------------------------------------------------------- | -------------------------------- | ------------------- |
| Grow an `amc::vector<uint32_t>` to 1 million elements with `emplace_back`                           | `std::vector`                    | 2.2× faster         |
| Insert copies at random positions of an `amc::vector` of a trivially relocatable type owning memory | `std::vector`                    | 10 to 11× faster    |
| Fill and read `SmallVector<uint32_t, 16>`, 90 % of them with at most 16 elements                    | `std::vector`                    | 2.7 to 3.3× faster  |
| Chained lookups in an `amc::FlatSet<uint32_t>` of 1 million elements                                | `std::set`, `std::unordered_set` | 14× and 7.7× faster |
| Fill `SmallSet<uint32_t, 16>` with a `FlatSet` as set type, all of them small                       | `std::set`                       | 6.3 to 8.1× faster  |
| Insert at the end and erase at the front of an `amc::vector` of a type owning memory                | `std::vector`                    | 1.2 to 1.4× slower  |
| Insert 200 random values in a `FlatSet` of a non trivially relocatable type                         | `std::set`                       | 4.1 to 4.2× slower  |
| Erase all the elements of a `FlatSet<uint32_t>` of 100 000 elements                                 | `std::set`                       | 3.8 to 4.1× slower  |

## Drop-in compatibility

The containers have the interface of C++17 `std::vector` and `std::set`, with the C++20 and C++23 additions (`<=>`, `erase_if`, `contains`, `insert_range`...): in most code, switching from `std::vector` to `amc::vector`, or from `std::set` to `amc::FlatSet`, is a change of type. They handle the corner cases of the standard (arguments aliasing the container, single pass input iterators, allocator propagation...). Their differences are mostly the price of their optimizations:

| Area                 | Difference from the standard containers                                                                                                                                                                                                                                        | Containers                                                     |
| -------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | -------------------------------------------------------------- |
| `size_type`          | `std::uint32_t` by default (the smallest type holding `N` for `FixedCapacityVector`): `max_size()` is 2^32 - 1, and larger counts wrap around when converted at the call. It is a template parameter.                                                                          | vectors, `FlatSet`                                             |
| Allocator            | `amc::allocator` (`malloc`, `realloc`, `free`) by default. The `construct` and `destroy` members of allocators are not called: `std::pmr` and scoped allocators do not propagate to the elements.                                                                              | `amc::vector`, `SmallVector`, `FlatSet`, `SmallSet`            |
| Iterators            | raw pointers                                                                                                                                                                                                                                                                   | vectors, `FlatSet`                                             |
| Relocation           | trivially relocatable elements are moved with `memcpy`: their move constructors and destructors are not called                                                                                                                                                                 | vectors, `FlatSet`, `SmallSet`                                 |
| Inline storage       | move and `swap` move the inline elements: O(N), iterators do not follow them, `noexcept` only if moving `T` is                                                                                                                                                                 | `SmallVector`, `FixedCapacityVector`, `SmallSet` (while small) |
| Sorted vector        | insertions and erasures are O(n) and invalidate iterators, the elements must be movable                                                                                                                                                                                        | `FlatSet`                                                      |
| Unsorted small state | iteration in insertion order, no `lower_bound`, `upper_bound` nor `equal_range`, `end()` invalidated by insertions and erasures                                                                                                                                                | `SmallSet`                                                     |
| Exceptions           | growth moves the elements even if their move constructor may throw (`std::vector` copies them if it can): basic guarantee only. Exceeding `max_size()` throws `std::overflow_error` (`std::out_of_range` by default for `FixedCapacityVector`) instead of `std::length_error`. | vectors                                                        |
| Inheritance          | deriving from the containers is undefined behavior                                                                                                                                                                                                                                                                                              |
| Not provided         | `constexpr`, class template argument deduction for the vectors, the packed `vector<bool>`, `std::from_range` constructors, heterogeneous `erase` and `extract` (C++23). Call `erase(c, x)` and `erase_if(c, pred)` unqualified, not `std::erase`.                              | all                                                            |

The [compatibility page](docs/compatibility.md) lists, container by container, what behaves like the standard and every known difference.

## Integration

amc is a CMake interface library: link the `amc::amc` target, or add its `include` directory to the include path of any build system.

With [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) or `add_subdirectory` (CMake 3.21 or later):

```cmake
include(FetchContent)
FetchContent_Declare(
  amc
  GIT_REPOSITORY https://github.com/AmadeusITGroup/amc.git
  GIT_TAG        main  # or a release tag, for reproducible builds
)
FetchContent_MakeAvailable(amc)

target_link_libraries(my_target PRIVATE amc::amc)
```

Or once installed (`cmake --install <build directory>`, which installs the headers and a CMake package):

```cmake
find_package(amc CONFIG REQUIRED)
target_link_libraries(my_target PRIVATE amc::amc)
```

A few non-standard members (`append`, `pop_back_val`, `swap2`, the vector stealing of `FlatSet`...) are only declared if `AMC_NONSTD_FEATURES` is defined. Define it in all the translation units of the program, as it changes the class definitions: `target_compile_definitions(my_target PUBLIC AMC_NONSTD_FEATURES)`. See the [list of extensions](docs/containers.md#non-standard-extensions).

### C++20 module

`import amc;` provides all the containers without parsing their headers. On a translation unit using 6 vector types, it compiles 19 to 25 % faster than with `#include <amc/vector.hpp>` at `-O0`, and 5 to 17 % at `-O2` (Clang 23 and libstdc++ 13, in C++20 and C++23).

With CMake 3.28 or later, the `AMC_ENABLE_MODULE` option adds the `amc::module` target, which builds the module interface unit `include/amc/amc.cppm` in the build of its consumers:

```cmake
cmake_minimum_required(VERSION 3.28)  # scans the C++20 sources for imports

set(AMC_ENABLE_MODULE ON)  # before FetchContent_MakeAvailable(amc), or -DAMC_ENABLE_MODULE=ON when installing amc
# ...
target_link_libraries(my_target PRIVATE amc::module)
```

```cpp
#include <cstdint>  // the module does not export the standard names it uses

import amc;

amc::SmallVector<std::uint32_t, 8> path;
```

It needs a generator and a compiler supporting C++20 modules in CMake: Ninja or Visual Studio, and Clang with `clang-scan-deps`, GCC 15 or later (not with `-fsanitize=undefined`, which makes GCC crash building any module), or MSVC 2022. The module exports the public names of the headers, which declare the same entities: a program can import it in some translation units and include the headers in others, or do both in the same one (headers first). Like with the headers, `AMC_NONSTD_FEATURES` must be defined in the whole program or nowhere, and compilers do not diagnose a mismatch: `-DAMC_MODULE_NONSTD_FEATURES=ON` builds the module with the extensions, and defines `AMC_NONSTD_FEATURES` for the targets linking `amc::module`. The same goes for the macros of the [trivially relocatable standard types](docs/containers.md#trivially-relocatable-types), which their CMake options define for the targets linking `amc::amc`, `amc::module` included. The module and its importers must also be built with exceptions enabled or disabled alike. Modules do not export macros: include `<amc/config.hpp>` for the `AMC_*` ones.

## Building the tests and benchmarks

The unit tests ([GoogleTest](https://github.com/google/googletest)) and the benchmarks ([Google Benchmark](https://github.com/google/benchmark)) are built when amc is the main project (CMake 3.21 or later), in C++20 unless `CMAKE_CXX_STANDARD` says otherwise. CMake fetches them if they are not installed.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

| CMake option                           | Description                                                                                                                                                                                      | Default                              |
| -------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ | ------------------------------------ |
| `AMC_ENABLE_TESTS`                     | Build the unit tests                                                                                                                                                                             | `ON` for the main project            |
| `AMC_ENABLE_BENCHMARKS`                | Build the benchmarks                                                                                                                                                                             | `ON` for the main project, not Debug |
| `AMC_ENABLE_ASAN`                      | Build with the address and undefined behavior sanitizers (GCC and Clang)                                                                                                                         | `OFF`                                |
| `AMC_ENABLE_COVERAGE`                  | Instrument the unit tests for code coverage, and add a `coverage` target (Clang)                                                                                                                 | `OFF`                                |
| `AMC_WARNINGS_AS_ERRORS`               | Treat warnings as errors in Debug builds of the unit tests and benchmarks                                                                                                                        | `OFF`                                |
| `AMC_PEDANTIC`                         | Build the unit tests and benchmarks without the [non-standard extensions](docs/containers.md#non-standard-extensions)                                                                            | `OFF`                                |
| `AMC_ENABLE_MODULE`                    | Add the `amc::module` target, providing the [C++20 module](#c20-module) `amc` (CMake 3.28 or later)                                                                                              | `OFF`                                |
| `AMC_MODULE_NONSTD_FEATURES`           | Build the module with the non-standard extensions, and define `AMC_NONSTD_FEATURES` for the targets linking `amc::module`                                                                        | `OFF`                                |
| `AMC_STD_TRIVIALLY_RELOCATABLE`        | Declare the [standard library types](docs/containers.md#trivially-relocatable-types) trivially relocatable (`OFF` defines `AMC_NO_STD_TRIVIALLY_RELOCATABLE` for the targets linking `amc::amc`) | `ON`                                 |
| `AMC_STD_STRING_TRIVIALLY_RELOCATABLE` | Declare [`std::string`](docs/containers.md#trivially-relocatable-types) trivially relocatable where it is: libc++, MSVC STL in Release (defines the macro for the targets linking `amc::amc`)    | `OFF`                                |

Code coverage uses [LLVM source-based code coverage](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html), so it needs Clang, and `llvm-profdata` and `llvm-cov` of the same version (package `llvm-<version>` on Debian and Ubuntu):

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DAMC_ENABLE_COVERAGE=ON
cmake --build build --target coverage
```

It runs the unit tests and prints the coverage of each header. The report, with line and branch counts, is written to `build/coverage/html/index.html`, and an lcov export to `build/coverage/amc.lcov`.

The CI builds and tests amc with GCC 8 to 13, Clang 17, 19, 20 and 21 (Linux), MSVC 2022 and clang-cl (Windows, 32 and 64-bit) and Apple Clang (macOS), in C++17, C++20 and C++23, with and without the sanitizers and the non-standard extensions. It tests the C++20 module with Clang 19 and 21, GCC 15 and MSVC 2022.

## Documentation

- [Containers guide](docs/containers.md): choosing a container, trivially relocatable types, details of each container, non-standard extensions.
- [Compatibility](docs/compatibility.md): similarities and differences with `std::vector`, `std::inplace_vector` and `std::set`.
- [Benchmarks](docs/benchmarks.md): charts, analysis and numbers.

## License

[MIT](LICENSE)
