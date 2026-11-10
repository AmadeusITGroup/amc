# 3.21: PROJECT_IS_TOP_LEVEL, and no MSVC warning flag (/W3) in the default CMAKE_CXX_FLAGS (policy CMP0092): the
# warning level is set on our targets only
cmake_minimum_required (VERSION 3.21)

project(amc
  VERSION 2.8.1
  DESCRIPTION "Header base library of C++ containers"
  LANGUAGES CXX
)

# This library needs C++11, except for SmallSet which requires std::variant from C++17.
# Emulations of newer C++ functions is provided for non C++14 / C++17 compilers.
# Default standard of the unit tests and benchmarks, not set for a project including amc: C++20, the standard of the
# module, in which SmallSet and FlatSet are faster.
if (PROJECT_IS_TOP_LEVEL)
  set(CMAKE_CXX_STANDARD 20 CACHE STRING "C++ standard")
  set(CMAKE_CXX_EXTENSIONS OFF CACHE STRING "Deactivate C++ extensions for maximum portability")
endif()

# By default, build benchmarks only if not Debug and main project (benchs do not make sense in Debug)
set(BENCH_BUILD ${PROJECT_IS_TOP_LEVEL})
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(BENCH_BUILD OFF)
endif()

option(AMC_ENABLE_TESTS "Build the unit tests" ${PROJECT_IS_TOP_LEVEL})
option(AMC_ENABLE_BENCHMARKS "Build the benchmarks" ${BENCH_BUILD})
option(AMC_ENABLE_ASAN "Compile with AddressSanitizer" OFF)
option(AMC_ENABLE_COVERAGE "Instrument the unit tests for LLVM source-based code coverage and add a 'coverage' target (Clang only)" OFF)
option(AMC_PEDANTIC "If set, only standard API (in respect to STL) will be defined. Additional features are enabled when OFF" OFF)
option(AMC_WARNINGS_AS_ERRORS "Treat compiler warnings as errors in Debug builds of the unit tests and benchmarks" OFF)
option(AMC_ENABLE_MODULE "Add the amc::module target, providing the amc C++20 module (CMake 3.28 or later)" OFF)
option(AMC_MODULE_NONSTD_FEATURES "Build the amc module with the non standard extensions, and define AMC_NONSTD_FEATURES for the targets linking amc::module" OFF)
option(AMC_STD_TRIVIALLY_RELOCATABLE "Declare trivially relocatable the standard library types which are in libstdc++, libc++ and the MSVC STL (std::unique_ptr, std::vector...). OFF defines AMC_NO_STD_TRIVIALLY_RELOCATABLE for the targets linking amc" ON)
option(AMC_STD_STRING_TRIVIALLY_RELOCATABLE "Declare std::basic_string trivially relocatable with the standard libraries in which it is (libc++, MSVC STL without iterator debugging, old ABI of libstdc++), defining AMC_STD_STRING_TRIVIALLY_RELOCATABLE for the targets linking amc" OFF)

if (AMC_PEDANTIC)
  message(STATUS "[amc] Compile without non standard features")
else()
  message(STATUS "[amc] Compile with non standard features")
endif()
message(STATUS "[amc] Standard types trivially relocatable: ${AMC_STD_TRIVIALLY_RELOCATABLE}, std::basic_string: ${AMC_STD_STRING_TRIVIALLY_RELOCATABLE}")

# Compilation options of the unit tests and benchmarks (AmcSetProjectProperties), never set globally
include(cmake/SetProjectProperties.cmake)

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

# They change amc::is_trivially_relocatable: defined for all the targets linking amc (and for the module), as the whole
# program must see the same trait
if (NOT AMC_STD_TRIVIALLY_RELOCATABLE)
  target_compile_definitions(${PROJECT_NAME} INTERFACE AMC_NO_STD_TRIVIALLY_RELOCATABLE)
endif()
if (AMC_STD_STRING_TRIVIALLY_RELOCATABLE)
  target_compile_definitions(${PROJECT_NAME} INTERFACE AMC_STD_STRING_TRIVIALLY_RELOCATABLE)
endif()

# C++20 module 'amc' ('import amc;'): a static library holding the module interface unit, whose BMI is built in the
# build of its consumers. Static, as it has nothing to export from a shared library.
if (AMC_ENABLE_MODULE)
  if (CMAKE_VERSION VERSION_LESS 3.28)
    message(FATAL_ERROR "AMC_ENABLE_MODULE requires CMake 3.28 or later (C++20 modules), not ${CMAKE_VERSION}")
  endif()
  add_library(${PROJECT_NAME}_module STATIC)
  add_library(${PROJECT_NAME}::module ALIAS ${PROJECT_NAME}_module)
  set_target_properties(${PROJECT_NAME}_module PROPERTIES EXPORT_NAME module)
  target_sources(${PROJECT_NAME}_module
                 PUBLIC FILE_SET CXX_MODULES
                        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
                        FILES ${PROJECT_SOURCE_DIR}/include/amc/amc.cppm)
  target_compile_features(${PROJECT_NAME}_module PUBLIC cxx_std_20)
  # The importers can include the headers too: they must see the same class definitions
  target_link_libraries(${PROJECT_NAME}_module PUBLIC ${PROJECT_NAME})
  if (AMC_MODULE_NONSTD_FEATURES)
    target_compile_definitions(${PROJECT_NAME}_module PUBLIC AMC_NONSTD_FEATURES)
  endif()
  if (AMC_ENABLE_TESTS)
    # Same warnings and sanitizers as the test importing it (the BMI must be compatible with it)
    AmcSetProjectProperties(${PROJECT_NAME}_module NO_NONSTD_FEATURES MODULE)
  endif()
  message(STATUS "[amc] C++20 module 'amc' (target amc::module), non standard features: ${AMC_MODULE_NONSTD_FEATURES}")
endif()

if (AMC_ENABLE_TESTS)
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

set(AMC_EXPORT_MODULE_ARGS)
if (AMC_ENABLE_MODULE)
  install(TARGETS ${PROJECT_NAME}_module
          EXPORT ${PROJECT_NAME}_Targets
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
          FILE_SET CXX_MODULES DESTINATION include)  # include/amc/amc.cppm, next to the headers
  set(AMC_EXPORT_MODULE_ARGS CXX_MODULES_DIRECTORY cxx-modules)
endif()

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
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake
        ${AMC_EXPORT_MODULE_ARGS})

install(FILES "${PROJECT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
              "${PROJECT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/${PROJECT_NAME}/cmake)

install(DIRECTORY ${PROJECT_SOURCE_DIR}/include/${PROJECT_NAME} DESTINATION include)
