# External dependencies (linked with FetchContent)
include(FetchContent)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        origin/master
)

# Do not test google benchmark
set (BENCHMARK_ENABLE_INSTALL OFF)
set (BENCHMARK_ENABLE_TESTING OFF)

FetchContent_MakeAvailable(googlebenchmark)

function(add_bench name)
  set(options)
  set(oneValueArgs)
  set(multiValueArgs)
  cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")
  add_executable(${name} ${MY_UNPARSED_ARGUMENTS})

  target_include_directories(${name} PRIVATE ../include)
  target_include_directories(${name} PRIVATE ../test)
  target_link_libraries(${name} PRIVATE benchmark::benchmark)
  if (AMC_ENABLE_ASAN)
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
