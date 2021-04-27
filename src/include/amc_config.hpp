#pragma once

#include <cstddef>

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

#if defined(_MSC_VER)
#if _MSVC_LANG > 201703L
#define AMC_CXX20 1
#endif

#if _MSVC_LANG >= 201703L
#define AMC_CXX17 1
#endif

#define AMC_CXX14 1

#else
#if __cplusplus >= 201402L
#define AMC_CXX14 1
#endif

#if __cplusplus >= 201703L
#define AMC_CXX17 1
#endif

#if __cplusplus > 201703L
#define AMC_CXX20 1
#endif
#endif
