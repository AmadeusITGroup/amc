#include <benchmark/benchmark.h>

#include <array>
#include <cstdint>
#include <numeric>
#include <vector>

#include "amc_fixedcapacityvector.hpp"
#include "amc_smallvector.hpp"
#include "amc_vector.hpp"
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
    uint64_t insertPos = hashs % v.size();
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

  const SizeType kMaxSize = 10000U;
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
          v.insert(v.end(), i, static_cast<ValueType>(value % kMaxValue));
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

}  // namespace

BENCHMARK_TEMPLATE(AssignRandom, REFRelocType);
BENCHMARK_TEMPLATE(AssignRandom, AMCRelocType);

BENCHMARK_TEMPLATE(SwapRandom, REFRelocType);
BENCHMARK_TEMPLATE(SwapRandom, AMCRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFRelocType);
BENCHMARK_TEMPLATE(EraseRandom, AMCRelocType);

BENCHMARK_TEMPLATE(InsertNElemsRandom, REFRelocType);
BENCHMARK_TEMPLATE(InsertNElemsRandom, AMCRelocType);

BENCHMARK_TEMPLATE(InsertFromPointerRandom, REFRelocType);
BENCHMARK_TEMPLATE(InsertFromPointerRandom, AMCRelocType);

BENCHMARK_TEMPLATE(InsertFromForwardItRandom, REFRelocType);
BENCHMARK_TEMPLATE(InsertFromForwardItRandom, AMCRelocType);

BENCHMARK_TEMPLATE(Growing, REFRelocType);
BENCHMARK_TEMPLATE(Growing, AMCRelocType);

BENCHMARK_TEMPLATE(AssignRandom, REFInt);
BENCHMARK_TEMPLATE(AssignRandom, AMCInt);

BENCHMARK_TEMPLATE(SwapRandom, REFInt);
BENCHMARK_TEMPLATE(SwapRandom, AMCInt);

BENCHMARK_TEMPLATE(EraseRandom, REFInt);
BENCHMARK_TEMPLATE(EraseRandom, AMCInt);

BENCHMARK_TEMPLATE(InsertNElemsRandom, REFInt);
BENCHMARK_TEMPLATE(InsertNElemsRandom, AMCInt);

BENCHMARK_TEMPLATE(InsertFromPointerRandom, REFInt);
BENCHMARK_TEMPLATE(InsertFromPointerRandom, AMCInt);

BENCHMARK_TEMPLATE(InsertFromForwardItRandom, REFInt);
BENCHMARK_TEMPLATE(InsertFromForwardItRandom, AMCInt);

BENCHMARK_TEMPLATE(Growing, REFInt);
BENCHMARK_TEMPLATE(Growing, AMCInt);

BENCHMARK_TEMPLATE(AssignRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(AssignRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(SwapRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(SwapRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(EraseRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(InsertNElemsRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(InsertNElemsRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(InsertFromPointerRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(InsertFromPointerRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(InsertFromForwardItRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(InsertFromForwardItRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(Growing, REFNonRelocType);
BENCHMARK_TEMPLATE(Growing, AMCNonRelocType);

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
