cmake_minimum_required (VERSION 3.14)

set(AMC_VERSION 1.2)

project(amc VERSION ${AMC_VERSION}
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

target_include_directories(amc INTERFACE 
  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src/include>
  $<INSTALL_INTERFACE:src/include>
)

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

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    "${PROJECT_BINARY_DIR}/amcConfigVersion.cmake"
    VERSION ${AMC_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(TARGETS amc
    EXPORT amcTargets
    LIBRARY DESTINATION lib COMPONENT Runtime
    ARCHIVE DESTINATION lib COMPONENT Development
    RUNTIME DESTINATION bin COMPONENT Runtime
    PUBLIC_HEADER DESTINATION include COMPONENT Development
    BUNDLE DESTINATION bin COMPONENT Runtime
)

configure_package_config_file(
    "${PROJECT_SOURCE_DIR}/cmake/amcConfig.cmake.in"
    "${PROJECT_BINARY_DIR}/amcConfig.cmake"
    INSTALL_DESTINATION lib/cmake/amc
)

install(EXPORT amcTargets DESTINATION lib/cmake/amc)
install(FILES "${PROJECT_BINARY_DIR}/amcConfigVersion.cmake"
              "${PROJECT_BINARY_DIR}/amcConfig.cmake"
        DESTINATION lib/cmake/amc)
install(DIRECTORY ${PROJECT_SOURCE_DIR}/src/include DESTINATION include)
