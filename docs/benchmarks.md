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

`amc::SmallVector<T, N>` stores up to `N` elements in the object itself: a vector which stays small never allocates. In this benchmark, a vector is filled by `emplace_back`, read and destroyed, a given share of the vectors being small (up to the inline capacity of 16 elements) and the others large (17 to 64 elements, which move to the heap). The more vectors stay small, the bigger the gain: up to 3.3 to 4.1 times for `uint32_t`, and 1.5 to 1.6 times for the complex types, whose elements allocate their own buffer. Even when all the vectors outgrow the inline storage, `SmallVector` is still 1.1 to 1.2 times faster than `std::vector` for the complex types, including the one which is not trivially relocatable.

`amc::vector` is faster than `std::vector` here too: 1.04 to 1.45 times for `uint32_t`, 1.09 to 1.15 times for the trivially relocatable complex type, and 1.06 to 1.12 times for the other one.

## FlatSet

![amc::FlatSet vs std::set and std::unordered_set](bench_sets.svg)

`amc::FlatSet` is a sorted vector: inserting or erasing an element shifts the following ones. This is fast for small sets of trivially relocatable types, but its cost grows linearly with the size of the set: erasing all the elements of a `uint32_t` set in random order is competitive with `std::set` up to 10 000 elements, and about 4.5 times slower with 100 000. For types which are not trivially relocatable, the shifts move the elements one by one, and `std::set` inserts and erases faster.

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

- **`uint32_t`, mostly small sets**: filling a `SmallSet` of up to 16 elements costs 1.5 to 2.6 ns per element, 6.7 to 10.6 times less than a `std::set` and 4.9 to 7.4 times less than an `amc::FlatSet`. As soon as some sets are large though, those of the default `SmallSet<T, 16>` allocate a `std::set` node per element: with 10 % of large sets, it is already 2.1 to 2.4 times slower than a `SmallSet` whose set type is an `amc::FlatSet`.
- **`SmallSet<T, N, std::less<T>, amc::allocator<T>, amc::FlatSet<T>>`** is the fastest of these sets of `uint32_t` from 50 % of small sets (within 9 % of the default `SmallSet` when all of them are small): 6.6 to 8.0 times faster than `std::set` when all the sets are small, and still 2.9 to 3.5 times when they all are large. Its large state is within 8 % of an `amc::FlatSet`, for fills and lookups.
- **Lookups** are not what `SmallSet` speeds up the most: up to 16 elements, the linear search of the small state is up to 1.9 times faster than the one of a `std::set` with GCC, but the branchless binary search of an `amc::FlatSet` is 1.6 to 2.1 times faster from 4 elements. With Clang, the three of them are within 1.5 times of each other.
- **`std::string`**: from C++20, when the comparator calls `operator<` or `operator>` (`std::less`, `std::greater`, transparent or not, or their `std::ranges` versions) and the looked up key is three-way comparable with the elements, the linear search compares it once with each element, with `operator<=>`, instead of twice with the comparator. This applies to strings, to pairs, tuples or containers of them, and to the lookup of a string view or a string literal among strings with a transparent comparator (in C++17, the comparator compares it twice with most elements, which makes these lookups about twice as slow with Clang 23). Filling a `SmallSet` of up to 16 strings costs 2.8 to 3.4 times less per element than a `std::set`, and its lookups are up to 1.4 (GCC) to 1.6 (Clang) times faster, except at 2 and 16 elements with GCC (4 and 9 % slower). `SmallSet` is faster than `std::set` from 50 % of small sets (as fast when they all are large), and 2.3 to 2.5 times faster when all the sets are small, then also 1.6 times faster than `std::unordered_set`, the fastest set of strings up to 90 % of small sets (with GCC, `SmallSet` ties with it at 95 %). `std::string` is not trivially relocatable though: an `amc::FlatSet` moves its elements one by one at each insertion, so the fill of a large `SmallSet` with an `amc::FlatSet` is 2.2 to 2.7 times slower than with a `std::set` at 256 elements, and 17 to 21 times at 4096.

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
| Complex trivially relocatable type: Assign a count or a range         |     6.71 µs |      6.7 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      310 ns |      294 ns | 1.1× faster |
| Complex trivially relocatable type: Insert at end, erase at front     |     45.2 µs |     52.9 µs | 1.2× slower |
| Complex trivially relocatable type: Insert N copies                   |      2.3 µs |      196 ns |  12× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     18.6 µs |     13.6 µs | 1.4× faster |
| Complex trivially relocatable type: Insert a std::set range           |     28.8 µs |     19.4 µs | 1.5× faster |
| uint32_t: Assign a count or a range                                   |     12.3 ns |     14.3 ns | 1.2× slower |
| uint32_t: Construct and swap                                          |     9.66 ns |     8.88 ns | 1.1× faster |
| uint32_t: Insert at end, erase at front                               |      222 ns |      209 ns | 1.1× faster |
| uint32_t: Insert N copies                                             |     27.7 ns |     29.9 ns | 1.1× slower |
| uint32_t: Insert a range of pointers                                  |      101 ns |     80.8 ns | 1.3× faster |
| uint32_t: Insert a std::set range                                     |     4.46 µs |     4.23 µs | 1.1× faster |
| uint32_t: Grow to 1M by emplace_back                                  |      2.2 ms |     1.05 ms | 2.1× faster |
| Complex non trivially relocatable type: Assign a count or a range     |      6.8 µs |      6.7 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      290 ns |      300 ns |        same |
| Complex non trivially relocatable type: Insert at end, erase at front |     46.6 µs |     56.4 µs | 1.2× slower |
| Complex non trivially relocatable type: Insert N copies               |     2.28 µs |     2.07 µs | 1.1× faster |
| Complex non trivially relocatable type: Insert a range of pointers    |     18.7 µs |       18 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     28.2 µs |     24.7 µs | 1.1× faster |

</details>

<details><summary>Vectors, Clang 23</summary>

| Operation                                                             | std::vector | amc::vector | amc         |
| --------------------------------------------------------------------- | ----------: | ----------: | ----------: |
| Complex trivially relocatable type: Assign a count or a range         |     6.53 µs |     6.54 µs |        same |
| Complex trivially relocatable type: Construct and swap                |      272 ns |      270 ns |        same |
| Complex trivially relocatable type: Insert at end, erase at front     |     45.5 µs |     53.9 µs | 1.2× slower |
| Complex trivially relocatable type: Insert N copies                   |     1.47 µs |      205 ns | 7.2× faster |
| Complex trivially relocatable type: Insert a range of pointers        |     17.8 µs |     13.2 µs | 1.3× faster |
| Complex trivially relocatable type: Insert a std::set range           |     28.3 µs |     18.3 µs | 1.5× faster |
| uint32_t: Assign a count or a range                                   |     11.8 ns |     10.5 ns | 1.1× faster |
| uint32_t: Construct and swap                                          |      8.4 ns |     9.22 ns | 1.1× slower |
| uint32_t: Insert at end, erase at front                               |      219 ns |      210 ns |        same |
| uint32_t: Insert N copies                                             |     29.9 ns |     27.9 ns | 1.1× faster |
| uint32_t: Insert a range of pointers                                  |     80.9 ns |       78 ns |        same |
| uint32_t: Insert a std::set range                                     |     4.66 µs |     4.38 µs | 1.1× faster |
| uint32_t: Grow to 1M by emplace_back                                  |     2.11 ms |     1.02 ms | 2.1× faster |
| Complex non trivially relocatable type: Assign a count or a range     |     6.54 µs |     6.55 µs |        same |
| Complex non trivially relocatable type: Construct and swap            |      306 ns |      270 ns | 1.1× faster |
| Complex non trivially relocatable type: Insert at end, erase at front |     46.6 µs |     58.6 µs | 1.3× slower |
| Complex non trivially relocatable type: Insert N copies               |     1.46 µs |     2.03 µs | 1.4× slower |
| Complex non trivially relocatable type: Insert a range of pointers    |     17.6 µs |       17 µs |        same |
| Complex non trivially relocatable type: Insert a std::set range       |     28.5 µs |     25.5 µs | 1.1× faster |

</details>

<details><summary>SmallVector, GCC 13</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      383 ns |      337 ns |                  310 ns |                1.2× faster |
| Complex trivially relocatable type: 50 % small     |      242 ns |      218 ns |                  189 ns |                1.3× faster |
| Complex trivially relocatable type: 80 % small     |      156 ns |      137 ns |                  114 ns |                1.4× faster |
| Complex trivially relocatable type: 90 % small     |      126 ns |      111 ns |                 87.5 ns |                1.4× faster |
| Complex trivially relocatable type: 95 % small     |      111 ns |     97.4 ns |                 74.8 ns |                1.5× faster |
| Complex trivially relocatable type: 99 % small     |     98.7 ns |     86.7 ns |                 63.9 ns |                1.5× faster |
| Complex trivially relocatable type: All small      |     95.4 ns |     82.8 ns |                   62 ns |                1.5× faster |
| uint32_t: All large                                |     77.7 ns |     55.8 ns |                 35.7 ns |                2.2× faster |
| uint32_t: 50 % small                               |     64.1 ns |     48.8 ns |                 26.9 ns |                2.4× faster |
| uint32_t: 80 % small                               |     53.6 ns |     38.4 ns |                 17.7 ns |                3.0× faster |
| uint32_t: 90 % small                               |     49.4 ns |     34.9 ns |                 14.4 ns |                3.4× faster |
| uint32_t: 95 % small                               |     47.2 ns |       33 ns |                 12.4 ns |                3.8× faster |
| uint32_t: 99 % small                               |     45.4 ns |     31.3 ns |                   11 ns |                4.1× faster |
| uint32_t: All small                                |     43.5 ns |     31.2 ns |                 10.9 ns |                4.0× faster |
| Complex non trivially relocatable type: All large  |      377 ns |      352 ns |                  329 ns |                1.1× faster |
| Complex non trivially relocatable type: 50 % small |      244 ns |      223 ns |                  199 ns |                1.2× faster |
| Complex non trivially relocatable type: 80 % small |      155 ns |      141 ns |                  118 ns |                1.3× faster |
| Complex non trivially relocatable type: 90 % small |      126 ns |      113 ns |                 90.3 ns |                1.4× faster |
| Complex non trivially relocatable type: 95 % small |      111 ns |     99.4 ns |                   76 ns |                1.5× faster |
| Complex non trivially relocatable type: 99 % small |     98.8 ns |     88.6 ns |                 64.8 ns |                1.5× faster |
| Complex non trivially relocatable type: All small  |     95.3 ns |     85.3 ns |                 61.4 ns |                1.6× faster |

</details>

<details><summary>SmallVector, Clang 23</summary>

| Vectors                                            | std::vector | amc::vector | amc::SmallVector<T, 16> | SmallVector vs std::vector |
| -------------------------------------------------- | ----------: | ----------: | ----------------------: | -------------------------: |
| Complex trivially relocatable type: All large      |      368 ns |      338 ns |                  341 ns |                1.1× faster |
| Complex trivially relocatable type: 50 % small     |      241 ns |      219 ns |                  215 ns |                1.1× faster |
| Complex trivially relocatable type: 80 % small     |      153 ns |      138 ns |                  125 ns |                1.2× faster |
| Complex trivially relocatable type: 90 % small     |      125 ns |      111 ns |                 94.6 ns |                1.3× faster |
| Complex trivially relocatable type: 95 % small     |      109 ns |     98.5 ns |                 81.5 ns |                1.3× faster |
| Complex trivially relocatable type: 99 % small     |     96.8 ns |     86.2 ns |                 68.5 ns |                1.4× faster |
| Complex trivially relocatable type: All small      |     94.1 ns |       82 ns |                 63.8 ns |                1.5× faster |
| uint32_t: All large                                |     65.4 ns |     63.1 ns |                 40.9 ns |                1.6× faster |
| uint32_t: 50 % small                               |     58.7 ns |     53.8 ns |                 31.6 ns |                1.9× faster |
| uint32_t: 80 % small                               |     46.8 ns |     42.6 ns |                 21.1 ns |                2.2× faster |
| uint32_t: 90 % small                               |     44.1 ns |     38.3 ns |                 15.8 ns |                2.8× faster |
| uint32_t: 95 % small                               |     39.6 ns |       36 ns |                 13.8 ns |                2.9× faster |
| uint32_t: 99 % small                               |       38 ns |     35.1 ns |                 12.1 ns |                3.1× faster |
| uint32_t: All small                                |     38.6 ns |     33.7 ns |                 11.6 ns |                3.3× faster |
| Complex non trivially relocatable type: All large  |      396 ns |      364 ns |                  356 ns |                1.1× faster |
| Complex non trivially relocatable type: 50 % small |      246 ns |      232 ns |                  219 ns |                1.1× faster |
| Complex non trivially relocatable type: 80 % small |      158 ns |      146 ns |                  127 ns |                1.2× faster |
| Complex non trivially relocatable type: 90 % small |      128 ns |      119 ns |                 94.8 ns |                1.3× faster |
| Complex non trivially relocatable type: 95 % small |      113 ns |      105 ns |                 80.3 ns |                1.4× faster |
| Complex non trivially relocatable type: 99 % small |      100 ns |       93 ns |                   68 ns |                1.5× faster |
| Complex non trivially relocatable type: All small  |     96.5 ns |       90 ns |                 63.7 ns |                1.5× faster |

</details>

<details><summary>Sets, GCC 13</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.47 µs |                    |      3.15 µs |     1.4× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   106 µs |                    |      54.3 µs |     1.9× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |   984 ns |                    |       179 ns |     5.5× faster |
| uint32_t: Insert 200 random values                               |   2.9 µs |            3.41 µs |      3.09 µs |     1.1× slower |
| uint32_t: Copy, erase all of 100                                 |  1.41 µs |            1.23 µs |      1.29 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |  17.5 µs |            24.4 µs |      21.3 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   801 µs |             286 µs |       707 µs |     1.1× faster |
| uint32_t: Copy, erase all of 100 000                             |  14.6 ms |            4.58 ms |      66.1 ms |     4.5× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.51 µs |                    |      20.6 µs |     4.6× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   110 µs |                    |       379 µs |     3.5× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |   221 ns |                    |      93.9 ns |     2.3× faster |

</details>

<details><summary>Sets, Clang 23</summary>

| Operation                                                        | std::set | std::unordered_set | amc::FlatSet | amc vs std::set |
| ---------------------------------------------------------------- | -------: | -----------------: | -----------: | --------------: |
| Complex trivially relocatable type: Insert 200 random values     |  4.83 µs |                    |      2.97 µs |     1.6× faster |
| Complex trivially relocatable type: Copy, erase all of 1 000     |   109 µs |                    |      54.5 µs |     2.0× faster |
| Complex trivially relocatable type: Lookup in 1 000 000          |   445 ns |                    |       134 ns |     3.3× faster |
| uint32_t: Insert 200 random values                               |  3.29 µs |            2.99 µs |      3.11 µs |     1.1× faster |
| uint32_t: Copy, erase all of 100                                 |  1.41 µs |            1.14 µs |      1.29 µs |     1.1× faster |
| uint32_t: Copy, erase all of 1 000                               |    18 µs |            22.7 µs |      21.7 µs |     1.2× slower |
| uint32_t: Copy, erase all of 10 000                              |   830 µs |             272 µs |       710 µs |     1.2× faster |
| uint32_t: Copy, erase all of 100 000                             |  15.1 ms |            4.48 ms |        66 ms |     4.4× slower |
| Complex non trivially relocatable type: Insert 200 random values |  4.88 µs |                    |        20 µs |     4.1× slower |
| Complex non trivially relocatable type: Copy, erase all of 1 000 |   112 µs |                    |       184 µs |     1.6× slower |
| Complex non trivially relocatable type: Lookup in 100 000        |  99.4 ns |                    |        76 ns |     1.3× faster |

</details>

<details><summary>uint32_t lookups (independent / chained), GCC 13</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 22.5 ns / 23.8 ns |    7.12 ns / 14 ns | 5.06 ns / 17.2 ns |
| 1 000      | 34.5 ns / 34.3 ns |  7.84 ns / 14.3 ns | 7.02 ns / 21.9 ns |
| 10 000     | 57.5 ns / 57.1 ns |  9.35 ns / 20.3 ns | 10.2 ns / 28.1 ns |
| 100 000    |   109 ns / 109 ns |   9.3 ns / 39.1 ns | 18.2 ns / 33.1 ns |
| 1 000 000  |   486 ns / 533 ns |   54.4 ns / 296 ns | 27.7 ns / 48.5 ns |
| 10 000 000 | 1.14 µs / 1.15 µs |     75 ns / 457 ns |   160 ns / 193 ns |

</details>

<details><summary>uint32_t lookups (independent / chained), Clang 23</summary>

| Elements   | std::set          | std::unordered_set | amc::FlatSet      |
| ---------- | ----------------: | -----------------: | ----------------: |
| 100        | 13.8 ns / 22.5 ns |  7.36 ns / 14.1 ns | 5.01 ns / 17.2 ns |
| 1 000      | 21.7 ns / 30.7 ns |  7.94 ns / 14.5 ns | 6.85 ns / 21.9 ns |
| 10 000     | 32.6 ns / 51.5 ns |  9.68 ns / 20.5 ns | 9.81 ns / 28.2 ns |
| 100 000    |  57.1 ns / 103 ns |  9.46 ns / 39.2 ns | 16.5 ns / 32.8 ns |
| 1 000 000  |   235 ns / 508 ns |   55.6 ns / 290 ns | 26.6 ns / 48.1 ns |
| 10 000 000 |  583 ns / 1.16 µs |   75.5 ns / 459 ns |   148 ns / 190 ns |

</details>

<details><summary>SmallSet, GCC 13</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  3.18 µs |            1.52 µs |       884 ns |                      873 ns |         2.98 µs |                   912 ns |                       3.5× faster |
| uint32_t: 50 % small                     |  1.86 µs |             909 ns |       524 ns |                      511 ns |         1.52 µs |                   493 ns |                       3.8× faster |
| uint32_t: 80 % small                     |  1.03 µs |             539 ns |       309 ns |                      291 ns |          640 ns |                   234 ns |                       4.4× faster |
| uint32_t: 90 % small                     |   750 ns |             413 ns |       236 ns |                      218 ns |          348 ns |                   146 ns |                       5.1× faster |
| uint32_t: 95 % small                     |   606 ns |             345 ns |       201 ns |                      181 ns |          201 ns |                   100 ns |                       6.0× faster |
| uint32_t: 99 % small                     |   479 ns |             295 ns |       172 ns |                      152 ns |         83.4 ns |                  65.7 ns |                       7.3× faster |
| uint32_t: All small                      |   457 ns |             282 ns |       162 ns |                      144 ns |         52.8 ns |                  57.2 ns |                       8.0× faster |
| std::string of 12 characters: All large  |  5.71 µs |            2.43 µs |      5.75 µs |                     5.74 µs |         5.55 µs |                  5.87 µs |                              same |
| std::string of 12 characters: 50 % small |   3.3 µs |            1.47 µs |      3.25 µs |                     3.26 µs |         2.96 µs |                  3.12 µs |                       1.1× faster |
| std::string of 12 characters: 80 % small |  1.84 µs |             882 ns |      1.75 µs |                     1.74 µs |         1.38 µs |                  1.45 µs |                       1.3× faster |
| std::string of 12 characters: 90 % small |  1.33 µs |             690 ns |      1.23 µs |                     1.22 µs |          842 ns |                   884 ns |                       1.5× faster |
| std::string of 12 characters: 95 % small |  1.06 µs |             590 ns |       962 ns |                      945 ns |          590 ns |                   603 ns |                       1.8× faster |
| std::string of 12 characters: 99 % small |   841 ns |             507 ns |       739 ns |                      720 ns |          371 ns |                   379 ns |                       2.2× faster |
| std::string of 12 characters: All small  |   782 ns |             490 ns |       684 ns |                      660 ns |          315 ns |                   321 ns |                       2.4× faster |

</details>

<details><summary>SmallSet, Clang 23</summary>

| Sets                                     | std::set | std::unordered_set | amc::FlatSet | FlatSet, SmallVector<T, 16> | SmallSet<T, 16> | SmallSet<T, 16, FlatSet> | SmallSet with FlatSet vs std::set |
| ---------------------------------------- | -------: | -----------------: | -----------: | --------------------------: | --------------: | -----------------------: | --------------------------------: |
| uint32_t: All large                      |  2.52 µs |            1.56 µs |       870 ns |                      874 ns |         2.35 µs |                   861 ns |                       2.9× faster |
| uint32_t: 50 % small                     |  1.46 µs |             927 ns |       523 ns |                      516 ns |         1.21 µs |                   465 ns |                       3.1× faster |
| uint32_t: 80 % small                     |   810 ns |             548 ns |       307 ns |                      295 ns |          520 ns |                   219 ns |                       3.7× faster |
| uint32_t: 90 % small                     |   580 ns |             421 ns |       233 ns |                      222 ns |          286 ns |                   137 ns |                       4.2× faster |
| uint32_t: 95 % small                     |   472 ns |             351 ns |       197 ns |                      185 ns |          171 ns |                  95.2 ns |                       5.0× faster |
| uint32_t: 99 % small                     |   374 ns |             298 ns |       167 ns |                      155 ns |         76.9 ns |                  62.3 ns |                       6.0× faster |
| uint32_t: All small                      |   352 ns |             286 ns |       159 ns |                      146 ns |         55.7 ns |                  53.6 ns |                       6.6× faster |
| std::string of 12 characters: All large  |  5.31 µs |            2.48 µs |      5.27 µs |                     5.36 µs |         5.32 µs |                  5.26 µs |                              same |
| std::string of 12 characters: 50 % small |  3.06 µs |             1.5 µs |      3.11 µs |                     3.06 µs |         2.83 µs |                   2.8 µs |                       1.1× faster |
| std::string of 12 characters: 80 % small |  1.67 µs |             900 ns |      1.65 µs |                     1.71 µs |         1.33 µs |                   1.3 µs |                       1.3× faster |
| std::string of 12 characters: 90 % small |   1.2 µs |             707 ns |      1.18 µs |                     1.17 µs |          813 ns |                   807 ns |                       1.5× faster |
| std::string of 12 characters: 95 % small |   967 ns |             608 ns |       939 ns |                      921 ns |          563 ns |                   568 ns |                       1.7× faster |
| std::string of 12 characters: 99 % small |   764 ns |             521 ns |       721 ns |                      698 ns |          364 ns |                   369 ns |                       2.1× faster |
| std::string of 12 characters: All small  |   718 ns |             500 ns |       672 ns |                      643 ns |          312 ns |                   312 ns |                       2.3× faster |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), GCC 13</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 10.5 ns / 5.02 ns |  18.2 ns / 6.69 ns |  9.99 ns / 6.4 ns |           4.31 ns / 6.44 ns | 1.48 ns / 5.22 ns |        1.53 ns / 5.13 ns |
| uint32_t: 4                         | 14.2 ns / 8.21 ns |   14.6 ns / 6.7 ns | 11.1 ns / 3.48 ns |           6.92 ns / 7.95 ns |  1.5 ns / 5.43 ns |        1.45 ns / 5.45 ns |
| uint32_t: 8                         |   18 ns / 9.01 ns |  14.5 ns / 9.15 ns | 11.9 ns / 2.64 ns |           9.12 ns / 8.64 ns | 1.72 ns / 5.64 ns |        1.73 ns / 5.71 ns |
| uint32_t: 16                        | 25.4 ns / 12.4 ns |  20.7 ns / 12.5 ns | 13.3 ns / 3.92 ns |           11.8 ns / 9.96 ns | 2.62 ns / 6.62 ns |        2.75 ns / 6.38 ns |
| uint32_t: 32                        | 31.2 ns / 14.7 ns |  24.4 ns / 11.5 ns | 14.8 ns / 3.87 ns |           14.1 ns / 10.8 ns | 28.3 ns / 14.1 ns |        14.1 ns / 3.88 ns |
| uint32_t: 64                        |   34.9 ns / 18 ns |    25.4 ns / 12 ns |  16.2 ns / 4.5 ns |           16.1 ns / 11.7 ns | 33.5 ns / 17.7 ns |         15.9 ns / 4.4 ns |
| uint32_t: 128                       | 37.7 ns / 21.2 ns |  25.9 ns / 11.9 ns | 17.8 ns / 5.06 ns |             18 ns / 12.7 ns | 37.6 ns / 20.4 ns |        17.8 ns / 5.09 ns |
| uint32_t: 256                       | 40.1 ns / 24.4 ns |  27.4 ns / 13.9 ns | 20.1 ns / 5.47 ns |           20.3 ns / 13.5 ns | 40.9 ns / 23.5 ns |        20.6 ns / 5.54 ns |
| uint32_t: 512                       |   41.4 ns / 27 ns |    30.7 ns / 14 ns | 22.5 ns / 6.36 ns |           22.8 ns / 14.7 ns | 42.7 ns / 27.2 ns |        22.6 ns / 6.37 ns |
| uint32_t: 1 024                     | 43.8 ns / 33.3 ns |  31.9 ns / 14.3 ns |   26 ns / 7.03 ns |           26.5 ns / 15.7 ns | 44.8 ns / 32.7 ns |        26.2 ns / 6.95 ns |
| uint32_t: 2 048                     | 47.3 ns / 39.8 ns |  32.8 ns / 14.9 ns | 31.6 ns / 7.67 ns |           32.1 ns / 16.8 ns | 49.1 ns / 40.6 ns |         31.7 ns / 7.7 ns |
| uint32_t: 4 096                     |   51 ns / 46.7 ns |  33.2 ns / 14.7 ns | 41.3 ns / 8.41 ns |             41.9 ns / 18 ns |   53.1 ns / 48 ns |        41.4 ns / 8.48 ns |
| std::string of 12 characters: 2     | 16.9 ns / 10.8 ns |  25.2 ns / 8.51 ns | 15.6 ns / 10.8 ns |           10.1 ns / 11.9 ns | 5.82 ns / 11.2 ns |        5.88 ns / 11.2 ns |
| std::string of 12 characters: 4     | 22.4 ns / 17.8 ns |  21.1 ns / 9.93 ns | 19.7 ns / 16.7 ns |           15.5 ns / 18.1 ns | 7.77 ns / 12.3 ns |        7.91 ns / 12.5 ns |
| std::string of 12 characters: 8     | 27.8 ns / 20.4 ns |  22.7 ns / 13.2 ns |   24 ns / 19.4 ns |           22.6 ns / 21.1 ns | 9.78 ns / 17.3 ns |          10 ns / 17.4 ns |
| std::string of 12 characters: 16    |   39.6 ns / 26 ns |  32.6 ns / 20.7 ns | 31.5 ns / 26.8 ns |           34.6 ns / 28.3 ns | 14.2 ns / 28.3 ns |        15.7 ns / 28.4 ns |
| std::string of 12 characters: 32    | 50.4 ns / 30.3 ns |  37.6 ns / 17.1 ns | 43.2 ns / 31.4 ns |           52.3 ns / 33.2 ns | 53.2 ns / 30.1 ns |        54.2 ns / 33.3 ns |
| std::string of 12 characters: 64    | 59.2 ns / 38.8 ns |  36.5 ns / 16.2 ns | 59.4 ns / 38.5 ns |             79.7 ns / 40 ns | 60.5 ns / 37.3 ns |        80.7 ns / 40.4 ns |
| std::string of 12 characters: 128   | 66.5 ns / 47.3 ns |  35.9 ns / 16.7 ns | 82.4 ns / 45.5 ns |            125 ns / 47.2 ns | 68.8 ns / 42.9 ns |         125 ns / 47.2 ns |
| std::string of 12 characters: 256   | 72.7 ns / 53.7 ns |  35.9 ns / 20.9 ns |  121 ns / 52.8 ns |            209 ns / 54.6 ns | 75.7 ns / 49.8 ns |         203 ns / 54.7 ns |
| std::string of 12 characters: 512   | 78.9 ns / 62.3 ns |  39.3 ns / 21.3 ns |    193 ns / 59 ns |            368 ns / 61.2 ns | 83.4 ns / 58.6 ns |         349 ns / 61.4 ns |
| std::string of 12 characters: 1 024 | 88.4 ns / 71.5 ns |  41.2 ns / 22.3 ns |  340 ns / 70.3 ns |            678 ns / 70.1 ns | 91.5 ns / 67.4 ns |         637 ns / 69.8 ns |
| std::string of 12 characters: 2 048 |   97.6 ns / 81 ns |  41.7 ns / 22.7 ns |  615 ns / 80.8 ns |           1.29 µs / 82.6 ns |  100 ns / 76.7 ns |         1.2 µs / 81.1 ns |
| std::string of 12 characters: 4 096 |  107 ns / 91.8 ns |    42 ns / 23.3 ns | 1.11 µs / 91.7 ns |             2.52 µs / 94 ns |  110 ns / 89.1 ns |        2.33 µs / 94.8 ns |

</details>

<details><summary>Sets by number of elements (fill per element / lookup), Clang 23</summary>

| Elements                            | std::set          | std::unordered_set | amc::FlatSet      | FlatSet, SmallVector<T, 16> | SmallSet<T, 16>   | SmallSet<T, 16, FlatSet> |
| ----------------------------------- | ----------------: | -----------------: | ----------------: | --------------------------: | ----------------: | -----------------------: |
| uint32_t: 2                         | 11.2 ns / 6.75 ns |  17.5 ns / 6.67 ns | 9.43 ns / 6.38 ns |           6.62 ns / 6.63 ns | 1.68 ns / 6.93 ns |          1.8 ns / 6.5 ns |
| uint32_t: 4                         | 14.6 ns / 5.47 ns |  14.3 ns / 7.07 ns | 10.8 ns / 8.07 ns |           8.82 ns / 8.23 ns | 1.53 ns / 6.71 ns |        1.74 ns / 6.66 ns |
| uint32_t: 8                         | 18.8 ns / 6.57 ns |    14.4 ns / 10 ns | 11.6 ns / 8.71 ns |           10.3 ns / 8.81 ns | 1.87 ns / 6.97 ns |        2.05 ns / 6.89 ns |
| uint32_t: 16                        | 27.6 ns / 10.6 ns |    21 ns / 13.1 ns | 12.9 ns / 9.99 ns |           12.2 ns / 10.2 ns | 2.61 ns / 7.69 ns |        2.95 ns / 7.56 ns |
| uint32_t: 32                        | 33.7 ns / 12.4 ns |      25 ns / 12 ns | 14.3 ns / 10.8 ns |             14.5 ns / 11 ns | 30.1 ns / 10.5 ns |        13.8 ns / 11.6 ns |
| uint32_t: 64                        | 38.8 ns / 12.4 ns |  25.1 ns / 12.6 ns | 15.9 ns / 11.7 ns |           16.2 ns / 11.9 ns | 35.4 ns / 14.1 ns |        15.9 ns / 12.7 ns |
| uint32_t: 128                       | 42.4 ns / 13.7 ns |  25.6 ns / 12.3 ns | 17.5 ns / 12.7 ns |           17.8 ns / 12.9 ns |   40 ns / 14.8 ns |        17.7 ns / 13.7 ns |
| uint32_t: 256                       | 45.8 ns / 16.5 ns |  27.1 ns / 14.4 ns | 19.7 ns / 13.7 ns |             20 ns / 13.7 ns | 44.1 ns / 18.5 ns |        19.8 ns / 14.6 ns |
| uint32_t: 512                       | 48.1 ns / 20.2 ns |  30.4 ns / 14.4 ns | 22.6 ns / 14.9 ns |             22.6 ns / 15 ns | 47.2 ns / 19.9 ns |        22.7 ns / 16.1 ns |
| uint32_t: 1 024                     | 51.1 ns / 22.1 ns |  31.9 ns / 14.8 ns | 26.1 ns / 15.9 ns |             26.2 ns / 16 ns | 49.9 ns / 21.5 ns |        26.2 ns / 16.8 ns |
| uint32_t: 2 048                     | 56.1 ns / 25.8 ns |  32.5 ns / 15.4 ns | 31.8 ns / 16.8 ns |             32.1 ns / 17 ns | 54.7 ns / 25.9 ns |        31.9 ns / 18.1 ns |
| uint32_t: 4 096                     |   61 ns / 29.5 ns |  33.1 ns / 15.3 ns | 41.9 ns / 18.1 ns |           42.1 ns / 18.2 ns | 59.8 ns / 29.1 ns |        41.8 ns / 19.2 ns |
| std::string of 12 characters: 2     | 17.5 ns / 12.3 ns |  25.1 ns / 9.38 ns |   15.7 ns / 11 ns |           9.48 ns / 11.2 ns | 5.77 ns / 10.9 ns |        7.42 ns / 9.88 ns |
| std::string of 12 characters: 4     | 22.8 ns / 18.4 ns |  22.1 ns / 10.6 ns | 20.7 ns / 16.3 ns |           14.4 ns / 16.4 ns | 6.95 ns / 12.3 ns |        7.73 ns / 11.4 ns |
| std::string of 12 characters: 8     | 30.2 ns / 24.2 ns |  23.2 ns / 13.7 ns |   24.8 ns / 19 ns |           20.3 ns / 19.3 ns | 8.81 ns / 15.1 ns |        8.45 ns / 13.8 ns |
| std::string of 12 characters: 16    | 43.5 ns / 30.7 ns |  33.3 ns / 20.7 ns | 33.4 ns / 26.3 ns |           30.2 ns / 26.8 ns | 13.3 ns / 22.1 ns |        12.2 ns / 19.8 ns |
| std::string of 12 characters: 32    | 54.6 ns / 37.5 ns |  38.2 ns / 18.1 ns | 47.2 ns / 30.5 ns |           45.9 ns / 30.9 ns | 54.3 ns / 35.2 ns |        47.6 ns / 30.5 ns |
| std::string of 12 characters: 64    | 64.6 ns / 42.9 ns |  37.8 ns / 17.1 ns |   68 ns / 37.4 ns |           66.6 ns / 37.5 ns | 61.5 ns / 45.1 ns |        69.1 ns / 37.3 ns |
| std::string of 12 characters: 128   | 72.9 ns / 49.1 ns |  37.1 ns / 17.7 ns | 99.7 ns / 44.8 ns |            101 ns / 43.7 ns | 69.5 ns / 50.6 ns |         105 ns / 44.3 ns |
| std::string of 12 characters: 256   | 80.6 ns / 56.4 ns |  37.3 ns / 22.1 ns |  158 ns / 51.9 ns |            160 ns / 51.1 ns | 77.3 ns / 58.2 ns |         169 ns / 51.5 ns |
| std::string of 12 characters: 512   | 87.9 ns / 65.4 ns |    40 ns / 22.4 ns |  289 ns / 57.7 ns |            271 ns / 56.8 ns | 84.7 ns / 66.3 ns |         268 ns / 57.5 ns |
| std::string of 12 characters: 1 024 | 97.8 ns / 74.7 ns |  42.3 ns / 23.6 ns |  526 ns / 68.5 ns |            478 ns / 68.8 ns | 94.1 ns / 75.2 ns |         522 ns / 67.9 ns |
| std::string of 12 characters: 2 048 |  108 ns / 84.9 ns |    42.8 ns / 24 ns |  983 ns / 80.8 ns |            896 ns / 81.1 ns |  104 ns / 85.7 ns |         981 ns / 80.6 ns |
| std::string of 12 characters: 4 096 |  118 ns / 97.1 ns |  42.6 ns / 24.7 ns | 1.72 µs / 93.8 ns |           1.72 µs / 97.1 ns |  115 ns / 97.3 ns |           1.9 µs / 94 ns |

</details>

<!-- END benchmark tables -->
