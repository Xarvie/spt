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
    fprintf(stderr,
            "[JIT] stats: recorded=%llu compiled=%llu aborted=%llu "
            "entries=%llu exits=%llu guard_fail=%llu\n",
            (unsigned long long)js->stats.traces_recorded,
            (unsigned long long)js->stats.traces_compiled,
            (unsigned long long)js->stats.traces_aborted,
            (unsigned long long)js->stats.trace_entries,
            (unsigned long long)js->stats.trace_exits,
            (unsigned long long)js->stats.trace_guard_fail);
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
  if (ir->reg_map[reg] >= 0)
    return ir->reg_map[reg];

  /* Load from stack: emit SLOAD with type from actual value. */
  StkId base = rc->ci->func.p + 1;
  TValue *v = s2v(base + reg);
  SPTType t = rec_value_type(v);

  /* Emit a stack load. */
  int ref = sptir_emit(ir, SPTIR_SLOAD, t, SPTIR_NULL, SPTIR_NULL, reg);
  ir->reg_map[reg] = ref;
  ir->reg_type[reg] = t;

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
static SPTType rec_array_elem_type(SPTRecCtx *rc, int areg, lua_Integer idx) {
  StkId base = rc->ci->func.p + 1;
  TValue *arrtv = s2v(base + areg);
  if (!ttisarray(arrtv)) return SPTT_ANY;
  Table *t = avalue(arrtv);
  if (idx < 0 || (lua_Unsigned)idx >= t->loglen) return SPTT_ANY;
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

/* Evaluate a numeric comparison at record time to learn which way the branch
   actually went, so the trace records the taken path (not the static
   fall-through). */
static int rec_eval_num_cmp(SPTIROp op, const TValue *x, const TValue *y) {
  double a = rec_num_as_double(x), b = rec_num_as_double(y);
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

/* Record a comparison + its trailing conditional JMP, following the branch the
   program actually takes at record time. 'fop' is the condition under which the
   bytecode falls through to the next instruction (the THEN block). If at record
   time that condition holds we keep the fall-through and guard 'fop'; otherwise
   the JMP is taken, so we guard the negation and continue at the JMP target.
   This makes biased branches stay on their hot path instead of side-exiting
   every iteration. Returns 1 to continue recording, 0 to abort. */
static int rec_cond_branch(SPTRecCtx *rc, SPTIROp fop, int aref, int bref,
                           SPTType at, SPTType bt,
                           const TValue *x, const TValue *y) {
  if (!sptt_isnum(at) || !sptt_isnum(bt)) { rc->aborted = 1; return 0; }
  const Instruction *jmp = rc->pc + 1; /* the JMP following the comparison */
  if (rec_eval_num_cmp(fop, x, y)) {
    /* Fall-through path taken: guard the fall-through condition. */
    rec_compare(rc, fop, aref, bref, at, bt);
    if (rc->aborted) return 0;
    rc->pc = jmp + 1;                  /* skip comparison + JMP */
  } else {
    /* Branch (JMP) taken: guard the negation, continue at the JMP target. */
    rec_compare(rc, rec_negate_cmp(fop), aref, bref, at, bt);
    if (rc->aborted) return 0;
    rc->pc = jmp + 1 + GETARG_sJ(*jmp); /* follow the JMP */
  }
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
      ir->reg_map[a] = bref;
      ir->reg_type[a] = ir->reg_type[b];
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADI: {
      int a = GETARG_A(i);
      int ref = sptir_kint(ir, GETARG_sBx(i));
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADF: {
      int a = GETARG_A(i);
      int ref = sptir_kflt(ir, (double)GETARG_sBx(i));
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_FLT;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADK: {
      int a = GETARG_A(i);
      int ref = rec_load_k(rc, GETARG_Bx(i));
      ir->reg_map[a] = ref;
      ir->reg_type[a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADKX: {
      int a = GETARG_A(i);
      rc->pc++; /* skip EXTRAARG */
      int ref = rec_load_k(rc, GETARG_Ax(*rc->pc));
      ir->reg_map[a] = ref;
      ir->reg_type[a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADFALSE: {
      int a = GETARG_A(i);
      ir->reg_map[a] = sptir_emit(ir, SPTIR_FALSE, SPTT_FALSE, SPTIR_NULL, SPTIR_NULL, 0);
      ir->reg_type[a] = SPTT_FALSE;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADTRUE: {
      int a = GETARG_A(i);
      ir->reg_map[a] = sptir_emit(ir, SPTIR_TRUE, SPTT_TRUE, SPTIR_NULL, SPTIR_NULL, 0);
      ir->reg_type[a] = SPTT_TRUE;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LFALSESKIP: {
      int a = GETARG_A(i);
      ir->reg_map[a] = sptir_emit(ir, SPTIR_FALSE, SPTT_FALSE, SPTIR_NULL, SPTIR_NULL, 0);
      ir->reg_type[a] = SPTT_FALSE;
      rc->pc++; /* skip next */
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LOADNIL: {
      int a = GETARG_A(i), b = GETARG_B(i);
      do {
        ir->reg_map[a] = sptir_emit(ir, SPTIR_NIL, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
        ir->reg_type[a] = SPTT_NIL;
        if (a > ir->maxslot) ir->maxslot = a;
        a++;
      } while (b--);
      break;
    }

    /* ---- Upvalues ---- */
    case OP_GETUPVAL: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int ref = sptir_emit(ir, SPTIR_ULOAD, SPTT_ANY, SPTIR_NULL, SPTIR_NULL, b);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_ANY;
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
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_ANY;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_GETI: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      SPTType bt = ir->reg_type[b];
      if (bt != SPTT_ARR) { rc->aborted = 1; return 0; }
      /* Element type from the live array; only trace numeric elements. */
      SPTType et = rec_array_elem_type(rc, b, c);
      if (et != SPTT_INT && et != SPTT_FLT) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= c < loglen (c is a constant here). */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LT, sptir_kint(ir, c), (int64_t)lenref, rc->pc);
      /* Type-guarded load. */
      int ref = sptir_emit(ir, SPTIR_GETI, et, bref, sptir_kint(ir, c), 0);
      int snap = sptir_snapshot(ir, rc->pc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[a] = ref;
      ir->reg_type[a] = et;
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
      SPTType bt = ir->reg_type[b];
      SPTType ct = ir->reg_type[c];
      if (bt != SPTT_ARR || ct != SPTT_INT) { rc->aborted = 1; return 0; }
      /* Live index value -> element type; only trace numeric elements. */
      StkId base = rc->ci->func.p + 1;
      TValue *idxtv = s2v(base + c);
      if (!ttisinteger(idxtv)) { rc->aborted = 1; return 0; }
      SPTType et = rec_array_elem_type(rc, b, ivalue(idxtv));
      if (et != SPTT_INT && et != SPTT_FLT) { rc->aborted = 1; return 0; }
      /* Bounds guard: 0 <= idx < loglen. */
      int lenref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      sptir_guard(ir, SPTIR_GUARD_LE, sptir_kint(ir, 0), (int64_t)cref, rc->pc);
      sptir_guard(ir, SPTIR_GUARD_LT, cref, (int64_t)lenref, rc->pc);
      /* Type-guarded load. */
      int ref = sptir_emit(ir, SPTIR_GETI, et, bref, cref, 0);
      int snap = sptir_snapshot(ir, rc->pc);
      ir->insts[ref].snap_idx = snap;
      ir->insts[ref].flags |= SPTIRF_GUARD;
      ir->reg_map[a] = ref;
      ir->reg_type[a] = et;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_SETI: {
      /* R[A][B] = RK(C), B is a constant index. */
      int a = GETARG_A(i), b = GETARG_B(i);
      int aref = rec_load_reg(rc, a);
      if (ir->reg_type[a] != SPTT_ARR) { rc->aborted = 1; return 0; }
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
      if (ir->reg_type[a] != SPTT_ARR || ir->reg_type[b] != SPTT_INT) {
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
      SPTType bt = ir->reg_type[b], ct = ir->reg_type[c];
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
      ir->reg_map[a] = ref;
      ir->reg_type[a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* skip MMBIN if present */
      break;
    }
    case OP_ADDK: case OP_SUBK: case OP_MULK: case OP_DIVK:
    case OP_MODK: case OP_IDIVK: case OP_POWK: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_k(rc, c);
      SPTType bt = ir->reg_type[b], ct = sptir_type(ir, cref);
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
      ir->reg_map[a] = ref;
      ir->reg_type[a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_ADDI: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_sC(i);
      int bref = rec_load_reg(rc, b);
      int cref = sptir_kint(ir, c);
      SPTType bt = ir->reg_type[b];
      int ref = rec_arith(rc, SPTIR_ADD, bref, cref, bt, SPTT_INT);
      if (rc->aborted) return 0;
      ir->reg_map[a] = ref;
      ir->reg_type[a] = sptir_type(ir, ref);
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }

    /* ---- Bitwise ---- */
    case OP_BAND: case OP_BOR: case OP_BXOR: case OP_SHL: case OP_SHR: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_reg(rc, c);
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
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_BANDK: case OP_BORK: case OP_BXORK: {
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_C(i);
      int bref = rec_load_reg(rc, b);
      int cref = rec_load_k(rc, c);
      SPTIROp irop;
      switch (op) {
        case OP_BANDK: irop = SPTIR_BAND; break;
        case OP_BORK:  irop = SPTIR_BOR;  break;
        case OP_BXORK: irop = SPTIR_BXOR; break;
        default: irop = SPTIR_BAND;
      }
      int ref = sptir_emit(ir, irop, SPTT_INT, bref, cref, 0);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
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
      /* Guard amount in [0, 63] (unsigned < 64). */
      sptir_guard(ir, SPTIR_GUARD_ULT, bref, 64, rc->pc);
      int cref = sptir_kint(ir, c);
      int ref = sptir_emit(ir, SPTIR_SHL, SPTT_INT, cref, bref, 0);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }
    case OP_SHRI: {
      /* R[A] = luaV_shiftl(R[B], -sC): R[B] shifted by the compile-time
         amount (-sC). Direction depends on the sign of -sC. */
      int a = GETARG_A(i), b = GETARG_B(i), c = GETARG_sC(i);
      int bref = rec_load_reg(rc, b);
      int ref = rec_shift_const(rc, bref, -(int64_t)c);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc);
      break;
    }

    /* ---- Unary ---- */
    case OP_UNM: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      SPTType bt = ir->reg_type[b];
      int ref = sptir_emit(ir, SPTIR_NEG, bt, bref, SPTIR_NULL, 0);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = bt;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* UNM has no MMBIN; no-op */
      break;
    }
    case OP_BNOT: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      int ref = sptir_emit(ir, SPTIR_BNOT, SPTT_INT, bref, SPTIR_NULL, 0);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* BNOT has no MMBIN; no-op */
      break;
    }
    case OP_NOT: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      int ref = sptir_emit(ir, SPTIR_NOT, SPTT_TRUE, bref, SPTIR_NULL, 0);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_TRUE;
      if (a > ir->maxslot) ir->maxslot = a;
      break;
    }
    case OP_LEN: {
      int a = GETARG_A(i), b = GETARG_B(i);
      int bref = rec_load_reg(rc, b);
      int ref = sptir_emit(ir, SPTIR_LEN, SPTT_INT, bref, SPTIR_NULL, 0);
      ir->reg_map[a] = ref;
      ir->reg_type[a] = SPTT_INT;
      if (a > ir->maxslot) ir->maxslot = a;
      rec_skip_mmbin(rc); /* LEN has no MMBIN; no-op */
      break;
    }

    /* ---- Comparisons ---- */
    case OP_EQ: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      SPTType at = ir->reg_type[a], bt = ir->reg_type[b];
      StkId base = rc->ci->func.p + 1;
      return rec_cond_branch(rc, k ? SPTIR_NE : SPTIR_EQ, aref, bref, at, bt,
                             s2v(base + a), s2v(base + b));
    }
    case OP_LT: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      SPTType at = ir->reg_type[a], bt = ir->reg_type[b];
      StkId base = rc->ci->func.p + 1;
      return rec_cond_branch(rc, k ? SPTIR_GE : SPTIR_LT, aref, bref, at, bt,
                             s2v(base + a), s2v(base + b));
    }
    case OP_LE: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_reg(rc, b);
      SPTType at = ir->reg_type[a], bt = ir->reg_type[b];
      StkId base = rc->ci->func.p + 1;
      return rec_cond_branch(rc, k ? SPTIR_GT : SPTIR_LE, aref, bref, at, bt,
                             s2v(base + a), s2v(base + b));
    }
    case OP_EQI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      StkId base = rc->ci->func.p + 1;
      TValue bv; setivalue(&bv, b);
      return rec_cond_branch(rc, k ? SPTIR_NE : SPTIR_EQ, aref, bref,
                             ir->reg_type[a], SPTT_INT, s2v(base + a), &bv);
    }
    case OP_LTI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      StkId base = rc->ci->func.p + 1;
      TValue bv; setivalue(&bv, b);
      return rec_cond_branch(rc, k ? SPTIR_GE : SPTIR_LT, aref, bref,
                             ir->reg_type[a], SPTT_INT, s2v(base + a), &bv);
    }
    case OP_LEI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      StkId base = rc->ci->func.p + 1;
      TValue bv; setivalue(&bv, b);
      return rec_cond_branch(rc, k ? SPTIR_GT : SPTIR_LE, aref, bref,
                             ir->reg_type[a], SPTT_INT, s2v(base + a), &bv);
    }
    case OP_GTI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      StkId base = rc->ci->func.p + 1;
      TValue bv; setivalue(&bv, b);
      return rec_cond_branch(rc, k ? SPTIR_LE : SPTIR_GT, aref, bref,
                             ir->reg_type[a], SPTT_INT, s2v(base + a), &bv);
    }
    case OP_GEI: {
      int a = GETARG_A(i), b = GETARG_sB(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = sptir_kint(ir, b);
      StkId base = rc->ci->func.p + 1;
      TValue bv; setivalue(&bv, b);
      return rec_cond_branch(rc, k ? SPTIR_LT : SPTIR_GE, aref, bref,
                             ir->reg_type[a], SPTT_INT, s2v(base + a), &bv);
    }
    case OP_EQK: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      int bref = rec_load_k(rc, b);
      StkId base = rc->ci->func.p + 1;
      return rec_cond_branch(rc, k ? SPTIR_NE : SPTIR_EQ, aref, bref,
                             ir->reg_type[a], sptir_type(ir, bref),
                             s2v(base + a), &rc->k[b]);
    }

    /* ---- Tests ---- */
    case OP_TEST: {
      int a = GETARG_A(i), k = GETARG_k(i);
      int aref = rec_load_reg(rc, a);
      /* Guard: R[A] is truthy/falsy. */
      sptir_guard(ir, SPTIR_GUARD, aref, k, rc->pc);
      rc->pc++; /* skip JMP */
      break;
    }
    case OP_TESTSET: {
      int a = GETARG_A(i), b = GETARG_B(i), k = GETARG_k(i);
      int bref = rec_load_reg(rc, b);
      sptir_guard(ir, SPTIR_GUARD, bref, k, rc->pc);
      ir->reg_map[a] = bref;
      ir->reg_type[a] = ir->reg_type[b];
      if (a > ir->maxslot) ir->maxslot = a;
      rc->pc++; /* skip JMP */
      break;
    }

    /* ---- Jumps ---- */
    case OP_JMP: {
      int sj = GETARG_sJ(i);
      if (sj < 0) {
        /* Backward jump: loop back-edge. This is where we stop. */
        sptir_loop(ir);
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
      ir->reg_map[a] = new_count;
      ir->reg_type[a] = SPTT_INT;

      /* new_idx = idx + step */
      int new_idx = sptir_emit(ir, SPTIR_ADD, SPTT_INT, idx_ref, step_ref, 0);
      ir->reg_map[a + 2] = new_idx;
      ir->reg_type[a + 2] = SPTT_INT;

      if (a > ir->maxslot) ir->maxslot = a;
      if (a + 2 > ir->maxslot) ir->maxslot = a + 2;

      /* This is the loop back-edge. */
      sptir_loop(ir);
      return 0;
    }

    /* ---- Returns ---- */
    case OP_RETURN0: {
      sptir_emit(ir, SPTIR_RETURN, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
      return 0;
    }
    case OP_RETURN1: {
      int a = GETARG_A(i);
      int aref = rec_load_reg(rc, a);
      sptir_emit(ir, SPTIR_RETURN, ir->reg_type[a], aref, SPTIR_NULL, 0);
      return 0;
    }
    case OP_RETURN: {
      int a = GETARG_A(i), b = GETARG_B(i);
      if (b == 1) {
        sptir_emit(ir, SPTIR_RETURN, SPTT_NIL, SPTIR_NULL, SPTIR_NULL, 0);
      } else {
        int aref = rec_load_reg(rc, a);
        sptir_emit(ir, SPTIR_RETURN, ir->reg_type[a], aref, SPTIR_NULL, 0);
      }
      return 0;
    }

    /* ---- Unsupported: abort trace ---- */
    case OP_CALL:
    case OP_TAILCALL:
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

  if (js->debug >= 2) sptir_dump(&t->ir, "pre-opt");

  /* Optimize the IR. */
  sptir_optimize(&t->ir);

  if (js->debug >= 2) sptir_dump(&t->ir, "post-opt");

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

  /* Increment counter. */
  if (e->counter < 0xFFFF)
    e->counter++;
  e->proto = p;
  e->pc_offset = pc_offset;

  /* Hot enough? Record a trace. */
  if (e->counter >= js->hot_threshold) {
    e->counter = 0;
    SPTTrace *t = record_trace(js, L, ci, pc);
    if (t) {
      e->trace = t;
      /* Enter the trace immediately. */
      return sptjit_trace_enter(L, ci, pc);
    }
    /* Recording aborted: penalize this PC so we eventually stop retrying. */
    if (e->aborts < 0xFFFF) e->aborts++;
  }

  return 0;
}
