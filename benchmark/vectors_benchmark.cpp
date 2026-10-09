#include <benchmark/benchmark.h>

#include <amc/fixedcapacityvector.hpp>
#include <amc/smallvector.hpp>
#include <amc/vector.hpp>
#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

#include "benchhelpers.hpp"
#include "testhelpers.hpp"
#include "testtypes.hpp"

namespace amc {

TypeStats TypeStats::_stats;

namespace {

using AMCRelocType = amc::vector<ComplexTriviallyRelocatableType>;
using AMCNonRelocType = amc::vector<ComplexNonTriviallyRelocatableType>;
using AMCInt = amc::vector<uint32_t>;

using REFRelocType = std::vector<ComplexTriviallyRelocatableType>;
using REFNonRelocType = std::vector<ComplexNonTriviallyRelocatableType>;
using REFInt = std::vector<uint32_t>;

/// Inline capacity of the SmallVector of the SmallSizes benchmark.
constexpr uint32_t kSmallSize = 16;

using AMCSmallRelocType = amc::SmallVector<ComplexTriviallyRelocatableType, kSmallSize>;
using AMCSmallNonRelocType = amc::SmallVector<ComplexNonTriviallyRelocatableType, kSmallSize>;
using AMCSmallInt = amc::SmallVector<uint32_t, kSmallSize>;

template <class VecType>
void InsertNElemsRandom(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  VecType v(1, 0);
  uint32_t s = 2U;
  TypeStats::_stats.start();
  for (auto _ : state) {
    uint32_t i = s % 20U;
    uint32_t oldSize = static_cast<uint32_t>(v.size());
    uintmax_t hash = HashValue64(i);
    uint32_t count = static_cast<uint32_t>(hash % static_cast<uintmax_t>(2));
    uint32_t value = static_cast<uint32_t>(hash % kMaxValue);
    v.insert(v.begin() + (hash % oldSize), count, value);
    v.push_back(value);
    v.pop_back();
    v.insert(v.end() - 1, value);
    v.erase(v.end() - 2);
    v.erase(v.end() - (v.size() - oldSize), v.end());
    v.push_back(value);
    ++s;
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

template <class VecType>
void InsertFromPointerRandom(benchmark::State &state) {
  using ValueType = typename VecType::value_type;
  TypeStats::_stats = TypeStats();
  VecType v(1, 0);
  std::array<ValueType, kMaxValue - 10> kTab;
  std::iota(kTab.begin(), kTab.end(), 10);
  uint32_t s = 0;
  for (auto _ : state) {
    uint32_t i = s % 20U;
    uint32_t oldSize = static_cast<uint32_t>(v.size());
    uintmax_t hash = HashValue64(i);
    TypeStats::_stats.start();
    v.insert(v.begin() + (hash % v.size()), kTab.begin() + (hash % kTab.size()), kTab.end());
    v.erase(v.end() - (v.size() - oldSize), v.end());
    v.push_back(hash % kMaxValue);
    TypeStats::_stats.end();
    ++s;
  }
  PrintStats(state);
}

template <class VecType>
void InsertFromForwardItRandom(benchmark::State &state) {
  using ValueType = typename VecType::value_type;
  TypeStats::_stats = TypeStats();
  VecType v(1, 0);
  std::array<ValueType, kMaxValue - 10> kTab;
  std::set<ValueType> kSet;
  std::iota(kTab.begin(), kTab.end(), 10);
  kSet.insert(kTab.begin(), kTab.end());
  uint32_t s = 0;
  for (auto _ : state) {
    uint32_t i = s % 20U;
    uint64_t hashs = HashValue64(i);
    TypeStats::_stats.start();
    typename std::set<ValueType>::const_iterator first = std::next(kSet.begin(), hashs % kSet.size());
    auto nElemsToInsert = std::distance(first, kSet.end());
    const auto insertPos = static_cast<std::ptrdiff_t>(hashs % v.size());
    v.insert(v.begin() + insertPos, first, kSet.end());
    typename VecType::iterator vpos = v.begin() + insertPos;
    v.erase(vpos, vpos + nElemsToInsert);
    v.push_back(i);
    TypeStats::_stats.end();
    ++s;
  }
  PrintStats(state);
}

template <class VecType>
void EraseRandom(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  VecType v(1, 0);
  uint32_t s = 2U;
  for (auto _ : state) {
    uint32_t i = s % 20U;
    uint32_t oldSize = static_cast<uint32_t>(v.size());
    uintmax_t hashs = HashValue64(i);
    uint32_t count = static_cast<uint32_t>(hashs % static_cast<uintmax_t>(s));
    uint32_t value = static_cast<uint32_t>(hashs % kMaxValue);
    TypeStats::_stats.start();
    v.insert(v.end(), count, value);
    v.erase(v.begin(), v.begin() + (v.size() - oldSize));
    v.push_back(oldSize % kMaxValue);
    TypeStats::_stats.end();
    ++s;
  }
  PrintStats(state);
}

template <class VecType>
void AssignRandom(benchmark::State &state) {
  using ValueType = typename VecType::value_type;
  TypeStats::_stats = TypeStats();
  VecType v;
  std::iota(v.begin(), v.end(), 0);
  std::array<ValueType, kMaxValue - 1> kTab;
  std::iota(kTab.begin(), kTab.end(), 1);
  uint32_t s = 0;
  for (auto _ : state) {
    uint32_t i = s % 20U;
    uintmax_t hashs = HashValue64(i);
    TypeStats::_stats.start();
    if (hashs % 2 == 0) {
      v.assign(kTab.begin() + (hashs % kTab.size()), kTab.end());
    } else {
      v.assign(i, kTab[hashs % kTab.size()]);
    }
    TypeStats::_stats.end();
    ++s;
  }
  PrintStats(state);
}

template <class VecType>
void SwapRandom(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  VecType v;
  uint32_t s = 0;
  for (auto _ : state) {
    uint32_t i = 10U + s % 20U;
    VecType v2(i, 0);
    std::iota(v2.begin(), v2.end(), 10);
    TypeStats::_stats.start();
    v2.swap(v);
    TypeStats::_stats.end();
    ++s;
  }
  PrintStats(state);
}

template <class VecType>
void Growing(benchmark::State &state) {
  using SizeType = typename VecType::size_type;
  TypeStats::_stats = TypeStats();

  const SizeType kMaxSize = 1000000U;
  TypeStats::_stats.start();
  for (auto _ : state) {
    VecType v;
    for (SizeType s = 0; s < kMaxSize; s = v.size()) {
      SizeType i = 1U + v.size() / 8U;
      uint32_t value = static_cast<uint32_t>(HashValue64(i) % kMaxValue);
      v.emplace_back(value);
    }
  }
  TypeStats::_stats.end();

  PrintStats(state);
}

template <class VecType, unsigned TypicalMaxSize>
void CommonUsage(benchmark::State &state) {
  using ValueType = typename VecType::value_type;
  TypeStats::_stats = TypeStats();

  uint32_t seed = 0;
  for (auto _ : state) {
    VecType v;
    for (uint32_t s = 0; s < TypicalMaxSize; s = v.size()) {
      auto i = 1U + v.size() / 8U;
      TypeStats::_stats.start();
      uint64_t value = HashValue64(++seed);
      switch (value % 5) {
        case 0:
          v.insert(v.end(), static_cast<typename VecType::size_type>(i), static_cast<ValueType>(value % kMaxValue));
          break;
        case 1:
          if (v.size() > 1) {
            v.erase(v.begin());
          } else {
            v.emplace_back(value % kMaxValue);
          }
          break;
        default:
          v.emplace_back(value % kMaxValue);
          break;
      }
      int sum = 0;
      for (ValueType e : v) {
        sum += e;
      }
      v.back() = sum % kMaxValue;
    }
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

/// Life of a vector: filled by emplace_back, read, then destroyed.
/// state.range(0) % of the vectors are small, with 1 to kSmallSize elements, the others have kSmallSize + 1 to
/// 4 * kSmallSize elements (sizes are uniformly distributed in both ranges).
template <class VecType>
void SmallSizes(benchmark::State &state) {
  using ValueType = typename VecType::value_type;
  TypeStats::_stats = TypeStats();
  const auto smallPercent = static_cast<uint64_t>(state.range(0));
  uint64_t s = 0;
  uint32_t sum = 0;
  TypeStats::_stats.start();
  for (auto _ : state) {
    const uint64_t hash = HashValue64(++s);
    const auto r = static_cast<uint32_t>(hash >> 32);
    const uint32_t size = hash % 100U < smallPercent ? 1U + r % kSmallSize : kSmallSize + 1U + r % (3U * kSmallSize);
    VecType v;
    for (uint32_t i = 0; i < size; ++i) {
      v.emplace_back(i);
    }
    // Without it, the compiler could remove the allocations of a vector which does not escape
    benchmark::DoNotOptimize(v.data());
    for (const ValueType &e : v) {
      sum += static_cast<uint32_t>(e);
    }
    benchmark::DoNotOptimize(sum);
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

/// Percentages of small vectors of the SmallSizes benchmark.
void SmallSizesArgs(benchmark::internal::Benchmark *b) {
  for (int smallPercent : {0, 50, 80, 90, 95, 99, 100}) {
    b->Arg(smallPercent);
  }
}

}  // namespace

// The vector of these benchmarks grows at each iteration: with the adaptive number of iterations of Google Benchmark,
// the fastest container would be measured on a bigger vector. A fixed number makes std and amc do the same work.
constexpr benchmark::IterationCount kNbIterations = 10000;

BENCHMARK_TEMPLATE(AssignRandom, REFRelocType);
BENCHMARK_TEMPLATE(AssignRandom, AMCRelocType);

BENCHMARK_TEMPLATE(SwapRandom, REFRelocType);
BENCHMARK_TEMPLATE(SwapRandom, AMCRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(EraseRandom, AMCRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertNElemsRandom, REFRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertNElemsRandom, AMCRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertFromPointerRandom, REFRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertFromPointerRandom, AMCRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertFromForwardItRandom, REFRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertFromForwardItRandom, AMCRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(Growing, REFRelocType);
BENCHMARK_TEMPLATE(Growing, AMCRelocType);

BENCHMARK_TEMPLATE(AssignRandom, REFInt);
BENCHMARK_TEMPLATE(AssignRandom, AMCInt);

BENCHMARK_TEMPLATE(SwapRandom, REFInt);
BENCHMARK_TEMPLATE(SwapRandom, AMCInt);

BENCHMARK_TEMPLATE(EraseRandom, REFInt)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(EraseRandom, AMCInt)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertNElemsRandom, REFInt)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertNElemsRandom, AMCInt)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertFromPointerRandom, REFInt)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertFromPointerRandom, AMCInt)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertFromForwardItRandom, REFInt)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertFromForwardItRandom, AMCInt)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(Growing, REFInt);
BENCHMARK_TEMPLATE(Growing, AMCInt);

BENCHMARK_TEMPLATE(AssignRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(AssignRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(SwapRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(SwapRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFNonRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(EraseRandom, AMCNonRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertNElemsRandom, REFNonRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertNElemsRandom, AMCNonRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertFromPointerRandom, REFNonRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertFromPointerRandom, AMCNonRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(InsertFromForwardItRandom, REFNonRelocType)->Iterations(kNbIterations);
BENCHMARK_TEMPLATE(InsertFromForwardItRandom, AMCNonRelocType)->Iterations(kNbIterations);

BENCHMARK_TEMPLATE(Growing, REFNonRelocType);
BENCHMARK_TEMPLATE(Growing, AMCNonRelocType);

BENCHMARK_TEMPLATE(SmallSizes, REFRelocType)->Apply(SmallSizesArgs);
BENCHMARK_TEMPLATE(SmallSizes, AMCRelocType)->Apply(SmallSizesArgs);
BENCHMARK_TEMPLATE(SmallSizes, AMCSmallRelocType)->Apply(SmallSizesArgs);

BENCHMARK_TEMPLATE(SmallSizes, REFInt)->Apply(SmallSizesArgs);
BENCHMARK_TEMPLATE(SmallSizes, AMCInt)->Apply(SmallSizesArgs);
BENCHMARK_TEMPLATE(SmallSizes, AMCSmallInt)->Apply(SmallSizesArgs);

BENCHMARK_TEMPLATE(SmallSizes, REFNonRelocType)->Apply(SmallSizesArgs);
BENCHMARK_TEMPLATE(SmallSizes, AMCNonRelocType)->Apply(SmallSizesArgs);
BENCHMARK_TEMPLATE(SmallSizes, AMCSmallNonRelocType)->Apply(SmallSizesArgs);

BENCHMARK_TEMPLATE(CommonUsage, amc::vector<int>, 30);
BENCHMARK_TEMPLATE(CommonUsage, amc::SmallVector<int, 32>, 30);
BENCHMARK_TEMPLATE(CommonUsage, amc::FixedCapacityVector<int, 40>, 30);

BENCHMARK_TEMPLATE(CommonUsage, amc::vector<ComplexTriviallyRelocatableType>, 60);
BENCHMARK_TEMPLATE(CommonUsage, amc::SmallVector<ComplexTriviallyRelocatableType, 64>, 60);
BENCHMARK_TEMPLATE(CommonUsage, amc::FixedCapacityVector<ComplexTriviallyRelocatableType, 80>, 60);

BENCHMARK_TEMPLATE(CommonUsage, amc::vector<ComplexNonTriviallyRelocatableType>, 100);
BENCHMARK_TEMPLATE(CommonUsage, amc::SmallVector<ComplexNonTriviallyRelocatableType, 100>, 100);

}  // namespace amc

BENCHMARK_MAIN();
