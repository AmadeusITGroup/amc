# Benchmarks

How the amc containers compare to the standard ones, as measured by the benchmarks of [benchmark/](../benchmark). The gains come with trade-offs, which are shown as well.

- [Method](#method)
- [Vectors](#vectors)
- [SmallVector](#smallvector)
- [FlatSet](#flatset)
- [SmallSet](#smallset)
- [Numbers](#numbers)

## Method

The charts are drawn by [benchmark/plot_benchmarks.py](../benchmark/plot_benchmarks.py) from the results stored in [docs/benchmarks](benchmarks). Each benchmark runs in its own process, pinned to a single core of an idle machine, and the median of 5 repetitions is kept. The standard library is libstdc++ 13. The `SmallSet` benchmarks are built in C++20 (`-DCMAKE_CXX_STANDARD=20`), which its single comparison per element requires (see [SmallSet](#smallset)), and the other ones in C++17. Beyond the L3 cache, results vary by about 15 % between runs.

The script docstring explains how to run the benchmarks and refresh the charts and the [tables](#numbers) of this page.

The "complex" types are 16 byte structures owning a dynamically allocated buffer: copying one allocates, and moving one steals the buffer. The first one is declared trivially relocatable, the second one is not.

## Vectors

![amc::vector vs std::vector](bench_vectors.svg)

The gains come from trivially relocatable types, whose elements are shifted with a single `memmove` instead of one move per element. `std::vector` already does this for trivially copyable types like `uint32_t`. For types which are not trivially relocatable, `amc::vector` uses the same algorithms as `std::vector`.

`amc::vector` doubles its capacity while it takes fewer than 4 KiB, like libstdc++ and libc++, as reallocating a small block costs more than the memory left unused. Beyond that, it grows its capacity by a factor of 1.5, like the MSVC standard library, to save memory: a large vector reallocates about 1.7 times more often, and moves each element about twice instead of once on average. For trivially relocatable types, `amc::vector` grows with `realloc`, which can extend large blocks without copying them: growing a `uint32_t` vector to 1 million elements is 2.2 times faster than with `std::vector` despite the smaller factor. Blocks of up to 1 KiB are copied to a new block instead, as `realloc` can seldom extend them in place and bypasses the fast paths of `malloc` and `free` for small blocks. The smaller factor costs time for large vectors of types which are not trivially relocatable. Growing a vector of the complex types is not shown: its time is dominated by the allocations of the elements themselves.

These allocations also dominate inserting at the end and erasing at the front, where `amc::vector` is slower than `std::vector` for the complex types despite the `memmove`. Each iteration allocates and frees about 2 400 element buffers, and glibc returns the free memory at the top of the heap to the system once it exceeds 128 KiB (`M_TRIM_THRESHOLD`): the next allocations take page faults to get it back. Whether this happens depends on the exact sequence of allocations. Before inserting copies of a value, libstdc++ copies it into a temporary, which `amc::vector` does not need. This one extra block per iteration changes which freed blocks glibc keeps in its thread cache, where they are never merged, and one of them then usually sits right below the top of the heap, preventing the trimming: `amc::vector` takes about 34 page faults per iteration, `std::vector` about 1. With the thread cache disabled, `std::vector` takes as many page faults as `amc::vector`. With the trimming disabled instead (`GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824`), `amc::vector` is 1.2 times faster than `std::vector` for the trivially relocatable type, and as fast for the other one.

## SmallVector

![amc::SmallVector vs std::vector](bench_smallvector.svg)

`amc::SmallVector<T, N>` stores up to `N` elements in the object itself: a vector which stays small never allocates. In this benchmark, a vector is filled by `emplace_back`, read and destroyed, a given share of the vectors being small (up to the inline capacity of 16 elements) and the others large (17 to 64 elements, which move to the heap). The more vectors stay small, the bigger the gain: up to 3.2 to 3.9 times for `uint32_t`, and 1.5 to 1.6 times for the complex types, whose elements allocate their own buffer. Even when all the vectors outgrow the inline storage, `SmallVector` is still 1.1 to 1.2 times faster than `std::vector` for the complex types, including the one which is not trivially relocatable.

`amc::vector` is faster than `std::vector` here too: 1.1 to 1.4 times for `uint32_t`, 1.1 to 1.2 times for the trivially relocatable complex type, and 1.02 to 1.16 times for the other one.

## FlatSet

![amc::FlatSet vs std::set and std::unordered_set](bench_sets.svg)

`amc::FlatSet` is a sorted vector: inserting or erasing an element shifts the following ones. This is fast for small sets of trivially relocatable types, but its cost grows linearly with the size of the set: erasing all the elements of a `uint32_t` set in random order is competitive with `std::set` up to 10 000 elements, and about 4 times slower with 100 000. For types which are not trivially relocatable, the shifts move the elements one by one, and `std::set` inserts and erases faster.

![uint32_t set lookups by number of elements](bench_set_lookups.svg)

Lookup time mostly depends on where the set fits in the cache hierarchy: per element, `amc::FlatSet<uint32_t>` takes 4 bytes, `std::set` and `std::unordered_set` about 40 to 50 bytes (node, allocation overhead and buckets). Values are pseudo random, as consecutive integers never collide with the identity hash of libstdc++.

- **Independent** lookups: the CPU overlaps the cache misses of successive lookups, so this is a throughput, which can be shorter than a memory access.
- **Chained** lookups: each looked up value depends on the element found by the previous lookup, so this is the latency of a single lookup.

Observations:

- With 1 000 000 elements, `amc::FlatSet` (4 MB) still fits in the L3 cache while the node based sets (40 to 50 MB) do not: its lookup latency is 7.7 times lower than the one of `std::unordered_set`, with both compilers.
- The binary searches of `amc::FlatSet<uint32_t>` are branchless with both compilers: the next half of the range is selected with a conditional move, which, unlike a branch, is never mispredicted. The address of the next element to compare then depends on the result of the current comparison, so in sets larger than the L1 cache, each step prefetches the 4 elements which the two following steps may compare: the cache misses of successive steps overlap. This applies to scalar types (arithmetic, enumerations, pointers) ordered by `std::less` or `std::greater`. With costlier comparisons, like those of `std::string` or of a comparator dereferencing pointers, the branchless search is slower, so `amc::FlatSet` keeps `std::lower_bound` for them.
- GCC compiles the tree search of `std::set` with branches, Clang with conditional moves. Mispredicted branches serialize the lookups, so with GCC, independent lookups of `std::set` are barely faster than chained ones.

## SmallSet

![amc::SmallSet vs std::set](bench_smallset.svg)

`amc::SmallSet<T, N>` keeps up to `N` elements unsorted in an inline vector: an insertion is a linear search followed by a `push_back`, without allocation. Beyond `N` elements, it moves them to its set type, `std::set` by default. The charts compare `N = 16` to the other sets (`amc::FlatSet` uses an `amc::vector`), with elements which are cheap to compare and trivially relocatable (`uint32_t`), or not (short `std::string`):

- **`uint32_t`, mostly small sets**: filling a `SmallSet` of up to 16 elements costs 1.5 to 3 ns per element, 6 to 10 times less than a `std::set` and 4.5 to 7 times less than an `amc::FlatSet`. As soon as some sets are large though, those of the default `SmallSet<T, 16>` allocate a `std::set` node per element: with 10 % of large sets, it is already 2.1 to 2.6 times slower than a `SmallSet` whose set type is an `amc::FlatSet`.
- **`SmallSet<T, N, std::less<T>, amc::allocator<T>, amc::FlatSet<T>>`** is the fastest of these sets of `uint32_t` from 50 % of small sets (within 12 % of the default `SmallSet` when all of them are small): 6.3 to 8.1 times faster than `std::set` when all the sets are small, and still 2.9 to 3.4 times when they all are large. Its large state is within 11 % of an `amc::FlatSet`, for fills and lookups.
- **Lookups** are not what `SmallSet` speeds up the most: up to 16 elements, the linear search of the small state is up to 2 times faster than the one of a `std::set` with GCC, but the branchless binary search of an `amc::FlatSet` is 1.5 to 2.2 times faster from 4 elements. With Clang, the three of them are within 1.5 times of each other.
- **`std::string`**: from C++20, when the comparator calls `operator<` or `operator>` (`std::less`, `std::greater`, transparent or not, or their `std::ranges` versions) and the looked up key is three-way comparable with the elements, the linear search compares it once with each element, with `operator<=>`, instead of twice with the comparator. This applies to strings, to pairs, tuples or containers of them, and to the lookup of a string view or a string literal among strings with a transparent comparator (in C++17, the comparator compares it twice with most elements, which makes these lookups about twice as slow with Clang 23). Filling a `SmallSet` of up to 16 strings costs 2.6 to 3.5 times less per element than a `std::set`, and its lookups are up to 1.5 (GCC) to 1.7 (Clang) times faster, except at 16 elements with GCC (7 % slower). `SmallSet` is faster than `std::set` from 50 % of small sets (within 8 % when they all are large), and 2.2 to 2.4 times faster when all the sets are small, then also 1.5 to 1.7 times faster than `std::unordered_set`, the fastest set of strings up to 90 % (Clang) or 95 % (GCC) of small sets. `std::string` is not trivially relocatable though: an `amc::FlatSet` moves its elements one by one at each insertion, so the fill of a large `SmallSet` with an `amc::FlatSet` is 2.1 to 2.8 times slower than with a `std::set` at 256 elements, and 16 to 22 times at 4096.

![Sets by number of elements](bench_smallset_sizes.svg)

`SmallSet` with an `amc::FlatSet` is thus the set of choice for sets of integers, enumerations, pointers or small trivially relocatable types which are often built, most of them with at most `N` elements: like for `SmallVector`, choose `N` to cover most of them. For strings, the default `SmallSet` (with a `std::set`) pays off when most sets are small. Keep `std::set` (or `std::unordered_set` when the order of the elements does not matter) for sets of strings which are mostly large, and for other types with costly comparisons.

## Numbers

The numbers behind the charts, for both compilers.

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
| uint32_t: All large                      |  3.16 µs |            1.53 µs |       891 ns |                      871 ns |         3.19 µs |                   917 ns |                       3.4× faster |
| uint32_t: 50 % small                     |  1.84 µs |             906 ns |       540 ns |                      512 ns |         1.61 µs |                   492 ns |                       3.7× faster |
| uint32_t: 80 % small                     |  1.03 µs |             732 ns |       312 ns |                      292 ns |          674 ns |                   229 ns |                       4.5× faster |
| uint32_t: 90 % small                     |   757 ns |             430 ns |       238 ns |                      218 ns |          362 ns |                   141 ns |                       5.4× faster |
| uint32_t: 95 % small                     |   611 ns |             359 ns |       201 ns |                      181 ns |          206 ns |                  97.6 ns |                       6.3× faster |
| uint32_t: 99 % small                     |   479 ns |             297 ns |       172 ns |                      152 ns |         81.3 ns |                  61.6 ns |                       7.8× faster |
| uint32_t: All small                      |   445 ns |             283 ns |       163 ns |                      144 ns |         49.5 ns |                  55.2 ns |                       8.1× faster |
| std::string of 12 characters: All large  |  5.69 µs |             2.4 µs |      5.74 µs |                     5.78 µs |         6.11 µs |                  5.94 µs |                              same |
| std::string of 12 characters: 50 % small |  3.28 µs |            1.46 µs |      3.27 µs |                      3.3 µs |         3.23 µs |                   3.1 µs |                       1.1× faster |
| std::string of 12 characters: 80 % small |  1.82 µs |             898 ns |      1.78 µs |                     1.77 µs |         1.52 µs |                  1.45 µs |                       1.3× faster |
| std::string of 12 characters: 90 % small |  1.31 µs |             685 ns |      1.25 µs |                     1.24 µs |          916 ns |                   887 ns |                       1.5× faster |
| std::string of 12 characters: 95 % small |  1.05 µs |             589 ns |       991 ns |                      964 ns |          627 ns |                   618 ns |                       1.7× faster |
| std::string of 12 characters: 99 % small |   835 ns |             520 ns |       749 ns |                      736 ns |          380 ns |                   388 ns |                       2.1× faster |
| std::string of 12 characters: All small  |   779 ns |             490 ns |       689 ns |                      682 ns |          323 ns |                   326 ns |                       2.4× faster |

</details>

<details><summary>SmallSet, Clang 23</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  2.49 µs |            1.55 µs |       870 ns |                      884 ns |         2.43 µs |                   858 ns |                       2.9× faster |
| uint32_t: 50 % small                     |  1.45 µs |             923 ns |       522 ns |                      519 ns |         1.26 µs |                   464 ns |                       3.1× faster |
| uint32_t: 80 % small                     |   799 ns |             550 ns |       309 ns |                      297 ns |          538 ns |                   219 ns |                       3.7× faster |
| uint32_t: 90 % small                     |   574 ns |             415 ns |       234 ns |                      230 ns |          296 ns |                   141 ns |                       4.1× faster |
| uint32_t: 95 % small                     |   464 ns |             350 ns |       195 ns |                      186 ns |          174 ns |                  99.8 ns |                       4.6× faster |
| uint32_t: 99 % small                     |   370 ns |             297 ns |       167 ns |                      156 ns |         77.6 ns |                  62.8 ns |                       5.9× faster |
| uint32_t: All small                      |   345 ns |             283 ns |       160 ns |                      148 ns |         53.8 ns |                  54.5 ns |                       6.3× faster |
| std::string of 12 characters: All large  |  5.29 µs |            2.48 µs |      5.25 µs |                     5.25 µs |         5.46 µs |                  5.22 µs |                              same |
| std::string of 12 characters: 50 % small |  3.04 µs |            1.51 µs |      3.05 µs |                     3.02 µs |         2.91 µs |                  2.79 µs |                       1.1× faster |
| std::string of 12 characters: 80 % small |  1.67 µs |             893 ns |      1.63 µs |                     1.63 µs |         1.34 µs |                   1.3 µs |                       1.3× faster |
| std::string of 12 characters: 90 % small |  1.19 µs |             694 ns |      1.15 µs |                     1.14 µs |          815 ns |                   823 ns |                       1.4× faster |
| std::string of 12 characters: 95 % small |   965 ns |             596 ns |       917 ns |                      886 ns |          559 ns |                   567 ns |                       1.7× faster |
| std::string of 12 characters: 99 % small |   759 ns |             513 ns |       714 ns |                      678 ns |          349 ns |                   377 ns |                       2.0× faster |
| std::string of 12 characters: All small  |   712 ns |             491 ns |       655 ns |                      618 ns |          297 ns |                   319 ns |                       2.2× faster |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), GCC 13</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 11.3 ns / 5.29 ns |     19 ns / 6.9 ns |  9.76 ns / 6.5 ns |           4.52 ns / 6.47 ns | 1.58 ns / 5.12 ns |        1.57 ns / 5.39 ns |
| uint32_t: 4                         |    14 ns / 8.4 ns |   14.8 ns / 6.9 ns | 10.8 ns / 3.53 ns |            6.97 ns / 8.3 ns | 1.55 ns / 5.35 ns |        1.53 ns / 5.32 ns |
| uint32_t: 8                         | 17.8 ns / 9.09 ns |  14.3 ns / 9.26 ns | 11.8 ns / 2.65 ns |           9.13 ns / 8.94 ns | 1.75 ns / 5.76 ns |        1.78 ns / 5.64 ns |
| uint32_t: 16                        | 25.7 ns / 12.4 ns |  20.9 ns / 12.7 ns | 13.2 ns / 3.94 ns |           11.7 ns / 10.1 ns | 2.48 ns / 6.44 ns |        2.63 ns / 6.28 ns |
| uint32_t: 32                        | 31.6 ns / 14.8 ns |  24.2 ns / 11.7 ns | 14.8 ns / 3.95 ns |           14.2 ns / 10.9 ns |   32 ns / 14.7 ns |          14 ns / 3.97 ns |
| uint32_t: 64                        | 35.3 ns / 18.1 ns |  25.1 ns / 12.2 ns | 17.7 ns / 4.77 ns |           16.3 ns / 12.3 ns | 36.8 ns / 18.1 ns |        15.9 ns / 4.41 ns |
| uint32_t: 128                       | 37.9 ns / 21.6 ns |    25.9 ns / 12 ns | 18.3 ns / 5.68 ns |             18 ns / 12.8 ns | 39.6 ns / 21.1 ns |        18.1 ns / 5.05 ns |
| uint32_t: 256                       | 40.2 ns / 24.8 ns |  27.5 ns / 14.1 ns | 20.3 ns / 5.56 ns |           20.3 ns / 13.6 ns | 41.3 ns / 24.1 ns |        20.2 ns / 5.62 ns |
| uint32_t: 512                       | 41.8 ns / 28.5 ns |  31.2 ns / 14.1 ns | 22.8 ns / 6.44 ns |             23 ns / 14.9 ns | 43.1 ns / 27.5 ns |        22.7 ns / 6.56 ns |
| uint32_t: 1 024                     | 43.5 ns / 34.2 ns |  32.4 ns / 14.4 ns | 26.4 ns / 7.35 ns |             26.6 ns / 16 ns | 45.2 ns / 32.8 ns |        26.7 ns / 7.05 ns |
| uint32_t: 2 048                     |   47.4 ns / 42 ns |    32.9 ns / 15 ns | 32.1 ns / 7.74 ns |             32.1 ns / 17 ns | 49.4 ns / 40.5 ns |        31.9 ns / 7.81 ns |
| uint32_t: 4 096                     | 51.6 ns / 49.5 ns |    33.5 ns / 15 ns |   42 ns / 8.53 ns |             42 ns / 18.3 ns | 53.1 ns / 48.6 ns |        41.7 ns / 8.45 ns |
| std::string of 12 characters: 2     | 17.3 ns / 11.4 ns |  24.3 ns / 8.54 ns | 16.1 ns / 10.7 ns |           10.1 ns / 12.1 ns | 5.82 ns / 11.7 ns |         6.2 ns / 11.8 ns |
| std::string of 12 characters: 4     | 21.7 ns / 18.9 ns |  20.7 ns / 9.95 ns | 19.9 ns / 16.4 ns |           15.3 ns / 18.3 ns | 7.86 ns / 12.5 ns |        8.32 ns / 13.8 ns |
| std::string of 12 characters: 8     | 27.6 ns / 23.4 ns |  21.5 ns / 13.3 ns |   26 ns / 19.5 ns |             21 ns / 21.7 ns | 9.46 ns / 17.8 ns |        10.3 ns / 18.1 ns |
| std::string of 12 characters: 16    | 39.4 ns / 27.4 ns |  32.3 ns / 20.9 ns | 32.8 ns / 27.4 ns |           29.9 ns / 29.4 ns | 13.7 ns / 29.4 ns |        14.7 ns / 29.1 ns |
| std::string of 12 characters: 32    | 50.3 ns / 31.8 ns |    36.9 ns / 17 ns | 45.1 ns / 31.1 ns |           42.3 ns / 33.4 ns | 59.5 ns / 33.1 ns |        54.8 ns / 34.3 ns |
| std::string of 12 characters: 64    | 59.3 ns / 39.8 ns |  36.1 ns / 16.1 ns | 62.2 ns / 38.9 ns |           58.5 ns / 40.5 ns | 65.5 ns / 39.8 ns |        82.4 ns / 41.4 ns |
| std::string of 12 characters: 128   |   66.8 ns / 45 ns |  36.6 ns / 16.6 ns | 89.1 ns / 46.3 ns |           81.7 ns / 48.3 ns | 70.8 ns / 47.2 ns |         133 ns / 49.1 ns |
| std::string of 12 characters: 256   | 72.4 ns / 51.1 ns |  36.9 ns / 20.9 ns |    137 ns / 53 ns |              121 ns / 56 ns |   77 ns / 53.1 ns |         213 ns / 56.4 ns |
| std::string of 12 characters: 512   | 78.8 ns / 59.6 ns |  39.3 ns / 21.2 ns |  221 ns / 59.2 ns |            191 ns / 62.1 ns | 84.4 ns / 61.7 ns |         361 ns / 62.8 ns |
| std::string of 12 characters: 1 024 |   88 ns / 70.9 ns |  42.1 ns / 22.5 ns |  390 ns / 70.8 ns |            318 ns / 71.3 ns | 92.8 ns / 71.1 ns |           661 ns / 71 ns |
| std::string of 12 characters: 2 048 | 97.8 ns / 80.7 ns |  42.5 ns / 22.7 ns |  701 ns / 80.7 ns |            579 ns / 84.1 ns |    101 ns / 81 ns |        1.24 µs / 83.7 ns |
| std::string of 12 characters: 4 096 |  108 ns / 92.5 ns |  42.2 ns / 23.3 ns | 1.35 µs / 90.9 ns |            1.1 µs / 96.7 ns |  111 ns / 93.6 ns |        2.43 µs / 95.1 ns |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), Clang 23</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 10.8 ns / 6.75 ns |  17.7 ns / 6.53 ns |  9.3 ns / 6.48 ns |           6.81 ns / 6.55 ns | 1.88 ns / 6.63 ns |        1.69 ns / 6.62 ns |
| uint32_t: 4                         | 14.5 ns / 5.45 ns |  14.1 ns / 7.17 ns | 10.5 ns / 8.12 ns |           8.93 ns / 8.22 ns | 1.73 ns / 6.65 ns |        1.66 ns / 6.65 ns |
| uint32_t: 8                         | 18.6 ns / 6.59 ns |  14.1 ns / 10.6 ns | 11.4 ns / 8.87 ns |            10.4 ns / 8.8 ns | 1.97 ns / 6.99 ns |        1.98 ns / 6.95 ns |
| uint32_t: 16                        | 26.9 ns / 10.7 ns |  19.9 ns / 13.1 ns | 12.8 ns / 10.1 ns |           12.3 ns / 10.2 ns |  2.76 ns / 7.9 ns |        2.63 ns / 7.63 ns |
| uint32_t: 32                        | 33.5 ns / 12.6 ns |    23.8 ns / 12 ns | 14.4 ns / 10.9 ns |             14.6 ns / 11 ns |     33 ns / 13 ns |        13.9 ns / 11.7 ns |
| uint32_t: 64                        | 38.4 ns / 12.6 ns |  24.8 ns / 12.6 ns | 15.8 ns / 11.9 ns |           16.3 ns / 11.9 ns | 37.6 ns / 12.9 ns |        15.6 ns / 12.7 ns |
| uint32_t: 128                       | 42.3 ns / 13.7 ns |  25.6 ns / 12.4 ns | 17.6 ns / 12.8 ns |             18 ns / 12.9 ns | 41.3 ns / 13.9 ns |        17.5 ns / 13.8 ns |
| uint32_t: 256                       | 45.8 ns / 16.6 ns |    27 ns / 14.4 ns | 19.9 ns / 13.8 ns |           20.2 ns / 13.7 ns |   44.3 ns / 17 ns |        19.9 ns / 14.8 ns |
| uint32_t: 512                       | 48.3 ns / 20.3 ns |  30.5 ns / 14.4 ns | 22.4 ns / 14.9 ns |           22.8 ns / 15.2 ns | 47.2 ns / 20.6 ns |        22.3 ns / 15.9 ns |
| uint32_t: 1 024                     | 51.6 ns / 22.1 ns |  31.4 ns / 14.8 ns |   26 ns / 15.9 ns |           26.4 ns / 16.1 ns | 50.5 ns / 22.5 ns |        25.8 ns / 17.6 ns |
| uint32_t: 2 048                     | 56.1 ns / 25.8 ns |  32.5 ns / 15.3 ns | 31.7 ns / 17.1 ns |               32 ns / 17 ns | 55.6 ns / 26.3 ns |        31.5 ns / 18.1 ns |
| uint32_t: 4 096                     | 60.8 ns / 29.5 ns |  33.4 ns / 15.4 ns | 41.8 ns / 18.3 ns |           42.1 ns / 18.5 ns | 60.2 ns / 30.1 ns |        41.6 ns / 19.2 ns |
| std::string of 12 characters: 2     | 16.9 ns / 12.6 ns |  24.3 ns / 9.87 ns | 15.8 ns / 11.1 ns |           9.52 ns / 11.4 ns | 5.67 ns / 10.5 ns |        5.72 ns / 10.2 ns |
| std::string of 12 characters: 4     | 22.2 ns / 18.7 ns |  21.6 ns / 11.1 ns | 20.4 ns / 16.7 ns |           14.4 ns / 16.5 ns | 7.07 ns / 12.6 ns |        7.02 ns / 11.6 ns |
| std::string of 12 characters: 8     |   30 ns / 24.7 ns |  22.6 ns / 14.4 ns | 24.7 ns / 19.2 ns |           20.2 ns / 19.2 ns |  8.7 ns / 15.4 ns |        8.79 ns / 14.6 ns |
| std::string of 12 characters: 16    |   42.4 ns / 31 ns |  32.8 ns / 34.3 ns | 33.3 ns / 26.3 ns |           30.1 ns / 26.9 ns | 13.3 ns / 20.7 ns |        13.6 ns / 21.3 ns |
| std::string of 12 characters: 32    |   53.6 ns / 38 ns |  38.2 ns / 24.7 ns | 47.2 ns / 31.1 ns |           46.6 ns / 30.7 ns | 59.3 ns / 37.7 ns |          47 ns / 30.8 ns |
| std::string of 12 characters: 64    | 63.5 ns / 43.3 ns |    37 ns / 20.9 ns |   68 ns / 38.3 ns |           68.5 ns / 37.5 ns | 65.1 ns / 43.2 ns |        68.2 ns / 38.3 ns |
| std::string of 12 characters: 128   | 71.9 ns / 49.8 ns |  36.9 ns / 17.7 ns |  102 ns / 45.2 ns |            103 ns / 44.7 ns | 72.4 ns / 49.7 ns |         104 ns / 46.1 ns |
| std::string of 12 characters: 256   | 79.3 ns / 57.2 ns |  37.3 ns / 22.1 ns |    163 ns / 52 ns |            164 ns / 51.9 ns | 78.9 ns / 57.1 ns |         164 ns / 53.1 ns |
| std::string of 12 characters: 512   | 86.8 ns / 66.1 ns |  40.7 ns / 22.7 ns |    279 ns / 59 ns |            281 ns / 58.7 ns | 85.9 ns / 66.2 ns |         279 ns / 59.8 ns |
| std::string of 12 characters: 1 024 |   96 ns / 75.5 ns |  42.9 ns / 23.6 ns |  503 ns / 68.5 ns |              509 ns / 69 ns | 95.5 ns / 76.1 ns |           516 ns / 69 ns |
| std::string of 12 characters: 2 048 |  107 ns / 85.9 ns |  43.4 ns / 24.1 ns |  943 ns / 79.5 ns |            954 ns / 83.2 ns |  106 ns / 86.4 ns |         950 ns / 81.1 ns |
| std::string of 12 characters: 4 096 |  118 ns / 98.3 ns |  42.8 ns / 24.7 ns |  1.8 µs / 90.4 ns |           1.81 µs / 94.4 ns |  117 ns / 97.7 ns |        1.87 µs / 91.3 ns |

</details>

<!-- END benchmark tables -->
