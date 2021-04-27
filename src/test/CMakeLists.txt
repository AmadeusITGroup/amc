# External dependencies (linked with FetchContent)
include(FetchContent)

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        origin/master
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Create a unit test with google test
function(add_unit_test name)
   set(options)
   set(oneValueArgs)
   set(multiValueArgs)
   cmake_parse_arguments(PARSE_ARGV 1 MY "${options}" "${oneValueArgs}" "${multiValueArgs}")

   add_executable(${name} ${MY_UNPARSED_ARGUMENTS})
   target_link_libraries(${name} PRIVATE gtest gmock gmock_main)
   target_include_directories(${name} PRIVATE ../include)

   add_test(NAME ${name} COMMAND ${name})
   set_tests_properties(${name} PROPERTIES
                        ENVIRONMENT "UBSAN_OPTIONS=halt_on_error=1 abort_on_error=1 print_stacktrace=1; LSAN_OPTIONS=detect_leaks=1 malloc_context_size=2 print_suppressions=0"
                        WORKING_DIRECTORY ${CMAKE_HOME_DIRECTORY})
endfunction()

add_unit_test(
  vectors_test
  vectors_test.cpp
)

add_unit_test(
  sets_test
  sets_test.cpp
)