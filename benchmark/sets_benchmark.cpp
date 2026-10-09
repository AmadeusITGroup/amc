#include <benchmark/benchmark.h>

#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/smallvector.hpp>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <type_traits>
#include <unordered_set>
#include <utility>
#include <vector>
#ifdef AMC_SMALLSET
#include <amc/smallset.hpp>
#endif

#include "benchhelpers.hpp"
#include "testhelpers.hpp"
#include "testtypes.hpp"

namespace amc {
TypeStats TypeStats::_stats;

namespace {

using REFRelocType = std::set<ComplexTriviallyRelocatableType>;
using REFNonRelocType = std::set<ComplexNonTriviallyRelocatableType>;
using REFInt = std::set<uint32_t>;
using REFUnoInt = std::unordered_set<uint32_t>;

using AMCRelocType = amc::FlatSet<ComplexTriviallyRelocatableType>;
using AMCNonRelocType = amc::FlatSet<ComplexNonTriviallyRelocatableType>;
using AMCInt = amc::FlatSet<uint32_t>;

template <class SetType>
void InsertRandom(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  TypeStats::_stats.start();
  for (auto _ : state) {
    SetType v;
    for (uint32_t s = 0; v.size() < kMaxValue / 5; ++s) {
      uint32_t value = static_cast<uint32_t>(HashValue64(s) % kMaxValue);
      v.emplace(value);
    }
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

/// Value of the 'i'-th element of the sets of the EraseRandom and LookUp benchmarks.
/// Values are spread over the whole uint32_t range and inserted in random order: consecutive values would be collision
/// free with the identity hash of libstdc++ and libc++, and would lay out the nodes of node based sets in memory in the
/// order of their values, both of which are unrealistic.
uint32_t RandomValue(uint64_t i) { return static_cast<uint32_t>(HashValue64(i)); }

/// Copies a set of state.range(0) values, then erases all of them in random order.
template <class SetType>
void EraseRandom(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  const auto size = static_cast<uint32_t>(state.range(0));
  using ValueType = typename SetType::value_type;
  std::vector<uint32_t> values(size);
  for (uint32_t i = 0; i < size; ++i) {
    values[i] = RandomValue(i);
  }
  const SetType elems(values.begin(), values.end());
  // Erase in another order than the insertion one (Fisher-Yates shuffle), the same at each iteration
  for (uint32_t i = size; i > 1; --i) {
    std::swap(values[i - 1], values[static_cast<std::size_t>(HashValue64(size + i) % i)]);
  }
  const std::vector<ValueType> eraseOrder(values.begin(), values.end());
  TypeStats::_stats.start();
  for (auto _ : state) {
    SetType v = elems;
    for (const ValueType &value : eraseOrder) {
      v.erase(value);
    }
    benchmark::DoNotOptimize(v);
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

/// Set of the last LookUp benchmark run, with its size and the address of a variable identifying its type.
struct LookUpSetCache {
  std::shared_ptr<const void> set;
  const void *typeId = nullptr;
  uint32_t size = 0;
};

LookUpSetCache gLookUpSetCache;

/// Returns the set of the first 'size' values of RandomValue (slightly fewer elements, because of duplicates).
/// Google Benchmark calls a benchmark several times to calibrate its number of iterations, and filling a big node based
/// set takes much longer than the measured lookups: the set is reused by successive calls with the same type and size.
/// Only the last one is kept, to bound memory usage.
template <class SetType>
const SetType &LookUpSet(uint32_t size) {
  static const char kTypeId = 0;
  if (gLookUpSetCache.typeId != &kTypeId || gLookUpSetCache.size != size) {
    gLookUpSetCache = LookUpSetCache();  // frees the previous set before filling the new one
    std::vector<uint32_t> values(size);
    for (uint32_t i = 0; i < size; ++i) {
      values[i] = RandomValue(i);
    }
    gLookUpSetCache.set = std::make_shared<SetType>(values.begin(), values.end());
    gLookUpSetCache.typeId = &kTypeId;
    gLookUpSetCache.size = size;
  }
  return *static_cast<const SetType *>(gLookUpSetCache.set.get());
}

/// Makes the next value looked up by LookUpImpl depend on the element 'it' found by the current lookup.
template <class It>
void ChainLookUp(std::true_type, It it, uint64_t &s) {
  // Without it, the compiler knows that *it is the looked up value, and would remove the dependency
  benchmark::DoNotOptimize(it);
  s += static_cast<uint32_t>(*it);
}

template <class It>
void ChainLookUp(std::false_type, It, uint64_t &) {}

/// Looks up random elements of a set of state.range(0) values (all are found).
/// If 'Chained' is false, lookups are independent from each other, so the CPU overlaps the cache misses of successive
/// ones: the measured time is a throughput. Otherwise, each looked up value depends on the element found by the
/// previous lookup, which serializes them: the measured time is the latency of a lookup.
template <class SetType, bool Chained>
void LookUpImpl(benchmark::State &state) {
  const auto size = static_cast<uint32_t>(state.range(0));
  const SetType &elems = LookUpSet<SetType>(size);
  using ValueType = typename SetType::value_type;
  TypeStats::_stats = TypeStats();
  TypeStats::_stats.start();
  uint32_t out = 0;
  uint64_t s = 0;
  for (auto _ : state) {
    // Maps 32 random bits to [0, size) with a multiplication instead of a slower modulo
    const uint64_t i = ((HashValue64(++s) >> 32) * size) >> 32;
    ValueType vToLookFor(RandomValue(i));
    const auto it = elems.find(vToLookFor);
    if (it != elems.end()) {
      ChainLookUp(std::integral_constant<bool, Chained>(), it, s);
      ++out;
    }
    benchmark::DoNotOptimize(out);
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

/// Throughput of independent lookups.
template <class SetType>
void LookUp(benchmark::State &state) {
  LookUpImpl<SetType, false>(state);
}

/// Latency of a lookup.
template <class SetType>
void LookUpChained(benchmark::State &state) {
  LookUpImpl<SetType, true>(state);
}

template <class SetType, unsigned TypicalMaxSize>
void CommonUsage(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  TypeStats::_stats.start();
  uint32_t out = 0;
  uint32_t s = 0;
  for (auto _ : state) {
    SetType v;
    for (; v.size() < TypicalMaxSize; ++s) {
      uint32_t value = static_cast<uint32_t>(HashValue64(s) % TypicalMaxSize);
      v.emplace(value);
    }
    auto it = v.find(HashValue64(++s) % TypicalMaxSize);
    if (it != v.end()) {
      v.erase(it);
    }
    for (const auto &el : v) {
      out += uint32_t(el);
    }
  }
  benchmark::DoNotOptimize(out);
  TypeStats::_stats.end();
  PrintStats(state);
}

}  // namespace

BENCHMARK_TEMPLATE(InsertRandom, REFRelocType);
BENCHMARK_TEMPLATE(InsertRandom, AMCRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFRelocType)->Arg(1000);
BENCHMARK_TEMPLATE(EraseRandom, AMCRelocType)->Arg(1000);

BENCHMARK_TEMPLATE(LookUp, REFRelocType)->Arg(1000000);
BENCHMARK_TEMPLATE(LookUp, AMCRelocType)->Arg(1000000);

BENCHMARK_TEMPLATE(InsertRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(InsertRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFNonRelocType)->Arg(1000);
BENCHMARK_TEMPLATE(EraseRandom, AMCNonRelocType)->Arg(1000);

BENCHMARK_TEMPLATE(LookUp, REFNonRelocType)->Arg(100000);
BENCHMARK_TEMPLATE(LookUp, AMCNonRelocType)->Arg(100000);

BENCHMARK_TEMPLATE(InsertRandom, REFInt);
BENCHMARK_TEMPLATE(InsertRandom, REFUnoInt);
BENCHMARK_TEMPLATE(InsertRandom, AMCInt);

BENCHMARK_TEMPLATE(EraseRandom, REFInt)->RangeMultiplier(10)->Range(100, 100000);
BENCHMARK_TEMPLATE(EraseRandom, REFUnoInt)->RangeMultiplier(10)->Range(100, 100000);
BENCHMARK_TEMPLATE(EraseRandom, AMCInt)->RangeMultiplier(10)->Range(100, 100000);

// From sets fitting in L1 cache to sets much bigger than the L3 cache
BENCHMARK_TEMPLATE(LookUp, REFInt)->RangeMultiplier(10)->Range(100, 10000000);
BENCHMARK_TEMPLATE(LookUp, REFUnoInt)->RangeMultiplier(10)->Range(100, 10000000);
BENCHMARK_TEMPLATE(LookUp, AMCInt)->RangeMultiplier(10)->Range(100, 10000000);

BENCHMARK_TEMPLATE(LookUpChained, REFInt)->RangeMultiplier(10)->Range(100, 10000000);
BENCHMARK_TEMPLATE(LookUpChained, REFUnoInt)->RangeMultiplier(10)->Range(100, 10000000);
BENCHMARK_TEMPLATE(LookUpChained, AMCInt)->RangeMultiplier(10)->Range(100, 10000000);

#ifdef AMC_SMALLSET
BENCHMARK_TEMPLATE(CommonUsage, amc::SmallSet<uint32_t, 50>, 50);
BENCHMARK_TEMPLATE(CommonUsage, std::unordered_set<uint32_t>, 50);
#endif
}  // namespace amc

BENCHMARK_MAIN();
