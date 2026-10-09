[![Ubuntu](https://github.com/AmadeusITGroup/amc/actions/workflows/ubuntu.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/ubuntu.yml)
[![Windows](https://github.com/AmadeusITGroup/amc/actions/workflows/windows.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/windows.yml)
[![MacOS](https://github.com/AmadeusITGroup/amc/actions/workflows/macos.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/macos.yml)

[![formatted](https://github.com/AmadeusITGroup/amc/actions/workflows/clang-format-check.yml/badge.svg)](https://github.com/AmadeusITGroup/amc/actions/workflows/clang-format-check.yml)
[![codecov](https://codecov.io/gh/AmadeusITGroup/amc/branch/main/graph/badge.svg)](https://codecov.io/gh/AmadeusITGroup/amc)

[![GitHub license](https://img.shields.io/badge/license-MIT-blue.svg)](https://raw.githubusercontent.com/AmadeusITGroup/amc/master/LICENSE)
[![GitHub Releases](https://img.shields.io/github/release/AmadeusITGroup/amc.svg)](https://github.com/AmadeusITGroup/amc/releases)

# AMadeus (C++) Containers

<details><summary>Sections</summary>
<p>

- [AMadeus (C++) Containers](#amadeus-c-containers)
  - [Brief](#brief)
  - [Contents](#contents)
  - [Benefits](#benefits)
    - [Performance optimizations](#performance-optimizations)
      - [Vectors](#vectors)
      - [Sets](#sets)
    - [Other benefits](#other-benefits)
      - [For vector types](#for-vector-types)
      - [For FlatSet](#for-flatset)
  - [What is a trivially relocatable type?](#what-is-a-trivially-relocatable-type)
  - [Build with CMake](#build-with-cmake)
    - [Options](#options)
    - [As a main project](#as-a-main-project)
      - [Code coverage](#code-coverage)
    - [As a sub-project with cmake](#as-a-sub-project-with-cmake)
      - [With FetchContent](#with-fetchcontent)
      - [By installing amc](#by-installing-amc)
    - [Tested environments](#tested-environments)
  - [Usage examples](#usage-examples)
    - [Vectors](#vectors-1)
      - [amc::vector](#amcvector)
      - [SmallVector](#smallvector)
      - [FixedCapacityVector](#fixedcapacityvector)
    - [Sets](#sets-1)
      - [FlatSet](#flatset)
      - [SmallSet (c++17)](#smallset-c17)

</p>
</details>

## Brief

Collection of high performance C++ containers, drop-in replacements for `std::vector` and `std::set`, used in Amadeus pricing and shopping engines instead of standard ones.

## Contents

This header based library (to be more precise, `cmake` interface) provides the following containers:

| Container Name      | STL equivalent | Brief                                                               | Why?                                                         |
| ------------------- | -------------- | ------------------------------------------------------------------- | ------------------------------------------------------------ |
| FixedCapacityVector | std::vector    | Vector-like which cannot grow, max capacity defined at compile time | No dynamic memory allocation                                 |
| SmallVector         | std::vector    | Vector-like optimized for small sizes                               | No dynamic memory allocation for small sizes                 |
| vector              | std::vector    | Vector optimized for trivially relocatable types                    | Optimized for trivially relocatable types                    |
| FlatSet             | std::set       | Set-like implemented as a sorted vector                             | Alternate structure for sets optimized for read-heavy usages |
| SmallSet (\*)       | std::set       | Set-like optimized for small sizes                                  | No dynamic memory allocation and unsorted for small sizes    |

 \*: C++17 compiler only (uses `std::variant` & `std::optional`)

## Benefits

### Performance optimizations

 - For types taking an integral `N` as template parameter, container does not allocate dynamic memory as long as its capacity does not exceed `N`
 - Vectors (and `FlatSet`, as it uses `amc::vector` by default) are all optimized for **trivially relocatable** types (definition below).

Performance compared to the standard containers, as measured by the provided benchmarks (see [Benchmarks](#benchmarks)): the gains come with trade-offs, shown as well.

#### Vectors

![amc::vector vs std::vector](./docs/bench_vectors.svg)

The complex types are 16 bytes structures owning a dynamically allocated buffer (copying them allocates, moving them steals the buffer), the first one being declared trivially relocatable. The gains come from trivially relocatable types, whose elements are shifted with a single `memmove` instead of one move per element. `std::vector` already does it for trivially copyable types like `uint32_t`, and for types which are not trivially relocatable, `amc::vector` uses the same algorithms as `std::vector`.

`amc::vector` grows its capacity by a factor of 1.5, like the MSVC standard library, where libstdc++ and libc++ double it: it reallocates about 1.7 times more often, and moves each element about twice instead of once on average. For trivially relocatable types, `amc::vector` grows with `realloc`, which can extend large blocks without copying them: growing a `uint32_t` vector to 1 million elements is 1.2 times faster than with `std::vector` despite the smaller factor. The smaller factor costs for small vectors, and for types which are not trivially relocatable (see `amc::vector` in the `SmallVector` chart below). Growing a vector of the complex types is not shown: its time is dominated by the allocations of the elements themselves.

#### SmallVector

![amc::SmallVector vs std::vector](./docs/bench_smallvector.svg)

`amc::SmallVector<T, N>` stores up to `N` elements in the object itself: a vector which stays small never allocates. In this benchmark, a vector is filled by `emplace_back`, read and destroyed, a given share of the vectors being small (up to the inline capacity of 16 elements) and the others large (17 to 64 elements, which move to the heap). The more vectors stay small, the bigger the gain: up to 2.4 to 3.2 times for `uint32_t`, and 1.3 to 1.6 times for the complex types, whose elements allocate their own buffer. When most vectors outgrow the inline storage, moving their elements to the heap costs, especially for types which are not trivially relocatable: `SmallVector` then takes about a third more time than `std::vector`.

`amc::vector` is slower than `std::vector` for the complex types here, because of its smaller growth factor, and for the trivially relocatable one, because `realloc` cannot extend a block in place when the buffers of the elements are allocated right after it.

#### Sets

![amc::FlatSet vs std::set and std::unordered_set](./docs/bench_sets.svg)

`amc::FlatSet` is a sorted vector: inserting or erasing an element shifts the following ones. This is fast for small sets of trivially relocatable types, but its cost grows linearly with the size of the set: erasing all the elements of a `uint32_t` set in random order is competitive with `std::set` up to 10 000 elements, and more than 4 times slower with 100 000. For types which are not trivially relocatable, the shifts move the elements one by one, and `std::set` inserts and erases faster.

![uint32_t set lookups by number of elements](./docs/bench_set_lookups.svg)

Lookup time mostly depends on where the set fits in the cache hierarchy: per element, `amc::FlatSet<uint32_t>` takes 4 bytes, `std::set` and `std::unordered_set` about 40 to 50 bytes (node, allocation overhead and buckets). Values are pseudo random, as consecutive integers never collide with the identity hash of libstdc++.

 - **independent** lookups: the CPU overlaps the cache misses of successive lookups, so this is a throughput, which can be shorter than a memory access,
 - **chained** lookups: each looked up value depends on the element found by the previous lookup, so this is the latency of a single lookup.

 - With 1 000 000 elements, `amc::FlatSet` (4 MB) still fits in the L3 cache while the node based sets (40 to 50 MB) do not: its lookup latency is about 3.5 times lower than the one of `std::unordered_set`.
 - Clang compiles the binary searches of `amc::FlatSet` (`std::lower_bound`) and `std::set` with conditional moves, GCC with branches. Mispredicted branches serialize the lookups, so with GCC independent lookups are barely faster than chained ones. Beyond the L3 cache however, the speculative execution of the predicted branch prefetches the next levels of the binary search of `amc::FlatSet`, which is then faster with GCC than with Clang.

#### Benchmarks

The charts are drawn by [benchmark/plot_benchmarks.py](benchmark/plot_benchmarks.py) from the results stored in [docs/benchmarks](docs/benchmarks): median of 5 repetitions, pinned to a single core of an idle machine, with libstdc++ 13. Beyond the L3 cache, results vary by about 15 % between runs. The numbers of both compilers:

<!-- BEGIN benchmark tables, generated by benchmark/plot_benchmarks.py -->

<details><summary>Vectors, GCC 13</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.64 µs |     6.58 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      279 ns |      276 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     49.1 µs |     40.6 µs | 1.2× faster |
| Complex trivially relocatable type: Insert N copies                   |     2.13 µs |      227 ns | 9.4× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     17.3 µs |     13.9 µs | 1.2× faster |
| Complex trivially relocatable type: Insert a std::set range           |     28.4 µs |     17.4 µs | 1.6× faster |
| uint32_t: Assign a count or a range                                   |       12 ns |     14.3 ns | 1.2× slower |
| uint32_t: Construct and swap                                          |     10.3 ns |     8.85 ns | 1.2× faster |
| uint32_t: Insert at end, erase at front                               |      216 ns |      208 ns |        same |
| uint32_t: Insert N copies                                             |     27.8 ns |     30.5 ns | 1.1× slower |
| uint32_t: Insert a range of pointers                                  |       84 ns |     80.2 ns |        same |
| uint32_t: Insert a std::set range                                     |     5.56 µs |      5.3 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     1.35 ms |     1.12 ms | 1.2× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.69 µs |      6.6 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      280 ns |      286 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     45.7 µs |     45.4 µs |        same |
| Complex non trivially relocatable type: Insert N copies               |     2.32 µs |      2.1 µs | 1.1× faster |
| Complex non trivially relocatable type: Insert a range of pointers    |     19.1 µs |     18.4 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     27.7 µs |       25 µs | 1.1× faster |

</details>

<details><summary>Vectors, Clang 23</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.47 µs |     6.55 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      261 ns |      259 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     51.4 µs |     53.8 µs |        same |
| Complex trivially relocatable type: Insert N copies                   |     1.56 µs |      178 ns | 8.8× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     16.9 µs |     13.3 µs | 1.3× faster |
| Complex trivially relocatable type: Insert a std::set range           |     26.8 µs |     16.2 µs | 1.7× faster |
| uint32_t: Assign a count or a range                                   |     12.8 ns |     11.3 ns | 1.1× faster |
| uint32_t: Construct and swap                                          |     8.03 ns |     9.68 ns | 1.2× slower |
| uint32_t: Insert at end, erase at front                               |      213 ns |      208 ns |        same |
| uint32_t: Insert N copies                                             |       30 ns |     30.1 ns |        same |
| uint32_t: Insert a range of pointers                                  |     81.9 ns |     79.4 ns |        same |
| uint32_t: Insert a std::set range                                     |     5.51 µs |     5.42 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     1.29 ms |     1.07 ms | 1.2× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.47 µs |      6.6 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      268 ns |      262 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     46.2 µs |     45.8 µs |        same |
| Complex non trivially relocatable type: Insert N copies               |     1.62 µs |     1.86 µs | 1.1× slower |
| Complex non trivially relocatable type: Insert a range of pointers    |     17.6 µs |     17.3 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     27.4 µs |     23.7 µs | 1.2× faster |

</details>

<details><summary>SmallVector, GCC 13</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      385 ns |      413 ns |                  361 ns |                1.1× faster |
| Complex trivially relocatable type: 50 % small     |      246 ns |      274 ns |                  215 ns |                1.1× faster |
| Complex trivially relocatable type: 80 % small     |      157 ns |      181 ns |                  126 ns |                1.2× faster |
| Complex trivially relocatable type: 90 % small     |      126 ns |      148 ns |                 94.5 ns |                1.3× faster |
| Complex trivially relocatable type: 95 % small     |      112 ns |      131 ns |                 79.1 ns |                1.4× faster |
| Complex trivially relocatable type: 99 % small     |     99.4 ns |      116 ns |                 67.3 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     96.3 ns |      112 ns |                 62.3 ns |                1.5× faster |
| uint32_t: All large                                |     74.5 ns |       87 ns |                 60.7 ns |                1.2× faster |
| uint32_t: 50 % small                               |     63.1 ns |     65.3 ns |                 44.1 ns |                1.4× faster |
| uint32_t: 80 % small                               |     51.4 ns |     46.2 ns |                 26.5 ns |                1.9× faster |
| uint32_t: 90 % small                               |     47.7 ns |     39.9 ns |                 20.6 ns |                2.3× faster |
| uint32_t: 95 % small                               |     45.6 ns |     36.5 ns |                 17.7 ns |                2.6× faster |
| uint32_t: 99 % small                               |     43.6 ns |     34.4 ns |                 15.1 ns |                2.9× faster |
| uint32_t: All small                                |     43.4 ns |     34.7 ns |                 13.8 ns |                3.2× faster |
| Complex non trivially relocatable type: All large  |      371 ns |      524 ns |                  495 ns |                1.3× slower |
| Complex non trivially relocatable type: 50 % small |      240 ns |      328 ns |                  286 ns |                1.2× slower |
| Complex non trivially relocatable type: 80 % small |      157 ns |      194 ns |                  153 ns |                       same |
| Complex non trivially relocatable type: 90 % small |      125 ns |      154 ns |                  109 ns |                1.1× faster |
| Complex non trivially relocatable type: 95 % small |      112 ns |      135 ns |                 87.4 ns |                1.3× faster |
| Complex non trivially relocatable type: 99 % small |     99.9 ns |      118 ns |                 69.1 ns |                1.4× faster |
| Complex non trivially relocatable type: All small  |     99.4 ns |      113 ns |                 62.3 ns |                1.6× faster |

</details>

<details><summary>SmallVector, Clang 23</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      370 ns |      460 ns |                  390 ns |                1.1× slower |
| Complex trivially relocatable type: 50 % small     |      240 ns |      293 ns |                  237 ns |                       same |
| Complex trivially relocatable type: 80 % small     |      158 ns |      198 ns |                  138 ns |                1.1× faster |
| Complex trivially relocatable type: 90 % small     |      129 ns |      160 ns |                  106 ns |                1.2× faster |
| Complex trivially relocatable type: 95 % small     |      112 ns |      144 ns |                 89.2 ns |                1.3× faster |
| Complex trivially relocatable type: 99 % small     |      102 ns |      131 ns |                 75.8 ns |                1.3× faster |
| Complex trivially relocatable type: All small      |     98.3 ns |      124 ns |                 71.3 ns |                1.4× faster |
| uint32_t: All large                                |     62.2 ns |     86.8 ns |                   74 ns |                1.2× slower |
| uint32_t: 50 % small                               |       58 ns |     65.3 ns |                 50.1 ns |                1.2× faster |
| uint32_t: 80 % small                               |     44.9 ns |     46.6 ns |                 30.8 ns |                1.5× faster |
| uint32_t: 90 % small                               |     41.3 ns |       40 ns |                   23 ns |                1.8× faster |
| uint32_t: 95 % small                               |       40 ns |     37.2 ns |                 19.4 ns |                2.1× faster |
| uint32_t: 99 % small                               |     38.5 ns |     34.8 ns |                 16.1 ns |                2.4× faster |
| uint32_t: All small                                |     37.7 ns |     33.9 ns |                 15.6 ns |                2.4× faster |
| Complex non trivially relocatable type: All large  |      362 ns |      525 ns |                  490 ns |                1.4× slower |
| Complex non trivially relocatable type: 50 % small |      237 ns |      329 ns |                  288 ns |                1.2× slower |
| Complex non trivially relocatable type: 80 % small |      151 ns |      202 ns |                  160 ns |                1.1× slower |
| Complex non trivially relocatable type: 90 % small |      123 ns |      161 ns |                  116 ns |                1.1× faster |
| Complex non trivially relocatable type: 95 % small |      109 ns |      141 ns |                   94 ns |                1.2× faster |
| Complex non trivially relocatable type: 99 % small |     96.5 ns |      122 ns |                 76.7 ns |                1.3× faster |
| Complex non trivially relocatable type: All small  |     93.3 ns |      118 ns |                 71.3 ns |                1.3× faster |

</details>

<details><summary>Sets, GCC 13</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.43 µs |                    |       3.2 µs |     1.4× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   109 µs |                    |      47.8 µs |     2.3× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |  1.13 µs |                    |       210 ns |     5.4× faster |
| uint32_t: Insert 200 random values                               |  2.75 µs |            3.72 µs |      1.26 µs |     2.2× faster |
| uint32_t: Copy, erase all of 100                                 |  1.33 µs |             1.2 µs |       429 ns |     3.1× faster |
| uint32_t: Copy, erase all of 1 000                               |  16.6 µs |            24.4 µs |      9.76 µs |     1.7× faster |
| uint32_t: Copy, erase all of 10 000                              |   796 µs |             291 µs |       920 µs |     1.2× slower |
| uint32_t: Copy, erase all of 100 000                             |    15 ms |            4.59 ms |      69.9 ms |     4.7× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.44 µs |                    |      18.8 µs |     4.2× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   108 µs |                    |       360 µs |     3.3× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   268 ns |                    |       106 ns |     2.5× faster |

</details>

<details><summary>Sets, Clang 23</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.71 µs |                    |       3.2 µs |     1.5× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   111 µs |                    |      47.9 µs |     2.3× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |   486 ns |                    |       127 ns |     3.8× faster |
| uint32_t: Insert 200 random values                               |  3.27 µs |            3.05 µs |      3.04 µs |     1.1× faster |
| uint32_t: Copy, erase all of 100                                 |  1.38 µs |            1.12 µs |      1.21 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |  17.8 µs |            23.3 µs |      21.2 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   826 µs |             276 µs |       704 µs |     1.2× faster |
| uint32_t: Copy, erase all of 100 000                             |  15.3 ms |            4.48 ms |      67.5 ms |     4.4× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.77 µs |                    |      20.7 µs |     4.4× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   110 µs |                    |       179 µs |     1.6× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |  97.4 ns |                    |      76.9 ns |     1.3× faster |

</details>

<details><summary>uint32_t lookups (independent / chained), GCC 13</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 22.9 ns / 24.3 ns |    7.04 ns / 14 ns |   24 ns / 26.3 ns |
| 1 000      | 34.4 ns / 35.2 ns |  7.74 ns / 14.6 ns |   37 ns / 39.6 ns |
| 10 000     | 58.8 ns / 59.8 ns |  9.29 ns / 20.7 ns | 48.9 ns / 50.5 ns |
| 100 000    |   112 ns / 114 ns |  9.23 ns / 39.1 ns |   64 ns / 65.9 ns |
| 1 000 000  |   536 ns / 568 ns |   56.4 ns / 319 ns | 94.9 ns / 95.8 ns |
| 10 000 000 |  1.2 µs / 1.24 µs |   75.6 ns / 464 ns |   207 ns / 288 ns |

</details>

<details><summary>uint32_t lookups (independent / chained), Clang 23</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 13.8 ns / 22.5 ns |  7.38 ns / 14.4 ns | 10.7 ns / 18.2 ns |
| 1 000      | 21.9 ns / 31.3 ns |  7.97 ns / 14.7 ns | 11.7 ns / 21.9 ns |
| 10 000     | 33.2 ns / 52.9 ns |  9.57 ns / 21.1 ns |     21 ns / 29 ns |
| 100 000    |  58.3 ns / 108 ns |   9.5 ns / 39.4 ns | 29.7 ns / 42.6 ns |
| 1 000 000  |   248 ns / 658 ns |   57.4 ns / 321 ns | 46.2 ns / 89.2 ns |
| 10 000 000 |  605 ns / 1.26 µs |   76.5 ns / 466 ns |   259 ns / 548 ns |

</details>

<!-- END benchmark tables -->

### Other benefits

 - All 3 vector flavors share the same code / algorithms for vector operations.
 - Templated code generation is minimized thanks to the late location of the integral N template parameter
 - Optimized emulations of standard library features for older C++ compilers are provided when C++ version < C++17

A set of non standard methods and constructors are defined for convenience, provided that `amc` is compiled with `AMC_PEDANTIC` disabled (default, see [Options](#options)).
Here is a brief summary of these extras (compared to their STL equivalents):

#### For vector types
For all vectors (`FixedCapacityVector`, `SmallVector`, `vector`)
| Method         | Description                                                  |
| -------------- | ------------------------------------------------------------ |
| `pop_back_val` | Same as `pop_back`, returning popped value.                  |
| `append`       | Same as `insert(vec.end(), ...)`                             |
| `swap2`        | Swap with all other flavors of vectors, not just `this` type |

For `SmallVector` only, there is a constructor from a rvalue of a `amc::vector` that allows stealing of its dynamic storage.

#### For FlatSet
| Method          | Description                                                                                |
| --------------- | ------------------------------------------------------------------------------------------ |
| `data`          | Returns `data` const pointer from underlying vector                                        |
| `operator[n]`   | Access to the underlying value at position 'n' for the `FlatSet`                           |
| `at(n)`         | Access to the underlying value at position 'n' for the `FlatSet`, throwing if our of range |
| `capacity`      | Calls underlying vector `capacity` method                                                  |
| `reserve`       | Calls underlying vector `reserve` method                                                   |
| `shrink_to_fit` | Calls `shrink_to_fit` of underlying vector                                                 |

There is an additional constructor and assignment operator from a rvalue of the underlying vector type, stealing its dynamic storage.

## What is a trivially relocatable type?

It describes the ability of moving around memory a value of type T by using `memcpy` (as opposed to the conservative approach of calling the copy constructor and the destroying the old temporary). 
It is a type trait currently not (yet?) present in the standard, although is has been proposed (more information [here](https://quuxplusone.github.io/blog/2018/07/18/announcing-trivially-relocatable/)).
No need to use a modified compiler to benefit from trivially relocatibilty optimizations: you can use helper type traits provided by this library to mark explicitely types that you know **are** trivially relocatable. The conservative approach assumes that all trivially copyable types are trivially relocatable, so no need to mark them as such.
With trivially relocatable types, performance gains are easily measurable for all operations of the Vector like container involving *relocation* of elements (grow, insert in middle, etc).

Fortunately, most types are trivially relocatable. `amc::vector` itself is trivially relocatable (as well as `FixedCapacityVector` and `SmallVector` if T is). Types containing pointers to parts of themselves are typically not trivially relocatable, because moving them would require to update the internal pointers they hold to parts of themselves (`std::list`, `std::set`, `std::map` are for instance). `std::string` is not trivially relocatable in some implementations, but some open source equivalents are (for instance, [folly::fbstring](https://github.com/facebook/folly/blob/master/folly/FBString.h)). More information [here](https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable/).

The most convenient way to mark a type as trivially relocatable is to declare in the public part of the class:

`using trivially_relocatable = std::true_type;`

This is only necessary for non trivially copyable types, because trivially copyable types are trivially relocatable by default.

## Build with CMake

### Options

| CMake flag             | Description                                                                                                        |
| ---------------------- | ------------------------------------------------------------------------------------------------------------------ |
| AMC_ENABLE_TESTS       | Build **amc** with unit tests (default if main project)                                                            |
| AMC_ENABLE_BENCHMARKS  | Build **amc** with benchmarks against STL (default if main project and Release mode)                               |
| AMC_ENABLE_ASAN        | Build with Address and Undefined Behavior Sanitizers (only GCC and Clang, **OFF** by default)                      |
| AMC_ENABLE_COVERAGE    | Instrument the unit tests for code coverage and add a `coverage` target (only Clang, **OFF** by default)           |
| AMC_WARNINGS_AS_ERRORS | Treat warnings as errors in Debug builds of the unit tests and benchmarks (**OFF** by default, set in the CI)      |
| AMC_PEDANTIC           | If **OFF**, non standard methods and constructors are added for containers (see [Other benefits](#other-benefits)) |

### As a main project

This library is header only library, with one file to be included per container.

Vectors and `FlatSet` containers require a C++11 compiler. 
`SmallSet` however, needs a C++17 compiler because it uses `std::variant` and `std::optional`, although `boost::variant` could be used as a workaround if a C++17 compiler is not available.

Unit tests and benchmarks are provided. They can be compiled with **cmake**. 

By default, both will be compiled only if 'amc' is instantiated as the main project. You can manually force the build of the tests and benchmarks thanks to following `cmake` flags:
```
AMC_ENABLE_TESTS
AMC_ENABLE_BENCHMARKS
```

Bundled tests depend on [Google Test](https://github.com/google/googletest), benchmarks on [Google benchmarks](https://github.com/google/benchmark).

If not installed on your machine, `cmake` will retrieve them automatically thanks to [FetchContent](https://cmake.org/cmake/help/latest/module/FetchContent.html) feature.

To compile and launch the tests in `Debug` mode, simply launch

`mkdir build && cd build && cmake -DCMAKE_BUILD_TYPE=Debug .. && make && ctest`

#### Code coverage

Code coverage is measured with [LLVM source-based code coverage](https://clang.llvm.org/docs/SourceBasedCodeCoverage.html), so it requires Clang, and `llvm-profdata` / `llvm-cov` of the same version (package `llvm-<version>` on Debian / Ubuntu).
Configure with `AMC_ENABLE_COVERAGE` and build the `coverage` target:

```
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_COMPILER=clang++ -DAMC_ENABLE_COVERAGE=ON
cmake --build build --target coverage
```

It runs all the unit tests and prints the coverage of each header of `include/amc`. The detailed report, with line and branch counts, is written in `build/coverage/html/index.html`, and an lcov export in `build/coverage/amc.lcov`.

### As a sub-project with cmake

#### With FetchContent

```
include(FetchContent)

FetchContent_Declare(
  amadeusamc
  GIT_REPOSITORY https://github.com/AmadeusITGroup/amc.git
  GIT_TAG        origin/main
)

FetchContent_MakeAvailable(amadeusamc)
```

Official documentation [here](https://cmake.org/cmake/help/latest/module/FetchContent.html).

By default, `amc` unit tests and benchmarks will not be compiled when used as a sub-project, which is probably what you want. 

`cmake` targets using amc containers can then be linked with the interface library `amc::amc`:
```
target_link_libraries(my_target PRIVATE amc::amc)
```

#### By installing amc

Just use `sudo make install` or `sudo ninja install` depending on your generator to install headers on your machine.

If you plan to use non standard extra features, make sure you add:
```
#define AMC_NONSTD_FEATURES
```

before any include of `amc` headers (it is defined if `AMC_PEDANTIC` CMake flag is `OFF` when building as a main project).

And that's all. You just need to include the corresponding container's header file to be used in your application code, and why not define them in your namespace.

```cpp
#define AMC_NONSTD_FEATURES // If you need non standard features
#include <amc/vector.hpp>
#include <amc/smallvector.hpp>
#include <amc/fixedcapacityvector.hpp>

#include <amc/flatset.hpp>
#include <amc/smallset.hpp> // Requires C++17
#undef AMC_NONSTD_FEATURES

namespace my_namespace {
using amc::vector;
using amc::SmallVector;
using amc::FixedCapacityVector;

using amc::FlatSet;
using amc::SmallSet;
}
```

### Tested environments

This library has been tested on Ubuntu 18.04 and Windows 10 (Visual Studio 2019), from cmake 3.15 and the following compilers:
 - GCC from version 5.5 to 10
 - Clang from version 6.0
 - MSVC 19.28

You can refer to the CI configurations (lots of compilers are tested) to see the full list of tested compilers.

## Usage examples

### Vectors 

#### amc::vector

`amc::vector` can be used as drop-in replacement for `std::vector`, especially when the underlying type is *trivially relocatable*.
If your type is *trivially copyable*, optimizations are automatically activated.
If your type is not trivially copyable but is *trivially relocatable*, make sure to mark it as such to activate optimizations.

```cpp
#include <amc/vector.hpp>

struct MyTriviallyRelocatableType {
  MyTriviallyRelocatableType() {}

  // MyTriviallyRelocatableType is not trivially copyable...
  ~MyTriviallyRelocatableType() { free(ptr); }

  //... but trivially relocatable!
  using trivially_relocatable = std::true_type;

  void *ptr{};
};

using MyTriviallyRelocatableTypeVector = amc::vector<MyTriviallyRelocatableType>;
```

#### SmallVector

Special variation of `amc::vector` which does not allocate memory and store objects inline up to a maximum capacity defined at compile-time.
If `SmallVector` has to grow beyond this upper bound capacity, it will behave like a `amc::vector` by allocating dynamic memory.
Once a `SmallVector` has allocated dynamic memory, it will not release its memory and come back to its 'small' state when its size goes back under the maximum inline capacity, unless `shrink_to_fit` is called.

Use it when most of the time (let's say, for instance, in 90 % of the cases) the maximum size of the `SmallVector` does not exceed a compile-time constant to save memory allocations.

```cpp
#include <amc/smallvector.hpp>

using ResidencesOfUser = amc::SmallVector<Residence, 1>;
```

#### FixedCapacityVector

Use it when in your application constraints define a compile-time upper bound of the maximum size of your vector.
Elements are stored inline in the object and no memory allocation occur.

```cpp
#include <cstdint>
#include <amc/fixedcapacityvector.hpp>

using SoldUnitsPerDayInMonth = amc::FixedCapacityVector<int, 31>;
```

In the unlikely event that the vector attempts to grow beyond its maximum capacity, behavior can be controlled thanks to the third template parameter `GrowingPolicy`:
 - `ExceptionGrowingPolicy`: throw `std::out_of_range` exception (default)
 - `UncheckedGrowingPolicy`: assert check (nothing is done in `Release`, invoking undefined behavior, abort will be called in `Debug`).

Compared to a `SmallVector` that would never grow, `FixedCapacityVector` will be slightly more efficient (less checks) and make the intent clear, with nice additional iterator validity properties (`begin()` is never invalidated, iterators before any insert / erase are never invalidated).
In addition, if type is trivially destructible, `FixedCapacityVector` will be itself trivially destructible.

### Sets

#### FlatSet

Also sometimes called `SortedVector`, it uses a sorted `amc::vector` as storage (by default, provided as template type) and is thus cache friendly and memory efficient set-like container.
It can be used as a drop-in replacement for `std::set` especially when the read operations occur much frequently than the writes.
Even if there are a lot of writes, it is still very efficient for *trivially relocatable* types as it uses `amc::vector` by default which relocates elements very efficiently.

Besides, the vector container is templated and thus can be combined with above vectors variations to optimize memory allocations (`SmallVector` or `FixedCapacityVector`).

```cpp
#include <cstdint>
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>

using CapitalLettersSetCont = amc::FixedCapacityVector<char, 26>;
using CapitalLettersSet = amc::FlatSet<char, std::less<char>, CapitalLettersSetCont::allocator_type, CapitalLettersSetCont>;
```

#### SmallSet (c++17)

Additional variation of `std::set` like container. This one has a hybrid behavior similar to `SmallVector`:
 - In its 'small' state, there is no dynamic allocation and elements are stored unordered in an inline vector
 - In its large state, `SmallSet` uses the templated provided Set type. It is a `std::set` by default, but it could be any type which provides a set like interface, like `FlatSet` for instance. In this case, `SmallSet` iterators are optimized into pointers.

Note that insertions have linear complexity in the small state so the inline capacity should not be too large.

```cpp
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/smallset.hpp>

using VisitedCountries = amc::SmallSet<Country, 5>;
using VisitedCities = amc::SmallSet<City, 20, std::less<City>, amc::allocator<City>, amc::FlatSet<City>>;
```