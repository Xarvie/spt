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
} SPTRecCtx;

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
    sptir_guard(ir, SPTIR_GUARD_T, ref, (int64_t)t, rc->pc);
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

/* Recover the record-time array (Table*) behind an IR ref: an SLOAD reads the
   stack slot; a nested GETI evaluates array[index] recursively (for chained
   access m[i][j], whose intermediate array's slot may be reused). NULL if
   undeterminable. */
static Table *rec_eval_array(SPTRecCtx *rc, int ref) {
  SPTIRInst *in = sptir_get(rc->ir, ref);
  if (!in) return NULL;
  if (in->op == SPTIR_SLOAD) {
    TValue *v = s2v((rc->ci->func.p + 1) + (int)in->aux);
    return ttisarray(v) ? avalue(v) : NULL;
  }
  if (in->op == SPTIR_GETI) {
    Table *arr = rec_eval_array(rc, in->op1);
    lua_Integer idx;
    if (!arr || !rec_eval_int(rc, in->op2, &idx)) return NULL;
    if (idx < 0 || (lua_Unsigned)idx >= arr->loglen) return NULL;
    lu_byte tag = *getArrTag(arr, (lua_Unsigned)idx);
    TValue tmp;
    tmp.tt_ = tag;
    tmp.value_ = *getArrVal(arr, (lua_Unsigned)idx);
    return ttisarray(&tmp) ? avalue(&tmp) : NULL;
  }
  return NULL;
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

/* Parse an if-conversion arm body [start,end): a straight-line run of
   non-trapping integer value-producing ops, each optionally followed by its
   MMBIN marker, and nothing else. Unions the distinct destination slots into
   wslots (which already holds *nw entries from a previously parsed arm).
   Rejects: a non-convertible op, a float-producing op, an operand that was
   written earlier in THIS arm (an intra-arm dependency would make the
   pre-recording type check unreliable), and more than IFCONV_MAX_SLOTS slots.
   Returns 1 on success (and only if the arm writes at least one slot). */
static int parse_ifconv_arm(SPTRecCtx *rc, const Instruction *start,
                            const Instruction *end, int *wslots, int *nw) {
  int local[16], nlocal = 0;          /* slots written so far within this arm */
  int wrote = 0, nops = 0;
  for (const Instruction *pc = start; pc < end; pc++) {
    OpCode o = GET_OPCODE(*pc);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (++nops > 16) return 0;
    if (!opcode_is_ifconv_safe(o)) return 0;
    if (!ifconv_arm_int_result(rc, pc)) return 0;
    int b = GETARG_B(*pc), c = GETARG_C(*pc);
    int checkB = (o != OP_LOADI);
    int checkC = (o == OP_ADD || o == OP_SUB || o == OP_MUL || o == OP_BAND ||
                  o == OP_BOR || o == OP_BXOR || o == OP_SHL || o == OP_SHR);
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
  return ttisinteger(s2v((rc->ci->func.p + 1) + reg));
}

/* Will this arm op produce an INTEGER result, given the current operand types?
   Checked before recording so a float-producing arm cleanly falls back to the
   guarded branch instead of being recorded and then aborted. */
static int ifconv_arm_int_result(SPTRecCtx *rc, const Instruction *op_pc) {
  Instruction ins = *op_pc;
  OpCode o = GET_OPCODE(ins);
  int b = GETARG_B(ins), c = GETARG_C(ins);
  int bint = ifconv_reg_is_int(rc, b);
  switch (o) {
    case OP_LOADI: return 1;
    case OP_MOVE: case OP_UNM: case OP_BNOT: case OP_ADDI:
      return bint;
    case OP_ADD: case OP_SUB: case OP_MUL:
    case OP_BAND: case OP_BOR: case OP_BXOR:
    case OP_SHL: case OP_SHR:
      return bint && ifconv_reg_is_int(rc, c);
    case OP_ADDK: case OP_SUBK: case OP_MULK:
    case OP_BANDK: case OP_BORK: case OP_BXORK:
      return bint && ttisinteger(&rc->p->k[c]);
    default: return 0;
  }
}

/* Try to if-convert the comparison at rc->pc. Returns 1 if it took the
   conversion path (caller returns 1 unless rc->aborted got set), 0 if not
   applicable (caller falls back to the guarded branch, leaving state untouched). */
static int rec_try_ifconv(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                          SPTType at, SPTType bt) {
  if (rc->frame_base != 0) return 0;              /* root frame only */
  if (at != SPTT_INT || bt != SPTT_INT) return 0; /* integer compare only */
  SPTIRBuilder *ir = rc->ir;
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

  /* Parse both arms, unioning the set of written slots. An arm that does not
     write a given slot leaves it unchanged (its select input is the old value). */
  int wslots[IFCONV_MAX_SLOTS]; int nw = 0;
  if (!parse_ifconv_arm(rc, then_start, then_end, wslots, &nw)) return 0;
  if (is_ifelse && !parse_ifconv_arm(rc, else_start, merge, wslots, &nw)) return 0;

  /* Every written slot must currently hold an integer: an if-only arm leaves the
     other branch as the slot's old value, and arms may read the slot (s=s+1). */
  for (int k = 0; k < nw; k++)
    if (!ifconv_reg_is_int(rc, wslots[k])) return 0;

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
    if (ir->reg_type[wslots[k]] != SPTT_INT) { rc->aborted = 1; return 1; }
    then_ref[k] = ir->reg_map[wslots[k]];
  }
  for (int k = 0; k < nw; k++) {
    ir->reg_map[wslots[k]] = old_ref[k]; ir->reg_type[wslots[k]] = SPTT_INT;
  }

  if (is_ifelse) {                                  /* record the else-arm */
    rc->pc = else_start;
    while (rc->pc < merge) { if (!rec_inst(rc)) return 1; }
    if (rc->pc != merge) { rc->aborted = 1; return 1; }
    for (int k = 0; k < nw; k++) {
      if (ir->reg_type[wslots[k]] != SPTT_INT) { rc->aborted = 1; return 1; }
      else_ref[k] = ir->reg_map[wslots[k]];
    }
    for (int k = 0; k < nw; k++) {
      ir->reg_map[wslots[k]] = old_ref[k]; ir->reg_type[wslots[k]] = SPTT_INT;
    }
  } else {
    for (int k = 0; k < nw; k++) else_ref[k] = old_ref[k];
  }

  /* select_k = else_k + (then_k - else_k) * cond,  cond in {0,1} (one shared cmp) */
  int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
  for (int k = 0; k < nw; k++) {
    int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, then_ref[k], else_ref[k], 0);
    int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
    int res  = sptir_emit(ir, SPTIR_ADD, SPTT_INT, else_ref[k], prod, 0);
    ir->reg_map[wslots[k]] = res;
    ir->reg_type[wslots[k]] = SPTT_INT;
    if (wslots[k] > ir->maxslot) ir->maxslot = wslots[k];
  }
  rc->pc = merge;
  return 1;
}

/* When recording an inlined callee whose entire body is a conditional return
   `if(c){return A} return B`, if-convert it: record both return values against a
   forked callee frame and bind the caller's result slot to a branchless select
   (B + (A-B)*c), then resume in the caller -- exactly like an OP_RETURN1 but with
   no side exit. Integer returns only (the static gate proto_is_condreturn_inlinable
   already restricts the body to non-trapping integer ops). Returns 1 if it took
   this path (caller returns 1 unless rc->aborted), 0 if not applicable (the caller
   falls back to the guarded branch, which records one return path -- also correct). */
static int rec_try_condreturn_ifconv(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                                     SPTType at, SPTType bt) {
  if (rc->frame_base == 0) return 0;              /* only inside an inlined callee */
  if (at != SPTT_INT || bt != SPTT_INT) return 0; /* integer compare only */
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
    OpCode o = GET_OPCODE(*q);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe(o)) return 0;
  }
  const Instruction *else_ret = NULL;             /* else-arm: first return */
  for (const Instruction *q = T1; q < cend; q++) {
    OpCode o = GET_OPCODE(*q);
    if (o == OP_RETURN1) { else_ret = q; break; }
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe(o)) return 0;
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
  if (then_ref < 0 || ir->reg_type[fb + then_reg] != SPTT_INT) { rc->aborted = 1; return 1; }
  for (int k = 0; k < cmax; k++) { ir->reg_map[fb+k] = save_map[k]; ir->reg_type[fb+k] = save_type[k]; }

  /* ---- record the else-arm's value computation */
  rc->pc = T1;
  while (rc->pc < else_ret) { if (!rec_inst(rc)) return 1; }
  if (rc->pc != else_ret) { rc->aborted = 1; return 1; }
  int else_ref = ir->reg_map[fb + else_reg];
  if (else_ref < 0 || ir->reg_type[fb + else_reg] != SPTT_INT) { rc->aborted = 1; return 1; }
  for (int k = 0; k < cmax; k++) { ir->reg_map[fb+k] = save_map[k]; ir->reg_type[fb+k] = save_type[k]; }

  /* ---- result = else + (then-else)*cond, bind to caller, resume in caller */
  int cref = sptir_emit(ir, SPTIR_CMPSET, SPTT_INT, aref, bref, (int64_t)fop);
  int diff = sptir_emit(ir, SPTIR_SUB, SPTT_INT, then_ref, else_ref, 0);
  int prod = sptir_emit(ir, SPTIR_MUL, SPTT_INT, diff, cref, 0);
  int res  = sptir_emit(ir, SPTIR_ADD, SPTT_INT, else_ref, prod, 0);
  ir->reg_map[rc->call_result_slot] = res;
  ir->reg_type[rc->call_result_slot] = SPTT_INT;
  if (rc->call_result_slot > ir->maxslot) ir->maxslot = rc->call_result_slot;
  rc->p = rc->save_p;
  rc->k = rc->save_k;
  rc->cl = rc->save_cl;
  rc->frame_base = rc->save_frame_base;
  rc->pc = rc->save_pc;
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
  if (rec_try_condreturn_ifconv(rc, fop, aref, bref, at, bt)) return rc->aborted ? 0 : 1;
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
     majority only yields a suboptimal trace -- never an incorrect result. */
  if (g_prof.proto == rc->p && g_prof.n > 0) {
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
    int snap = sptir_snapshot(ir, rc->pc);
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
    int snap = sptir_snapshot(ir, rc->pc);
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

static int op_is_comparison(OpCode o) {
  return o == OP_EQ || o == OP_LT || o == OP_LE || o == OP_EQK ||
         o == OP_EQI || o == OP_LTI || o == OP_LEI || o == OP_GTI || o == OP_GEI;
}

/* Decide whether `p` is an inlinable conditional-return leaf of the exact shape
   `if (c) { return A } return B`: a straight-line prefix that computes the
   compare operands, a single comparison + JMP, a then-arm that computes A and
   ends in RETURN1, an else-arm that computes B and ends in RETURN1, and only
   dead return boilerplate afterwards. Every non-control op (prefix and both
   arms) must be a non-trapping integer if-conversion op -- this both guarantees
   the two returned values are integers (so the select arithmetic is bit-exact)
   and forbids a second branch, a call, a loop, or any side effect. Such a callee
   is inlined and its conditional return if-converted into a branchless select. */
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
    if (!opcode_is_ifconv_safe(o)) return 0;
  }
  for (int i = ci + 2; i < t1 - 1; i++) {        /* then-arm compute */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe(o)) return 0;
  }
  int e = -1;
  for (int i = t1; i < n; i++) {                 /* else-arm: first RETURN1 */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o == OP_RETURN1) { e = i; break; }
    if (o == OP_MMBIN || o == OP_MMBINI || o == OP_MMBINK) continue;
    if (!opcode_is_ifconv_safe(o)) return 0;
  }
  if (e < 0) return 0;
  for (int i = e + 1; i < n; i++) {              /* only dead boilerplate after */
    OpCode o = GET_OPCODE(p->code[i]);
    if (o != OP_RETURN0 && o != OP_RETURN) return 0;
  }
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
      /* UpValue[B][K[C]:shortstring] */
      int upref = sptir_emit(ir, SPTIR_ULOAD, SPTT_ANY, SPTIR_NULL, SPTIR_NULL, b);
      int ref = sptir_emit(ir, SPTIR_GETTABUP, SPTT_ANY, upref, SPTIR_NULL,
                           (int64_t)tsvalue(&rc->k[GETARG_C(i)]));
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = SPTT_ANY;
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
      if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR && et != SPTT_ARR) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= c < loglen (c is a constant here). */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LT, sptir_kint(ir, c), (int64_t)lenref, rc->pc);
      /* Type-guarded load. */
      int ref = sptir_emit(ir, SPTIR_GETI, et, bref, sptir_kint(ir, c), 0);
      int snap = sptir_snapshot(ir, rc->pc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_GETFIELD: {
      /* Map field reads: separate (unverified) path; abort for now. */
      rc->aborted = 1;
      return 0;
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
      if (et != SPTT_INT && et != SPTT_FLT && et != SPTT_STR && et != SPTT_ARR) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= idx < loglen. */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LE, sptir_kint(ir, 0), (int64_t)cref, rc->pc);
      sptir_guard(ir, SPTIR_GUARD_LT, cref, (int64_t)lenref, rc->pc);
      /* Type-guarded load. */
      int ref = sptir_emit(ir, SPTIR_GETI, et, bref, cref, 0);
      int snap = sptir_snapshot(ir, rc->pc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[rc->frame_base + a] = ref;
      ir->reg_type[rc->frame_base + a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_SETI: {
      /* R[A][B] = RK(C), B is a constant index. */
      int a = GETARG_A(i), b = GETARG_B(i);
      int aref = rec_load_reg(rc, a);
      if (ir->reg_type[rc->frame_base + a] != SPTT_ARR) { rc->aborted = 1; return 0; }
      int cref = rec_load_rkc(rc, i);
      SPTType vt = sptir_type(ir, cref);
      if (vt != SPTT_INT && vt != SPTT_FLT) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= b < loglen (in-bounds write only; growth -> exit). */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, aref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LT, sptir_kint(ir, b), (int64_t)lenref, rc->pc);
      sptir_emit(ir, SPTIR_SETI, SPTT_NIL, aref, sptir_kint(ir, b), (int64_t)cref);
      break;
    }
    case OP_SETTABLE: {
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
      sptir_guard(ir, SPTIR_GUARD_LE, sptir_kint(ir, 0), (int64_t)bref, rc->pc);
      sptir_guard(ir, SPTIR_GUARD_LT, bref, (int64_t)lenref, rc->pc);
      sptir_emit(ir, SPTIR_SETI, SPTT_NIL, aref, bref, (int64_t)cref);
      break;
    }
    case OP_SETFIELD: {
      /* Map field writes: separate (unverified) path; abort for now. */
      rc->aborted = 1;
      return 0;
    }

    /* ---- Arithmetic ---- */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV:
    case OP_MOD: case OP_IDIV: case OP_POW: {
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
        sptir_guard(ir, SPTIR_GUARD_ULT, cref, 64, rc->pc);
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
      sptir_guard(ir, SPTIR_GUARD_ULT, bref, 64, rc->pc);
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
         FORPREP jumps to FORLOOP, so we skip it in the trace. */
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
        rc->aborted = 1;
        return 0;
      }

      int count_ref = rec_load_reg(rc, a);       /* R[A] = count */
      int step_ref = rec_load_reg(rc, a + 1);    /* R[A+1] = step */
      int idx_ref = rec_load_reg(rc, a + 2);     /* R[A+2] = idx */

      /* Guard: count > 0 (continue loop). Use GUARD_LT with 0 < count. */
      int zero_ref = sptir_kint(ir, 0);
      sptir_guard(ir, SPTIR_GUARD_LT, zero_ref, (int64_t)count_ref, rc->pc);

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
      if (rc->frame_base != 0) { rc->aborted = 1; return 0; } /* inlined callee must RETURN1 */
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
        return 1;
      }
      int aref = rec_load_reg(rc, a);
      sptir_emit(ir, SPTIR_RETURN, ir->reg_type[rc->frame_base + a], aref, SPTIR_NULL, 0);
      return 0;
    }
    case OP_RETURN: {
      int a = GETARG_A(i), b = GETARG_B(i);
      if (rc->frame_base != 0) { rc->aborted = 1; return 0; } /* inlined callee must RETURN1 */
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
      /* CALL A B C : R[A](R[A+1..A+B-1]) -> R[A..A+C-2].
         SPT uses a slot-0 receiver, so R[A+1] is the (nil) receiver and
         R[A+2..] are the actual arguments. */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);

      /* Restrict to: a single fixed result (C==2), a fixed, known argument
         count (B!=0), and depth-1 (not already inside an inlined callee). */
      if (c != 2 || b < 1 || rc->frame_base != 0) { rc->aborted = 1; return 0; }

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

      /* Callee must be a pure straight-line leaf, or a conditional-return leaf
         `if(c){return A}return B` (whose return is if-converted to a select). */
      int ret_reg = -1;
      int straight = proto_is_inlinable(callee_p, &ret_reg);
      if (!straight && !proto_is_condreturn_inlinable(callee_p)) { rc->aborted = 1; return 0; }

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
      rc->pc = callee_p->code;
      return 1;                                  /* keep recording in the callee */
    }


    case OP_TFORPREP:
    case OP_TFORCALL:
    case OP_TFORLOOP:
    case OP_SETLIST:
    case OP_CLOSURE:
    case OP_VARARG:
    case OP_GETVARG:
    case OP_VARARGPREP:
    case OP_CONCAT:
    case OP_CLOSE:
    case OP_TBC:
    case OP_NEWTABLE:
    case OP_NEWLIST:
    case OP_SELF:
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
                               const Instruction *start_pc) {
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
  rc.inline_fn_slot = -1;
  rc.inline_fn_proto = NULL;

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

  if (js->debug >= 2) sptir_dump(&t->ir, "pre-opt");

  /* Optimize the IR. */
  sptir_optimize(&t->ir);

  if (js->debug >= 2) sptir_dump(&t->ir, "post-opt");

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

  /* Inlined-call entry guard: a trace that inlined a pure leaf call may only be
     entered while the function slot still holds a closure of the inlined proto.
     If something else is there now, the inlined body would be wrong, so decline
     and let the interpreter run the loop (and the call) normally. */
  if (e->trace->inline_fn_slot >= 0) {
    TValue *fv = s2v(ci->func.p + 1 + e->trace->inline_fn_slot);
    if (!ttisLclosure(fv) || clLvalue(fv)->p != e->trace->inline_fn_proto)
      return 0;
  }

  /* Loop-carried live-in type guard. A trace pins the type of each live-in it
     reads (a GUARD_T on the SLOAD). When a value's type changes mid-loop -- e.g.
     an integer accumulator that becomes a float on a side branch -- that change
     persists, and the trace's hoisted entry guard would then fail on *every*
     subsequent entry. Re-validating in C and declining on a mismatch lets the
     interpreter run (and advance) the loop instead of livelocking on a guard
     that never lets the trace make progress.

     The check iterates the compact (slot,type) list precomputed at record time
     rather than rescanning the whole IR each entry; this matters on hot loops
     re-entered millions of times. n_livein == -1 means the list overflowed, so
     fall back to a full IR scan (rare). */
  {
    SPTTrace *t = e->trace;
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
  }

  /* Enter the trace. */
  js->stats.trace_entries++;
  SPTTraceEntry entry = (SPTTraceEntry)e->trace->code;
  entry(L, ci);
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
      SPTTrace *t = record_trace(js, L, ci, pc);
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
