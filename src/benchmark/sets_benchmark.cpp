#include <benchmark/benchmark.h>

#include <set>
#include <unordered_set>

#include "amc_fixedcapacityvector.hpp"
#include "amc_flatset.hpp"
#include "amc_smallvector.hpp"
#include "benchhelpers.hpp"
#include "testhelpers.hpp"
#include "testtypes.hpp"

#ifdef AMC_CXX17
#include "amc_smallset.hpp"
#endif

namespace amc {
TypeStats TypeStats::_stats;

namespace {

typedef std::set<ComplexTriviallyRelocatableType> REFRelocType;
typedef std::set<ComplexNonTriviallyRelocatableType> REFNonRelocType;
typedef std::set<uint32_t> REFInt;
typedef std::unordered_set<uint32_t> REFUnoInt;

typedef amc::FlatSet<ComplexTriviallyRelocatableType> AMCRelocType;
typedef amc::FlatSet<ComplexNonTriviallyRelocatableType> AMCNonRelocType;
typedef amc::FlatSet<uint32_t> AMCInt;

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

template <class SetType, unsigned InitNbInserts>
void EraseRandom(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  uint32_t s = 0;
  SetType elems;
  typedef typename SetType::value_type ValueType;
  std::vector<ValueType> remainingElems;
  for (uint32_t i = 0; i < InitNbInserts; ++i) {
    elems.emplace(i);
    if (remainingElems.empty()) {
      remainingElems.emplace_back(i);
    } else {
      remainingElems.emplace(remainingElems.begin() + (HashValue64(++s) % remainingElems.size()), i);
    }
  }
  TypeStats::_stats.start();
  for (auto _ : state) {
    SetType v = elems;
    while (!remainingElems.empty() && !v.empty()) {
      ValueType elemToRemove = std::move(remainingElems.back());
      remainingElems.pop_back();
      v.erase(elemToRemove);
    }
  }
  TypeStats::_stats.end();
  PrintStats(state);
}

template <class SetType, unsigned Size>
void LookUp(benchmark::State &state) {
  TypeStats::_stats = TypeStats();
  uint32_t s = 0;
  SetType elems;
  typedef typename SetType::value_type ValueType;
  for (uint32_t i = 0; i < Size; ++i) {
    elems.emplace(i);
  }
  TypeStats::_stats.start();
  uint32_t out = 0;
  for (auto _ : state) {
    ValueType vToLookFor(HashValue64(++s) % Size);
    if (elems.find(vToLookFor) != elems.end()) {
      ++out;
    }
    benchmark::DoNotOptimize(out);
  }
  TypeStats::_stats.end();
  PrintStats(state);
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
  TypeStats::_stats.end();
  PrintStats(state);
}

}  // namespace

BENCHMARK_TEMPLATE(InsertRandom, REFRelocType);
BENCHMARK_TEMPLATE(InsertRandom, AMCRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFRelocType, 1000);
BENCHMARK_TEMPLATE(EraseRandom, AMCRelocType, 1000);

BENCHMARK_TEMPLATE(LookUp, REFRelocType, 1000000);
BENCHMARK_TEMPLATE(LookUp, AMCRelocType, 1000000);

BENCHMARK_TEMPLATE(InsertRandom, REFNonRelocType);
BENCHMARK_TEMPLATE(InsertRandom, AMCNonRelocType);

BENCHMARK_TEMPLATE(EraseRandom, REFNonRelocType, 1000);
BENCHMARK_TEMPLATE(EraseRandom, AMCNonRelocType, 1000);

BENCHMARK_TEMPLATE(LookUp, REFNonRelocType, 100000);
BENCHMARK_TEMPLATE(LookUp, AMCNonRelocType, 100000);

BENCHMARK_TEMPLATE(InsertRandom, REFInt);
BENCHMARK_TEMPLATE(InsertRandom, REFUnoInt);
BENCHMARK_TEMPLATE(InsertRandom, AMCInt);

BENCHMARK_TEMPLATE(EraseRandom, REFInt, 100000);
BENCHMARK_TEMPLATE(EraseRandom, REFUnoInt, 100000);
BENCHMARK_TEMPLATE(EraseRandom, AMCInt, 100000);

BENCHMARK_TEMPLATE(LookUp, REFInt, 100000);
BENCHMARK_TEMPLATE(LookUp, REFUnoInt, 100000);
BENCHMARK_TEMPLATE(LookUp, AMCInt, 100000);

#ifdef AMC_CXX17
BENCHMARK_TEMPLATE(CommonUsage, amc::SmallSet<uint32_t, 50>, 50);
BENCHMARK_TEMPLATE(CommonUsage, std::unordered_set<uint32_t>, 50);
#endif
}  // namespace amc

BENCHMARK_MAIN();
