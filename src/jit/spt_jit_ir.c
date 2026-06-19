/*
** spt_jit_ir.c — SSA IR Builder and Optimizer for the SPT Trace JIT
**
** Builds SSA IR from bytecode during trace recording, then optimizes:
**   - Constant folding
**   - Copy propagation
**   - Common subexpression elimination (CSE)
**   - Dead code elimination (DCE)
*/
#include "spt_jit_ir.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =====================================================================
** IR Builder
** ===================================================================== */

void sptir_init(SPTIRBuilder *b) {
  memset(b, 0, sizeof(*b));
  b->cap = 256;
  b->insts = (SPTIRInst *)calloc(b->cap, sizeof(SPTIRInst));
  b->snap_cap = 16;
  b->snaps = (SPTSnapshot **)calloc(b->snap_cap, sizeof(SPTSnapshot *));
  sptir_reset(b);
}

void sptir_free(SPTIRBuilder *b) {
  free(b->insts);
  for (int i = 0; i < b->nsnaps; i++)
    free(b->snaps[i]);
  free(b->snaps);
  memset(b, 0, sizeof(*b));
}

void sptir_reset(SPTIRBuilder *b) {
  b->ninst = 0;
  b->nsnaps = 0;
  b->maxslot = 0;
  b->loop_start = -1;
  b->loop_end = -1;
  b->have_loop = 0;
  for (int i = 0; i < 256; i++) {
    b->reg_map[i] = SPTIR_NULL;
    b->reg_type[i] = SPTT_NIL;
    b->loop_reg_map[i] = SPTIR_NULL;
  }
  /* Emit a dummy instruction at index 0 so that ref 0 is never used.
     This lets us use 0 as "invalid" alongside SPTIR_NULL (-1). */
  if (b->cap > 0) {
    memset(&b->insts[0], 0, sizeof(SPTIRInst));
    b->insts[0].op = SPTIR_NOP;
    b->insts[0].type = SPTT_DEAD;
    b->ninst = 1;
  }
}

/* Ensure capacity for at least n more instructions. */
static void sptir_ensure(SPTIRBuilder *b, int n) {
  if (b->ninst + n <= b->cap) return;
  int newcap = b->cap;
  while (newcap < b->ninst + n) newcap *= 2;
  if (newcap > SPT_JIT_MAX_IR) newcap = SPT_JIT_MAX_IR;
  SPTIRInst *ni = (SPTIRInst *)realloc(b->insts, newcap * sizeof(SPTIRInst));
  if (!ni) return; /* out of memory: caller checks */
  b->insts = ni;
  b->cap = newcap;
}

int sptir_emit(SPTIRBuilder *b, SPTIROp op, SPTType type, int32_t op1, int32_t op2, int64_t aux) {
  sptir_ensure(b, 1);
  if (b->ninst >= b->cap) return SPTIR_NULL; /* OOM */
  int ref = b->ninst++;
  SPTIRInst *ir = &b->insts[ref];
  ir->op = op;
  ir->type = type;
  ir->flags = 0;
  ir->op1 = op1;
  ir->op2 = op2;
  ir->aux = aux;
  ir->prev = SPTIR_NULL;
  ir->snap_idx = -1;
  return ref;
}

int sptir_kint(SPTIRBuilder *b, int64_t val) {
  return sptir_emit(b, SPTIR_KINT, SPTT_INT, SPTIR_NULL, SPTIR_NULL, val);
}

int sptir_kflt(SPTIRBuilder *b, double val) {
  int64_t aux;
  memcpy(&aux, &val, 8);
  return sptir_emit(b, SPTIR_KFLT, SPTT_FLT, SPTIR_NULL, SPTIR_NULL, aux);
}

int sptir_kstr(SPTIRBuilder *b, void *str) {
  return sptir_emit(b, SPTIR_KSTR, SPTT_STR, SPTIR_NULL, SPTIR_NULL, (int64_t)str);
}

int sptir_kptr(SPTIRBuilder *b, void *ptr) {
  return sptir_emit(b, SPTIR_KPTR, SPTT_ANY, SPTIR_NULL, SPTIR_NULL, (int64_t)ptr);
}

SPTType sptir_type(const SPTIRBuilder *b, int ref) {
  if (ref < 0 || ref >= b->ninst) return SPTT_ANY;
  return b->insts[ref].type;
}

/* =====================================================================
** Snapshots
** ===================================================================== */

int sptir_snapshot(SPTIRBuilder *b, const Instruction *exit_pc) {
  if (b->nsnaps >= SPT_JIT_MAX_SNAPSHOTS) return -1;
  if (b->nsnaps >= b->snap_cap) {
    b->snap_cap *= 2;
    b->snaps = (SPTSnapshot **)realloc(b->snaps, b->snap_cap * sizeof(SPTSnapshot *));
  }
  int nslots = b->maxslot + 1;
  if (nslots > 255) nslots = 255;
  SPTSnapshot *s = (SPTSnapshot *)malloc(sizeof(SPTSnapshot) + nslots * sizeof(int32_t));
  s->exit_id = b->nsnaps;
  s->pc_offset = 0; /* unused; exit_pc stored separately */
  s->nslots = nslots;
  for (int i = 0; i < nslots; i++)
    s->slot_map[i] = b->reg_map[i];
  /* Store the exit PC for this snapshot. */
  b->exit_pcs[b->nsnaps] = exit_pc;
  b->snaps[b->nsnaps++] = s;
  /* Mark the previous instruction as having a snapshot. */
  if (b->ninst > 0)
    b->insts[b->ninst - 1].flags |= SPTIRF_SNAP;
  return s->exit_id;
}

int sptir_guard(SPTIRBuilder *b, SPTIROp gop, int32_t val_ref, int64_t aux,
                const Instruction *exit_pc) {
  int ref = sptir_emit(b, gop, SPTT_NIL, val_ref, SPTIR_NULL, aux);
  if (ref >= 0) {
    b->insts[ref].flags |= SPTIRF_GUARD;
    int snap = sptir_snapshot(b, exit_pc);
    b->insts[ref].snap_idx = snap;
  }
  return ref;
}

/* =====================================================================
** Loop handling: insert phi nodes at loop header
** ===================================================================== */

void sptir_loop(SPTIRBuilder *b) {
  b->have_loop = 1;
  b->loop_end = b->ninst;
  /* Emit LOOP marker. The codegen will emit a jump back to loop_start. */
  sptir_emit(b, SPTIR_LOOP, SPTT_NIL, b->loop_start, SPTIR_NULL, 0);
}

/* =====================================================================
** IR Optimization
** ===================================================================== */

/* Check if an instruction is a constant. */
static int ir_is_const(const SPTIRInst *ir) {
  switch (ir->op) {
    case SPTIR_NIL: case SPTIR_FALSE: case SPTIR_TRUE:
    case SPTIR_KINT: case SPTIR_KFLT: case SPTIR_KSTR:
    case SPTIR_KPTR: case SPTIR_KGC:
      return 1;
    default:
      return 0;
  }
}

/* Get constant int64 value from a constant instruction. */
static int64_t ir_const_int(const SPTIRInst *ir) {
  return ir->aux;
}

/* Get constant double value from a KFLT instruction. */
static double ir_const_flt(const SPTIRInst *ir) {
  double d;
  memcpy(&d, &ir->aux, 8);
  return d;
}

/* Constant folding for binary arithmetic. */
static int try_const_fold(SPTIRBuilder *b, int idx) {
  SPTIRInst *ir = &b->insts[idx];
  /* Never fold a guard: its side effect (the side-exit + snapshot) must be
     preserved, and removing it would desync exit indices in codegen. */
  if (ir->flags & SPTIRF_GUARD) return 0;
  if (ir->op1 < 0 || ir->op2 < 0) return 0;
  SPTIRInst *a = &b->insts[ir->op1];
  SPTIRInst *c = &b->insts[ir->op2];

  /* Both integer constants: fold integer ops. */
  if (a->op == SPTIR_KINT && c->op == SPTIR_KINT) {
    int64_t x = a->aux, y = c->aux, r = 0;
    int ok = 1;
    switch (ir->op) {
      case SPTIR_ADD:  r = x + y; break;
      case SPTIR_SUB:  r = x - y; break;
      case SPTIR_MUL:  r = x * y; break;
      case SPTIR_IDIV: if (y == 0) ok = 0; else r = x / y; break;
      case SPTIR_MOD:  if (y == 0) ok = 0; else r = x % y; break;
      case SPTIR_BAND: r = x & y; break;
      case SPTIR_BOR:  r = x | y; break;
      case SPTIR_BXOR: r = x ^ y; break;
      case SPTIR_SHL:  r = x << (y & 63); break;
      case SPTIR_SHR:  r = x >> (y & 63); break;
      case SPTIR_EQ:   ir->op = (x == y) ? SPTIR_TRUE : SPTIR_FALSE; ir->type = SPTT_TRUE; ir->op1 = ir->op2 = SPTIR_NULL; return 1;
      case SPTIR_LT:   ir->op = (x < y) ? SPTIR_TRUE : SPTIR_FALSE; ir->type = SPTT_TRUE; ir->op1 = ir->op2 = SPTIR_NULL; return 1;
      case SPTIR_LE:   ir->op = (x <= y) ? SPTIR_TRUE : SPTIR_FALSE; ir->type = SPTT_TRUE; ir->op1 = ir->op2 = SPTIR_NULL; return 1;
      default: ok = 0;
    }
    if (ok) {
      ir->op = SPTIR_KINT;
      ir->type = SPTT_INT;
      ir->aux = r;
      ir->op1 = ir->op2 = SPTIR_NULL;
      return 1;
    }
  }

  /* Both float constants: fold float ops. */
  if (a->op == SPTIR_KFLT && c->op == SPTIR_KFLT) {
    double x = ir_const_flt(a), y = ir_const_flt(c), r = 0;
    int ok = 1;
    switch (ir->op) {
      case SPTIR_ADD: r = x + y; break;
      case SPTIR_SUB: r = x - y; break;
      case SPTIR_MUL: r = x * y; break;
      case SPTIR_DIV: if (y == 0.0) ok = 0; else r = x / y; break;
      case SPTIR_POW: r = 1; { int64_t p = (int64_t)y; if (p == y && p >= 0) { for (int i = 0; i < p; i++) r *= x; } else ok = 0; } break;
      default: ok = 0;
    }
    if (ok) {
      int64_t aux;
      memcpy(&aux, &r, 8);
      ir->op = SPTIR_KFLT;
      ir->type = SPTT_FLT;
      ir->aux = aux;
      ir->op1 = ir->op2 = SPTIR_NULL;
      return 1;
    }
  }

  /* Mixed int/float: promote to float. */
  if (sptt_isnum(a->type) && sptt_isnum(c->type) &&
      (a->op == SPTIR_KINT || a->op == SPTIR_KFLT) &&
      (c->op == SPTIR_KINT || c->op == SPTIR_KFLT)) {
    double x = (a->op == SPTIR_KINT) ? (double)a->aux : ir_const_flt(a);
    double y = (c->op == SPTIR_KINT) ? (double)c->aux : ir_const_flt(c);
    double r = 0;
    int ok = 1;
    switch (ir->op) {
      case SPTIR_ADD: r = x + y; break;
      case SPTIR_SUB: r = x - y; break;
      case SPTIR_MUL: r = x * y; break;
      case SPTIR_DIV: r = x / y; break;
      default: ok = 0;
    }
    if (ok) {
      int64_t aux;
      memcpy(&aux, &r, 8);
      ir->op = SPTIR_KFLT;
      ir->type = SPTT_FLT;
      ir->aux = aux;
      ir->op1 = ir->op2 = SPTIR_NULL;
      return 1;
    }
  }

  return 0;
}

/* Algebraic simplification. */
static int try_algebraic(SPTIRBuilder *b, int idx) {
  SPTIRInst *ir = &b->insts[idx];
  if (ir->op1 < 0) return 0;
  SPTIRInst *a = &b->insts[ir->op1];

  /* x + 0 = x, x - 0 = x */
  if ((ir->op == SPTIR_ADD || ir->op == SPTIR_SUB) && ir->op2 >= 0) {
    SPTIRInst *c = &b->insts[ir->op2];
    if (c->op == SPTIR_KINT && c->aux == 0) {
      ir->op = SPTIR_NOP;
      ir->op2 = SPTIR_NULL;
      return 1;
    }
  }
  /* x * 1 = x */
  if (ir->op == SPTIR_MUL && ir->op2 >= 0) {
    SPTIRInst *c = &b->insts[ir->op2];
    if (c->op == SPTIR_KINT && c->aux == 1) {
      ir->op = SPTIR_NOP;
      ir->op2 = SPTIR_NULL;
      return 1;
    }
  }
  /* x * 0 = 0 */
  if (ir->op == SPTIR_MUL && ir->op2 >= 0) {
    SPTIRInst *c = &b->insts[ir->op2];
    if (c->op == SPTIR_KINT && c->aux == 0) {
      ir->op = SPTIR_KINT;
      ir->type = SPTT_INT;
      ir->aux = 0;
      ir->op1 = ir->op2 = SPTIR_NULL;
      return 1;
    }
  }
  return 0;
}

/* CSE: find identical instruction earlier in the IR. */
static int try_cse(SPTIRBuilder *b, int idx) {
  SPTIRInst *ir = &b->insts[idx];
  /* Only CSE pure arithmetic on constants/loads. */
  switch (ir->op) {
    case SPTIR_ADD: case SPTIR_SUB: case SPTIR_MUL:
    case SPTIR_BAND: case SPTIR_BOR: case SPTIR_BXOR:
    case SPTIR_SHL: case SPTIR_SHR:
      break;
    default:
      return 0;
  }
  /* Search backwards for identical instruction (limited window). */
  int start = idx > 64 ? idx - 64 : 0;
  for (int i = idx - 1; i >= start; i--) {
    SPTIRInst *o = &b->insts[i];
    if (o->op == ir->op && o->type == ir->type &&
        o->op1 == ir->op1 && o->op2 == ir->op2 &&
        !(o->flags & SPTIRF_DEAD)) {
      /* Found identical: turn this into a NOP-alias whose op1 points at the
         earlier identical instruction (the CSE result). op1 must be reset to i
         -- keeping the original first operand would alias to an operand value
         (e.g. `a` of `a*b`) instead of the computed result. */
      ir->op = SPTIR_NOP;
      ir->op1 = i;
      ir->op2 = SPTIR_NULL;
      return 1;
    }
  }
  return 0;
}

/* Mark instruction as used (for DCE). */
static void mark_used(SPTIRBuilder *b, int ref, uint8_t *used) {
  if (ref < 0 || ref >= b->ninst) return;
  if (used[ref]) return;
  used[ref] = 1;
  SPTIRInst *ir = &b->insts[ref];
  mark_used(b, ir->op1, used);
  mark_used(b, ir->op2, used);
  /* GUARD_LT/GUARD_LE keep their bound (e.g. an array LEN) in aux as an IR ref,
     not in op2. Follow it, or the bound is dead-code-eliminated and the guard
     reads an uninitialised spill slot at run time. */
  if (ir->op == SPTIR_GUARD_LT || ir->op == SPTIR_GUARD_LE)
    mark_used(b, (int)ir->aux, used);
}

/* Dead code elimination. */
static void dce(SPTIRBuilder *b) {
  uint8_t *used = (uint8_t *)calloc(b->ninst, 1);
  if (!used) return;

  /* Roots: guards, exits, loops, returns, stores, snapshots. */
  for (int i = 0; i < b->ninst; i++) {
    SPTIRInst *ir = &b->insts[i];
    if (ir->flags & (SPTIRF_GUARD | SPTIRF_SNAP)) {
      used[i] = 1;
      mark_used(b, ir->op1, used);
      mark_used(b, ir->op2, used);
      /* bounds-guard bound lives in aux as a ref (see mark_used). */
      if (ir->op == SPTIR_GUARD_LT || ir->op == SPTIR_GUARD_LE)
        mark_used(b, (int)ir->aux, used);
    }
    switch (ir->op) {
      case SPTIR_SSTORE: case SPTIR_USTORE: case SPTIR_SETI:
      case SPTIR_SETFIELD: case SPTIR_RETURN: case SPTIR_LOOP:
      case SPTIR_EXIT: case SPTIR_CALL:
        used[i] = 1;
        mark_used(b, ir->op1, used);
        mark_used(b, ir->op2, used);
        break;
      default: break;
    }
  }

  /* The LOOP back-edge writes all live registers (from reg_map) to the
     interpreter stack. Mark all reg_map entries as used. */
  if (b->have_loop) {
    for (int i = 0; i <= b->maxslot; i++) {
      mark_used(b, b->reg_map[i], used);
    }
  }

  /* Snapshots reference register values for exit restoration. Mark all
     snapshot slot_map entries as used. */
  for (int s = 0; s < b->nsnaps; s++) {
    SPTSnapshot *snap = b->snaps[s];
    if (!snap) continue;
    for (int i = 0; i < snap->nslots; i++) {
      mark_used(b, snap->slot_map[i], used);
    }
  }

  /* Mark dead instructions. */
  for (int i = 0; i < b->ninst; i++) {
    if (!used[i] && b->insts[i].op != SPTIR_NOP) {
      b->insts[i].flags |= SPTIRF_DEAD;
    }
  }
  free(used);
}

static const char *iropname(int op) {
  switch (op) {
    case SPTIR_NIL: return "NIL"; case SPTIR_FALSE: return "FALSE";
    case SPTIR_TRUE: return "TRUE"; case SPTIR_KINT: return "KINT";
    case SPTIR_KFLT: return "KFLT"; case SPTIR_KSTR: return "KSTR";
    case SPTIR_KPTR: return "KPTR"; case SPTIR_KGC: return "KGC";
    case SPTIR_SLOAD: return "SLOAD"; case SPTIR_SSTORE: return "SSTORE";
    case SPTIR_ULOAD: return "ULOAD"; case SPTIR_USTORE: return "USTORE";
    case SPTIR_ADD: return "ADD"; case SPTIR_SUB: return "SUB";
    case SPTIR_MUL: return "MUL"; case SPTIR_DIV: return "DIV";
    case SPTIR_MOD: return "MOD"; case SPTIR_IDIV: return "IDIV";
    case SPTIR_POW: return "POW"; case SPTIR_NEG: return "NEG";
    case SPTIR_BAND: return "BAND"; case SPTIR_BOR: return "BOR";
    case SPTIR_BXOR: return "BXOR"; case SPTIR_BNOT: return "BNOT";
    case SPTIR_SHL: return "SHL"; case SPTIR_SHR: return "SHR";
    case SPTIR_EQ: return "EQ"; case SPTIR_LT: return "LT";
    case SPTIR_LE: return "LE"; case SPTIR_GT: return "GT";
    case SPTIR_GE: return "GE"; case SPTIR_NOT: return "NOT";
    case SPTIR_GETI: return "GETI"; case SPTIR_SETI: return "SETI";
    case SPTIR_GETFIELD: return "GETFIELD"; case SPTIR_SETFIELD: return "SETFIELD";
    case SPTIR_GETTABUP: return "GETTABUP"; case SPTIR_LEN: return "LEN";
    case SPTIR_TOFLT: return "TOFLT"; case SPTIR_TOINT: return "TOINT";
    case SPTIR_GUARD: return "GUARD"; case SPTIR_GUARD_LT: return "GUARD_LT";
    case SPTIR_GUARD_LE: return "GUARD_LE"; case SPTIR_GUARD_EQ: return "GUARD_EQ";
    case SPTIR_GUARD_T: return "GUARD_T"; case SPTIR_EXIT: return "EXIT";
    case SPTIR_LOOP: return "LOOP"; case SPTIR_PHI: return "PHI";
    case SPTIR_CALL: return "CALL"; case SPTIR_RETURN: return "RETURN";
    case SPTIR_NOP: return "NOP"; default: return "???";
  }
}

static const char *irtypename(int t) {
  switch (t) {
    case SPTT_NIL: return "nil"; case SPTT_FALSE: return "fls";
    case SPTT_TRUE: return "tru"; case SPTT_INT: return "int";
    case SPTT_FLT: return "flt"; case SPTT_STR: return "str";
    case SPTT_ARR: return "arr"; case SPTT_TAB: return "tab";
    case SPTT_FUNC: return "fun"; case SPTT_UD: return "ud ";
    case SPTT_ANY: return "any"; case SPTT_DEAD: return "DED";
    default: return "?? ";
  }
}

void sptir_dump(const SPTIRBuilder *b, const char *title) {
  fprintf(stderr, "---- IR dump: %s (%d insts, maxslot=%d, loop_start=%d) ----\n",
          title ? title : "", b->ninst, b->maxslot, b->loop_start);
  for (int i = 0; i < b->ninst; i++) {
    const SPTIRInst *ir = &b->insts[i];
    char fl[8]; int fi = 0;
    if (ir->flags & SPTIRF_GUARD) fl[fi++] = 'G';
    if (ir->flags & SPTIRF_SNAP)  fl[fi++] = 'S';
    if (ir->flags & SPTIRF_DEAD)  fl[fi++] = 'D';
    if (ir->flags & SPTIRF_PHI)   fl[fi++] = 'P';
    fl[fi] = 0;
    fprintf(stderr, "  %4d  %-3s %-9s op1=%-4d op2=%-4d aux=%lld  %s\n",
            i, irtypename(ir->type), iropname(ir->op),
            (int)ir->op1, (int)ir->op2, (long long)ir->aux, fl);
  }
  fprintf(stderr, "  reg_map (slot->ref):");
  for (int s = 0; s <= b->maxslot; s++)
    fprintf(stderr, " [%d]=%d", s, (int)b->reg_map[s]);
  fprintf(stderr, "\n----\n");
}

/* Chase a NOP-alias chain (from algebraic simplification or CSE) to the
   instruction that actually computes the value. */
static int canon_nop(SPTIRBuilder *b, int ref) {
  int guard = 0;
  while (ref >= 0 && ref < b->ninst &&
         b->insts[ref].op == SPTIR_NOP && guard++ < 256)
    ref = b->insts[ref].op1;
  return ref;
}

/* Rewrite every reference to a NOP-alias so it points at the canonical
   computing instruction: instruction operands, guard bounds (the aux ref of
   GUARD_LT/GUARD_LE), and the live slot map. After this, NOPs are unreferenced
   (DCE drops them) and codegen/RA never bind a register to a value-less NOP --
   which would otherwise leave that register holding a stale live-in (the cause
   of x*1 / x+0 and CSE miscompiles under register residency). Snapshots, taken
   during recording before optimization, are handled separately by gen_load's
   own NOP forwarding. */
static void forward_nops(SPTIRBuilder *b) {
  for (int i = 0; i < b->ninst; i++) {
    SPTIRInst *in = &b->insts[i];
    if (in->op == SPTIR_NOP) continue;          /* leave the alias itself */
    if (in->op1 >= 0) in->op1 = canon_nop(b, in->op1);
    if (in->op2 >= 0) in->op2 = canon_nop(b, in->op2);
    if ((in->op == SPTIR_GUARD_LT || in->op == SPTIR_GUARD_LE) &&
        in->aux >= 0 && in->aux < b->ninst)
      in->aux = canon_nop(b, (int)in->aux);
  }
  for (int s = 0; s <= b->maxslot && s < 256; s++)
    if (b->reg_map[s] >= 0) b->reg_map[s] = canon_nop(b, b->reg_map[s]);
}

void sptir_optimize(SPTIRBuilder *b) {
  /* Pass 1: constant folding + algebraic simplification. */
  for (int i = 0; i < b->ninst; i++) {
    if (b->insts[i].flags & SPTIRF_DEAD) continue;
    if (try_const_fold(b, i)) continue;
    try_algebraic(b, i);
  }

  /* Pass 2: CSE. */
  for (int i = 0; i < b->ninst; i++) {
    if (b->insts[i].flags & SPTIRF_DEAD) continue;
    try_cse(b, i);
  }

  /* Pass 2.5: forward NOP-aliases so no live reference points at a NOP. */
  forward_nops(b);

  /* Pass 3: Dead code elimination. */
  dce(b);
}
