/*
 * jit/jit.h — JIT backend interface.
 *
 * The interpreter is authoritative and runs every function. When SPT_HAS_JIT is
 * set, hot prototypes are handed to the MIR backend, which compiles the
 * bytecode to a native function whose pointer is cached in Proto::jit_entry;
 * subsequent calls jump straight to native code.
 *
 * When SPT_HAS_JIT is NOT set (e.g. iOS, where executable-page generation is
 * forbidden), this header collapses every entry point to an inline no-op and no
 * MIR code is compiled or linked. The same source builds and runs unchanged —
 * interpreter-only.
 */
#ifndef SPT_JIT_H
#define SPT_JIT_H

#include "spt/state.h"

#ifdef SPT_HAS_JIT

/* Opaque per-Global JIT context (owns the MIR_context_t). */
typedef struct spt_Jit spt_Jit;

SPT_API void spt_jit_init(spt_State *L);
SPT_API void spt_jit_shutdown(spt_State *L);

/* Runtime master switch — lets a host disable the JIT without rebuilding. */
SPT_API void spt_jit_set_enabled(spt_State *L, int enabled);
SPT_API int  spt_jit_is_enabled(spt_State *L);

/* Attempt to compile a hot prototype. On success sets p->jit_entry. */
SPT_API void spt_jit_try_compile(spt_State *L, Proto *p);

/* Enter a previously compiled prototype (p->jit_entry != NULL). Results are
 * left exactly where the interpreter would leave them (at the callable slot). */
SPT_API void spt_jit_enter(spt_State *L, Proto *p);

#else  /* interpreter-only build */

#define spt_jit_init(L)              ((void)0)
#define spt_jit_shutdown(L)          ((void)0)
#define spt_jit_set_enabled(L, e)    ((void)0)
#define spt_jit_is_enabled(L)        (0)
#define spt_jit_try_compile(L, p)    ((void)0)
#define spt_jit_enter(L, p)          ((void)0)

#endif /* SPT_HAS_JIT */

#endif /* SPT_JIT_H */
