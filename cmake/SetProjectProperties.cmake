# Usage: AmcSetProjectProperties(<target> [NO_NONSTD_FEATURES] [MODULE])
#
# Compilation options of the targets built by amc itself (unit tests, benchmarks). They are set on these targets only,
# never globally: the projects using amc and the dependencies of amc (GoogleTest, Google Benchmark) are not affected.
# NO_NONSTD_FEATURES: do not define AMC_NONSTD_FEATURES, for the module and its test, which take it from
#                     AMC_MODULE_NONSTD_FEATURES instead.
# MODULE: the target builds the module or imports it, and CMake builds a BMI of the module with the options of each of
#         its importers. Not with the undefined behavior sanitizer for GCC, which crashes writing the BMI of instrumented
#         inline functions (GCC 14.2 to 15.3, internal compiler error in cp/module.cc, even for a module only re-exporting
#         std::vector).
function(AmcSetProjectProperties name)
  cmake_parse_arguments(PARSE_ARGV 1 MY "NO_NONSTD_FEATURES;MODULE" "" "")

  # Non standard features are only enabled for our targets: clients define AMC_NONSTD_FEATURES themselves if they want
  # them (see README)
  if (NOT AMC_PEDANTIC AND NOT MY_NO_NONSTD_FEATURES)
    target_compile_definitions(${name} PRIVATE AMC_NONSTD_FEATURES)
  endif()

  # Warnings. They are only treated as errors in Debug: with optimizations, GCC reports false positives
  # (-Warray-bounds, -Wstringop-overflow, -Wmaybe-uninitialized...).
  if (MSVC)
    set(options /W4 /bigobj)
    if (AMC_WARNINGS_AS_ERRORS)
      list(APPEND options $<$<CONFIG:Debug>:/WX>)
    endif()
  else()
    set(options -Wall -Wextra -pedantic)
    if (AMC_WARNINGS_AS_ERRORS)
      list(APPEND options $<$<CONFIG:Debug>:-Werror>)
    endif()
  endif()

  # Address and Undefined Behavior sanitizers
  if (AMC_ENABLE_ASAN AND NOT MSVC)
    set(sanitizer_options -fsanitize=address)
    if (NOT (MY_MODULE AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU"))
      list(APPEND sanitizer_options -fsanitize=undefined -fsanitize=float-divide-by-zero)
    endif()
    list(APPEND options -g ${sanitizer_options} -fno-sanitize-recover=all)
    target_link_options(${name} PRIVATE ${sanitizer_options})
  endif()

  # Only in the build tree: the installed module target exports the compile options of the module, with which its
  # importers build its BMI, and these ones are not for them.
  target_compile_options(${name} PRIVATE "$<BUILD_INTERFACE:${options}>")
endfunction()
