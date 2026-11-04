#include <benchmark/benchmark.h>

#include <amc/config.hpp>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef AMC_SMALLSET
#include <amc/flatset.hpp>
#include <amc/smallset.hpp>
#include <amc/smallvector.hpp>
#endif

#include "testhelpers.hpp"

namespace amc {
namespace {

#ifdef AMC_SMALLSET

/// Inline capacity of the SmallSets, and of the SmallVector of a FlatSet (same as the SmallVector benchmarks).
constexpr uint32_t kSmallSize = 16;

/// Largest number of elements of the sets of the benchmarks by number of elements.
constexpr uint32_t kMaxSize = 4096;

/// The benchmarked containers, for each element type. The names of the aliases are the ones of the benchmark runs read
/// by benchmark/plot_benchmarks.py.
template <class T>
using REF = std::set<T>;
template <class T>
using REFUno = std::unordered_set<T>;
template <class T>
using AMCFlat = FlatSet<T>;
template <class T>
using AMCFlatSmallVec = FlatSet<T, std::less<T>, amc::allocator<T>, SmallVector<T, kSmallSize>>;
template <class T>
using AMCSmall = SmallSet<T, kSmallSize>;
template <class T>
using AMCSmallFlat = SmallSet<T, kSmallSize, std::less<T>, amc::allocator<T>, FlatSet<T>>;

using REFInt = REF<uint32_t>;
using REFUnoInt = REFUno<uint32_t>;
using AMCFlatInt = AMCFlat<uint32_t>;
using AMCFlatSmallVecInt = AMCFlatSmallVec<uint32_t>;
using AMCSmallInt = AMCSmall<uint32_t>;
using AMCSmallFlatInt = AMCSmallFlat<uint32_t>;

using REFString = REF<std::string>;
using REFUnoString = REFUno<std::string>;
using AMCFlatString = AMCFlat<std::string>;
using AMCFlatSmallVecString = AMCFlatSmallVec<std::string>;
using AMCSmallString = AMCSmall<std::string>;
using AMCSmallFlatString = AMCSmallFlat<std::string>;

template <class T>
T MakeValue(uint32_t v);

template <>
uint32_t MakeValue<uint32_t>(uint32_t v) {
  return v;
}

/// Short strings with a common prefix, like codes or identifiers: they fit in the inline buffer of std::string (no
/// allocation), and their comparisons cost more than the ones of integers.
template <>
std::string MakeValue<std::string>(uint32_t v) {
  char buf[16];
  std::snprintf(buf, sizeof(buf), "key-%08x", static_cast<unsigned>(v));
  return buf;
}

uint32_t ToUInt32(uint32_t v) { return v; }
uint32_t ToUInt32(const std::string& s) { return static_cast<unsigned char>(s.back()); }

/// Distinct values spread over the whole uint32_t range, in random order. A set of n elements is filled with n
/// consecutive values of the pool, the n following ones are absent from it. They are built before the measures.
template <class T>
const std::vector<T>& Pool() {
  static const std::vector<T> pool = [] {
    std::vector<T> values;
    std::unordered_set<uint32_t> seen;
    for (uint64_t i = 0; values.size() < 4 * kMaxSize; ++i) {
      const auto v = static_cast<uint32_t>(HashValue64(i));
      if (seen.insert(v).second) {
        values.push_back(MakeValue<T>(v));
      }
    }
    return values;
  }();
  return pool;
}

/// First value of the pool of a window of 'n' values, chosen from the 64 random bits 'hash'.
uint32_t WindowStart(uint64_t hash, uint32_t n) {
  return static_cast<uint32_t>(((hash >> 32) * (4 * kMaxSize - n + 1)) >> 32);
}

/// Life of a set: filled by insertions of distinct values, each of them and as many absent values are looked up, its
/// elements are read, and it is destroyed.
/// state.range(0) % of the sets are small, with 1 to kSmallSize elements, the others have kSmallSize + 1 to
/// 4 * kSmallSize elements (sizes are uniformly distributed in both ranges), like in the SmallVector benchmarks.
template <class SetType>
void SmallSizes(benchmark::State& state) {
  using ValueType = typename SetType::value_type;
  const std::vector<ValueType>& pool = Pool<ValueType>();
  const auto smallPercent = static_cast<uint64_t>(state.range(0));
  uint64_t s = 0;
  uint32_t sum = 0;
  for (auto _ : state) {
    const uint64_t hash = HashValue64(++s);
    const auto r = static_cast<uint32_t>(hash >> 32);
    const uint32_t size = hash % 100U < smallPercent ? 1U + r % kSmallSize : kSmallSize + 1U + r % (3U * kSmallSize);
    const ValueType* values = pool.data() + WindowStart(HashValue64(hash), 2 * size);
    SetType set;
    for (uint32_t i = 0; i < size; ++i) {
      set.insert(values[i]);
    }
    for (uint32_t i = 2 * size; i > 0; --i) {  // absent values first, then present ones in reverse insertion order
      sum += static_cast<uint32_t>(set.find(values[i - 1]) != set.end());
    }
    for (const ValueType& v : set) {
      sum += ToUInt32(v);
    }
    benchmark::DoNotOptimize(sum);
  }
}

/// Fills an empty set with state.range(0) distinct values inserted in random order, then destroys it.
template <class SetType>
void Fill(benchmark::State& state) {
  using ValueType = typename SetType::value_type;
  const std::vector<ValueType>& pool = Pool<ValueType>();
  const auto size = static_cast<uint32_t>(state.range(0));
  uint64_t s = 0;
  for (auto _ : state) {
    const ValueType* values = pool.data() + WindowStart(HashValue64(++s), size);
    SetType set;
    for (uint32_t i = 0; i < size; ++i) {
      set.insert(values[i]);
    }
    benchmark::DoNotOptimize(set);
  }
}

/// Looks up random values in a set of state.range(0) elements: half of them are present, half are absent.
template <class SetType>
void LookUp(benchmark::State& state) {
  using ValueType = typename SetType::value_type;
  const std::vector<ValueType>& pool = Pool<ValueType>();
  const auto size = static_cast<uint32_t>(state.range(0));
  const SetType set(pool.begin(), pool.begin() + size);
  uint64_t s = 0;
  uint32_t found = 0;
  for (auto _ : state) {
    // Maps 32 random bits to [0, 2 * size) with a multiplication instead of a slower modulo
    const auto i = static_cast<std::size_t>(((HashValue64(++s) >> 32) * (2 * size)) >> 32);
    found += static_cast<uint32_t>(set.find(pool[i]) != set.end());
    benchmark::DoNotOptimize(found);
  }
}

/// Percentages of small sets of the SmallSizes benchmark.
void SmallSizesArgs(benchmark::internal::Benchmark* b) {
  for (int smallPercent : {0, 50, 80, 90, 95, 99, 100}) {
    b->Arg(smallPercent);
  }
}

/// Numbers of elements of the Fill and LookUp benchmarks, from small sets to sets much larger than kSmallSize.
void SizesArgs(benchmark::internal::Benchmark* b) { b->RangeMultiplier(2)->Range(2, kMaxSize); }

#define AMC_SMALLSET_BENCHMARKS(Type)                                           \
  BENCHMARK_TEMPLATE(SmallSizes, REF##Type)->Apply(SmallSizesArgs);             \
  BENCHMARK_TEMPLATE(SmallSizes, REFUno##Type)->Apply(SmallSizesArgs);          \
  BENCHMARK_TEMPLATE(SmallSizes, AMCFlat##Type)->Apply(SmallSizesArgs);         \
  BENCHMARK_TEMPLATE(SmallSizes, AMCFlatSmallVec##Type)->Apply(SmallSizesArgs); \
  BENCHMARK_TEMPLATE(SmallSizes, AMCSmall##Type)->Apply(SmallSizesArgs);        \
  BENCHMARK_TEMPLATE(SmallSizes, AMCSmallFlat##Type)->Apply(SmallSizesArgs);    \
  BENCHMARK_TEMPLATE(Fill, REF##Type)->Apply(SizesArgs);                        \
  BENCHMARK_TEMPLATE(Fill, REFUno##Type)->Apply(SizesArgs);                     \
  BENCHMARK_TEMPLATE(Fill, AMCFlat##Type)->Apply(SizesArgs);                    \
  BENCHMARK_TEMPLATE(Fill, AMCFlatSmallVec##Type)->Apply(SizesArgs);            \
  BENCHMARK_TEMPLATE(Fill, AMCSmall##Type)->Apply(SizesArgs);                   \
  BENCHMARK_TEMPLATE(Fill, AMCSmallFlat##Type)->Apply(SizesArgs);               \
  BENCHMARK_TEMPLATE(LookUp, REF##Type)->Apply(SizesArgs);                      \
  BENCHMARK_TEMPLATE(LookUp, REFUno##Type)->Apply(SizesArgs);                   \
  BENCHMARK_TEMPLATE(LookUp, AMCFlat##Type)->Apply(SizesArgs);                  \
  BENCHMARK_TEMPLATE(LookUp, AMCFlatSmallVec##Type)->Apply(SizesArgs);          \
  BENCHMARK_TEMPLATE(LookUp, AMCSmall##Type)->Apply(SizesArgs);                 \
  BENCHMARK_TEMPLATE(LookUp, AMCSmallFlat##Type)->Apply(SizesArgs)

AMC_SMALLSET_BENCHMARKS(Int);
AMC_SMALLSET_BENCHMARKS(String);

#endif

}  // namespace
}  // namespace amc

BENCHMARK_MAIN();
