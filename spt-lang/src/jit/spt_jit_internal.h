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

  /* Link info: which trace does each exit link to? -1 = no link */
  int exit_links[SPT_JIT_MAX_EXITS];

  /* Next trace in hash chain */
  SPTTrace *next;

  /* Call inlining: if this trace inlined a pure leaf call, it may only be
     entered when stack slot `inline_fn_slot` holds a closure of
     `inline_fn_proto`. -1 = no inlined call (no entry check needed). */
  int inline_fn_slot;
  Proto *inline_fn_proto;
};

/* =====================================================================
** Hot counter entry
** ===================================================================== */

typedef struct {
  Proto *proto;
  int pc_offset;
  uint16_t counter;
  uint16_t aborts;  /* times recording aborted here; blacklist once it's high */
  SPTTrace *trace;  /* compiled trace, if any */
} SPTHotEntry;

/* =====================================================================
** JIT State
** ===================================================================== */

struct SPTJitState {
  int mode;                /* SPT_JIT_MODE_* */
  SPTJitStats stats;

  uint16_t hot_threshold;  /* trips before recording (configurable) */
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
