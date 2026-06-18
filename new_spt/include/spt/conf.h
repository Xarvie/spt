/*
 * spt/conf.h — Build-time configuration, platform detection, and core types.
 *
 * This header centralises every compile-time switch so the rest of the engine
 * never has to reason about the target directly. Two switches matter most:
 *
 *   SPT_JIT  : when defined (and the platform allows it) the MIR-backed JIT is
 *              compiled in. The interpreter is ALWAYS present and authoritative;
 *              the JIT is a purely additive fast path. Building with the JIT
 *              disabled links zero MIR code.
 *
 *   iOS      : Apple forbids W^X / executable-page generation in App Store apps,
 *              so JIT is impossible there. We detect iOS and force interpreter-
 *              only mode regardless of how SPT_JIT was requested.
 */
#ifndef SPT_CONF_H
#define SPT_CONF_H

#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Platform detection                                                  */
/* ------------------------------------------------------------------ */
#if defined(__APPLE__)
#  include <TargetConditionals.h>
#  if TARGET_OS_IPHONE || TARGET_OS_SIMULATOR
#    define SPT_PLATFORM_IOS 1
#  endif
#endif

/* ------------------------------------------------------------------ */
/* JIT availability                                                    */
/*                                                                     */
/* SPT_JIT is requested by the build system (-DSPT_JIT). We gate it    */
/* behind platform capability: on iOS the request is silently dropped  */
/* and the engine runs interpreter-only.                               */
/* ------------------------------------------------------------------ */
#if defined(SPT_JIT) && !defined(SPT_PLATFORM_IOS)
#  define SPT_HAS_JIT 1
#else
#  undef  SPT_HAS_JIT
#endif

/* ------------------------------------------------------------------ */
/* Interpreter dispatch                                                */
/*                                                                     */
/* Direct-threaded (computed-goto) dispatch is the single biggest      */
/* interpreter performance lever. It is a GCC/Clang extension; MSVC    */
/* lacks labels-as-values and falls back to a switch — exactly as PUC- */
/* Lua does, so "no slower than Lua" still holds on that toolchain.    */
/* ------------------------------------------------------------------ */
#if (defined(__GNUC__) || defined(__clang__)) && !defined(SPT_FORCE_SWITCH_DISPATCH)
#  define SPT_USE_COMPUTED_GOTO 1
#endif

/* ------------------------------------------------------------------ */
/* Symbol visibility / inlining                                        */
/* ------------------------------------------------------------------ */
#if defined(__GNUC__) || defined(__clang__)
#  define SPT_INLINE   static inline __attribute__((always_inline))
#  define SPT_NOINLINE __attribute__((noinline))
#  define SPT_LIKELY(x)   __builtin_expect(!!(x), 1)
#  define SPT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#  define SPT_INLINE   static inline
#  define SPT_NOINLINE
#  define SPT_LIKELY(x)   (x)
#  define SPT_UNLIKELY(x) (x)
#endif

#define SPT_API extern

/* ------------------------------------------------------------------ */
/* Core numeric types                                                  */
/*                                                                     */
/* SPT keeps integers and floats as distinct first-class types (unlike */
/* stock Lua's single number subtyped at runtime). 64-bit on both.     */
/* ------------------------------------------------------------------ */
typedef int64_t  spt_Integer;
typedef double   spt_Number;

/* Tunables ---------------------------------------------------------- */
#define SPT_MIN_STACK        40    /* slots guaranteed to a C frame      */
#define SPT_STACK_INIT       256   /* initial value-stack size           */
#define SPT_MAXREGS          250   /* max registers per function frame   */
#define SPT_GC_PAUSE_KB      256   /* alloc bytes between auto collections*/

/* JIT tunables (only meaningful when SPT_HAS_JIT) ------------------- */
#define SPT_JIT_THRESHOLD    50    /* calls before a Proto is compiled   */
#define SPT_JIT_OPT_LEVEL    2     /* MIR optimisation level (0..3)      */

#endif /* SPT_CONF_H */
