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

/* Inner-loop unrolling (Phase 3a / nested loops): when an outer trace records
   through an inner numeric for-loop whose init/limit/step are compile-time
   integer constants and whose body is straight-line, the inner loop is fully
   unrolled inline (the whole nested loop becomes one linear trace) provided the
   trip count does not exceed this cap. This removes the per-outer-iteration
   inner-trace exit/re-entry that made short inner loops a net loss (measured:
   inner=4 was 0.98x, a negative optimization). Larger inner loops are left as
   their own trace (already profitable; unrolling them would bloat the trace).
   Env-overridable via SPT_JIT_UNROLL_MAX (0 disables unrolling entirely). */
#define SPT_JIT_UNROLL_MAX 16

/* Maximum IR instructions per trace. */
#define SPT_JIT_MAX_IR 8192

/* Maximum snapshots per trace. */
#define SPT_JIT_MAX_SNAPSHOTS 256

/* Maximum side exits per trace. */
#define SPT_JIT_MAX_EXITS 128

/* Trace-linking ("stitching", Phase 2): after a trace exits with full state
   flushed to the interpreter stack, sptjit_trace_enter may re-enter a compiled
   trace found at the resume PC instead of returning to the interpreter dispatch
   loop. This is a hard backstop on the number of consecutive native hand-offs
   per C entry; in practice the chain is 1-3 hops (parent -> side trace -> back).
   The strict forward-progress check is the primary terminator; this cap is an
   absolute safety net against any unforeseen non-progressing cycle. */
#define SPT_JIT_MAX_LINK_HOPS 64

/* Side-trace recording (Phase 2): a parent trace's side exit becomes a side-
   trace candidate after it is taken this many times. Recording a side trace
   rooted at the exit PC turns the "wrong arm + rest of loop body" path -- which
   otherwise round-trips through the interpreter every time -- into native code
   that the link trampoline hands off to. Kept well above SPT_JIT_HOT so we only
   spend a side trace on an exit that is genuinely, repeatedly hot. */
#define SPT_JIT_SIDE_HOT 200

/* Minimum IR size (total instructions) for a side trace to be KEPT. Stack-
   mediated linking pays a fixed per-hand-off cost: the parent's exit flushes all
   live state to the stack, then the side trace runs its own prologue + SLOAD
   reloads + epilogue. For a tiny minority arm that fixed cost exceeds simply
   letting the interpreter dispatch the arm's few bytecodes, making the side trace
   a net LOSS versus the pre-Phase-2 fallback. Measured (20M-iter 50/50, side
   trace vs no-side-trace): ~21-inst arm 0.84x (loss), ~32-inst 1.11x, ~49-inst
   1.25x (wins). So a side trace is only kept once its body is large enough to
   amortize the link overhead; below this it is discarded and the arm runs in the
   interpreter (no regression from Phase 2). Env-overridable (SPT_JIT_SIDE_MIN_IR);
   set to 0 to force-record every side trace (used by the test harness to exercise
   the recording/linking path on small kernels). */
#define SPT_JIT_SIDE_MIN_IR 28

/* Maximum distinct live-in slots tracked for the per-entry type recheck.
   Loops rarely carry more than a handful of typed live-ins; a trace exceeding
   this falls back to a full-IR scan at entry (correct, just slower). */
#define SPT_JIT_MAX_LIVEIN 32

/* Maximum upvalues referenced by a trace. */
#define SPT_JIT_MAX_UPVALS 32

/* Maximum distinct inlined methods (OP_SELF + CALL) pinned by one trace. Real
   OOP loops call only a handful of distinct methods on stable receivers (e.g.
   `a.foo(); a.bar()`); a trace exceeding this aborts the extra method (correct,
   just not inlined). Each pinned method is re-validated once per entry. */
#define SPT_JIT_MAX_METHODS 8

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
