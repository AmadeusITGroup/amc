

find_package(GTest CONFIG)
if (NOT GTest_FOUND)

  message(STATUS "Could not find local installation of Gtest, fetching sources")

  include(FetchContent)

  FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        release-1.11.0
  )

  # For Windows: Prevent overriding the parent project's compiler/linker settings
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

  FetchContent_MakeAvailable(googletest)
endif()

enable_testing()

# Create a unit test with google test
function(add_unit_test name)
   set(options)
   set(oneValueArgs)
   set(multiValueArgs)
   cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")

   add_executable(${name} ${MY_UNPARSED_ARGUMENTS})
   target_link_libraries(${name} PRIVATE GTest::gtest GTest::gmock GTest::gmock_main)
   target_include_directories(${name} PRIVATE ../include)

   add_test(NAME ${name} COMMAND ${name})
   if (AMC_ENABLE_ASAN AND NOT MSVC)
     set_tests_properties(${name} PROPERTIES
                          ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1 abort_on_error=1 print_stacktrace=1; LSAN_OPTIONS=detect_leaks=1 malloc_context_size=2 print_suppressions=0"
                          WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY})
     target_link_options(${name} PRIVATE -fsanitize=address -fsanitize=undefined -fsanitize=float-divide-by-zero)
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