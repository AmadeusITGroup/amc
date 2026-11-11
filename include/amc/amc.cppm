/// The amc C++20 module: 'import amc;' provides all the containers of the amc headers, without their parsing cost.
///
/// The headers are included in the global module fragment, and their public names are exported with using
/// declarations: the entities are the same as the ones of the headers, so a program may both import the module and
/// include the headers, in different translation units or in the same one (preferably including the headers first).
///
/// Build the module with the options of the translation units importing it: same C++ standard (C++20 or later),
/// exceptions enabled or disabled alike (without them, the errors abort instead of throwing; Clang refuses a mismatch),
/// and the macros changing the definitions of the classes and traits defined or not like in the whole program, as
/// compilers do not diagnose a mismatch: AMC_NONSTD_FEATURES, AMC_NO_STD_TRIVIALLY_RELOCATABLE,
/// AMC_STD_STRING_TRIVIALLY_RELOCATABLE and AMC_STD_DEQUE_TRIVIALLY_RELOCATABLE (the CMake options define the last
/// three for the targets linking amc::amc, amc::module included). Macros are not exported: include <amc/config.hpp> for
/// the AMC_* ones, and the standard headers for the std names.
module;

#include "config.hpp"

#ifndef AMC_CXX20
#error "The amc module needs C++20"
#endif

#include "allocator.hpp"
#include "fixedcapacityvector.hpp"
#include "flatset.hpp"
#include "memory.hpp"
#include "smallset.hpp"
#include "smallvector.hpp"
#include "type_traits.hpp"
#include "vector.hpp"
#include "vectorcommon.hpp"

export module amc;

export namespace amc {
// Containers. Vector is the class template of all the vectors: the other names are alias templates of it.
using amc::FixedCapacityVector;
using amc::FlatSet;
using amc::inplace_vector;
using amc::SmallSet;
using amc::SmallVector;
using amc::swap;
using amc::vector;
using amc::Vector;

// Allocators
using amc::allocator;
using amc::BasicAllocatorWrapper;
using amc::BasicSingletonAllocatorAdaptor;
using amc::SimpleAllocator;

// Trivially relocatable types: the trait, to specialize for the types which cannot declare it, and the relocation
// helpers
using amc::is_trivially_relocatable;
using amc::is_trivially_relocatable_v;
using amc::relocate_at;
using amc::uninitialized_relocate;
using amc::uninitialized_relocate_n;
}  // namespace amc

// Growing policies of the vectors
export namespace amc::vec {
using amc::vec::BadAllocGrowingPolicy;
using amc::vec::DynamicGrowingPolicy;
using amc::vec::ExceptionGrowingPolicy;
using amc::vec::UncheckedGrowingPolicy;
}  // namespace amc::vec
