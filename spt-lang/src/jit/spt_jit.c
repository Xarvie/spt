/*
** spt_jit.c — SPT Trace JIT Core
**
** Hot loop detection, trace recording, trace cache, and trace entry/exit.
**
** The JIT hooks into the interpreter at backward jumps (loop back-edges).
** When a loop is hot (executed SPT_JIT_HOT times), a trace is recorded
** by interpreting the bytecode and building SSA IR. The IR is optimized
** and compiled to native x86-64 code. On subsequent encounters of the
** same loop, the compiled trace is entered directly.
*/
#include "spt_jit.h"
#include "spt_jit_ir.h"
#include "spt_jit_asm.h"
#include "spt_jit_internal.h"

/* VM internals */
#include "lua.h"
#include "lobject.h"
#include "lstate.h"
#include "lopcodes.h"
#include "lopnames.h"
#include "ltable.h"
#include "ltm.h"
#include "lvm.h"
#include "ldebug.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* Maps a math-library C function to its unary libm function (double->double),
   or NULL if not a JIT-lowerable strictly-unary math fn. Defined in lmathlib.c
   (the math_* functions are file-static there). */
typedef double (*spt_unary_libm_fn)(double);
extern spt_unary_libm_fn spt_jit_unary_math(lua_CFunction f);
/* Identify string.len / string.byte for SLEN / SBYTE lowering (defined in
   lstrlib.c where str_len/str_byte are file-static). 1 = str_len, 2 = str_byte. */
extern int spt_jit_str_op(lua_CFunction f);
/* Identify math.min / math.max for branchless-select lowering (defined in
   lmathlib.c where math_min/math_max are file-static). 1 = min, 2 = max. */
extern int spt_jit_math_minmax(lua_CFunction f);
/* Identify math.abs for branchless-select (int) / FMATH-fabs (float) lowering
   (defined in lmathlib.c where math_abs is file-static). Returns the libm fabs
   pointer when f is math_abs, else NULL. */
extern spt_unary_libm_fn spt_jit_math_abs(lua_CFunction f);
/* Identify math.floor / math.ceil for rounding-convert lowering (SPTIR_TOINT)
   (defined in lmathlib.c where math_floor/math_ceil are file-static). 1 = floor,
   2 = ceil, else 0. */
extern int spt_jit_math_floorceil(lua_CFunction f);
/* Address of luaB_next (the pairs() iterator); used to pin a for-each trace's
   iterator with an entry guard. See lbaselib.c. */
extern lua_CFunction spt_jit_pairs_next(void);

/* =====================================================================
** Branch-direction profiling (see spt_jit.h). Global fast gate + tally.
** ===================================================================== */

int sptjit_profiling_active = 0;

/* The loop currently being profiled and a small per-branch tally keyed by the
** comparison instruction's PC offset. Only one loop profiles at a time. */
#define SPT_PROF_MAX_BRANCH 32
#define SPT_PROF_ITERS 64      /* branch-direction samples before recording */
#define SPT_PROF_BUDGET 512    /* abandon a stalled profile after this many hot ticks */
static struct {
  Proto *proto;
  int pc_start, pc_end;        /* loop body PC-offset range [start, end) */
  int iters;                   /* samples gathered on the profiled loop */
  int budget;                  /* remaining hot ticks before abandoning */
  int n;
  int br_pc[SPT_PROF_MAX_BRANCH];   /* comparison PC offset */
  uint32_t ft[SPT_PROF_MAX_BRANCH]; /* fall-through count */
  uint32_t tk[SPT_PROF_MAX_BRANCH]; /* take-JMP count */
} g_prof;

void sptjit_profile_cond(lua_State *L, const Instruction *pc, int fall_through) {
  CallInfo *ci = L->ci;
  if (!ci) return;
  LClosure *cl = clLvalue(s2v(ci->func.p));
  if (!cl || cl->p != g_prof.proto) return;        /* not the profiled proto */
  /* `pc` points at the JMP that follows the comparison; the comparison is the
     instruction before it. */
  int cmp_off = (int)((pc - 1) - g_prof.proto->code);
  if (cmp_off < g_prof.pc_start || cmp_off >= g_prof.pc_end) return;
  for (int k = 0; k < g_prof.n; k++) {
    if (g_prof.br_pc[k] == cmp_off) {
      if (fall_through) g_prof.ft[k]++; else g_prof.tk[k]++;
      return;
    }
  }
  if (g_prof.n < SPT_PROF_MAX_BRANCH) {
    g_prof.br_pc[g_prof.n] = cmp_off;
    g_prof.ft[g_prof.n] = fall_through ? 1 : 0;
    g_prof.tk[g_prof.n] = fall_through ? 0 : 1;
    g_prof.n++;
  }
}

/* =====================================================================
** Struct offsets for JIT code generation
** ===================================================================== */

/* Use offsetof to get exact offsets for struct field access in generated code. */
#define OFF_L_TOP       offsetof(lua_State, top)
#define OFF_L_CI        offsetof(lua_State, ci)
#define OFF_CI_FUNC     offsetof(CallInfo, func)
#define OFF_CI_TOP      offsetof(CallInfo, top)
#define OFF_CI_SAVEDPC  offsetof(CallInfo, u.l.savedpc)
#define OFF_CI_NEXTRA   offsetof(CallInfo, u.l.nextraargs)
#define OFF_LCLOSURE_P  offsetof(LClosure, p)
#define OFF_PROTO_K     offsetof(Proto, k)
#define OFF_PROTO_CODE  offsetof(Proto, code)
#define OFF_PROTO_SIZECODE offsetof(Proto, sizecode)
#define OFF_PROTO_MAXSTACK offsetof(Proto, maxstacksize)
#define OFF_TVALUE_VAL  offsetof(TValue, value_)
#define OFF_TVALUE_TT   offsetof(TValue, tt_)
#define OFF_TABLE_ASIZE offsetof(Table, asize)
#define OFF_TABLE_LOGLEN offsetof(Table, loglen)
#define OFF_TABLE_ARRAY offsetof(Table, array)
#define OFF_TABLE_MODE  offsetof(Table, mode)
#define OFF_TABLE_FLAGS offsetof(Table, flags)
#define OFF_TABLE_METATABLE offsetof(Table, metatable)
#define OFF_UPVAL_V     offsetof(UpVal, v)
#define OFF_LCLOSURE_UPVALS offsetof(LClosure, upvals)

/* Stack slot size = sizeof(StackValue) = 16 bytes */
#define SLOT_SIZE 16

/* =====================================================================
** Hash function for (proto, pc_offset)
** ===================================================================== */

static uint32_t hot_hash(Proto *p, int pc_offset) {
  uintptr_t v = (uintptr_t)p;
  v ^= (uintptr_t)p >> 32;
  v = (v * 2654435761u) + (uint32_t)pc_offset;
  return (uint32_t)v;
}

/* =====================================================================
** JIT State Management
** ===================================================================== */

SPTJitState *sptjit_create(void) {
  SPTJitState *js = (SPTJitState *)calloc(1, sizeof(SPTJitState));
  if (!js) return NULL;
  js->mode = SPT_JIT_MODE_OFF;
  js->hot_size = 256; /* power of 2 */
  js->hot_table = (SPTHotEntry *)calloc(js->hot_size, sizeof(SPTHotEntry));
  js->code_buf_size = SPT_JIT_CODE_SIZE;
  js->code_buf = (uint8_t *)sptjit_alloc_exec(js->code_buf_size);
  if (!js->code_buf) {
    free(js->hot_table);
    free(js);
    return NULL;
  }
  js->hot_threshold = SPT_JIT_HOT;
  js->side_hot_threshold = SPT_JIT_SIDE_HOT;
  js->side_min_ir = SPT_JIT_SIDE_MIN_IR;
  js->unroll_max = SPT_JIT_UNROLL_MAX;

  /* Environment configuration:
     SPT_JIT=1 / on / true  -> enable JIT
     SPT_JIT=0 / off / false -> keep disabled (default)
     SPT_JIT_HOT=<n>        -> override hot-loop trip threshold
     SPT_JIT_DEBUG=1        -> emit recording/compile diagnostics to stderr */
  {
    const char *e = getenv("SPT_JIT");
    if (e && *e && e[0] != '0' &&
        e[0] != 'o' /* "off" */ && e[0] != 'O' &&
        e[0] != 'f' /* "false" */ && e[0] != 'F' &&
        e[0] != 'n' /* "no" */ && e[0] != 'N') {
      js->mode = SPT_JIT_MODE_ON;
    }
    /* "on"/"true"/"yes" explicitly enable */
    if (e && (e[0] == 'o' || e[0] == 'O' || e[0] == 't' || e[0] == 'T' ||
              e[0] == 'y' || e[0] == 'Y'))
      js->mode = SPT_JIT_MODE_ON;

    const char *h = getenv("SPT_JIT_HOT");
    if (h && *h) {
      int v = atoi(h);
      if (v > 0) js->hot_threshold = (uint16_t)(v > 0xFFFE ? 0xFFFE : v);
    }
    const char *sh = getenv("SPT_JIT_SIDE_HOT");
    if (sh && *sh) {
      int v = atoi(sh);
      if (v > 0) js->side_hot_threshold = (uint32_t)v;
    }
    const char *smi = getenv("SPT_JIT_SIDE_MIN_IR");
    if (smi && *smi) {
      int v = atoi(smi);
      if (v >= 0) js->side_min_ir = v;
    }
    const char *um = getenv("SPT_JIT_UNROLL_MAX");
    if (um && *um) {
      int v = atoi(um);
      if (v >= 0) js->unroll_max = v;
    }
    const char *d = getenv("SPT_JIT_DEBUG");
    js->debug = (d && *d) ? atoi(d) : 0;
  }
  return js;
}

void sptjit_destroy(SPTJitState *js) {
  if (!js) return;
  if (js->debug) {
    /* Aggregate per-trace exit counters into the global total (traces live in
       the hot table until shutdown, so this captures every exit taken). */
    js->stats.trace_exits = 0;
    for (int i = 0; i < js->hot_size; i++) {
      SPTTrace *t = js->hot_table[i].trace;
      if (!t) continue;
      for (int s = 0; s < t->ir.nsnaps && s < SPT_JIT_MAX_SNAPSHOTS; s++)
        js->stats.trace_exits += t->exit_count[s];
    }
    fprintf(stderr,
            "[JIT] stats: recorded=%llu compiled=%llu aborted=%llu "
            "entries=%llu exits=%llu guard_fail=%llu\n",
            (unsigned long long)js->stats.traces_recorded,
            (unsigned long long)js->stats.traces_compiled,
            (unsigned long long)js->stats.traces_aborted,
            (unsigned long long)js->stats.trace_entries,
            (unsigned long long)js->stats.trace_exits,
            (unsigned long long)js->stats.trace_guard_fail);
    /* Per-exit breakdown: which exit points are hot. A trace that loops well
       concentrates its exits on the loop-end snapshot; a hot *side* exit marks
       a spot a side-trace would pay off. */
    if (js->debug >= 2) {
      for (int i = 0; i < js->hot_size; i++) {
        SPTTrace *t = js->hot_table[i].trace;
        if (!t) continue;
        int any = 0;
        for (int s = 0; s < t->ir.nsnaps && s < SPT_JIT_MAX_SNAPSHOTS; s++)
          if (t->exit_count[s]) { any = 1; break; }
        if (!any) continue;
        fprintf(stderr, "[JIT] trace proto=%p pc@%d exits:",
                (void *)t->proto, t->pc_offset);
        for (int s = 0; s < t->ir.nsnaps && s < SPT_JIT_MAX_SNAPSHOTS; s++) {
          if (!t->exit_count[s]) continue;
          int epc = (t->exit_pcs[s] && t->proto)
                        ? (int)(t->exit_pcs[s] - t->proto->code) : -1;
          fprintf(stderr, " [snap%d->pc%d:%llu]", s, epc,
                  (unsigned long long)t->exit_count[s]);
        }
        fprintf(stderr, "\n");
      }
    }
  }
  /* Free all traces */
  for (int i = 0; i < js->hot_size; i++) {
    SPTTrace *t = js->hot_table[i].trace;
    if (t) {
      sptir_free(&t->ir);
      free(t);
    }
  }
  if (js->code_buf)
    sptjit_free_exec(js->code_buf, js->code_buf_size);
  free(js->hot_table);
  free(js);
}

void sptjit_enable(SPTJitState *js) { if (js) js->mode = SPT_JIT_MODE_ON; }
void sptjit_disable(SPTJitState *js) { if (js) js->mode = SPT_JIT_MODE_OFF; }
int sptjit_is_enabled(const SPTJitState *js) { return js && js->mode != SPT_JIT_MODE_OFF; }

void sptjit_get_stats(const SPTJitState *js, SPTJitStats *stats) {
  if (js && stats) *stats = js->stats;
}

void sptjit_flush_all(SPTJitState *js) {
  if (!js) return;
  for (int i = 0; i < js->hot_size; i++) {
    SPTTrace *t = js->hot_table[i].trace;
    if (t) {
      sptir_free(&t->ir);
      free(t);
      js->hot_table[i].trace = NULL;
    }
    js->hot_table[i].counter = 0;
  }
  js->code_buf_used = 0;
}

void sptjit_invalidate_proto(SPTJitState *js, Proto *p) {
  if (!js) return;
  for (int i = 0; i < js->hot_size; i++) {
    if (js->hot_table[i].proto == p) {
      SPTTrace *t = js->hot_table[i].trace;
      if (t) {
        sptir_free(&t->ir);
        free(t);
      }
      js->hot_table[i].proto = NULL;
      js->hot_table[i].trace = NULL;
      js->hot_table[i].counter = 0;
    }
  }
}

/* =====================================================================
** Hot loop detection
** ===================================================================== */

/* Find or create a hot entry for (proto, pc_offset). */
static SPTHotEntry *hot_lookup(SPTJitState *js, Proto *p, int pc_offset) {
  uint32_t h = hot_hash(p, pc_offset) & (js->hot_size - 1);
  for (int i = 0; i < js->hot_size; i++) {
    int idx = (h + i) & (js->hot_size - 1);
    SPTHotEntry *e = &js->hot_table[idx];
    if (e->proto == p && e->pc_offset == pc_offset)
      return e;
    if (e->proto == NULL && e->counter == 0)
      return e; /* empty slot */
  }
  return NULL; /* table full */
}

/* =====================================================================
** Trace recording: bytecode -> IR
** ===================================================================== */

/* Recording context */
typedef struct {
  SPTJitState *js;
  lua_State *L;
  CallInfo *ci;
  LClosure *cl;
  Proto *p;
  const TValue *k;        /* constants */
  const Instruction *start_pc;  /* loop header PC */
  const Instruction *pc;  /* current PC */
  SPTIRBuilder *ir;       /* IR builder */
  int inst_count;         /* instructions recorded */
  int aborted;            /* recording aborted */
  const Instruction *abort_pc; /* PC where abort happened */
  /* Call inlining: absolute base of the frame currently being recorded.
     0 for the root function; when a pure straight-line leaf call is inlined,
     reg_map/reg_type are indexed by (frame_base + reg) so the callee's
     registers occupy absolute slots above the caller's, and the callee's
     argument slots coincide with the caller's argument registers already in
     reg_map (Lua slot-0-receiver: callee slot 0 = caller A+1). */
  int frame_base;
  /* Side-trace mode (Phase 2): this trace was started at a parent trace's hot
     exit PC, mid-loop. Its forward recording will eventually reach a loop
     back-edge whose target is *before* start_pc -- in a root trace that aborts
     (§nested-loop guard), but a side trace instead closes with an unconditional
     exit-to-interpreter at the back-edge (sptir_exit), handing control back so
     the interpreter runs the back-edge and re-enters the parent. */
  int is_side_trace;
  /* Single inlined-call entry check, set on the first inlined CALL. The trace
     may only be entered when this stack slot still holds this exact proto;
     all inlined calls in one trace must share the same (slot, proto). */
  int inline_fn_slot;       /* -1 = no inlined call yet */
  Proto *inline_fn_proto;
  /* Saved caller state while recording an inlined callee (depth-1 only). */
  Proto *save_p;
  const TValue *save_k;
  LClosure *save_cl;
  const Instruction *save_pc;   /* caller PC just after the CALL */
  int save_frame_base;
  int call_result_slot;     /* absolute reg_map slot receiving the return value */
  /* Pending unary-math call set up by an OP_SELF that resolved a known libm
     math method (e.g. math.sqrt): the immediately following OP_CALL on this
     slot is lowered to an SPTIR_FMATH instead of a real call. -1 slot = none. */
  void *pending_cfn_libm;   /* libm double(*)(double), baked into the FMATH */
  int pending_cfn_slot;     /* absolute slot R[A] of the SELF/CALL; -1 = none */
  /* Pending string-module call armed by an OP_SELF that resolved string.len /
     string.byte to its C function. The next CALL on this slot lowers to SLEN /
     SBYTE instead of a real call. pending_str_op: 1 = SLEN (string.len(s)),
     2 = SBYTE (string.byte(s,i)). -1 slot = none. */
  int pending_str_slot;     /* absolute slot R[A] of the SELF/CALL; -1 = none */
  int pending_str_op;       /* 1 = str_len -> SLEN, 2 = str_byte -> SBYTE */
  /* SELF instruction PC, so the SLEN/SBYTE short-string (and SBYTE bounds) guard
     resumes at the SELF -- the interpreter re-executes SELF+CALL and runs the
     real string.len/byte for a long string (or errors identically for an out-of-
     range byte). Without this, a long string at runtime side-exits to the CALL
     PC where R[A] lacks the C function -> 'attempt to call a table value'. Same
     resume-at-SELF idea as math.floor/ceil (§10.61) and inlined methods (§10.47);
     idempotent because length/byte reads have no side effects. */
  const Instruction *pending_str_self_pc;
  /* While recording the args of a math/string library call -- the window from
     the SELF that arms the pending op to the CALL that consumes it -- any guard
     (e.g. the arg's SLOAD type / short-string guard) must resume at the SELF,
     not mid-arg-load: the SELF is folded into the lowered op and never
     materializes the resolved function, so reg_map still maps R[A] to the
     module table. Resuming after the SELF would then CALL that table
     ('attempt to call a table value'). Set when arming, cleared at the CALL.
     NULL = not in such a window. (Root only; the inlined-method path at
     frame_base != 0 uses method_self_pc / method_resume_snap instead.) */
  const Instruction *call_arg_self_pc;
  /* Pending math.min / math.max armed by an OP_SELF. The next CALL on a pending
     slot lowers to a branchless select instead of a real call (1 = min, 2 = max).
     A STACK, not a single slot: a clamp `math.max(lo, math.min(hi, x))` arms the
     outer max, then the inner min, before either CALL -- the inner CALL must not
     strand the outer. CALLs pop in LIFO order (well-nested). */
  int pending_minmax_slot[8];
  int pending_minmax_op[8];
  int pending_minmax_top;   /* number of live entries; 0 = none */
  /* When a min/max CALL has its result consumed as the trailing argument of an
     enclosing call (a nested clamp), Lua emits the inner call with c == 0
     (multiret: 1 result left on the stack) and the outer call with b == 0 (args
     run to the stack top). This records the slot the last such inner result
     landed in, so the very next CALL can recover that its trailing arg count is
     exactly one. Valid for ONE following CALL only; -1 = none. */
  int minmax_multiret_top;
  /* Pending math.abs armed by an OP_SELF. The next CALL on this slot lowers to
     abs(x) without a real call: INT -> branchless select (x<0)?-x:x (reusing
     emit_select; INT64_MIN self-maps as Lua's 0u-n wraparound does), FLOAT ->
     SPTIR_FMATH(fabs) (a select would keep -0.0's bit pattern; fabs returns
     +0.0 and is also correct for NaN). Composes with min/max via the shared
     minmax_multiret_top (abs of a nested clamp, or abs feeding min/max).
     -1 slot = none. */
  int pending_abs_slot;     /* absolute slot R[A] of the SELF/CALL; -1 = none */
  void *pending_abs_fabs;   /* libm fabs double(*)(double), for the float path */
  /* Pending math.floor / math.ceil armed by an OP_SELF. The next CALL on this
     slot lowers to a rounding convert: INT arg -> identity (integer is its own
     floor/ceil), FLOAT arg -> SPTIR_TOINT (roundsd + range guard; out-of-range
     side-exits and the interpreter returns the float, as Lua does). Composes
     with min/max/abs via the shared minmax_multiret_top. mode 1 = floor, 2 =
     ceil. -1 slot = none. */
  int pending_floorceil_slot;
  int pending_floorceil_mode;
  /* SELF instruction PC, so the FLOAT-path range guard resumes at the SELF
     (re-executing SELF+CALL in the interpreter, which returns the float when
     out of range). Safe because floor/ceil is side-effect-free -- the same
     resume-at-SELF idea the inlined pure-read method path uses. */
  const Instruction *pending_floorceil_self_pc;
  /* for-each over a List specialized to a native index loop (OP_TFORCALL).
     The iterator function is loop-invariant; the trace pins it to luaB_next
     with a once-per-entry C guard (analogous to inline_fn). -1 slot = none. */
  int forin_iter_slot;      /* absolute slot of the iterator fn (ra+0); -1 = none */
  lua_CFunction forin_iter_fn; /* expected iterator (= luaB_next) */
  /* Pending user-method call armed by an OP_SELF that resolved `obj.method` to a
     Lua closure via the class metatable. The next CALL on this slot inlines the
     method's proto with the receiver as callee slot 0. -1 slot = none. */
  int pending_method_slot;     /* absolute slot R[A] of the SELF/CALL; -1 = none */
  Proto *pending_method_proto; /* resolved method proto to inline */
  void *pending_method_cl;     /* the method LClosure (callee_cl for the inline) */
  /* Method entry-guard identities (copied onto the trace). A trace may inline
     several distinct methods (`a.foo(); a.bar()`); each receiver is loop-
     invariant, and at entry we pin its metatable (the class) and re-resolve the
     method to the same proto. n_methods == 0 = not a method trace. */
  SPTMethodId methods[SPT_JIT_MAX_METHODS];
  int n_methods;
  /* Resume-at-SELF for guards inside an inlined (pure-read) method body. Such a
     guard, on failure, cannot resume at the callee PC (no callee frame exists);
     instead it resumes at the SELF instruction in the caller and the interpreter
     re-executes SELF+CALL+method. Safe only because pure-read method bodies have
     no committed side effects, so re-execution is idempotent. method_self_pc is
     the SELF instruction; method_resume_snap is the shared snapshot all in-method
     guards point at (-1 outside a method body). */
  const Instruction *method_self_pc;
  int method_resume_snap;
  /* Targeted store-to-load forwarding, active ONLY inside an inlined method body
     (frame_base != 0). After a SETFIELD to (base_ref, key), a GETFIELD of the
     SAME base ref + key forwards to the written value with NO emit and NO guard --
     this makes `int inc(){ this.v = this.v + 1; return this.v; }` (write then
     return the written field) a guard-free single-trailing-write (§10.49 safety:
     the SETFIELD stays the last guard-emitting op, so resume-at-SELF never fires
     after the write commits). Reset to -1 at trace start and at each method-inline
     entry; invalidated by any call / array-or-generic write that could alias.
     A non-forwardable GETFIELD after a write in a method body aborts (it would be
     a guard after the write -> double-write risk). */
  int fwd_base;             /* IR ref of the last SETFIELD's base table; -1 = none */
  void *fwd_key;            /* TString* key of the last SETFIELD */
  int fwd_val;              /* IR ref of the last SETFIELD's written value */
  /* Multi-write method mode: when set, GETFIELD/SETFIELD in the current inlined
     method body emit guard-free (entry field-layout guards verify key + type).
     Set at method-inline entry if proto_is_multiwrite_method_inlinable. */
  int multiwrite_mode;
  SPTFieldLayout field_layouts[SPT_JIT_MAX_FIELD_LAYOUTS];
  int n_field_layouts;
  /* Snapshot index of the FORLOOP "count > 0" guard (loop-continuation guard).
     Copied to SPTTrace.loop_end_snap after recording. -1 = none. */
  int loop_end_snap;
} SPTRecCtx;

/* Get current type of a register from the actual stack value. */
static int rec_snap(SPTRecCtx *rc);
static const Instruction *rec_guard_pc(SPTRecCtx *rc);
static int condreturn_method_op_ok(OpCode o);

/* Get current type of a register from the actual stack value. */
static SPTType rec_value_type(const TValue *v) {
  switch (ttypetag(v)) {
    case LUA_VNIL:     return SPTT_NIL;
    case LUA_VFALSE:   return SPTT_FALSE;
    case LUA_VTRUE:    return SPTT_TRUE;
    case LUA_VNUMINT:  return SPTT_INT;
    case LUA_VNUMFLT:  return SPTT_FLT;
    case LUA_VSHRSTR:
    case LUA_VLNGSTR:  return SPTT_STR;
    case LUA_VARRAY:   return SPTT_ARR;
    case LUA_VTABLE:   return SPTT_TAB;
    case LUA_VLCL:
    case LUA_VLCF:
    case LUA_VCCL:     return SPTT_FUNC;
    case LUA_VUSERDATA: return SPTT_UD;
    default:           return SPTT_ANY;
  }
}

/* Look a constant short-string key up in a table's hash part by walking the
   main position + collision chain (mirrors luaH_getshortstr / gen_hash_find).
   Returns the value TValue*, or NULL if the key is absent. Used at record time
   to resolve stable library lookups (e.g. _ENV["math"]["sqrt"]). */
static const TValue *rec_table_getstr(Table *t, TString *key) {
  Node *n = &t->node[(unsigned int)key->hash & ((1u << t->lsizenode) - 1u)];
  for (;;) {
    if (keytt(n) == ctb(LUA_VSHRSTR) && (void *)keyval(n).gc == (void *)key)
      return &n->i_val;
    int nx = (int)n->u.next;
    if (nx == 0) return NULL;
    n += nx;
  }
}

/* Load a register's value into IR, with type guard if needed. */
static int rec_load_reg(SPTRecCtx *rc, int reg) {
  SPTIRBuilder *ir = rc->ir;
  if (ir->reg_map[rc->frame_base + reg] >= 0)
    return ir->reg_map[rc->frame_base + reg];

  /* Inside an inlined callee (frame_base != 0) every slot read must already be
     resident in reg_map -- it is either an argument the caller placed there or
     a value the callee just computed. A miss would mean reading the wrong
     (root) stack slot, so abort and let the interpreter run the call. */
  if (rc->frame_base != 0) {
    rc->aborted = 1;
    rc->abort_pc = rc->pc;
    return SPTIR_NULL;
  }

  /* Load from stack: emit SLOAD with type from actual value. */
  StkId base = rc->ci->func.p + 1;
  TValue *v = s2v(base + reg);
  SPTType t = rec_value_type(v);

  /* Emit a stack load. */
  int ref = sptir_emit(ir, SPTIR_SLOAD, t, SPTIR_NULL, SPTIR_NULL, reg);
  ir->reg_map[rc->frame_base + reg] = ref;
  ir->reg_type[rc->frame_base + reg] = t;

  /* Emit type guard: the loaded value must have the expected type. */
  if (t != SPTT_ANY) {
    sptir_guard(ir, SPTIR_GUARD_T, ref, (int64_t)t, rec_guard_pc(rc));
  }

  if (reg > ir->maxslot) ir->maxslot = reg;
  return ref;
}

/* Load a constant from the constant table. */
static int rec_load_k(SPTRecCtx *rc, int kidx) {
  SPTIRBuilder *ir = rc->ir;
  const TValue *v = &rc->k[kidx];
  switch (ttypetag(v)) {
    case LUA_VNIL:    return sptir_emit(ir, SPTIR_NIL, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
    case LUA_VFALSE:  return sptir_emit(ir, SPTIR_FALSE, SPTT_FALSE, SPTIR_NULL, SPTIR_NULL, 0);
    case LUA_VTRUE:   return sptir_emit(ir, SPTIR_TRUE, SPTT_TRUE, SPTIR_NULL, SPTIR_NULL, 0);
    case LUA_VNUMINT: return sptir_kint(ir, ivalue(v));
    case LUA_VNUMFLT: return sptir_kflt(ir, fltvalue(v));
    case LUA_VSHRSTR:
    case LUA_VLNGSTR: return sptir_kstr(ir, (void*)tsvalue(v));
    default:          return sptir_kptr(ir, (void*)gcvalue(v));
  }
}

/* Load RKC: either register or constant. */
static int rec_load_rkc(SPTRecCtx *rc, Instruction i) {
  if (TESTARG_k(i))
    return rec_load_k(rc, GETARG_C(i));
  else
    return rec_load_reg(rc, GETARG_C(i));
}

/* Read the runtime type of array[idx] during recording, so the loaded value can
   be typed and a type guard emitted. Returns SPTT_ANY if the slot isn't an
   array, the index is out of range, or the element isn't a type we trace. */
/* Recover the record-time integer behind an IR ref (constant or stack load). */
static int rec_eval_int(SPTRecCtx *rc, int ref, lua_Integer *out) {
  SPTIRInst *in = sptir_get(rc->ir, ref);
  if (!in) return 0;
  if (in->op == SPTIR_KINT) { *out = (lua_Integer)in->aux; return 1; }
  if (in->op == SPTIR_SLOAD) {
    TValue *v = s2v((rc->ci->func.p + 1) + (int)in->aux);
    if (ttisinteger(v)) { *out = ivalue(v); return 1; }
  }
  return 0;
}

/* Recover the record-time TValue behind an IR ref that denotes a *container
   element/field*, following SLOAD (stack slot), GETI (List element), and
   GETFIELD (Map field by interned short-string key). Fills *out and returns 1
   on success, 0 if undeterminable. This unifies arbitrary chained container
   access -- a[i][j], m["k"][j], m["k1"]["k2"], g.list[j] (g via SLOAD of an
   upvalue-backed local) -- so element types can be predicted; the value is
   always bounds/type-guarded at run time, so this is only a prediction.
   List vs Map travels with the TValue tag (LUA_VARRAY vs LUA_VTABLE), so each
   level checks the matching tag before using avalue()/hvalue(). */
static int rec_eval_container(SPTRecCtx *rc, int ref, TValue *out) {
  SPTIRInst *in = sptir_get(rc->ir, ref);
  if (!in) return 0;
  switch (in->op) {
    case SPTIR_SLOAD: {
      *out = *s2v((rc->ci->func.p + 1) + (int)in->aux);
      return 1;
    }
    case SPTIR_GETI: {                       /* element of a List */
      TValue cont;
      if (!rec_eval_container(rc, in->op1, &cont) || !ttisarray(&cont)) return 0;
      Table *arr = avalue(&cont);
      lua_Integer idx;
      if (!rec_eval_int(rc, in->op2, &idx)) return 0;
      if (idx < 0 || (lua_Unsigned)idx >= arr->loglen) return 0;
      out->tt_ = *getArrTag(arr, (lua_Unsigned)idx);
      out->value_ = *getArrVal(arr, (lua_Unsigned)idx);
      return 1;
    }
    case SPTIR_GETFIELD: {                   /* field of a Map by interned key */
      TValue cont;
      if (!rec_eval_container(rc, in->op1, &cont) || !ttistable(&cont)) return 0;
      TString *key = (TString *)(intptr_t)in->aux;
      /* Fills *out with the field value (or a nil tag if absent); the caller's
         ttisarray/ttistable check then rejects an absent or wrong-typed field. */
      luaH_getshortstr(rc->L, hvalue(&cont), key, out);
      return 1;
    }
    default: return 0;
  }
}

/* Recover the record-time List (array-mode Table*) behind an IR ref. NULL if it
   does not resolve to a List value. */
static Table *rec_eval_array(SPTRecCtx *rc, int ref) {
  TValue v;
  if (!rec_eval_container(rc, ref, &v)) return NULL;
  return ttisarray(&v) ? avalue(&v) : NULL;
}

/* Snapshot for a guard at the current PC, EXCEPT inside an inlined pure-read
   method body (frame_base!=0 with method_resume_snap armed): there we reuse the
   one resume snapshot whose exit PC is the caller's SELF instruction, so a guard
   failure re-executes SELF+CALL+method in the interpreter rather than resuming at
   a callee PC with no callee frame. Pure-read bodies have no committed side
   effects, so re-execution is idempotent. Everywhere else: a fresh snapshot. */
static int rec_snap(SPTRecCtx *rc) {
  if (rc->frame_base != 0 && rc->method_resume_snap >= 0)
    return rc->method_resume_snap;
  if (rc->call_arg_self_pc != NULL)            /* arg-load window: resume at the SELF */
    return sptir_snapshot(rc->ir, rc->call_arg_self_pc);
  return sptir_snapshot(rc->ir, rc->pc);
}

/* The exit PC a guard should resume at. Inside an inlined pure-read method body
   it is the caller's SELF instruction (so a guard failure re-executes the call
   in the interpreter); everywhere else it is the current PC. Used for guards
   built via sptir_guard(), which create their own snapshot at the given PC. */
static const Instruction *rec_guard_pc(SPTRecCtx *rc) {
  if (rc->frame_base != 0 && rc->method_self_pc != NULL)
    return rc->method_self_pc;
  if (rc->call_arg_self_pc != NULL)            /* arg-load window: resume at the SELF */
    return rc->call_arg_self_pc;
  return rc->pc;
}

static SPTType rec_array_elem_type(SPTRecCtx *rc, int areg, lua_Integer idx) {
  /* Resolve the array through the IR ref so chained m[i][j] works: the
     intermediate array isn't on the stack (its slot is reused), but it is in
     the IR. The inner array-producing GETI is deliberately NOT LICM-hoisted
     (see ra_hoist_invariants: SPTT_ARR/SPTT_TAB excluded), so it is recomputed
     each inner iteration -- the same correct path the variable-index m[i][j]
     already uses. */
  Table *t = rec_eval_array(rc, rc->ir->reg_map[rc->frame_base + areg]);
  if (!t) return SPTT_ANY;
  /* The index is predicted from the index slot's stack value, but one-pass
     recording reads it at the back-edge (end of iteration). If the compiler
     reused the index register for another value (e.g. the loop accumulator),
     that value is stale and may be out of range. Fall back to element 0 for the
     TYPE prediction; the actual index is bounds-guarded and the GETI load is
     type-guarded, so correctness holds regardless of which element is loaded
     (a misprediction merely side-exits). Empty arrays still can't predict. */
  if (idx < 0 || (lua_Unsigned)idx >= t->loglen) {
    if (t->loglen == 0) return SPTT_ANY;
    idx = 0;
  }
  lu_byte tag = *getArrTag(t, (lua_Unsigned)idx);
  TValue tmp;
  tmp.tt_ = tag;
  tmp.value_ = *getArrVal(t, (lua_Unsigned)idx);
  return rec_value_type(&tmp);
}

/* Forward declaration: the branch helpers below emit comparison guards. */
static int rec_compare(SPTRecCtx *rc, SPTIROp op, int a_ref, int b_ref,
                       SPTType a_type, SPTType b_type);

/* Numeric value of a TValue as double, for record-time branch evaluation. */
static double rec_num_as_double(const TValue *v) {
  if (ttisinteger(v)) return (double)ivalue(v);
  if (ttisfloat(v)) return fltvalue(v);
  return 0.0;
}

/* Recover a record-time numeric value to PREDICT a branch direction. The
   prediction only needs to be a good heuristic -- correctness is enforced by
   the guard regardless of which arm we record. Rules:
     - KINT/KFLT  -> the IR constant (reused-slot-safe: a slot that held a
                     LOADK constant may have since been reassigned on the stack,
                     so we must NOT read the stack for it).
     - otherwise  -> the operand's stack slot 'reg' (an SLOAD reads the current
                     value; a value computed last iteration is a fine predictor).
   'reg' < 0 means the operand has no stack slot (a pure immediate/constant). */
static int rec_pred_num(SPTRecCtx *rc, int ref, int reg, double *out) {
  SPTIRInst *in = sptir_get(rc->ir, ref);
  if (in && in->op == SPTIR_KINT) { *out = (double)(int64_t)in->aux; return 1; }
  if (in && in->op == SPTIR_KFLT) { double d; memcpy(&d, &in->aux, sizeof d); *out = d; return 1; }
  if (reg >= 0) {
    TValue *v = s2v((rc->ci->func.p + 1) + reg);
    if (ttisinteger(v)) { *out = (double)ivalue(v); return 1; }
    if (ttisfloat(v))   { *out = fltvalue(v); return 1; }
  }
  return 0;
}

/* Record-time truthiness behind an IR ref (for OP_TEST/TESTSET). Boolean and
   nil constants and an SLOAD's tag determine it; a value whose IR type already
   pins truthiness (everything except SPTT_ANY) is decidable too. */
static int rec_ref_truthy(SPTRecCtx *rc, int ref, int *out) {
  SPTIRInst *in = sptir_get(rc->ir, ref);
  if (!in) return 0;
  switch (in->op) {
    case SPTIR_NIL: case SPTIR_FALSE: *out = 0; return 1;
    case SPTIR_TRUE: *out = 1; return 1;
    case SPTIR_SLOAD: {
      TValue *v = s2v((rc->ci->func.p + 1) + (int)in->aux);
      *out = !l_isfalse(v); return 1;
    }
    default: break;
  }
  switch ((SPTType)in->type) {
    case SPTT_NIL: case SPTT_FALSE: *out = 0; return 1;
    case SPTT_ANY: return 0;
    default:       *out = 1; return 1; /* int/flt/str/arr/tab/func/ud/true */
  }
}

/* Evaluate a numeric comparison at record time. */
static int rec_eval_num_cmp(SPTIROp op, double a, double b) {
  switch (op) {
    case SPTIR_LT: return a < b;
    case SPTIR_LE: return a <= b;
    case SPTIR_GT: return a > b;
    case SPTIR_GE: return a >= b;
    case SPTIR_EQ: return a == b;
    case SPTIR_NE: return a != b;
    default:       return 0;
  }
}

/* The opposite comparison (used when the branch, not the fall-through, runs). */
static SPTIROp rec_negate_cmp(SPTIROp op) {
  switch (op) {
    case SPTIR_LT: return SPTIR_GE;
    case SPTIR_GE: return SPTIR_LT;
    case SPTIR_LE: return SPTIR_GT;
    case SPTIR_GT: return SPTIR_LE;
    case SPTIR_EQ: return SPTIR_NE;
    case SPTIR_NE: return SPTIR_EQ;
    default:       return op;
  }
}

static int rec_inst(SPTRecCtx *rc); /* fwd decl: if-conversion records arm ops */

/* ---- If-conversion (branchless select) ----------------------------------
   A simple integer `if (c) {slot = A} [else {slot = B}]` can be compiled
   without a side exit by materializing the condition as a 0/1 integer and
   computing  slot = B + (A - B)*c  with ordinary integer arithmetic. Both arms
   are evaluated unconditionally, so this is only valid when each arm is a single
   NON-TRAPPING integer value-producing op (no div/mod/pow: the untaken arm must
   not be able to fault) writing the same slot. Integer add/sub/mul/bitwise wrap
   identically whether branched or computed, so the result is bit-identical to
   the branch. Anything outside this shape falls back to the guarded branch. */

/* Is this opcode a single, non-trapping, value-producing op we can if-convert?
   Excludes DIV/IDIV/MOD/POW (can trap), all control flow, calls, and memory. */
static int opcode_is_ifconv_safe(OpCode o) {
  switch (o) {
    case OP_MOVE:
    case OP_LOADI:
    case OP_ADD: case OP_SUB: case OP_MUL:
    case OP_ADDI:
    case OP_ADDK: case OP_SUBK: case OP_MULK:
    case OP_BAND: case OP_BOR: case OP_BXOR:
    case OP_BANDK: case OP_BORK: case OP_BXORK:
    case OP_SHL: case OP_SHR:
    case OP_BNOT: case OP_UNM:
      return 1;
    default:
      return 0;
  }
}

#define IFCONV_MAX_SLOTS 4

static int ifconv_reg_is_int(SPTRecCtx *rc, int reg);
static int ifconv_arm_int_result(SPTRecCtx *rc, const Instruction *op_pc);
static int opcode_is_ifconv_safe_flt(OpCode o);
static int ifconv_arm_flt_result(SPTRecCtx *rc, const Instruction *op_pc);

/* Parse an if-conversion arm body [start,end): a straight-line run of
   non-trapping value-producing ops (integer when is_flt==0, float when is_flt==1),
   each optionally followed by its MMBIN marker, and nothing else. Unions the
   distinct destination slots into wslots (which already holds *nw entries from a
   previously parsed arm). Rejects: a non-convertible op, a wrong-typed-result op,
   an operand that was written earlier in THIS arm (an intra-arm dependency would
   make the pre-recording type check unreliable), and more than IFCONV_MAX_SLOTS
   slots. Returns 1 on success (and only if the arm writes at least one slot). */
static int parse_ifconv_arm(SPTRecCtx *rc, const Instruction *start,
                            const Instruction *end, int *wslots, int *nw,
                            int is_flt) {
  int local[16], nlocal = 0;          /* slots written so far within this arm */
  int wrote = 0, nops = 0;
  for (const Instruction *pc = start; pc < end; pc++) {
    OpCode o = GET_OPCODE(*pc);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (++nops > 16) return 0;
    if (is_flt) {
      if (!opcode_is_ifconv_safe_flt(o)) return 0;
      if (!ifconv_arm_flt_result(rc, pc)) return 0;
    } else {
      if (!opcode_is_ifconv_safe(o)) return 0;
      if (!ifconv_arm_int_result(rc, pc)) return 0;
    }
    int b = GETARG_B(*pc), c = GETARG_C(*pc);
    int checkB = (o != OP_LOADI && o != OP_LOADF && o != OP_LOADK);
    int checkC = (o == OP_ADD || o == OP_SUB || o == OP_MUL || o == OP_DIV ||
                  o == OP_BAND || o == OP_BOR || o == OP_BXOR ||
                  o == OP_SHL || o == OP_SHR);
    for (int k = 0; k < nlocal; k++) {
      if (checkB && local[k] == b) return 0;
      if (checkC && local[k] == c) return 0;
    }
    int a = GETARG_A(*pc);
    int seen = 0;
    for (int k = 0; k < nlocal; k++) if (local[k] == a) { seen = 1; break; }
    if (!seen) { if (nlocal >= 16) return 0; local[nlocal++] = a; }
    seen = 0;
    for (int k = 0; k < *nw; k++) if (wslots[k] == a) { seen = 1; break; }
    if (!seen) { if (*nw >= IFCONV_MAX_SLOTS) return 0; wslots[(*nw)++] = a; }
    wrote = 1;
  }
  return wrote;
}

/* Is register `reg` currently an integer? Uses the trace's tracked type if the
   slot is already materialized, otherwise reads the live stack value's type --
   so a loop-carried accumulator first referenced inside the arms (not yet loaded
   at the comparison) is still classified without emitting any IR. Root frame. */
static int ifconv_reg_is_int(SPTRecCtx *rc, int reg) {
  SPTIRBuilder *ir = rc->ir;
  if (ir->reg_map[rc->frame_base + reg] >= 0)
    return ir->reg_type[rc->frame_base + reg] == SPTT_INT;
  /* Not materialized. Inside an inlined callee (frame_base != 0) the live stack
     slot is NOT at (ci->func+1)+reg -- that addresses the root frame -- so we
     cannot determine the type and must bail (the caller falls back cleanly). */
  if (rc->frame_base != 0) return 0;
  return ttisinteger(s2v((rc->ci->func.p + 1) + reg));
}

/* Will this arm op produce an INTEGER result, given the current operand types?
   Checked before recording so a float-producing arm cleanly falls back to the
   guarded branch instead of being recorded and then aborted. */
static int ifconv_arm_int_result(SPTRecCtx *rc, const Instruction *op_pc) {
  Instruction ins = *op_pc;
  OpCode o = GET_OPCODE(ins);
  int b = GETARG_B(ins), c = GETARG_C(ins);
  /* Probe operand register types lazily, inside the cases that actually use B
     (and C) as registers: for LOADI the B/Bx field is an immediate, not a
     register, so reading reg B's type would index off the end of the stack
     (caught by ASan). */
  switch (o) {
    case OP_LOADI: return 1;
    case OP_MOVE: case OP_UNM: case OP_BNOT: case OP_ADDI:
      return ifconv_reg_is_int(rc, b);
    case OP_ADD: case OP_SUB: case OP_MUL:
    case OP_BAND: case OP_BOR: case OP_BXOR:
    case OP_SHL: case OP_SHR:
      return ifconv_reg_is_int(rc, b) && ifconv_reg_is_int(rc, c);
    case OP_ADDK: case OP_SUBK: case OP_MULK:
    case OP_BANDK: case OP_BORK: case OP_BXORK:
      return ifconv_reg_is_int(rc, b) && ttisinteger(&rc->p->k[c]);
    default: return 0;
  }
}

/* Float analogues. A float slot's live type, with the same callee-frame caveat. */
static int ifconv_reg_is_flt(SPTRecCtx *rc, int reg) {
  SPTIRBuilder *ir = rc->ir;
  if (ir->reg_map[rc->frame_base + reg] >= 0)
    return ir->reg_type[rc->frame_base + reg] == SPTT_FLT;
  if (rc->frame_base != 0) return 0;
  return ttisfloat(s2v((rc->ci->func.p + 1) + reg));
}

/* Is this op a non-trapping FLOAT-producing arm op? Float arithmetic never traps
   (IEEE: div-by-zero -> inf, no exception), so even DIV is admissible in an arm
   whose untaken side is still evaluated -- unlike the integer case. */
static int opcode_is_ifconv_safe_flt(OpCode o) {
  switch (o) {
    case OP_MOVE: case OP_LOADF: case OP_LOADK:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
    case OP_UNM:
      return 1;
    default: return 0;
  }
}

/* A proto body op is statically inlinable as a conditional-assignment arm if it
   is a non-trapping value op of EITHER numeric kind -- the actual int/float type
   is resolved at record time (the callee if-conversion picks the right select),
   so the static gate accepts the union. */
static int opcode_is_ifconv_safe_any(OpCode o) {
  return opcode_is_ifconv_safe(o) || opcode_is_ifconv_safe_flt(o);
}

/* Will this arm op produce a FLOAT result, given the current operand types? */
static int ifconv_arm_flt_result(SPTRecCtx *rc, const Instruction *op_pc) {
  Instruction ins = *op_pc;
  OpCode o = GET_OPCODE(ins);
  int b = GETARG_B(ins), c = GETARG_C(ins);
  /* Probe operand register types lazily, inside the cases that use B (and C) as
     registers: LOADF/LOADK carry an immediate / constant index in the B/Bx
     field, not a register, so reading reg B's type would run off the stack
     (caught by ASan). */
  switch (o) {
    case OP_LOADF: return 1;
    case OP_LOADK: return ttisfloat(&rc->p->k[GETARG_Bx(ins)]);
    case OP_MOVE: case OP_UNM:
      return ifconv_reg_is_flt(rc, b);
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
      return ifconv_reg_is_flt(rc, b) && ifconv_reg_is_flt(rc, c);
    case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
      return ifconv_reg_is_flt(rc, b) && ttisfloat(&rc->p->k[c]);
    default: return 0;
  }
}

/* Try to if-convert the comparison at rc->pc. Returns 1 if it took the
   conversion path (caller returns 1 unless rc->aborted got set), 0 if not
   applicable (caller falls back to the guarded branch, leaving state untouched). */
static int rec_try_ifconv(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                          SPTType at, SPTType bt) {
  /* A comparison counts as float if EITHER operand is float -- LTI/LEI/GTI/GEI
     pass an integer immediate for the second operand even when the first is a
     float (e.g. `v < 0.0` compiles to `LTI v, 0`), so the int side is promoted
     to double below for the mask. Both must be numeric. */
  if (!(sptt_isnum(at) && sptt_isnum(bt))) return 0;
  int is_flt_cmp = (at == SPTT_FLT || bt == SPTT_FLT);
  SPTIRBuilder *ir = rc->ir;
  int fb = rc->frame_base;                         /* 0 at root; nonzero in an inlined callee */
  const Instruction *cmp_pc = rc->pc;
  const Instruction *jmp = cmp_pc + 1;
  if (GET_OPCODE(*jmp) != OP_JMP) return 0;
  const Instruction *T1 = jmp + 1 + GETARG_sJ(*jmp); /* JMP1 target */
  if (T1 <= cmp_pc + 2) return 0;                 /* must skip forward over the then-arm */

  const Instruction *then_start = cmp_pc + 2, *then_end, *else_start = NULL, *merge;
  int is_ifelse = 0;
  if (GET_OPCODE(T1[-1]) == OP_JMP) {             /* if-else: JMP2 just before else */
    const Instruction *jmp2 = T1 - 1;
    merge = jmp2 + 1 + GETARG_sJ(*jmp2);
    if (merge <= T1) return 0;                    /* JMP2 must skip forward over the else-arm */
    is_ifelse = 1; then_end = jmp2; else_start = T1;
  } else {                                        /* if-only */
    merge = T1; then_end = T1;
  }

  /* Decide int vs float select from what the arms WRITE, not just the
     comparison: a branch with an INTEGER condition but FLOAT arms (e.g.
     `if(i%2==0){s=s+v}else{s=s+1.0}`) still needs the bit-exact float blend,
     with the mask built from the integer compare (ICMPMASK). Peek the first
     arm op's destination slot to classify; a wrong guess just makes the arm
     parse below fail and we fall back cleanly. */
  int arm_is_flt = 0;
  for (const Instruction *pc = then_start; pc < then_end; pc++) {
    OpCode o = GET_OPCODE(*pc);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    arm_is_flt = ifconv_reg_is_flt(rc, GETARG_A(*pc));
    break;
  }
  int is_flt = is_flt_cmp || arm_is_flt;
  SPTType want = is_flt ? SPTT_FLT : SPTT_INT;

  /* Parse both arms, unioning the set of written slots. An arm that does not
     write a given slot leaves it unchanged (its select input is the old value). */
  int wslots[IFCONV_MAX_SLOTS]; int nw = 0;
  if (!parse_ifconv_arm(rc, then_start, then_end, wslots, &nw, is_flt)) return 0;
  if (is_ifelse && !parse_ifconv_arm(rc, else_start, merge, wslots, &nw, is_flt)) return 0;

  /* Every written slot must currently hold the expected type: an if-only arm
     leaves the other branch as the slot's old value, and arms may read the slot
     (s=s+1). Inside a callee an unmaterialized slot bails here (the type probe
     returns 0), so the rec_load_reg below never hits its callee-frame abort. */
  for (int k = 0; k < nw; k++)
    if (!(is_flt ? ifconv_reg_is_flt(rc, wslots[k])
                 : ifconv_reg_is_int(rc, wslots[k]))) return 0;

  /* ---- commit. Make all written slots resident (load + type-guard live-ins),
     then record each arm against a forked copy and build one select per slot. */
  int old_ref[IFCONV_MAX_SLOTS], then_ref[IFCONV_MAX_SLOTS], else_ref[IFCONV_MAX_SLOTS];
  for (int k = 0; k < nw; k++) {
    old_ref[k] = rec_load_reg(rc, wslots[k]);
    if (rc->aborted) return 1;
  }

  rc->pc = then_start;                              /* record the then-arm */
  while (rc->pc < then_end) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != then_end) { rc->aborted = 1; return 1; }
  for (int k = 0; k < nw; k++) {
    if (ir->reg_type[fb + wslots[k]] != want) { rc->aborted = 1; return 1; }
    then_ref[k] = ir->reg_map[fb + wslots[k]];
  }
  for (int k = 0; k < nw; k++) {
    ir->reg_map[fb + wslots[k]] = old_ref[k]; ir->reg_type[fb + wslots[k]] = want;
  }

  if (is_ifelse) {                                  /* record the else-arm */
    rc->pc = else_start;
    while (rc->pc < merge) { if (!rec_inst(rc)) return 1; }
    if (rc->pc != merge) { rc->aborted = 1; return 1; }
    for (int k = 0; k < nw; k++) {
      if (ir->reg_type[fb + wslots[k]] != want) { rc->aborted = 1; return 1; }
      else_ref[k] = ir->reg_map[fb + wslots[k]];
    }
    for (int k = 0; k < nw; k++) {
      ir->reg_map[fb + wslots[k]] = old_ref[k]; ir->reg_type[fb + wslots[k]] = want;
    }
  } else {
    for (int k = 0; k < nw; k++) else_ref[k] = old_ref[k];
  }

  if (is_flt) {
    /* Bit-exact blend: mask = (a fop b) ? all-ones : all-zeros; per slot
       result = (then & mask) | (else & ~mask). A float select via the integer
       trick else+(then-else)*cond would round, so it must NOT be used here.
       The mask comes from a float compare (FCMPMASK, with the int side promoted
       and GT/GE emitted as swapped LT/LE -- CMPSD has no >/>= predicate that
       keeps Lua's NaN-as-false sense) when either operand is float, or from an
       integer compare (ICMPMASK, no swap) when the condition is integer.
       One shared mask across all slots. */
    int mask;
    if (is_flt_cmp) {
      int ca = aref, cb = bref;
      if (at == SPTT_INT) ca = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, aref, -1, 0);
      if (bt == SPTT_INT) cb = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, bref, -1, 0);
      int ma = ca, mb = cb; SPTIROp mop = fop;
      if (fop == SPTIR_GT)      { mop = SPTIR_LT; ma = cb; mb = ca; }
      else if (fop == SPTIR_GE) { mop = SPTIR_LE; ma = cb; mb = ca; }
      mask = sptir_emit(ir, SPTIR_FCMPMASK, SPTT_FLT, ma, mb, (int64_t)mop);
    } else {
      mask = sptir_emit(ir, SPTIR_ICMPMASK, SPTT_FLT, aref, bref, (int64_t)fop);
    }
    for (int k = 0; k < nw; k++) {
      int sel = sptir_emit(ir, SPTIR_FSELECT, SPTT_FLT,
                           then_ref[k], else_ref[k], (int64_t)mask);
      ir->reg_map[fb + wslots[k]] = sel;
      ir->reg_type[fb + wslots[k]] = SPTT_FLT;
      if (fb + wslots[k] > ir->maxslot) ir->maxslot = fb + wslots[k];
    }
  } else {
    /* select_k = else_k + (then_k - else_k) * cond,  cond in {0,1} (one shared cmp) */
    int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
    for (int k = 0; k < nw; k++) {
      int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, then_ref[k], else_ref[k], 0);
      int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
      int res  = sptir_emit(ir, SPTIR_ADD, SPTT_INT, else_ref[k], prod, 0);
      ir->reg_map[fb + wslots[k]] = res;
      ir->reg_type[fb + wslots[k]] = SPTT_INT;
      if (fb + wslots[k] > ir->maxslot) ir->maxslot = fb + wslots[k];
    }
  }
  rc->pc = merge;
  return 1;
}

/* When recording an inlined callee whose entire body is a conditional return
   `if(c){return A} return B`, if-convert it: record both return values against a
   forked callee frame and bind the caller's result slot to a branchless select,
   then resume in the caller -- exactly like an OP_RETURN1 but with no side exit.
   Both arms INT (with an INT compare) -> integer select B+(A-B)*c. Both arms
   FLOAT -> bit-exact masked blend (FCMPMASK/ICMPMASK + FSELECT, the same machinery
   rec_try_ifconv uses for conditional assignment: the integer trick would round,
   so it must NOT be used for floats; GT/GE compile as swapped LT/LE to keep Lua's
   NaN-as-false sense). Mixed arms, or int arms under a float compare, abort
   cleanly. Returns 1 if it took this path (caller returns 1 unless rc->aborted),
   0 if not applicable (the caller falls back to the guarded branch). */
/* Decode a comparison instruction into (fop, aref, bref, at, bt) -- the same
   loading + fop-from-k logic the per-opcode OP_LT/OP_LE/... handlers use, factored
   out so the chained cond-return recorder can decode the SECOND+ compares of a
   chain (which it processes manually rather than via rec_inst). fop is the
   condition under which the THEN-arm (fall-through) is taken, exactly as the
   handlers pass to rec_cond_branch. Handles the ordered + equality forms that
   appear in clamp-style chains; returns 0 for anything else (caller aborts). */
static int op_is_comparison(OpCode o);     /* defined below, used by the chained recorder */
static int decode_compare(SPTRecCtx *rc, const Instruction *pc,
                          SPTIROp *fop, int *aref, int *bref, SPTType *at, SPTType *bt) {
  SPTIRBuilder *ir = rc->ir;
  Instruction i = *pc;
  OpCode o = GET_OPCODE(i);
  int a = GETARG_A(i), k = GETARG_k(i);
  int fb = rc->frame_base;
  switch (o) {
    case OP_LT: case OP_LE: case OP_EQ: {
      int b = GETARG_B(i);
      *aref = rec_load_reg(rc, a);
      *bref = rec_load_reg(rc, b);
      *at = ir->reg_type[fb + a]; *bt = ir->reg_type[fb + b];
      if (o == OP_EQ && (*at == SPTT_STR || *bt == SPTT_STR)) return 0;  /* string EQ unsafe here */
      *fop = (o == OP_LT) ? (k ? SPTIR_GE : SPTIR_LT)
           : (o == OP_LE) ? (k ? SPTIR_GT : SPTIR_LE)
                          : (k ? SPTIR_NE : SPTIR_EQ);
      return 1;
    }
    case OP_LTI: case OP_LEI: case OP_GTI: case OP_GEI: case OP_EQI: {
      int sb = GETARG_sB(i);
      *aref = rec_load_reg(rc, a);
      *bref = sptir_kint(ir, sb);
      *at = ir->reg_type[fb + a]; *bt = SPTT_INT;
      *fop = (o == OP_LTI) ? (k ? SPTIR_GE : SPTIR_LT)
           : (o == OP_LEI) ? (k ? SPTIR_GT : SPTIR_LE)
           : (o == OP_GTI) ? (k ? SPTIR_LE : SPTIR_GT)
           : (o == OP_GEI) ? (k ? SPTIR_LT : SPTIR_GE)
                           : (k ? SPTIR_NE : SPTIR_EQ);
      return 1;
    }
    default:
      return 0;
  }
}

/* Emit one branchless select: result = cond ? then : else, with cond = fop(aref,
   bref). Integer arms under an integer compare use the rounding-free trick
   else+(then-else)*c; float arms use a bit-exact masked blend (FCMPMASK/ICMPMASK +
   FSELECT; GT/GE as swapped LT/LE to keep Lua's NaN-as-false). Mixed/unsupported
   arm types abort. Returns 1 with *out_ref/*out_t set, else 0 (rc->aborted set).
   Factored out for the chained cond-return fold; the single if-converters keep
   their own inline copy untouched. */
static int emit_select(SPTRecCtx *rc, SPTIROp fop, int aref, int bref, SPTType at, SPTType bt,
                       int then_ref, SPTType then_t, int else_ref, SPTType else_t,
                       int *out_ref, SPTType *out_t) {
  SPTIRBuilder *ir = rc->ir;
  if (then_t == SPTT_INT && else_t == SPTT_INT && at == SPTT_INT && bt == SPTT_INT) {
    int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
    int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, then_ref, else_ref, 0);
    int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
    *out_ref = sptir_emit(ir, SPTIR_ADD, SPTT_INT, else_ref, prod, 0);
    *out_t = SPTT_INT;
    return 1;
  }
  if (then_t == SPTT_FLT && else_t == SPTT_FLT) {
    int is_flt_cmp = (at == SPTT_FLT || bt == SPTT_FLT);
    int mask;
    if (is_flt_cmp) {
      int ca = aref, cb = bref;
      if (at == SPTT_INT) ca = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, aref, -1, 0);
      if (bt == SPTT_INT) cb = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, bref, -1, 0);
      int ma = ca, mb = cb; SPTIROp mop = fop;
      if (fop == SPTIR_GT)      { mop = SPTIR_LT; ma = cb; mb = ca; }
      else if (fop == SPTIR_GE) { mop = SPTIR_LE; ma = cb; mb = ca; }
      mask = sptir_emit(ir, SPTIR_FCMPMASK, SPTT_FLT, ma, mb, (int64_t)mop);
    } else {
      mask = sptir_emit(ir, SPTIR_ICMPMASK, SPTT_FLT, aref, bref, (int64_t)fop);
    }
    *out_ref = sptir_emit(ir, SPTIR_FSELECT, SPTT_FLT, then_ref, else_ref, (int64_t)mask);
    *out_t = SPTT_FLT;
    return 1;
  }
  rc->aborted = 1;
  return 0;
}

static int rec_try_condreturn_ifconv(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                                     SPTType at, SPTType bt) {
  if (rc->frame_base == 0) return 0;              /* only inside an inlined callee */
  if (!sptt_isnum(at) || !sptt_isnum(bt)) return 0; /* numeric compare (int/float) */
  SPTIRBuilder *ir = rc->ir;
  int fb = rc->frame_base;
  const Instruction *cmp_pc = rc->pc;
  const Instruction *jmp = cmp_pc + 1;
  if (GET_OPCODE(*jmp) != OP_JMP) return 0;
  const Instruction *T1 = jmp + 1 + GETARG_sJ(*jmp);
  const Instruction *cend = rc->p->code + rc->p->sizecode;
  if (T1 <= cmp_pc + 2 || T1 >= cend) return 0;
  if (GET_OPCODE(T1[-1]) != OP_RETURN1) return 0; /* then-arm ends in a return */
  const Instruction *then_ret = T1 - 1;
  for (const Instruction *q = cmp_pc + 2; q < then_ret; q++) {
    /* condreturn_method_op_ok is the superset (ifconv-safe + GETFIELD/LEN); a
       free-function callee was already gated to ifconv-safe-only by
       proto_is_condreturn_inlinable, so it never carries a field read here. */
    if (!condreturn_method_op_ok(GET_OPCODE(*q))) return 0;
  }
  const Instruction *else_ret = NULL;             /* else-arm: first return */
  for (const Instruction *q = T1; q < cend; q++) {
    OpCode o = GET_OPCODE(*q);
    if (o == OP_RETURN1) { else_ret = q; break; }
    if (!condreturn_method_op_ok(o)) return 0;
  }
  if (!else_ret) return 0;
  int then_reg = GETARG_A(*then_ret), else_reg = GETARG_A(*else_ret);

  int cmax = rc->p->maxstacksize;
  if (cmax > 64) return 0;
  int save_map[64]; SPTType save_type[64];
  for (int k = 0; k < cmax; k++) { save_map[k] = ir->reg_map[fb+k]; save_type[k] = ir->reg_type[fb+k]; }

  /* ---- commit: record the then-arm's value computation against a forked frame */
  rc->pc = cmp_pc + 2;
  while (rc->pc < then_ret) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != then_ret) { rc->aborted = 1; return 1; }
  int then_ref = ir->reg_map[fb + then_reg];
  SPTType then_t = ir->reg_type[fb + then_reg];
  if (then_ref < 0 || !sptt_isnum(then_t)) { rc->aborted = 1; return 1; }
  for (int k = 0; k < cmax; k++) { ir->reg_map[fb+k] = save_map[k]; ir->reg_type[fb+k] = save_type[k]; }

  /* ---- record the else-arm's value computation */
  rc->pc = T1;
  while (rc->pc < else_ret) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != else_ret) { rc->aborted = 1; return 1; }
  int else_ref = ir->reg_map[fb + else_reg];
  SPTType else_t = ir->reg_type[fb + else_reg];
  if (else_ref < 0 || !sptt_isnum(else_t)) { rc->aborted = 1; return 1; }
  for (int k = 0; k < cmax; k++) { ir->reg_map[fb+k] = save_map[k]; ir->reg_type[fb+k] = save_type[k]; }

  /* ---- branchless select, bind to caller, resume in caller */
  int res; SPTType res_t;
  if (then_t == SPTT_INT && else_t == SPTT_INT && at == SPTT_INT && bt == SPTT_INT) {
    /* integer: result = else + (then-else)*cond, cond in {0,1} (one shared cmp) */
    int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
    int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, then_ref, else_ref, 0);
    int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
    res = sptir_emit(ir, SPTIR_ADD, SPTT_INT, else_ref, prod, 0);
    res_t = SPTT_INT;
  } else if (then_t == SPTT_FLT && else_t == SPTT_FLT) {
    /* float: bit-exact masked blend = (then & mask) | (else & ~mask). The integer
       else+(then-else)*cond trick would round, so it is NOT used here. Mask from a
       float compare (FCMPMASK, int side promoted, GT/GE as swapped LT/LE to keep
       Lua's NaN-as-false sense) when either operand is float, else an integer
       compare (ICMPMASK). Mirrors rec_try_ifconv's float select. */
    int is_flt_cmp = (at == SPTT_FLT || bt == SPTT_FLT);
    int mask;
    if (is_flt_cmp) {
      int ca = aref, cb = bref;
      if (at == SPTT_INT) ca = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, aref, -1, 0);
      if (bt == SPTT_INT) cb = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, bref, -1, 0);
      int ma = ca, mb = cb; SPTIROp mop = fop;
      if (fop == SPTIR_GT)      { mop = SPTIR_LT; ma = cb; mb = ca; }
      else if (fop == SPTIR_GE) { mop = SPTIR_LE; ma = cb; mb = ca; }
      mask = sptir_emit(ir, SPTIR_FCMPMASK, SPTT_FLT, ma, mb, (int64_t)mop);
    } else {
      mask = sptir_emit(ir, SPTIR_ICMPMASK, SPTT_FLT, aref, bref, (int64_t)fop);
    }
    res = sptir_emit(ir, SPTIR_FSELECT, SPTT_FLT, then_ref, else_ref, (int64_t)mask);
    res_t = SPTT_FLT;
  } else {
    rc->aborted = 1; return 1;            /* mixed arms / int-arms-under-float-cmp: fall back */
  }
  ir->reg_map[rc->call_result_slot] = res;
  ir->reg_type[rc->call_result_slot] = res_t;
  if (rc->call_result_slot > ir->maxslot) ir->maxslot = rc->call_result_slot;
  rc->p = rc->save_p;
  rc->k = rc->save_k;
  rc->cl = rc->save_cl;
  rc->frame_base = rc->save_frame_base;
  rc->pc = rc->save_pc;
  return 1;
}

/* Chained conditional-return (>=2 cond-returns + a final return), e.g. the
   double-sided clamp `if(x<lo) return lo; if(x>hi) return hi; return x`. Folds to a
   NESTED branchless select select(c1, A1, select(c2, A2, ... select(cn, An, Afinal)
   ...)) -- no branches, no side exits. PARALLEL to the single rec_try_condreturn_
   ifconv: only fires when the else-arm is ITSELF a cond-return (a chain); a single
   cond-return returns 0 here and is handled by the untouched single path. Each
   segment's compute is recorded against a FORKED frame (segments are independent --
   each tests the params/fields and computes a return value), the nested compares
   are decoded by decode_compare (rec_inst is not used for them, to avoid the
   single path's bind-and-unwind), and the selects are folded inside-out by
   emit_select. Bit-exact: int segments use the rounding-free integer select, float
   segments the masked blend; mixed-type arms abort cleanly. */
#define SPT_MAXCHAIN 8
static int rec_try_chained_condreturn_ifconv(SPTRecCtx *rc, SPTIROp fop0, int aref0, int bref0,
                                             SPTType at0, SPTType bt0) {
  if (rc->frame_base == 0) return 0;              /* only inside an inlined callee */
  if (!sptt_isnum(at0) || !sptt_isnum(bt0)) return 0;
  (void)fop0; (void)aref0; (void)bref0;           /* seg0 is re-decoded uniformly below */
  SPTIRBuilder *ir = rc->ir;
  int fb = rc->frame_base;
  const Instruction *cend = rc->p->code + rc->p->sizecode;

  /* --- structural peek: chain only if the else-arm at T1 is itself a cond-return. */
  const Instruction *cmp1 = rc->pc;
  const Instruction *jmp1 = cmp1 + 1;
  if (GET_OPCODE(*jmp1) != OP_JMP) return 0;
  const Instruction *t1 = jmp1 + 1 + GETARG_sJ(*jmp1);
  if (t1 <= cmp1 + 2 || t1 >= cend) return 0;
  if (GET_OPCODE(t1[-1]) != OP_RETURN1) return 0;
  int is_chain = 0;
  for (const Instruction *q = t1; q < cend; q++) {
    OpCode o = GET_OPCODE(*q);
    if (op_is_comparison(o)) { is_chain = 1; break; }
    if (o == OP_RETURN1 || o == OP_RETURN0 || o == OP_RETURN) break;
  }
  if (!is_chain) return 0;                         /* single cond-return -> untouched path */

  int cmax = rc->p->maxstacksize;
  if (cmax > 64) return 0;
  int save_map[64]; SPTType save_type[64];

  SPTIROp s_fop[SPT_MAXCHAIN]; int s_aref[SPT_MAXCHAIN], s_bref[SPT_MAXCHAIN];
  SPTType s_at[SPT_MAXCHAIN], s_bt[SPT_MAXCHAIN];
  int s_A[SPT_MAXCHAIN]; SPTType s_At[SPT_MAXCHAIN];
  int nseg = 0;
  int final_ref = -1; SPTType final_t = SPTT_NIL;
  const Instruction *cur = cmp1;

  for (;;) {
    const Instruction *ci = NULL, *ri = NULL;     /* next compare or return at/after cur */
    for (const Instruction *q = cur; q < cend; q++) {
      OpCode o = GET_OPCODE(*q);
      if (op_is_comparison(o)) { ci = q; break; }
      if (o == OP_RETURN1 || o == OP_RETURN0 || o == OP_RETURN) { ri = q; break; }
    }
    if (ci) {                                      /* a cond-return segment */
      if (nseg >= SPT_MAXCHAIN) { rc->aborted = 1; return 1; }
      const Instruction *jmp = ci + 1;
      if (GET_OPCODE(*jmp) != OP_JMP) { rc->aborted = 1; return 1; }
      const Instruction *t = jmp + 1 + GETARG_sJ(*jmp);
      if (t <= ci + 2 || t > cend) { rc->aborted = 1; return 1; }
      if (GET_OPCODE(t[-1]) != OP_RETURN1) { rc->aborted = 1; return 1; }
      const Instruction *then_ret = t - 1;
      for (int kk = 0; kk < cmax; kk++) { save_map[kk]=ir->reg_map[fb+kk]; save_type[kk]=ir->reg_type[fb+kk]; }
      rc->pc = cur;                                /* record the compare's compute prefix */
      while (rc->pc < ci) { if (!rec_inst(rc)) return 1; }
      if (rc->pc != ci) { rc->aborted = 1; return 1; }
      SPTIROp fp; int ar, br; SPTType a_t, b_t;
      if (!decode_compare(rc, ci, &fp, &ar, &br, &a_t, &b_t)) { rc->aborted = 1; return 1; }
      if (!sptt_isnum(a_t) || !sptt_isnum(b_t)) { rc->aborted = 1; return 1; }
      rc->pc = ci + 2;                             /* record the then-arm's compute of A */
      while (rc->pc < then_ret) { if (!rec_inst(rc)) return 1; }
      if (rc->pc != then_ret) { rc->aborted = 1; return 1; }
      int areg = GETARG_A(*then_ret);
      int aval = ir->reg_map[fb + areg]; SPTType avt = ir->reg_type[fb + areg];
      if (aval < 0 || !sptt_isnum(avt)) { rc->aborted = 1; return 1; }
      for (int kk = 0; kk < cmax; kk++) { ir->reg_map[fb+kk]=save_map[kk]; ir->reg_type[fb+kk]=save_type[kk]; }
      s_fop[nseg]=fp; s_aref[nseg]=ar; s_bref[nseg]=br; s_at[nseg]=a_t; s_bt[nseg]=b_t;
      s_A[nseg]=aval; s_At[nseg]=avt; nseg++;
      cur = t;
    } else if (ri) {                               /* the final return */
      if (GET_OPCODE(*ri) != OP_RETURN1) { rc->aborted = 1; return 1; }
      for (int kk = 0; kk < cmax; kk++) { save_map[kk]=ir->reg_map[fb+kk]; save_type[kk]=ir->reg_type[fb+kk]; }
      rc->pc = cur;
      while (rc->pc < ri) { if (!rec_inst(rc)) return 1; }
      if (rc->pc != ri) { rc->aborted = 1; return 1; }
      int freg = GETARG_A(*ri);
      final_ref = ir->reg_map[fb + freg]; final_t = ir->reg_type[fb + freg];
      if (final_ref < 0 || !sptt_isnum(final_t)) { rc->aborted = 1; return 1; }
      for (int kk = 0; kk < cmax; kk++) { ir->reg_map[fb+kk]=save_map[kk]; ir->reg_type[fb+kk]=save_type[kk]; }
      break;
    } else { rc->aborted = 1; return 1; }
  }
  if (nseg < 2 || final_ref < 0) { rc->aborted = 1; return 1; }

  /* fold selects inside-out: res = Afinal; res = select(c_i, A_i, res) for i = n..0 */
  int res = final_ref; SPTType res_t = final_t;
  for (int i = nseg - 1; i >= 0; i--) {
    int nr; SPTType nt;
    if (!emit_select(rc, s_fop[i], s_aref[i], s_bref[i], s_at[i], s_bt[i],
                     s_A[i], s_At[i], res, res_t, &nr, &nt)) return 1;   /* mixed types -> abort */
    res = nr; res_t = nt;
  }

  /* bind to caller, unwind (identical to the single cond-return tail) */
  ir->reg_map[rc->call_result_slot] = res;
  ir->reg_type[rc->call_result_slot] = res_t;
  if (rc->call_result_slot > ir->maxslot) ir->maxslot = rc->call_result_slot;
  rc->p = rc->save_p;
  rc->k = rc->save_k;
  rc->cl = rc->save_cl;
  rc->frame_base = rc->save_frame_base;
  rc->pc = rc->save_pc;
  return 1;
}

/* When recording an inlined VOID method whose body is an if-only conditional
   field write `if(c) this.f = A;`, if-convert it to ONE unconditional trailing
   write of a branchless select: this.f = select(c, A, old), where old is this.f's
   current value -- which the comparison reads, so a compare operand IS the
   GETFIELD of the written field. e.g. the running-max tracker
   `if(x > this.peak) this.peak = x;` becomes this.peak = max(this.peak, x); the
   in-place clamp `if(this.peak < 0) this.peak = 0;` becomes max(this.peak, 0).
   Composes the §10.30/§10.48 bit-exact select with §10.49's single-trailing-write
   safety: the SETFIELD is the last guard-emitting op, so resume-at-SELF (which is
   idempotent for a pure-read body up to the write) never fires after the write
   commits -- no double write, no new codegen, no entry-layout guard. Returns 1 if
   it took this path (caller returns 1 unless rc->aborted), 0 if not applicable. */
static int rec_try_condwrite_ifconv(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                                    SPTType at, SPTType bt) {
  if (rc->frame_base == 0) return 0;              /* only inside an inlined method */
  if (!sptt_isnum(at) || !sptt_isnum(bt)) return 0;
  SPTIRBuilder *ir = rc->ir;
  int fb = rc->frame_base;
  const Instruction *cmp_pc = rc->pc;
  const Instruction *jmp = cmp_pc + 1;
  if (GET_OPCODE(*jmp) != OP_JMP) return 0;
  const Instruction *T1 = jmp + 1 + GETARG_sJ(*jmp);    /* JMP target = merge */
  const Instruction *cend = rc->p->code + rc->p->sizecode;
  if (T1 <= cmp_pc + 2 || T1 > cend) return 0;
  const Instruction *sf_pc = T1 - 1;                    /* last then-arm op = the write */
  if (GET_OPCODE(*sf_pc) != OP_SETFIELD) return 0;

  int sf_a = GETARG_A(*sf_pc), sf_b = GETARG_B(*sf_pc);
  const TValue *kc = &rc->k[sf_b];
  if (ttypetag(kc) != LUA_VSHRSTR) return 0;
  TString *key = tsvalue(kc);

  /* old = this.f's current value: one of the compare operands must be a GETFIELD
     of exactly this key (the compare reads the field it conditionally updates).
     Otherwise abort cleanly -- we have no other source for the unchanged value. */
  /* old = this.f's current value. No write has committed yet (the conditional
     write is the only SETFIELD and is emitted last), so any read of this.f in the
     method yields the unchanged value. Two sources:
       (§10.51) a COMPARE operand is a GETFIELD of this exact key -- the compare
                reads the field it conditionally updates (`if(x>this.peak)...`); OR
       (§10.53) the THEN-ARM itself reads this.f, as in conditional accumulation
                `if(cond) this.sum = this.sum + x` (the compare is on x, but the
                compute `this.sum + x` loads this.sum). We pick that GETFIELD up
                after recording the then-arm, scoped to the ops it emitted and
                matched by key. */
  int old_ref = -1;
  if (aref >= 0 && ir->insts[aref].op == SPTIR_GETFIELD &&
      (TString *)(intptr_t)ir->insts[aref].aux == key)      old_ref = aref;
  else if (bref >= 0 && ir->insts[bref].op == SPTIR_GETFIELD &&
      (TString *)(intptr_t)ir->insts[bref].aux == key)      old_ref = bref;

  /* record the then-arm's compute of A (ops before the SETFIELD; usually none for
     `this.f = x`, a constant load for `this.f = 0`, or a field read + arithmetic
     for `this.f = this.f + x`). These run unconditionally after if-conversion, but
     the static gate restricted them to non-trapping ops (condreturn_method_op_ok). */
  int thenarm_ir0 = rc->ir->ninst;                /* watermark: ops the then-arm emits */
  rc->pc = cmp_pc + 2;
  while (rc->pc < sf_pc) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != sf_pc) { rc->aborted = 1; return 1; }

  int a_ref = rec_load_rkc(rc, *sf_pc);           /* A = the written value RK(C) */
  if (rc->aborted) return 1;
  SPTType a_t = sptir_type(ir, a_ref);

  if (old_ref < 0) {                              /* conditional RMW: old from then-arm read */
    for (int k = thenarm_ir0; k < rc->ir->ninst; k++)
      if (ir->insts[k].op == SPTIR_GETFIELD &&
          (TString *)(intptr_t)ir->insts[k].aux == key) { old_ref = k; break; }
  }
  if (old_ref < 0) {
    /* §10.55: old is read nowhere -- the condition is on a different value AND the
       then-arm doesn't read the written field: cross-field write `if(c) this.a =
       this.b + x`, conditional constant write `if(x>0) this.v = 5`, conditional
       field copy `if(c) this.a = this.b`. Emit a FRESH guarded read of the written
       field to supply the unchanged value. It is a read BEFORE the single SETFIELD,
       so its guard precedes the write -- single-trailing-write safety holds (no
       write has committed, so it reads the unchanged value). Replicates the
       OP_GETFIELD core (base resolve, key-exists walk, type predict, guarded emit). */
    int gbase = rec_load_reg(rc, sf_a);
    if (rc->aborted) return 1;
    if (ir->reg_type[fb + sf_a] != SPTT_TAB) { rc->aborted = 1; return 1; }
    TValue gmap;
    if (!rec_eval_container(rc, gbase, &gmap) || !ttistable(&gmap)) { rc->aborted = 1; return 1; }
    Table *gt = hvalue(&gmap);
    Node *gn = &gt->node[(unsigned int)key->hash & ((1u << gt->lsizenode) - 1u)];
    for (;;) {
      if (keytt(gn) == ctb(LUA_VSHRSTR) && (void *)keyval(gn).gc == (void *)key) break;
      int nx = (int)gn->u.next;
      if (nx == 0) { rc->aborted = 1; return 1; }   /* field absent -> abort (as the SETFIELD would) */
      gn += nx;
    }
    SPTType get = rec_value_type(&gn->i_val);
    if (get != SPTT_INT && get != SPTT_FLT) { rc->aborted = 1; return 1; }  /* numeric old for the select */
    int gref = sptir_emit(ir, SPTIR_GETFIELD, get, gbase, SPTIR_NULL, (int64_t)(intptr_t)key);
    int gsnap = rec_snap(rc);
    ir->insts[gref].snap_idx = gsnap;
    ir->insts[gref].flags |= SPTIRF_GUARD;
    old_ref = gref;
  }
  SPTType old_t = sptir_type(ir, old_ref);

  /* newval = select(fop, A, old): take A when the then-condition holds, else keep
     old (the unchanged field). Identical bit-exact select to the cond-return path. */
  int res; SPTType res_t;
  if (a_t == SPTT_INT && old_t == SPTT_INT && at == SPTT_INT && bt == SPTT_INT) {
    int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
    int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, a_ref, old_ref, 0);
    int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
    res = sptir_emit(ir, SPTIR_ADD, SPTT_INT, old_ref, prod, 0);
    res_t = SPTT_INT;
  } else if (a_t == SPTT_FLT && old_t == SPTT_FLT) {
    int is_flt_cmp = (at == SPTT_FLT || bt == SPTT_FLT);
    int mask;
    if (is_flt_cmp) {
      int ca = aref, cb = bref;
      if (at == SPTT_INT) ca = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, aref, -1, 0);
      if (bt == SPTT_INT) cb = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, bref, -1, 0);
      int ma = ca, mb = cb; SPTIROp mop = fop;
      if (fop == SPTIR_GT)      { mop = SPTIR_LT; ma = cb; mb = ca; }
      else if (fop == SPTIR_GE) { mop = SPTIR_LE; ma = cb; mb = ca; }
      mask = sptir_emit(ir, SPTIR_FCMPMASK, SPTT_FLT, ma, mb, (int64_t)mop);
    } else {
      mask = sptir_emit(ir, SPTIR_ICMPMASK, SPTT_FLT, aref, bref, (int64_t)fop);
    }
    res = sptir_emit(ir, SPTIR_FSELECT, SPTT_FLT, a_ref, old_ref, (int64_t)mask);
    res_t = SPTT_FLT;
  } else {
    rc->aborted = 1; return 1;                    /* mixed types: fall back */
  }

  /* emit ONE unconditional `this.f = newval` -- replicates the OP_SETFIELD handler
     core (base resolution, key-exists chain walk, snapshot, guarded emit) but with
     the selected value `res` in place of RK(C). */
  int base_ref = rec_load_reg(rc, sf_a);
  if (rc->aborted) return 1;
  if (ir->reg_type[fb + sf_a] != SPTT_TAB) { rc->aborted = 1; return 1; }
  if (res_t != SPTT_INT && res_t != SPTT_FLT) { rc->aborted = 1; return 1; }  /* no GC barrier */
  TValue mapval;
  if (!rec_eval_container(rc, base_ref, &mapval) || !ttistable(&mapval)) { rc->aborted = 1; return 1; }
  Table *t = hvalue(&mapval);
  Node *nd = &t->node[(unsigned int)key->hash & ((1u << t->lsizenode) - 1u)];
  for (;;) {
    if (keytt(nd) == ctb(LUA_VSHRSTR) && (void *)keyval(nd).gc == (void *)key) break;
    int nx = (int)nd->u.next;
    if (nx == 0) { rc->aborted = 1; return 1; }   /* key absent -> abort (would insert) */
    nd += nx;
  }
  int snap = rec_snap(rc);
  int sref = sptir_emit(ir, SPTIR_SETFIELD, SPTT_NIL, base_ref, res, (int64_t)(intptr_t)key);
  ir->insts[sref].snap_idx = snap;
  ir->insts[sref].flags |= SPTIRF_GUARD;

  rc->pc = T1;                                    /* continue at merge (RETURN0 -> void return) */
  return 1;
}

/* If-ELSE conditional field write: `if(c) this.g = A; else this.g = B;` (both arms
   write the SAME field) -> one unconditional trailing write this.g = select(c,A,B).
   Distinguished from the if-only path by T1-1 being the skip-else JMP. No `old` is
   needed -- A and B are explicit. Records each arm's compute against a forked frame
   (so the arms' temporaries don't interfere), then the identical bit-exact select +
   single guarded SETFIELD as the if-only path. Same single-trailing-write safety:
   the SETFIELD is the only write and is emitted last. */
static int rec_try_condwrite_ifelse_ifconv(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                                           SPTType at, SPTType bt) {
  if (rc->frame_base == 0) return 0;              /* only inside an inlined method */
  if (!sptt_isnum(at) || !sptt_isnum(bt)) return 0;
  SPTIRBuilder *ir = rc->ir;
  int fb = rc->frame_base;
  const Instruction *cmp_pc = rc->pc;
  const Instruction *jmp = cmp_pc + 1;
  if (GET_OPCODE(*jmp) != OP_JMP) return 0;
  const Instruction *else_start = jmp + 1 + GETARG_sJ(*jmp);    /* else-arm start */
  const Instruction *cend = rc->p->code + rc->p->sizecode;
  if (else_start <= cmp_pc + 2 || else_start >= cend) return 0;
  const Instruction *then_jmp = else_start - 1;                 /* then-arm skip-else JMP */
  if (GET_OPCODE(*then_jmp) != OP_JMP) return 0;
  const Instruction *then_sf = then_jmp - 1;                    /* then-arm SETFIELD */
  if (then_sf < cmp_pc + 2 || GET_OPCODE(*then_sf) != OP_SETFIELD) return 0;
  const Instruction *merge = then_jmp + 1 + GETARG_sJ(*then_jmp);  /* skip-else target = merge */
  if (merge <= else_start || merge > cend) return 0;
  const Instruction *else_sf = merge - 1;                      /* else-arm SETFIELD */
  if (else_sf < else_start || GET_OPCODE(*else_sf) != OP_SETFIELD) return 0;

  int sf_a = GETARG_A(*then_sf);
  const TValue *kc = &rc->k[GETARG_B(*then_sf)];
  if (ttypetag(kc) != LUA_VSHRSTR) return 0;
  TString *key = tsvalue(kc);
  const TValue *kc2 = &rc->k[GETARG_B(*else_sf)];               /* both write the same field */
  if (ttypetag(kc2) != LUA_VSHRSTR || tsvalue(kc2) != key) return 0;
  if (GETARG_A(*else_sf) != sf_a) return 0;                    /* ... on the same receiver */

  int cmax = rc->p->maxstacksize;
  if (cmax > 64) return 0;
  int save_map[64]; SPTType save_type[64];
  for (int k = 0; k < cmax; k++) { save_map[k] = ir->reg_map[fb+k]; save_type[k] = ir->reg_type[fb+k]; }

  /* ---- record then-arm's compute of A (forked frame), A = the written value RK(C) */
  rc->pc = cmp_pc + 2;
  while (rc->pc < then_sf) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != then_sf) { rc->aborted = 1; return 1; }
  int a_ref = rec_load_rkc(rc, *then_sf);
  if (rc->aborted) return 1;
  SPTType a_t = sptir_type(ir, a_ref);
  for (int k = 0; k < cmax; k++) { ir->reg_map[fb+k] = save_map[k]; ir->reg_type[fb+k] = save_type[k]; }

  /* ---- record else-arm's compute of B (forked frame), B = the written value RK(C) */
  rc->pc = else_start;
  while (rc->pc < else_sf) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != else_sf) { rc->aborted = 1; return 1; }
  int b_ref = rec_load_rkc(rc, *else_sf);
  if (rc->aborted) return 1;
  SPTType b_t = sptir_type(ir, b_ref);
  for (int k = 0; k < cmax; k++) { ir->reg_map[fb+k] = save_map[k]; ir->reg_type[fb+k] = save_type[k]; }

  /* newval = select(fop, A, B): A when the then-condition holds, else B. */
  int res; SPTType res_t;
  if (a_t == SPTT_INT && b_t == SPTT_INT && at == SPTT_INT && bt == SPTT_INT) {
    int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
    int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, a_ref, b_ref, 0);
    int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
    res = sptir_emit(ir, SPTIR_ADD, SPTT_INT, b_ref, prod, 0);
    res_t = SPTT_INT;
  } else if (a_t == SPTT_FLT && b_t == SPTT_FLT) {
    int is_flt_cmp = (at == SPTT_FLT || bt == SPTT_FLT);
    int mask;
    if (is_flt_cmp) {
      int ca = aref, cb = bref;
      if (at == SPTT_INT) ca = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, aref, -1, 0);
      if (bt == SPTT_INT) cb = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, bref, -1, 0);
      int ma = ca, mb = cb; SPTIROp mop = fop;
      if (fop == SPTIR_GT)      { mop = SPTIR_LT; ma = cb; mb = ca; }
      else if (fop == SPTIR_GE) { mop = SPTIR_LE; ma = cb; mb = ca; }
      mask = sptir_emit(ir, SPTIR_FCMPMASK, SPTT_FLT, ma, mb, (int64_t)mop);
    } else {
      mask = sptir_emit(ir, SPTIR_ICMPMASK, SPTT_FLT, aref, bref, (int64_t)fop);
    }
    res = sptir_emit(ir, SPTIR_FSELECT, SPTT_FLT, a_ref, b_ref, (int64_t)mask);
    res_t = SPTT_FLT;
  } else {
    rc->aborted = 1; return 1;                    /* mixed types: fall back */
  }

  /* emit ONE unconditional `this.f = newval` (replicates the OP_SETFIELD core) */
  int base_ref = rec_load_reg(rc, sf_a);
  if (rc->aborted) return 1;
  if (ir->reg_type[fb + sf_a] != SPTT_TAB) { rc->aborted = 1; return 1; }
  if (res_t != SPTT_INT && res_t != SPTT_FLT) { rc->aborted = 1; return 1; }
  TValue mapval;
  if (!rec_eval_container(rc, base_ref, &mapval) || !ttistable(&mapval)) { rc->aborted = 1; return 1; }
  Table *t = hvalue(&mapval);
  Node *nd = &t->node[(unsigned int)key->hash & ((1u << t->lsizenode) - 1u)];
  for (;;) {
    if (keytt(nd) == ctb(LUA_VSHRSTR) && (void *)keyval(nd).gc == (void *)key) break;
    int nx = (int)nd->u.next;
    if (nx == 0) { rc->aborted = 1; return 1; }
    nd += nx;
  }
  int snap = rec_snap(rc);
  int sref = sptir_emit(ir, SPTIR_SETFIELD, SPTT_NIL, base_ref, res, (int64_t)(intptr_t)key);
  ir->insts[sref].snap_idx = snap;
  ir->insts[sref].flags |= SPTIRF_GUARD;

  rc->pc = merge;                                 /* continue at merge (RETURN0 -> void return) */
  return 1;
}

/* Record a comparison + its trailing conditional JMP, following the branch the
   program actually takes at record time. 'fop' is the condition under which the
   bytecode falls through to the next instruction (the THEN block). We evaluate
   it on the operands' record-time values (from the IR, not the stack): if it
   holds we keep the fall-through and guard 'fop'; otherwise the JMP is taken, so
   we guard the negation and continue at the JMP target. If the values can't be
   known (an operand computed mid-trace), we keep the static fall-through, which
   is always correct (just possibly cold). Returns 1 to continue, 0 to abort. */
static int rec_cond_branch(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                           SPTType at, SPTType bt, int a_reg, int b_reg) {
  if (!sptt_isnum(at) || !sptt_isnum(bt)) { rc->aborted = 1; return 0; }
  /* Prefer branchless if-conversion for simple integer if-else / if-only: it
     keeps both arms in the trace with no side exit, which is a large win for
     ~50/50 branches that would otherwise exit every other iteration. Falls back
     to the guarded branch (below) when the shape isn't convertible. */
  if (rec_try_ifconv(rc, fop, aref, bref, at, bt)) return rc->aborted ? 0 : 1;
  if (rec_try_chained_condreturn_ifconv(rc, fop, aref, bref, at, bt)) return rc->aborted ? 0 : 1;
  if (rec_try_condreturn_ifconv(rc, fop, aref, bref, at, bt)) return rc->aborted ? 0 : 1;
  if (rec_try_condwrite_ifconv(rc, fop, aref, bref, at, bt)) return rc->aborted ? 0 : 1;
  if (rec_try_condwrite_ifelse_ifconv(rc, fop, aref, bref, at, bt)) return rc->aborted ? 0 : 1;
  /* A comparison inside an inlined callee that couldn't be if-converted would need
     a guarded branch, i.e. a mid-callee control-flow split with a snapshot that
     restores the synthetic callee frame -- which a single-entry trace cannot
     represent. Abort instead (the call simply isn't inlined; never incorrect). */
  if (rc->frame_base != 0) { rc->aborted = 1; return 0; }
  const Instruction *jmp = rc->pc + 1; /* the JMP following the comparison */
  double xv, yv;
  int fall_through = 1; /* default: static fall-through (correct, maybe cold) */
  if (rec_pred_num(rc, aref, a_reg, &xv) && rec_pred_num(rc, bref, b_reg, &yv))
    fall_through = rec_eval_num_cmp(fop, xv, yv);
  /* Override the single recording iteration's direction with the profiled
     majority for this branch, when we have samples. Which way the one recording
     iteration went is a coin-flip; the majority is what the loop does most of
     the time, so recording it keeps the trace's hot path on the common side and
     lets it stay internally looping instead of side-exiting every iteration.
     Correctness-safe: the IR is built symbolically via reg_map (not the live
     stack), and the emitted comparison is still a runtime guard, so a wrong
     majority only yields a suboptimal trace -- never an incorrect result.

     NOT for a side trace: a side trace is recorded precisely because the parent
     kept side-exiting at this branch, so it exists to cover the *minority*
     direction. The record-time frozen value here IS that minority direction (the
     parent just exited on it), which is exactly what the side trace must record.
     Applying the parent's majority profile would make the side trace record the
     same direction as the parent -- a useless trace that immediately re-exits on
     its own root guard every time it is entered. So follow the frozen value. */
  if (!rc->is_side_trace && g_prof.proto == rc->p && g_prof.n > 0) {
    int cmp_off = (int)(rc->pc - rc->p->code);
    for (int k = 0; k < g_prof.n; k++) {
      if (g_prof.br_pc[k] == cmp_off) {
        if (g_prof.ft[k] + g_prof.tk[k] > 0)
          fall_through = (g_prof.ft[k] >= g_prof.tk[k]);
        break;
      }
    }
  }
  if (fall_through) {
    rec_compare(rc, fop, aref, bref, at, bt);
    if (rc->aborted) return 0;
    rc->pc = jmp + 1;                   /* skip comparison + JMP */
  } else {
    rec_compare(rc, rec_negate_cmp(fop), aref, bref, at, bt);
    if (rc->aborted) return 0;
    rc->pc = jmp + 1 + GETARG_sJ(*jmp); /* follow the JMP */
  }
  return 1;
}

/* The record-time TString behind an IR ref: a KSTR constant, or the string in
   the operand's stack slot. NULL if not a string. */
static TString *rec_str_at(SPTRecCtx *rc, int ref, int reg) {
  SPTIRInst *in = sptir_get(rc->ir, ref);
  if (in && in->op == SPTIR_KSTR) return (TString *)(uintptr_t)in->aux;
  if (reg >= 0) {
    TValue *v = s2v((rc->ci->func.p + 1) + reg);
    if (ttisstring(v)) return tsvalue(v);
  }
  return NULL;
}

/* Like rec_cond_branch but for string EQ/NE (e.g. `if s == "literal"`). The
   guard is a pointer comparison, valid because the constant operand is a short
   interned string (the OP_EQK handler checks this). The taken branch is
   predicted by comparing the record-time string pointers. */
static int rec_str_cond_branch(SPTRecCtx *rc, int k, int aref, int bref,
                               int a_reg, int b_reg) {
  SPTIROp fop = k ? SPTIR_NE : SPTIR_EQ; /* fall-through condition */
  TString *sa = rec_str_at(rc, aref, a_reg);
  TString *sb = rec_str_at(rc, bref, b_reg);
  int fall_through = 1;
  if (sa && sb) {
    int eq = (sa == sb);
    fall_through = (fop == SPTIR_EQ) ? eq : !eq;
  }
  const Instruction *jmp = rc->pc + 1;
  SPTIROp gop = fall_through ? fop : rec_negate_cmp(fop);
  rec_compare(rc, gop, aref, bref, SPTT_STR, SPTT_STR);
  if (rc->aborted) return 0;
  if (fall_through) rc->pc = jmp + 1;
  else rc->pc = jmp + 1 + GETARG_sJ(*jmp);
  return 1;
}

/* Record an arithmetic operation. */
static int rec_arith(SPTRecCtx *rc, SPTIROp op, int a_ref, int b_ref, SPTType a_type, SPTType b_type) {
  SPTIRBuilder *ir = rc->ir;

  /* DIV ('/') and POW ('^') always produce a float in SPT/Lua, even for two
     integer operands (only '//' and '%' stay integer when both are int). Force
     both operands to float and type the result float, mirroring luaV_div /
     luaV_pow. Without this, `i / 2` was recorded as integer division followed
     by a widening, computing floor(i/2) and dropping the fractional part. */
  if (op == SPTIR_DIV || op == SPTIR_POW) {
    if (a_type == SPTT_INT) {
      a_ref = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, a_ref, SPTIR_NULL, 0);
      a_type = SPTT_FLT;
    }
    if (b_type == SPTT_INT) {
      b_ref = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, b_ref, SPTIR_NULL, 0);
      b_type = SPTT_FLT;
    }
    if (a_type == SPTT_FLT && b_type == SPTT_FLT)
      return sptir_emit(ir, op, SPTT_FLT, a_ref, b_ref, 0);
    rc->aborted = 1;
    return SPTIR_NULL;
  }

  /* MOD ('%') and IDIV ('~/') stay integer-only here. Float mod/idiv need a
     libm fmod/floor plus Lua's sign correction and are not handled in codegen
     (emitting them as a float op produced garbage), so abort and let the
     interpreter -- which is correct -- run them. Integer mod/idiv fall through
     to the both-int path below, where codegen applies the floored correction. */
  if ((op == SPTIR_MOD || op == SPTIR_IDIV) &&
      !(a_type == SPTT_INT && b_type == SPTT_INT)) {
    rc->aborted = 1;
    return SPTIR_NULL;
  }

  /* If both are int, do integer arithmetic. */
  if (a_type == SPTT_INT && b_type == SPTT_INT) {
    return sptir_emit(ir, op, SPTT_INT, a_ref, b_ref, 0);
  }

  /* If both are float, do float arithmetic. */
  if (a_type == SPTT_FLT && b_type == SPTT_FLT) {
    return sptir_emit(ir, op, SPTT_FLT, a_ref, b_ref, 0);
  }

  /* Mixed: promote int to float. */
  if (a_type == SPTT_INT && b_type == SPTT_FLT) {
    int a_flt = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, a_ref, SPTIR_NULL, 0);
    return sptir_emit(ir, op, SPTT_FLT, a_flt, b_ref, 0);
  }
  if (a_type == SPTT_FLT && b_type == SPTT_INT) {
    int b_flt = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, b_ref, SPTIR_NULL, 0);
    return sptir_emit(ir, op, SPTT_FLT, a_ref, b_flt, 0);
  }

  /* Unknown types: abort trace (too complex for now). */
  rc->aborted = 1;
  return SPTIR_NULL;
}

/* Record a comparison. */
static int rec_compare(SPTRecCtx *rc, SPTIROp op, int a_ref, int b_ref,
                       SPTType a_type, SPTType b_type) {
  SPTIRBuilder *ir = rc->ir;

  /* If both int or both float, direct comparison. */
  if (sptt_isnum(a_type) && sptt_isnum(b_type)) {
    /* If mixed, promote. */
    if (a_type == SPTT_INT && b_type == SPTT_FLT) {
      a_ref = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, a_ref, SPTIR_NULL, 0);
      a_type = SPTT_FLT;
    } else if (a_type == SPTT_FLT && b_type == SPTT_INT) {
      b_ref = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, b_ref, SPTIR_NULL, 0);
      b_type = SPTT_FLT;
    }
    int ref = sptir_emit(ir, op, SPTT_TRUE, a_ref, b_ref, 0);
    /* Comparison is a guard: if it fails, take side exit. */
    ir->insts[ref].flags |= SPTIRF_GUARD;
    int snap = rec_snap(rc);
    ir->insts[ref].snap_idx = snap;
    return ref;
  }

  /* String equality/inequality via pointer comparison. The caller guarantees a
     short interned constant operand, so pointer equality is exact: equal short
     strings share one interned object, and a long string can never equal a
     short constant. Only EQ/NE (ordering would need content comparison). */
  if (a_type == SPTT_STR && b_type == SPTT_STR &&
      (op == SPTIR_EQ || op == SPTIR_NE)) {
    int ref = sptir_emit(ir, op, SPTT_TRUE, a_ref, b_ref, 0);
    ir->insts[ref].flags |= SPTIRF_GUARD;
    int snap = rec_snap(rc);
    ir->insts[ref].snap_idx = snap;
    return ref;
  }

  /* Non-numeric comparison: abort. */
  rc->aborted = 1;
  return SPTIR_NULL;
}

/* Conditionally skip a trailing metamethod-fallback instruction.
   Binary arithmetic/bitwise ops are followed by OP_MMBIN/MMBINI/MMBINK in the
   bytecode (the slow-path metamethod call). Unary ops (UNM, BNOT, LEN) are NOT.
   Only advance pc if the *next* instruction really is an MMBIN-family op, so a
   unary op never accidentally swallows the following real instruction. */
static void rec_skip_mmbin(SPTRecCtx *rc) {
  OpCode next = GET_OPCODE(rc->pc[1]);
  if (next == OP_MMBIN || next == OP_MMBINI || next == OP_MMBINK)
    rc->pc++;
}

/* Emit IR for a shift by a COMPILE-TIME-KNOWN amount, matching luaV_shiftl:
   sh > 0  => logical left shift by sh   (0 if sh >= 64)
   sh < 0  => logical right shift by -sh  (0 if -sh >= 64)
   sh == 0 => identity.
   This is needed because Lua encodes 'x << k' as OP_SHRI with shift -k, so the
   effective direction can be the opposite of the opcode mnemonic. */
static int rec_shift_const(SPTRecCtx *rc, int val_ref, int64_t sh) {
  SPTIRBuilder *ir = rc->ir;
  if (sh >= 64 || sh <= -64)
    return sptir_kint(ir, 0);
  if (sh == 0)
    return val_ref;
  if (sh > 0)
    return sptir_emit(ir, SPTIR_SHL, SPTT_INT, val_ref, sptir_kint(ir, sh), 0);
  return sptir_emit(ir, SPTIR_SHR, SPTT_INT, val_ref, sptir_kint(ir, -sh), 0);
}

/* Record one bytecode instruction. Returns 1 to continue, 0 to stop. */
/* Decide whether `p` is a pure straight-line leaf function we can inline:
   fixed arity (no varargs), no nested protos (no closures), body consists only
   of register moves, constant loads, and scalar arithmetic/bitwise ops, and is
   terminated by exactly one RETURN1. No control flow (jumps/comparisons/loops),
   no calls, no table/list/upvalue/global access. Such a callee emits no guards
   when recorded (operand types are already known from the caller), so there are
   no mid-callee snapshots or exits to reconstruct, and its temporaries are dead
   after the return. On success returns 1 and writes the register holding the
   returned value to *ret_reg. */
static int proto_is_inlinable(Proto *p, int *ret_reg) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;          /* has nested protos => may close over */
  int n = p->sizecode;
  if (n < 1 || n > 48) return 0;        /* keep inlined bodies small */
  int saw_return = 0;
  for (int i = 0; i < n; i++) {
    Instruction ins = p->code[i];
    OpCode o = GET_OPCODE(ins);
    switch (o) {
      /* value-producing, side-effect-free, straight-line ops */
      case OP_MOVE:
      case OP_LOADI: case OP_LOADF: case OP_LOADK: case OP_LOADKX:
      case OP_LOADFALSE: case OP_LOADTRUE: case OP_LOADNIL:
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
      case OP_MOD: case OP_IDIV: case OP_POW:
      case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
      case OP_MODK: case OP_IDIVK: case OP_POWK:
      case OP_ADDI:
      case OP_BAND: case OP_BOR: case OP_BXOR:
      case OP_BANDK: case OP_BORK: case OP_BXORK:
      case OP_SHL: case OP_SHR: case OP_SHLI: case OP_SHRI:
      case OP_BNOT: case OP_UNM: case OP_NOT:
      case OP_MMBIN: case OP_MMBINI: case OP_MMBINK: /* metamethod markers; skipped */
      case OP_EXTRAARG:
      /* A trailing RETURN0/RETURN after the RETURN1 is dead compiler boilerplate
         and is never recorded (RETURN1 redirects recording back to the caller),
         so accepting it here is safe; it does not set a return register. */
      case OP_RETURN0: case OP_RETURN:
        break;
      case OP_RETURN1:
        if (saw_return) return 0;       /* only a single return point */
        *ret_reg = GETARG_A(ins);
        saw_return = 1;
        break;
      default:
        return 0;                       /* anything else: not inlinable */
    }
  }
  return saw_return;
}

/* Like proto_is_inlinable, but for an inlined *pure-read* method body:
   additionally permit straight-line field/element READS on `this` (and any
   params/locals) -- GETFIELD/GETI/GETTABLE and LEN. Writes (SETFIELD/SETI/
   SETTABLE) are deliberately excluded: a guard that fails after a committed
   write could not be safely re-executed (see rec_snap / resume-at-SELF). A read
   body has no committed side effects, so any in-method guard failure re-executes
   SELF+CALL+method in the interpreter idempotently. Still a single RETURN1, no
   branches/loops/calls, kept small. */
static int proto_is_method_inlinable(Proto *p, int *ret_reg) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 1 || n > 48) return 0;
  int saw_return = 0;
  for (int i = 0; i < n; i++) {
    Instruction ins = p->code[i];
    OpCode o = GET_OPCODE(ins);
    switch (o) {
      case OP_MOVE:
      case OP_LOADI: case OP_LOADF: case OP_LOADK: case OP_LOADKX:
      case OP_LOADFALSE: case OP_LOADTRUE: case OP_LOADNIL:
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
      case OP_MOD: case OP_IDIV: case OP_POW:
      case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
      case OP_MODK: case OP_IDIVK: case OP_POWK:
      case OP_ADDI:
      case OP_BAND: case OP_BOR: case OP_BXOR:
      case OP_BANDK: case OP_BORK: case OP_BXORK:
      case OP_SHL: case OP_SHR: case OP_SHLI: case OP_SHRI:
      case OP_BNOT: case OP_UNM: case OP_NOT:
      case OP_MMBIN: case OP_MMBINI: case OP_MMBINK:
      case OP_EXTRAARG:
      /* read-only field / element access on this (and locals) */
      case OP_GETFIELD: case OP_GETI: case OP_GETTABLE:
      case OP_LEN:
      case OP_RETURN0: case OP_RETURN:
        break;
      case OP_RETURN1:
        if (saw_return) return 0;
        *ret_reg = GETARG_A(ins);
        saw_return = 1;
        break;
      default:
        return 0;
    }
  }
  return saw_return;
}

static int op_is_comparison(OpCode o) {
  return o == OP_EQ || o == OP_LT || o == OP_LE || o == OP_EQK ||
         o == OP_EQI || o == OP_LTI || o == OP_LEI || o == OP_GTI || o == OP_GEI;
}

/* Decide whether `p` is an inlinable conditional-return leaf of the exact shape
   `if (c) { return A } return B`: a straight-line prefix that computes the
   compare operands, a single comparison + JMP, a then-arm that computes A and
   ends in RETURN1, an else-arm that computes B and ends in RETURN1, and only
   dead return boilerplate afterwards. Every non-control op (prefix and both
   arms) must be a non-trapping int- OR float-if-conversion op (opcode_is_
   ifconv_safe_any). The trapping integer `//` (OP_IDIV) is in neither set, so
   both arms stay trap-free; `/` (OP_DIV) is float-producing, so an arm using it
   simply becomes a float arm and rec_try_condreturn_ifconv selects bit-exactly.
   This forbids a second branch, a call, a loop, or any side effect. Such a
   callee is inlined and its conditional return if-converted into a branchless
   integer or float select (see rec_try_condreturn_ifconv). */
static int proto_is_condreturn_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 4 || n > 48) return 0;
  int ci = -1;
  for (int i = 0; i < n; i++)
    if (op_is_comparison(GET_OPCODE(p->code[i]))) { ci = i; break; }
  if (ci < 0) return 0;
  if (ci + 1 >= n || GET_OPCODE(p->code[ci+1]) != OP_JMP) return 0;
  int t1 = ci + 2 + GETARG_sJ(p->code[ci+1]);    /* JMP target = else-arm start */
  if (t1 <= ci + 2 || t1 >= n) return 0;
  if (GET_OPCODE(p->code[t1-1]) != OP_RETURN1) return 0;
  for (int i = 0; i < ci; i++) {                 /* prefix */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe_any(o)) return 0;
  }
  for (int i = ci + 2; i < t1 - 1; i++) {        /* then-arm compute */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe_any(o)) return 0;
  }
  int e = -1;
  for (int i = t1; i < n; i++) {                 /* else-arm: first RETURN1 */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_RETURN1) { e = i; break; }
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe_any(o)) return 0;
  }
  if (e < 0) return 0;
  for (int i = e + 1; i < n; i++) {              /* only dead boilerplate after */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o != OP_RETURN0 && o != OP_RETURN) return 0;
  }
  return 1;
}

/* Ops permitted in a conditional-return *method* body (prefix and both arms):
   the non-trapping integer if-conversion ops PLUS read-only field access on
   `this` (GETFIELD) and array length (LEN). Bounds-guarded element reads
   (GETI/GETTABLE) are deliberately EXCLUDED: if-conversion evaluates BOTH arms
   unconditionally, so a variable-index read in the not-taken arm would side-exit
   on every iteration whose index is out of that arm's intended range (a negative
   optimization), whereas GETFIELD/LEN guards on a pinned class are stable and
   never fire in steady state. Writes stay excluded (a committed side effect
   breaks resume-at-SELF). */
static int condreturn_method_op_ok(OpCode o) {
  if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) return 1;
  if (opcode_is_ifconv_safe_any(o)) return 1;   /* int- OR float-safe (float const loads, float DIV) */
  return o == OP_GETFIELD || o == OP_LEN;
}

/* Like proto_is_condreturn_inlinable, but for an inlined pure-read METHOD body:
   the `if (c) { return A } return B` shape where the prefix and arms may also
   read `this` fields (GETFIELD/LEN), e.g. `int clamp(){ if(this.v<0) return 0;
   return this.v; }`. The branch if-converts to a branchless select (no in-callee
   guarded branch -- see rec_try_condreturn_ifconv), and field-read guards inside
   resume at the caller's SELF (idempotent, since the body has no writes). Return
   types are validated at record time (integer select must be bit-exact); a float
   field/return cleanly aborts there. */
static int proto_is_condreturn_method_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 4 || n > 48) return 0;
  int ci = -1;
  for (int i = 0; i < n; i++)
    if (op_is_comparison(GET_OPCODE(p->code[i]))) { ci = i; break; }
  if (ci < 0) return 0;
  if (ci + 1 >= n || GET_OPCODE(p->code[ci+1]) != OP_JMP) return 0;
  int t1 = ci + 2 + GETARG_sJ(p->code[ci+1]);    /* JMP target = else-arm start */
  if (t1 <= ci + 2 || t1 >= n) return 0;
  if (GET_OPCODE(p->code[t1-1]) != OP_RETURN1) return 0;
  for (int i = 0; i < ci; i++)                    /* prefix */
    if (!condreturn_method_op_ok(GET_OPCODE(p->code[i]))) return 0;
  for (int i = ci + 2; i < t1 - 1; i++)           /* then-arm compute */
    if (!condreturn_method_op_ok(GET_OPCODE(p->code[i]))) return 0;
  int e = -1;
  for (int i = t1; i < n; i++) {                  /* else-arm: first RETURN1 */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_RETURN1) { e = i; break; }
    if (!condreturn_method_op_ok(o)) return 0;
  }
  if (e < 0) return 0;
  for (int i = e + 1; i < n; i++) {               /* only dead boilerplate after */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o != OP_RETURN0 && o != OP_RETURN) return 0;
  }
  return 1;
}

static int chain_op_ok(OpCode o, int allow_fields) {
  if (allow_fields) return condreturn_method_op_ok(o);
  return o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK || opcode_is_ifconv_safe_any(o);
}

/* Gate for a CHAIN of conditional returns (>=2) ending in a final return -- the
   double-sided clamp `if(x<lo) return lo; if(x>hi) return hi; return x`. Walks the
   linear segment sequence: each cond-return segment is [compute operands] CMP JMP
   [compute A] RETURN1 with the JMP target = the next segment; the chain ends at a
   segment that is just [compute] RETURN1 (the final return). `allow_fields` picks
   the op set (methods may read this-fields; free functions may not). Returns 1 only
   for >=2 cond-returns -- a single one is left to the single cond-return gate.
   Inlined, rec_try_chained_condreturn_ifconv folds it to a nested branchless select
   (no branch, no side exit). The walk MIRRORS the recorder so a gate pass implies a
   recordable shape (and any mismatch just aborts cleanly). */
static int proto_is_chained_condreturn(Proto *p, int allow_fields) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 6 || n > 64) return 0;
  int pc = 0, nseg = 0;
  for (;;) {
    int ci = -1, ri = -1;
    for (int i = pc; i < n; i++) {                 /* compute prefix, then CMP or RETURN */
      OpCode o = GET_OPCODE(p->code[i]);
      if (op_is_comparison(o)) { ci = i; break; }
      if (o == OP_RETURN1 || o == OP_RETURN0 || o == OP_RETURN) { ri = i; break; }
      if (!chain_op_ok(o, allow_fields)) return 0;
    }
    if (ci >= 0) {
      if (ci + 1 >= n || GET_OPCODE(p->code[ci+1]) != OP_JMP) return 0;
      int t = ci + 2 + GETARG_sJ(p->code[ci+1]);   /* JMP target = next segment */
      if (t <= ci + 2 || t > n) return 0;
      if (GET_OPCODE(p->code[t-1]) != OP_RETURN1) return 0;
      for (int i = ci + 2; i < t - 1; i++)         /* then-arm compute A */
        if (!chain_op_ok(GET_OPCODE(p->code[i]), allow_fields)) return 0;
      nseg++;
      pc = t;
    } else if (ri >= 0) {
      if (GET_OPCODE(p->code[ri]) != OP_RETURN1) return 0;   /* final must be a value return */
      for (int i = ri + 1; i < n; i++) {           /* only dead boilerplate after */
        OpCode o = GET_OPCODE(p->code[i]);
        if (o != OP_RETURN0 && o != OP_RETURN && o != OP_EXTRAARG) return 0;
      }
      break;
    } else return 0;
  }
  return nseg >= 2;                                 /* >=2 cond-returns = a chain */
}

/* Ops permitted BEFORE the write in a single-trailing-write method body: the
   pure-read method set (reads on `this`/params + non-control compute), i.e. the
   same set proto_is_method_inlinable accepts minus the returns and the write
   itself. A read's guard (field key/type, element bounds) sits before the write,
   so a failure side-exits before the write commits -- safe with resume-at-SELF. */
static int method_read_op_ok(OpCode o) {
  switch (o) {
    case OP_MOVE:
    case OP_LOADI: case OP_LOADF: case OP_LOADK: case OP_LOADKX:
    case OP_LOADFALSE: case OP_LOADTRUE: case OP_LOADNIL:
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_IDIV: case OP_POW:
    case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
    case OP_MODK: case OP_IDIVK: case OP_POWK:
    case OP_ADDI:
    case OP_BAND: case OP_BOR: case OP_BXOR:
    case OP_BANDK: case OP_BORK: case OP_BXORK:
    case OP_SHL: case OP_SHR: case OP_SHLI: case OP_SHRI:
    case OP_BNOT: case OP_UNM: case OP_NOT:
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK:
    case OP_EXTRAARG:
    case OP_GETFIELD: case OP_GETI: case OP_GETTABLE:
    case OP_LEN:
      return 1;
    default:
      return 0;
  }
}

/* Decide whether `p` is an inlinable single-trailing-write VOID method: a
   straight-line body that reads this-fields/params and computes a value, then
   performs exactly ONE field write (SETFIELD) as its LAST guard-emitting op,
   then returns void (RETURN0/RETURN). e.g. `void set(int x){ this.v = x; }` or
   the accumulator `void add(int x){ this.total = this.total + x; }`.

   This is the WRITE-method subset that is safe with resume-at-SELF WITHOUT the
   entry-layout-guard + no-guard-field-access codegen the general case needs
   (roadmap §3c.1 #1). Safety: the write is the last committing op, and every
   in-body guard (field-read key/type guards, the SETFIELD's own key guard which
   the codegen evaluates BEFORE the store) precedes it. So any guard failure
   side-exits BEFORE the write commits, and re-executing SELF+CALL+method in the
   interpreter writes exactly once -- no double write. A guard AFTER a committed
   write (e.g. a second field access, as in `this.a=..; this.b=..`) would break
   this, hence: at most one write, positioned last (only void-return boilerplate
   after it). The written value must be int/float (the SETFIELD handler enforces
   this at record time: non-collectable => no GC write barrier). The receiver is
   loop-invariant (SLOAD) and its class/method/field layout is pinned by the
   once-per-entry method guard, so the SETFIELD key guard holds in steady state. */
static int proto_is_write_method_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 1 || n > 48) return 0;
  int wi = -1;                          /* index of the single SETFIELD */
  for (int i = 0; i < n; i++) {
    if (GET_OPCODE(p->code[i]) == OP_SETFIELD) {
      if (wi >= 0) return 0;            /* more than one write -> not this subset */
      wi = i;
    }
  }
  if (wi < 0) return 0;                 /* no write -> not a write method */
  for (int i = 0; i < wi; i++)          /* reads/compute before the write */
    if (!method_read_op_ok(GET_OPCODE(p->code[i]))) return 0;
  /* After the write, two safe tails are allowed:
       (a) void return: only RETURN0/RETURN/EXTRAARG; OR
       (b) return the JUST-WRITTEN field: GETFIELD of the SAME key (store-to-load
           forwarded => guard-free) + MOVE + RETURN1. e.g. the increment/accumulate
           -and-return-new-value pattern `int inc(){ this.v=this.v+1; return this.v; }`.
     Any OTHER op after the write (a different-field read, an array index, more
     arithmetic) would emit a guard after the write and is rejected. */
  const TValue *wkc = &p->k[GETARG_B(p->code[wi])];          /* the written field's key */
  TString *wkey = (ttypetag(wkc) == LUA_VSHRSTR) ? tsvalue(wkc) : NULL;
  for (int i = wi + 1; i < n; i++) {
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_RETURN0 || o == OP_RETURN || o == OP_EXTRAARG ||
        o == OP_RETURN1 || o == OP_MOVE) continue;
    if (o == OP_GETFIELD) {            /* must re-read the written field (forwardable) */
      const TValue *gkc = &p->k[GETARG_C(p->code[i])];
      if (!wkey || ttypetag(gkc) != LUA_VSHRSTR || tsvalue(gkc) != wkey) return 0;
      continue;
    }
    return 0;
  }
  return 1;
}

/* Decide whether `p` is an inlinable MULTI-write void method: a straight-line
   body with TWO OR MORE SETFIELDs (e.g. `void update(int x){ this.a=x; this.b=x+1; }`
   or `void swap(){ int t=this.a; this.a=this.b; this.b=t; }`). The §10.49 single-
   trailing-write safety does NOT hold (the 2nd SETFIELD's key-existence guard
   would fire after the 1st write commits -> resume-at-SELF double-writes), so
   instead we hoist ALL field-access guards to the trace entry: each accessed
   this.<field> is verified once per entry (key present + value type matches),
   and the body emits guard-free GETFIELD/SETFIELD. Zero in-body field guards
   -> no in-callee exits from field access -> write-safe regardless of write
   count. Body shape: straight-line (no branches), only method_read_op_ok ops
   + SETFIELD + void return. Value-returning multi-write is NOT supported
   (would need exit-safe read-after-write of the returned field). */
static int proto_is_multiwrite_method_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 2 || n > 64) return 0;
  int n_writes = 0;
  for (int i = 0; i < n; i++) {
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_SETFIELD) n_writes++;
    if (o == OP_RETURN1) return 0;            /* value return not supported */
  }
  if (n_writes < 2) return 0;                 /* single write -> use §10.49 path */
  /* straight-line body: only reads + SETFIELD + void return */
  for (int i = 0; i < n; i++) {
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_SETFIELD) continue;
    if (o == OP_RETURN0 || o == OP_RETURN || o == OP_EXTRAARG) continue;
    if (method_read_op_ok(o)) continue;
    return 0;
  }
  return 1;
}

/* Decide whether `p` is an inlinable conditional-WRITE void method of the if-only
   shape `if (c) { <compute A> this.f = A } ` with nothing after the if but void-
   return boilerplate -- e.g. the ubiquitous running-max/min tracker
   `void track(int x){ if(x > this.peak) this.peak = x; }` or the in-place clamp
   `void clampLow(){ if(this.peak < 0) this.peak = 0; }`. The conditional write is
   if-converted (rec_try_condwrite_ifconv) to ONE unconditional trailing write of
   a branchless select: `this.f = select(c, A, old)` where old is this.f's current
   value. This composes the §10.30/§10.48 select with the §10.49 single-trailing-
   write safety: the single SETFIELD is the last guard-emitting op, every guard
   (the field-read guards, the SETFIELD's own key guard) precedes it, so a guard
   failure side-exits BEFORE the write commits and the interpreter re-runs SELF+
   CALL+method writing exactly once -- no double write, NO new codegen, NO entry-
   layout guard. Shape only: exactly one SETFIELD positioned as the last then-arm
   op (if-only, no else, no trailing JMP), a comparison + forward JMP whose target
   is the merge, a non-trapping then-arm compute (evaluated unconditionally after
   if-conversion), and void return. The recorder additionally requires that the
   comparison reads the written field (so `old` is a compare operand); if not, it
   aborts cleanly. */
static int proto_is_condwrite_method_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 4 || n > 48) return 0;
  int wi = -1;                          /* the single SETFIELD */
  for (int i = 0; i < n; i++) {
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_SETFIELD) { if (wi >= 0) return 0; wi = i; }
    if (o == OP_RETURN1) return 0;      /* value return -> not a void conditional-write */
  }
  if (wi < 0) return 0;
  int ci = -1;                          /* the comparison */
  for (int i = 0; i < n; i++)
    if (op_is_comparison(GET_OPCODE(p->code[i]))) { ci = i; break; }
  if (ci < 0) return 0;
  if (ci + 1 >= n || GET_OPCODE(p->code[ci+1]) != OP_JMP) return 0;
  int t1 = ci + 2 + GETARG_sJ(p->code[ci+1]);   /* JMP target = merge */
  if (t1 <= ci + 2 || t1 > n) return 0;
  if (wi != t1 - 1) return 0;           /* single write is the LAST then-arm op (if-only) */
  for (int i = 0; i < ci; i++)          /* prefix: reads + compute compare operands */
    if (!method_read_op_ok(GET_OPCODE(p->code[i]))) return 0;
  for (int i = ci + 2; i < wi; i++)     /* then-arm compute A: non-trapping (eval'd unconditionally) */
    if (!condreturn_method_op_ok(GET_OPCODE(p->code[i]))) return 0;
  for (int i = t1; i < n; i++) {        /* after merge: void-return boilerplate only */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o != OP_RETURN0 && o != OP_RETURN && o != OP_EXTRAARG) return 0;
  }
  return 1;
}

/* If-ELSE conditional field write: `void f(...){ if(c) this.g = A; else this.g = B; }`
   -- both arms write the SAME field, so it if-converts to one unconditional trailing
   write this.g = select(c, A, B). Bytecode shape:
       <prefix>; CMP; JMP->else; <thenA>; SETFIELD this.g=A; JMP->merge;
       else: <elseB>; SETFIELD this.g=B; merge: RETURN0.
   Distinguished from the if-only gate by T1-1 being the skip-else JMP (not the
   SETFIELD). No `old` is needed (both A and B are explicit). Composes the §10.30
   select with §10.49's single-trailing-write safety (one SETFIELD, emitted last). */
static int proto_is_condwrite_ifelse_method_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 5 || n > 48) return 0;
  int sf1 = -1, sf2 = -1;               /* exactly two SETFIELDs, no value return */
  for (int i = 0; i < n; i++) {
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_SETFIELD) { if (sf1 < 0) sf1 = i; else if (sf2 < 0) sf2 = i; else return 0; }
    if (o == OP_RETURN1) return 0;
  }
  if (sf2 < 0) return 0;
  int ci = -1;                          /* the comparison */
  for (int i = 0; i < n; i++)
    if (op_is_comparison(GET_OPCODE(p->code[i]))) { ci = i; break; }
  if (ci < 0) return 0;
  if (ci + 1 >= n || GET_OPCODE(p->code[ci+1]) != OP_JMP) return 0;
  int else_start = ci + 2 + GETARG_sJ(p->code[ci+1]);   /* JMP target = else-arm start */
  if (else_start <= ci + 2 || else_start >= n) return 0;
  int then_jmp = else_start - 1;                         /* then-arm's skip-else JMP */
  if (then_jmp < ci + 2 || GET_OPCODE(p->code[then_jmp]) != OP_JMP) return 0;
  int then_sf = then_jmp - 1;                            /* then-arm SETFIELD (= sf1) */
  if (then_sf < ci + 2 || then_sf != sf1) return 0;
  int merge = then_jmp + 1 + GETARG_sJ(p->code[then_jmp]);  /* skip-else target = merge */
  if (merge <= else_start || merge > n) return 0;
  int else_sf = merge - 1;                              /* else-arm SETFIELD (= sf2) */
  if (else_sf < else_start || else_sf != sf2) return 0;
  const TValue *k1 = &p->k[GETARG_B(p->code[then_sf])];  /* both write the SAME field */
  const TValue *k2 = &p->k[GETARG_B(p->code[else_sf])];
  if (ttypetag(k1) != LUA_VSHRSTR || ttypetag(k2) != LUA_VSHRSTR) return 0;
  if (tsvalue(k1) != tsvalue(k2)) return 0;
  for (int i = 0; i < ci; i++)          /* prefix: reads + compute compare operands */
    if (!method_read_op_ok(GET_OPCODE(p->code[i]))) return 0;
  for (int i = ci + 2; i < then_sf; i++)  /* then-arm compute A: non-trapping */
    if (!condreturn_method_op_ok(GET_OPCODE(p->code[i]))) return 0;
  for (int i = else_start; i < else_sf; i++)  /* else-arm compute B: non-trapping */
    if (!condreturn_method_op_ok(GET_OPCODE(p->code[i]))) return 0;
  for (int i = merge; i < n; i++) {     /* after merge: void-return boilerplate only */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o != OP_RETURN0 && o != OP_RETURN && o != OP_EXTRAARG) return 0;
  }
  return 1;
}

/* Decide whether `p` is an inlinable conditional-assignment-then-return leaf of
   the shape `if (c) { <assigns> } <straight-line> return slot` (if-only): a
   straight-line prefix that computes the compare operands, a single comparison +
   forward JMP, a then-arm of straight-line integer assignments (NO return), then
   post-merge straight-line code ending in exactly one RETURN1, then only dead
   boilerplate. Every non-control op must be a non-trapping integer if-conversion
   op; opcode_is_ifconv_safe excludes RETURN1/JMP/comparisons, so the arm and
   post-merge scans reject a second branch, a nested return, a call, or a loop.
   When inlined, the conditional assignment if-converts (rec_try_ifconv now fires
   in callee frames) to slot = select(c, A, old), and the trailing RETURN1 binds
   that to the caller's result -- branchless, no side exit. */
static int proto_is_condassign_inlinable(Proto *p) {
  if (isvararg(p)) return 0;
  if (p->sizep != 0) return 0;
  int n = p->sizecode;
  if (n < 4 || n > 48) return 0;
  int ci = -1;
  for (int i = 0; i < n; i++)
    if (op_is_comparison(GET_OPCODE(p->code[i]))) { ci = i; break; }
  if (ci < 0) return 0;
  if (ci + 1 >= n || GET_OPCODE(p->code[ci+1]) != OP_JMP) return 0;
  int t1 = ci + 2 + GETARG_sJ(p->code[ci+1]);    /* JMP target = merge (if-only) */
  if (t1 <= ci + 2 || t1 >= n) return 0;         /* forward, leaving room for the return */
  for (int i = 0; i < ci; i++) {                 /* prefix */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe_any(o)) return 0;
  }
  for (int i = ci + 2; i < t1; i++) {            /* then-arm: assignments only, no return */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe_any(o)) return 0;
  }
  int ret_idx = -1;
  for (int i = t1; i < n; i++) {                 /* post-merge: compute then RETURN1 */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_RETURN1) { ret_idx = i; break; }
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe_any(o)) return 0;
  }
  if (ret_idx < 0) return 0;
  for (int i = ret_idx + 1; i < n; i++) {        /* only dead boilerplate after */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o != OP_RETURN0 && o != OP_RETURN) return 0;
  }
  return 1;
}


/* ===================================================================
   Inner-loop unrolling (Phase 3a / nested-loop optimization)
   ===================================================================
   A nested loop where only the inner loop is hot gets traced as the inner
   loop alone; per outer iteration the interpreter drives the outer FORLOOP and
   re-enters the inner trace, paying an exit (state flush) + re-entry (reload)
   each time. For a SHORT inner loop that overhead is not amortized -- measured
   as a *negative* optimization (inner trip=4 ran at 0.98x, slower than the
   interpreter). When the outer trace records *through* an inner numeric for
   loop, we instead unroll the inner loop inline so the whole nest is one
   linear trace with no per-outer-iteration hand-off. This is correctness-
   tractable only under tight conditions (constant trip count, straight-line
   body); otherwise we fall back to the existing behaviour (skip + abort the
   outer trace, keep the inner loop's own trace), which is always safe. */

/* Ops that make an inner-loop body un-unrollable: any control flow (we are
   linearising the body, so a branch/skip would be miscompiled), any nested
   loop, and any call/return/closure/vararg (frame effects). Everything else is
   left to rec_inst, which aborts the whole trace if it cannot handle an op --
   also safe. */
static int unroll_unsafe_op(OpCode op) {
  switch (op) {
    /* branches + conditional skips */
    case OP_JMP:
    case OP_EQ: case OP_LT: case OP_LE:
    case OP_EQK: case OP_EQI: case OP_LTI: case OP_LEI:
    case OP_GTI: case OP_GEI:
    case OP_TEST: case OP_TESTSET:
    /* generic-for loops (iterator protocol) -- not handled by the recorder */
    case OP_TFORPREP: case OP_TFORCALL: case OP_TFORLOOP:
    /* calls / returns / frame effects */
    case OP_CALL: case OP_TAILCALL:
    case OP_RETURN: case OP_RETURN0: case OP_RETURN1:
    case OP_CLOSURE: case OP_CLOSE: case OP_TBC:
    case OP_VARARG: case OP_VARARGPREP:
      return 1;
    /* NOTE: OP_FORPREP / OP_FORLOOP (a *nested numeric for*) are allowed in the
       body: the replay reaches the inner FORPREP via rec_inst and recursively
       unrolls it, so an all-constant nested loop (e.g. a fixed-size matrix
       multiply) collapses fully into one linear trace. If an inner loop turns
       out NOT to be unrollable (e.g. variable bounds), the recursive attempt
       falls back and the inner FORLOOP back-edge aborts the whole trace -- the
       same safe fall-back as before, just discovered one level deeper. */
    default:
      return 0;
  }
}

/* Whether opcode `op` writes its A field as a fresh destination register (so an
   A that equals a loop control slot means the loop variable is reassigned). The
   exceptions take A as an operand (MMBIN family) or write through R[A] without
   reassigning it (table/upvalue stores). Everything else here writes R[A]. */
static int unroll_writes_dest_A(OpCode op) {
  switch (op) {
    case OP_MMBIN: case OP_MMBINI: case OP_MMBINK:
    case OP_SETI: case OP_SETTABLE: case OP_SETFIELD:
    case OP_SETTABUP: case OP_SETUPVAL:
      return 0;
    default:
      return 1;
  }
}

/* Evaluate an IR ref to an integer the recorder can observe right now, reading
   the LIVE value of any SLOAD leaf. Succeeds only for a constant or a pure
   integer expression over loop-INVARIANT stack loads -- a leaf must be a KINT or
   an SLOAD whose slot still maps to that same load (i.e. not reassigned, and in
   particular not a PHI / loop induction variable). This drives *speculative*
   inner-loop unrolling: the value is only a guess that we PIN with a run-time
   guard, so a wrong guess stays correct (the guard side-exits). Excluding PHIs
   and recomputed values keeps the guard from failing on every iteration -- e.g.
   a triangular `for j = 0, i` (limit = the outer loop var, a PHI) is rejected
   here, so we never speculate it into a negative optimization. */
static int eval_invariant_int(SPTRecCtx *rc, int ref, lua_Integer *out) {
  SPTIRBuilder *ir = rc->ir;
  if (ref < 0 || ref >= ir->ninst) return 0;
  SPTIRInst *in = &ir->insts[ref];
  switch (in->op) {
    case SPTIR_KINT:
      *out = (lua_Integer)in->aux;
      return 1;
    case SPTIR_SLOAD: {
      int slot = (int)in->aux;
      if (slot < 0 || slot >= 256) return 0;
      /* Invariant only if the slot has not been reassigned since this load. */
      if (ir->reg_map[slot] != ref) return 0;
      TValue *v = s2v((rc->ci->func.p + 1) + slot);
      if (!ttisinteger(v)) return 0;
      *out = ivalue(v);
      return 1;
    }
    case SPTIR_ADD: case SPTIR_SUB: case SPTIR_MUL: {
      lua_Integer x, y;
      if (!eval_invariant_int(rc, in->op1, &x)) return 0;
      if (!eval_invariant_int(rc, in->op2, &y)) return 0;
      *out = (in->op == SPTIR_ADD) ? x + y
           : (in->op == SPTIR_SUB) ? x - y
                                   : x * y;
      return 1;
    }
    default:
      return 0;
  }
}

/* Pin IR value `ref` to the constant `val` with a run-time guard (two GUARD_LE:
   ref <= val and val <= ref). If either fails the trace side-exits at the
   snapshot (taken by the caller at the inner FORPREP), so the interpreter
   re-runs the loop with the real bound. GUARD_EQ has no codegen; two GUARD_LE
   reuse the existing path. GUARD_LE(op1, aux) means R[op1] <= R[aux]. */
static void emit_pin_guard(SPTIRBuilder *ir, int ref, lua_Integer val, int snap) {
  int kv = sptir_kint(ir, val);
  int g1 = sptir_emit(ir, SPTIR_GUARD_LE, SPTT_NIL, ref, SPTIR_NULL, kv);
  if (g1 != SPTIR_NULL) { ir->insts[g1].flags |= SPTIRF_GUARD; ir->insts[g1].snap_idx = snap; }
  int g2 = sptir_emit(ir, SPTIR_GUARD_LE, SPTT_NIL, kv, SPTIR_NULL, ref);
  if (g2 != SPTIR_NULL) { ir->insts[g2].flags |= SPTIRF_GUARD; ir->insts[g2].snap_idx = snap; }
}

/* Try to unroll the inner numeric for-loop whose FORPREP is `i` (at rc->pc)
   inline into the current trace. Returns 1 if unrolled (rc->pc has been
   advanced past the inner FORLOOP; the caller should continue recording the
   outer body), 0 if not unrollable (caller falls back to skip+abort). On a
   replay abort the whole trace is aborted (rc->aborted set) and 1 is returned
   so the caller stops; the trace is then discarded -- safe. */
static int try_unroll_inner_loop(SPTRecCtx *rc, Instruction i) {
  SPTIRBuilder *ir = rc->ir;
  SPTJitState *js = rc->js;
  if (js->unroll_max <= 0) return 0;

  int a = GETARG_A(i);
  int bx = GETARG_Bx(i);
  int base = rc->frame_base;

  /* Pre-FORPREP register layout: R[A]=init, R[A+1]=limit, R[A+2]=step.
     A bound that is already an integer KINT needs no guard. A bound that is a
     loop-INVARIANT integer expression (SLOAD leaves) is read now and PINNED with
     a run-time guard, so the speculative trip count is safe (a mismatch
     side-exits). Anything else (a PHI/loop-variable bound, a non-integer, a
     value we cannot evaluate) makes the trip count unknowable -- bail. */
  int init_ref  = ir->reg_map[base + a];
  int limit_ref = ir->reg_map[base + a + 1];
  int step_ref  = ir->reg_map[base + a + 2];
  if (init_ref < 0 || limit_ref < 0 || step_ref < 0) return 0;
  if (init_ref >= ir->ninst || limit_ref >= ir->ninst || step_ref >= ir->ninst)
    return 0;
  int init_k  = (ir->insts[init_ref].op  == SPTIR_KINT);
  int limit_k = (ir->insts[limit_ref].op == SPTIR_KINT);
  int step_k  = (ir->insts[step_ref].op  == SPTIR_KINT);
  lua_Integer init, limit, step;
  if (init_k)  init  = (lua_Integer)ir->insts[init_ref].aux;
  else if (!eval_invariant_int(rc, init_ref, &init))   return 0;
  if (limit_k) limit = (lua_Integer)ir->insts[limit_ref].aux;
  else if (!eval_invariant_int(rc, limit_ref, &limit)) return 0;
  if (step_k)  step  = (lua_Integer)ir->insts[step_ref].aux;
  else if (!eval_invariant_int(rc, step_ref, &step))   return 0;
  if (step == 0) return 0;

  /* Trip count, computed exactly as the VM's forprep() does for an integer
     loop. The body runs (count + 1) times with idx = init + k*step, and
     R[A] (the FORLOOP counter) holds (count - k) during body iteration k. */
  lua_Unsigned count;
  if (step > 0) {
    if (init > limit) return 0;          /* empty loop: rare, just bail */
    count = (lua_Unsigned)limit - (lua_Unsigned)init;
    if (step != 1) count /= (lua_Unsigned)step;
  } else {
    if (init < limit) return 0;          /* empty loop */
    count = (lua_Unsigned)init - (lua_Unsigned)limit;
    count /= (lua_Unsigned)(-(step + 1)) + 1u;
  }
  if (count + 1 > (lua_Unsigned)js->unroll_max) return 0;   /* too long */
  int trips = (int)(count + 1);

  /* Inner body is [body_pc, forloop_pc); FORLOOP closes it. */
  const Instruction *body_pc    = rc->pc + 1;
  const Instruction *forloop_pc = rc->pc + bx + 1;
  if (forloop_pc <= body_pc) return 0;
  if (GET_OPCODE(*forloop_pc) != OP_FORLOOP) return 0;
  if (GETARG_A(*forloop_pc) != a) return 0;
  /* FORLOOP must jump back to body_pc (its target = (forloop_pc+1) - Bx). */
  if ((forloop_pc + 1) - GETARG_Bx(*forloop_pc) != body_pc) return 0;

  /* Body must be straight-line and must not overwrite the loop control slots
     (A=counter, A+1=step, A+2=idx); we pin those to constants per copy. */
  /* Body must be straight-line and must not REASSIGN the loop control slots
     (A=counter, A+1=step, A+2=idx) -- those are pinned to constants per copy, so
     a body that overwrote the loop variable would be miscompiled (in SPT the
     body reads the control var R[A+2] directly, no private copy). The check only
     applies to ops whose A is a destination: MMBIN/MMBINI/MMBINK take A as an
     OPERAND (a metamethod-fallback hint, skipped on the numeric fast path), and
     SETI/SETTABLE/SETFIELD/SETTABUP/SETUPVAL write *through* R[A] (a table or
     upvalue) without reassigning R[A]; none clobber the loop variable. Reading
     R[A+2] as an operand (the normal use of the loop index) is fine.

     We also refuse to unroll a body that both READS and WRITES the same array
     (a read-modify-write like `a[idx] = a[idx] + ...`). Fully unrolling such a
     loop produces many element loads/stores to indices that fold to constants;
     when the same element is touched repeatedly through *colliding* computed
     indices (e.g. a histogram `a[i+j]++`), the large straight-line trace
     mis-codegens the load-after-store dependency. Matrix multiply etc. are
     unaffected: they read a/b and write a *different* array c, with no
     element aliased. Detect by array-base register: GETI/GETTABLE/GETFIELD read
     R[B], SETI/SETTABLE/SETFIELD write R[A]; a register in both sets means the
     same array is read and written, so we fall back (safe) rather than risk it. */
  uint8_t reads[256]; uint8_t writes[256];
  memset(reads, 0, sizeof(reads)); memset(writes, 0, sizeof(writes));
  for (const Instruction *p = body_pc; p < forloop_pc; p++) {
    OpCode op = GET_OPCODE(*p);
    if (unroll_unsafe_op(op)) return 0;
    if (unroll_writes_dest_A(op)) {
      int da = GETARG_A(*p);
      if (da == a || da == a + 1 || da == a + 2) return 0;
    }
    switch (op) {
      case OP_GETI: case OP_GETTABLE: case OP_GETFIELD:
        reads[GETARG_B(*p) & 0xFF] = 1; break;
      case OP_SETI: case OP_SETTABLE: case OP_SETFIELD:
        writes[GETARG_A(*p) & 0xFF] = 1; break;
      default: break;
    }
  }
  for (int s = 0; s < 256; s++)
    if (reads[s] && writes[s]) return 0;     /* read-modify-write same array */

  /* For any bound that was not a compile-time KINT, pin it to the value we
     speculated with a run-time guard, snapshotting the FORPREP so a mismatch
     resumes the interpreter there (which re-runs the inner loop normally). The
     snapshot must be taken BEFORE pinning the control slots below, so it
     captures the real init/limit/step. All-constant loops emit no guard and are
     byte-identical to the previous (constant-only) unroller. */
  if (!init_k || !limit_k || !step_k) {
    int snap = rec_snap(rc);
    if (!init_k)  emit_pin_guard(ir, init_ref,  init,  snap);
    if (!limit_k) emit_pin_guard(ir, limit_ref, limit, snap);
    if (!step_k)  emit_pin_guard(ir, step_ref,  step,  snap);
  }

  /* Commit: replay the body `trips` times. From here we emit IR; if rec_inst
     aborts we propagate the abort (the trace is discarded). */
  for (int k = 0; k < trips; k++) {
    lua_Integer idx = init + (lua_Integer)k * step;
    /* Pin the control slots so any guard's snapshot in this copy flushes the
       exact interpreter state for inner iteration k (counter = count - k). */
    ir->reg_map[base + a]     = sptir_kint(ir, (lua_Integer)(count - (lua_Unsigned)k));
    ir->reg_type[base + a]    = SPTT_INT;
    ir->reg_map[base + a + 1] = sptir_kint(ir, step);
    ir->reg_type[base + a + 1] = SPTT_INT;
    ir->reg_map[base + a + 2] = sptir_kint(ir, idx);
    ir->reg_type[base + a + 2] = SPTT_INT;
    if (a + 2 > ir->maxslot) ir->maxslot = a + 2;

    rc->pc = body_pc;
    while (rc->pc < forloop_pc) {
      if (!rec_inst(rc)) {
        if (!rc->aborted) rc->aborted = 1;  /* unexpected early close -> discard */
        return 1;
      }
    }
  }

  /* Post-loop register state (what the interpreter would hold after the inner
     FORLOOP falls through): counter = 0, step unchanged, idx = init + count*step
     (the last body iteration's value; FORLOOP does not bump idx on exit). */
  ir->reg_map[base + a]     = sptir_kint(ir, 0);
  ir->reg_type[base + a]    = SPTT_INT;
  ir->reg_map[base + a + 1] = sptir_kint(ir, step);
  ir->reg_type[base + a + 1] = SPTT_INT;
  ir->reg_map[base + a + 2] = sptir_kint(ir, init + (lua_Integer)count * step);
  ir->reg_type[base + a + 2] = SPTT_INT;

  rc->pc = forloop_pc + 1;                 /* continue the outer body */
  return 1;
}


static int rec_inst(SPTRecCtx *rc) {
  SPTIRBuilder *ir = rc->ir;
  Instruction i = *rc->pc;
  OpCode op = GET_OPCODE(i);

  if (rc->js->debug >= 3) {
    fprintf(stderr, "  [rec] pc=%d %-12s A=%d B=%d C=%d k=%d sBx=%d\n",
            (int)(rc->pc - rc->p->code), opnames[op], GETARG_A(i), GETARG_B(i),
            GETARG_C(i), GETARG_k(i), GETARG_sBx(i));
  }

  if (rc->inst_count++ > SPT_JIT_MAX_TRACE) {
    rc->aborted = 1;
    return 0;
  }

  switch (op) {
    /* ---- Constants and moves ---- */
    case OP_MOVE: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      ir->reg_map[rc->frame_base + a] = bref;
      ir->reg_type[rc->frame_base + a] = ir->reg_type[rc->frame_base + b];
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADI: {
      int a = GETARG_A(i);
      int ref = sptir_kint(ir, GETARG_sBx(i));
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADF: {
      int a = GETARG_A(i);
      int ref = sptir_kflt(ir, (double)GETARG_sBx(i));
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_FLT;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADK: {
      int a = GETARG_A(i);
      int ref = rec_load_k(rc, GETARG_Bx(i));
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADKX: {
      int a = GETARG_A(i);
      rc->pc++; /* skip EXTRAARG */
      int ref = rec_load_k(rc, GETARG_Ax(*rc->pc));
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADFALSE: {
      int a = GETARG_A(i);
      ir->reg_map[rc->frame_base + a] = sptir_emit(ir, SPTIR_FALSE, SPTT_FALSE, SPTIR_NULL, SPTIR_NULL, 0);
      ir->reg_type[rc->frame_base + a] = SPTT_FALSE;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADTRUE: {
      int a = GETARG_A(i);
      ir->reg_map[rc->frame_base + a] = sptir_emit(ir, SPTIR_TRUE, SPTT_TRUE, SPTIR_NULL, SPTIR_NULL, 0);
      ir->reg_type[rc->frame_base + a] = SPTT_TRUE;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LFALSESKIP: {
      int a = GETARG_A(i);
      ir->reg_map[rc->frame_base + a] = sptir_emit(ir, SPTIR_FALSE, SPTT_FALSE, SPTIR_NULL, SPTIR_NULL, 0);
      ir->reg_type[rc->frame_base + a] = SPTT_FALSE;
      rc->pc++; /* skip next */
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADNIL: {
      int a = GETARG_A(i), b = GETARG_B(i);
      do {
        ir->reg_map[rc->frame_base + a] = sptir_emit(ir, SPTIR_NIL, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
        ir->reg_type[rc->frame_base + a] = SPTT_NIL;
        if (a > ir->maxslot) ir->maxslot = a;
        a++;
      } while (b--);
      break;
    }

    /* ---- Upvalues ---- */
    case OP_GETUPVAL: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int ref = sptir_emit(ir, SPTIR_ULOAD, SPTT_ANY, SPTIR_NULL, SPTIR_NULL, b);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_ANY;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_SETUPVAL: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int aref = rec_load_reg(rc, a);
      sptir_emit(ir, SPTIR_USTORE, SPTT_NIL, b, aref, 0);
      break;
    }

    /* ---- Table/Array access ---- */
    case OP_GETTABUP: {
      int a = GETARG_A(i), b = GETARG_B(i);
      /* UpValue[B][K[C]] for a constant short-string key (e.g. a global read:
         _ENV["g"], or a library table like _ENV["math"]). ULOAD yields the
         upvalue table; the codegen then does the same inline hash-slot search as
         GETFIELD (gen_hash_find) on it. We read the live upvalue table here to
         (a) confirm the key is present and (b) predict the value type; the
         codegen re-walks at run time so a moved/absent key still resolves or
         side-exits. Only the top frame's upvalues are read (an inlined callee
         would have different upvalues). */
      if (rc->frame_base != 0) { rc->aborted = 1; return 0; }
      TValue *kc = &rc->k[GETARG_C(i)];
      if (!ttisshrstring(kc)) { rc->aborted = 1; return 0; }
      TString *key = tsvalue(kc);
      LClosure *cl = clLvalue(s2v(rc->ci->func.p));
      if (b >= cl->nupvalues) { rc->aborted = 1; return 0; }
      TValue *envtv = cl->upvals[b]->v.p;
      if (!ttistable(envtv)) { rc->aborted = 1; return 0; }
      Table *envt = hvalue(envtv);
      Node *n = &envt->node[(unsigned int)key->hash & ((1u << envt->lsizenode) - 1u)];
      for (;;) {
        if (keytt(n) == ctb(LUA_VSHRSTR) && (void *)keyval(n).gc == (void *)key) break;
        int nx = (int)n->u.next;
        if (nx == 0) { rc->aborted = 1; return 0; }   /* key absent -> abort */
        n += nx;
      }
      SPTType et = rec_value_type(&n->i_val);
      if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR &&
          et != SPTT_ARR && et != SPTT_TAB) { rc->aborted = 1; return 0; }
      int upref = sptir_emit(ir, SPTIR_ULOAD, SPTT_ANY, SPTIR_NULL, SPTIR_NULL, b);
      int ref = sptir_emit(ir, SPTIR_GETTABUP, et, upref, SPTIR_NULL,
                           (int64_t)(intptr_t)key);
      int snap = rec_snap(rc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_GETI: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      SPTType bt = ir->reg_type[rc->frame_base + b];
      if (bt != SPTT_ARR) { rc->aborted = 1; return 0; }
      /* Element type from the live array. Numeric elements and (short or long)
         strings are loadable: the GETI codegen tag-guards then loads the 8-byte
         value (the TString* for a string), and exits restore any type. */
      SPTType et = rec_array_elem_type(rc, b, c);
      if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR && et != SPTT_ARR && et != SPTT_TAB) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= c < loglen (c is a constant here). */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LT, sptir_kint(ir, c), (int64_t)lenref, rec_guard_pc(rc));
      /* Type-guarded load. */
      int ref = sptir_emit(ir, SPTIR_GETI, et, bref, sptir_kint(ir, c), 0);
      int snap = rec_snap(rc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_GETFIELD: {
      /* R[A] = R[B][K[C]:shortstring]  -- map read by a constant short-string
         key. The key is interned, so the codegen's inline hash-slot fast path
         can pointer-compare it (see SPTIR_GETFIELD codegen). We record it only
         when, in the live map, the key sits at its MAIN POSITION (so the
         fast-path key guard will match) and its value has a traceable type;
         otherwise the compiled trace would always side-exit, so abort instead
         (correctness either way -- the guard, not this prediction, is load-bearing). */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      if (ir->reg_type[rc->frame_base + b] != SPTT_TAB) { rc->aborted = 1; return 0; }
      const TValue *kc = &rc->k[c];
      if (ttypetag(kc) != LUA_VSHRSTR) { rc->aborted = 1; return 0; }
      TString *key = tsvalue(kc);
      /* Multi-write method mode: guard-free field read. The entry field-layout
         guard already verified the key exists + value type matches, so we emit
         GETFIELD without SPTIRF_GUARD (no snapshot, no exit stub). This lifts
         the §10.49 single-trailing-write restriction: a read after a write no
         longer risks double-write on resume-at-SELF because there is no
         in-body guard to resume from. */
      if (rc->frame_base != 0 && rc->multiwrite_mode) {
        TValue mapval;
        if (!rec_eval_container(rc, bref, &mapval))
          mapval = *s2v((rc->ci->func.p + 1) + b);
        if (!ttistable(&mapval)) { rc->aborted = 1; return 0; }
        Table *t = hvalue(&mapval);
        Node *n = &t->node[(unsigned int)key->hash & ((1u << t->lsizenode) - 1u)];
        for (;;) {
          if (keytt(n) == ctb(LUA_VSHRSTR) && (void *)keyval(n).gc == (void *)key) break;
          int nx = (int)n->u.next;
          if (nx == 0) { rc->aborted = 1; return 0; }
          n += nx;
        }
        SPTType et = rec_value_type(&n->i_val);
        if (et != SPTT_INT && et != SPTT_FLT) { rc->aborted = 1; return 0; }
        int ref = sptir_emit(ir, SPTIR_GETFIELD, et, bref, SPTIR_NULL, (int64_t)(intptr_t)key);
        /* NO snap, NO SPTIRF_GUARD -- entry field-layout guard covers it */
        ir->reg_map[rc->frame_base + a] = ref;
        ir->reg_type[rc->frame_base + a] = et;
        if (a > ir->maxslot) ir->maxslot = a;
        /* register field for entry layout guard (dedup by key, update type) */
        for (int fi = 0; fi < rc->n_field_layouts; fi++) {
          if (rc->field_layouts[fi].key == (void *)key) {
            rc->field_layouts[fi].value_type = (uint8_t)et;
            goto getfield_mw_done;
          }
        }
        if (rc->n_field_layouts >= SPT_JIT_MAX_FIELD_LAYOUTS) { rc->aborted = 1; return 0; }
        rc->field_layouts[rc->n_field_layouts].key = (void *)key;
        rc->field_layouts[rc->n_field_layouts].value_type = (uint8_t)et;
        rc->n_field_layouts++;
        getfield_mw_done:
        break;
      }
      /* Store-to-load forwarding, inlined method bodies only (frame_base != 0):
         a read of the field just written by the preceding SETFIELD (same base IR
         ref + key) resolves to the written value with NO emit and NO guard. This
         keeps `int inc(){ this.v = this.v + 1; return this.v; }` (write then
         return the written field) a guard-free single-trailing-write -- the
         SETFIELD stays the last guard-emitting op (§10.49 safety). A read after a
         write that does NOT forward would emit a GETFIELD guard AFTER the write;
         on failure resume-at-SELF re-runs the whole method, double-writing -- so
         abort instead. (No existing inlinable shape reads a field after a write,
         so this only ever fires for the write-and-return shape or aborts.) */
      if (rc->frame_base != 0 && rc->fwd_base >= 0) {
        if (bref == rc->fwd_base && rc->fwd_key == (void *)key) {
          ir->reg_map[rc->frame_base + a] = rc->fwd_val;
          ir->reg_type[rc->frame_base + a] = sptir_type(ir, rc->fwd_val);
          if (a > ir->maxslot) ir->maxslot = a;
          break;
        }
        rc->aborted = 1; return 0;
      }
      /* Resolve the base Map through the IR when possible: for a chained
         m["k1"]["k2"] the intermediate map is produced earlier in THIS trace, so
         its live stack slot is stale (holds the previous iteration's value --
         the same §10.42 hazard). rec_eval_container follows SLOAD/GETI/GETFIELD
         to the record-time map; fall back to the live slot for a base it can't
         follow (e.g. a loop-invariant global via GETTABUP), preserving the prior
         behavior. Record-time only; the codegen re-walks with guards. */
      TValue mapval;
      if (!rec_eval_container(rc, bref, &mapval))
        mapval = *s2v((rc->ci->func.p + 1) + b);
      if (!ttistable(&mapval)) { rc->aborted = 1; return 0; }
      Table *t = hvalue(&mapval);
      /* Walk the key's main position + collision chain, exactly as the codegen
         (gen_hash_find) will, to (a) confirm the key is PRESENT -- if it is
         absent we abort, since the compiled chain-walk would side-exit every
         time -- and (b) predict the value's type for the guard. The codegen
         re-walks at run time, so a key that moves between record and run still
         resolves correctly (or side-exits); this is only a prediction. */
      Node *n = &t->node[(unsigned int)key->hash & ((1u << t->lsizenode) - 1u)];
      for (;;) {
        if (keytt(n) == ctb(LUA_VSHRSTR) && (void *)keyval(n).gc == (void *)key) break;
        int nx = (int)n->u.next;
        if (nx == 0) { rc->aborted = 1; return 0; }   /* key absent -> abort */
        n += nx;
      }
      SPTType et = rec_value_type(&n->i_val);
      if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR &&
          et != SPTT_ARR && et != SPTT_TAB) { rc->aborted = 1; return 0; }
      int ref = sptir_emit(ir, SPTIR_GETFIELD, et, bref, SPTIR_NULL, (int64_t)(intptr_t)key);
      int snap = rec_snap(rc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_GETTABLE: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_reg(rc, c);
      SPTType bt = ir->reg_type[rc->frame_base + b];
      SPTType ct = ir->reg_type[rc->frame_base + c];
      if (bt != SPTT_ARR || ct != SPTT_INT) { rc->aborted = 1; return 0; }
      /* Predict the element type from the index's stack value. One-pass
         recording reads it at the back-edge, so a reused index register may
         show a stale, non-integer, or out-of-range value; the IR already knows
         the index is an integer (ct == SPTT_INT) and the load is bounds- and
         type-guarded, so fall back to element 0 when the predictor is unusable
         (correctness is enforced by those guards, not by this prediction). */
      StkId base = rc->ci->func.p + 1;
      TValue *idxtv = s2v(base + c);
      lua_Integer pidx = ttisinteger(idxtv) ? ivalue(idxtv) : 0;
      SPTType et = rec_array_elem_type(rc, b, pidx);
      if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR && et != SPTT_ARR && et != SPTT_TAB) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= idx < loglen. */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LE, sptir_kint(ir, 0), (int64_t)cref, rec_guard_pc(rc));
      sptir_guard(ir, SPTIR_GUARD_LT, cref, (int64_t)lenref, rec_guard_pc(rc));
      /* Type-guarded load. */
      int ref = sptir_emit(ir, SPTIR_GETI, et, bref, cref, 0);
      int snap = rec_snap(rc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_SETI: {
      rc->fwd_base = -1;   /* array write may alias: drop store-to-load forwarding */
      /* R[A][B] = RK(C), B is a constant index. */
      int a = GETARG_A(i), b = GETARG_B(i);
      int aref = rec_load_reg(rc, a);
      if (ir->reg_type[rc->frame_base + a] != SPTT_ARR) { rc->aborted = 1; return 0; }
      int cref = rec_load_rkc(rc, i);
      SPTType vt = sptir_type(ir, cref);
      if (vt != SPTT_INT && vt != SPTT_FLT) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= b < loglen (in-bounds write only; growth -> exit). */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, aref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LT, sptir_kint(ir, b), (int64_t)lenref, rec_guard_pc(rc));
      sptir_emit(ir, SPTIR_SETI, SPTT_NIL, aref, sptir_kint(ir, b), (int64_t)cref);
      break;
    }
    case OP_SETTABLE: {
      rc->fwd_base = -1;   /* generic write may alias: drop store-to-load forwarding */
      /* R[A][R[B]] = RK(C), variable index. */
      int a = GETARG_A(i), b = GETARG_B(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      if (ir->reg_type[rc->frame_base + a] != SPTT_ARR || ir->reg_type[rc->frame_base + b] != SPTT_INT) {
        rc->aborted = 1; return 0;
      }
      int cref = rec_load_rkc(rc, i);
      SPTType vt = sptir_type(ir, cref);
      if (vt != SPTT_INT && vt != SPTT_FLT) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= idx < loglen. */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, aref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LE, sptir_kint(ir, 0), (int64_t)bref, rec_guard_pc(rc));
      sptir_guard(ir, SPTIR_GUARD_LT, bref, (int64_t)lenref, rec_guard_pc(rc));
      sptir_emit(ir, SPTIR_SETI, SPTT_NIL, aref, bref, (int64_t)cref);
      break;
    }
    case OP_SETFIELD: {
      /* R[A][K[B]:shortstring] := RK(C)  -- map write by a constant short
         string key. Symmetric to OP_GETFIELD's inline hash-slot fast path: we
         compile it only when the key ALREADY EXISTS at its MAIN POSITION in the
         live map (so the codegen overwrites the right node; an absent key would
         need an insert/rehash, and a chained key isn't at the computed slot --
         both side-exit to the interpreter). We also store only INTEGER/FLOAT
         values: those are non-collectable, so no GC write barrier is needed
         (exactly the restriction SETI uses). A collectable value (string/table)
         would require luaC_barrier and is left to the interpreter. */
      int a = GETARG_A(i), b = GETARG_B(i);
      int aref = rec_load_reg(rc, a);
      if (ir->reg_type[rc->frame_base + a] != SPTT_TAB) { rc->aborted = 1; return 0; }
      const TValue *kc = &rc->k[b];
      if (ttypetag(kc) != LUA_VSHRSTR) { rc->aborted = 1; return 0; }
      TString *key = tsvalue(kc);
      int cref = rec_load_rkc(rc, i);
      SPTType vt = sptir_type(ir, cref);
      if (vt != SPTT_INT && vt != SPTT_FLT) { rc->aborted = 1; return 0; }  /* no barrier */
      /* Resolve the base map by following the IR (SLOAD/GETI/GETFIELD), NOT the
         raw stack slot `a`: inside an inlined method `a` is the callee-relative
         slot (0 = this), whose absolute stack location is frame_base+a. The
         receiver SLOAD resolves to the live instance table. (Mirrors GETFIELD's
         §10.44 base resolution.) */
      TValue mapval;
      if (!rec_eval_container(rc, aref, &mapval) || !ttistable(&mapval)) { rc->aborted = 1; return 0; }
      Table *t = hvalue(&mapval);
      /* Confirm the key EXISTS somewhere in its chain (we update, never insert;
         an absent key side-exits via the codegen chain-walk). Same walk as
         gen_hash_find / luaH_Hgetshortstr. */
      Node *n = &t->node[(unsigned int)key->hash & ((1u << t->lsizenode) - 1u)];
      for (;;) {
        if (keytt(n) == ctb(LUA_VSHRSTR) && (void *)keyval(n).gc == (void *)key) break;
        int nx = (int)n->u.next;
        if (nx == 0) { rc->aborted = 1; return 0; }   /* key absent -> abort (would insert) */
        n += nx;
      }
      if (rc->frame_base != 0 && rc->multiwrite_mode) {
        /* Multi-write mode: guard-free field write. Entry field-layout guard
           verified the key exists, so no in-body guard needed. No snapshot,
           no exit stub -> no in-callee exit -> write-safe regardless of how
           many SETFIELDs precede this one. */
        int ref = sptir_emit(ir, SPTIR_SETFIELD, SPTT_NIL, aref, cref, (int64_t)(intptr_t)key);
        /* NO snap, NO SPTIRF_GUARD */
        /* register field for entry layout guard (dedup by key, update type) */
        for (int fi = 0; fi < rc->n_field_layouts; fi++) {
          if (rc->field_layouts[fi].key == (void *)key) {
            rc->field_layouts[fi].value_type = (uint8_t)vt;
            goto setfield_mw_done;
          }
        }
        if (rc->n_field_layouts >= SPT_JIT_MAX_FIELD_LAYOUTS) { rc->aborted = 1; return 0; }
        rc->field_layouts[rc->n_field_layouts].key = (void *)key;
        rc->field_layouts[rc->n_field_layouts].value_type = (uint8_t)vt;
        rc->n_field_layouts++;
        setfield_mw_done:
        break;
      }
      int snap = rec_snap(rc);
      int ref = sptir_emit(ir, SPTIR_SETFIELD, SPTT_NIL, aref, cref, (int64_t)(intptr_t)key);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      /* arm store-to-load forwarding: a later GETFIELD of this exact base+key in
         the same inlined method body forwards to the written value, guard-free. */
      rc->fwd_base = aref; rc->fwd_key = (void *)key; rc->fwd_val = cref;
      break;
    }

    /* ---- Arithmetic ---- */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_IDIV: case OP_POW: {
      /* OP_POW emits SPTIR_POW, which has NO codegen (it would need a libm pow
         C call). No current SPT syntax generates OP_POW (`^` lexes to bitwise
         XOR / OP_BXOR), so this is defensive: abort cleanly rather than emit an
         un-codegen-able op that would compile to garbage if OP_POW ever appears.
         (Enable via the C-call milestone -- see JIT_DEV_NOTES §10.40.) */
      if (op == OP_POW) { rc->aborted = 1; return 0; }
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_reg(rc, c);
      SPTType bt = ir->reg_type[rc->frame_base + b], ct = ir->reg_type[rc->frame_base + c];
      SPTIROp irop;
      switch (op) {
        case OP_ADD:  irop = SPTIR_ADD;  break;
        case OP_SUB:  irop = SPTIR_SUB;  break;
        case OP_MUL:  irop = SPTIR_MUL;  break;
        case OP_DIV:  irop = SPTIR_DIV;  break;
        case OP_MOD:  irop = SPTIR_MOD;  break;
        case OP_IDIV: irop = SPTIR_IDIV; break;
        case OP_POW:  irop = SPTIR_POW;  break;
        default: irop = SPTIR_ADD;
      }
      int ref = rec_arith(rc, irop, bref, cref, bt, ct);
      if (rc->aborted) return 0;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* skip MMBIN if present */
      break;
    }
    case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
    case OP_MODK: case OP_IDIVK: case OP_POWK: {
      /* See OP_POW above: SPTIR_POW has no codegen; abort defensively. */
      if (op == OP_POWK) { rc->aborted = 1; return 0; }
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_k(rc, c);
      SPTType bt = ir->reg_type[rc->frame_base + b], ct = sptir_type(ir, cref);
      SPTIROp irop;
      switch (op) {
        case OP_ADDK:  irop = SPTIR_ADD;  break;
        case OP_SUBK:  irop = SPTIR_SUB;  break;
        case OP_MULK:  irop = SPTIR_MUL;  break;
        case OP_DIVK:  irop = SPTIR_DIV;  break;
        case OP_MODK:  irop = SPTIR_MOD;  break;
        case OP_IDIVK: irop = SPTIR_IDIV; break;
        case OP_POWK:  irop = SPTIR_POW;  break;
        default: irop = SPTIR_ADD;
      }
      int ref = rec_arith(rc, irop, bref, cref, bt, ct);
      if (rc->aborted) return 0;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_ADDI: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_sC(i);
      int bref = rec_load_reg(rc, b);
      int cref = sptir_kint(ir, c);
      SPTType bt = ir->reg_type[rc->frame_base + b];
      int ref = rec_arith(rc, SPTIR_ADD, bref, cref, bt, SPTT_INT);
      if (rc->aborted) return 0;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }

    /* ---- Bitwise ---- */
    case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_reg(rc, c);
      /* Bitwise ops require integer operands. A float operand is converted to an
         integer by the VM (erroring if it has a fractional part) -- we don't
         replicate that, and emitting the op on a float reinterprets its raw IEEE
         bits, so abort and let the interpreter handle non-int operands. */
      if (ir->reg_type[rc->frame_base + b] != SPTT_INT ||
          ir->reg_type[rc->frame_base + c] != SPTT_INT) {
        rc->aborted = 1; return 0;
      }
      SPTIROp irop;
      switch (op) {
        case OP_BAND: irop = SPTIR_BAND; break;
        case OP_BOR:  irop = SPTIR_BOR;  break;
        case OP_BXOR: irop = SPTIR_BXOR; break;
        case OP_SHL:  irop = SPTIR_SHL;  break;
        case OP_SHR:  irop = SPTIR_SHR;  break;
        default: irop = SPTIR_BAND;
      }
      /* x86 variable shift masks the count to 6 bits, diverging from
         luaV_shiftl for counts outside [0,63] (Lua yields 0). Guard the count
         in range so the common case is correct; unusual counts side-exit. */
      if (op == OP_SHL || op == OP_SHR)
        sptir_guard(ir, SPTIR_GUARD_ULT, cref, 64, rec_guard_pc(rc));
      int ref = sptir_emit(ir, irop, SPTT_INT, bref, cref, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_BANDK: case OP_BORK: case OP_BXORK: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_k(rc, c);
      if (ir->reg_type[rc->frame_base + b] != SPTT_INT ||
          sptir_type(ir, cref) != SPTT_INT) {
        rc->aborted = 1; return 0;  /* see OP_BAND: non-int needs the interpreter */
      }
      SPTIROp irop;
      switch (op) {
        case OP_BANDK: irop = SPTIR_BAND; break;
        case OP_BORK:  irop = SPTIR_BOR;  break;
        case OP_BXORK: irop = SPTIR_BXOR; break;
        default: irop = SPTIR_BAND;
      }
      int ref = sptir_emit(ir, irop, SPTT_INT, bref, cref, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_SHLI: {
      /* R[A] = luaV_shiftl(sC, R[B]) = constant sC shifted left by R[B].
         The shift AMOUNT is the dynamic register R[B]; the value is the
         immediate sC. We specialize the common case (0 <= R[B] < 64) and
         guard it so out-of-range counts side-exit to the interpreter. */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_sC(i);
      int bref = rec_load_reg(rc, b);
      if (ir->reg_type[rc->frame_base + b] != SPTT_INT) {
        rc->aborted = 1; return 0;  /* non-int shift amount -> interpreter */
      }
      /* Guard amount in [0, 63] (unsigned < 64). */
      sptir_guard(ir, SPTIR_GUARD_ULT, bref, 64, rec_guard_pc(rc));
      int cref = sptir_kint(ir, c);
      int ref = sptir_emit(ir, SPTIR_SHL, SPTT_INT, cref, bref, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_SHRI: {
      /* R[A] = luaV_shiftl(R[B], -sC): R[B] shifted by the compile-time
         amount (-sC). Direction depends on the sign of -sC. */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_sC(i);
      int bref = rec_load_reg(rc, b);
      if (ir->reg_type[rc->frame_base + b] != SPTT_INT) {
        rc->aborted = 1; return 0;  /* non-int shifted value -> interpreter */
      }
      int ref = rec_shift_const(rc, bref, -(int64_t)c);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }

    /* ---- Unary ---- */
    case OP_UNM: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      SPTType bt = ir->reg_type[rc->frame_base + b];
      /* Negation is numeric-only; SPTIR_NEG negates an int or float register.
         A string/array/etc. operand would be negated as an integer (garbage),
         and the VM errors on it anyway, so abort for non-numeric operands. */
      if (bt != SPTT_INT && bt != SPTT_FLT) {
        rc->aborted = 1; return 0;
      }
      int ref = sptir_emit(ir, SPTIR_NEG, bt, bref, SPTIR_NULL, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = bt;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* UNM has no MMBIN; no-op */
      break;
    }
    case OP_BNOT: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      /* '~' is integer-only; a float operand is converted to an integer by the
         VM (erroring on a fractional part). Emitting an integer complement on a
         float reinterprets its raw bits, so abort and let the interpreter run a
         non-int operand (same as the other bitwise ops). */
      if (ir->reg_type[rc->frame_base + b] != SPTT_INT) {
        rc->aborted = 1; return 0;
      }
      int ref = sptir_emit(ir, SPTIR_BNOT, SPTT_INT, bref, SPTIR_NULL, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* BNOT has no MMBIN; no-op */
      break;
    }
    case OP_NOT: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      int ref = sptir_emit(ir, SPTIR_NOT, SPTT_TRUE, bref, SPTIR_NULL, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_TRUE;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LEN: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      /* String '#': SHORT-string byte length via SLEN. The op reads TString.
         shrlen as an unsigned byte and guards short (long strings side-exit).
         A LONG string would fail that guard EVERY iteration, and a trace whose
         guard always fails is discarded and re-recorded -- an unbounded compile
         thrash that the runtime never escapes. To avoid that we only emit SLEN
         when we can OBSERVE the live string is short at record time, and only at
         root (an inlined method body has no live callee frame to read R[b]). A
         long (or non-observable) string aborts recording instead, so the abort
         counter blacklists this PC and the interpreter takes over -- bounded and
         correct. The runtime short-guard then only ever fires on the rare
         short->long mutation, which re-records, re-observes long, and blacklists. */
      if (ir->reg_type[rc->frame_base + b] == SPTT_STR) {
        if (rc->frame_base != 0) { rc->aborted = 1; return 0; }
        TValue *sv = s2v((rc->ci->func.p + 1) + b);
        if (!ttisstring(sv) || !strisshr(tsvalue(sv))) { rc->aborted = 1; return 0; }
        int ref = sptir_emit(ir, SPTIR_SLEN, SPTT_INT, bref, SPTIR_NULL, 0);
        int snap = rec_snap(rc);
        ir->insts[ref].snap_idx = snap;
        ir->insts[ref].flags |= SPTIRF_GUARD;
        ir->reg_map[rc->frame_base + a] = ref;
        ir->reg_type[rc->frame_base + a] = SPTT_INT;
        if (a > ir->maxslot) ir->maxslot = a;
        rec_skip_mmbin(rc); /* LEN has no MMBIN; no-op */
        break;
      }
      /* SPTIR_LEN reads an array's length field (Table->loglen). For a string,
         a map, or anything else, '#' has different semantics (e.g. byte length,
         stored in a different field that varies between short and long strings),
         so reading the array field yields garbage. Only arrays are handled here;
         everything else aborts to the interpreter, which is correct. */
      if (ir->reg_type[rc->frame_base + b] != SPTT_ARR) {
        rc->aborted = 1; return 0;
      }
      int ref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* LEN has no MMBIN; no-op */
      break;
    }

    /* ---- Comparisons ---- */
    case OP_EQ: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      SPTType at = ir->reg_type[rc->frame_base + a], bt = ir->reg_type[rc->frame_base + b];
      /* Two string *variables*: either could be a long (non-interned) string,
         so pointer comparison is unsafe. Abort. */
      if (at == SPTT_STR || bt == SPTT_STR) { rc->aborted = 1; return 0; }
      return rec_cond_branch(rc, k ? SPTIR_NE : SPTIR_EQ, aref, bref, at, bt, a, b);
    }
    case OP_LT: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      SPTType at = ir->reg_type[rc->frame_base + a], bt = ir->reg_type[rc->frame_base + b];
      return rec_cond_branch(rc, k ? SPTIR_GE : SPTIR_LT, aref, bref, at, bt, a, b);
    }
    case OP_LE: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      SPTType at = ir->reg_type[rc->frame_base + a], bt = ir->reg_type[rc->frame_base + b];
      return rec_cond_branch(rc, k ? SPTIR_GT : SPTIR_LE, aref, bref, at, bt, a, b);
    }
    case OP_EQI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      return rec_cond_branch(rc, k ? SPTIR_NE : SPTIR_EQ, aref, bref,
                             ir->reg_type[rc->frame_base + a], SPTT_INT, a, -1);
    }
    case OP_LTI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      return rec_cond_branch(rc, k ? SPTIR_GE : SPTIR_LT, aref, bref,
                             ir->reg_type[rc->frame_base + a], SPTT_INT, a, -1);
    }
    case OP_LEI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      return rec_cond_branch(rc, k ? SPTIR_GT : SPTIR_LE, aref, bref,
                             ir->reg_type[rc->frame_base + a], SPTT_INT, a, -1);
    }
    case OP_GTI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      return rec_cond_branch(rc, k ? SPTIR_LE : SPTIR_GT, aref, bref,
                             ir->reg_type[rc->frame_base + a], SPTT_INT, a, -1);
    }
    case OP_GEI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      return rec_cond_branch(rc, k ? SPTIR_LT : SPTIR_GE, aref, bref,
                             ir->reg_type[rc->frame_base + a], SPTT_INT, a, -1);
    }
    case OP_EQK: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_k(rc, b);
      SPTType at = ir->reg_type[rc->frame_base + a], bt = sptir_type(ir, bref);
      if (at == SPTT_STR && bt == SPTT_STR) {
        /* `s == "literal"`: only safe when the constant is a short interned
           string -- then pointer equality is exact. */
        if (!ttisshrstring(&rc->k[b])) { rc->aborted = 1; return 0; }
        return rec_str_cond_branch(rc, k, aref, bref, a, b);
      }
      return rec_cond_branch(rc, k ? SPTIR_NE : SPTIR_EQ, aref, bref,
                             at, bt, a, -1);
    }

    /* ---- Tests ---- */
    case OP_TEST: {
      int a = GETARG_A(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a); /* emits GUARD_T pinning R[A]'s exact tag */
      int cond;
      /* The tag guard fixes truthiness (true/false/nil are distinct tags); read
         it from the IR ref (a reused slot's stack value would be stale). */
      if (!rec_ref_truthy(rc, aref, &cond)) { rc->aborted = 1; return 0; }
      const Instruction *jmp = rc->pc + 1;
      if (cond != k)
        rc->pc = jmp + 1;                            /* fall-through */
      else
        rc->pc = jmp + 1 + GETARG_sJ(*jmp);          /* take the JMP */
      return 1;
    }
    case OP_TESTSET: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int bref = rec_load_reg(rc, b); /* GUARD_T pins R[B]'s tag/truthiness */
      int cond;
      if (!rec_ref_truthy(rc, bref, &cond)) { rc->aborted = 1; return 0; }
      int isfalse = !cond;
      const Instruction *jmp = rc->pc + 1;
      if (isfalse == k) {
        /* Fall-through: R[A] is NOT assigned. */
        rc->pc = jmp + 1;
      } else {
        /* Branch: R[A] := R[B], then take the JMP. */
        ir->reg_map[rc->frame_base + a] = bref;
        ir->reg_type[rc->frame_base + a] = ir->reg_type[rc->frame_base + b];
        if (a > ir->maxslot) ir->maxslot = a;
        rc->pc = jmp + 1 + GETARG_sJ(*jmp);
      }
      return 1;
    }

    /* ---- Jumps ---- */
    case OP_JMP: {
      int sj = GETARG_sJ(i);
      if (sj < 0) {
        /* Backward jump. Only our own loop header (start_pc) closes the trace.
           A backward jump to any OTHER target is an INNER loop's back-edge --
           e.g. a `while` nested inside the `for` we are tracing. We cannot
           represent a nested loop in a single linear trace; closing here would
           fold the inner loop's pre-header (its init) into the body and produce
           a non-terminating trace (constant exit guard). So abort instead and
           let the interpreter run this loop; the inner loop still gets its own
           correct trace from its own header. */
        const Instruction *target = rc->pc + 1 + sj;
        if (target == rc->start_pc) {
          sptir_loop(ir);
          return 0;
        }
        /* Back-edge to a target before our start. In a root trace this is an
           inner loop we can't represent (abort). In a SIDE trace started
           mid-loop, the parent's loop header is necessarily before our start, so
           this is the expected close point: emit an unconditional exit to the
           interpreter at the back-edge PC and end successfully. The interpreter
           runs the back-edge JMP and re-enters the parent (or the link
           trampoline hands off). */
        if (rc->is_side_trace) {
          sptir_exit(ir, rc->pc);
          return 0;
        }
        rc->aborted = 1;
        rc->abort_pc = rc->pc;
        return 0;
      }
      /* Forward jump: follow it. The straight-line trace must track the actual
         control flow taken at record time. A forward JMP is unconditional (any
         condition was already turned into a guard by the preceding test), so we
         jump to its target rather than falling through -- otherwise we'd record
         BOTH arms of an if/else. Target = pc + 1 + sJ; returning 1 skips the
         default pc++ at the end of rec_inst. */
      rc->pc += sj + 1;
      return 1;
    }

    /* ---- Numeric for loop ---- */
    case OP_FORPREP: {
      /* SPT for loop: after FORPREP, R[A]=count, R[A+1]=step, R[A+2]=init.
         FORPREP jumps to FORLOOP, so we skip it in the trace. But if this is an
         inner loop inside the (outer) trace we are recording, try to unroll it
         inline first so the whole nest becomes one linear trace (Phase 3a). */
      if (try_unroll_inner_loop(rc, i))
        return rc->aborted ? 0 : 1;
      int bx = GETARG_Bx(i);
      rc->pc += bx + 1;
      return 1;
    }
    case OP_FORLOOP: {
      /* SPT FORLOOP layout (different from standard Lua!):
         R[A] = count (unsigned, decremented each iteration)
         R[A+1] = step
         R[A+2] = idx (incremented by step each iteration)
         if count > 0: count--; idx += step; jump back to loop body */
      int a = GETARG_A(i);

      /* Nested-loop guard: this back-edge only closes the trace if it jumps
         to OUR loop header. The VM computes the target as pc - Bx with pc
         already advanced past the FORLOOP, i.e. (rc->pc + 1) - Bx here. A
         FORLOOP that targets a different header belongs to an inner loop we
         recorded through; a single linear trace can't represent that, so we
         abort and let the interpreter drive the outer loop. The inner loop
         keeps its own (already compiled) trace, so the hot path is preserved. */
      const Instruction *target = rc->pc + 1 - GETARG_Bx(i);
      if (target != rc->start_pc) {
        /* Side trace reaching the (outer) loop's FORLOOP back-edge: it targets
           the loop header, which is before our mid-loop start, so we cannot loop
           on it. Close with an unconditional exit at the FORLOOP PC *before*
           emitting the count/idx update -- the interpreter then executes FORLOOP
           (decrement, idx += step, back-jump) and its hot-check re-enters the
           parent. Slots we never touched (idx, count) keep their flushed stack
           values via the snapshot's -1 entries; that is exactly correct. */
        if (rc->is_side_trace) {
          sptir_exit(ir, rc->pc);
          return 0;
        }
        rc->aborted = 1;
        return 0;
      }

      int count_ref = rec_load_reg(rc, a);       /* R[A] = count */
      int step_ref = rec_load_reg(rc, a + 1);    /* R[A+1] = step */
      int idx_ref = rec_load_reg(rc, a + 2);     /* R[A+2] = idx */

      /* Guard: count > 0 (continue loop). Use GUARD_LT with 0 < count. */
      int zero_ref = sptir_kint(ir, 0);
      int guard_ref = sptir_guard(ir, SPTIR_GUARD_LT, zero_ref,
                                  (int64_t)count_ref, rec_guard_pc(rc));
      if (guard_ref >= 0)
        rc->loop_end_snap = ir->insts[guard_ref].snap_idx;

      /* new_count = count - 1 */
      int one_ref = sptir_kint(ir, 1);
      int new_count = sptir_emit(ir, SPTIR_SUB, SPTT_INT, count_ref, one_ref, 0);
      ir->reg_map[rc->frame_base + a] = new_count;
      ir->reg_type[rc->frame_base + a] = SPTT_INT;

      /* new_idx = idx + step */
      int new_idx = sptir_emit(ir, SPTIR_ADD, SPTT_INT, idx_ref, step_ref, 0);
      ir->reg_map[rc->frame_base + a + 2] = new_idx;
      ir->reg_type[rc->frame_base + a + 2] = SPTT_INT;

      if (a > ir->maxslot) ir->maxslot = a;
      if (a + 2 > ir->maxslot) ir->maxslot = a + 2;

      /* This is the loop back-edge. */
      sptir_loop(ir);
      return 0;
    }

    /* ---- Returns ---- */
    case OP_RETURN0: {
      if (rc->frame_base != 0) {
        /* Void return from an inlined single-trailing-write method: restore the
           caller's recording context with NO result to bind, and keep recording
           the caller after the CALL. The method's only side effect -- its single
           trailing field write -- is already emitted and resume-at-SELF
           protected: the write is the last committing op, so any guard failure
           side-exits before it commits and the interpreter re-runs the call
           exactly once (no double write). See proto_is_write_method_inlinable. */
        rc->p = rc->save_p;
        rc->k = rc->save_k;
        rc->cl = rc->save_cl;
        rc->frame_base = rc->save_frame_base;
        rc->pc = rc->save_pc;
        rc->method_resume_snap = -1;
        rc->method_self_pc = NULL;
        return 1;
      }
      sptir_emit(ir, SPTIR_RETURN, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
      return 0;
    }
    case OP_RETURN1: {
      int a = GETARG_A(i);
      if (rc->frame_base != 0) {
        /* Returning from an inlined callee: bind the caller's result register to
           the returned value's IR ref, restore the caller's recording context,
           and keep recording the caller after the CALL. */
        int aref = rec_load_reg(rc, a);
        if (rc->aborted) return 0;
        SPTType rt = ir->reg_type[rc->frame_base + a];
        rc->p = rc->save_p;
        rc->k = rc->save_k;
        rc->cl = rc->save_cl;
        rc->frame_base = rc->save_frame_base;
        rc->pc = rc->save_pc;
        ir->reg_map[rc->call_result_slot] = aref;
        ir->reg_type[rc->call_result_slot] = rt;
        if (rc->call_result_slot > ir->maxslot) ir->maxslot = rc->call_result_slot;
        rc->method_resume_snap = -1;            /* leaving method body */
        rc->method_self_pc = NULL;
        return 1;
      }
      int aref = rec_load_reg(rc, a);
      sptir_emit(ir, SPTIR_RETURN, ir->reg_type[rc->frame_base + a], aref, SPTIR_NULL, 0);
      return 0;
    }
    case OP_RETURN: {
      int a = GETARG_A(i), b = GETARG_B(i);
      if (rc->frame_base != 0) {
        /* Inlined method void return (b==1): restore the caller like OP_RETURN0,
           no result to bind. A value-returning OP_RETURN from an inlined method
           is not supported here (write methods are void). */
        if (b != 1) { rc->aborted = 1; return 0; }
        rc->p = rc->save_p;
        rc->k = rc->save_k;
        rc->cl = rc->save_cl;
        rc->frame_base = rc->save_frame_base;
        rc->pc = rc->save_pc;
        rc->method_resume_snap = -1;
        rc->method_self_pc = NULL;
        return 1;
      }
      if (b == 1) {
        sptir_emit(ir, SPTIR_RETURN, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
      } else {
        int aref = rec_load_reg(rc, a);
        sptir_emit(ir, SPTIR_RETURN, ir->reg_type[rc->frame_base + a], aref, SPTIR_NULL, 0);
      }
      return 0;
    }

    /* ---- Call inlining (pure straight-line leaf functions) ---- */
    case OP_CALL: {
      rc->fwd_base = -1;   /* a call may mutate: drop store-to-load forwarding (re-armed on method inline) */
      rc->call_arg_self_pc = NULL;  /* close the SELF->CALL arg-load window */
      /* CALL A B C : R[A](R[A+1..A+B-1]) -> R[A..A+C-2].
         SPT uses a slot-0 receiver, so R[A+1] is the (nil) receiver and
         R[A+2..] are the actual arguments. */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);

      /* A multiret arg slot (set by a nested min/max whose result this CALL may
         consume) is valid for exactly this one CALL. Capture and clear it now so
         it never leaks to a later CALL. */
      int incoming_mm_top = rc->minmax_multiret_top;
      rc->minmax_multiret_top = -1;

      /* Pending unary-math call armed by a preceding SELF (e.g. math.sqrt)?
         The SPT convention puts the receiver at R[A+1] and the single real
         argument at R[A+2]; math_* read luaL_checknumber(L, 2). Lower it to a
         direct libm call (SPTIR_FMATH). Requires exactly one real arg
         (B == 3: function + receiver + arg) and one result (C == 2). */
      if (rc->pending_cfn_slot == rc->frame_base + a) {
        void *libm = rc->pending_cfn_libm;
        rc->pending_cfn_slot = -1; rc->pending_cfn_libm = NULL;
        if (b != 3 || c != 2) { rc->aborted = 1; return 0; }
        int arg = ir->reg_map[rc->frame_base + a + 2];
        if (arg < 0) { rc->aborted = 1; return 0; }
        SPTType at = ir->reg_type[rc->frame_base + a + 2];
        if (at == SPTT_INT) {
          arg = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, arg, SPTIR_NULL, 0);
        } else if (at != SPTT_FLT) {
          rc->aborted = 1; return 0;       /* non-numeric arg */
        }
        int fref = sptir_emit(ir, SPTIR_FMATH, SPTT_FLT, arg, SPTIR_NULL,
                              (int64_t)(intptr_t)libm);
        ir->reg_map[rc->frame_base + a] = fref;
        ir->reg_type[rc->frame_base + a] = SPTT_FLT;
        if (a > ir->maxslot) ir->maxslot = a;
        break;
      }

      /* Pending string.len / string.byte armed by a preceding SELF? Lower to
         SLEN / SBYTE. SPT puts the receiver (string module) at R[A+1], s at
         R[A+2], and (for byte) i at R[A+3] -- str_* read arg2 = s, arg3 = i.
         Only at root (frame_base == 0), where R[A+2] can be read live. SLEN /
         SBYTE guard short at runtime, but a LONG string fails that guard every
         iteration and thrashes, so we additionally require the live string to be
         observably SHORT here (the abort blacklists the PC -> bounded), exactly
         as the '#s' path does. SBYTE's bounds guard needs no such check: an
         out-of-range string.byte returns no value, so the side-exit lands the
         interpreter on the same error (it never loops gracefully). */
      if (rc->pending_str_slot == rc->frame_base + a) {
        int sop = rc->pending_str_op;
        const Instruction *str_self_pc = rc->pending_str_self_pc;
        rc->pending_str_slot = -1; rc->pending_str_op = 0;
        if (rc->frame_base != 0 || c != 2) { rc->aborted = 1; return 0; }
        int sref = ir->reg_map[rc->frame_base + a + 2];
        if (sref < 0 || ir->reg_type[rc->frame_base + a + 2] != SPTT_STR) { rc->aborted = 1; return 0; }
        TValue *sv = s2v((rc->ci->func.p + 1) + (a + 2));
        if (!ttisstring(sv) || !strisshr(tsvalue(sv))) { rc->aborted = 1; return 0; }
        if (sop == 1) {                          /* string.len(s) -> SLEN */
          if (b != 3) { rc->aborted = 1; return 0; }
          int ref = sptir_emit(ir, SPTIR_SLEN, SPTT_INT, sref, SPTIR_NULL, 0);
          /* Resume the short-string guard at the SELF (not this CALL): the
             interpreter re-executes SELF+CALL and runs string.len for a long
             string. Idempotent: a length read has no side effects. */
          int snap = sptir_snapshot(ir, str_self_pc);
          ir->insts[ref].snap_idx = snap; ir->insts[ref].flags |= SPTIRF_GUARD;
          ir->reg_map[rc->frame_base + a] = ref;
          ir->reg_type[rc->frame_base + a] = SPTT_INT;
          if (a > ir->maxslot) ir->maxslot = a;
          break;
        } else {                                 /* string.byte(s, i) -> SBYTE */
          if (b != 4) { rc->aborted = 1; return 0; }
          int iref = ir->reg_map[rc->frame_base + a + 3];
          if (iref < 0 || ir->reg_type[rc->frame_base + a + 3] != SPTT_INT) { rc->aborted = 1; return 0; }
          int ref = sptir_emit(ir, SPTIR_SBYTE, SPTT_INT, sref, iref, 0);
          /* Resume the short-string / bounds guard at the SELF (not this CALL):
             the interpreter re-executes SELF+CALL and runs the real string.byte
             for a long string, or errors identically for an out-of-range index.
             Idempotent: a byte read has no side effects. */
          int snap = sptir_snapshot(ir, str_self_pc);
          ir->insts[ref].snap_idx = snap; ir->insts[ref].flags |= SPTIRF_GUARD;
          ir->reg_map[rc->frame_base + a] = ref;
          ir->reg_type[rc->frame_base + a] = SPTT_INT;
          if (a > ir->maxslot) ir->maxslot = a;
          break;
        }
      }

      /* Pending math.min / math.max armed by a preceding SELF? Lower to a
         branchless select (reusing emit_select). SPT puts the receiver (math
         module) at R[A+1] and the two values at R[A+2], R[A+3]. Requires exactly
         two args (b == 4) and one result (c == 2). Lua's math.max(a,b) =
         (b > a) ? b : a = (a < b) ? b : a; math.min(a,b) = (b < a) ? b : a =
         (a > b) ? b : a -- so out = (a FOP b) ? b : a with FOP = LT for max,
         GT for min. emit_select's compare-false -> else (= a) matches Lua's
         "keep the current arg unless strictly beaten" exactly (ties and NaN both
         fall to a). Matches the LIFO top of the pending stack so a nested clamp
         pops the inner min before the outer max. Mixed int/float args are
         promoted to float first (Lua promotes); result type follows emit_select. */
      if (rc->pending_minmax_top > 0 &&
          rc->pending_minmax_slot[rc->pending_minmax_top - 1] == rc->frame_base + a) {
        rc->pending_minmax_top--;
        int mmop = rc->pending_minmax_op[rc->pending_minmax_top];
        /* Two values at R[A+2], R[A+3]. Normally b == 4 (fn + receiver + 2). When
           the trailing arg is itself a (just-lowered) nested min/max, Lua emits
           this CALL with b == 0 (args to top); that inner result is the single
           value at minmax_multiret_top, which must be exactly R[A+3] (i.e. exactly
           2 args). c == 2 for one result, or c == 0 when THIS result in turn feeds
           an enclosing call (still a single value). */
        if (b != 4 && !(b == 0 && incoming_mm_top == rc->frame_base + a + 3)) { rc->aborted = 1; return 0; }
        if (c != 2 && c != 0) { rc->aborted = 1; return 0; }
        int aref = ir->reg_map[rc->frame_base + a + 2];
        int bref = ir->reg_map[rc->frame_base + a + 3];
        if (aref < 0 || bref < 0) { rc->aborted = 1; return 0; }
        SPTType at = ir->reg_type[rc->frame_base + a + 2];
        SPTType bt = ir->reg_type[rc->frame_base + a + 3];
        if (!sptt_isnum(at) || !sptt_isnum(bt)) { rc->aborted = 1; return 0; }
        /* Promote a mixed pair to float so emit_select's both-float path applies
           and the result is float, as Lua does for a mixed min/max. */
        if (at == SPTT_INT && bt == SPTT_FLT) {
          aref = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, aref, SPTIR_NULL, 0); at = SPTT_FLT;
        } else if (at == SPTT_FLT && bt == SPTT_INT) {
          bref = sptir_emit(ir, SPTIR_TOFLT, SPTT_FLT, bref, SPTIR_NULL, 0); bt = SPTT_FLT;
        }
        SPTIROp fop = (mmop == 2) ? SPTIR_LT : SPTIR_GT;   /* 2 = max -> LT, 1 = min -> GT */
        int out_ref; SPTType out_t;
        /* out = (a FOP b) ? then(=b) : else(=a) */
        if (!emit_select(rc, fop, aref, bref, at, bt, bref, bt, aref, at, &out_ref, &out_t)) {
          rc->aborted = 1; return 0;
        }
        ir->reg_map[rc->frame_base + a] = out_ref;
        ir->reg_type[rc->frame_base + a] = out_t;
        if (a > ir->maxslot) ir->maxslot = a;
        /* If this result is itself a trailing arg (c == 0), record its slot so the
           enclosing CALL can confirm its arg count. */
        if (c == 0) rc->minmax_multiret_top = rc->frame_base + a;
        break;
      }

      /* Pending math.abs armed by a preceding SELF? Lower abs(x) with no real
         call. SPT puts the receiver (math module) at R[A+1] and the single value
         at R[A+2]. Normally b == 3 (fn + receiver + 1 arg); when that arg is a
         just-lowered nested min/max, Lua emits this CALL with b == 0 (args to
         top) and the lone value is at minmax_multiret_top, which must be exactly
         R[A+2]. c == 2 for one result, or c == 0 when this result in turn feeds
         an enclosing call. INT: branchless select (x<0)?(0-x):x via emit_select
         -- bit-exact incl. INT64_MIN (SPTIR_SUB wraps as Lua's 0u-n). FLOAT:
         SPTIR_FMATH(fabs) -- not a select, so -0.0 -> +0.0 and NaN are correct. */
      if (rc->pending_abs_slot == rc->frame_base + a) {
        void *absfn = rc->pending_abs_fabs;
        rc->pending_abs_slot = -1; rc->pending_abs_fabs = NULL;
        if (b != 3 && !(b == 0 && incoming_mm_top == rc->frame_base + a + 2)) { rc->aborted = 1; return 0; }
        if (c != 2 && c != 0) { rc->aborted = 1; return 0; }
        int xref = ir->reg_map[rc->frame_base + a + 2];
        if (xref < 0) { rc->aborted = 1; return 0; }
        SPTType xt = ir->reg_type[rc->frame_base + a + 2];
        int out_ref; SPTType out_t;
        if (xt == SPTT_INT) {
          int zero = sptir_kint(ir, 0);
          int neg = sptir_emit(ir, SPTIR_SUB, SPTT_INT, zero, xref, 0);   /* 0 - x */
          /* out = (x < 0) ? (0 - x) : x */
          if (!emit_select(rc, SPTIR_LT, xref, zero, SPTT_INT, SPTT_INT,
                           neg, SPTT_INT, xref, SPTT_INT, &out_ref, &out_t)) {
            rc->aborted = 1; return 0;
          }
        } else if (xt == SPTT_FLT) {
          out_ref = sptir_emit(ir, SPTIR_FMATH, SPTT_FLT, xref, SPTIR_NULL,
                               (int64_t)(intptr_t)absfn);
          out_t = SPTT_FLT;
        } else {
          rc->aborted = 1; return 0;            /* non-numeric arg */
        }
        ir->reg_map[rc->frame_base + a] = out_ref;
        ir->reg_type[rc->frame_base + a] = out_t;
        if (a > ir->maxslot) ir->maxslot = a;
        /* If this result is itself a trailing arg (c == 0), record its slot so the
           enclosing CALL can confirm its arg count (abs feeding min/max/abs). */
        if (c == 0) rc->minmax_multiret_top = rc->frame_base + a;
        break;
      }

      /* Pending math.floor / math.ceil armed by a preceding SELF? Lower with no
         real call. Arg at R[A+2]. b == 3 (fn + receiver + 1), or b == 0 when the
         arg is a just-lowered nested min/max/abs (multiret; the lone value at
         minmax_multiret_top must be R[A+2]). c == 2 for one result, or c == 0
         when this result feeds an enclosing call. INT arg: floor/ceil is identity
         (an integer is its own floor/ceil) -> pass the ref through. FLOAT arg:
         SPTIR_TOINT (roundsd + range guard); out-of-range/NaN side-exits and the
         interpreter returns the float, exactly as Lua's pushnumint does. */
      if (rc->pending_floorceil_slot == rc->frame_base + a) {
        int mode = rc->pending_floorceil_mode;
        const Instruction *fc_self_pc = rc->pending_floorceil_self_pc;
        rc->pending_floorceil_slot = -1; rc->pending_floorceil_mode = 0;
        if (b != 3 && !(b == 0 && incoming_mm_top == rc->frame_base + a + 2)) { rc->aborted = 1; return 0; }
        if (c != 2 && c != 0) { rc->aborted = 1; return 0; }
        int xref = ir->reg_map[rc->frame_base + a + 2];
        if (xref < 0) { rc->aborted = 1; return 0; }
        SPTType xt = ir->reg_type[rc->frame_base + a + 2];
        int out_ref; SPTType out_t;
        if (xt == SPTT_INT) {
          out_ref = xref; out_t = SPTT_INT;          /* integer is its own floor/ceil */
        } else if (xt == SPTT_FLT) {
          /* Record-time range observation at root (cf. SLEN/SBYTE's short-string
             check): if the live argument already lies outside lua_Integer range,
             the guard would side-exit on every iteration, so abort and let the
             interpreter run the loop rather than compile an always-exiting trace
             (the abort blacklists the PC -> bounded). Only at frame_base==0, where
             the arg's live slot is addressable; an in-range value still compiles,
             and a later out-of-range value side-exits correctly via resume-at-SELF. */
          if (rc->frame_base == 0) {
            TValue *av = s2v((rc->ci->func.p + 1) + (a + 2));
            if (ttisfloat(av)) {
              double xv = fltvalue(av);
              if (!(xv >= -9223372036854775808.0 && xv < 9223372036854775808.0)) {
                rc->aborted = 1; return 0;
              }
            }
          }
          out_ref = sptir_emit(ir, SPTIR_TOINT, SPTT_INT, xref, SPTIR_NULL, (int64_t)mode);
          /* Resume the range guard at the SELF (not this CALL): the interpreter
             re-executes SELF+CALL and returns the float for out-of-range inputs,
             matching Lua's pushnumint. Idempotent: floor/ceil has no side effects. */
          int snap = sptir_snapshot(ir, fc_self_pc);
          ir->insts[out_ref].snap_idx = snap;
          ir->insts[out_ref].flags |= SPTIRF_GUARD;
          out_t = SPTT_INT;
        } else {
          rc->aborted = 1; return 0;                 /* non-numeric arg */
        }
        ir->reg_map[rc->frame_base + a] = out_ref;
        ir->reg_type[rc->frame_base + a] = out_t;
        if (a > ir->maxslot) ir->maxslot = a;
        if (c == 0) rc->minmax_multiret_top = rc->frame_base + a;
        break;
      }

      /* Pending user method armed by a preceding SELF (`obj.method`)? Inline the
         resolved method proto, with the receiver already staged in R[A+1] as
         callee slot 0 (this). Reuses the leaf-inline machinery below: same body
         purity gate, frame-base switch, and depth-1 restriction. The method
         identity is pinned by the once-per-entry C guard (method_recv_slot). */
      if (rc->pending_method_slot == rc->frame_base + a) {
        Proto *callee_p = rc->pending_method_proto;
        LClosure *callee_cl = (LClosure *)rc->pending_method_cl;
        rc->pending_method_slot = -1;
        rc->pending_method_proto = NULL;
        rc->pending_method_cl = NULL;
        /* c == 2: one result (pure-read / cond-return method). c == 1: zero
           results (void single-trailing-write method, e.g. a setter/accumulator
           called as a statement). Both are valid; the RETURN handler binds a
           result only for the value-returning case. */
        if ((c != 1 && c != 2) || b < 1 || rc->frame_base != 0) { rc->aborted = 1; return 0; }
        if ((b - 1) != callee_p->numparams) { rc->aborted = 1; return 0; }
        int ret_reg = -1;
        if (!proto_is_method_inlinable(callee_p, &ret_reg) &&
            !proto_is_condreturn_method_inlinable(callee_p) &&
            !proto_is_chained_condreturn(callee_p, 1) &&
            !proto_is_write_method_inlinable(callee_p) &&
            !proto_is_condwrite_method_inlinable(callee_p) &&
            !proto_is_condwrite_ifelse_method_inlinable(callee_p) &&
            !proto_is_multiwrite_method_inlinable(callee_p)) { rc->aborted = 1; return 0; }
        int new_fb = rc->frame_base + a + 1;
        if (new_fb + callee_p->maxstacksize >= 256) { rc->aborted = 1; return 0; }
        /* Arm the shared resume snapshot for guards inside the method body. Its
           exit PC is the SELF instruction (in the caller / main proto), so a
           guard failure re-executes SELF+CALL+method in the interpreter. Taken
           now, while still in the caller frame, so it captures the caller's live
           state (loop vars, accumulator, receiver source). */
        rc->method_resume_snap =
            (rc->method_self_pc != NULL) ? sptir_snapshot(ir, rc->method_self_pc) : -1;
        rc->save_p = rc->p;
        rc->save_k = rc->k;
        rc->save_cl = rc->cl;
        rc->save_pc = rc->pc + 1;
        rc->save_frame_base = rc->frame_base;
        rc->call_result_slot = rc->frame_base + a;
        rc->p = callee_p;
        rc->k = callee_p->k;
        rc->cl = callee_cl;
        rc->frame_base = new_fb;
        rc->fwd_base = -1;            /* fresh store-to-load forwarding per inlined method */
        rc->multiwrite_mode = proto_is_multiwrite_method_inlinable(callee_p);
        rc->pc = callee_p->code;
        return 1;                                 /* keep recording in the method */
      }

      /* The function value must be a plain SLOAD from a stable stack slot, i.e.
         a loop-invariant function. That tells us which proto to inline and lets
         us guard the function once at trace entry instead of every iteration. */
      int fref = ir->reg_map[rc->frame_base + a];
      if (fref < 0) { rc->aborted = 1; return 0; }
      SPTIRInst *fi = sptir_get(ir, fref);
      if (!fi || fi->op != SPTIR_SLOAD) { rc->aborted = 1; return 0; }
      int fslot = (int)fi->aux;
      TValue *fv = s2v(rc->ci->func.p + 1 + fslot);
      if (!ttisLclosure(fv)) { rc->aborted = 1; return 0; }
      LClosure *callee_cl = clLvalue(fv);
      Proto *callee_p = callee_cl->p;

      /* Argument count must match: B-1 passed values = receiver + numparams. */
      if ((b - 1) != callee_p->numparams) { rc->aborted = 1; return 0; }

      /* Callee must be a pure straight-line leaf, a conditional-return leaf
         `if(c){return A}return B`, or a conditional-assignment-then-return leaf
         `if(c){slot=A}return slot` (the latter two get their branch if-converted). */
      int ret_reg = -1;
      int straight = proto_is_inlinable(callee_p, &ret_reg);
      if (!straight && !proto_is_condreturn_inlinable(callee_p) &&
          !proto_is_chained_condreturn(callee_p, 0) &&
          !proto_is_condassign_inlinable(callee_p)) { rc->aborted = 1; return 0; }

      /* New frame base = A+1 (the slot after the function). Keep the callee's
         whole frame inside the fixed-size backing reg_map array. */
      int new_fb = rc->frame_base + a + 1;
      if (new_fb + callee_p->maxstacksize >= 256) { rc->aborted = 1; return 0; }

      /* One entry check per trace: every inlined call must share (slot, proto). */
      if (rc->inline_fn_slot < 0) {
        rc->inline_fn_slot = fslot;
        rc->inline_fn_proto = callee_p;
      } else if (rc->inline_fn_slot != fslot || rc->inline_fn_proto != callee_p) {
        rc->aborted = 1; return 0;
      }

      /* Save caller state and switch the recorder into the callee. The callee's
         argument slots already coincide with the caller's argument registers in
         reg_map (callee slot k == caller slot A+1+k), so nothing is copied. */
      rc->save_p = rc->p;
      rc->save_k = rc->k;
      rc->save_cl = rc->cl;
      rc->save_pc = rc->pc + 1;                  /* caller resumes after the CALL */
      rc->save_frame_base = rc->frame_base;
      rc->call_result_slot = rc->frame_base + a; /* caller R[A] takes the result */

      rc->p = callee_p;
      rc->k = callee_p->k;
      rc->cl = callee_cl;
      rc->frame_base = new_fb;
      rc->fwd_base = -1;            /* fresh store-to-load forwarding per inlined method */
      rc->multiwrite_mode = 0;      /* multi-write only applies to method inlining */
      rc->pc = callee_p->code;
      return 1;                                  /* keep recording in the callee */
    }


    case OP_TFORPREP: {
      /* Generic-for preheader. The outer loop's own TFORPREP is before
         start_pc and never recorded; reaching one here means an *inner*
         for-each nested in the trace we're recording. We don't unroll
         for-each, so abort and let the interpreter drive the inner loop. */
      rc->aborted = 1;
      return 0;
    }

    case OP_TFORCALL: {
      /* Generic-for iterator step, specialized ONLY for `for k[,v] : pairs(L)`
         over a List. luaH_next over an array-mode table yields keys
         0,1,...,loglen-1 with value = L[key] (see ltable.c luaH_next), so the
         whole loop is a native index loop -- no C call, no GC.

         SPT layout after TFORCALL (see lvm.c): iterator at ra+0, state at ra+1
         (both loop-invariant), the call results land at ra+3 (key, which also
         serves as the next iteration's control) and ra+4 (value). We model the
         step as: newkey = key + 1; GUARD newkey < #state (loop-continue);
         value = state[newkey]. The guard/value snapshots capture the PRE-update
         state at this PC, so when the guard fails (key was the last element) the
         exit re-runs the real TFORCALL -> next returns nil -> TFORLOOP ends the
         loop. The iterator identity (== luaB_next) is pinned with a once-per-
         entry C guard (forin_iter_slot). Top frame only. */
      int a = GETARG_A(i), c = GETARG_C(i);
      if (rc->frame_base != 0) { rc->aborted = 1; return 0; }
      /* next yields exactly (key,value); support 1 or 2 loop vars. >2 would bind
         nils we don't model. */
      if (c < 1 || c > 2) { rc->aborted = 1; return 0; }

      StkId base = rc->ci->func.p + 1;
      /* Iterator must be luaB_next (a light C function); pinned in C at entry. */
      TValue *fv = s2v(base + a);
      if (!ttislcf(fv) || fvalue(fv) != spt_jit_pairs_next()) { rc->aborted = 1; return 0; }
      /* State must be a List (array-mode table). */
      TValue *sv = s2v(base + a + 1);
      if (!ttisarray(sv)) { rc->aborted = 1; return 0; }
      /* SLOAD the state (loop-invariant) -> emits GUARD_T(ARR), hoisted and
         checked at entry via the live-in list. The iterator is NOT loaded into
         the IR (no use); its identity is the C entry guard below. */
      int sref = rec_load_reg(rc, a + 1);
      if (ir->reg_type[rc->frame_base + a + 1] != SPTT_ARR) { rc->aborted = 1; return 0; }
      /* Record the iterator entry guard. All for-each loops in one trace must
         share the same iterator slot. */
      if (rc->forin_iter_slot < 0) {
        rc->forin_iter_slot = a;
        rc->forin_iter_fn = fvalue(fv);
      } else if (rc->forin_iter_slot != a) {
        rc->aborted = 1; return 0;
      }

      /* Control variable (current key) at ra+3 -- must be an integer. */
      int keyref = rec_load_reg(rc, a + 3);
      if (ir->reg_type[rc->frame_base + a + 3] != SPTT_INT) { rc->aborted = 1; return 0; }

      /* newkey = key + 1 */
      int newkey = sptir_emit(ir, SPTIR_ADD, SPTT_INT, keyref, sptir_kint(ir, 1), 0);
      /* len = #state */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, sref, SPTIR_NULL, 0);
      /* Loop-continue guard: newkey < len. Snapshot = pre-update state at this
         PC; on failure the interpreter re-runs the real TFORCALL (next -> nil). */
      sptir_guard(ir, SPTIR_GUARD_LT, newkey, (int64_t)lenref, rec_guard_pc(rc));

      if (c >= 2) {
        /* value = state[newkey]. newkey >= 1 > 0 and < len (continue guard) so it
           is in bounds; SPTIR_GETI only type-guards and relies on the preceding
           bounds guard, exactly like OP_GETI/OP_GETTABLE. Predict the element
           type from L[newkey] (rec_array_elem_type clamps out-of-range); the
           runtime type guard, not this prediction, is load-bearing. */
        TValue *ktv = s2v(base + a + 3);
        lua_Integer kpred = ttisinteger(ktv) ? ivalue(ktv) + 1 : 0;
        SPTType et = rec_array_elem_type(rc, a + 1, kpred);
        if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR && et != SPTT_ARR && et != SPTT_TAB) {
          rc->aborted = 1; return 0;
        }
        int vref = sptir_emit(ir, SPTIR_GETI, et, sref, newkey, 0);
        int snap = rec_snap(rc);
        ir->insts[vref].snap_idx = snap;
        ir->insts[vref].flags |= SPTIRF_GUARD;
        ir->reg_map[rc->frame_base + a + 4] = vref;
        ir->reg_type[rc->frame_base + a + 4] = et;
        if (a + 4 > ir->maxslot) ir->maxslot = a + 4;
      }

      /* Commit the new key AFTER the snapshots above (so they carry the OLD
         key). This is the loop-carried induction variable. */
      ir->reg_map[rc->frame_base + a + 3] = newkey;
      ir->reg_type[rc->frame_base + a + 3] = SPTT_INT;
      if (a + 3 > ir->maxslot) ir->maxslot = a + 3;
      break;
    }

    case OP_TFORLOOP: {
      /* Back-edge of the generic-for. Mirrors OP_FORLOOP: only closes the trace
         when it targets our loop header; an inner loop we recorded through has a
         different target -> a side trace exits to the interpreter, a root trace
         aborts. The continue test was already emitted as the GUARD_LT in the
         preceding TFORCALL. */
      const Instruction *target = rc->pc + 1 - GETARG_Bx(i);
      if (target != rc->start_pc) {
        if (rc->is_side_trace) { sptir_exit(ir, rc->pc); return 0; }
        rc->aborted = 1;
        return 0;
      }
      sptir_loop(ir);
      return 0;
    }

    case OP_SETLIST:
    case OP_CLOSURE:
    case OP_VARARG:
    case OP_SELF: {
      /* R[A+1] = R[B]; R[A] = R[B][K[C]].  Specialized ONLY for the unary-libm
         math-method idiom (e.g. math.sqrt): R[B] is a library/global table read
         (a GETTABUP) and R[B][key] resolves to a known math C function. We don't
         materialize the function/receiver -- we arm a pending FMATH that the
         next CALL on R[A] consumes, and pin the method with a GUARD_CFUNC so
         reassigning it (or the library table) side-exits. Anything else aborts.

         The library table must be re-derived from the STABLE upvalue source, NOT
         the live R[B] register: in this idiom R[B] is overwritten by the call
         result within the loop body, so the live stack at record time holds a
         stale value. We recover _ENV (the upvalue) and the library key from the
         GETTABUP/ULOAD IR and look up _ENV[libkey][methodkey] directly. The run-
         time GUARD_CFUNC re-walks on the (dynamic) GETTABUP result, so the live
         lookup here is only a prediction. Top frame only. */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      if (rc->frame_base != 0) { rc->aborted = 1; return 0; }
      const TValue *kc = &rc->k[c];
      if (ttypetag(kc) != LUA_VSHRSTR) { rc->aborted = 1; return 0; }
      TString *key = tsvalue(kc);
      int bref = rec_load_reg(rc, b);
      if (ir->reg_type[rc->frame_base + b] != SPTT_TAB) { rc->aborted = 1; return 0; }
      SPTIRInst *gi = sptir_get(ir, bref);
      if (!gi || gi->op != SPTIR_GETTABUP) {
        /* Not a library/global table read -> try the user-method idiom:
           `obj.method()` on a class instance held in a stable (loop-invariant)
           slot. SPT classes are `setmetatable(inst, Class)` with
           `Class.__index = Class`, so the method lives directly on the
           receiver's metatable. Resolve it, stage the receiver into R[A+1]
           (callee slot 0 = `this`), and arm the next CALL to inline the method
           proto. The metatable + method identity is pinned by a once-per-entry C
           guard (method_recv_slot), so a different class / reassigned method
           declines. Requiring an SLOAD receiver also guarantees loop-invariance
           (a receiver reassigned in the body would not be a bare SLOAD). */
        if (gi && gi->op == SPTIR_SLOAD) {
          int recv_slot = (int)gi->aux;
          TValue *recv = s2v((rc->ci->func.p + 1) + b);
          if (!ttistable(recv)) { rc->aborted = 1; return 0; }
          Table *mt = hvalue(recv)->metatable;
          if (!mt) { rc->aborted = 1; return 0; }
          const TValue *mv = rec_table_getstr(mt, key);
          if (!mv || !ttisLclosure(mv)) { rc->aborted = 1; return 0; }
          LClosure *mcl = clLvalue(mv);
          rc->method_self_pc = rc->pc;          /* resume point for in-method guards */
          ir->reg_map[rc->frame_base + a + 1] = bref;        /* R[A+1] = receiver */
          ir->reg_type[rc->frame_base + a + 1] = SPTT_TAB;
          if (a + 1 > ir->maxslot) ir->maxslot = a + 1;
          rc->pending_method_slot = rc->frame_base + a;       /* next CALL inlines it */
          rc->pending_method_proto = mcl->p;
          rc->pending_method_cl = mcl;
          /* Register this method's identity for the once-per-entry guard.
             Several distinct methods may be inlined into one trace
             (`a.foo(); a.bar()`); each is re-validated independently at entry,
             and a repeat call to the same method reuses its existing entry. */
          int mfound = 0;
          for (int mi = 0; mi < rc->n_methods; mi++) {
            if (rc->methods[mi].recv_slot == recv_slot &&
                rc->methods[mi].class_mt == mt &&
                rc->methods[mi].proto == mcl->p) { mfound = 1; break; }
          }
          if (!mfound) {
            if (rc->n_methods >= SPT_JIT_MAX_METHODS) { rc->aborted = 1; return 0; }
            rc->methods[rc->n_methods].recv_slot = recv_slot;
            rc->methods[rc->n_methods].class_mt = mt;
            rc->methods[rc->n_methods].key = key;
            rc->methods[rc->n_methods].proto = mcl->p;
            rc->n_methods++;
          }
          break;
        }
        rc->aborted = 1; return 0;
      }
      SPTIRInst *uli = sptir_get(ir, gi->op1);
      if (!uli || uli->op != SPTIR_ULOAD) { rc->aborted = 1; return 0; }
      int uvidx = (int)uli->aux;
      TString *libkey = (TString *)(intptr_t)gi->aux;       /* e.g. "math" */
      LClosure *cl = clLvalue(s2v(rc->ci->func.p));
      if (uvidx >= cl->nupvalues) { rc->aborted = 1; return 0; }
      TValue *envtv = cl->upvals[uvidx]->v.p;
      if (!ttistable(envtv)) { rc->aborted = 1; return 0; }
      const TValue *libtv = rec_table_getstr(hvalue(envtv), libkey);
      if (!libtv || !ttistable(libtv)) { rc->aborted = 1; return 0; }
      const TValue *fv = rec_table_getstr(hvalue(libtv), key);
      if (!fv) { rc->aborted = 1; return 0; }
      /* math.* are registered by luaL_newlib (nup=0) as LIGHT C functions
         (LUA_VLCF), so the value is the lua_CFunction directly. */
      if (!ttislcf(fv)) { rc->aborted = 1; return 0; }
      spt_unary_libm_fn libm = spt_jit_unary_math(fvalue(fv));
      int strop = libm ? 0 : spt_jit_str_op(fvalue(fv));
      int mmop = (libm || strop) ? 0 : spt_jit_math_minmax(fvalue(fv));
      spt_unary_libm_fn absfn = (libm || strop || mmop) ? NULL : spt_jit_math_abs(fvalue(fv));
      int fcop = (libm || strop || mmop || absfn) ? 0 : spt_jit_math_floorceil(fvalue(fv));
      if (!libm && !strop && !mmop && !absfn && !fcop) { rc->aborted = 1; return 0; }
      int kref = sptir_kptr(ir, (void *)key);
      int gref = sptir_emit(ir, SPTIR_GUARD_CFUNC, SPTT_NIL, bref, kref,
                            (int64_t)(intptr_t)fvalue(fv));
      int snap = rec_snap(rc);
      ir->insts[gref].snap_idx = snap;
      ir->insts[gref].flags |= SPTIRF_GUARD;
      /* Open the arg-load window: until the CALL, guards resume at this SELF. */
      rc->call_arg_self_pc = rc->pc;
      if (libm) {
        rc->pending_cfn_libm = (void *)libm;
        rc->pending_cfn_slot = rc->frame_base + a;
      } else if (strop) {                     /* string.len (1) / string.byte (2) */
        rc->pending_str_op = strop;
        rc->pending_str_slot = rc->frame_base + a;
        rc->pending_str_self_pc = rc->pc;  /* resume point for the SLEN/SBYTE guard */
      } else if (mmop) {                      /* math.min (1) / math.max (2) */
        if (rc->pending_minmax_top >= 8) { rc->aborted = 1; return 0; }
        rc->pending_minmax_slot[rc->pending_minmax_top] = rc->frame_base + a;
        rc->pending_minmax_op[rc->pending_minmax_top] = mmop;
        rc->pending_minmax_top++;
      } else if (absfn) {                     /* math.abs */
        rc->pending_abs_slot = rc->frame_base + a;
        rc->pending_abs_fabs = (void *)absfn;
      } else {                                /* math.floor (1) / math.ceil (2) */
        rc->pending_floorceil_slot = rc->frame_base + a;
        rc->pending_floorceil_mode = fcop;
        rc->pending_floorceil_self_pc = rc->pc;  /* resume point for range guard */
      }
      break;
    }
    case OP_GETVARG:
    case OP_VARARGPREP:
    case OP_CONCAT:
    case OP_CLOSE:
    case OP_TBC:
    case OP_NEWTABLE:
    case OP_NEWLIST:
    case OP_ERRNNIL:
    case OP_SETTABUP:
    case OP_EXTRAARG:
    case OP_MMBIN:
    case OP_MMBINI:
    case OP_MMBINK:
      /* These instructions are either skipped (MMBIN) or abort the trace. */
      if (op == OP_MMBIN || op == OP_MMBINI || op == OP_MMBINK) {
        /* Already skipped by the arithmetic handler. If we reach here,
           it means the arithmetic wasn't handled. Skip it. */
        break;
      }
      rc->aborted = 1;
      return 0;

    default:
      rc->aborted = 1;
      return 0;
  }

  rc->pc++;
  return 1;
}

/* Record a trace starting from start_pc. */
static SPTTrace *record_trace(SPTJitState *js, lua_State *L, CallInfo *ci,
                               const Instruction *start_pc, int is_side) {
  LClosure *cl = clLvalue(s2v(ci->func.p));
  Proto *p = cl->p;

  SPTTrace *t = (SPTTrace *)calloc(1, sizeof(SPTTrace));
  if (!t) return NULL;
  sptir_init(&t->ir);

  SPTRecCtx rc;
  memset(&rc, 0, sizeof(rc));
  rc.js = js;
  rc.L = L;
  rc.ci = ci;
  rc.cl = cl;
  rc.p = p;
  rc.k = p->k;
  rc.start_pc = start_pc;
  rc.pc = start_pc;
  rc.ir = &t->ir;
  rc.frame_base = 0;
  rc.is_side_trace = is_side;
  rc.fwd_base = -1;            /* store-to-load forwarding: none pending */
  rc.multiwrite_mode = 0;
  rc.n_field_layouts = 0;
  rc.inline_fn_slot = -1;
  rc.inline_fn_proto = NULL;
  rc.pending_cfn_libm = NULL;
  rc.pending_cfn_slot = -1;
  rc.pending_str_slot = -1;
  rc.pending_str_op = 0;
  rc.pending_str_self_pc = NULL;
  rc.call_arg_self_pc = NULL;
  rc.pending_minmax_top = 0;
  rc.minmax_multiret_top = -1;
  rc.pending_abs_slot = -1;
  rc.pending_abs_fabs = NULL;
  rc.pending_floorceil_slot = -1;
  rc.pending_floorceil_mode = 0;
  rc.pending_floorceil_self_pc = NULL;
  rc.forin_iter_slot = -1;
  rc.forin_iter_fn = NULL;
  rc.pending_method_slot = -1;
  rc.pending_method_proto = NULL;
  rc.pending_method_cl = NULL;
  rc.n_methods = 0;
  rc.method_self_pc = NULL;
  rc.method_resume_snap = -1;
  rc.loop_end_snap = -1;

  /* Mark loop start in IR. */
  t->ir.loop_start = t->ir.ninst;

  /* Record instructions until we hit the loop back-edge or abort. */
  int max_steps = SPT_JIT_MAX_TRACE;
  while (rc.inst_count < max_steps) {
    if (!rec_inst(&rc))
      break;
  }

  if (rc.aborted) {
    js->stats.traces_aborted++;
    if (js->debug) {
      OpCode bad = GET_OPCODE(*rc.pc);
      fprintf(stderr,
              "[JIT] aborted trace: proto=%p start_pc_offset=%d at op=%d "
              "(pc_offset=%d) after %d insts\n",
              (void *)p, (int)(start_pc - p->code), (int)bad,
              (int)(rc.pc - p->code), rc.inst_count);
    }
    sptir_free(&t->ir);
    free(t);
    return NULL;
  }

  /* Copy exit PCs from IR builder to trace (used by codegen for exit stubs). */
  for (int i = 0; i < t->ir.nsnaps && i < SPT_JIT_MAX_SNAPSHOTS; i++)
    t->exit_pcs[i] = t->ir.exit_pcs[i];

  /* Carry the inlined-call entry check (if any) onto the trace. */
  t->inline_fn_slot = rc.inline_fn_slot;
  t->inline_fn_proto = rc.inline_fn_proto;

  /* Carry the for-each iterator entry check (if any) onto the trace. */
  t->forin_iter_slot = rc.forin_iter_slot;
  t->forin_iter_fn = rc.forin_iter_fn;
  t->n_methods = rc.n_methods;
  for (int mi = 0; mi < rc.n_methods; mi++) t->methods[mi] = rc.methods[mi];

  /* Carry the multi-write field-layout entry guards (if any). */
  t->n_field_layouts = rc.n_field_layouts;
  for (int fi = 0; fi < rc.n_field_layouts; fi++)
    t->field_layouts[fi] = rc.field_layouts[fi];

  /* Snapshot index of the loop-continuation guard (FORLOOP "count > 0").
     Used by the runtime guard-failure blacklist to exclude normal loop-end
     exits from the side-exit tally. */
  t->loop_end_snap = rc.loop_end_snap;

  if (js->debug >= 2) sptir_dump(&t->ir, "pre-opt");

  /* Optimize the IR. */
  sptir_optimize(&t->ir);

  if (js->debug >= 2) sptir_dump(&t->ir, "post-opt");

  /* Inlined-frame exit safety net. An exit stub sets ci->u.l.savedpc = exit_pc
     and returns to the interpreter, which resumes in the CALLER's CallInfo (the
     inline never pushed a callee frame). So a guard that survives optimization
     with an exit PC pointing INTO an inlined callee's bytecode (outside the main
     proto's [code, code+sizecode)) would resume callee bytecode under the caller
     frame -> corruption. For a loop-invariant receiver, method-field guards are
     loop-invariant and hoist/fold away, leaving only top-frame (loop-boundary)
     exits; but a method body with a per-iteration in-callee guard (e.g. variable
     index access) might not. Detect any surviving in-callee exit and refuse to
     compile -- the trace then falls back to the interpreter, exactly like the
     side-trace amortization gate below. (Free-function inlines have zero-guard
     bodies, so this only ever fires for risky method bodies; non-inlined traces
     keep all exit PCs in the main proto, so there are no false positives.) */
  {
    const Instruction *lo = p->code, *hi = p->code + p->sizecode;
    for (int k = 0; k < t->ir.ninst; k++) {
      SPTIRInst *gi = &t->ir.insts[k];
      if (!(gi->flags & SPTIRF_GUARD) || (gi->flags & SPTIRF_DEAD)) continue;
      int si = gi->snap_idx;
      if (si < 0 || si >= t->ir.nsnaps) continue;
      const Instruction *epc = t->ir.exit_pcs[si];
      if (epc && (epc < lo || epc >= hi)) {
        if (js->debug)
          fprintf(stderr, "[JIT] trace has an un-hoisted in-callee guard (exit "
                  "pc outside main proto); refusing to compile, fall back to "
                  "interpreter\n");
        sptir_free(&t->ir);
        free(t);
        return NULL;
      }
    }
  }

  /* Precompute the compact live-in type-check list from the final IR: each
     GUARD_T whose operand is an SLOAD pins a stack slot to an SPTType. Dedupe by
     slot (a slot may be guarded more than once). The entry-time recheck in
     sptjit_trace_enter iterates this short list instead of rescanning the whole
     IR on every entry. Overflow -> n_livein = -1 (entry does a full scan). */
  t->n_livein = 0;
  for (int k = 0; k < t->ir.ninst; k++) {
    SPTIRInst *gi = &t->ir.insts[k];
    if (gi->op != SPTIR_GUARD_T) continue;
    int sref = gi->op1;
    if (sref < 0 || sref >= t->ir.ninst) continue;
    if (t->ir.insts[sref].op != SPTIR_SLOAD) continue;
    int slot = (int)t->ir.insts[sref].aux;
    if (slot < 0 || slot > 255) continue;
    int dup = 0;
    for (int j = 0; j < t->n_livein; j++)
      if (t->livein_slot[j] == (uint8_t)slot) { dup = 1; break; }
    if (dup) continue;
    if (t->n_livein >= SPT_JIT_MAX_LIVEIN) { t->n_livein = -1; break; }
    t->livein_slot[t->n_livein] = (uint8_t)slot;
    t->livein_type[t->n_livein] = (uint8_t)(SPTType)gi->aux;
    t->n_livein++;
  }

  /* Side-trace amortization gate: discard a side trace whose body is too small to
     pay back the stack-mediated link overhead (see SPT_JIT_SIDE_MIN_IR). Done
     after optimize (so the count matches the post-opt IR size) but BEFORE codegen,
     so a rejected side trace never consumes code-pool space. Root traces are
     never gated. Returning NULL makes maybe_record_side_trace leave the arm to
     the interpreter -- exactly the pre-Phase-2 behavior, so no regression. */
  if (rc.is_side_trace && (int)t->ir.ninst < js->side_min_ir) {
    if (js->debug)
      fprintf(stderr, "[JIT] side trace too small (ir=%d < %d), discarded; "
              "arm stays in interpreter\n", t->ir.ninst, js->side_min_ir);
    sptir_free(&t->ir);
    free(t);
    return NULL;
  }

  /* Generate native code. */
  sptjit_codegen_compile(t, js);

  if (!t->code) {
    js->stats.traces_aborted++;
    sptir_free(&t->ir);
    free(t);
    return NULL;
  }

  t->proto = p;
  t->pc_offset = (int)(start_pc - p->code);
  js->stats.traces_recorded++;
  js->stats.traces_compiled++;

  if (js->debug) {
    fprintf(stderr,
            "[JIT] compiled trace: proto=%p pc_offset=%d  ir=%d insts, "
            "code=%zu bytes, %d exits\n",
            (void *)p, t->pc_offset, t->ir.ninst, t->code_size, t->nexits);
  }

  return t;
}

/* =====================================================================
** Trace entry and exit
** ===================================================================== */

/* The trace entry point type. */
typedef void (*SPTTraceEntry)(lua_State *L, CallInfo *ci);

/* Re-validate a trace's entry guards against the live interpreter state in `ci`.
   Returns 1 if the trace may be entered, 0 if a guard would fail (so the
   interpreter should run instead). Factored out so the link trampoline can apply
   the same checks to every trace it hands off to.

   (1) Inlined-call guard: a trace that inlined a pure leaf call may only be
       entered while the function slot still holds a closure of the inlined
       proto; otherwise the inlined body would be wrong.
   (2) Loop-carried live-in type guard: a trace pins the type of each live-in it
       reads (a GUARD_T on the SLOAD). When such a value's type changes mid-loop
       (e.g. an int accumulator that becomes a float on a side branch) the change
       persists, and the trace's hoisted entry guard would then fail on *every*
       subsequent entry -- re-validating in C and declining lets the interpreter
       advance the loop instead of livelocking. The compact (slot,type) list is
       precomputed at record time; n_livein == -1 means it overflowed, so fall
       back to a full IR scan (rare). */
static int trace_entry_guards_ok(SPTTrace *t, CallInfo *ci) {
  if (t->inline_fn_slot >= 0) {
    TValue *fv = s2v(ci->func.p + 1 + t->inline_fn_slot);
    if (!ttisLclosure(fv) || clLvalue(fv)->p != t->inline_fn_proto)
      return 0;
  }
  /* for-each list specialization: the iterator slot must still hold the exact
     C function (luaB_next) we specialized for. A custom iterator or a map
     iterator would not produce the contiguous 0-based key sequence the native
     loop assumes, so decline and let the interpreter run the generic-for. */
  if (t->forin_iter_slot >= 0) {
    TValue *fv = s2v(ci->func.p + 1 + t->forin_iter_slot);
    if (!ttislcf(fv) || fvalue(fv) != t->forin_iter_fn)
      return 0;
  }
  /* Method-call trace: every pinned method's receiver slot must still hold a
     table whose metatable is the pinned class, and that class must still resolve
     the method name to the same proto (re-resolved here, so reassigning the
     class method side-steps a stale inline). The receiver itself may be any
     instance of that class. A trace may pin several methods (`a.foo(); a.bar()`). */
  for (int mi = 0; mi < t->n_methods; mi++) {
    SPTMethodId *m = &t->methods[mi];
    TValue *recv = s2v(ci->func.p + 1 + m->recv_slot);
    if (!ttistable(recv)) return 0;
    Table *mt = hvalue(recv)->metatable;
    if (mt != (Table *)m->class_mt) return 0;
    const TValue *mv = rec_table_getstr(mt, (TString *)m->key);
    if (!mv || !ttisLclosure(mv) || clLvalue(mv)->p != m->proto) return 0;
  }
  /* Multi-write field-layout guards: each this.<field> accessed in the inlined
     body is verified at entry (key present in the receiver table + value type
     matches the recorded type). This lets the body emit guard-free GETFIELD/
     SETFIELD. The receiver is methods[0] (multi-write traces are single-
     method). */
  if (t->n_field_layouts > 0) {
    if (t->n_methods < 1) return 0;
    SPTMethodId *m = &t->methods[0];
    TValue *recv = s2v(ci->func.p + 1 + m->recv_slot);
    if (!ttistable(recv)) return 0;
    Table *tbl = hvalue(recv);
    for (int fi = 0; fi < t->n_field_layouts; fi++) {
      const TValue *v = rec_table_getstr(tbl, (TString *)t->field_layouts[fi].key);
      if (!v) return 0;                             /* key absent -> decline */
      if (rec_value_type(v) != (SPTType)t->field_layouts[fi].value_type)
        return 0;                                   /* type changed -> decline */
    }
  }
  if (t->n_livein >= 0) {
    for (int k = 0; k < t->n_livein; k++) {
      int slot = t->livein_slot[k];
      if (rec_value_type(s2v(ci->func.p + 1 + slot)) != (SPTType)t->livein_type[k])
        return 0;
    }
  } else {
    SPTIRBuilder *tir = &t->ir;
    for (int k = 0; k < tir->ninst; k++) {
      SPTIRInst *gi = &tir->insts[k];
      if (gi->op != SPTIR_GUARD_T) continue;
      int sref = gi->op1;
      if (sref < 0 || sref >= tir->ninst) continue;
      if (tir->insts[sref].op != SPTIR_SLOAD) continue;
      int slot = (int)tir->insts[sref].aux;
      if (rec_value_type(s2v(ci->func.p + 1 + slot)) != (SPTType)gi->aux)
        return 0;
    }
  }
  return 1;
}

/* Phase 2: when a parent trace's side exit at `exit_pc` has been taken often
   enough and no trace is compiled there yet, record a side trace rooted at
   exit_pc. The parent has just flushed full interpreter state to the stack, so
   the recorder reads exactly the post-exit state (frozen stack). A successful
   side trace is registered in the hot table at (proto, exit_pc); the link
   trampoline then hands off to it on subsequent exits, turning the otherwise-
   interpreted "wrong arm + rest of loop body" into native code.

   Called only on a trampoline miss (no trace at exit_pc yet). Once a side trace
   exists the trampoline enters it instead, so this stops firing for that PC.
   Abort accounting mirrors the root path: repeated failures blacklist the PC. */
static void maybe_record_side_trace(SPTJitState *js, lua_State *L, CallInfo *ci,
                                     SPTTrace *parent, const Instruction *exit_pc) {
  if (!parent || !exit_pc) return;

  LClosure *cl = clLvalue(s2v(ci->func.p));
  if (!cl || !cl->p) return;
  Proto *p = cl->p;
  int pc_offset = (int)(exit_pc - p->code);
  if (pc_offset < 0 || pc_offset >= p->sizecode) return;

  /* Don't root a side trace on a loop back-edge. A trace started at a FORLOOP /
     TFORLOOP / backward JMP would hit that back-edge as its first instruction --
     its target is before its own start, so it would close immediately with an
     exit at the same PC: a degenerate "enter, exit where you started" trace that
     the trampoline's progress check rejects anyway. The interpreter runs the
     back-edge (one bytecode) and re-enters the parent, which is already optimal. */
  OpCode bop = GET_OPCODE(*exit_pc);
  if (bop == OP_FORLOOP || bop == OP_TFORLOOP) return;
  if (bop == OP_JMP && GETARG_sJ(*exit_pc) < 0) return;

  /* Cheap rejections FIRST -- this runs on every trampoline miss, including the
     steady state of a PC we have already blacklisted (e.g. a too-small side trace
     that was discarded). Doing the hot_lookup + blacklist check before the
     snapshot scan keeps that steady-state path O(1), so a gated arm costs the same
     as the pre-Phase-2 fallback (no per-miss snapshot loop). */
  SPTHotEntry *e = hot_lookup(js, p, pc_offset);
  if (!e) return;
  if (e->trace && e->trace->code) return;        /* already have a side trace */
  if (e->aborts >= SPT_JIT_MAX_ABORTS) return;   /* gave up on this PC */

  /* Is this exit hot? Find a parent snapshot whose exit PC matches and whose
     taken-count (bumped by the exit stub) has crossed the side-trace threshold.
     Multiple snapshots may share a PC; any one being hot makes the PC hot. */
  int hot = 0;
  for (int s = 0; s < parent->ir.nsnaps && s < SPT_JIT_MAX_SNAPSHOTS; s++) {
    if (parent->exit_pcs[s] == exit_pc &&
        parent->exit_count[s] >= js->side_hot_threshold) { hot = 1; break; }
  }
  if (!hot) return;

  SPTTrace *st = record_trace(js, L, ci, exit_pc, /*is_side=*/1);
  if (st) {
    e->proto = p;
    e->pc_offset = pc_offset;
    e->trace = st;
    if (js->debug)
      fprintf(stderr, "[JIT] recorded side trace: proto=%p pc_offset=%d "
              "(parent exit hot)\n", (void *)p, pc_offset);
  } else if (e->aborts < 0xFFFF) {
    e->aborts++;
  }
}

int sptjit_trace_enter(lua_State *L, CallInfo *ci, const Instruction *pc) {
  global_State *g = G(L);
  SPTJitState *js = (SPTJitState *)g->jit_state; /* will be added */
  if (!js || js->mode == SPT_JIT_MODE_OFF) return 0;

  LClosure *cl = clLvalue(s2v(ci->func.p));
  if (!cl || !cl->p) return 0;
  Proto *p = cl->p;
  int pc_offset = (int)(pc - p->code);

  SPTHotEntry *e = hot_lookup(js, p, pc_offset);
  if (!e || !e->trace || !e->trace->code) return 0;
  if (!trace_entry_guards_ok(e->trace, ci)) return 0;

  /* Enter the trace, then follow stitched links (Phase 2). When a trace exits it
     has written every live stack slot back and set ci->u.l.savedpc to the resume
     PC -- the interpreter stack is fully consistent, exactly as if the
     interpreter were about to dispatch savedpc. So if a compiled trace exists at
     that PC and its entry guards pass, we re-enter it directly here instead of
     returning to the dispatch loop. Every hand-off goes through the exit stub's
     full stack flush and the next trace's SLOAD reload, so the stack is the
     single source of truth: no register-state matching between traces is needed
     (the lower-risk alternative to LuaJIT's register-mediated linking).

     Termination is guaranteed two ways: we only continue while the resume PC
     strictly differs from the PC we just entered at (so a trace that exits where
     it started -- or that left savedpc unchanged, e.g. a degenerate top-level
     RETURN -- breaks the chain rather than spinning), and SPT_JIT_MAX_LINK_HOPS
     is an absolute cap. On any miss we fall through to the interpreter exactly as
     the single-entry path did before. ci->func (hence the proto) is unchanged by
     a trace -- traces never perform a real call or return -- so `p` stays valid
     across hops; the PC bounds check defends against it anyway. */
  const Instruction *entered_pc = pc;
  SPTTrace *t = e->trace;
  for (int hops = 0; hops < SPT_JIT_MAX_LINK_HOPS; hops++) {
    js->stats.trace_entries++;
    t->entry_count++;
    SPTTraceEntry entry = (SPTTraceEntry)t->code;
    entry(L, ci);

    const Instruction *next_pc = ci->u.l.savedpc;
    if (next_pc == entered_pc) break;       /* no forward progress */
    int npc_off = (int)(next_pc - p->code);
    if (npc_off < 0 || npc_off >= p->sizecode) break;
    SPTHotEntry *ne = hot_lookup(js, p, npc_off);
    if (!ne || !ne->trace || !ne->trace->code) {
      /* No trace at this exit PC. If the exit is hot, record a side trace here;
         the trampoline picks it up on the next exit. Either way, stop and let
         the interpreter resume at next_pc this time. */
      maybe_record_side_trace(js, L, ci, t, next_pc);
      break;
    }
    if (!trace_entry_guards_ok(ne->trace, ci)) break;

    entered_pc = next_pc;
    t = ne->trace;
  }
  return 1;
}

int sptjit_trace_hot(lua_State *L, CallInfo *ci, const Instruction *pc) {
  global_State *g = G(L);
  SPTJitState *js = (SPTJitState *)g->jit_state;
  if (!js || js->mode == SPT_JIT_MODE_OFF) return 0;

  LClosure *cl = clLvalue(s2v(ci->func.p));
  if (!cl || !cl->p) return 0;
  Proto *p = cl->p;
  int pc_offset = (int)(pc - p->code);

  SPTHotEntry *e = hot_lookup(js, p, pc_offset);
  if (!e) return 0;

  /* If trace already exists, enter it. */
  if (e->trace && e->trace->code) {
    /* Runtime guard-failure blacklist: periodically check whether this trace's
       side exits (guard failures, excluding normal loop-end termination) have
       become excessive. A trace that fails a guard on every iteration is a net
       loss -- the prologue + exit-stub flush costs more than interpreting. The
       existing abort blacklist can't catch this (recording succeeded). We
       discard the trace and bump runtime_fails; after MAX_RUNTIME_FAILS
       discards the entry is fully blacklisted (aborts = MAX_ABORTS). */
    SPTTrace *t = e->trace;
    if (t->entry_count > 0 &&
        (t->entry_count & (SPT_JIT_BLACKLIST_CHECK_INTERVAL - 1)) == 0) {
      uint64_t side_exits = 0;
      int nsnaps = t->ir.nsnaps;
      if (nsnaps > SPT_JIT_MAX_SNAPSHOTS) nsnaps = SPT_JIT_MAX_SNAPSHOTS;
      for (int i = 0; i < nsnaps; i++) {
        if (i != t->loop_end_snap)
          side_exits += t->exit_count[i];
      }
      if (side_exits > SPT_JIT_BLACKLIST_SIDE_EXITS) {
        if (js->debug) {
          fprintf(stderr,
                  "[JIT] runtime blacklist: proto=%p pc_offset=%d "
                  "side_exits=%llu entry_count=%u runtime_fails=%d\n",
                  (void *)p, pc_offset,
                  (unsigned long long)side_exits, t->entry_count,
                  e->runtime_fails + 1);
        }
        sptir_free(&t->ir);
        free(t);
        e->trace = NULL;
        if (e->runtime_fails < 0xFFFF) e->runtime_fails++;
        if (e->runtime_fails >= SPT_JIT_MAX_RUNTIME_FAILS) {
          e->aborts = SPT_JIT_MAX_ABORTS;  /* full blacklist */
        } else {
          e->counter = e->aborts + e->runtime_fails;  /* phase-shift retry */
        }
        return 0;
      }
    }
    return sptjit_trace_enter(L, ci, pc);
  }

  /* Blacklisted: this PC has aborted recording too many times (e.g. a loop
     containing a call, generic-for, or other un-traceable op). Stop trying --
     repeatedly recording and discarding makes the JIT slower than the plain
     interpreter. */
  if (e->aborts >= SPT_JIT_MAX_ABORTS) return 0;

  /* Branch-direction profiling phase. Once a loop is hot we don't record
     immediately; we first sample which way each conditional branch goes for a
     short window, so the recorder can take the *majority* direction rather than
     whatever the single recording iteration happened to do. Recording the
     minority side of a biased branch makes the trace exit on the common path
     every iteration -- slower than the interpreter and dependent on which
     iteration tripped the threshold (a coin-flip). See §10.23. */
  if (sptjit_profiling_active) {
    int is_profiled = (g_prof.proto == p && g_prof.pc_start == pc_offset);
    if (g_prof.budget > 0) g_prof.budget--;
    if (is_profiled && ++g_prof.iters >= SPT_PROF_ITERS) {
      /* Enough samples: stop profiling and record using the majority tally. */
      sptjit_profiling_active = 0;
      e->counter = 0;
      SPTTrace *t = record_trace(js, L, ci, pc, /*is_side=*/0);
      if (t) { e->trace = t; return sptjit_trace_enter(L, ci, pc); }
      if (e->aborts < 0xFFFF) e->aborts++;
      e->counter = e->aborts;
      return 0;
    }
    if (g_prof.budget <= 0) {
      /* The profiled loop stalled (it stopped iterating before we gathered
         enough samples, e.g. it exited). Abandon so other loops aren't starved;
         fall through and handle the current loop normally. */
      sptjit_profiling_active = 0;
    } else {
      return 0;  /* keep sampling (this loop or let the profiled one finish) */
    }
  }

  /* Increment counter. */
  if (e->counter < 0xFFFF)
    e->counter++;
  e->proto = p;
  e->pc_offset = pc_offset;

  /* Hot enough? Begin profiling this loop's branches (we record after the
     sampling window completes, above). */
  if (e->counter >= js->hot_threshold) {
    if (!sptjit_profiling_active) {
      sptjit_profiling_active = 1;
      g_prof.proto = p;
      g_prof.pc_start = pc_offset;
      g_prof.pc_end = p->sizecode;
      g_prof.iters = 0;
      g_prof.budget = SPT_PROF_BUDGET;
      g_prof.n = 0;
    }
    /* else: another loop is mid-profile; this one waits and re-trips shortly. */
    return 0;
  }

  return 0;
}
