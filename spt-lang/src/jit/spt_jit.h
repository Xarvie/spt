/*
** spt_jit.h — SPT Trace JIT Compiler
**
** A trace-based JIT compiler for the SPT Lua VM, inspired by LuaJIT.
** Records hot loops into SSA IR, optimizes, and generates native x86-64 code.
**
** Architecture:
**   1. Hot loop detection via backward-jump counters in the interpreter
**   2. Trace recording: interpret bytecode, build typed SSA IR with guards
**   3. IR optimization: constant folding, CSE, DCE, copy propagation
**   4. Native x86-64 code generation with linear-scan register allocation
**   5. Guard-based side exits that restore interpreter state via snapshots
**
** Value representation: 16-byte TValue (8-byte Value + tag), unchanged.
** The JIT specializes on int/float at trace-record time and operates on
** raw 64-bit values in CPU registers when types are known.
*/
#ifndef SPT_JIT_H
#define SPT_JIT_H

#include <stdint.h>
#include <stddef.h>

/* Forward declarations from VM */
typedef struct lua_State lua_State;
typedef struct CallInfo CallInfo;
typedef struct Proto Proto;
typedef struct LClosure LClosure;
typedef struct TValue TValue;
typedef union StackValue StackValue;
typedef unsigned int l_uint32;
typedef l_uint32 Instruction;

/* =====================================================================
** JIT Configuration
** ===================================================================== */

/* Compile-time enable/disable. Define SPT_JIT_DISABLE to remove all JIT. */
#if !defined(SPT_JIT_DISABLE)
#define SPT_JIT_ENABLED 1
#else
#define SPT_JIT_ENABLED 0
#endif

/* Hot loop detection: number of iterations before recording a trace. */
#define SPT_JIT_HOT 60

/* Abort backlist: after this many failed recording attempts at one location,
   stop trying to trace it. Prevents the JIT from repeatedly recording and
   discarding an un-traceable loop (which is slower than just interpreting). */
#define SPT_JIT_MAX_ABORTS 8

/* Maximum trace length (instructions). Longer traces are aborted. */
#define SPT_JIT_MAX_TRACE 4096

/* Maximum IR instructions per trace. */
#define SPT_JIT_MAX_IR 8192

/* Maximum snapshots per trace. */
#define SPT_JIT_MAX_SNAPSHOTS 256

/* Maximum side exits per trace. */
#define SPT_JIT_MAX_EXITS 128

/* Maximum distinct live-in slots tracked for the per-entry type recheck.
   Loops rarely carry more than a handful of typed live-ins; a trace exceeding
   this falls back to a full-IR scan at entry (correct, just slower). */
#define SPT_JIT_MAX_LIVEIN 32

/* Maximum upvalues referenced by a trace. */
#define SPT_JIT_MAX_UPVALS 32

/* Trace cache size (must be power of 2). */
#define SPT_JIT_CACHE_SIZE 256

/* Initial executable code buffer size (1 MB). */
#define SPT_JIT_CODE_SIZE (1024 * 1024)

/* =====================================================================
** JIT Mode Control
** ===================================================================== */

enum SPTJitMode {
  SPT_JIT_MODE_OFF = 0,
  SPT_JIT_MODE_ON = 1,
  SPT_JIT_MODE_RECORD = 2,  /* currently recording a trace */
};

/* =====================================================================
** Forward declarations of internal types
** ===================================================================== */

typedef struct SPTJitState SPTJitState;
typedef struct SPTTrace SPTTrace;
typedef struct SPTIRInst SPTIRInst;
typedef struct SPTSnapshot SPTSnapshot;
typedef struct SPTExitInfo SPTExitInfo;
typedef struct SPTAsm SPTAsm;
typedef struct SPTRegMap SPTRegMap;

/* =====================================================================
** Public API
** ===================================================================== */

#if defined(__cplusplus)
extern "C" {
#endif

/* Initialize JIT state for a global state. Called once per lua_State creation. */
SPTJitState *sptjit_create(void);

/* Destroy JIT state and free all resources. */
void sptjit_destroy(SPTJitState *js);

/* Enable/disable JIT at runtime. */
void sptjit_enable(SPTJitState *js);
void sptjit_disable(SPTJitState *js);

/* Check if JIT is enabled. */
int sptjit_is_enabled(const SPTJitState *js);

/*
** Called from the interpreter on every backward jump / loop iteration.
** Increments the hot counter for (proto, pc). When hot enough, triggers
** trace recording and execution.
**
** Returns 1 if the JIT took over execution (interpreter should return),
** 0 otherwise (interpreter should continue normally).
*/
int sptjit_trace_hot(lua_State *L, CallInfo *ci, const Instruction *pc);

/*
** Called from the interpreter to enter an already-compiled trace.
** Returns 1 if trace was entered, 0 if no trace exists or entry failed.
*/
int sptjit_trace_enter(lua_State *L, CallInfo *ci, const Instruction *pc);

/*
** Branch-direction profiling. Before recording a loop, the JIT briefly profiles
** which way each conditional branch goes so it records the *majority* direction
** (instead of whichever way the one recording iteration happened to go -- a
** non-deterministic coin-flip that can produce traces slower than the
** interpreter). `sptjit_profiling_active` is a process-global fast gate: it is
** non-zero only during the short profiling window, so the per-comparison hook
** is a single predicted-not-taken branch the rest of the time. `fall_through`
** is (cond != k): true means the comparison fell through (skipped the JMP).
*/
extern int sptjit_profiling_active;
void sptjit_profile_cond(lua_State *L, const Instruction *pc, int fall_through);

/* Invalidate all traces for a given Proto (e.g., on proto GC). */
void sptjit_invalidate_proto(SPTJitState *js, Proto *p);

/* Flush all traces (e.g., for debugging). */
void sptjit_flush_all(SPTJitState *js);

/* Statistics */
typedef struct SPTJitStats {
  uint64_t traces_recorded;
  uint64_t traces_aborted;
  uint64_t traces_compiled;
  uint64_t trace_entries;
  uint64_t trace_exits;
  uint64_t trace_guard_fail;
} SPTJitStats;

void sptjit_get_stats(const SPTJitState *js, SPTJitStats *stats);

#if defined(__cplusplus)
}
#endif

#endif /* SPT_JIT_H */
