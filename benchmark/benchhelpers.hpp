#pragma once

#include <benchmark/benchmark.h>

#include <cstdint>

#include "testtypes.hpp"

namespace amc {
constexpr uint32_t kMaxValue = 1000U;

void PrintStats(benchmark::State &state) {
  const TypeStats &stats = TypeStats::_stats;

  size_t nbIt = state.iterations();
  state.counters["Cons"] = static_cast<double>(stats._nbConstructs) / nbIt;
  state.counters["Dest"] = static_cast<double>(stats._nbDestructs) / nbIt;
  state.counters["CpyC"] = static_cast<double>(stats._nbCopyConstructs) / nbIt;
  state.counters["CpyA"] = static_cast<double>(stats._nbCopyAssignments) / nbIt;
  state.counters["MovC"] = static_cast<double>(stats._nbMoveConstructs) / nbIt;
  state.counters["MovA"] = static_cast<double>(stats._nbMoveAssignments) / nbIt;
  state.counters["Allc"] = static_cast<double>(stats._nbMallocs) / nbIt;
  state.counters["Real"] = static_cast<double>(stats._nbReallocs) / nbIt;
  state.counters["Free"] = static_cast<double>(stats._nbFree) / nbIt;
}

}  // namespace amc
