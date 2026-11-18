# Benchmarks

How the amc containers compare to the standard ones, as measured by the benchmarks of [benchmark/](../benchmark). The gains come with trade-offs, which are shown as well.

- [Method](#method)
- [Vectors](#vectors)
- [SmallVector](#smallvector)
- [FlatSet](#flatset)
- [SmallSet](#smallset)
- [Re-running the benchmarks](#re-running-the-benchmarks)
- [Numbers](#numbers)

## Method

The charts are drawn by [benchmark/plot_benchmarks.py](../benchmark/plot_benchmarks.py) from the results stored in [docs/benchmarks](benchmarks). Each benchmark runs in its own process, pinned to a single core of an idle machine, and the median of 5 repetitions is kept. The standard library is libstdc++ 13. The `SmallSet` benchmarks are built in C++20 (`-DCMAKE_CXX_STANDARD=20`), which its single comparison per element requires (see [SmallSet](#smallset)), and the other ones in C++17 (`-DCMAKE_CXX_STANDARD=17`, as the default is C++20). Beyond the L3 cache, results vary by about 15 % between runs. Even on an idle machine, a process now and then measures about 10 % more than the other processes of the same executable, whatever the size of the data: its 5 repetitions, which run in this same process, do not reveal it.

One command re-runs the benchmarks this way and refreshes the charts and the [tables](#numbers) of this page, see [Re-running the benchmarks](#re-running-the-benchmarks).

The "complex" types are 16 byte structures owning a dynamically allocated buffer: copying one allocates, and moving one steals the buffer. The first one is declared trivially relocatable, the second one is not.

## Vectors

![amc::vector vs std::vector](bench_vectors.svg)

The gains come from trivially relocatable types, whose elements are shifted with a single `memmove` instead of one move per element. `std::vector` already does this for trivially copyable types like `uint32_t`. For types which are not trivially relocatable, `amc::vector` uses the same algorithms as `std::vector`.

`amc::vector` doubles its capacity while it takes fewer than 4 KiB, like libstdc++ and libc++, as reallocating a small block costs more than the memory left unused. Beyond that, it grows its capacity by a factor of 1.5, like the MSVC standard library, to save memory: a large vector reallocates about 1.7 times more often, and moves each element about twice instead of once on average. For trivially relocatable types, `amc::vector` grows with `realloc`, which can extend large blocks without copying them: growing a `uint32_t` vector to 1 million elements is 2.1 times faster than with `std::vector` despite the smaller factor. Blocks of up to 1 KiB are copied to a new block instead, as `realloc` can seldom extend them in place and bypasses the fast paths of `malloc` and `free` for small blocks. The smaller factor costs time for large vectors of types which are not trivially relocatable. Growing a vector of the complex types is not shown: its time is dominated by the allocations of the elements themselves.

These allocations also dominate inserting at the end and erasing at the front, where `amc::vector` is slower than `std::vector` for the complex types despite the `memmove`. Each iteration allocates and frees about 2 400 element buffers, and glibc returns the free memory at the top of the heap to the system once it exceeds 128 KiB (`M_TRIM_THRESHOLD`): the next allocations take page faults to get it back. Whether this happens depends on the exact sequence of allocations. Before inserting copies of a value, libstdc++ copies it into a temporary, which `amc::vector` does not need. This one extra block per iteration changes which freed blocks glibc keeps in its thread cache, where they are never merged, and one of them then usually sits right below the top of the heap, preventing the trimming: `amc::vector` takes about 34 page faults per iteration, `std::vector` about 1. With the thread cache disabled, `std::vector` takes as many page faults as `amc::vector`. With the trimming disabled instead (`GLIBC_TUNABLES=glibc.malloc.trim_threshold=1073741824`), `amc::vector` is 1.2 times faster than `std::vector` for the trivially relocatable type, and as fast for the other one.

## SmallVector

![amc::SmallVector vs std::vector](bench_smallvector.svg)

`amc::SmallVector<T, N>` stores up to `N` elements in the object itself: a vector which stays small never allocates. In this benchmark, a vector is filled by `emplace_back`, read and destroyed, a given share of the vectors being small (up to the inline capacity of 16 elements) and the others large (17 to 64 elements, which move to the heap). The more vectors stay small, the bigger the gain: up to 3.3 to 4.0 times for `uint32_t`, and 1.5 times for the complex types, whose elements allocate their own buffer. Even when all the vectors outgrow the inline storage, `SmallVector` is still 1.05 to 1.2 times faster than `std::vector` for the complex types, including the one which is not trivially relocatable.

`amc::vector` is faster than `std::vector` here too: 1.07 to 1.38 times for `uint32_t` and 1.07 to 1.17 times for the trivially relocatable complex type. For the other one, it is 1.08 to 1.12 times faster with GCC, and as fast with Clang (within 4 %).

## FlatSet

![amc::FlatSet vs std::set and std::unordered_set](bench_sets.svg)

`amc::FlatSet` is a sorted vector: inserting or erasing an element shifts the following ones. This is fast for small sets of trivially relocatable types, but its cost grows linearly with the size of the set: erasing all the elements of a `uint32_t` set in random order is competitive with `std::set` up to 10 000 elements, and 4.4 to 4.5 times slower with 100 000. For types which are not trivially relocatable, the shifts move the elements one by one, and `std::set` inserts and erases faster.

![uint32_t set lookups by number of elements](bench_set_lookups.svg)

Lookup time mostly depends on where the set fits in the cache hierarchy: per element, `amc::FlatSet<uint32_t>` takes 4 bytes, `std::set` and `std::unordered_set` about 40 to 50 bytes (node, allocation overhead and buckets). Values are pseudo random, as consecutive integers never collide with the identity hash of libstdc++.

- **Independent** lookups: the CPU overlaps the cache misses of successive lookups, so this is a throughput, which can be shorter than a memory access.
- **Chained** lookups: each looked up value depends on the element found by the previous lookup, so this is the latency of a single lookup.

Observations:

- With 1 000 000 elements, `amc::FlatSet` (4 MB) still fits in the L3 cache while the node based sets (40 to 50 MB) do not: its lookup latency is 6 times lower than the one of `std::unordered_set`, with both compilers.
- The binary searches of `amc::FlatSet<uint32_t>` are branchless with both compilers: the next half of the range is selected with a conditional move, which, unlike a branch, is never mispredicted. The address of the next element to compare then depends on the result of the current comparison, so in sets larger than the L1 cache, each step prefetches the 4 elements which the two following steps may compare: the cache misses of successive steps overlap. This applies to scalar types (arithmetic, enumerations, pointers) ordered by `std::less` or `std::greater`. With costlier comparisons, like those of `std::string` or of a comparator dereferencing pointers, the branchless search is slower, so `amc::FlatSet` keeps `std::lower_bound` for them.
- GCC compiles the tree search of `std::set` with branches, Clang with conditional moves. Mispredicted branches serialize the lookups, so with GCC, independent lookups of `std::set` are barely faster than chained ones.

## SmallSet

![amc::SmallSet vs std::set](bench_smallset.svg)

`amc::SmallSet<T, N>` keeps up to `N` elements unsorted in an inline vector: an insertion is a linear search followed by a `push_back`, without allocation. Beyond `N` elements, it moves them to its set type, `std::set` by default. The charts compare `N = 16` to the other sets (`amc::FlatSet` uses an `amc::vector`), with elements which are cheap to compare and trivially relocatable (`uint32_t`), or not (short `std::string`):

- **`uint32_t`, mostly small sets**: filling a `SmallSet` of up to 16 elements costs 1.5 to 2.9 ns per element, 6.5 to 10.6 times less than a `std::set` and 5.0 to 9.0 times less than an `amc::FlatSet`. As soon as some sets are large though, those of the default `SmallSet<T, 16>` allocate a `std::set` node per element: with 10 % of large sets, it is already 2.1 to 2.4 times slower than a `SmallSet` whose set type is an `amc::FlatSet`.
- **`SmallSet<T, N, std::less<T>, amc::allocator<T>, amc::FlatSet<T>>`** is the fastest of these sets of `uint32_t` from 50 % of small sets (within 2 % of the default `SmallSet` when all of them are small): 6.8 to 8.2 times faster than `std::set` when all the sets are small, and still 2.9 to 3.5 times when they all are large. Its large state is within 10 % of an `amc::FlatSet`, for fills and lookups.
- **Lookups** are not what `SmallSet` speeds up the most: up to 16 elements, the linear search of the small state is up to 1.9 times faster than the one of a `std::set` with GCC, but the branchless binary search of an `amc::FlatSet` is 1.5 to 2.1 times faster from 4 elements. With Clang, the three of them are within 1.5 times of each other.
- **`std::string`**: from C++20, when the comparator calls `operator<` or `operator>` (`std::less`, `std::greater`, transparent or not, or their `std::ranges` versions) and the looked up key is three-way comparable with the elements, the linear search compares it once with each element, with `operator<=>`, instead of twice with the comparator. This applies to strings, to pairs, tuples or containers of them, and to the lookup of a string view or a string literal among strings with a transparent comparator (in C++17, the comparator compares it twice with most elements, which makes these lookups about twice as slow with Clang 23). Filling a `SmallSet` of up to 16 strings costs 2.6 to 3.7 times less per element than a `std::set`, and its lookups are up to 1.5 (GCC) to 1.7 (Clang) times faster, except at 2 and 16 elements with GCC (4 and 8 % slower). `SmallSet` is faster than `std::set` from 50 % of small sets (as fast when they all are large), and 2.4 to 2.5 times faster when all the sets are small, then also 1.6 to 1.8 times faster than `std::unordered_set`, the fastest set of strings up to 90 % of small sets (with GCC, `SmallSet` ties with it at 95 %). `std::string` is not trivially relocatable though: an `amc::FlatSet` moves its elements one by one at each insertion, so the fill of a large `SmallSet` with an `amc::FlatSet` is 1.7 to 2.0 times slower than with a `std::set` at 256 elements, and 10 to 15 times at 4096.

![Sets by number of elements](bench_smallset_sizes.svg)

`SmallSet` with an `amc::FlatSet` is thus the set of choice for sets of integers, enumerations, pointers or small trivially relocatable types which are often built, most of them with at most `N` elements: like for `SmallVector`, choose `N` to cover most of them. For strings, the default `SmallSet` (with a `std::set`) pays off when most sets are small. Keep `std::set` (or `std::unordered_set` when the order of the elements does not matter) for sets of strings which are mostly large, and for other types with costly comparisons.

## Re-running the benchmarks

```bash
python3 benchmark/run_benchmarks.py
```

builds the benchmarks with each compiler, runs them as described in [Method](#method), then redraws the charts (`docs/bench_*.svg`) and rewrites the [tables](#numbers) of this page. It requires Python 3, CMake 3.21 or later, the compilers (`g++` and `clang++-23` by default) and, to pin the benchmarks to a core, `taskset` (Linux). It uses Ninja if installed, and CMake downloads Google Benchmark at the start unless it is installed. Leave the machine idle: about 600 benchmarks run for each compiler, 5 repetitions of each, which takes tens of minutes per compiler.

It:

1. builds the benchmarks of every compiler before running any, so that a compilation error or a download without network stops it within minutes, not after the benchmarks of the first compiler. Each compiler builds in Release in a temporary directory: `vectors_benchmark` and `sets_benchmark` in C++17, then `smallsets_benchmark` in C++20. The directory is in `$XDG_RUNTIME_DIR`, a RAM disk on most Linux desktops, which spares the SSD about 30 MB of throwaway writes per build, and is removed once the executables are copied into the outputs directory. Google Benchmark is downloaded once, by the first build, whose sources the next ones reuse;
2. runs each benchmark in its own process, pinned to one core, and keeps the JSON output of Google Benchmark. By default, the core is picked among the fastest ones of the machine, from the frequencies and the L3 caches of its CPUs (on a hybrid CPU, the big cores): core 2 on the machine of the stored results.

Then `benchmark/plot_benchmarks.py --import` stores the medians of all the outputs in [docs/benchmarks](benchmarks), one file per benchmark and compiler, and generates the charts and the tables from them.

A run which stops after the builds (Ctrl-C, a failed benchmark, a killed process...) keeps its outputs and prints the command which continues it, `--resume DIR`: it only runs the benchmarks missing from `DIR`, with the executables and the options of the stopped run, then imports the results. The default outputs directory is in `$XDG_RUNTIME_DIR` though, which a reboot wipes.

| Option               | Effect                                                                                                                                       |
| -------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `--cxx COMPILER`     | Compiler to benchmark, repeatable (default: `g++` and `clang++-23`)                                                                          |
| `--cpu CPUS`         | `auto` (default) picks one of the fastest cores. A list like `2,3` runs a benchmark on each of these cores at the same time                  |
| `--jobs N`           | With `--cpu auto`, run `N` benchmarks at the same time, each on its own fast core (default: 1). See [below](#running-benchmarks-in-parallel) |
| `--repetitions N`    | Repetitions of each benchmark, whose median is kept (default: 5)                                                                             |
| `--min-time SECONDS` | Minimum time of a repetition of the benchmarks without a fixed number of iterations (default: 0.5 s). `0.01` for quick checks                |
| `--out DIR`          | Keep the JSON outputs in `DIR` (default: a temporary directory, removed after the import)                                                    |
| `--resume DIR`       | Continue the stopped run of the outputs directory `DIR`, with its executables and options (no other option)                                  |
| `--no-import`        | Only run the benchmarks and keep their outputs: import them later with `plot_benchmarks.py --import DIR`                                     |
| `--filter REGEX`     | Only run the benchmarks matching `REGEX`, for a quick check. Implies `--no-import`                                                           |
| `--main TAG`         | Compiler of the charts of the vectors and sets (default: `gcc13`)                                                                            |

To try it in a minute, without touching `docs/`: `python3 benchmark/run_benchmarks.py --cxx g++ --filter '^Growing<' --repetitions 1`.

Afterwards:

- Check `git diff docs/`: on a desktop, results vary between runs (see [Method](#method)), so rerun the benchmarks whose unchanged series moved.
- The prose of this page quotes figures of the tables (like "2.2 times faster"): they are not generated, update those which changed.
- Results are stored by compiler tag, like `gcc13` or `clang23`, taken from the version of the compiler: a new major version adds results next to the old ones. Delete the old `docs/benchmarks/*-gcc13.json` files to replace them, and pass `--main` if the charts should use another compiler.
- To redraw without running anything (after changing the script, for instance): `python3 benchmark/plot_benchmarks.py`.

### Running benchmarks in parallel

`--jobs 4` runs four benchmarks at once, each pinned to its own fast core (never two threads of a core), and takes about a quarter of the time: 41 s instead of 155 to 159 s for 58 of the benchmarks on the 4 fastest cores of the machine of the stored results. The cores share the L3 cache and the memory bandwidth though, which the benchmarks working on large data feel. In that run (GCC 13, 3 repetitions, 4 jobs against 1, the noise being measured by running the single job twice):

- lookups in sets of up to 10 000 elements and the vector benchmarks did not move beyond the noise (0.4 to 5 % at the median);
- lookups in sets of 1 million elements and more were 8 % slower at the median (2.8 % of noise) and up to 28 %, and one lookup in a set of 100 000 elements was 2.1 times slower.

This is why `--jobs` (and a list of CPUs) does not import its results into `docs/`: it prints the command to do so. Use it for quick checks and before / after comparisons (with the same number of jobs for both), and a single job for the numbers of this page. Whatever the number of jobs, the machine should be idle: another benchmark running on a core of the same L3 cache disturbs the results too.

## Numbers

The numbers behind the charts, for both compilers, generated by [benchmark/plot_benchmarks.py](../benchmark/plot_benchmarks.py): do not edit them by hand, [re-run the benchmarks](#re-running-the-benchmarks) instead.

<!-- BEGIN benchmark tables, generated by benchmark/plot_benchmarks.py -->

<details><summary>Vectors, GCC 13</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.68 µs |     6.67 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      297 ns |      303 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     46.1 µs |     52.8 µs | 1.1× slower |
| Complex trivially relocatable type: Insert N copies                   |     2.29 µs |      174 ns |  13× faster |
| Complex trivially relocatable type: Insert a range of pointers        |       19 µs |     13.2 µs | 1.4× faster |
| Complex trivially relocatable type: Insert a std::set range           |     28.6 µs |     18.3 µs | 1.6× faster |
| uint32_t: Assign a count or a range                                   |     11.8 ns |     13.9 ns | 1.2× slower |
| uint32_t: Construct and swap                                          |     9.68 ns |     9.51 ns |        same |
| uint32_t: Insert at end, erase at front                               |      220 ns |      210 ns |        same |
| uint32_t: Insert N copies                                             |     28.3 ns |     31.4 ns | 1.1× slower |
| uint32_t: Insert a range of pointers                                  |     84.8 ns |     84.5 ns |        same |
| uint32_t: Insert a std::set range                                     |      4.4 µs |     4.24 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     2.25 ms |     1.05 ms | 2.1× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.67 µs |     6.66 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      296 ns |      281 ns | 1.1× faster |
| Complex non trivially relocatable type: Insert at end, erase at front |       45 µs |     60.8 µs | 1.4× slower |
| Complex non trivially relocatable type: Insert N copies               |     2.41 µs |      1.4 µs | 1.7× faster |
| Complex non trivially relocatable type: Insert a range of pointers    |     18.5 µs |       17 µs | 1.1× faster |
| Complex non trivially relocatable type: Insert a std::set range       |     28.5 µs |     24.2 µs | 1.2× faster |

</details>

<details><summary>Vectors, Clang 23</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.54 µs |     6.49 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      286 ns |      276 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     45.6 µs |     52.3 µs | 1.1× slower |
| Complex trivially relocatable type: Insert N copies                   |     1.49 µs |      155 ns | 9.6× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     17.8 µs |     13.2 µs | 1.4× faster |
| Complex trivially relocatable type: Insert a std::set range           |     28.5 µs |     18.2 µs | 1.6× faster |
| uint32_t: Assign a count or a range                                   |     11.5 ns |     10.1 ns | 1.1× faster |
| uint32_t: Construct and swap                                          |     8.07 ns |     9.49 ns | 1.2× slower |
| uint32_t: Insert at end, erase at front                               |      218 ns |      211 ns |        same |
| uint32_t: Insert N copies                                             |     29.9 ns |     29.6 ns |        same |
| uint32_t: Insert a range of pointers                                  |     81.4 ns |       78 ns |        same |
| uint32_t: Insert a std::set range                                     |     4.48 µs |     4.34 µs |        same |
| uint32_t: Grow to 1M by emplace_back                                  |     2.12 ms |     1.02 ms | 2.1× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.55 µs |      6.5 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      293 ns |      278 ns | 1.1× faster |
| Complex non trivially relocatable type: Insert at end, erase at front |     45.5 µs |     61.3 µs | 1.3× slower |
| Complex non trivially relocatable type: Insert N copies               |     1.63 µs |     1.55 µs |        same |
| Complex non trivially relocatable type: Insert a range of pointers    |     17.2 µs |     17.7 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     28.4 µs |     25.7 µs | 1.1× faster |

</details>

<details><summary>SmallVector, GCC 13</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      373 ns |      347 ns |                  311 ns |                1.2× faster |
| Complex trivially relocatable type: 50 % small     |      241 ns |      225 ns |                  195 ns |                1.2× faster |
| Complex trivially relocatable type: 80 % small     |      154 ns |      139 ns |                  118 ns |                1.3× faster |
| Complex trivially relocatable type: 90 % small     |      127 ns |      113 ns |                 90.1 ns |                1.4× faster |
| Complex trivially relocatable type: 95 % small     |      111 ns |      104 ns |                 76.2 ns |                1.5× faster |
| Complex trivially relocatable type: 99 % small     |     99.5 ns |     88.3 ns |                 66.1 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     95.1 ns |     83.7 ns |                 61.6 ns |                1.5× faster |
| uint32_t: All large                                |     73.2 ns |     56.9 ns |                 36.1 ns |                2.0× faster |
| uint32_t: 50 % small                               |     62.6 ns |     49.7 ns |                 26.8 ns |                2.3× faster |
| uint32_t: 80 % small                               |     50.8 ns |     38.8 ns |                   18 ns |                2.8× faster |
| uint32_t: 90 % small                               |     47.4 ns |     35.1 ns |                 14.3 ns |                3.3× faster |
| uint32_t: 95 % small                               |     44.7 ns |     33.4 ns |                 12.6 ns |                3.6× faster |
| uint32_t: 99 % small                               |     43.9 ns |       32 ns |                   11 ns |                4.0× faster |
| uint32_t: All small                                |     43.5 ns |     31.4 ns |                 10.9 ns |                4.0× faster |
| Complex non trivially relocatable type: All large  |      382 ns |      354 ns |                  333 ns |                1.1× faster |
| Complex non trivially relocatable type: 50 % small |      243 ns |      223 ns |                  202 ns |                1.2× faster |
| Complex non trivially relocatable type: 80 % small |      157 ns |      142 ns |                  123 ns |                1.3× faster |
| Complex non trivially relocatable type: 90 % small |      125 ns |      113 ns |                 92.3 ns |                1.4× faster |
| Complex non trivially relocatable type: 95 % small |      110 ns |     99.1 ns |                 78.2 ns |                1.4× faster |
| Complex non trivially relocatable type: 99 % small |     99.1 ns |     88.3 ns |                 65.6 ns |                1.5× faster |
| Complex non trivially relocatable type: All small  |     94.8 ns |     84.6 ns |                   63 ns |                1.5× faster |

</details>

<details><summary>SmallVector, Clang 23</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      360 ns |      336 ns |                  341 ns |                1.1× faster |
| Complex trivially relocatable type: 50 % small     |      236 ns |      218 ns |                  206 ns |                1.1× faster |
| Complex trivially relocatable type: 80 % small     |      154 ns |      138 ns |                  120 ns |                1.3× faster |
| Complex trivially relocatable type: 90 % small     |      123 ns |      112 ns |                 93.9 ns |                1.3× faster |
| Complex trivially relocatable type: 95 % small     |      110 ns |     98.4 ns |                 79.3 ns |                1.4× faster |
| Complex trivially relocatable type: 99 % small     |     99.1 ns |     87.2 ns |                 67.6 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     96.2 ns |     82.5 ns |                   63 ns |                1.5× faster |
| uint32_t: All large                                |     68.6 ns |     64.2 ns |                 39.8 ns |                1.7× faster |
| uint32_t: 50 % small                               |     60.9 ns |     53.6 ns |                 30.5 ns |                2.0× faster |
| uint32_t: 80 % small                               |     46.9 ns |     41.3 ns |                 19.9 ns |                2.4× faster |
| uint32_t: 90 % small                               |     42.8 ns |     38.8 ns |                 15.9 ns |                2.7× faster |
| uint32_t: 95 % small                               |     40.6 ns |     36.5 ns |                   14 ns |                2.9× faster |
| uint32_t: 99 % small                               |     38.9 ns |     35.8 ns |                 12.1 ns |                3.2× faster |
| uint32_t: All small                                |     38.2 ns |     35.8 ns |                 11.6 ns |                3.3× faster |
| Complex non trivially relocatable type: All large  |      378 ns |      380 ns |                  350 ns |                1.1× faster |
| Complex non trivially relocatable type: 50 % small |      238 ns |      241 ns |                  212 ns |                1.1× faster |
| Complex non trivially relocatable type: 80 % small |      153 ns |      149 ns |                  124 ns |                1.2× faster |
| Complex non trivially relocatable type: 90 % small |      124 ns |      122 ns |                 95.9 ns |                1.3× faster |
| Complex non trivially relocatable type: 95 % small |      111 ns |      107 ns |                 80.3 ns |                1.4× faster |
| Complex non trivially relocatable type: 99 % small |     99.3 ns |     95.4 ns |                 67.1 ns |                1.5× faster |
| Complex non trivially relocatable type: All small  |     94.6 ns |       91 ns |                 62.3 ns |                1.5× faster |

</details>

<details><summary>Sets, GCC 13</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.49 µs |                    |      3.09 µs |     1.5× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   108 µs |                    |      54.4 µs |     2.0× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |  1.17 µs |                    |       181 ns |     6.5× faster |
| uint32_t: Insert 200 random values                               |  2.91 µs |            3.42 µs |      3.15 µs |     1.1× slower |
| uint32_t: Copy, erase all of 100                                 |  1.38 µs |            1.23 µs |      1.28 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |  16.9 µs |            24.5 µs |      21.3 µs |     1.3× slower |
| uint32_t: Copy, erase all of 10 000                              |   798 µs |             289 µs |       709 µs |     1.1× faster |
| uint32_t: Copy, erase all of 100 000                             |  14.6 ms |             4.6 ms |        66 ms |     4.5× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.55 µs |                    |      18.7 µs |     4.1× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   109 µs |                    |       424 µs |     3.9× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   228 ns |                    |      92.5 ns |     2.5× faster |

</details>

<details><summary>Sets, Clang 23</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |   4.9 µs |                    |         3 µs |     1.6× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   110 µs |                    |      54.5 µs |     2.0× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |   443 ns |                    |       124 ns |     3.6× faster |
| uint32_t: Insert 200 random values                               |  3.34 µs |            3.09 µs |      3.11 µs |     1.1× faster |
| uint32_t: Copy, erase all of 100                                 |  1.46 µs |            1.12 µs |      1.29 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |    18 µs |            23.1 µs |      21.6 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   836 µs |             273 µs |       710 µs |     1.2× faster |
| uint32_t: Copy, erase all of 100 000                             |  15.1 ms |             4.5 ms |      66.1 ms |     4.4× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.89 µs |                    |      20.1 µs |     4.1× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   110 µs |                    |       183 µs |     1.7× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |  92.5 ns |                    |      76.5 ns |     1.2× faster |

</details>

<details><summary>uint32_t lookups (independent / chained), GCC 13</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 22.6 ns / 23.9 ns |    7.16 ns / 14 ns | 5.12 ns / 17.2 ns |
| 1 000      | 33.4 ns / 33.8 ns |   7.8 ns / 14.3 ns |    7 ns / 21.9 ns |
| 10 000     | 57.2 ns / 56.6 ns |  9.36 ns / 20.3 ns | 10.3 ns / 28.2 ns |
| 100 000    |   110 ns / 110 ns |    9.33 ns / 39 ns | 18.2 ns / 33.1 ns |
| 1 000 000  |   499 ns / 514 ns |   55.2 ns / 298 ns | 27.9 ns / 48.5 ns |
| 10 000 000 | 1.15 µs / 1.17 µs |   75.3 ns / 459 ns |   160 ns / 193 ns |

</details>

<details><summary>uint32_t lookups (independent / chained), Clang 23</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 13.7 ns / 22.5 ns |  7.34 ns / 14.1 ns | 5.01 ns / 17.2 ns |
| 1 000      | 21.7 ns / 30.7 ns |  7.92 ns / 14.5 ns | 6.86 ns / 21.9 ns |
| 10 000     | 32.6 ns / 51.5 ns |  9.49 ns / 20.5 ns | 9.82 ns / 28.2 ns |
| 100 000    |  57.2 ns / 103 ns |  9.43 ns / 39.5 ns | 16.5 ns / 32.9 ns |
| 1 000 000  |   232 ns / 511 ns |   54.9 ns / 290 ns |   26.6 ns / 48 ns |
| 10 000 000 |  570 ns / 1.18 µs |   75.6 ns / 459 ns |   147 ns / 189 ns |

</details>

<details><summary>SmallSet, GCC 13</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  3.16 µs |            1.53 µs |       906 ns |                      874 ns |         2.98 µs |                   909 ns |                       3.5× faster |
| uint32_t: 50 % small                     |  1.83 µs |             920 ns |       548 ns |                      511 ns |         1.53 µs |                   491 ns |                       3.7× faster |
| uint32_t: 80 % small                     |  1.03 µs |             542 ns |       327 ns |                      291 ns |          644 ns |                   233 ns |                       4.4× faster |
| uint32_t: 90 % small                     |   750 ns |             414 ns |       252 ns |                      218 ns |          346 ns |                   146 ns |                       5.2× faster |
| uint32_t: 95 % small                     |   609 ns |             350 ns |       217 ns |                      181 ns |          199 ns |                   100 ns |                       6.1× faster |
| uint32_t: 99 % small                     |   485 ns |             299 ns |       186 ns |                      151 ns |         79.6 ns |                  65.3 ns |                       7.4× faster |
| uint32_t: All small                      |   453 ns |             284 ns |       178 ns |                      144 ns |         53.9 ns |                  55.1 ns |                       8.2× faster |
| std::string of 12 characters: All large  |  5.65 µs |            2.43 µs |      5.22 µs |                     5.17 µs |          5.6 µs |                  5.48 µs |                              same |
| std::string of 12 characters: 50 % small |  3.28 µs |            1.48 µs |         3 µs |                     2.97 µs |         2.97 µs |                  2.88 µs |                       1.1× faster |
| std::string of 12 characters: 80 % small |  1.81 µs |             885 ns |      1.64 µs |                     1.62 µs |         1.38 µs |                  1.35 µs |                       1.3× faster |
| std::string of 12 characters: 90 % small |   1.3 µs |             713 ns |      1.16 µs |                     1.14 µs |          839 ns |                   844 ns |                       1.5× faster |
| std::string of 12 characters: 95 % small |  1.05 µs |             596 ns |       918 ns |                      900 ns |          579 ns |                   586 ns |                       1.8× faster |
| std::string of 12 characters: 99 % small |   834 ns |             521 ns |       714 ns |                      692 ns |          368 ns |                   382 ns |                       2.2× faster |
| std::string of 12 characters: All small  |   777 ns |             496 ns |       657 ns |                      637 ns |          317 ns |                   342 ns |                       2.3× faster |

</details>

<details><summary>SmallSet, Clang 23</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  2.52 µs |            1.53 µs |       864 ns |                      875 ns |         2.36 µs |                   865 ns |                       2.9× faster |
| uint32_t: 50 % small                     |  1.45 µs |             911 ns |       517 ns |                      515 ns |         1.21 µs |                   464 ns |                       3.1× faster |
| uint32_t: 80 % small                     |   805 ns |             543 ns |       303 ns |                      296 ns |          525 ns |                   218 ns |                       3.7× faster |
| uint32_t: 90 % small                     |   580 ns |             412 ns |       232 ns |                      222 ns |          289 ns |                   137 ns |                       4.2× faster |
| uint32_t: 95 % small                     |   468 ns |             350 ns |       195 ns |                      185 ns |          171 ns |                  94.9 ns |                       4.9× faster |
| uint32_t: 99 % small                     |   376 ns |             300 ns |       165 ns |                      155 ns |         80.3 ns |                  60.4 ns |                       6.2× faster |
| uint32_t: All small                      |   356 ns |             284 ns |       158 ns |                      147 ns |         53.2 ns |                  52.3 ns |                       6.8× faster |
| std::string of 12 characters: All large  |  5.33 µs |            2.46 µs |      5.26 µs |                     5.25 µs |         5.33 µs |                   5.3 µs |                              same |
| std::string of 12 characters: 50 % small |  3.07 µs |            1.49 µs |      3.04 µs |                        3 µs |         2.81 µs |                  2.83 µs |                       1.1× faster |
| std::string of 12 characters: 80 % small |  1.68 µs |             915 ns |      1.66 µs |                     1.64 µs |          1.3 µs |                   1.3 µs |                       1.3× faster |
| std::string of 12 characters: 90 % small |  1.21 µs |             718 ns |      1.14 µs |                     1.15 µs |          799 ns |                   798 ns |                       1.5× faster |
| std::string of 12 characters: 95 % small |   979 ns |             606 ns |       897 ns |                      901 ns |          546 ns |                   562 ns |                       1.7× faster |
| std::string of 12 characters: 99 % small |   778 ns |             526 ns |       712 ns |                      692 ns |          350 ns |                   365 ns |                       2.1× faster |
| std::string of 12 characters: All small  |   726 ns |             525 ns |       653 ns |                      634 ns |          296 ns |                   314 ns |                       2.3× faster |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), GCC 13</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         |   11 ns / 5.24 ns |  18.5 ns / 6.78 ns | 10.8 ns / 6.34 ns |           4.27 ns / 6.73 ns | 1.68 ns / 5.12 ns |           2 ns / 5.09 ns |
| uint32_t: 4                         | 13.9 ns / 8.24 ns |  14.5 ns / 6.79 ns | 13.7 ns / 3.45 ns |              7 ns / 8.19 ns | 1.52 ns / 5.32 ns |          1.5 ns / 5.3 ns |
| uint32_t: 8                         | 17.7 ns / 8.97 ns |   15.2 ns / 9.2 ns | 13.7 ns / 2.69 ns |            9.2 ns / 8.69 ns |  1.8 ns / 5.61 ns |        1.78 ns / 5.57 ns |
| uint32_t: 16                        | 26.5 ns / 12.1 ns |    21 ns / 12.8 ns | 14.7 ns / 3.91 ns |           11.8 ns / 10.1 ns | 2.85 ns / 6.44 ns |        2.79 ns / 6.31 ns |
| uint32_t: 32                        | 31.3 ns / 14.6 ns |    25 ns / 11.6 ns | 15.5 ns / 3.87 ns |           14.2 ns / 10.9 ns | 28.6 ns / 13.9 ns |          14 ns / 3.87 ns |
| uint32_t: 64                        |   35.2 ns / 18 ns |  25.5 ns / 12.2 ns |  16.7 ns / 4.5 ns |           16.1 ns / 11.8 ns |   34 ns / 17.5 ns |        15.9 ns / 4.41 ns |
| uint32_t: 128                       | 37.4 ns / 21.2 ns |  26.6 ns / 11.9 ns | 18.3 ns / 5.07 ns |             18 ns / 12.8 ns | 37.8 ns / 20.2 ns |        17.9 ns / 5.09 ns |
| uint32_t: 256                       | 39.5 ns / 24.2 ns |    28.3 ns / 14 ns | 20.5 ns / 5.47 ns |           20.3 ns / 13.6 ns | 40.6 ns / 23.2 ns |        20.2 ns / 5.54 ns |
| uint32_t: 512                       | 41.6 ns / 27.3 ns |  32.6 ns / 14.2 ns | 22.8 ns / 6.35 ns |           22.7 ns / 14.9 ns | 42.9 ns / 26.3 ns |        22.8 ns / 6.37 ns |
| uint32_t: 1 024                     | 43.2 ns / 32.8 ns |    34 ns / 14.3 ns | 26.2 ns / 7.03 ns |           26.4 ns / 15.9 ns |   45.3 ns / 31 ns |        26.2 ns / 6.95 ns |
| uint32_t: 2 048                     | 47.4 ns / 40.5 ns |    34.8 ns / 15 ns | 31.8 ns / 7.66 ns |           32.1 ns / 16.9 ns | 49.3 ns / 38.4 ns |        31.7 ns / 7.68 ns |
| uint32_t: 4 096                     | 50.9 ns / 48.3 ns |    35.3 ns / 15 ns | 41.6 ns / 8.42 ns |           41.9 ns / 18.2 ns | 52.9 ns / 45.9 ns |        41.5 ns / 8.51 ns |
| std::string of 12 characters: 2     | 17.8 ns / 10.7 ns |  24.9 ns / 8.72 ns | 15.1 ns / 10.7 ns |           10.1 ns / 11.8 ns |  5.9 ns / 11.1 ns |        5.88 ns / 11.2 ns |
| std::string of 12 characters: 4     | 22.7 ns / 17.9 ns |    21.4 ns / 10 ns | 18.8 ns / 16.6 ns |           15.1 ns / 17.6 ns | 8.13 ns / 12.1 ns |        7.92 ns / 12.5 ns |
| std::string of 12 characters: 8     | 28.5 ns / 20.6 ns |  22.6 ns / 13.2 ns | 23.4 ns / 19.4 ns |           20.8 ns / 20.7 ns |   10.2 ns / 17 ns |        10.1 ns / 17.4 ns |
| std::string of 12 characters: 16    | 40.3 ns / 25.8 ns |    33.6 ns / 20 ns |   31.2 ns / 27 ns |             30 ns / 27.9 ns | 15.6 ns / 27.9 ns |        14.8 ns / 28.4 ns |
| std::string of 12 characters: 32    | 50.5 ns / 30.2 ns |    38.1 ns / 17 ns |   43 ns / 31.8 ns |             42 ns / 32.6 ns | 54.3 ns / 30.9 ns |        46.6 ns / 33.1 ns |
| std::string of 12 characters: 64    | 60.1 ns / 37.2 ns |  37.3 ns / 16.1 ns |   59.3 ns / 39 ns |           61.7 ns / 39.6 ns | 61.7 ns / 37.8 ns |        63.3 ns / 40.1 ns |
| std::string of 12 characters: 128   | 67.6 ns / 44.5 ns |  36.2 ns / 16.6 ns | 82.7 ns / 46.8 ns |           83.3 ns / 46.7 ns | 68.9 ns / 43.6 ns |          86.9 ns / 47 ns |
| std::string of 12 characters: 256   | 73.6 ns / 50.6 ns |  36.3 ns / 20.8 ns |  121 ns / 54.3 ns |            123 ns / 54.4 ns | 77.4 ns / 50.9 ns |         128 ns / 54.7 ns |
| std::string of 12 characters: 512   | 82.2 ns / 58.6 ns |  39.5 ns / 21.1 ns |  194 ns / 61.6 ns |            193 ns / 60.9 ns | 83.6 ns / 59.6 ns |         198 ns / 60.9 ns |
| std::string of 12 characters: 1 024 | 89.1 ns / 69.6 ns |  41.9 ns / 22.2 ns |  326 ns / 71.1 ns |            327 ns / 69.6 ns | 92.2 ns / 69.5 ns |         331 ns / 69.2 ns |
| std::string of 12 characters: 2 048 | 99.3 ns / 79.6 ns |  41.7 ns / 22.6 ns |  586 ns / 81.2 ns |            635 ns / 81.5 ns |  101 ns / 78.2 ns |           591 ns / 81 ns |
| std::string of 12 characters: 4 096 |  109 ns / 91.4 ns |  42.3 ns / 23.3 ns | 1.11 µs / 91.8 ns |           1.11 µs / 93.8 ns |  111 ns / 89.8 ns |        1.11 µs / 94.5 ns |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), Clang 23</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 11.3 ns / 6.59 ns |  17.1 ns / 6.72 ns | 9.55 ns / 6.46 ns |            6.7 ns / 6.56 ns | 1.67 ns / 6.41 ns |        1.73 ns / 6.26 ns |
| uint32_t: 4                         | 14.7 ns / 5.27 ns |  14.3 ns / 7.15 ns | 10.5 ns / 8.12 ns |           8.82 ns / 8.13 ns | 1.56 ns / 6.54 ns |        1.64 ns / 6.43 ns |
| uint32_t: 8                         | 18.5 ns / 6.58 ns |  14.4 ns / 10.2 ns | 11.4 ns / 8.69 ns |           10.4 ns / 8.78 ns | 1.88 ns / 6.81 ns |         2.03 ns / 6.7 ns |
| uint32_t: 16                        | 27.2 ns / 10.3 ns |    20.2 ns / 13 ns |     13 ns / 10 ns |           12.3 ns / 9.94 ns | 2.57 ns / 7.28 ns |         3.11 ns / 7.3 ns |
| uint32_t: 32                        | 33.8 ns / 12.2 ns |  24.5 ns / 11.9 ns | 14.4 ns / 10.8 ns |           14.7 ns / 10.9 ns |   30 ns / 10.3 ns |        14.2 ns / 11.7 ns |
| uint32_t: 64                        | 38.9 ns / 12.1 ns |  25.2 ns / 12.5 ns | 15.9 ns / 11.8 ns |           16.3 ns / 11.8 ns | 35.7 ns / 13.9 ns |          16 ns / 12.6 ns |
| uint32_t: 128                       | 42.6 ns / 13.2 ns |  25.6 ns / 12.2 ns | 17.6 ns / 12.8 ns |           18.1 ns / 12.8 ns | 40.3 ns / 14.6 ns |        17.8 ns / 13.6 ns |
| uint32_t: 256                       | 45.6 ns / 16.2 ns |  27.2 ns / 14.3 ns | 19.9 ns / 13.6 ns |           20.3 ns / 13.7 ns | 43.8 ns / 18.4 ns |        19.9 ns / 14.5 ns |
| uint32_t: 512                       | 48.3 ns / 19.9 ns |  30.6 ns / 14.4 ns |   22.6 ns / 15 ns |           22.8 ns / 14.9 ns | 46.9 ns / 19.6 ns |        22.7 ns / 16.1 ns |
| uint32_t: 1 024                     | 51.6 ns / 21.8 ns |    32 ns / 14.6 ns | 26.2 ns / 15.9 ns |           26.5 ns / 15.9 ns |   50 ns / 21.3 ns |        26.3 ns / 16.7 ns |
| uint32_t: 2 048                     | 56.2 ns / 25.4 ns |  32.6 ns / 15.4 ns | 31.9 ns / 16.8 ns |           32.3 ns / 16.9 ns |   55 ns / 25.5 ns |          32 ns / 17.8 ns |
| uint32_t: 4 096                     |   61 ns / 29.2 ns |  32.9 ns / 15.6 ns |   42 ns / 18.1 ns |           42.2 ns / 18.3 ns | 59.3 ns / 28.9 ns |        41.9 ns / 18.9 ns |
| std::string of 12 characters: 2     | 17.1 ns / 12.3 ns |  24.8 ns / 9.63 ns |   15.3 ns / 11 ns |           9.53 ns / 11.2 ns | 5.43 ns / 10.8 ns |        7.43 ns / 9.88 ns |
| std::string of 12 characters: 4     | 22.5 ns / 18.3 ns |    21.8 ns / 11 ns | 18.6 ns / 16.4 ns |           14.5 ns / 16.8 ns | 6.89 ns / 12.2 ns |        7.83 ns / 11.1 ns |
| std::string of 12 characters: 8     |   31 ns / 24.2 ns |  23.1 ns / 14.2 ns | 23.4 ns / 19.2 ns |           20.4 ns / 19.5 ns |  8.4 ns / 14.6 ns |        8.41 ns / 14.5 ns |
| std::string of 12 characters: 16    | 43.4 ns / 30.7 ns |  34.1 ns / 21.7 ns |   32 ns / 26.5 ns |           30.3 ns / 26.8 ns | 12.7 ns / 20.7 ns |        12.4 ns / 20.8 ns |
| std::string of 12 characters: 32    | 54.9 ns / 37.4 ns |  38.8 ns / 17.8 ns | 46.3 ns / 30.9 ns |           44.6 ns / 30.9 ns | 53.8 ns / 35.2 ns |        45.7 ns / 31.3 ns |
| std::string of 12 characters: 64    |   64.2 ns / 43 ns |    37.3 ns / 17 ns |   66.7 ns / 38 ns |           65.9 ns / 37.8 ns | 61.6 ns / 44.8 ns |        65.7 ns / 38.1 ns |
| std::string of 12 characters: 128   | 72.2 ns / 49.2 ns |  36.9 ns / 17.5 ns | 99.4 ns / 45.2 ns |           99.4 ns / 44.8 ns | 70.2 ns / 50.4 ns |        98.6 ns / 44.9 ns |
| std::string of 12 characters: 256   | 80.1 ns / 56.5 ns |  37.1 ns / 21.8 ns |  159 ns / 52.4 ns |            157 ns / 52.4 ns | 77.9 ns / 57.9 ns |           157 ns / 52 ns |
| std::string of 12 characters: 512   | 88.4 ns / 65.6 ns |  40.2 ns / 22.2 ns |  269 ns / 58.6 ns |            266 ns / 57.5 ns | 85.3 ns / 65.8 ns |         266 ns / 58.9 ns |
| std::string of 12 characters: 1 024 | 97.5 ns / 75.1 ns |  42.5 ns / 23.3 ns |  479 ns / 70.4 ns |            476 ns / 69.1 ns | 95.2 ns / 74.8 ns |           476 ns / 70 ns |
| std::string of 12 characters: 2 048 |  108 ns / 85.1 ns |  42.8 ns / 23.7 ns |  897 ns / 81.5 ns |            889 ns / 81.9 ns |  105 ns / 84.9 ns |         890 ns / 80.9 ns |
| std::string of 12 characters: 4 096 |  119 ns / 96.9 ns |  42.9 ns / 24.5 ns | 1.74 µs / 95.5 ns |           1.71 µs / 96.6 ns |  116 ns / 96.3 ns |        1.72 µs / 95.8 ns |

</details>

<!-- END benchmark tables -->
