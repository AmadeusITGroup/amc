
find_package(benchmark CONFIG)

if (NOT benchmark_FOUND)
  include(FetchContent)

  message(STATUS "Could not find local installation of google/benchmark, fetching sources")

  # Do not test google benchmark
  set (BENCHMARK_ENABLE_INSTALL OFF)
  set (BENCHMARK_ENABLE_TESTING OFF)

  FetchContent_Declare(
    googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.6.0
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
  if (AMC_ENABLE_ASAN AND NOT MSVC)
    target_link_options(${name} PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=float-divide-by-zero)
  endif()
endfunction()

add_bench(
  vectors_benchmark
  vectors_benchmark.cpp
)

add_bench(
  sets_benchmark
  sets_benchmark.cpp
)
