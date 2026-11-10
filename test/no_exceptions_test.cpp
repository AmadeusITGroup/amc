// The headers compiled with exceptions disabled (-fno-exceptions): the errors which would throw abort the program, and
// the containers otherwise work as usual, element types whose operations throw excepted.
// GoogleTest needs exceptions, so this test is a plain executable, reporting failures with its exit code.

#include <algorithm>
#include <amc/algorithm.hpp>
#include <amc/allocator.hpp>
#include <amc/config.hpp>
#include <amc/fixedcapacityvector.hpp>
#include <amc/flatset.hpp>
#include <amc/memory.hpp>
#include <amc/smallvector.hpp>
#include <amc/type_traits.hpp>
#include <amc/vector.hpp>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#ifdef AMC_CXX17
#include <amc/smallset.hpp>
#endif

namespace {

int gNbFailures = 0;

void Check(bool condition, const char* description) {
  if (!condition) {
    std::fprintf(stderr, "FAILED: %s\n", description);
    ++gNbFailures;
  }
}

template <class VectorType>
bool Equals(const VectorType& v, const std::vector<std::string>& ref) {
  return v.size() == ref.size() && std::equal(v.begin(), v.end(), ref.begin());
}

/// Grows, inserts into, erases from, copies and swaps a vector of strings, which are not trivially relocatable,
/// comparing it with a std::vector.
template <class VectorType>
void CheckVectorOfStrings(const char* description) {
  const std::vector<std::string> source{"one", "two", std::string(40, 'x'), "four"};
  VectorType v;
  std::vector<std::string> ref;
  for (int i = 0; i < 40; ++i) {
    const std::string s(static_cast<std::size_t>(i % 30), static_cast<char>('a' + i % 26));
    v.push_back(s);
    ref.push_back(s);
  }
  v.insert(v.begin() + 3, 5, std::string("inserted"));
  ref.insert(ref.begin() + 3, 5, std::string("inserted"));
  v.insert(v.begin(), source.begin(), source.end());
  ref.insert(ref.begin(), source.begin(), source.end());
  v.emplace(v.begin() + 7, 3, 'z');
  ref.emplace(ref.begin() + 7, 3, 'z');
  v.erase(v.begin() + 1, v.begin() + 9);
  ref.erase(ref.begin() + 1, ref.begin() + 9);
  Check(Equals(v, ref), description);

  VectorType copy(v);
  const std::vector<std::string> copyRef = ref;
  v.resize(70, std::string("resized"));
  ref.resize(70, std::string("resized"));
  copy.swap(v);
  copy.shrink_to_fit();
  Check(Equals(copy, ref) && Equals(v, copyRef), description);
}

/// Fills and empties a vector of unique_ptr, which owns memory.
template <class VectorType>
void CheckVectorOfUniquePtrs(const char* description) {
  VectorType v;
  for (int i = 0; i < 100; ++i) {
    v.push_back(std::unique_ptr<int>(new int(i)));
  }
  v.erase(v.begin(), v.begin() + 50);
  v.insert(v.begin(), std::unique_ptr<int>(new int(-1)));
  bool ok = v.size() == 51U && *v.front() == -1;
  for (int i = 1; i < 51; ++i) {
    ok = ok && *v[static_cast<typename VectorType::size_type>(i)] == 49 + i;
  }
  Check(ok, description);
}

/// The exit code once the expected abort happened: the result of the previous checks.
void ExitAfterExpectedAbort(int) { std::_Exit(gNbFailures == 0 ? EXIT_SUCCESS : EXIT_FAILURE); }

}  // namespace

int main() {
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS)
  std::fprintf(stderr, "FAILED: compiled with exceptions\n");
  return EXIT_FAILURE;
#endif
  CheckVectorOfStrings<amc::vector<std::string>>("vector of strings");
  CheckVectorOfStrings<amc::vector<std::string, std::allocator<std::string>>>("vector of strings, std::allocator");
  CheckVectorOfStrings<amc::SmallVector<std::string, 4>>("SmallVector of strings");
  CheckVectorOfUniquePtrs<amc::vector<std::unique_ptr<int>>>("vector of unique_ptr");
  CheckVectorOfUniquePtrs<amc::SmallVector<std::unique_ptr<int>, 3>>("SmallVector of unique_ptr");

  amc::FixedCapacityVector<int, 10, amc::vec::UncheckedGrowingPolicy> fixed(3, 42);
  fixed.insert(fixed.begin(), {1, 2});
  fixed.erase(fixed.begin() + 1);
  Check(fixed == amc::FixedCapacityVector<int, 10, amc::vec::UncheckedGrowingPolicy>({1, 42, 42, 42}),
        "FixedCapacityVector with UncheckedGrowingPolicy");

  amc::inplace_vector<std::string, 3> inplace;
  inplace.emplace_back("abc");
  inplace.push_back("def");
  Check(inplace.try_emplace_back("ghi") != nullptr && inplace.try_emplace_back("jkl") == nullptr, "inplace_vector");

  amc::FlatSet<std::string> flatSet;
  for (int i = 0; i < 30; ++i) {
    flatSet.insert(std::to_string(i % 20));
  }
  flatSet.erase("5");
  Check(flatSet.size() == 19U && flatSet.count("19") == 1U && flatSet.count("5") == 0U, "FlatSet of strings");

#ifdef AMC_CXX17
  amc::SmallSet<std::string, 4> smallSet;
  for (int i = 0; i < 10; ++i) {
    smallSet.insert(std::to_string(i % 7));
  }
  smallSet.erase("3");
  Check(smallSet.size() == 6U && smallSet.count("6") == 1U && smallSet.count("3") == 0U, "SmallSet of strings");
#endif

  // An error which would throw aborts the program: the handler exits with the result of the checks above.
  if (std::signal(SIGABRT, ExitAfterExpectedAbort) == SIG_ERR) {
    std::fprintf(stderr, "FAILED: cannot install the SIGABRT handler\n");
    return EXIT_FAILURE;
  }
  amc::vector<int> v(2, 0);
  const int value = v.at(2);
  std::fprintf(stderr, "FAILED: at() out of range did not abort the program (%d)\n", value);
  return EXIT_FAILURE;
}
