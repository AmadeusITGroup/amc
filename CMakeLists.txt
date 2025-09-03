cmake_minimum_required (VERSION 3.14)

project(amc 
  VERSION 2.6.0
  DESCRIPTION "Header base library of C++ containers"
  LANGUAGES CXX
)

# This library needs C++11, except for SmallSet which requires std::variant from C++17.
# Emulations of newer C++ functions is provided for non C++14 / C++17 compilers.
set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard")
set(CMAKE_CXX_EXTENSIONS OFF CACHE STRING "Deactivate C++ extensions for maximum portability")

##
## MAIN_PROJECT CHECK
## determine if amc is built as a subproject or if it is the main project
##
set(MAIN_PROJECT OFF)
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  set(MAIN_PROJECT ON)
endif()

# By default, build benchmarks only if not Debug and main project (benchs do not make sense in Debug)
set(ASAN_BUILD OFF)
set(BENCH_BUILD ${MAIN_PROJECT})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(ASAN_BUILD ON)
  set(BENCH_BUILD OFF)
endif()

option(AMC_ENABLE_TESTS "Build the unit tests" ${MAIN_PROJECT})
option(AMC_ENABLE_BENCHMARKS "Build the benchmarks" ${BENCH_BUILD})
option(AMC_ENABLE_ASAN "Compile with AddressSanitizer" ${ASAN_BUILD})
option(AMC_PEDANTIC "If set, only standard API (in respect to STL) will be defined. Additional features are enabled when OFF" OFF)

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

if (AMC_PEDANTIC)
  message(STATUS "AMC - Compile without non standard features")
else()
  message(STATUS "AMC - Compile with non standard features")
  add_compile_definitions(AMC_NONSTD_FEATURES)
endif()

# Create interface library for header files
include(GNUInstallDirs)

add_library(${PROJECT_NAME} INTERFACE)
# add alias so the project can be uses with add_subdirectory
add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_include_directories(
  ${PROJECT_NAME}
  INTERFACE $<BUILD_INTERFACE:${${PROJECT_NAME}_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_compile_features(${PROJECT_NAME} INTERFACE cxx_std_11)

if (AMC_ENABLE_TESTS)
  if (MSVC)
    add_definitions(/bigobj)
  endif()

  enable_testing()
  
  add_subdirectory(test)
endif()

if (AMC_ENABLE_BENCHMARKS)
  add_subdirectory(benchmark)
endif()

install(TARGETS ${PROJECT_NAME}
        EXPORT ${PROJECT_NAME}_Targets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
  "${PROJECT_NAME}ConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
  "${PROJECT_SOURCE_DIR}/cmake/${PROJECT_NAME}Config.cmake.in"
  "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
  INSTALL_DESTINATION
  ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake
)

install(EXPORT ${PROJECT_NAME}_Targets
        FILE ${PROJECT_NAME}Targets.cmake
        NAMESPACE ${PROJECT_NAME}::
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
              "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME} DESTINATION include)
