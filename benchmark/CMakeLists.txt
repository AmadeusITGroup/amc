
find_package(benchmark CONFIG)

if (NOT benchmark_FOUND)
  include(FetchContent)

  message(STATUS "Could not find local installation of google/benchmark, fetching sources")

  # Do not test google benchmark
  set (BENCHMARK_ENABLE_INSTALL OFF)
  set (BENCHMARK_ENABLE_TESTING OFF)

  FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.9.4.tar.gz
    URL_HASH SHA256=b334658edd35efcf06a99d9be21e4e93e092bd5f95074c1673d5c8705d95c104
  )

  FetchContent_MakeAvailable(googlebenchmark)
endif()

function(add_bench name)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs)
  cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")
  add_executable(${name} ${MY_UNPARSED_ARGUMENTS})

  target_include_directories(${name} PRIVATE ../include)
  target_include_directories(${name} PRIVATE ../test)
  target_link_libraries(${name} PRIVATE benchmark::benchmark)
  AmcSetProjectProperties(${name})
endfunction()

add_bench(
  vectors_benchmark
  vectors_benchmark.cpp
)

add_bench(
  sets_benchmark
  sets_benchmark.cpp
)
