cmake_minimum_required (VERSION 3.14)
project(amc VERSION 1.1
            DESCRIPTION "Header base library of C++ containers"
            LANGUAGES CXX)

# This library needs C++11, except for SmallSet which requires std::variant from C++17.
# Emulations of newer C++ functions is provided for non C++14 / C++17 compilers.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS OFF)

##
## MAIN_PROJECT CHECK
## determine if amc is built as a subproject or if it is the main project
##
set(MAIN_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(MAIN_PROJECT ON)
endif()

# By default, build benchmarks only if not Debug and main project
set(ASAN_BUILD OFF)
set(BENCH_BUILD ${MAIN_PROJECT})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(ASAN_BUILD ON)
  set(BENCH_BUILD OFF)
endif()

option(AMC_ENABLE_TESTS "Build the unit tests" ${MAIN_PROJECT})
option(AMC_ENABLE_BENCHMARKS "Build the benchmarks" ${BENCH_BUILD})
option(AMC_ENABLE_ASAN "Compile with AddressSanitizer" ${ASAN_BUILD})

# Activate all warnings, in all build modes
if(MAIN_PROJECT)
  if (MSVC)
    add_compile_options(/W4)
  else()
    add_compile_options(-Wall -Wextra -pedantic)
  endif()
endif()

if(AMC_ENABLE_ASAN AND NOT MSVC)
  add_compile_options(-g -fsanitize=address -fsanitize=undefined -fsanitize=float-divide-by-zero -fno-sanitize-recover=all)
endif()

# Create interface library for header files
add_library(amc INTERFACE)
add_library(amc::amc ALIAS amc)

target_include_directories(amc INTERFACE src/include)

if (AMC_ENABLE_TESTS)
  if (MSVC)
    add_definitions(/bigobj)
  endif()

  enable_testing()
  
  add_subdirectory(src/test)
endif()

if (AMC_ENABLE_BENCHMARKS)
  add_subdirectory(src/benchmark)
endif()
