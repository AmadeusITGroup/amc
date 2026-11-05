// Translation unit of module_test including the amc headers, like the parts of a program which do not import the
// module, and then importing it: the module exports the same entities as the headers, in the same translation unit and
// across translation units (module_test.cpp calls these functions).
#include <amc/flatset.hpp>
#include <amc/smallvector.hpp>
#include <cstddef>
#include <string>

import amc;

amc::SmallVector<std::string, 2> MakeStringsWithHeaders(int n);
std::size_t SizeWithHeaders(const amc::FlatSet<int>& s);

amc::SmallVector<std::string, 2> MakeStringsWithHeaders(int n) {
  amc::SmallVector<std::string, 2> v;
  for (int i = 0; i < n; ++i) {
    v.emplace_back(32U, static_cast<char>('a' + i));
  }
  return v;
}

std::size_t SizeWithHeaders(const amc::FlatSet<int>& s) { return s.size(); }
