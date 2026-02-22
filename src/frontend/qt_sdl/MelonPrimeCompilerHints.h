#ifndef MELONPRIME_COMPILER_HINTS_H
#define MELONPRIME_COMPILER_HINTS_H

// =========================================================================
// Shared compiler hint macros for MelonPrime modules.
//
// Previously duplicated between MelonPrime.h and MelonPrimeRawInputState.h.
// Centralised here to guarantee consistency and eliminate ODR/redefinition
// issues across translation units.
// =========================================================================

#ifndef FORCE_INLINE
#  if defined(_MSC_VER)
#    define FORCE_INLINE __forceinline
#  elif defined(__GNUC__) || defined(__clang__)
#    define FORCE_INLINE __attribute__((always_inline)) inline
#  else
#    define FORCE_INLINE inline
#  endif
#endif

#ifndef NOINLINE
#  if defined(_MSC_VER)
#    define NOINLINE __declspec(noinline)
#  elif defined(__GNUC__) || defined(__clang__)
#    define NOINLINE __attribute__((noinline))
#  else
#    define NOINLINE
#  endif
#endif

#ifndef HOT_FUNCTION
#  if defined(__GNUC__) || defined(__clang__)
#    define HOT_FUNCTION __attribute__((hot))
#  else
#    define HOT_FUNCTION
#  endif
#endif

#ifndef COLD_FUNCTION
#  if defined(__GNUC__) || defined(__clang__)
#    define COLD_FUNCTION __attribute__((cold))
#  else
#    define COLD_FUNCTION
#  endif
#endif

#ifndef LIKELY
#  if defined(__GNUC__) || defined(__clang__)
#    define LIKELY(x)   __builtin_expect(!!(x), 1)
#    define UNLIKELY(x) __builtin_expect(!!(x), 0)
#  else
#    define LIKELY(x)   (x)
#    define UNLIKELY(x) (x)
#  endif
#endif

#ifndef PREFETCH_READ
#  if defined(__GNUC__) || defined(__clang__)
#    define PREFETCH_READ(addr)  __builtin_prefetch((addr), 0, 3)
#    define PREFETCH_WRITE(addr) __builtin_prefetch((addr), 1, 3)
#  elif defined(_MSC_VER)
#    include <intrin.h>
#    define PREFETCH_READ(addr)  _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#    define PREFETCH_WRITE(addr) _mm_prefetch((const char*)(addr), _MM_HINT_T0)
#  else
#    define PREFETCH_READ(addr)  ((void)0)
#    define PREFETCH_WRITE(addr) ((void)0)
#  endif
#endif

#endif // MELONPRIME_COMPILER_HINTS_H
