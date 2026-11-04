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
      - [SmallSet](#smallset)
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
        - [Differences with std::set](#differences-with-stdset)

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
 - The binary searches of `amc::FlatSet<uint32_t>` are branchless with both compilers: the next half of the range is selected with a conditional move, which, unlike a branch, is never mispredicted. The address of the next element to compare then depends on the result of the current comparison, so in sets larger than the L1 cache, each step prefetches the 4 elements which the two following steps may compare: the cache misses of successive steps overlap. This applies to scalar types (arithmetic, enumerations, pointers) ordered by `std::less` or `std::greater`. With costlier comparisons, like those of `std::string` or of a comparator dereferencing pointers, the branchless search is slower, so `amc::FlatSet` keeps `std::lower_bound` for them.
 - GCC compiles the tree search of `std::set` with branches, Clang with conditional moves. Mispredicted branches serialize the lookups, so with GCC independent lookups of `std::set` are barely faster than chained ones.

#### SmallSet

![amc::SmallSet vs std::set](./docs/bench_smallset.svg)

`amc::SmallSet<T, N>` keeps up to `N` elements unsorted in an inline vector: an insertion is a linear search followed by a `push_back`, without allocation. Beyond `N` elements, it moves them to its set type, `std::set` by default. The charts compare `N = 16` to the other sets (`amc::FlatSet` uses an `amc::vector`), with elements which are cheap to compare and trivially relocatable (`uint32_t`), or not (short `std::string`):

 - **`uint32_t`, mostly small sets**: filling a `SmallSet` of up to 16 elements costs 1.5 to 3 ns per element, 6 to 10 times less than a `std::set` and 4.5 to 7.5 times less than an `amc::FlatSet`. As soon as some sets are large though, those of the default `SmallSet<T, 16>` allocate a `std::set` node per element: with 10 % of large sets, it is already 2.3 to 2.4 times slower than a `SmallSet` whose set type is an `amc::FlatSet`.
 - **`SmallSet<T, N, std::less<T>, amc::allocator<T>, amc::FlatSet<T>>`** is the fastest of these sets of `uint32_t` from 50 % of small sets: 6.5 to 6.9 times faster than `std::set` when all the sets are small, and still 2.9 to 3.5 times when they all are large. Its large state is within 10 % of an `amc::FlatSet`, for fills and lookups.
 - **Lookups** are not what `SmallSet` speeds up the most: up to 16 elements, the linear search of the small state is up to 1.7 times faster than the one of a `std::set` with GCC, but the branchless binary search of an `amc::FlatSet` is about twice faster from 4 elements. With Clang, the three of them are within 1.4 times of each other.
 - **`std::string`**: the linear search compares the looked up value with each element (once or twice), so at 16 elements, a lookup takes 1.4 (Clang) to 2 (GCC) times as long as in a `std::set`, and `SmallSet` is 1.5 to 1.6 times slower than `std::set` even when all the sets are small. Only the sets of at most 8 elements are faster to fill. `std::string` is not trivially relocatable either: an `amc::FlatSet` moves its elements one by one at each insertion, so the fill of a large `SmallSet` with an `amc::FlatSet` is 2 to 3 times slower than with a `std::set` at 256 elements, and 17 to 22 times at 4096. `std::unordered_set` is the fastest set of strings, except to fill the smallest ones.

![Sets by number of elements](./docs/bench_smallset_sizes.svg)

`SmallSet` with an `amc::FlatSet` is thus the set of choice for sets of integers, enumerations, pointers or small trivially relocatable types which are often built, most of them with at most `N` elements: like `SmallVector`, choose `N` to cover most of them. Keep `std::set` (or `std::unordered_set` when the order of the elements does not matter) for strings and other types with costly comparisons. The SmallSet benchmarks ran 4 processes at a time, each pinned to its own core of the same L3 cache: they are about 4 % slower than alone, all of them alike.

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

<details><summary>SmallSet, GCC 13</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  3.26 µs |            1.54 µs |       912 ns |                      925 ns |         3.13 µs |                   938 ns |                       3.5× faster |
| uint32_t: 50 % small                     |  1.95 µs |             941 ns |       546 ns |                      539 ns |         1.63 µs |                   516 ns |                       3.8× faster |
| uint32_t: 80 % small                     |  1.06 µs |             564 ns |       328 ns |                      310 ns |          705 ns |                   257 ns |                       4.1× faster |
| uint32_t: 90 % small                     |   768 ns |             423 ns |       248 ns |                      230 ns |          385 ns |                   159 ns |                       4.8× faster |
| uint32_t: 95 % small                     |   626 ns |             356 ns |       208 ns |                      194 ns |          236 ns |                   113 ns |                       5.5× faster |
| uint32_t: 99 % small                     |   499 ns |             306 ns |       179 ns |                      160 ns |          107 ns |                  80.1 ns |                       6.2× faster |
| uint32_t: All small                      |   458 ns |             291 ns |       171 ns |                      151 ns |         73.1 ns |                  70.9 ns |                       6.5× faster |
| std::string of 12 characters: All large  |  5.89 µs |            2.55 µs |      5.78 µs |                     6.25 µs |         6.95 µs |                  7.01 µs |                       1.2× slower |
| std::string of 12 characters: 50 % small |  3.42 µs |            1.55 µs |      3.34 µs |                     3.51 µs |         4.17 µs |                  4.27 µs |                       1.2× slower |
| std::string of 12 characters: 80 % small |  1.93 µs |             934 ns |       1.8 µs |                     1.87 µs |         2.46 µs |                  2.47 µs |                       1.3× slower |
| std::string of 12 characters: 90 % small |   1.4 µs |             733 ns |      1.28 µs |                     1.31 µs |         1.87 µs |                   1.9 µs |                       1.4× slower |
| std::string of 12 characters: 95 % small |  1.12 µs |             646 ns |      1.02 µs |                     1.02 µs |         1.59 µs |                  1.63 µs |                       1.5× slower |
| std::string of 12 characters: 99 % small |   883 ns |             542 ns |       830 ns |                      798 ns |         1.35 µs |                  1.37 µs |                       1.5× slower |
| std::string of 12 characters: All small  |   830 ns |             515 ns |       743 ns |                      730 ns |          1.3 µs |                  1.32 µs |                       1.6× slower |

</details>

<details><summary>SmallSet, Clang 23</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  2.57 µs |            1.62 µs |       899 ns |                      920 ns |         2.51 µs |                   899 ns |                       2.9× faster |
| uint32_t: 50 % small                     |  1.48 µs |             964 ns |       543 ns |                      542 ns |         1.29 µs |                   481 ns |                       3.1× faster |
| uint32_t: 80 % small                     |   811 ns |             563 ns |       317 ns |                      312 ns |          549 ns |                   222 ns |                       3.7× faster |
| uint32_t: 90 % small                     |   586 ns |             431 ns |       240 ns |                      232 ns |          313 ns |                   137 ns |                       4.3× faster |
| uint32_t: 95 % small                     |   477 ns |             365 ns |       202 ns |                      194 ns |          182 ns |                  95.8 ns |                       5.0× faster |
| uint32_t: 99 % small                     |   379 ns |             312 ns |       175 ns |                      162 ns |         81.2 ns |                  60.1 ns |                       6.3× faster |
| uint32_t: All small                      |   353 ns |             294 ns |       164 ns |                      154 ns |         54.8 ns |                  51.2 ns |                       6.9× faster |
| std::string of 12 characters: All large  |  5.44 µs |            2.62 µs |      5.73 µs |                     5.73 µs |         6.42 µs |                  6.24 µs |                       1.1× slower |
| std::string of 12 characters: 50 % small |  3.12 µs |            1.57 µs |      3.26 µs |                      3.3 µs |         3.77 µs |                  3.75 µs |                       1.2× slower |
| std::string of 12 characters: 80 % small |  1.74 µs |             940 ns |      1.83 µs |                     1.77 µs |         2.19 µs |                  2.18 µs |                       1.3× slower |
| std::string of 12 characters: 90 % small |  1.25 µs |             750 ns |      1.24 µs |                     1.25 µs |         1.66 µs |                  1.66 µs |                       1.3× slower |
| std::string of 12 characters: 95 % small |   992 ns |             634 ns |       984 ns |                      961 ns |         1.43 µs |                   1.4 µs |                       1.4× slower |
| std::string of 12 characters: 99 % small |   792 ns |             545 ns |       761 ns |                      733 ns |         1.17 µs |                   1.2 µs |                       1.5× slower |
| std::string of 12 characters: All small  |   747 ns |             517 ns |       705 ns |                      671 ns |         1.12 µs |                  1.13 µs |                       1.5× slower |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), GCC 13</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         |  10.8 ns / 5.4 ns |  18.7 ns / 6.89 ns |   10 ns / 6.67 ns |            4.4 ns / 6.68 ns | 1.63 ns / 3.94 ns |        1.72 ns / 3.93 ns |
| uint32_t: 4                         | 14.4 ns / 8.45 ns |  15.1 ns / 6.91 ns |  11.4 ns / 3.7 ns |           7.22 ns / 8.53 ns |  1.5 ns / 6.44 ns |        1.59 ns / 6.33 ns |
| uint32_t: 8                         | 18.2 ns / 9.27 ns |   15.9 ns / 9.6 ns | 12.3 ns / 2.83 ns |           9.56 ns / 9.12 ns | 1.87 ns / 6.68 ns |        1.88 ns / 6.65 ns |
| uint32_t: 16                        | 27.7 ns / 12.6 ns |  22.7 ns / 13.1 ns | 13.9 ns / 4.14 ns |           12.4 ns / 10.4 ns | 2.68 ns / 7.59 ns |        2.89 ns / 7.04 ns |
| uint32_t: 32                        |   32.9 ns / 15 ns |    26 ns / 12.1 ns | 15.3 ns / 4.12 ns |           14.6 ns / 11.2 ns | 34.1 ns / 14.9 ns |         14.7 ns / 4.1 ns |
| uint32_t: 64                        |   37 ns / 18.6 ns |    27 ns / 12.6 ns | 17.2 ns / 4.81 ns |           16.7 ns / 12.6 ns | 38.1 ns / 18.6 ns |        16.8 ns / 4.56 ns |
| uint32_t: 128                       | 39.8 ns / 21.7 ns |  27.6 ns / 12.4 ns | 18.7 ns / 5.45 ns |           18.6 ns / 13.1 ns | 41.6 ns / 21.9 ns |        19.4 ns / 5.28 ns |
| uint32_t: 256                       | 42.7 ns / 24.9 ns |  30.1 ns / 14.6 ns | 20.8 ns / 5.94 ns |           20.9 ns / 14.2 ns | 44.6 ns / 25.4 ns |         20.9 ns / 5.8 ns |
| uint32_t: 512                       | 43.6 ns / 28.1 ns |  34.4 ns / 14.6 ns | 23.1 ns / 6.57 ns |           23.5 ns / 15.2 ns | 45.5 ns / 28.9 ns |        23.4 ns / 6.61 ns |
| uint32_t: 1 024                     | 45.7 ns / 33.6 ns |  36.4 ns / 14.8 ns | 26.9 ns / 7.61 ns |           27.1 ns / 16.2 ns | 47.7 ns / 34.4 ns |          27 ns / 7.12 ns |
| uint32_t: 2 048                     | 50.5 ns / 41.8 ns |  37.9 ns / 15.5 ns | 32.8 ns / 8.19 ns |           33.2 ns / 17.2 ns |   52.7 ns / 42 ns |        32.9 ns / 7.89 ns |
| uint32_t: 4 096                     | 54.5 ns / 50.3 ns |  38.2 ns / 15.4 ns | 43.4 ns / 9.15 ns |           43.6 ns / 18.7 ns | 56.3 ns / 50.1 ns |        43.8 ns / 8.95 ns |
| std::string of 12 characters: 2     | 18.8 ns / 11.2 ns |  25.4 ns / 9.15 ns | 17.2 ns / 12.5 ns |           11.6 ns / 12.6 ns | 10.4 ns / 16.3 ns |        11.4 ns / 16.7 ns |
| std::string of 12 characters: 4     | 24.9 ns / 19.2 ns |  22.7 ns / 10.7 ns | 22.1 ns / 19.1 ns |               17 ns / 19 ns | 16.6 ns / 22.7 ns |        17.4 ns / 22.4 ns |
| std::string of 12 characters: 8     | 30.3 ns / 21.9 ns |    23 ns / 13.9 ns | 27.7 ns / 22.8 ns |           23.4 ns / 22.5 ns |   24 ns / 31.6 ns |        24.7 ns / 32.1 ns |
| std::string of 12 characters: 16    | 43.4 ns / 27.9 ns |    34 ns / 21.1 ns |   37 ns / 30.6 ns |           33.4 ns / 29.8 ns |   51 ns / 56.2 ns |        52.2 ns / 55.1 ns |
| std::string of 12 characters: 32    | 53.8 ns / 32.5 ns |  39.2 ns / 17.7 ns | 50.8 ns / 35.6 ns |             46.5 ns / 35 ns | 85.5 ns / 33.9 ns |        78.3 ns / 35.2 ns |
| std::string of 12 characters: 64    | 62.4 ns / 42.3 ns |  38.7 ns / 16.8 ns |   71 ns / 42.5 ns |             65 ns / 42.1 ns | 80.4 ns / 40.9 ns |        97.8 ns / 42.3 ns |
| std::string of 12 characters: 128   | 70.1 ns / 50.4 ns |    38 ns / 17.2 ns |  102 ns / 51.2 ns |           89.2 ns / 49.9 ns |   81 ns / 48.1 ns |         141 ns / 49.7 ns |
| std::string of 12 characters: 256   | 76.6 ns / 57.1 ns |  38.2 ns / 21.5 ns |  157 ns / 57.8 ns |            132 ns / 57.5 ns | 85.3 ns / 55.1 ns |         228 ns / 58.6 ns |
| std::string of 12 characters: 512   | 83.5 ns / 66.3 ns |  41.7 ns / 21.8 ns |  259 ns / 65.1 ns |            210 ns / 64.4 ns | 91.9 ns / 63.6 ns |         402 ns / 65.3 ns |
| std::string of 12 characters: 1 024 | 94.1 ns / 76.8 ns |    44.8 ns / 23 ns |  455 ns / 74.2 ns |              353 ns / 74 ns |  100 ns / 74.3 ns |         729 ns / 73.6 ns |
| std::string of 12 characters: 2 048 |  102 ns / 86.2 ns |  44.2 ns / 23.6 ns |  833 ns / 86.3 ns |            632 ns / 88.1 ns |  109 ns / 83.9 ns |        1.38 µs / 85.6 ns |
| std::string of 12 characters: 4 096 |  113 ns / 97.4 ns |    45 ns / 24.4 ns |  1.6 µs / 98.8 ns |             1.2 µs / 104 ns |    120 ns / 97 ns |         2.7 µs / 99.2 ns |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), Clang 23</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 11.3 ns / 7.41 ns |  18.5 ns / 6.66 ns | 10.2 ns / 6.68 ns |           6.91 ns / 6.67 ns | 1.94 ns / 6.72 ns |        1.78 ns / 6.71 ns |
| uint32_t: 4                         |    15 ns / 5.8 ns |  14.7 ns / 7.24 ns | 11.3 ns / 8.39 ns |           9.22 ns / 8.35 ns | 1.79 ns / 6.76 ns |        1.77 ns / 6.75 ns |
| uint32_t: 8                         |   19 ns / 7.03 ns |  15.1 ns / 10.3 ns | 11.9 ns / 9.03 ns |           10.7 ns / 9.05 ns | 2.08 ns / 7.24 ns |        2.19 ns / 7.22 ns |
| uint32_t: 16                        | 28.3 ns / 11.4 ns |  21.1 ns / 13.5 ns | 13.4 ns / 10.4 ns |           12.6 ns / 10.4 ns | 2.97 ns / 8.11 ns |        3.17 ns / 8.14 ns |
| uint32_t: 32                        |   35.6 ns / 13 ns |  25.1 ns / 12.2 ns |   15 ns / 11.2 ns |             15 ns / 11.2 ns | 34.7 ns / 13.3 ns |        14.7 ns / 12.1 ns |
| uint32_t: 64                        |   40.2 ns / 13 ns |      26 ns / 13 ns | 16.7 ns / 12.1 ns |           16.8 ns / 12.2 ns | 39.4 ns / 13.2 ns |        16.6 ns / 13.1 ns |
| uint32_t: 128                       |   44 ns / 14.1 ns |  26.8 ns / 12.8 ns |   18 ns / 13.1 ns |           18.4 ns / 13.1 ns | 42.8 ns / 13.9 ns |            18 ns / 14 ns |
| uint32_t: 256                       | 47.2 ns / 17.2 ns |  28.2 ns / 14.9 ns | 20.4 ns / 14.1 ns |             20.7 ns / 14 ns | 46.1 ns / 17.1 ns |          20.4 ns / 15 ns |
| uint32_t: 512                       | 50.8 ns / 20.9 ns |  31.8 ns / 14.8 ns | 23.2 ns / 16.2 ns |           23.4 ns / 15.2 ns | 48.9 ns / 20.8 ns |        23.1 ns / 16.2 ns |
| uint32_t: 1 024                     | 53.3 ns / 22.7 ns |  33.4 ns / 15.2 ns | 26.8 ns / 16.2 ns |           27.1 ns / 16.2 ns | 52.5 ns / 22.6 ns |        26.9 ns / 17.3 ns |
| uint32_t: 2 048                     | 58.9 ns / 26.8 ns |  34.2 ns / 15.9 ns | 32.6 ns / 17.2 ns |           33.4 ns / 17.4 ns | 57.3 ns / 26.7 ns |        32.7 ns / 18.5 ns |
| uint32_t: 4 096                     | 63.6 ns / 30.5 ns |    34.6 ns / 16 ns | 42.7 ns / 19.1 ns |           43.7 ns / 19.3 ns | 62.1 ns / 31.7 ns |        43.3 ns / 20.2 ns |
| std::string of 12 characters: 2     | 18.8 ns / 13.1 ns |    26.9 ns / 10 ns | 17.3 ns / 11.8 ns |           11.4 ns / 11.8 ns | 10.1 ns / 13.6 ns |          10 ns / 13.1 ns |
| std::string of 12 characters: 4     | 23.8 ns / 19.5 ns |  23.3 ns / 11.4 ns | 22.4 ns / 17.3 ns |             17 ns / 17.9 ns | 14.6 ns / 20.6 ns |        14.5 ns / 19.9 ns |
| std::string of 12 characters: 8     | 32.2 ns / 25.3 ns |  25.4 ns / 15.7 ns | 28.8 ns / 19.9 ns |             25 ns / 20.5 ns | 22.6 ns / 27.4 ns |        21.9 ns / 26.9 ns |
| std::string of 12 characters: 16    | 45.7 ns / 32.1 ns |  34.7 ns / 22.7 ns | 37.8 ns / 27.7 ns |           35.9 ns / 28.5 ns | 47.8 ns / 44.9 ns |            48 ns / 45 ns |
| std::string of 12 characters: 32    | 56.9 ns / 38.6 ns |  40.2 ns / 18.8 ns | 52.6 ns / 32.3 ns |           53.1 ns / 33.7 ns | 82.6 ns / 38.8 ns |        70.9 ns / 32.9 ns |
| std::string of 12 characters: 64    | 66.1 ns / 44.2 ns |  39.1 ns / 17.7 ns | 75.8 ns / 39.9 ns |           77.4 ns / 40.7 ns | 77.4 ns / 44.2 ns |        87.3 ns / 41.3 ns |
| std::string of 12 characters: 128   | 75.9 ns / 50.4 ns |  39.6 ns / 18.3 ns |  115 ns / 47.8 ns |            118 ns / 48.1 ns | 80.2 ns / 50.5 ns |         119 ns / 48.2 ns |
| std::string of 12 characters: 256   |   83.2 ns / 58 ns |  39.4 ns / 22.9 ns |  181 ns / 55.5 ns |              186 ns / 56 ns | 85.1 ns / 58.2 ns |         186 ns / 55.3 ns |
| std::string of 12 characters: 512   | 90.4 ns / 67.1 ns |  42.8 ns / 23.2 ns |  311 ns / 62.4 ns |            313 ns / 62.8 ns | 90.3 ns / 66.7 ns |         315 ns / 62.6 ns |
| std::string of 12 characters: 1 024 | 99.8 ns / 75.8 ns |  45.3 ns / 24.5 ns |  565 ns / 72.6 ns |            569 ns / 73.9 ns |  100 ns / 77.9 ns |         564 ns / 72.2 ns |
| std::string of 12 characters: 2 048 |  112 ns / 88.2 ns |  46.2 ns / 25.1 ns | 1.07 µs / 83.3 ns |           1.05 µs / 87.2 ns |  109 ns / 86.6 ns |        1.05 µs / 83.4 ns |
| std::string of 12 characters: 4 096 |  121 ns / 97.9 ns |  45.6 ns / 25.6 ns | 2.03 µs / 94.7 ns |             2.04 µs / 98 ns |  121 ns / 98.7 ns |        2.07 µs / 95.3 ns |

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

For `SmallVector` only, there is a constructor from a rvalue of a `amc::vector` that steals its dynamic storage if its capacity is larger than the inline capacity `N` (otherwise, its elements are relocated inline: a `SmallVector` which allocates always has a capacity larger than `N`).

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

Fortunately, most types are trivially relocatable. `amc::vector` itself is trivially relocatable if its allocator is (`amc::allocator` and `std::allocator` are), `SmallVector` if T and its allocator are, and `FixedCapacityVector` if T is. Types containing pointers to parts of themselves are typically not trivially relocatable, because moving them would require to update the internal pointers they hold to parts of themselves (`std::list`, `std::set`, `std::map` are for instance). `std::string` is not trivially relocatable in some implementations, but some open source equivalents are (for instance, [folly::fbstring](https://github.com/facebook/folly/blob/master/folly/FBString.h)). More information [here](https://quuxplusone.github.io/blog/2019/02/20/p1144-what-types-are-relocatable/).

The most convenient way to mark a type as trivially relocatable is to declare in the public part of the class:

`using trivially_relocatable = std::true_type;`

This is only necessary for non trivially copyable types, because trivially copyable types are trivially relocatable by default.
This also applies to allocators: a custom allocator which is not trivially copyable (for instance with a user-provided copy constructor) makes the vectors using it not trivially relocatable, unless it is marked as such. `amc::BasicAllocatorWrapper` is trivially relocatable if its basic allocator is.

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
Lookups (`find`, `lower_bound`, `insert`...) of scalar types ordered by `std::less` or `std::greater` use a branchless binary search, which prefetches its next steps in large sets (see [Sets](#sets)).
`merge` of trivially relocatable types stored in the amc vectors is linear: the elements are relocated from the ends of both sets to the storage of the destination set, instead of being inserted one by one.
The construction from a range, the construction from and the assignment of a vector (see [For FlatSet](#for-flatset)), and the insertion of a range only sort the new elements if they are not already sorted: the check is linear, and stops at the first pair out of order on unsorted input. Constructing a `FlatSet<uint32_t>` from 1 000 000 sorted values is about 10 times faster than sorting them.
If the new elements (at least 32) start with a sorted prefix of at least two thirds of them, only the other ones are sorted, then merged with the prefix: from 1 000 to 1 000 000 `uint32_t` values whose first 90 % are sorted, the construction is 4 to 7 times faster.
The insertion of a range then merges the new elements only with the present elements which are not less than the smallest new one: inserting `k` elements greater than all the `n` present ones (like increasing keys) costs `O(log(n) + k log(k))`, and `O(log(n) + k)` if they are sorted, independently of the size of the set.

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

Like the node handles of `std::set`, the `node_type` of `FlatSet` and `SmallSet` only depends on the value and allocator types: `extract` and `insert` move elements between sets of different comparators and underlying containers, from a `FlatSet` to a `SmallSet` and back.

##### Differences with std::set

By design, `SmallSet` does not offer all the guarantees of `std::set`:

 - **Order**: in the small state, the elements are iterated in insertion order, not sorted. `*begin()` is not the smallest element, and the algorithms requiring sorted ranges (`std::includes`, `std::set_union`, `std::set_intersection`, `std::merge`...) must not be used. The order changes when the set grows (the elements are sorted from then on) and when it becomes small again. The comparison operators sort the elements: they give the same results as the ones of `std::set`.
 - **No `lower_bound`, `upper_bound` nor `equal_range`**: they would require sorted elements.
 - **Iterator invalidation**: iterators, references and pointers to the elements are invalidated like the ones of a vector in the small state, and like the ones of the set type in the large state (with `FlatSet`, any insertion or erasure may invalidate them). Unlike with `std::set`, `end()` is not stable: it should not be kept to be compared later with the result of a `find`, or in an erasure loop.

| Operation                                                              | Invalidated iterators                                                                                              |
| ---------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------ |
| Insertion in the small state (`insert`, `emplace`, `merge`...)         | `end()`                                                                                                            |
| Insertion making the set grow beyond `N` elements                      | All of them, including `end()` and the ones to pass as hints: the elements are moved to the set                    |
| Erasure in the small state (`erase`, `extract`, `clear`, `merge` from) | The ones to the erased elements and to the following ones, which are shifted, and `end()`                          |
| Erasure emptying a large set (`erase`, `extract`, `clear`)             | `end()`: the set becomes small again                                                                               |
| `swap` of a set in the small state                                     | The ones to its elements: they are exchanged between the inline storages, the iterators do not follow them         |

For instance, this loop, valid with a `std::set`, must compare `it` with `s.end()` at each step with a `SmallSet`:

```cpp
for (auto it = s.begin(), last = s.end(); it != last;) {  // 'last' may be invalidated by 'erase'
  it = pred(*it) ? s.erase(it) : std::next(it);
}
```