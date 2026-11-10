#pragma once

#if defined(__clang__) && defined(__clang_minor__)
#define AMC_CLANG (__clang_major__ * 10000 + __clang_minor__ * 100 + __clang_patchlevel__)
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) && defined(__GNUC_PATCHLEVEL__)
#define AMC_GCC (__GNUC__ * 10000 + __GNUC_MINOR__ * 100 + __GNUC_PATCHLEVEL__)
#endif

#if defined(AMC_GCC) && AMC_GCC < 40600
#define AMC_PUSH_WARNING
#define AMC_POP_WARNING
#else
#define AMC_PUSH_WARNING _Pragma("GCC diagnostic push")
#define AMC_POP_WARNING _Pragma("GCC diagnostic pop")
#endif
#define AMC_DISABLE_WARNING_INTERNAL(warningName) #warningName
#define AMC_DISABLE_WARNING(warningName) _Pragma(AMC_DISABLE_WARNING_INTERNAL(GCC diagnostic ignored warningName))
#ifdef AMC_CLANG
#define AMC_CLANG_DISABLE_WARNING(warningName) AMC_DISABLE_WARNING(warningName)
#define AMC_GCC_DISABLE_WARNING(warningName)
#else
#define AMC_CLANG_DISABLE_WARNING(warningName)
#define AMC_GCC_DISABLE_WARNING(warningName) AMC_DISABLE_WARNING(warningName)
#endif

#if defined(__GNUC__)
#define AMC_LIKELY(x) (__builtin_expect(!!(x), 1))
#define AMC_UNLIKELY(x) (__builtin_expect(!!(x), 0))
#else
#define AMC_LIKELY(x) (!!(x))
#define AMC_UNLIKELY(x) (!!(x))
#endif

// Hints that a condition is random: Clang then compiles it into a conditional move rather than a branch (whose
// prediction would fail half of the time), which it does not otherwise in loops. Not parenthesized: Clang only takes
// the hint when the builtin call is directly the condition of the 'if'. Only defined if the compiler supports it.
#ifdef __has_builtin
#if __has_builtin(__builtin_unpredictable)
#define AMC_UNPREDICTABLE(x) __builtin_unpredictable(!!(x))
#endif
#endif

// Asks the CPU to start loading the cache line of 'addr', which will be read soon. It has no other effect.
#if defined(__GNUC__) || defined(__clang__)
#define AMC_PREFETCH(addr) __builtin_prefetch(addr)
#elif defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64)) && !defined(_M_ARM64EC)
#include <xmmintrin.h>
#define AMC_PREFETCH(addr) _mm_prefetch(reinterpret_cast<const char*>(addr), _MM_HINT_T0)
#elif defined(_MSC_VER) && defined(_M_ARM64)
#include <intrin.h>
#define AMC_PREFETCH(addr) __prefetch(addr)
#else
#define AMC_PREFETCH(addr) static_cast<void>(addr)
#endif

// With GCC, compiles a function out of line, with all the calls of its body inlined into it, for the loops which move
// elements one by one (types which are not trivially relocatable). Inlined into its callers, such a loop depends on how
// big they are: GCC (13 and 15 at least) then inlines the move assignment of the elements (like the one of std::string)
// into some of them only, and the loop is up to 2.4 times slower in the other ones. Out of line, its code is also
// shared by all the vectors of a same element type. The other compilers keep an inline function: Clang inlines the
// moves into the loop anyway, and out of line, the call makes the insertion into small vectors up to 10 % slower.
#ifdef AMC_GCC
#define AMC_FLATTEN_NOINLINE __attribute__((noinline, flatten))
#else
#define AMC_FLATTEN_NOINLINE inline
#endif

#if defined(_MSC_VER)

#define AMC_CXX14 1

#if _MSVC_LANG >= 201703L
#define AMC_CXX17 1
#define AMC_SMALLSET 1
#endif

#if _MSVC_LANG >= 202002L
#define AMC_CXX20 1
#endif

#if _MSVC_LANG > 202002L
#define AMC_CXX23 1
#endif

#else
#if __cplusplus >= 201402L
#define AMC_CXX14 1
#endif

#if __cplusplus >= 201703L
#define AMC_CXX17 1
#define AMC_SMALLSET 1
#endif

#if __cplusplus >= 202002L
#define AMC_CXX20 1
#endif

#if __cplusplus > 202002L
#define AMC_CXX23 1
#endif

#endif

// Exceptions may be disabled (-fno-exceptions for GCC and Clang, no /EH option for MSVC): the errors which throw then
// abort the program, and the clean-up code which rethrows the exceptions of the elements is never executed. AMC_TRY
// and AMC_CATCH_ALL become an 'if (true)' and its 'else', a single statement in both modes. AMC_RETHROW then aborts
// too, so that a 'try' block returning a value does not fall off the end of its function in the 'else' branch.
#if defined(__cpp_exceptions) || defined(__EXCEPTIONS) || defined(_CPPUNWIND)
#define AMC_TRY try
#define AMC_CATCH_ALL catch (...)
#define AMC_RETHROW throw
#define AMC_THROW(exception) throw exception
#else
#include <cstdlib>
#define AMC_TRY if (true)
#define AMC_CATCH_ALL else
#define AMC_RETHROW std::abort()
#define AMC_THROW(exception) std::abort()
#endif

// Namespace scope constants of the headers: inline from C++17, which gives them external linkage (the C++20 module must
// not expose entities with internal linkage to the code importing it).
#ifdef AMC_CXX17
#define AMC_INLINE_VARIABLE inline
#else
#define AMC_INLINE_VARIABLE
#endif
