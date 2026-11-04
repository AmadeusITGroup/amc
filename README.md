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

`amc::vector` doubles its capacity while it takes fewer than 4 KiB, like libstdc++ and libc++, as reallocating a small block costs more than the memory left unused. Beyond, it grows its capacity by a factor of 1.5, like the MSVC standard library, to save memory: a large vector reallocates about 1.7 times more often, and moves each element about twice instead of once on average. For trivially relocatable types, `amc::vector` grows with `realloc`, which can extend large blocks without copying them: growing a `uint32_t` vector to 1 million elements is 2.2 times faster than with `std::vector` despite the smaller factor. Blocks of up to 1 KiB are copied to a new block instead, as `realloc` can seldom extend them in place and bypasses the fast paths of `malloc` and `free` for small blocks. The smaller factor costs for large vectors of types which are not trivially relocatable. Growing a vector of the complex types is not shown: its time is dominated by the allocations of the elements themselves.

These allocations also dominate inserting at the end and erasing at the front, where `amc::vector` is slower than `std::vector` for the complex types despite the `memmove`. Each iteration allocates and frees about 2 400 element buffers, and glibc returns the free memory at the top of the heap to the system once it exceeds 128 KiB (`M_TRIM_THRESHOLD`): the next allocations take page faults to get it back. Whether this happens depends on the exact sequence of allocations. Before inserting copies of a value, libstdc++ copies it into a temporary, which `amc::vector` does not need. This one extra block per iteration changes which freed blocks glibc keeps in its thread cache, where they are never merged, and one of them then usually sits right below the top of the heap, preventing the trimming: `amc::vector` takes about 34 page faults per iteration, `std::vector` about 1. With the thread cache disabled, `std::vector` takes as many page faults as `amc::vector`. With the trimming disabled instead (`GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824`), `amc::vector` is 1.2 times faster than `std::vector` for the trivially relocatable type, and as fast for the other one.

#### SmallVector

![amc::SmallVector vs std::vector](./docs/bench_smallvector.svg)

`amc::SmallVector<T, N>` stores up to `N` elements in the object itself: a vector which stays small never allocates. In this benchmark, a vector is filled by `emplace_back`, read and destroyed, a given share of the vectors being small (up to the inline capacity of 16 elements) and the others large (17 to 64 elements, which move to the heap). The more vectors stay small, the bigger the gain: up to 3.2 to 3.9 times for `uint32_t`, and 1.5 to 1.6 times for the complex types, whose elements allocate their own buffer. Even when all the vectors outgrow the inline storage, `SmallVector` is still 1.1 to 1.2 times faster than `std::vector` for the complex types, including the one which is not trivially relocatable.

`amc::vector` is faster than `std::vector` here: 1.1 to 1.4 times for `uint32_t`, 1.1 to 1.2 times for the trivially relocatable complex type, and 1.02 to 1.16 times for the other one.

#### Sets

![amc::FlatSet vs std::set and std::unordered_set](./docs/bench_sets.svg)

`amc::FlatSet` is a sorted vector: inserting or erasing an element shifts the following ones. This is fast for small sets of trivially relocatable types, but its cost grows linearly with the size of the set: erasing all the elements of a `uint32_t` set in random order is competitive with `std::set` up to 10 000 elements, and about 4 times slower with 100 000. For types which are not trivially relocatable, the shifts move the elements one by one, and `std::set` inserts and erases faster.

![uint32_t set lookups by number of elements](./docs/bench_set_lookups.svg)

Lookup time mostly depends on where the set fits in the cache hierarchy: per element, `amc::FlatSet<uint32_t>` takes 4 bytes, `std::set` and `std::unordered_set` about 40 to 50 bytes (node, allocation overhead and buckets). Values are pseudo random, as consecutive integers never collide with the identity hash of libstdc++.

 - **independent** lookups: the CPU overlaps the cache misses of successive lookups, so this is a throughput, which can be shorter than a memory access,
 - **chained** lookups: each looked up value depends on the element found by the previous lookup, so this is the latency of a single lookup.

 - With 1 000 000 elements, `amc::FlatSet` (4 MB) still fits in the L3 cache while the node based sets (40 to 50 MB) do not: its lookup latency is 7.7 times lower than the one of `std::unordered_set`, with both compilers.
 - The binary searches of `amc::FlatSet<uint32_t>` are branchless with both compilers: the next half of the range is selected with a conditional move, which, unlike a branch, is never mispredicted. The address of the next element to compare then depends on the result of the current comparison, so in sets larger than the L1 cache, each step prefetches the 4 elements which the two following steps may compare: the cache misses of successive steps overlap. This applies to scalar types (arithmetic, enumerations, pointers) ordered by `std::less` or `std::greater`. With costlier comparisons, like those of `std::string` or of a comparator dereferencing pointers, the branchless search is slower, so `amc::FlatSet` keeps `std::lower_bound` for them.
 - GCC compiles the tree search of `std::set` with branches, Clang with conditional moves. Mispredicted branches serialize the lookups, so with GCC independent lookups of `std::set` are barely faster than chained ones.

#### SmallSet

![amc::SmallSet vs std::set](./docs/bench_smallset.svg)

`amc::SmallSet<T, N>` keeps up to `N` elements unsorted in an inline vector: an insertion is a linear search followed by a `push_back`, without allocation. Beyond `N` elements, it moves them to its set type, `std::set` by default. The charts compare `N = 16` to the other sets (`amc::FlatSet` uses an `amc::vector`), with elements which are cheap to compare and trivially relocatable (`uint32_t`), or not (short `std::string`):

 - **`uint32_t`, mostly small sets**: filling a `SmallSet` of up to 16 elements costs 1.5 to 3 ns per element, 6 to 10 times less than a `std::set` and 4.5 to 7 times less than an `amc::FlatSet`. As soon as some sets are large though, those of the default `SmallSet<T, 16>` allocate a `std::set` node per element: with 10 % of large sets, it is already 2.2 to 2.4 times slower than a `SmallSet` whose set type is an `amc::FlatSet`.
 - **`SmallSet<T, N, std::less<T>, amc::allocator<T>, amc::FlatSet<T>>`** is the fastest of these sets of `uint32_t` from 50 % of small sets (within 6 % of the default `SmallSet` when all of them are small): 6.4 to 6.5 times faster than `std::set` when all the sets are small, and still 2.9 to 3.6 times when they all are large. Its large state is within 10 % of an `amc::FlatSet`, for fills and lookups.
 - **Lookups** are not what `SmallSet` speeds up the most: up to 16 elements, the linear search of the small state is up to 1.8 times faster than the one of a `std::set` with GCC, but the branchless binary search of an `amc::FlatSet` is about twice faster from 4 elements. With Clang, the three of them are within 1.5 times of each other.
 - **`std::string`**: the linear search compares the looked up value with each element (once or twice), so at 16 elements, a lookup takes 1.4 (Clang) to 2 (GCC) times as long as in a `std::set`, and `SmallSet` is 1.5 times slower than `std::set` even when all the sets are small. Only the sets of at most 8 elements are faster to fill. `std::string` is not trivially relocatable either: an `amc::FlatSet` moves its elements one by one at each insertion, so the fill of a large `SmallSet` with an `amc::FlatSet` is 2 to 2.6 times slower than with a `std::set` at 256 elements, and 16 to 21 times at 4096. `std::unordered_set` is the fastest set of strings, except to fill the smallest ones.

![Sets by number of elements](./docs/bench_smallset_sizes.svg)

`SmallSet` with an `amc::FlatSet` is thus the set of choice for sets of integers, enumerations, pointers or small trivially relocatable types which are often built, most of them with at most `N` elements: like `SmallVector`, choose `N` to cover most of them. Keep `std::set` (or `std::unordered_set` when the order of the elements does not matter) for strings and other types with costly comparisons.

#### Benchmarks

The charts are drawn by [benchmark/plot_benchmarks.py](benchmark/plot_benchmarks.py) from the results stored in [docs/benchmarks](docs/benchmarks): each benchmark in its own process, median of 5 repetitions, pinned to a single core of an idle machine, with libstdc++ 13. Beyond the L3 cache, results vary by about 15 % between runs. The numbers of both compilers:

<!-- BEGIN benchmark tables, generated by benchmark/plot_benchmarks.py -->

<details><summary>Vectors, GCC 13</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.85 µs |     6.83 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      296 ns |      282 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     48.5 µs |     61.8 µs | 1.3× slower |
| Complex trivially relocatable type: Insert N copies                   |     2.15 µs |      211 ns |  10× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     20.7 µs |     14.5 µs | 1.4× faster |
| Complex trivially relocatable type: Insert a std::set range           |     27.9 µs |     19.5 µs | 1.4× faster |
| uint32_t: Assign a count or a range                                   |     12.2 ns |       14 ns | 1.1× slower |
| uint32_t: Construct and swap                                          |     9.98 ns |     9.24 ns | 1.1× faster |
| uint32_t: Insert at end, erase at front                               |      221 ns |      211 ns | 1.1× faster |
| uint32_t: Insert N copies                                             |     27.9 ns |     30.3 ns | 1.1× slower |
| uint32_t: Insert a range of pointers                                  |       88 ns |     80.7 ns | 1.1× faster |
| uint32_t: Insert a std::set range                                     |     4.33 µs |     4.39 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     2.35 ms |     1.06 ms | 2.2× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.77 µs |     6.86 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      286 ns |      279 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     50.4 µs |     63.8 µs | 1.3× slower |
| Complex non trivially relocatable type: Insert N copies               |     2.35 µs |     2.17 µs | 1.1× faster |
| Complex non trivially relocatable type: Insert a range of pointers    |     20.1 µs |     18.8 µs | 1.1× faster |
| Complex non trivially relocatable type: Insert a std::set range       |     28.5 µs |     25.7 µs | 1.1× faster |

</details>

<details><summary>Vectors, Clang 23</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |      6.7 µs |     6.88 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      264 ns |      262 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     47.7 µs |     58.7 µs | 1.2× slower |
| Complex trivially relocatable type: Insert N copies                   |     1.83 µs |      170 ns |  11× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     17.7 µs |     14.4 µs | 1.2× faster |
| Complex trivially relocatable type: Insert a std::set range           |     27.6 µs |     18.7 µs | 1.5× faster |
| uint32_t: Assign a count or a range                                   |     11.6 ns |     9.87 ns | 1.2× faster |
| uint32_t: Construct and swap                                          |     8.24 ns |        9 ns | 1.1× slower |
| uint32_t: Insert at end, erase at front                               |      220 ns |      209 ns | 1.1× faster |
| uint32_t: Insert N copies                                             |     30.4 ns |     28.2 ns | 1.1× faster |
| uint32_t: Insert a range of pointers                                  |     84.4 ns |       79 ns | 1.1× faster |
| uint32_t: Insert a std::set range                                     |     4.38 µs |     4.35 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     2.24 ms |     1.03 ms | 2.2× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.51 µs |     6.65 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      266 ns |      266 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     47.2 µs |     65.7 µs | 1.4× slower |
| Complex non trivially relocatable type: Insert N copies               |     1.51 µs |     1.71 µs | 1.1× slower |
| Complex non trivially relocatable type: Insert a range of pointers    |     18.1 µs |     19.2 µs | 1.1× slower |
| Complex non trivially relocatable type: Insert a std::set range       |     27.9 µs |     24.9 µs | 1.1× faster |

</details>

<details><summary>SmallVector, GCC 13</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      379 ns |      339 ns |                  314 ns |                1.2× faster |
| Complex trivially relocatable type: 50 % small     |      243 ns |      220 ns |                  193 ns |                1.3× faster |
| Complex trivially relocatable type: 80 % small     |      157 ns |      138 ns |                  114 ns |                1.4× faster |
| Complex trivially relocatable type: 90 % small     |      129 ns |      112 ns |                 90.3 ns |                1.4× faster |
| Complex trivially relocatable type: 95 % small     |      113 ns |     98.1 ns |                 75.9 ns |                1.5× faster |
| Complex trivially relocatable type: 99 % small     |      101 ns |       87 ns |                 66.1 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     98.1 ns |     87.9 ns |                 61.9 ns |                1.6× faster |
| uint32_t: All large                                |     76.4 ns |     54.6 ns |                 35.6 ns |                2.1× faster |
| uint32_t: 50 % small                               |     63.8 ns |     47.7 ns |                 27.9 ns |                2.3× faster |
| uint32_t: 80 % small                               |     51.9 ns |     37.8 ns |                 17.6 ns |                2.9× faster |
| uint32_t: 90 % small                               |     47.4 ns |     34.6 ns |                 14.2 ns |                3.3× faster |
| uint32_t: 95 % small                               |     45.2 ns |     32.9 ns |                 12.5 ns |                3.6× faster |
| uint32_t: 99 % small                               |     43.5 ns |     31.6 ns |                 11.2 ns |                3.9× faster |
| uint32_t: All small                                |     42.9 ns |     30.9 ns |                 10.9 ns |                3.9× faster |
| Complex non trivially relocatable type: All large  |      376 ns |      353 ns |                  325 ns |                1.2× faster |
| Complex non trivially relocatable type: 50 % small |      243 ns |      225 ns |                  199 ns |                1.2× faster |
| Complex non trivially relocatable type: 80 % small |      158 ns |      144 ns |                  118 ns |                1.3× faster |
| Complex non trivially relocatable type: 90 % small |      129 ns |      115 ns |                 91.1 ns |                1.4× faster |
| Complex non trivially relocatable type: 95 % small |      112 ns |      101 ns |                 76.9 ns |                1.5× faster |
| Complex non trivially relocatable type: 99 % small |      103 ns |     89.5 ns |                 65.3 ns |                1.6× faster |
| Complex non trivially relocatable type: All small  |       99 ns |     85.9 ns |                 63.9 ns |                1.6× faster |

</details>

<details><summary>SmallVector, Clang 23</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      372 ns |      333 ns |                  327 ns |                1.1× faster |
| Complex trivially relocatable type: 50 % small     |      239 ns |      212 ns |                  200 ns |                1.2× faster |
| Complex trivially relocatable type: 80 % small     |      154 ns |      138 ns |                  119 ns |                1.3× faster |
| Complex trivially relocatable type: 90 % small     |      127 ns |      109 ns |                 92.6 ns |                1.4× faster |
| Complex trivially relocatable type: 95 % small     |      113 ns |     95.9 ns |                 78.7 ns |                1.4× faster |
| Complex trivially relocatable type: 99 % small     |      103 ns |     86.4 ns |                 66.7 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     96.9 ns |     86.7 ns |                 63.6 ns |                1.5× faster |
| uint32_t: All large                                |     63.8 ns |     54.4 ns |                 38.7 ns |                1.6× faster |
| uint32_t: 50 % small                               |     56.1 ns |       49 ns |                   30 ns |                1.9× faster |
| uint32_t: 80 % small                               |     45.5 ns |     38.1 ns |                 19.4 ns |                2.3× faster |
| uint32_t: 90 % small                               |     41.9 ns |     34.9 ns |                 15.7 ns |                2.7× faster |
| uint32_t: 95 % small                               |     39.6 ns |     33.4 ns |                 13.9 ns |                2.9× faster |
| uint32_t: 99 % small                               |     38.1 ns |     31.9 ns |                 12.3 ns |                3.1× faster |
| uint32_t: All small                                |     38.1 ns |     31.6 ns |                 11.8 ns |                3.2× faster |
| Complex non trivially relocatable type: All large  |      371 ns |      365 ns |                  341 ns |                1.1× faster |
| Complex non trivially relocatable type: 50 % small |      239 ns |      235 ns |                  209 ns |                1.1× faster |
| Complex non trivially relocatable type: 80 % small |      153 ns |      149 ns |                  124 ns |                1.2× faster |
| Complex non trivially relocatable type: 90 % small |      124 ns |      121 ns |                 94.5 ns |                1.3× faster |
| Complex non trivially relocatable type: 95 % small |      112 ns |      106 ns |                 80.5 ns |                1.4× faster |
| Complex non trivially relocatable type: 99 % small |     98.3 ns |       94 ns |                   67 ns |                1.5× faster |
| Complex non trivially relocatable type: All small  |     95.5 ns |     90.7 ns |                 63.4 ns |                1.5× faster |

</details>

<details><summary>Sets, GCC 13</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.51 µs |                    |       3.1 µs |     1.5× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   113 µs |                    |      66.7 µs |     1.7× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |  1.39 µs |                    |       226 ns |     6.2× faster |
| uint32_t: Insert 200 random values                               |  2.89 µs |             3.5 µs |      3.16 µs |     1.1× slower |
| uint32_t: Copy, erase all of 100                                 |   1.4 µs |            1.28 µs |      1.27 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |  17.5 µs |            24.7 µs |      21.4 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   823 µs |             309 µs |       711 µs |     1.2× faster |
| uint32_t: Copy, erase all of 100 000                             |  17.5 ms |            5.27 ms |      66.7 ms |     3.8× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.61 µs |                    |      18.7 µs |     4.1× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   116 µs |                    |       390 µs |     3.4× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   375 ns |                    |      93.3 ns |     4.0× faster |

</details>

<details><summary>Sets, Clang 23</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.86 µs |                    |      2.93 µs |     1.7× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   114 µs |                    |      65.6 µs |     1.7× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |   568 ns |                    |       137 ns |     4.2× faster |
| uint32_t: Insert 200 random values                               |  3.33 µs |            2.96 µs |      3.18 µs |            same |
| uint32_t: Copy, erase all of 100                                 |   1.4 µs |            1.13 µs |      1.29 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |    18 µs |            23.9 µs |      21.5 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   847 µs |             290 µs |       713 µs |     1.2× faster |
| uint32_t: Copy, erase all of 100 000                             |  16.1 ms |            4.77 ms |      66.5 ms |     4.1× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.84 µs |                    |      20.4 µs |     4.2× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   116 µs |                    |       202 µs |     1.7× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   123 ns |                    |      77.1 ns |     1.6× faster |

</details>

<details><summary>uint32_t lookups (independent / chained), GCC 13</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        |   22.9 ns / 24 ns |    7.04 ns / 14 ns | 5.11 ns / 17.2 ns |
| 1 000      | 33.4 ns / 34.1 ns |  7.68 ns / 14.4 ns | 7.14 ns / 21.9 ns |
| 10 000     | 57.6 ns / 57.7 ns |  9.27 ns / 20.3 ns | 10.6 ns / 28.2 ns |
| 100 000    |   115 ns / 117 ns |  9.43 ns / 39.8 ns | 18.4 ns / 33.3 ns |
| 1 000 000  |   690 ns / 727 ns |   60.5 ns / 376 ns |   28.3 ns / 49 ns |
| 10 000 000 |  1.36 µs / 1.4 µs |   77.2 ns / 477 ns |   183 ns / 216 ns |

</details>

<details><summary>uint32_t lookups (independent / chained), Clang 23</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 13.7 ns / 22.6 ns |  7.37 ns / 14.1 ns | 5.02 ns / 17.2 ns |
| 1 000      | 21.8 ns / 31.1 ns |  7.95 ns / 14.5 ns | 6.89 ns / 21.9 ns |
| 10 000     |   33 ns / 52.1 ns |   9.5 ns / 20.7 ns | 9.87 ns / 28.2 ns |
| 100 000    |  59.3 ns / 107 ns |    9.62 ns / 40 ns |   16.6 ns / 33 ns |
| 1 000 000  |   282 ns / 690 ns |   60.5 ns / 372 ns | 26.9 ns / 48.5 ns |
| 10 000 000 |  654 ns / 1.37 µs |   78.2 ns / 474 ns |   158 ns / 208 ns |

</details>

<details><summary>SmallSet, GCC 13</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  3.23 µs |            1.54 µs |       872 ns |                      872 ns |         3.08 µs |                   905 ns |                       3.6× faster |
| uint32_t: 50 % small                     |  1.88 µs |             914 ns |       526 ns |                      516 ns |         1.59 µs |                   498 ns |                       3.8× faster |
| uint32_t: 80 % small                     |  1.06 µs |             535 ns |       309 ns |                      293 ns |          673 ns |                   242 ns |                       4.4× faster |
| uint32_t: 90 % small                     |   764 ns |             409 ns |       237 ns |                      219 ns |          372 ns |                   157 ns |                       4.9× faster |
| uint32_t: 95 % small                     |   613 ns |             347 ns |       199 ns |                      182 ns |          219 ns |                   115 ns |                       5.3× faster |
| uint32_t: 99 % small                     |   491 ns |             296 ns |       171 ns |                      152 ns |         97.7 ns |                  79.6 ns |                       6.2× faster |
| uint32_t: All small                      |   459 ns |             282 ns |       163 ns |                      144 ns |         66.8 ns |                  70.7 ns |                       6.5× faster |
| std::string of 12 characters: All large  |  5.64 µs |            2.41 µs |      5.51 µs |                     5.87 µs |         6.52 µs |                  6.58 µs |                       1.2× slower |
| std::string of 12 characters: 50 % small |  3.26 µs |            1.46 µs |      3.14 µs |                      3.3 µs |         3.88 µs |                   3.9 µs |                       1.2× slower |
| std::string of 12 characters: 80 % small |  1.81 µs |             880 ns |      1.71 µs |                     1.76 µs |         2.29 µs |                  2.29 µs |                       1.3× slower |
| std::string of 12 characters: 90 % small |  1.31 µs |             686 ns |      1.22 µs |                     1.23 µs |         1.74 µs |                  1.75 µs |                       1.3× slower |
| std::string of 12 characters: 95 % small |  1.05 µs |             584 ns |       969 ns |                      965 ns |         1.47 µs |                  1.48 µs |                       1.4× slower |
| std::string of 12 characters: 99 % small |   835 ns |             508 ns |       761 ns |                      752 ns |         1.25 µs |                  1.26 µs |                       1.5× slower |
| std::string of 12 characters: All small  |   776 ns |             480 ns |       707 ns |                      694 ns |         1.19 µs |                   1.2 µs |                       1.5× slower |

</details>

<details><summary>SmallSet, Clang 23</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  2.45 µs |            1.55 µs |       863 ns |                      877 ns |         2.47 µs |                   854 ns |                       2.9× faster |
| uint32_t: 50 % small                     |  1.43 µs |             924 ns |       516 ns |                      516 ns |         1.27 µs |                   463 ns |                       3.1× faster |
| uint32_t: 80 % small                     |   789 ns |             546 ns |       302 ns |                      296 ns |          542 ns |                   216 ns |                       3.7× faster |
| uint32_t: 90 % small                     |   570 ns |             419 ns |       232 ns |                      222 ns |          298 ns |                   135 ns |                       4.2× faster |
| uint32_t: 95 % small                     |   458 ns |             353 ns |       195 ns |                      184 ns |          176 ns |                  94.2 ns |                       4.9× faster |
| uint32_t: 99 % small                     |   366 ns |             302 ns |       166 ns |                      155 ns |         78.5 ns |                    61 ns |                       6.0× faster |
| uint32_t: All small                      |   341 ns |             287 ns |       159 ns |                      147 ns |         54.8 ns |                  53.4 ns |                       6.4× faster |
| std::string of 12 characters: All large  |  5.34 µs |             2.5 µs |      5.49 µs |                     5.36 µs |         6.15 µs |                  5.99 µs |                       1.1× slower |
| std::string of 12 characters: 50 % small |  3.07 µs |            1.52 µs |      3.15 µs |                     3.07 µs |         3.61 µs |                  3.52 µs |                       1.1× slower |
| std::string of 12 characters: 80 % small |  1.68 µs |             921 ns |       1.7 µs |                     1.66 µs |         2.11 µs |                  2.07 µs |                       1.2× slower |
| std::string of 12 characters: 90 % small |   1.2 µs |             712 ns |      1.21 µs |                     1.16 µs |          1.6 µs |                  1.58 µs |                       1.3× slower |
| std::string of 12 characters: 95 % small |   961 ns |             614 ns |       956 ns |                      908 ns |         1.35 µs |                  1.32 µs |                       1.4× slower |
| std::string of 12 characters: 99 % small |   769 ns |             530 ns |       744 ns |                      693 ns |         1.14 µs |                  1.13 µs |                       1.5× slower |
| std::string of 12 characters: All small  |   717 ns |             507 ns |       680 ns |                      641 ns |         1.08 µs |                  1.07 µs |                       1.5× slower |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), GCC 13</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 10.4 ns / 5.05 ns |    18 ns / 6.72 ns | 9.58 ns / 6.28 ns |           4.36 ns / 6.49 ns | 1.59 ns / 3.63 ns |         1.68 ns / 3.9 ns |
| uint32_t: 4                         | 13.9 ns / 8.28 ns |  14.5 ns / 6.74 ns | 10.8 ns / 3.48 ns |           6.89 ns / 8.03 ns |  1.5 ns / 5.87 ns |         1.6 ns / 6.09 ns |
| uint32_t: 8                         | 17.8 ns / 9.02 ns |  14.5 ns / 9.18 ns | 11.7 ns / 2.67 ns |           9.26 ns / 8.75 ns | 1.76 ns / 6.16 ns |        1.79 ns / 6.37 ns |
| uint32_t: 16                        | 25.5 ns / 12.4 ns |  20.4 ns / 12.6 ns | 13.1 ns / 3.93 ns |             11.8 ns / 10 ns | 2.61 ns / 6.73 ns |        2.68 ns / 6.99 ns |
| uint32_t: 32                        | 31.4 ns / 14.8 ns |  24.2 ns / 11.5 ns | 14.6 ns / 3.88 ns |           14.1 ns / 10.8 ns | 32.3 ns / 14.6 ns |        13.9 ns / 3.94 ns |
| uint32_t: 64                        |   35.1 ns / 18 ns |    25.2 ns / 12 ns | 16.3 ns / 4.86 ns |             16 ns / 11.8 ns |   36.4 ns / 18 ns |         15.9 ns / 4.4 ns |
| uint32_t: 128                       | 37.6 ns / 21.3 ns |  25.9 ns / 11.8 ns | 18.3 ns / 5.44 ns |           17.9 ns / 12.8 ns | 39.3 ns / 21.2 ns |          18 ns / 5.04 ns |
| uint32_t: 256                       | 39.8 ns / 24.7 ns |  27.7 ns / 13.9 ns | 20.3 ns / 5.53 ns |           20.2 ns / 13.6 ns |   41.4 ns / 24 ns |        20.2 ns / 5.54 ns |
| uint32_t: 512                       | 41.4 ns / 28.3 ns |    30.9 ns / 14 ns | 22.9 ns / 6.35 ns |           22.9 ns / 14.8 ns | 43.2 ns / 27.4 ns |        22.7 ns / 6.37 ns |
| uint32_t: 1 024                     | 43.3 ns / 33.3 ns |  32.3 ns / 14.3 ns | 26.4 ns / 7.08 ns |           26.4 ns / 15.7 ns | 45.3 ns / 32.7 ns |        26.4 ns / 6.95 ns |
| uint32_t: 2 048                     | 47.3 ns / 40.9 ns |      33 ns / 15 ns | 31.9 ns / 7.73 ns |           32.1 ns / 16.8 ns | 49.6 ns / 40.8 ns |        31.9 ns / 7.66 ns |
| uint32_t: 4 096                     | 51.1 ns / 47.7 ns |    33.4 ns / 15 ns | 41.8 ns / 8.52 ns |           41.9 ns / 18.1 ns | 53.4 ns / 48.9 ns |        41.7 ns / 8.41 ns |
| std::string of 12 characters: 2     | 23.7 ns / 10.7 ns |   24.3 ns / 8.7 ns |     16 ns / 12 ns |             11.1 ns / 12 ns | 9.71 ns / 15.6 ns |        10.2 ns / 16.8 ns |
| std::string of 12 characters: 4     |   25.3 ns / 18 ns |    21.2 ns / 10 ns | 20.1 ns / 17.9 ns |           16.4 ns / 18.1 ns | 15.4 ns / 21.4 ns |        16.3 ns / 21.1 ns |
| std::string of 12 characters: 8     | 29.9 ns / 20.8 ns |    22.1 ns / 13 ns | 25.1 ns / 21.3 ns |           22.7 ns / 21.2 ns | 22.3 ns / 31.6 ns |        24.3 ns / 29.6 ns |
| std::string of 12 characters: 16    | 40.9 ns / 26.3 ns |  32.1 ns / 20.1 ns | 33.2 ns / 28.7 ns |           32.7 ns / 28.3 ns | 47.9 ns / 52.7 ns |        50.2 ns / 53.1 ns |
| std::string of 12 characters: 32    | 51.2 ns / 30.6 ns |  37.1 ns / 17.5 ns | 45.3 ns / 33.2 ns |             47 ns / 33.3 ns | 80.9 ns / 32.6 ns |          74.6 ns / 33 ns |
| std::string of 12 characters: 64    | 59.7 ns / 37.7 ns |  35.9 ns / 16.6 ns | 61.2 ns / 40.3 ns |           66.8 ns / 40.1 ns | 75.4 ns / 38.8 ns |        91.2 ns / 40.4 ns |
| std::string of 12 characters: 128   | 66.9 ns / 44.6 ns |  35.8 ns / 17.1 ns | 84.3 ns / 47.6 ns |             98.4 ns / 47 ns | 76.5 ns / 46.1 ns |         129 ns / 47.8 ns |
| std::string of 12 characters: 256   | 73.2 ns / 50.7 ns |  36.1 ns / 21.2 ns |  124 ns / 55.2 ns |            151 ns / 54.8 ns | 80.2 ns / 52.7 ns |         205 ns / 55.3 ns |
| std::string of 12 characters: 512   | 80.1 ns / 58.9 ns |  39.2 ns / 21.5 ns |  194 ns / 61.6 ns |            248 ns / 61.2 ns |   85.9 ns / 61 ns |         353 ns / 62.5 ns |
| std::string of 12 characters: 1 024 |   89 ns / 69.9 ns |  41.4 ns / 22.6 ns |  327 ns / 70.3 ns |            434 ns / 70.2 ns | 93.5 ns / 69.7 ns |           638 ns / 71 ns |
| std::string of 12 characters: 2 048 | 98.7 ns / 79.8 ns |    41.6 ns / 23 ns |  616 ns / 81.3 ns |            796 ns / 81.9 ns |  102 ns / 79.3 ns |         1.2 µs / 81.6 ns |
| std::string of 12 characters: 4 096 |  108 ns / 91.2 ns |  41.9 ns / 23.6 ns |   1.12 µs / 95 ns |           1.52 µs / 95.2 ns |  111 ns / 92.1 ns |        2.34 µs / 95.7 ns |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), Clang 23</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 10.6 ns / 6.55 ns |  17.8 ns / 6.73 ns | 9.45 ns / 6.38 ns |           6.73 ns / 6.63 ns |  1.84 ns / 6.4 ns |        1.67 ns / 6.35 ns |
| uint32_t: 4                         |  14.3 ns / 5.3 ns |  14.5 ns / 7.16 ns | 10.6 ns / 8.07 ns |           8.95 ns / 8.19 ns | 1.73 ns / 6.45 ns |         1.58 ns / 6.5 ns |
| uint32_t: 8                         | 18.2 ns / 6.57 ns |  14.2 ns / 10.2 ns | 11.5 ns / 8.63 ns |           10.4 ns / 8.87 ns | 1.97 ns / 6.76 ns |        1.91 ns / 6.79 ns |
| uint32_t: 16                        | 26.9 ns / 10.4 ns |  20.4 ns / 13.1 ns |   12.9 ns / 10 ns |             12.3 ns / 10 ns |  2.9 ns / 7.36 ns |        2.57 ns / 7.46 ns |
| uint32_t: 32                        | 33.6 ns / 12.2 ns |  24.2 ns / 11.9 ns | 14.3 ns / 10.9 ns |           14.6 ns / 10.9 ns | 33.4 ns / 13.2 ns |        13.8 ns / 11.6 ns |
| uint32_t: 64                        | 38.7 ns / 12.2 ns |    25 ns / 12.7 ns | 15.9 ns / 11.8 ns |           16.2 ns / 11.9 ns | 37.9 ns / 12.7 ns |        15.6 ns / 12.6 ns |
| uint32_t: 128                       | 42.5 ns / 13.3 ns |  25.7 ns / 12.3 ns | 17.5 ns / 12.8 ns |           17.9 ns / 12.9 ns |   41.7 ns / 14 ns |        17.4 ns / 13.6 ns |
| uint32_t: 256                       | 45.8 ns / 16.2 ns |  27.5 ns / 14.4 ns |   20 ns / 13.6 ns |           20.2 ns / 13.8 ns | 44.8 ns / 16.8 ns |          20 ns / 14.7 ns |
| uint32_t: 512                       | 48.4 ns / 19.9 ns |  30.7 ns / 14.3 ns |   22.4 ns / 15 ns |           22.8 ns / 14.9 ns | 47.8 ns / 20.5 ns |        22.2 ns / 15.7 ns |
| uint32_t: 1 024                     | 51.3 ns / 21.9 ns |  32.2 ns / 14.5 ns |   26 ns / 15.9 ns |             26.4 ns / 16 ns | 50.6 ns / 22.1 ns |        25.9 ns / 16.7 ns |
| uint32_t: 2 048                     |   56 ns / 25.5 ns |    33 ns / 15.3 ns | 31.7 ns / 16.8 ns |             32 ns / 17.1 ns | 55.5 ns / 25.8 ns |          31.4 ns / 18 ns |
| uint32_t: 4 096                     | 60.7 ns / 29.2 ns |  33.3 ns / 15.5 ns | 41.6 ns / 18.2 ns |             42 ns / 18.4 ns | 60.3 ns / 29.7 ns |        41.2 ns / 19.1 ns |
| std::string of 12 characters: 2     | 16.9 ns / 12.6 ns |   24.9 ns / 9.8 ns | 16.5 ns / 11.2 ns |            9.9 ns / 11.5 ns | 9.68 ns / 13.3 ns |         9.5 ns / 12.2 ns |
| std::string of 12 characters: 4     | 22.6 ns / 18.7 ns |    22.1 ns / 11 ns | 20.9 ns / 16.6 ns |           15.3 ns / 17.1 ns |   14 ns / 19.6 ns |            14 ns / 19 ns |
| std::string of 12 characters: 8     | 30.2 ns / 24.7 ns |    22.9 ns / 14 ns | 25.3 ns / 19.3 ns |           21.6 ns / 19.7 ns | 20.8 ns / 26.6 ns |        20.6 ns / 25.9 ns |
| std::string of 12 characters: 16    | 43.1 ns / 31.1 ns |    32.9 ns / 21 ns | 34.1 ns / 26.9 ns |           32.2 ns / 27.2 ns | 44.6 ns / 43.8 ns |        45.1 ns / 42.5 ns |
| std::string of 12 characters: 32    |   54.1 ns / 38 ns |    38 ns / 18.1 ns |   49 ns / 31.3 ns |           48.7 ns / 31.7 ns | 77.5 ns / 37.9 ns |        66.1 ns / 31.7 ns |
| std::string of 12 characters: 64    | 63.5 ns / 43.5 ns |  37.1 ns / 17.1 ns |   69.9 ns / 38 ns |           72.2 ns / 38.7 ns | 73.9 ns / 43.3 ns |        78.3 ns / 38.7 ns |
| std::string of 12 characters: 128   | 71.8 ns / 49.6 ns |  37.1 ns / 17.7 ns |  104 ns / 45.5 ns |            109 ns / 46.1 ns | 76.1 ns / 49.3 ns |         108 ns / 45.5 ns |
| std::string of 12 characters: 256   | 79.3 ns / 57.1 ns |    37.2 ns / 22 ns |  165 ns / 52.1 ns |            176 ns / 53.3 ns | 80.9 ns / 57.1 ns |         167 ns / 52.6 ns |
| std::string of 12 characters: 512   |   87 ns / 66.2 ns |  40.2 ns / 22.4 ns |  280 ns / 59.1 ns |            300 ns / 59.3 ns | 86.7 ns / 66.4 ns |         282 ns / 59.9 ns |
| std::string of 12 characters: 1 024 | 96.8 ns / 75.6 ns |  42.6 ns / 23.6 ns |    504 ns / 70 ns |            537 ns / 69.3 ns | 95.1 ns / 75.7 ns |         503 ns / 69.8 ns |
| std::string of 12 characters: 2 048 |  107 ns / 85.6 ns |  42.8 ns / 23.9 ns |  942 ns / 80.5 ns |           1.02 µs / 80.5 ns |  105 ns / 85.9 ns |         939 ns / 80.6 ns |
| std::string of 12 characters: 4 096 |  118 ns / 98.6 ns |  43.1 ns / 24.6 ns | 1.82 µs / 91.7 ns |           1.96 µs / 91.5 ns |    116 ns / 98 ns |        1.82 µs / 92.3 ns |

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