#pragma once

/// Injection of allocation failures (as if out of memory) in the C allocator, to test the behavior of the containers
/// when an allocation fails, whatever the size of the allocation.
///
/// It replaces malloc, calloc and realloc of the test executable, forwarding them to glibc unless failures are enabled.
/// Hence this header defines functions with external linkage: include it in a single translation unit per executable.
///
/// Only available with glibc and without AddressSanitizer / ThreadSanitizer (which replace these functions themselves),
/// tests using it should be skipped when kMallocFailureInjection is false.

#include <atomic>
#include <cerrno>
#include <cstdlib>

#if defined(__GLIBC__) && !defined(__SANITIZE_ADDRESS__) && !defined(__SANITIZE_THREAD__)
#define AMC_TEST_MALLOC_FAILURE_INJECTION 1
#if defined(__clang__) && defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || __has_feature(memory_sanitizer)
#undef AMC_TEST_MALLOC_FAILURE_INJECTION
#endif
#endif
#endif

namespace amc {

#ifdef AMC_TEST_MALLOC_FAILURE_INJECTION
constexpr bool kMallocFailureInjection = true;
#else
constexpr bool kMallocFailureInjection = false;
#endif

/// Number of allocations that still succeed before all the next ones fail, negative if allocations never fail.
inline std::atomic<int>& NbAllocationsBeforeFailure() {
  static std::atomic<int> nbAllocationsBeforeFailure(-1);
  return nbAllocationsBeforeFailure;
}

/// Tells whether the current allocation fails, counting it.
inline bool MallocFails() {
  std::atomic<int>& nbAllocationsBeforeFailure = NbAllocationsBeforeFailure();
  int nb = nbAllocationsBeforeFailure.load(std::memory_order_relaxed);
  while (nb > 0 && !nbAllocationsBeforeFailure.compare_exchange_weak(nb, nb - 1, std::memory_order_relaxed)) {
  }
  return nb == 0;
}

/// Calls 'func' with the allocations of the C allocator (malloc, calloc and realloc) failing, after the first
/// 'nbSuccessfulAllocations' ones. Allocations succeed again before this function returns or throws.
template <class Func>
void CallWithFailingMalloc(Func&& func, int nbSuccessfulAllocations = 0) {
  NbAllocationsBeforeFailure().store(nbSuccessfulAllocations, std::memory_order_relaxed);
  try {
    func();
  } catch (...) {
    NbAllocationsBeforeFailure().store(-1, std::memory_order_relaxed);
    throw;
  }
  NbAllocationsBeforeFailure().store(-1, std::memory_order_relaxed);
}

}  // namespace amc

#ifdef AMC_TEST_MALLOC_FAILURE_INJECTION
extern "C" {
void* __libc_malloc(size_t size) noexcept;
void* __libc_calloc(size_t nmemb, size_t size) noexcept;
void* __libc_realloc(void* ptr, size_t size) noexcept;

// calloc is replaced as well, as compilers may merge malloc and memset into a call to calloc.
// free does not need to be replaced: the memory always comes from glibc.
void* malloc(size_t size) noexcept {
  if (amc::MallocFails()) {
    errno = ENOMEM;
    return nullptr;
  }
  return __libc_malloc(size);
}

void* calloc(size_t nmemb, size_t size) noexcept {
  if (amc::MallocFails()) {
    errno = ENOMEM;
    return nullptr;
  }
  return __libc_calloc(nmemb, size);
}

// On failure, the memory block 'ptr' is left untouched, as specified.
void* realloc(void* ptr, size_t size) noexcept {
  if (amc::MallocFails()) {
    errno = ENOMEM;
    return nullptr;
  }
  return __libc_realloc(ptr, size);
}
}
#endif
