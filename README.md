[![Ubuntu](https://github.com/AmadeusITGroup/amc/actions/workflows/ubuntu.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/ubuntu.yml)
[![Windows](https://github.com/AmadeusITGroup/amc/actions/workflows/windows.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/windows.yml)
[![MacOS](https://github.com/AmadeusITGroup/amc/actions/workflows/macos.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/macos.yml)

[![formatted](https://github.com/AmadeusITGroup/amc/actions/workflows/clang-format-check.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/clang-format-check.yml)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/AmadeusITGroup/amc/master/LICENSE)
[![GitHub Releases](https://img.shields.io/github/release/AmadeusITGroup/amc.svg)](https://github.com/AmadeusITGroup/amc/releases)

# AMadeus (C++) Containers

<details><summary>Sections</summary>
<p>

- [AMadeus (C++) Containers](#amadeus-c-containers)
  - [Brief](#brief)
  - [Benefits](#benefits)
    - [Performance optimizations](#performance-optimizations)
      - [Vectors](#vectors)
      - [Sets](#sets)
    - [Other benefits](#other-benefits)
  - [Contents](#contents)
  - [What is a trivially relocatable type?](#what-is-a-trivially-relocatable-type)
  - [Build](#build)
    - [Tested environments](#tested-environments)

</p>
</details>

## Brief

Collection of C++ containers, drop-in replacements for `std::vector` and `std::set` providing additional optimizations.

## Benefits

### Performance optimizations

 - For types taking an integral N as template parameter, container does not allocate dynamic memory as long as its capacity does not exceed N
 - Vectors (and `FlatSet`, as it uses a `vector`) are all optimized for **trivially relocatable** types (definition below).

Example of possible performance gains (directly extracted from the provided benchmarks, compiled with GCC 10.1 on Ubuntu 18:)

#### Vectors

![Alt text](./docs/vector_bench_reloctype.svg)

#### Sets

For sets, time axis is in logarithmic scale.

![Alt text](./docs/set_bench_reloctype.svg)
![Alt text](./docs/set_bench_int.svg)

### Other benefits

 - All 3 vector flavors share the same code / algorithms for vector operations.
 - Templated code generation is minimized thanks to the late location of the integral N template parameter
 - Optimized emulations of standard library features for older C++ compilers are provided when C++ version < C++17

## Contents

This library provides the following containers:

| Container Name      | STL equivalent | Brief                                                               | Why?                                                         |
| ------------------- | -------------- | ------------------------------------------------------------------- | ------------------------------------------------------------ |
| FixedCapacityVector | std::vector    | Vector-like which cannot grow, max capacity defined at compile time | No dynamic memory allocation                                 |
| SmallVector         | std::vector    | Vector-like optimized for small sizes                               | No dynamic memory allocation for small sizes                 |
| vector              | std::vector    | Vector optimized for trivially relocatable types                    | Optimized for trivially relocatable types                    |
| FlatSet             | std::set       | Set-like implemented as a sorted vector                             | Alternate structure for sets optimized for read-heavy usages |
| SmallSet (\*)       | std::set       | Set-like optimized for small sizes                                  | No dynamic memory allocation and unsorted for small sizes    |

 \*: C++17 compiler only (uses `std::variant` & `std::optional`)

## What is a trivially relocatable type?

It describes the ability of moving around memory a value of type T by using `memcpy` (as opposed to the conservative approach of calling the copy constructor and the destroying the old temporary). 
It is a type trait currently not (yet?) present in the standard, although is has been proposed (more information [here](https://quuxplusone.github.io/blog/2018/07/18/announcing-trivially-relocatable/)).
No need to use a modified compiler to benefit from trivially relocatibilty optimizations: you can use helper type traits provided by this library to mark explicitely types that you know **are** trivially relocatable. The conservative approach assumes that all trivially copyable types are trivially relocatable, so no need to mark them as such.
With trivially relocatable types, performance gains are easily measurable for all operations of the Vector like container involving *relocation* of elements (grow, insert in middle, etc).

Fortunately, most types are trivially relocatable. `amc::vector` itself is trivially relocatable (as well as `FixedCapacityVector` and `SmallVector` if T is). Types containing pointers to parts of themselves are typically not trivially relocatable, because moving them would require to update the internal pointers they hold to parts of themselves (`std::list`, `std::set`, `std::map` are for instance). `std::string` is not trivially relocatable in some implementations, but some open source equivalents are (for instance, [folly::fbstring](https://github.com/facebook/folly/blob/master/folly/FBString.h)). More information [here](https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable/).

The most convenient way to mark a type as trivially relocatable is to declare in the public part of the class:

`using trivially_relocatable = std::true_type;`

This is only necessary for non trivially copyable types, because trivially copyable types are trivially relocatable by default.

## Build

This library is header only library, with one file to be included per container.

Vectors and `FlatSet` containers require a C++11 compiler. 
`SmallSet` however, needs a C++17 compiler because it uses `std::variant` and `std::optional`, although `boost::variant` could be used as a workaround if a C++17 compiler is not available.

Unit tests and benchmarks are provided. They can be compiled with **cmake**. 

By default, both will be compiled only if 'amc' is instantiated as the main project. You can manually force the build of the tests and benchmarks thanks to following flags:
```
AMC_ENABLE_TESTS
AMC_ENABLE_BENCHMARKS
```

Bundled tests depend on [Google Test](https://github.com/google/googletest), benchmarks on [Google benchmarks](https://github.com/google/benchmark).

`cmake` will retrieve them automatically thanks to [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) feature.

To compile and launch the tests in `Debug` mode, simply launch

`mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make && ctest`

### Tested environments

This library has been tested on Ubuntu 18.04 and Windows 10 (Visual Studio 2019), cmake 3.19.4 and the following compilers:
 - GCC from version 5.5 to 10
 - Clang from version 6.0
 - MSVC 19.28
