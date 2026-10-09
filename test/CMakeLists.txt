

find_package(GTest CONFIG)
if (NOT GTest_FOUND)

  message(STATUS "Could not find local installation of Gtest, fetching sources")

  include(FetchContent)

  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.17.0.tar.gz
    URL_HASH SHA256=65fab701d9829d38cb77c14acdc431d2108bfdbf8979e40eb8ae567edf10b27c
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )

  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

if (AMC_ENABLE_COVERAGE)
  if (NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang" OR MSVC)
    message(FATAL_ERROR "AMC_ENABLE_COVERAGE requires Clang (LLVM source-based code coverage)")
  endif()

  # The LLVM tools must match the compiler version: search next to the real compiler first (/usr/lib/llvm-<N>/bin,
  # Xcode toolchain), then for the versioned names of the apt.llvm.org packages (llvm-cov-<N>).
  string(REGEX MATCH "^[0-9]+" AMC_CLANG_MAJOR "${CMAKE_CXX_COMPILER_VERSION}")
  get_filename_component(AMC_LLVM_BIN_DIR "${CMAKE_CXX_COMPILER}" REALPATH)
  get_filename_component(AMC_LLVM_BIN_DIR "${AMC_LLVM_BIN_DIR}" DIRECTORY)
  find_program(AMC_LLVM_PROFDATA NAMES llvm-profdata-${AMC_CLANG_MAJOR} llvm-profdata HINTS ${AMC_LLVM_BIN_DIR})
  find_program(AMC_LLVM_COV NAMES llvm-cov-${AMC_CLANG_MAJOR} llvm-cov HINTS ${AMC_LLVM_BIN_DIR})
  find_program(AMC_LLVM_CXXFILT NAMES llvm-cxxfilt-${AMC_CLANG_MAJOR} llvm-cxxfilt c++filt HINTS ${AMC_LLVM_BIN_DIR})
  if (NOT AMC_LLVM_PROFDATA OR NOT AMC_LLVM_COV)
    message(FATAL_ERROR "AMC_ENABLE_COVERAGE requires llvm-profdata and llvm-cov (package llvm-${AMC_CLANG_MAJOR}), "
                        "set AMC_LLVM_PROFDATA and AMC_LLVM_COV to their paths if they are not found automatically")
  endif()

  set(AMC_COVERAGE_DIR ${PROJECT_BINARY_DIR}/coverage)
  message(STATUS "AMC - Code coverage with ${AMC_LLVM_COV}, run target 'coverage' for a report in ${AMC_COVERAGE_DIR}")
endif()

# Create a unit test with google test.
# NO_GTEST: plain executable reporting failures with its exit code, for tests compiled with a standard older than C++17
#           (that google test requires), set with CXX_STANDARD.
function(add_unit_test name)
   set(options NO_GTEST)
   set(oneValueArgs CXX_STANDARD)
   set(multiValueArgs)
   cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")

   add_executable(${name} ${MY_UNPARSED_ARGUMENTS})
   if (NOT MY_NO_GTEST)
     target_link_libraries(${name} PRIVATE GTest::gtest GTest::gmock GTest::gmock_main)
   endif()
   if (MY_CXX_STANDARD)
     set_target_properties(${name} PROPERTIES CXX_STANDARD ${MY_CXX_STANDARD} CXX_STANDARD_REQUIRED ON)
   endif()
   target_include_directories(${name} PRIVATE ../include)

   add_test(NAME ${name} COMMAND ${name})
   if (AMC_ENABLE_ASAN AND NOT MSVC)
     set_tests_properties(${name} PROPERTIES
                          ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1 abort_on_error=1 print_stacktrace=1; LSAN_OPTIONS=detect_leaks=1 malloc_context_size=2 print_suppressions=0"
                          WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY})
     target_link_options(${name} PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=float-divide-by-zero)
   endif()
   if (AMC_ENABLE_COVERAGE)
     # Only the tests are instrumented, not GoogleTest. NDEBUG, otherwise each assert counts as a branch never taken.
     target_compile_definitions(${name} PRIVATE NDEBUG)
     target_compile_options(${name} PRIVATE -fprofile-instr-generate -fcoverage-mapping)
     target_link_options(${name} PRIVATE -fprofile-instr-generate)
     # One raw profile per process (%p), in a directory that the 'coverage' target merges as a whole
     set_property(TEST ${name} APPEND PROPERTY
                  ENVIRONMENT "LLVM_PROFILE_FILE=${AMC_COVERAGE_DIR}/profraw/${name}-%p.profraw")
   endif()
endfunction()

add_unit_test(
  amc_isdetected_test
  amc_isdetected_test.cpp
)

add_unit_test(
  vectors_test
  vectors_test.cpp
)

add_unit_test(
  sets_test
  sets_test.cpp
)

# amc emulations of standard library features, only used before C++17 (C++14 is the oldest standard of MSVC)
add_unit_test(
  cxx11_test
  cxx11_test.cpp
  NO_GTEST
  CXX_STANDARD 11
)
if (NOT MSVC)
  # A missing return statement is undefined behavior, make it a compilation error (it is an error by default for MSVC)
  target_compile_options(cxx11_test PRIVATE -Werror=return-type)
endif()

if (AMC_ENABLE_COVERAGE)
  # 'coverage' target: run all the tests from scratch, then report the coverage of the headers of include/ only.
  # Outputs in AMC_COVERAGE_DIR: html/index.html, amc.lcov (for Codecov) and report.txt (summary per file).
  # llvm-cov warns that "N functions have mismatched data": header functions that a test binary does not use get a
  # placeholder record, which conflicts with the real one of another binary. Harmless, the real record is kept.
  get_directory_property(amc_tests TESTS)
  set(amc_cov_args -instr-profile=${AMC_COVERAGE_DIR}/amc.profdata)
  foreach(test ${amc_tests})
    list(APPEND amc_cov_args -object $<TARGET_FILE:${test}>)
  endforeach()
  list(APPEND amc_cov_args -sources ${PROJECT_SOURCE_DIR}/include)

  set(amc_cov_show_args -show-branches=count -show-line-counts)
  if (AMC_LLVM_CXXFILT)
    list(APPEND amc_cov_show_args -Xdemangler=${AMC_LLVM_CXXFILT})
  endif()

  add_custom_target(coverage
    COMMAND ${CMAKE_COMMAND} -E remove_directory ${AMC_COVERAGE_DIR}
    COMMAND ${CMAKE_CTEST_COMMAND} -C $<CONFIG> --output-on-failure
    COMMAND ${AMC_LLVM_PROFDATA} merge -sparse ${AMC_COVERAGE_DIR}/profraw -o ${AMC_COVERAGE_DIR}/amc.profdata
    COMMAND ${AMC_LLVM_COV} export ${amc_cov_args} -format=lcov > ${AMC_COVERAGE_DIR}/amc.lcov
    COMMAND ${AMC_LLVM_COV} show ${amc_cov_args} ${amc_cov_show_args} -format=html -output-dir=${AMC_COVERAGE_DIR}/html
    COMMAND ${AMC_LLVM_COV} report ${amc_cov_args} > ${AMC_COVERAGE_DIR}/report.txt
    COMMAND ${AMC_LLVM_COV} report ${amc_cov_args}
    COMMAND ${CMAKE_COMMAND} -E echo "HTML report: ${AMC_COVERAGE_DIR}/html/index.html"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running the unit tests for code coverage"
    USES_TERMINAL
    VERBATIM)
  add_dependencies(coverage ${amc_tests})
endif()
