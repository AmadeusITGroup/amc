# Usage: AmcSetProjectProperties(<target>)
#
# Compilation options of the targets built by amc itself (unit tests, benchmarks). They are set on these targets only,
# never globally: the projects using amc and the dependencies of amc (GoogleTest, Google Benchmark) are not affected.
function(AmcSetProjectProperties name)
  # Non standard features are only enabled for our targets: clients define AMC_NONSTD_FEATURES themselves if they want
  # them (see README)
  if (NOT AMC_PEDANTIC)
    target_compile_definitions(${name} PRIVATE AMC_NONSTD_FEATURES)
  endif()

  # Warnings. They are only treated as errors in Debug: with optimizations, GCC reports false positives
  # (-Warray-bounds, -Wstringop-overflow, -Wmaybe-uninitialized...).
  if (MSVC)
    target_compile_options(${name} PRIVATE /W4 /bigobj)
    if (AMC_WARNINGS_AS_ERRORS)
      target_compile_options(${name} PRIVATE $<$<CONFIG:Debug>:/WX>)
    endif()
  else()
    target_compile_options(${name} PRIVATE -Wall -Wextra -pedantic)
    if (AMC_WARNINGS_AS_ERRORS)
      target_compile_options(${name} PRIVATE $<$<CONFIG:Debug>:-Werror>)
    endif()
  endif()

  # Address and Undefined Behavior sanitizers
  if (AMC_ENABLE_ASAN AND NOT MSVC)
    set(sanitizer_options -fsanitize=address -fsanitize=undefined -fsanitize=float-divide-by-zero)
    target_compile_options(${name} PRIVATE -g ${sanitizer_options} -fno-sanitize-recover=all)
    target_link_options(${name} PRIVATE ${sanitizer_options})
  endif()
endfunction()
