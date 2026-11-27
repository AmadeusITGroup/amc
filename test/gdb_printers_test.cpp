#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/smallset.hpp>
#include <amc/smallvector.hpp>
#include <amc/vector.hpp>
#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define AMC_GDB_TEST_NOINLINE __attribute__((noinline))
#else
#define AMC_GDB_TEST_NOINLINE
#endif

extern "C" AMC_GDB_TEST_NOINLINE void amc_gdb_test_breakpoint() {
  volatile int sink = 0;
  sink = sink + 1;
}

int main() {
  // Vectors: amc::vector, SmallVector (inline, grown to a dynamic storage, back inline), FixedCapacityVector and
  // inplace_vector (64-bit capacities: SmallVector<int, 4> stores 4 elements inline, SmallVector<char, 8> 8).
  amc::vector<int> emptyVector;
  amc::vector<int> intVector{1, 2, 3};
  intVector.reserve(8);
  amc::vector<std::string> stringVector{"alpha", "beta"};
  amc::SmallVector<int, 4> emptySmallVector;
  amc::SmallVector<int, 4> smallVectorInline{1, 2, 3};
  amc::SmallVector<int, 4> smallVectorHeap{1, 2, 3, 4};
  smallVectorHeap.push_back(5);  // the capacity doubles
  smallVectorHeap.push_back(6);
  amc::SmallVector<int, 4> smallVectorBackInline{1, 2, 3, 4, 5};
  smallVectorBackInline.resize(2);
  smallVectorBackInline.shrink_to_fit();
  amc::SmallVector<char, 8> charVector;
  charVector.push_back('a');
  charVector.push_back('b');
  charVector.push_back('c');
  amc::FixedCapacityVector<int, 5> fixedVector{4, 5};
  amc::FixedCapacityVector<int, 3> emptyFixedVector;
  amc::inplace_vector<int, 4> inplaceVector{7};
  amc::vector<amc::SmallVector<int, 2>> nestedVector{{1, 2}, {3}};
  amc::vector<int> largeVector;
  for (int i = 0; i < 1000; ++i) {
    largeVector.push_back(i);
  }

  // FlatSet in amc vectors and in a std::vector.
  amc::FlatSet<int> emptySortedSet;
  amc::FlatSet<int> sortedSet{5, 1, 3};
  amc::FlatSet<std::string> sortedStringSet{"beta", "alpha"};
  amc::FlatSet<int, std::less<int>, amc::allocator<int>, amc::SmallVector<int, 4>> sortedSetInline{2, 1};
  amc::FlatSet<int, std::less<int>, std::allocator<int>, std::vector<int>> sortedSetStdVector{9, 8};

  // SmallSet inline (insertion order), in its std::set or FlatSet, and back inline once emptied.
  amc::SmallSet<int, 4> emptyInlineSet;
  amc::SmallSet<int, 4> inlineSet{3, 1, 2};
  amc::SmallSet<int, 2> largeSet{5, 4, 6};
  amc::SmallSet<int, 2, std::less<int>, amc::allocator<int>, amc::FlatSet<int>> largeFlatSet{5, 4, 6};
  amc::SmallSet<std::string, 2> inlineStringSet{"one"};
  amc::SmallSet<int, 2> backInlineSet{1, 2, 3};
  backInlineSet.erase(1);
  backInlineSet.erase(2);
  backInlineSet.erase(3);
  auto itInlineSet = inlineSet.find(1);
  auto itLargeSet = largeSet.find(5);

  // Node handles of the sets.
  amc::FlatSet<int>::node_type emptyNode;
  amc::FlatSet<int> extractSet{10, 20};
  auto setNode = extractSet.extract(10);
  amc::SmallSet<std::string, 2> extractStringSet{"x"};
  auto stringNode = extractStringSet.extract("x");

  amc_gdb_test_breakpoint();

  // Keep all variables live past the breakpoint.
  const std::size_t total = emptyVector.size() + intVector.size() + stringVector.size() + emptySmallVector.size() +
                            smallVectorInline.size() + smallVectorHeap.size() + smallVectorBackInline.size() +
                            charVector.size() + fixedVector.size() + emptyFixedVector.size() + inplaceVector.size() +
                            nestedVector.size() + largeVector.size() + emptySortedSet.size() + sortedSet.size() +
                            sortedStringSet.size() + sortedSetInline.size() + sortedSetStdVector.size() +
                            emptyInlineSet.size() + inlineSet.size() + largeSet.size() + largeFlatSet.size() +
                            inlineStringSet.size() + backInlineSet.size() + (itInlineSet != inlineSet.end() ? 1U : 0U) +
                            (itLargeSet != largeSet.end() ? 1U : 0U) + (emptyNode.empty() ? 1U : 0U) +
                            (setNode.empty() ? 0U : 1U) + (stringNode.empty() ? 0U : 1U);
  return total > 0 ? 0 : 1;
}
