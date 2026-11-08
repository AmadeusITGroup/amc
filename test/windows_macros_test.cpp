// The Windows headers define macros with common names (small, near, far, interface...), and programs often include
// them before amc: the headers cannot use these names for their own identifiers. min and max are not covered, the
// headers need NOMINMAX on Windows.
// Only Windows has the real macros, the other platforms define the same names to detect a collision in any build.
// The containers are used rather than just included as, without /permissive-, MSVC only parses instantiated templates.
// This is a plain executable, reporting failures with its exit code: only the amc headers should see the macros.

#ifdef _WIN32

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#else

// Included before the macros: unlike the C++ standard headers, the C system headers may use these names
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <stdexcept>

#define small char
#define hyper long long
#define near
#define far
#define pascal
#define cdecl
#define interface struct
#define IN
#define OUT
#define OPTIONAL
#define CONST const
#define VOID void
#define ERROR 0
#define DELETE 0x00010000L
#define TRANSPARENT 1
#define OPAQUE 2

#endif

#include <amc/algorithm.hpp>
#include <amc/allocator.hpp>
#include <amc/config.hpp>
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/hasreallocate.hpp>
#include <amc/isdetected.hpp>
#include <amc/istransparent.hpp>
#include <amc/memory.hpp>
#include <amc/smallvector.hpp>
#include <amc/type_traits.hpp>
#include <amc/utility.hpp>
#include <amc/vector.hpp>
#include <amc/vectorcommon.hpp>

#ifdef AMC_CXX17
#include <amc/setnode.hpp>
#include <amc/smallset.hpp>
#endif

int main() {
  amc::vector<int> vector;
  vector.push_back(1);

  amc::SmallVector<int, 2> smallVector;
  smallVector.push_back(1);

  amc::FixedCapacityVector<int, 2> fixedCapacityVector;
  fixedCapacityVector.push_back(1);

  amc::FlatSet<int> flatSet;
  flatSet.insert(1);

  bool success =
      vector.size() == 1 && smallVector.size() == 1 && fixedCapacityVector.size() == 1 && flatSet.size() == 1;

#ifdef AMC_CXX17
  amc::SmallSet<int, 2> smallSet;
  amc::SmallSet<int, 2> otherSmallSet;
  otherSmallSet.insert(1);
  smallSet.merge(otherSmallSet);

  success = success && smallSet.size() == 1 && otherSmallSet.empty();
#endif

  return success ? 0 : 1;
}
