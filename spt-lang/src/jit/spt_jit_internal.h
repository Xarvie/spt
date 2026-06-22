/*
** spt_jit_internal.h — Internal JIT definitions shared between JIT modules
**
** This header is NOT part of the public API. It exposes the internal
** structure definitions (SPTTrace, SPTJitState) so that multiple JIT
** translation units (spt_jit.c, spt_jit_codegen.c) can share them.
*/
#ifndef SPT_JIT_INTERNAL_H
#define SPT_JIT_INTERNAL_H

#include "spt_jit.h"
#include "spt_jit_ir.h"

/* lua_CFunction is identical to lua.h's typedef (C11 allows the redefinition);
   declared here so SPTTrace can store the for-each iterator identity without
   pulling in the full VM headers. */
typedef int (*lua_CFunction)(lua_State *L);

/* One pinned inlined-method identity. A trace may inline several distinct
   methods (e.g. `a.foo(); a.bar()`); each is re-validated once per entry. The
   receiver is loop-invariant, so at entry we pin its metatable (the class) and
   re-resolve the method name to the same proto. */
typedef struct SPTMethodId {
  int recv_slot;     /* absolute receiver slot (R[B] of SELF) */
  void *class_mt;    /* Table* of the expected class metatable */
  void *key;         /* TString* of the method name */
  Proto *proto;      /* expected resolved method proto */
} SPTMethodId;

/* =====================================================================
** Trace structure
** ===================================================================== */

struct SPTTrace {
  Proto *proto;            /* Proto this trace was recorded from */
  int pc_offset;           /* PC offset from proto->code (loop header) */
  void *code;              /* executable code */
  size_t code_size;        /* code size */
  int nrefs;               /* reference count */

  /* IR (kept for debugging/recompilation) */
  SPTIRBuilder ir;

  /* Snapshots and exits */
  SPTExitInfo exits[SPT_JIT_MAX_EXITS];
  int nexits;

  /* Exit PC for each snapshot (where to resume in interpreter) */
  const Instruction *exit_pcs[SPT_JIT_MAX_SNAPSHOTS];

  /* Per-snapshot exit counter: how many times each exit (snapshot) was taken
     back to the interpreter. Incremented by the exit stub. Zeroed at alloc
     (calloc). Aggregated into stats.trace_exits at dump time. The "loop-end"
     count guard's snapshot dominates for a well-behaved looping trace; a hot
     *side* exit (type/bounds guard) is the signal for side-trace chaining. */
  uint64_t exit_count[SPT_JIT_MAX_SNAPSHOTS];

  /* Snapshot index of the FORLOOP "count > 0" guard (the loop-continuation
     guard). Exits from this snapshot are normal loop termination and are
     excluded from the runtime guard-failure blacklist check. -1 = none (e.g.
     side traces closed with sptir_exit). */
  int loop_end_snap;

  /* Number of times this trace has been entered (incremented in
     sptjit_trace_enter). Used to amortize the runtime blacklist check. */
  uint32_t entry_count;

  /* Link info: which trace does each exit link to? -1 = no link */
  int exit_links[SPT_JIT_MAX_EXITS];

  /* Next trace in hash chain */
  SPTTrace *next;

  /* Call inlining: if this trace inlined a pure leaf call, it may only be
     entered when stack slot `inline_fn_slot` holds a closure of
     `inline_fn_proto`. -1 = no inlined call (no entry check needed). */
  int inline_fn_slot;
  Proto *inline_fn_proto;

  /* for-each (OP_TFORCALL) list specialization: the trace iterated a List via
     pairs() and may only be entered while slot `forin_iter_slot` still holds the
     expected iterator C function `forin_iter_fn` (= luaB_next). Re-derives the
     key sequence (0,1,...) and value (GETI) natively, so a different iterator
     (custom / over a map) would be wrong -> decline. -1 = not a for-each trace. */
  int forin_iter_slot;
  lua_CFunction forin_iter_fn;

  /* Method-call (OP_SELF + CALL) inlining: the trace inlined one or more
     `obj.method()` calls where obj is a class instance. Enter only while every
     pinned method's receiver slot still holds a table whose metatable is the
     pinned class and whose name re-resolves to the pinned proto. n_methods == 0
     = not a method trace. */
  SPTMethodId methods[SPT_JIT_MAX_METHODS];
  int n_methods;

  /* Compact live-in type-check list, precomputed at record time from the IR's
     GUARD_T-on-SLOAD instructions (deduped by slot). The entry check in
     sptjit_trace_enter iterates THIS instead of scanning the full IR on every
     entry -- on a hot loop that is re-entered millions of times (e.g. a branch
     trace that exits each iteration) the full O(ninst) scan is a real cost.
     `livein_type[k]` holds the SPTType the slot was pinned to. n_livein == -1
     means the list overflowed SPT_JIT_MAX_LIVEIN -> fall back to a full scan. */
  int n_livein;
  uint8_t livein_slot[SPT_JIT_MAX_LIVEIN];
  uint8_t livein_type[SPT_JIT_MAX_LIVEIN];
};

/* =====================================================================
** Hot counter entry
** ===================================================================== */

typedef struct {
  Proto *proto;
  int pc_offset;
  uint16_t counter;
  uint16_t aborts;  /* times recording aborted here; blacklist once it's high */
  uint16_t runtime_fails; /* times a compiled trace was discarded for excessive
                              runtime guard failures; contributes to blacklist */
  SPTTrace *trace;  /* compiled trace, if any */
} SPTHotEntry;

/* =====================================================================
** JIT State
** ===================================================================== */

struct SPTJitState {
  int mode;                /* SPT_JIT_MODE_* */
  SPTJitStats stats;

  uint16_t hot_threshold;  /* trips before recording (configurable) */
  uint32_t side_hot_threshold; /* parent-exit taken-count before a side trace */
  int side_min_ir;         /* min IR size to keep a side trace (amortize linking) */
  int unroll_max;          /* max inner-loop trip count to unroll inline (0=off) */
  int debug;               /* emit diagnostics to stderr */

  /* Hot loop detection: hash table keyed by (proto, pc_offset) */
  SPTHotEntry *hot_table;
  int hot_size;            /* number of slots (power of 2) */
  int hot_count;           /* number of entries */

  /* Trace free list (for reuse) */
  SPTTrace *trace_freelist;

  /* Recording state */
  int recording;
  SPTTrace *rec_trace;
  const Instruction *rec_start_pc;
  const Instruction *rec_end_pc;
  int rec_inst_count;

  /* Executable code arena */
  uint8_t *code_buf;       /* executable memory pool */
  size_t code_buf_size;
  size_t code_buf_used;
};

/* =====================================================================
** Internal API (shared between JIT translation units)
** ===================================================================== */

/* Code generation entry point (defined in spt_jit_codegen.c). */
void sptjit_codegen_compile(SPTTrace *t, SPTJitState *js);

#endif /* SPT_JIT_INTERNAL_H */
