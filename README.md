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

`amc::vector` doubles its capacity while it takes fewer than 4 KiB, like libstdc++ and libc++, as reallocating a small block costs more than the memory left unused. Beyond, it grows its capacity by a factor of 1.5, like the MSVC standard library, to save memory: a large vector reallocates about 1.7 times more often, and moves each element about twice instead of once on average. For trivially relocatable types, `amc::vector` grows with `realloc`, which can extend large blocks without copying them: growing a `uint32_t` vector to 1 million elements is 2.1 times faster than with `std::vector` despite the smaller factor. Blocks of up to 1 KiB are copied to a new block instead, as `realloc` can seldom extend them in place and bypasses the fast paths of `malloc` and `free` for small blocks. The smaller factor costs for large vectors of types which are not trivially relocatable. Growing a vector of the complex types is not shown: its time is dominated by the allocations of the elements themselves.

These allocations also dominate inserting at the end and erasing at the front, where `amc::vector` is slower than `std::vector` for the complex types despite the `memmove`. Each iteration allocates and frees about 2 400 element buffers, and glibc returns the free memory at the top of the heap to the system once it exceeds 128 KiB (`M_TRIM_THRESHOLD`): the next allocations take page faults to get it back. Whether this happens depends on the exact sequence of allocations. Before inserting copies of a value, libstdc++ copies it into a temporary, which `amc::vector` does not need. This one extra block per iteration changes which freed blocks glibc keeps in its thread cache, where they are never merged, and one of them then usually sits right below the top of the heap, preventing the trimming: `amc::vector` takes about 34 page faults per iteration, `std::vector` about 1. With the thread cache disabled, `std::vector` takes as many page faults as `amc::vector`. With the trimming disabled instead (`GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824`), `amc::vector` is 1.2 times faster than `std::vector` for the trivially relocatable type, and as fast for the other one.

#### SmallVector

![amc::SmallVector vs std::vector](./docs/bench_smallvector.svg)

`amc::SmallVector<T, N>` stores up to `N` elements in the object itself: a vector which stays small never allocates. In this benchmark, a vector is filled by `emplace_back`, read and destroyed, a given share of the vectors being small (up to the inline capacity of 16 elements) and the others large (17 to 64 elements, which move to the heap). The more vectors stay small, the bigger the gain: up to 2.5 to 3.2 times for `uint32_t`, and 1.3 to 1.5 times for the complex types, whose elements allocate their own buffer. When most vectors outgrow the inline storage, moving their elements to the heap costs for types which are not trivially relocatable: `SmallVector` then takes up to 15 % more time than `std::vector`.

`amc::vector` is faster than `std::vector` here for `uint32_t` (1.2 to 1.4 times), and slower for the complex type which is not trivially relocatable (up to 1.3 times when all the vectors are large).

#### Sets

![amc::FlatSet vs std::set and std::unordered_set](./docs/bench_sets.svg)

`amc::FlatSet` is a sorted vector: inserting or erasing an element shifts the following ones. This is fast for small sets of trivially relocatable types, but its cost grows linearly with the size of the set: erasing all the elements of a `uint32_t` set in random order is competitive with `std::set` up to 10 000 elements, and more than 4 times slower with 100 000. For types which are not trivially relocatable, the shifts move the elements one by one, and `std::set` inserts and erases faster.

![uint32_t set lookups by number of elements](./docs/bench_set_lookups.svg)

Lookup time mostly depends on where the set fits in the cache hierarchy: per element, `amc::FlatSet<uint32_t>` takes 4 bytes, `std::set` and `std::unordered_set` about 40 to 50 bytes (node, allocation overhead and buckets). Values are pseudo random, as consecutive integers never collide with the identity hash of libstdc++.

 - **independent** lookups: the CPU overlaps the cache misses of successive lookups, so this is a throughput, which can be shorter than a memory access,
 - **chained** lookups: each looked up value depends on the element found by the previous lookup, so this is the latency of a single lookup.

 - With 1 000 000 elements, `amc::FlatSet` (4 MB) still fits in the L3 cache while the node based sets (40 to 50 MB) do not: its lookup latency is 3.3 to 4.4 times lower than the one of `std::unordered_set`.
 - Clang compiles the binary searches of `amc::FlatSet` (`std::lower_bound`) and `std::set` with conditional moves, GCC with branches. Mispredicted branches serialize the lookups, so with GCC independent lookups are barely faster than chained ones. Beyond the L3 cache however, the speculative execution of the predicted branch prefetches the next levels of the binary search of `amc::FlatSet`, which is then faster with GCC than with Clang.

#### Benchmarks

The charts are drawn by [benchmark/plot_benchmarks.py](benchmark/plot_benchmarks.py) from the results stored in [docs/benchmarks](docs/benchmarks): each benchmark in its own process, median of 5 repetitions, pinned to a single core of an idle machine, with libstdc++ 13. Beyond the L3 cache, results vary by about 15 % between runs. The numbers of both compilers:

<!-- BEGIN benchmark tables, generated by benchmark/plot_benchmarks.py -->

<details><summary>Vectors, GCC 13</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.68 µs |     6.61 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      283 ns |      283 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     46.6 µs |     56.1 µs | 1.2× slower |
| Complex trivially relocatable type: Insert N copies                   |     2.11 µs |      176 ns |  12× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     19.5 µs |     13.5 µs | 1.4× faster |
| Complex trivially relocatable type: Insert a std::set range           |     28.1 µs |     19.2 µs | 1.5× faster |
| uint32_t: Assign a count or a range                                   |       12 ns |     13.9 ns | 1.2× slower |
| uint32_t: Construct and swap                                          |     9.92 ns |     8.84 ns | 1.1× faster |
| uint32_t: Insert at end, erase at front                               |      217 ns |      211 ns |        same |
| uint32_t: Insert N copies                                             |     27.5 ns |     30.2 ns | 1.1× slower |
| uint32_t: Insert a range of pointers                                  |     87.4 ns |     81.1 ns | 1.1× faster |
| uint32_t: Insert a std::set range                                     |     4.42 µs |     4.29 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     2.27 ms |     1.06 ms | 2.1× faster |
| Complex non trivially relocatable type: Assign a count or a range     |      6.7 µs |     6.64 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      280 ns |      287 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     45.5 µs |     60.7 µs | 1.3× slower |
| Complex non trivially relocatable type: Insert N copies               |     2.32 µs |     2.06 µs | 1.1× faster |
| Complex non trivially relocatable type: Insert a range of pointers    |     18.8 µs |     18.7 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     27.7 µs |     24.9 µs | 1.1× faster |

</details>

<details><summary>Vectors, Clang 23</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.44 µs |     6.53 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      260 ns |      258 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     45.8 µs |     55.4 µs | 1.2× slower |
| Complex trivially relocatable type: Insert N copies                   |     1.55 µs |      166 ns | 9.3× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     17.1 µs |     13.4 µs | 1.3× faster |
| Complex trivially relocatable type: Insert a std::set range           |       27 µs |     18.6 µs | 1.5× faster |
| uint32_t: Assign a count or a range                                   |     11.4 ns |     9.84 ns | 1.2× faster |
| uint32_t: Construct and swap                                          |     8.09 ns |     8.93 ns | 1.1× slower |
| uint32_t: Insert at end, erase at front                               |      220 ns |      207 ns | 1.1× faster |
| uint32_t: Insert N copies                                             |     30.1 ns |     30.4 ns |        same |
| uint32_t: Insert a range of pointers                                  |     80.7 ns |     82.3 ns |        same |
| uint32_t: Insert a std::set range                                     |     4.38 µs |     4.34 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     2.17 ms |     1.03 ms | 2.1× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.52 µs |     6.56 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      260 ns |      256 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     46.2 µs |     60.5 µs | 1.3× slower |
| Complex non trivially relocatable type: Insert N copies               |     1.42 µs |     1.48 µs |        same |
| Complex non trivially relocatable type: Insert a range of pointers    |       17 µs |     17.3 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     27.8 µs |     24.5 µs | 1.1× faster |

</details>

<details><summary>SmallVector, GCC 13</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      370 ns |      334 ns |                  315 ns |                1.2× faster |
| Complex trivially relocatable type: 50 % small     |      238 ns |      215 ns |                  196 ns |                1.2× faster |
| Complex trivially relocatable type: 80 % small     |      154 ns |      136 ns |                  116 ns |                1.3× faster |
| Complex trivially relocatable type: 90 % small     |      125 ns |      111 ns |                 90.5 ns |                1.4× faster |
| Complex trivially relocatable type: 95 % small     |      110 ns |     97.6 ns |                 77.3 ns |                1.4× faster |
| Complex trivially relocatable type: 99 % small     |     99.1 ns |     86.4 ns |                 66.7 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     96.1 ns |     85.6 ns |                 62.9 ns |                1.5× faster |
| uint32_t: All large                                |     74.2 ns |     54.3 ns |                 43.2 ns |                1.7× faster |
| uint32_t: 50 % small                               |     63.4 ns |     47.5 ns |                 33.5 ns |                1.9× faster |
| uint32_t: 80 % small                               |     51.4 ns |     37.7 ns |                 22.5 ns |                2.3× faster |
| uint32_t: 90 % small                               |     47.4 ns |     34.4 ns |                 18.5 ns |                2.6× faster |
| uint32_t: 95 % small                               |     45.2 ns |     32.7 ns |                 16.3 ns |                2.8× faster |
| uint32_t: 99 % small                               |     43.4 ns |     31.3 ns |                 14.4 ns |                3.0× faster |
| uint32_t: All small                                |     42.7 ns |       31 ns |                 13.5 ns |                3.2× faster |
| Complex non trivially relocatable type: All large  |      366 ns |      428 ns |                  388 ns |                1.1× slower |
| Complex non trivially relocatable type: 50 % small |      237 ns |      263 ns |                  228 ns |                       same |
| Complex non trivially relocatable type: 80 % small |      153 ns |      164 ns |                  129 ns |                1.2× faster |
| Complex non trivially relocatable type: 90 % small |      124 ns |      130 ns |                 96.4 ns |                1.3× faster |
| Complex non trivially relocatable type: 95 % small |      118 ns |      114 ns |                 79.7 ns |                1.5× faster |
| Complex non trivially relocatable type: 99 % small |     97.8 ns |      101 ns |                 66.9 ns |                1.5× faster |
| Complex non trivially relocatable type: All small  |     94.4 ns |     95.1 ns |                 62.4 ns |                1.5× faster |

</details>

<details><summary>SmallVector, Clang 23</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      360 ns |      375 ns |                  352 ns |                       same |
| Complex trivially relocatable type: 50 % small     |      235 ns |      243 ns |                  218 ns |                1.1× faster |
| Complex trivially relocatable type: 80 % small     |      151 ns |      153 ns |                  133 ns |                1.1× faster |
| Complex trivially relocatable type: 90 % small     |      123 ns |      124 ns |                  102 ns |                1.2× faster |
| Complex trivially relocatable type: 95 % small     |      110 ns |      109 ns |                 87.6 ns |                1.3× faster |
| Complex trivially relocatable type: 99 % small     |     99.4 ns |     97.3 ns |                 75.4 ns |                1.3× faster |
| Complex trivially relocatable type: All small      |     96.6 ns |     97.7 ns |                 72.1 ns |                1.3× faster |
| uint32_t: All large                                |     61.5 ns |     53.2 ns |                 49.1 ns |                1.3× faster |
| uint32_t: 50 % small                               |     55.1 ns |     46.1 ns |                   37 ns |                1.5× faster |
| uint32_t: 80 % small                               |     44.1 ns |       37 ns |                 24.4 ns |                1.8× faster |
| uint32_t: 90 % small                               |     40.7 ns |     33.7 ns |                 20.1 ns |                2.0× faster |
| uint32_t: 95 % small                               |     38.9 ns |     32.3 ns |                 17.9 ns |                2.2× faster |
| uint32_t: 99 % small                               |     37.3 ns |     31.2 ns |                 15.7 ns |                2.4× faster |
| uint32_t: All small                                |     37.1 ns |     30.7 ns |                 14.9 ns |                2.5× faster |
| Complex non trivially relocatable type: All large  |      358 ns |      454 ns |                  411 ns |                1.1× slower |
| Complex non trivially relocatable type: 50 % small |      233 ns |      284 ns |                  247 ns |                1.1× slower |
| Complex non trivially relocatable type: 80 % small |      151 ns |      179 ns |                  144 ns |                       same |
| Complex non trivially relocatable type: 90 % small |      123 ns |      144 ns |                  108 ns |                1.1× faster |
| Complex non trivially relocatable type: 95 % small |      109 ns |      125 ns |                 90.9 ns |                1.2× faster |
| Complex non trivially relocatable type: 99 % small |       98 ns |      111 ns |                   76 ns |                1.3× faster |
| Complex non trivially relocatable type: All small  |     94.4 ns |      107 ns |                 73.2 ns |                1.3× faster |

</details>

<details><summary>Sets, GCC 13</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.47 µs |                    |         3 µs |     1.5× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   107 µs |                    |      64.7 µs |     1.6× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |  1.09 µs |                    |       190 ns |     5.7× faster |
| uint32_t: Insert 200 random values                               |  2.83 µs |            3.39 µs |      1.13 µs |     2.5× faster |
| uint32_t: Copy, erase all of 100                                 |  1.39 µs |            1.23 µs |       424 ns |     3.3× faster |
| uint32_t: Copy, erase all of 1 000                               |  16.7 µs |            24.2 µs |       9.6 µs |     1.7× faster |
| uint32_t: Copy, erase all of 10 000                              |   803 µs |             287 µs |       930 µs |     1.2× slower |
| uint32_t: Copy, erase all of 100 000                             |    15 ms |             4.6 ms |      69.6 ms |     4.7× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.47 µs |                    |      18.6 µs |     4.2× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   108 µs |                    |       377 µs |     3.5× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   252 ns |                    |      95.1 ns |     2.6× faster |

</details>

<details><summary>Sets, Clang 23</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.79 µs |                    |      3.11 µs |     1.5× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   114 µs |                    |      67.8 µs |     1.7× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |   532 ns |                    |       129 ns |     4.1× faster |
| uint32_t: Insert 200 random values                               |  3.36 µs |            2.92 µs |      2.93 µs |     1.1× faster |
| uint32_t: Copy, erase all of 100                                 |  1.39 µs |            1.13 µs |       1.2 µs |     1.2× faster |
| uint32_t: Copy, erase all of 1 000                               |  17.8 µs |            23.4 µs |        21 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   829 µs |             285 µs |       705 µs |     1.2× faster |
| uint32_t: Copy, erase all of 100 000                             |  16.4 ms |             4.6 ms |      67.4 ms |     4.1× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.83 µs |                    |        20 µs |     4.1× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   114 µs |                    |       198 µs |     1.7× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   117 ns |                    |        78 ns |     1.5× faster |

</details>

<details><summary>uint32_t lookups (independent / chained), GCC 13</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 22.5 ns / 23.9 ns |    7.44 ns / 14 ns | 24.1 ns / 25.9 ns |
| 1 000      | 34.7 ns / 34.6 ns |  8.17 ns / 14.4 ns |   36.9 ns / 39 ns |
| 10 000     | 57.6 ns / 57.4 ns |  9.73 ns / 20.3 ns | 49.1 ns / 50.1 ns |
| 100 000    |   109 ns / 109 ns |  9.56 ns / 39.1 ns | 63.8 ns / 66.3 ns |
| 1 000 000  |   551 ns / 591 ns |   55.9 ns / 315 ns | 95.6 ns / 96.9 ns |
| 10 000 000 | 1.19 µs / 1.22 µs |   75.6 ns / 464 ns |   204 ns / 285 ns |

</details>

<details><summary>uint32_t lookups (independent / chained), Clang 23</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 13.8 ns / 22.6 ns |  7.34 ns / 14.1 ns |   10.8 ns / 18 ns |
| 1 000      |     22 ns / 31 ns |  7.95 ns / 14.6 ns | 11.7 ns / 21.8 ns |
| 10 000     |   33 ns / 51.8 ns |  9.54 ns / 20.7 ns | 21.2 ns / 28.7 ns |
| 100 000    |  59.9 ns / 105 ns |   9.7 ns / 39.8 ns | 30.1 ns / 42.7 ns |
| 1 000 000  |   268 ns / 703 ns |   58.7 ns / 349 ns |   50.2 ns / 80 ns |
| 10 000 000 |  650 ns / 1.36 µs |   77.2 ns / 475 ns |   306 ns / 488 ns |

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