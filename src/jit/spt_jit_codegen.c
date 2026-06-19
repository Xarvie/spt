/*
** spt_jit_codegen.c — Native Code Generation for the SPT Trace JIT
**
** Converts SSA IR to x86-64 machine code using a simple but effective
** register allocation strategy:
**
**   - Each IR ref gets a "home" spill slot on the JIT frame
**   - Operations load operands from spill slots into scratch registers,
**     compute, and store results back to spill slots
**   - SLOAD loads from the interpreter stack (RBX-based addressing)
**   - Guards check conditions and jump to custom exit stubs on failure
**   - Exit stubs restore the interpreter stack state and return
**   - The loop back-edge writes all live values back to the interpreter
**     stack and jumps to the loop start
**
** This approach eliminates interpreter dispatch overhead, enables type
** specialization (int vs float operations), and allows constant folding
** and CSE to reduce the number of native instructions.
**
** Calling convention (Windows x64):
**   Entry: RCX = lua_State *L, RDX = CallInfo *ci
**   Registers: R12 = L, R13 = ci, RBX = base, R14 = k (constants)
**   Scratch: RAX, RCX, RDX, R8-R11, R15
**   Float: XMM0-XMM3
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
#include "ltable.h"

#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdio.h>

/* Same offsets as in spt_jit.c */
#define OFF_L_TOP       offsetof(lua_State, top)
#define OFF_CI_FUNC     offsetof(CallInfo, func)
#define OFF_CI_SAVEDPC  offsetof(CallInfo, u.l.savedpc)
#define OFF_LCLOSURE_P  offsetof(LClosure, p)
#define OFF_PROTO_K     offsetof(Proto, k)
#define OFF_TVALUE_VAL  offsetof(TValue, value_)
#define OFF_TVALUE_TT   offsetof(TValue, tt_)
#define OFF_TABLE_LOGLEN offsetof(Table, loglen)
#define OFF_TABLE_ARRAY offsetof(Table, array)
#define OFF_TABLE_MODE  offsetof(Table, mode)

#define SLOT_SIZE 16

/* Lua type tag values for writing to stack */
#define TAG_NIL    0x00
#define TAG_FALSE  0x01
#define TAG_TRUE   0x11
#define TAG_INT    0x03
#define TAG_FLT    0x13
#define TAG_STR    0x44
#define TAG_ARR    0x49
#define TAG_TAB    0x45
#define TAG_FUNC   0x46

/* Get the Lua tag for an SPT type. */
static uint8_t spt_type_to_tag(SPTType t) {
  switch (t) {
    case SPTT_NIL:   return TAG_NIL;
    case SPTT_FALSE: return TAG_FALSE;
    case SPTT_TRUE:  return TAG_TRUE;
    case SPTT_INT:   return TAG_INT;
    case SPTT_FLT:   return TAG_FLT;
    case SPTT_STR:   return TAG_STR;
    case SPTT_ARR:   return TAG_ARR;
    case SPTT_TAB:   return TAG_TAB;
    case SPTT_FUNC:  return TAG_FUNC;
    default:         return TAG_NIL;
  }
}

/* =====================================================================
** Code generation context
** ===================================================================== */

typedef struct {
  SPTAsm asm_;
  SPTTrace *trace;
  SPTJitState *js;

  /* Frame layout */
  int frame_size;       /* total frame size (aligned to 16) */
  int nspill_slots;     /* number of spill slots (one per IR ref) */
  int shadow_space;     /* 32 bytes on Windows x64 */

  /* Labels */
  int32_t loop_label;   /* label for loop start */
  int32_t epilogue_label; /* label for epilogue */

  /* Exit stub labels, indexed by snapshot index (each guard owns one
     snapshot, so snap_idx uniquely identifies its exit). This decouples
     exit-stub emission from instruction order, which lets us hoist guards
     into the loop preheader. */
  int32_t exit_label_for_snap[SPT_JIT_MAX_SNAPSHOTS];
  uint8_t snap_emitted[SPT_JIT_MAX_SNAPSHOTS];
  int32_t exit_labels[SPT_JIT_MAX_EXITS]; /* legacy, unused after refactor */
  int nexits;

  /* ---- Linear-scan register allocation ---- */
  int use_ra;                 /* 1 if register residency is active */
  int8_t ref_reg[SPT_JIT_MAX_TRACE]; /* physical GPR per IR ref, or -1 (spilled) */
  int8_t ref_xmm[SPT_JIT_MAX_TRACE]; /* physical XMM per IR ref, or -1 (spilled) */
  uint8_t ref_hoist[SPT_JIT_MAX_TRACE]; /* 1 if emitted in preheader, skipped in body */
  int8_t slot_reg[256];       /* GPR holding each int-resident slot, or -1 */
  int8_t slot_xmm[256];       /* XMM holding each float-resident slot, or -1 */

  /* Current PC offset (for restoring savedpc on exit) */
  int cur_pc_offset;
} SPTCodeGen;

/* Physical GPRs available for register allocation. These are all preserved by
   the prologue/epilogue (RBP,RBX,RDI,RSI,R12-R15 are pushed; R8-R11 are
   caller-saved but the scalar loops we allocate never call external C, so they
   are free). Excluded: R12=L, R13=ci, RBX=base, R14=k (fixed roles); RAX, RCX,
   RDX, and the XMMs are kept as scratch for loads, shifts, div/mod, and
   floats. */
static const SPTReg RA_POOL[] = {
  SPT_R15, SPT_RSI, SPT_RDI, SPT_R8, SPT_R9, SPT_R10, SPT_R11
};
#define RA_POOL_N ((int)(sizeof(RA_POOL) / sizeof(RA_POOL[0])))

/* XMM registers for float residency. On the SysV ABI every XMM is
   caller-saved, so a leaf trace may clobber these freely; XMM0-3 stay scratch
   for loads/conversions. On Win64, XMM6-15 are callee-saved — using them
   without saving would violate the ABI, so the pool is restricted to the
   volatile XMM4/XMM5. (To reclaim XMM6/7 on Win64, save/restore them in the
   prologue/epilogue.) */
#if defined(_WIN32)
static const SPTXmmReg XMM_POOL[] = {
  SPT_XMM4, SPT_XMM5
};
#else
static const SPTXmmReg XMM_POOL[] = {
  SPT_XMM4, SPT_XMM5, SPT_XMM6, SPT_XMM7
};
#endif
#define XMM_POOL_N ((int)(sizeof(XMM_POOL) / sizeof(XMM_POOL[0])))

/* Spill slot offset for IR ref r. */
static int spill_off(SPTCodeGen *cg, int r) {
  return cg->shadow_space + r * 8;
}

/* Get (creating on first use) the exit-stub label for a snapshot index. */
static int32_t ensure_exit_label(SPTCodeGen *cg, int snap_idx) {
  if (snap_idx < 0 || snap_idx >= SPT_JIT_MAX_SNAPSHOTS)
    snap_idx = 0;
  if (!cg->snap_emitted[snap_idx]) {
    cg->exit_label_for_snap[snap_idx] = sptasm_newlabel(&cg->asm_);
    cg->snap_emitted[snap_idx] = 1;
  }
  return cg->exit_label_for_snap[snap_idx];
}

/* Is this IR op safe to run while we hold loop-carried values in caller-saved
   registers? Anything that may call into C (table access, upvalue indirection
   through metamethods, pow via libm, calls, concat) could clobber those
   registers, so we disable register residency for traces containing them. */
static int ra_op_is_safe(int op) {
  switch (op) {
    case SPTIR_NIL: case SPTIR_FALSE: case SPTIR_TRUE:
    case SPTIR_KINT: case SPTIR_KFLT: case SPTIR_KSTR:
    case SPTIR_KPTR: case SPTIR_KGC:
    case SPTIR_SLOAD: case SPTIR_SSTORE:
    case SPTIR_ADD: case SPTIR_SUB: case SPTIR_MUL: case SPTIR_DIV:
    case SPTIR_MOD: case SPTIR_IDIV: case SPTIR_NEG:
    case SPTIR_BAND: case SPTIR_BOR: case SPTIR_BXOR: case SPTIR_BNOT:
    case SPTIR_SHL: case SPTIR_SHR:
    case SPTIR_EQ: case SPTIR_NE: case SPTIR_LT: case SPTIR_LE:
    case SPTIR_GT: case SPTIR_GE: case SPTIR_NOT:
    case SPTIR_TOFLT: case SPTIR_TOINT:
    case SPTIR_GUARD: case SPTIR_GUARD_LT: case SPTIR_GUARD_LE:
    case SPTIR_GUARD_EQ: case SPTIR_GUARD_T: case SPTIR_GUARD_ULT:
    case SPTIR_GETI: case SPTIR_LEN: case SPTIR_SETI:
    case SPTIR_LOOP: case SPTIR_PHI: case SPTIR_NOP:
      return 1;
    default:
      /* POW (libm), ULOAD/USTORE, GETFIELD/SETFIELD, GETTABUP,
         CALL, RETURN, EXIT: not safe / not handled here. */
      return 0;
  }
}

/* Linear-scan-style register allocation for loop-carried integer scalars.
   Strategy: every stack slot that has an integer live-in SLOAD becomes
   resident in a dedicated GPR for the whole loop. The SLOAD and its type guard
   are hoisted into a preheader (run once on entry); the loop body then reads
   and writes the register directly, and the back-edge needs no stack writeback.
   Side exits spill the live registers back to the stack via the snapshot.

   This is intentionally conservative: if the trace uses any non-scalar op, or
   needs more resident slots than we have registers, we leave use_ra = 0 and the
   existing spill-everything path runs unchanged (and correctly). */
/* Chase NOP aliases (produced by algebraic simplification x*1 / x+0 / x-0 or by
   CSE) to the instruction that actually computes the value. A NOP emits no code,
   so a register must be bound to the real computing instruction; binding it to
   the NOP leaves the register never written (the computed value lands elsewhere
   or stays the stale live-in). Bounded against malformed chains. */
static int ra_canon_ref(SPTIRBuilder *ir, int ref) {
  int guard = 0;
  while (ref >= 0 && ref < ir->ninst &&
         ir->insts[ref].op == SPTIR_NOP && guard++ < 256)
    ref = ir->insts[ref].op1;
  return ref;
}

static void ra_analyze(SPTCodeGen *cg) {
  SPTIRBuilder *ir = &cg->trace->ir;
  cg->use_ra = 0;
  for (int i = 0; i < ir->ninst && i < SPT_JIT_MAX_TRACE; i++) {
    cg->ref_reg[i] = -1;
    cg->ref_xmm[i] = -1;
    cg->ref_hoist[i] = 0;
  }
  for (int s = 0; s < 256; s++) { cg->slot_reg[s] = -1; cg->slot_xmm[s] = -1; }

  /* Only optimize loop traces (must end in a LOOP back-edge). */
  if (!ir->have_loop) return;
  if (ir->ninst > SPT_JIT_MAX_TRACE) return;

  /* Bail on any non-scalar / call-bearing op. Also bail on float comparisons:
     the comparison codegen is integer-only, and a resident float operand would
     otherwise be read from a stale spill slot. */
  for (int i = 0; i < ir->ninst; i++) {
    SPTIRInst *in = &ir->insts[i];
    if (in->flags & SPTIRF_DEAD) continue;
    if (!ra_op_is_safe(in->op)) return;
    switch (in->op) {
      case SPTIR_EQ: case SPTIR_NE: case SPTIR_LT:
      case SPTIR_LE: case SPTIR_GT: case SPTIR_GE: {
        int o1 = in->op1, o2 = in->op2;
        if ((o1 >= 0 && o1 < ir->ninst && ir->insts[o1].type == SPTT_FLT) ||
            (o2 >= 0 && o2 < ir->ninst && ir->insts[o2].type == SPTT_FLT))
          return;
        break;
      }
      default: break;
    }
  }

  /* Collect distinct slots that have a live-in SLOAD, split by type, in order.
     Integer slots get GPRs; float slots get XMMs. */
  int islots[RA_POOL_N], isload[RA_POOL_N], nint = 0;
  int fslots[XMM_POOL_N], fsload[XMM_POOL_N], nflt = 0;
  for (int i = 0; i < ir->ninst; i++) {
    SPTIRInst *in = &ir->insts[i];
    if (in->flags & SPTIRF_DEAD) continue;
    if (in->op != SPTIR_SLOAD) continue;
    int slot = (int)in->aux;
    if (in->type == SPTT_INT) {
      int seen = 0;
      for (int k = 0; k < nint; k++) if (islots[k] == slot) { seen = 1; break; }
      if (seen) continue;
      if (nint >= RA_POOL_N) return;
      islots[nint] = slot; isload[nint] = i; nint++;
    } else if (in->type == SPTT_FLT) {
      int seen = 0;
      for (int k = 0; k < nflt; k++) if (fslots[k] == slot) { seen = 1; break; }
      if (seen) continue;
      if (nflt >= XMM_POOL_N) return;
      fslots[nflt] = slot; fsload[nflt] = i; nflt++;
    } else {
      /* Non-numeric live-in (e.g. an array reference for GETI). Leave it
         spilled and read from the stack each iteration -- safe only if it is
         read-only across the loop. A modified one would need the writeback we
         omit under residency, so bail in that case. */
      int slot = (int)in->aux;
      if (ir->reg_map[slot] != i) return; /* modified -> bail */
      continue;                           /* read-only -> skip (stays on stack) */
    }
  }
  if (nint == 0 && nflt == 0) return; /* nothing to gain */

  /* Assign a GPR to each integer-resident slot. */
  for (int k = 0; k < nint; k++) {
    SPTReg reg = RA_POOL[k];
    int slot = islots[k];
    cg->slot_reg[slot] = (int8_t)reg;
    int sload = isload[k];
    cg->ref_reg[sload] = (int8_t)reg;
    cg->ref_hoist[sload] = 1;
    int final_ref = ra_canon_ref(ir, ir->reg_map[slot]);
    if (final_ref >= 0 && final_ref != sload && final_ref < ir->ninst)
      cg->ref_reg[final_ref] = (int8_t)reg;
  }
  /* Assign an XMM to each float-resident slot. */
  for (int k = 0; k < nflt; k++) {
    SPTXmmReg reg = XMM_POOL[k];
    int slot = fslots[k];
    cg->slot_xmm[slot] = (int8_t)reg;
    int sload = fsload[k];
    cg->ref_xmm[sload] = (int8_t)reg;
    cg->ref_hoist[sload] = 1;
    int final_ref = ra_canon_ref(ir, ir->reg_map[slot]);
    if (final_ref >= 0 && final_ref != sload && final_ref < ir->ninst)
      cg->ref_xmm[final_ref] = (int8_t)reg;
  }

  /* Hoist the type guards that check these live-ins (they read the stack tag,
     which is valid once, before the loop; the value stays int thereafter). */
  for (int i = 0; i < ir->ninst; i++) {
    SPTIRInst *in = &ir->insts[i];
    if (in->flags & SPTIRF_DEAD) continue;
    if (in->op != SPTIR_GUARD_T) continue;
    int v = in->op1;
    if (v >= 0 && v < ir->ninst && cg->ref_hoist[v])
      cg->ref_hoist[i] = 1;
  }

  cg->use_ra = 1;
}

/* Loop-invariant code motion (targeted). Once residency is decided, hoist
   instructions whose value is the same every iteration into the preheader so
   they run once: read-only-slot SLOADs (e.g. an array reference), their type
   guards, LEN of an invariant array, and pure arithmetic over invariant
   operands. This removes the per-iteration array-pointer load, array type
   guard, and length load from array loops. Invariant comparisons/bounds guards
   and GETI are intentionally not hoisted (they depend on the loop counter).
   Only values not bound to a register are hoisted, so RA is left untouched. */
static void ra_hoist_invariants(SPTCodeGen *cg) {
  SPTIRBuilder *ir = &cg->trace->ir;
  if (!cg->use_ra) return;
  uint8_t *inv = (uint8_t *)calloc(ir->ninst > 0 ? ir->ninst : 1, 1);
  if (!inv) return;

  /* If the loop writes any array element, an invariant-looking array LOAD may
     be aliased by that write, so we must not hoist array loads. (Bounds guards
     and LEN stay safe: an in-bounds SETI never changes a length.) */
  int has_seti = 0;
  for (int i = 0; i < ir->ninst; i++) {
    if ((ir->insts[i].flags & SPTIRF_DEAD)) continue;
    if (ir->insts[i].op == SPTIR_SETI) { has_seti = 1; break; }
  }

  for (int i = 0; i < ir->ninst; i++) {
    SPTIRInst *in = &ir->insts[i];
    if (in->flags & SPTIRF_DEAD) continue;
    int is_inv = 0;
    int o1 = in->op1, o2 = in->op2;
    int o1ok = (o1 < 0) || inv[o1];
    int o2ok = (o2 < 0) || inv[o2];
    switch (in->op) {
      case SPTIR_KINT: case SPTIR_KFLT: case SPTIR_KSTR:
      case SPTIR_KPTR: case SPTIR_KGC: case SPTIR_NIL:
      case SPTIR_TRUE: case SPTIR_FALSE:
        is_inv = 1; break;
      case SPTIR_SLOAD: {
        int slot = (int)in->aux;     /* read-only slot: never modified in loop */
        is_inv = (ir->reg_map[slot] == i);
        break;
      }
      case SPTIR_GUARD_T:            /* guards an invariant value -> invariant */
      case SPTIR_LEN:
      case SPTIR_TOFLT: case SPTIR_TOINT:
        is_inv = o1ok; break;
      case SPTIR_GUARD_LT: {
        /* Hoist a bounds guard only for a *constant* compared value with an
           invariant bound -- i.e. the `c < LEN` guard of a constant-index GETI.
           This excludes the FORLOOP count guard (bound = live counter, not
           invariant) and variable-index guards (op1 not constant), both of
           which break internal looping if hoisted. */
        SPTIRInst *v = sptir_get(ir, o1);
        int bnd = (int)in->aux;
        int bndok = (bnd < 0) || inv[bnd];
        is_inv = o1ok && bndok && v && v->op == SPTIR_KINT;
        break;
      }
      case SPTIR_GETI: {
        /* Loop-invariant array element load with a CONSTANT index (e.g.
           base[0]). Constant index only: a hoisted variable-index load made the
           trace stop looping internally (entries became per-iteration), a net
           slowdown -- so we keep those in the loop. Also requires no aliasing
           array write in the loop. */
        SPTIRInst *ix = sptir_get(ir, o2);
        is_inv = !has_seti && o1ok && o2ok && ix && ix->op == SPTIR_KINT;
        break;
      }
      case SPTIR_ADD: case SPTIR_SUB: case SPTIR_MUL:
      case SPTIR_BAND: case SPTIR_BOR: case SPTIR_BXOR:
      case SPTIR_SHL: case SPTIR_SHR:
        is_inv = o1ok && o2ok; break;
      case SPTIR_NEG:
        is_inv = o1ok; break;
      default: is_inv = 0; break;    /* SETI/LOOP/cmp guards: no */
    }
    inv[i] = (uint8_t)is_inv;
    /* Hoist only if not register-resident (don't disturb RA's bindings). */
    if (is_inv && i < SPT_JIT_MAX_TRACE &&
        cg->ref_reg[i] < 0 && cg->ref_xmm[i] < 0) {
      cg->ref_hoist[i] = 1;
    }
  }
  free(inv);
}

/* =====================================================================
** Prologue and epilogue
** ===================================================================== */

static void gen_prologue(SPTCodeGen *cg) {
  SPTAsm *a = &cg->asm_;

  /* Save callee-saved registers. */
  sptasm_push(a, SPT_RBP);
  sptasm_push(a, SPT_RBX);
  sptasm_push(a, SPT_RDI);
  sptasm_push(a, SPT_RSI);
  sptasm_push(a, SPT_R12);
  sptasm_push(a, SPT_R13);
  sptasm_push(a, SPT_R14);
  sptasm_push(a, SPT_R15);

  /* Allocate frame: shadow space + spill slots. */
  cg->shadow_space = SPT_ABI_SHADOW;
  cg->frame_size = cg->shadow_space + cg->nspill_slots * 8;
  cg->frame_size = (cg->frame_size + 15) & ~15;
  cg->frame_size += 8;
  sptasm_sub_rsp(a, cg->frame_size);

  /* Set up registers:
     R12 = L   (ARG0: RCX on Win64, RDI on SysV)
     R13 = ci  (ARG1: RDX on Win64, RSI on SysV)
     RBX = base = ci->func.p + 1
     R14 = k (constants table)
  */
  sptasm_mov_rr(a, SPT_R12, SPT_ABI_ARG0);  /* R12 = L */
  sptasm_mov_rr(a, SPT_R13, SPT_ABI_ARG1);  /* R13 = ci */

  /* base = ci->func.p + 1 */
  sptasm_mov_rm(a, SPT_RAX, SPT_R13, OFF_CI_FUNC);  /* RAX = ci->func.p */
  sptasm_lea(a, SPT_RBX, SPT_RAX, SLOT_SIZE);        /* RBX = base = func.p + 16 */

  /* k = ci_func(ci)->p->k */
  /* RAX = ci->func.p (StackValue*), [RAX] = value_.gc = LClosure* */
  sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TVALUE_VAL);  /* RAX = LClosure* */
  sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_LCLOSURE_P);  /* RAX = Proto* */
  sptasm_mov_rm(a, SPT_R14, SPT_RAX, OFF_PROTO_K);     /* R14 = k */
}

static void gen_epilogue(SPTCodeGen *cg) {
  SPTAsm *a = &cg->asm_;
  sptasm_place(a, cg->epilogue_label);
  sptasm_add_rsp(a, cg->frame_size);
  sptasm_pop(a, SPT_R15);
  sptasm_pop(a, SPT_R14);
  sptasm_pop(a, SPT_R13);
  sptasm_pop(a, SPT_R12);
  sptasm_pop(a, SPT_RSI);
  sptasm_pop(a, SPT_RDI);
  sptasm_pop(a, SPT_RBX);
  sptasm_pop(a, SPT_RBP);
  sptasm_ret(a);
}

/* =====================================================================
** Load and store helpers
** ===================================================================== */

/* Load an IR ref's value into a register.
   For constants, materialize on demand.
   For SLOAD, load from interpreter stack.
   For NOP (CSE/algebraic result), redirect to op1.
   For other refs, load from spill slot. */
static void gen_load(SPTCodeGen *cg, SPTReg dst, int ref, SPTType type) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  SPTIRInst *inst = sptir_get(ir, ref);
  if (!inst) return;

  /* Register-resident value: it lives in a GPR, not in memory. */
  if (cg->use_ra && ref >= 0 && ref < ir->ninst && cg->ref_reg[ref] >= 0) {
    SPTReg src = (SPTReg)cg->ref_reg[ref];
    if (dst != src) sptasm_mov_rr(a, dst, src);
    return;
  }
  /* Resident in an XMM register (e.g. a float used as an array store value):
     move its 64 raw bits into the GPR rather than reading a stale spill. */
  if (cg->use_ra && ref >= 0 && ref < ir->ninst && cg->ref_xmm[ref] >= 0) {
    sptasm_movq_xmm_to_gpr(a, dst, (SPTXmmReg)cg->ref_xmm[ref]);
    return;
  }

  switch (inst->op) {
    case SPTIR_NOP:
      /* CSE/algebraic simplification: redirect to op1 (the canonical ref). */
      if (inst->op1 >= 0)
        gen_load(cg, dst, inst->op1, type);
      else
        sptasm_xor_rr(a, dst, dst);
      break;
    case SPTIR_KINT:
      sptasm_mov_ri64(a, dst, inst->aux);
      break;
    case SPTIR_KFLT: {
      /* Load float constant from the spill slot (initialized by gen_inst). */
      sptasm_mov_rm(a, dst, SPT_RSP, spill_off(cg, ref));
      break;
    }
    case SPTIR_KSTR:
    case SPTIR_KPTR:
    case SPTIR_KGC:
      sptasm_mov_ri64(a, dst, inst->aux);
      break;
    case SPTIR_NIL:
    case SPTIR_FALSE:
      sptasm_xor_rr(a, dst, dst);
      break;
    case SPTIR_TRUE:
      sptasm_mov_ri32(a, dst, 1);
      break;
    case SPTIR_SLOAD:
      /* Load from spill slot (cached at trace start by gen_inst).
         This is correct for exit stubs where the stack may have been
         modified, and for the loop body where the value hasn't changed. */
      sptasm_mov_rm(a, dst, SPT_RSP, spill_off(cg, ref));
      break;
    default:
      /* Load from spill slot. */
      sptasm_mov_rm(a, dst, SPT_RSP, spill_off(cg, ref));
      break;
  }
}

/* Store a register's value to an IR ref's home (a GPR if resident, else its
   spill slot). */
static void gen_store(SPTCodeGen *cg, int ref, SPTReg src) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  if (cg->use_ra && ref >= 0 && ref < ir->ninst && cg->ref_reg[ref] >= 0) {
    SPTReg dst = (SPTReg)cg->ref_reg[ref];
    if (dst != src) sptasm_mov_rr(a, dst, src);
    return;
  }
  sptasm_mov_mr(a, SPT_RSP, spill_off(cg, ref), src);
}

/* Load a float IR ref into an XMM register (from its resident XMM if it has
   one, otherwise from its spill slot, where the raw double bits live). */
static void gen_load_xmm(SPTCodeGen *cg, SPTXmmReg dst, int ref) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  if (cg->use_ra && ref >= 0 && ref < ir->ninst && cg->ref_xmm[ref] >= 0) {
    SPTXmmReg src = (SPTXmmReg)cg->ref_xmm[ref];
    if (dst != src) sptasm_movsd_rr(a, dst, src);
    return;
  }
  sptasm_movsd_rm(a, dst, SPT_RSP, spill_off(cg, ref));
}

/* Store an XMM register to a float IR ref's home (resident XMM or spill slot). */
static void gen_store_xmm(SPTCodeGen *cg, int ref, SPTXmmReg src) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  if (cg->use_ra && ref >= 0 && ref < ir->ninst && cg->ref_xmm[ref] >= 0) {
    SPTXmmReg dst = (SPTXmmReg)cg->ref_xmm[ref];
    if (dst != src) sptasm_movsd_rr(a, dst, src);
    return;
  }
  sptasm_movsd_mr(a, SPT_RSP, spill_off(cg, ref), src);
}

/* Write a type tag to a stack slot.
   Uses: MOV byte [RBX + disp], imm8  (opcode C6 /0) */
static void gen_write_tag(SPTCodeGen *cg, int slot, uint8_t tag) {
  SPTAsm *a = &cg->asm_;
  int disp = slot * SLOT_SIZE + 8; /* tag is at offset 8 in TValue */

  sptasm_byte(a, 0xC6); /* MOV r/m8, imm8 */
  if (disp >= -128 && disp <= 127) {
    /* ModR/M: mod=01 (disp8), reg=0 (/0), rm=3 (RBX) */
    sptasm_byte(a, 0x43);
    sptasm_byte(a, (uint8_t)(disp & 0xff));
  } else {
    /* ModR/M: mod=10 (disp32), reg=0 (/0), rm=3 (RBX) */
    sptasm_byte(a, 0x83);
    sptasm_dword(a, (uint32_t)disp);
  }
  sptasm_byte(a, tag);
}

/* Load a type tag byte from a stack slot into RAX (zero-extended).
   Uses: MOVZX EAX, byte [RBX + disp]  (opcode 0F B6 /r) */
static void gen_load_tag(SPTCodeGen *cg, int slot) {
  SPTAsm *a = &cg->asm_;
  int disp = slot * SLOT_SIZE + 8; /* tag is at offset 8 in TValue */

  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xB6); /* MOVZX r32, r/m8 */
  if (disp >= -128 && disp <= 127) {
    /* ModR/M: mod=01 (disp8), reg=0 (RAX), rm=3 (RBX) */
    sptasm_byte(a, 0x43);
    sptasm_byte(a, (uint8_t)(disp & 0xff));
  } else {
    /* ModR/M: mod=10 (disp32), reg=0 (RAX), rm=3 (RBX) */
    sptasm_byte(a, 0x83);
    sptasm_dword(a, (uint32_t)disp);
  }
}

/* =====================================================================
** Guard exit stubs
** ===================================================================== */

/* Generate an exit stub that restores the interpreter state and returns.
   Indexed by snapshot index (== the guard's snap_idx). The exit stub writes all
   live values (from the snapshot) to the interpreter stack, sets savedpc, and
   returns. With register residency, the snapshot's refs are read via gen_load,
   which sources them from their resident registers. */
static void gen_exit_stub(SPTCodeGen *cg, int snap_idx) {
  SPTAsm *a = &cg->asm_;
  SPTTrace *t = cg->trace;
  SPTIRBuilder *ir = &t->ir;

  sptasm_place(a, cg->exit_label_for_snap[snap_idx]);

  /* Get the snapshot for this exit. */
  if (snap_idx >= ir->nsnaps) {
    /* No snapshot: just go to epilogue. */
    sptasm_jmp(a, cg->epilogue_label);
    return;
  }

  SPTSnapshot *snap = ir->snaps[snap_idx];

  /* With register residency there is no per-iteration stack writeback, so a
     resident slot's stack copy is stale. Flush every resident slot's current
     register value to the stack first (always INT-typed). The snapshot loop
     below then overrides any slot it covers with the precise value for this
     guard's PC (which matters for multi-step updates whose mid-value is
     spilled). The register holds exactly the slot's value at the guard point,
     because nothing between the guard's compare and this stub alters it. */
  if (cg->use_ra) {
    for (int slot = 0; slot < 256; slot++) {
      int reg = cg->slot_reg[slot];
      if (reg < 0) continue;
      sptasm_mov_mr(a, SPT_RBX, slot * SLOT_SIZE, (SPTReg)reg);
      gen_write_tag(cg, slot, TAG_INT);
    }
    /* Same for float-resident slots: flush the XMM and tag it as float. */
    for (int slot = 0; slot < 256; slot++) {
      int xreg = cg->slot_xmm[slot];
      if (xreg < 0) continue;
      sptasm_movsd_mr(a, SPT_RBX, slot * SLOT_SIZE, (SPTXmmReg)xreg);
      gen_write_tag(cg, slot, TAG_FLT);
    }
  }

  /* Write all live values to the interpreter stack. */
  for (int slot = 0; slot < snap->nslots; slot++) {
    int ref = snap->slot_map[slot];
    if (ref < 0) {
      /* This slot was never loaded or written by the trace, so the
         interpreter's stack slot already holds the correct, current value.
         (Genuine nils produced by the trace use a real SPTIR_NIL ref, never
         -1.) Leaving it untouched is essential: overwriting with nil would
         clobber a live variable that the trace simply hadn't referenced yet
         at this guard -- e.g. a loop-body accumulator at a top-of-loop guard. */
      continue;
    }

    SPTType type = sptir_type(ir, ref);
    SPTIRInst *inst = sptir_get(ir, ref);

    /* Float values must be written through an XMM register (and tagged float).
       A KFLT keeps its bits in the spill slot; resident/computed floats come
       from their XMM via gen_load_xmm. */
    if (type == SPTT_FLT) {
      if (inst && inst->op == SPTIR_KFLT) {
        sptasm_movsd_rm(a, SPT_XMM0, SPT_RSP, spill_off(cg, ref));
      } else {
        gen_load_xmm(cg, SPT_XMM0, ref);
      }
      sptasm_movsd_mr(a, SPT_RBX, slot * SLOT_SIZE, SPT_XMM0);
      gen_write_tag(cg, slot, TAG_FLT);
      continue;
    }

    /* Load the value into RAX. */
    if (inst && inst->op == SPTIR_KINT) {
      sptasm_mov_ri64(a, SPT_RAX, inst->aux);
    } else if (inst && (inst->op == SPTIR_NIL || inst->op == SPTIR_FALSE)) {
      sptasm_xor_rr(a, SPT_RAX, SPT_RAX);
    } else if (inst && inst->op == SPTIR_TRUE) {
      sptasm_mov_ri32(a, SPT_RAX, 1);
    } else {
      gen_load(cg, SPT_RAX, ref, type);
    }

    /* Write value to stack. */
    int disp = slot * SLOT_SIZE;
    sptasm_mov_mr(a, SPT_RBX, disp, SPT_RAX);

    /* Write tag. */
    gen_write_tag(cg, slot, spt_type_to_tag(type));
  }

  /* Set ci->u.l.savedpc = exit_pc.
     The exit PC is stored in the trace's exit_pcs array, indexed by snapshot. */
  const Instruction *exit_pc = t->exit_pcs[snap_idx];
  if (exit_pc) {
    /* We need to compute the PC offset from proto->code.
       For simplicity, store the absolute pointer. */
    sptasm_mov_ri64(a, SPT_RAX, (int64_t)exit_pc);
    sptasm_mov_mr(a, SPT_R13, OFF_CI_SAVEDPC, SPT_RAX);
  }

  /* Jump to epilogue. */
  sptasm_jmp(a, cg->epilogue_label);
}

/* =====================================================================
** IR instruction code generation
** ===================================================================== */

/* Emit an in-place integer binary op:  R = R <op> src.
   Used for two-address coalescing when a result is register-resident and its
   first operand is the same register (the common accumulator/counter update
   x = x OP y). Picks an immediate, register, or scratch-loaded source. R is
   always an RA_POOL register, so RAX is free as scratch. */
static void emit_int_inplace(SPTCodeGen *cg, int op, SPTReg R, int srcref) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  SPTIRInst *si = (srcref >= 0 && srcref < ir->ninst) ? &ir->insts[srcref] : NULL;

  /* Immediate source (fits in 32 bits). */
  if (si && si->op == SPTIR_KINT && si->aux >= INT32_MIN && si->aux <= INT32_MAX) {
    int32_t imm = (int32_t)si->aux;
    switch (op) {
      case SPTIR_ADD:  sptasm_add_ri(a, R, imm); return;
      case SPTIR_SUB:  sptasm_sub_ri(a, R, imm); return;
      case SPTIR_BAND: sptasm_and_ri(a, R, imm); return;
      case SPTIR_BOR:  sptasm_or_ri(a, R, imm);  return;
      case SPTIR_BXOR: sptasm_xor_ri(a, R, imm); return;
      case SPTIR_MUL:  sptasm_imul_rri(a, R, R, imm); return;
      default: break;
    }
  }

  /* Register source. */
  if (cg->use_ra && srcref >= 0 && srcref < ir->ninst && cg->ref_reg[srcref] >= 0) {
    SPTReg S = (SPTReg)cg->ref_reg[srcref];
    switch (op) {
      case SPTIR_ADD:  sptasm_add_rr(a, R, S); return;
      case SPTIR_SUB:  sptasm_sub_rr(a, R, S); return;
      case SPTIR_MUL:  sptasm_imul_rr(a, R, S); return;
      case SPTIR_BAND: sptasm_and_rr(a, R, S); return;
      case SPTIR_BOR:  sptasm_or_rr(a, R, S);  return;
      case SPTIR_BXOR: sptasm_xor_rr(a, R, S); return;
      default: break;
    }
  }

  /* Fallback: materialize/load the source into RAX, then op in place. */
  gen_load(cg, SPT_RAX, srcref, SPTT_INT);
  switch (op) {
    case SPTIR_ADD:  sptasm_add_rr(a, R, SPT_RAX); break;
    case SPTIR_SUB:  sptasm_sub_rr(a, R, SPT_RAX); break;
    case SPTIR_MUL:  sptasm_imul_rr(a, R, SPT_RAX); break;
    case SPTIR_BAND: sptasm_and_rr(a, R, SPT_RAX); break;
    case SPTIR_BOR:  sptasm_or_rr(a, R, SPT_RAX);  break;
    case SPTIR_BXOR: sptasm_xor_rr(a, R, SPT_RAX); break;
    default: break;
  }
}

static void gen_inst(SPTCodeGen *cg, int idx) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  SPTIRInst *inst = &ir->insts[idx];

  /* Skip dead instructions. */
  if (inst->flags & SPTIRF_DEAD) return;
  if (inst->op == SPTIR_NOP) return;

  switch (inst->op) {
    /* ---- Constants ----
       gen_load (and the exit stub) re-materialize integer/pointer constants
       and nil/true/false on demand, so storing them to a spill slot is dead
       work. Only emit a store when the constant is bound to a register (which
       the loop body may read across the back-edge). Float constants are the
       exception: gen_load sources them from the spill slot, so they must be
       written. */
    case SPTIR_NIL:
    case SPTIR_FALSE:
      if (cg->use_ra && idx < ir->ninst && cg->ref_reg[idx] >= 0) {
        sptasm_xor_rr(a, SPT_RAX, SPT_RAX);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    case SPTIR_TRUE:
      if (cg->use_ra && idx < ir->ninst && cg->ref_reg[idx] >= 0) {
        sptasm_mov_ri32(a, SPT_RAX, 1);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    case SPTIR_KINT:
    case SPTIR_KSTR:
    case SPTIR_KPTR:
    case SPTIR_KGC:
      if (cg->use_ra && idx < ir->ninst && cg->ref_reg[idx] >= 0) {
        sptasm_mov_ri64(a, SPT_RAX, inst->aux);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    case SPTIR_KFLT:
      /* Float constants are read from the spill slot by gen_load. */
      sptasm_mov_ri64(a, SPT_RAX, inst->aux);
      sptasm_mov_mr(a, SPT_RSP, spill_off(cg, idx), SPT_RAX);
      break;

    /* ---- Stack load ---- */
    case SPTIR_SLOAD: {
      int slot = (int)inst->aux;
      if (cg->use_ra && idx < ir->ninst && cg->ref_xmm[idx] >= 0) {
        /* Float-resident: load the double straight into its XMM register. */
        sptasm_movsd_rm(a, (SPTXmmReg)cg->ref_xmm[idx], SPT_RBX, slot * SLOT_SIZE);
      } else {
        sptasm_mov_rm(a, SPT_RAX, SPT_RBX, slot * SLOT_SIZE);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    }

    /* ---- Upvalue load ---- */
    case SPTIR_ULOAD: {
      /* Load upvalue: cl->upvals[aux]->v.p->value_ */
      int uv_idx = (int)inst->aux;
      /* cl = ci_func(ci) = s2v(ci->func.p)->value_.gc */
      sptasm_mov_rm(a, SPT_RAX, SPT_R13, OFF_CI_FUNC); /* RAX = ci->func.p */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TVALUE_VAL); /* RAX = LClosure* */
      /* RAX = cl->upvals[uv_idx] */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX,
                    offsetof(LClosure, upvals) + uv_idx * 8);
      /* RAX = upval->v.p (TValue*) */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, offsetof(UpVal, v));
      /* RAX = *upval->v.p (the value) */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TVALUE_VAL);
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    /* ---- Arithmetic (integer) ---- */
    case SPTIR_ADD:
    case SPTIR_SUB:
    case SPTIR_MUL:
    case SPTIR_BAND:
    case SPTIR_BOR:
    case SPTIR_BXOR: {
      if (inst->type == SPTT_INT) {
        /* Two-address coalescing: if the result is resident in R and its first
           operand is the same register, compute in place (one instruction)
           instead of load/load/op/store. Commutative ops also catch the case
           where the second operand is the result register. */
        int rr = (cg->use_ra && idx < ir->ninst) ? cg->ref_reg[idx] : -1;
        int r1 = (cg->use_ra && inst->op1 >= 0 && inst->op1 < ir->ninst)
                   ? cg->ref_reg[inst->op1] : -1;
        int r2 = (cg->use_ra && inst->op2 >= 0 && inst->op2 < ir->ninst)
                   ? cg->ref_reg[inst->op2] : -1;
        int commutative = (inst->op == SPTIR_ADD || inst->op == SPTIR_MUL ||
                           inst->op == SPTIR_BAND || inst->op == SPTIR_BOR ||
                           inst->op == SPTIR_BXOR);
        if (rr >= 0 && rr == r1) {
          emit_int_inplace(cg, inst->op, (SPTReg)rr, inst->op2);
        } else if (rr >= 0 && commutative && rr == r2) {
          emit_int_inplace(cg, inst->op, (SPTReg)rr, inst->op1);
        } else {
          gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
          gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);
          switch (inst->op) {
            case SPTIR_ADD:  sptasm_add_rr(a, SPT_RAX, SPT_RCX); break;
            case SPTIR_SUB:  sptasm_sub_rr(a, SPT_RAX, SPT_RCX); break;
            case SPTIR_MUL:  sptasm_imul_rr(a, SPT_RAX, SPT_RCX); break;
            case SPTIR_BAND: sptasm_and_rr(a, SPT_RAX, SPT_RCX); break;
            case SPTIR_BOR:  sptasm_or_rr(a, SPT_RAX, SPT_RCX); break;
            case SPTIR_BXOR: sptasm_xor_rr(a, SPT_RAX, SPT_RCX); break;
          }
          gen_store(cg, idx, SPT_RAX);
        }
      } else if (inst->type == SPTT_FLT) {
        /* Float arithmetic in XMM, with two-address coalescing when the result
           is XMM-resident and shares a register with op1 (commutative ops also
           catch op2). Otherwise compute in XMM0 and store. */
        int xr = (cg->use_ra && idx < ir->ninst) ? cg->ref_xmm[idx] : -1;
        int x1 = (cg->use_ra && inst->op1 >= 0 && inst->op1 < ir->ninst)
                   ? cg->ref_xmm[inst->op1] : -1;
        int x2 = (cg->use_ra && inst->op2 >= 0 && inst->op2 < ir->ninst)
                   ? cg->ref_xmm[inst->op2] : -1;
        int commutative = (inst->op == SPTIR_ADD || inst->op == SPTIR_MUL);
        int inplace = 0, srcref = -1;
        if (xr >= 0 && xr == x1) { inplace = 1; srcref = inst->op2; }
        else if (xr >= 0 && commutative && xr == x2) { inplace = 1; srcref = inst->op1; }

        if (inplace) {
          /* R = R <op> src. Use src's resident XMM, else load into XMM1. */
          SPTXmmReg R = (SPTXmmReg)xr, S;
          if (cg->use_ra && srcref >= 0 && srcref < ir->ninst &&
              cg->ref_xmm[srcref] >= 0 && cg->ref_xmm[srcref] != xr) {
            S = (SPTXmmReg)cg->ref_xmm[srcref];
          } else {
            gen_load_xmm(cg, SPT_XMM1, srcref);
            S = SPT_XMM1;
          }
          switch (inst->op) {
            case SPTIR_ADD: sptasm_addsd(a, R, S); break;
            case SPTIR_SUB: sptasm_subsd(a, R, S); break;
            case SPTIR_MUL: sptasm_mulsd(a, R, S); break;
            case SPTIR_DIV: sptasm_divsd(a, R, S); break;
          }
        } else {
          gen_load_xmm(cg, SPT_XMM0, inst->op1);
          gen_load_xmm(cg, SPT_XMM1, inst->op2);
          switch (inst->op) {
            case SPTIR_ADD: sptasm_addsd(a, SPT_XMM0, SPT_XMM1); break;
            case SPTIR_SUB: sptasm_subsd(a, SPT_XMM0, SPT_XMM1); break;
            case SPTIR_MUL: sptasm_mulsd(a, SPT_XMM0, SPT_XMM1); break;
            case SPTIR_DIV: sptasm_divsd(a, SPT_XMM0, SPT_XMM1); break;
          }
          gen_store_xmm(cg, idx, SPT_XMM0);
        }
      }
      break;
    }

    case SPTIR_DIV: {
      if (inst->type == SPTT_FLT) {
        gen_load_xmm(cg, SPT_XMM0, inst->op1);
        gen_load_xmm(cg, SPT_XMM1, inst->op2);
        sptasm_divsd(a, SPT_XMM0, SPT_XMM1);
        gen_store_xmm(cg, idx, SPT_XMM0);
      } else if (inst->type == SPTT_INT) {
        /* Integer division: IDIV */
        gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
        gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);
        sptasm_cqo(a);      /* sign-extend RAX into RDX:RAX */
        sptasm_idiv_r(a, SPT_RCX);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    }

    case SPTIR_MOD: {
      if (inst->type == SPTT_INT) {
        gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
        gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);
        sptasm_cqo(a);
        sptasm_idiv_r(a, SPT_RCX);
        /* Remainder is in RDX. */
        sptasm_mov_rr(a, SPT_RAX, SPT_RDX);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    }

    case SPTIR_IDIV: {
      if (inst->type == SPTT_INT) {
        gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
        gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);
        sptasm_cqo(a);
        sptasm_idiv_r(a, SPT_RCX);
        gen_store(cg, idx, SPT_RAX);
      }
      break;
    }

    case SPTIR_NEG: {
      if (inst->type == SPTT_INT) {
        gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
        sptasm_neg_r(a, SPT_RAX);
        gen_store(cg, idx, SPT_RAX);
      } else if (inst->type == SPTT_FLT) {
        /* Negate a double by flipping its sign bit. Move the bits into RAX
           (dumping a resident XMM to its spill slot first), XOR the sign bit,
           then write back to the result's home. */
        if (cg->use_ra && inst->op1 >= 0 && inst->op1 < ir->ninst &&
            cg->ref_xmm[inst->op1] >= 0) {
          sptasm_movsd_mr(a, SPT_RSP, spill_off(cg, inst->op1),
                          (SPTXmmReg)cg->ref_xmm[inst->op1]);
        }
        sptasm_mov_rm(a, SPT_RAX, SPT_RSP, spill_off(cg, inst->op1));
        sptasm_mov_ri64(a, SPT_RCX, (int64_t)0x8000000000000000ULL);
        sptasm_xor_rr(a, SPT_RAX, SPT_RCX);
        sptasm_mov_mr(a, SPT_RSP, spill_off(cg, idx), SPT_RAX);
        if (cg->use_ra && idx < ir->ninst && cg->ref_xmm[idx] >= 0) {
          sptasm_movsd_rm(a, (SPTXmmReg)cg->ref_xmm[idx], SPT_RSP, spill_off(cg, idx));
        }
      }
      break;
    }

    case SPTIR_BNOT: {
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      sptasm_not_r(a, SPT_RAX);
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    case SPTIR_SHL:
    case SPTIR_SHR: {
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);
      if (inst->op == SPTIR_SHL)
        sptasm_shl_cl(a, SPT_RAX);
      else
        sptasm_shr_cl(a, SPT_RAX);
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    case SPTIR_TOFLT: {
      /* Convert int to float. */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      sptasm_cvtsi2sd(a, SPT_XMM0, SPT_RAX);
      gen_store_xmm(cg, idx, SPT_XMM0);
      break;
    }

    case SPTIR_LEN: {
      /* Get array length: Table->loglen */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ARR);
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TABLE_LOGLEN);
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    /* ---- Comparisons (as guards) ---- */
    case SPTIR_EQ:
    case SPTIR_NE:
    case SPTIR_LT:
    case SPTIR_LE:
    case SPTIR_GT:
    case SPTIR_GE: {
      /* Comparison: if condition is FALSE, take side exit. */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);
      sptasm_cmp_rr(a, SPT_RAX, SPT_RCX);

      SPTCC cc;
      switch (inst->op) {
        case SPTIR_EQ: cc = SPT_CC_NE; break;  /* continue if ==, exit if != */
        case SPTIR_NE: cc = SPT_CC_E; break;   /* continue if !=, exit if == */
        case SPTIR_LT: cc = SPT_CC_NL; break;  /* if not less, exit */
        case SPTIR_LE: cc = SPT_CC_NLE; break; /* if not le, exit */
        case SPTIR_GT: cc = SPT_CC_LE; break;  /* if not gt, exit */
        case SPTIR_GE: cc = SPT_CC_L; break;   /* if not ge, exit */
        default: cc = SPT_CC_NE;
      }

      int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
      sptasm_jcc(a, cc, exlbl);
      break;
    }

    /* ---- Guards ---- */
    case SPTIR_GUARD_T: {
      /* Check type tag of the value on the stack. */
      int slot = -1;
      SPTIRInst *val = sptir_get(ir, inst->op1);
      if (val && val->op == SPTIR_SLOAD)
        slot = (int)val->aux;

      if (slot >= 0) {
        /* Load the tag byte from [RBX + slot * 16 + 8] and compare. */
        gen_load_tag(cg, slot);
        sptasm_cmp_ri(a, SPT_RAX, spt_type_to_tag((SPTType)inst->aux));

        int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
        sptasm_jcc(a, SPT_CC_NE, exlbl);
      }
      /* If we can't find the slot, skip the guard (conservative). */
      break;
    }

    case SPTIR_GUARD_LT: {
      /* Guard: op1 < aux (aux is an IR ref to the bound). The FORLOOP emits
         this as KINT(0) < count; specialize a constant op1 to a single
         test/cmp on the bound, dropping the constant materialization. */
      SPTIRInst *o1 = sptir_get(ir, inst->op1);
      int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
      if (o1 && o1->op == SPTIR_KINT && o1->aux >= INT32_MIN && o1->aux <= INT32_MAX) {
        int bound = (int)inst->aux;
        SPTReg br;
        if (cg->use_ra && bound >= 0 && bound < ir->ninst && cg->ref_reg[bound] >= 0)
          br = (SPTReg)cg->ref_reg[bound];
        else { gen_load(cg, SPT_RAX, bound, SPTT_INT); br = SPT_RAX; }
        int32_t c = (int32_t)o1->aux;
        if (c == 0) sptasm_test_rr(a, br, br);
        else sptasm_cmp_ri(a, br, c);
        sptasm_jcc(a, SPT_CC_LE, exlbl); /* fail (exit) when bound <= c */
      } else {
        gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
        gen_load(cg, SPT_RCX, (int)inst->aux, SPTT_INT);
        sptasm_cmp_rr(a, SPT_RAX, SPT_RCX);
        sptasm_jcc(a, SPT_CC_NL, exlbl); /* if not <, exit */
      }
      break;
    }

    case SPTIR_GUARD_LE: {
      SPTIRInst *o1 = sptir_get(ir, inst->op1);
      int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
      if (o1 && o1->op == SPTIR_KINT && o1->aux >= INT32_MIN && o1->aux <= INT32_MAX) {
        int bound = (int)inst->aux;
        SPTReg br;
        if (cg->use_ra && bound >= 0 && bound < ir->ninst && cg->ref_reg[bound] >= 0)
          br = (SPTReg)cg->ref_reg[bound];
        else { gen_load(cg, SPT_RAX, bound, SPTT_INT); br = SPT_RAX; }
        sptasm_cmp_ri(a, br, (int32_t)o1->aux);
        sptasm_jcc(a, SPT_CC_L, exlbl); /* op1<=bound fails when bound < op1 */
      } else {
        gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
        gen_load(cg, SPT_RCX, (int)inst->aux, SPTT_INT);
        sptasm_cmp_rr(a, SPT_RAX, SPT_RCX);
        sptasm_jcc(a, SPT_CC_NLE, exlbl); /* if not <=, exit */
      }
      break;
    }

    case SPTIR_GUARD_ULT: {
      /* Guard: (unsigned)op1 < aux (an immediate constant). Used for shift
         counts: a single unsigned compare rejects both negative (huge
         unsigned) and >= aux. If it fails, side-exit. */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      sptasm_cmp_ri(a, SPT_RAX, (int32_t)inst->aux);
      int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
      sptasm_jcc(a, SPT_CC_NB, exlbl); /* if !(unsigned <), exit */
      break;
    }

    case SPTIR_GUARD: {
      /* Truthiness guard: if value is not truthy/falsy per aux, exit. */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ANY);
      sptasm_test_rr(a, SPT_RAX, SPT_RAX);
      int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
      if (inst->aux) {
        /* Want truthy: exit if zero. */
        sptasm_jcc(a, SPT_CC_E, exlbl);
      } else {
        /* Want falsy: exit if non-zero. */
        sptasm_jcc(a, SPT_CC_NE, exlbl);
      }
      break;
    }

    /* ---- Array access ---- */
    case SPTIR_GETI: {
      /* R[A] = R[B][C] for arrays.  op1 = array ref, op2 = index ref.
         Memory layout (see ltable.h): t->array points between values and tags.
           value k = t->array[-(k+1)]            = [array - (k+1)*8]
           tag   k = ((uint8_t*)t->array)[4 + k] = [array + 4 + k]
         This is a guarded load: check the element's tag against the recorded
         type (inst->type) and side-exit on mismatch, then load the value. */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ARR);  /* RAX = Table* */
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);  /* RCX = index */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TABLE_ARRAY); /* RAX = t->array */

      /* --- type guard: load tag at [array + 4 + index], compare, exit --- */
      sptasm_lea(a, SPT_RDX, SPT_RAX, 4);   /* RDX = array + 4 */
      sptasm_add_rr(a, SPT_RDX, SPT_RCX);   /* RDX = array + 4 + index */
      sptasm_byte(a, 0x0F); sptasm_byte(a, 0xB6); sptasm_byte(a, 0x12); /* movzx edx,byte[rdx] */
      sptasm_cmp_ri(a, SPT_RDX, spt_type_to_tag(inst->type));
      {
        int32_t exlbl = ensure_exit_label(cg, inst->snap_idx);
        sptasm_jcc(a, SPT_CC_NE, exlbl);
      }

      /* --- load value at [array - (index+1)*8] (RAX=array, RCX=index) --- */
      sptasm_neg_r(a, SPT_RCX);             /* RCX = -index */
      sptasm_lea(a, SPT_RDX, SPT_RAX, -8);  /* RDX = array - 8 */
      sptasm_imul_rri(a, SPT_RCX, SPT_RCX, 8); /* RCX = -index*8 */
      sptasm_add_rr(a, SPT_RDX, SPT_RCX);   /* RDX = array - 8 - index*8 */
      sptasm_mov_rm(a, SPT_RAX, SPT_RDX, 0); /* RAX = value */
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    case SPTIR_SETI: {
      /* R[A][B] = val.  op1 = array ref, op2 = index ref, aux = value ref.
         Layout (ltable.h): value k at [array-(k+1)*8], tag k at [array+4+k].
         Bounds are guarded by the recorder. Uses only RAX/RCX/RDX scratch via
         SIB addressing, so this op is RA-safe (write loops keep residency). */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ARR);      /* RAX = Table* */
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);      /* RCX = index */
      gen_load(cg, SPT_RDX, (int)inst->aux, SPTT_ANY); /* RDX = value bits */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TABLE_ARRAY); /* RAX = t->array */

      /* tag store: mov byte [RAX + RCX*1 + 4], tag  (uses index, before scaling) */
      {
        uint8_t tag = spt_type_to_tag(sptir_type(ir, (int)inst->aux));
        sptasm_byte(a, 0xC6);   /* MOV r/m8, imm8 */
        sptasm_byte(a, 0x44);   /* mod=01 reg=000 rm=100(SIB) */
        sptasm_byte(a, 0x08);   /* SIB: scale=1 index=RCX base=RAX */
        sptasm_byte(a, 0x04);   /* disp8 = +4 */
        sptasm_byte(a, tag);
      }
      /* value store: mov [RAX + RCX*1 - 8], RDX, with RCX = -index*8 */
      sptasm_imul_rri(a, SPT_RCX, SPT_RCX, 8);  /* RCX = index*8 */
      sptasm_neg_r(a, SPT_RCX);                 /* RCX = -index*8 */
      sptasm_byte(a, 0x48);   /* REX.W */
      sptasm_byte(a, 0x89);   /* MOV r/m64, r64 */
      sptasm_byte(a, 0x54);   /* mod=01 reg=RDX rm=100(SIB) */
      sptasm_byte(a, 0x08);   /* SIB: scale=1 index=RCX base=RAX */
      sptasm_byte(a, 0xF8);   /* disp8 = -8 */
      break;
    }

    /* ---- Loop back-edge ---- */
    case SPTIR_LOOP: {
      /* With register residency, all loop-carried values live in registers
         and flow across the back-edge directly, so no stack writeback is
         needed (side exits spill them via snapshots). Without residency, write
         all live values back to the interpreter stack each iteration. */
      if (!cg->use_ra) {
        for (int slot = 0; slot <= ir->maxslot; slot++) {
          int ref = ir->reg_map[slot];
          if (ref < 0) continue;
          SPTType type = ir->reg_type[slot];
          gen_load(cg, SPT_RAX, ref, type);
          sptasm_mov_mr(a, SPT_RBX, slot * SLOT_SIZE, SPT_RAX);
          gen_write_tag(cg, slot, spt_type_to_tag(type));
        }
      }
      /* Jump back to loop start. */
      sptasm_jmp(a, cg->loop_label);
      break;
    }

    /* ---- Return ---- */
    case SPTIR_RETURN: {
      if (inst->op1 >= 0) {
        /* Load return value and store to R[A] (slot 0). */
        gen_load(cg, SPT_RAX, inst->op1, inst->type);
        sptasm_mov_mr(a, SPT_RBX, 0, SPT_RAX);
        gen_write_tag(cg, 0, spt_type_to_tag(inst->type));
      }
      /* Jump to epilogue. */
      sptasm_jmp(a, cg->epilogue_label);
      break;
    }

    case SPTIR_NOT: {
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ANY);
      sptasm_test_rr(a, SPT_RAX, SPT_RAX);
      sptasm_setcc(a, SPT_CC_E, SPT_RAX);
      sptasm_movzx_r8(a, SPT_RAX, SPT_RAX);
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    /* ---- Unhandled (treat as NOP) ---- */
    default:
      break;
  }
}

/* =====================================================================
** Main compilation entry point
** ===================================================================== */

void sptjit_codegen_compile(SPTTrace *t, SPTJitState *js) {
  SPTCodeGen cg;
  memset(&cg, 0, sizeof(cg));
  cg.trace = t;
  cg.js = js;

  SPTIRBuilder *ir = &t->ir;

  /* Determine the number of spill slots (one per IR ref). */
  cg.nspill_slots = ir->ninst + 1;

  /* Initialize assembler. */
  sptasm_init(&cg.asm_, 4096);

  /* Plan register allocation (sets cg.use_ra and the residency maps). */
  ra_analyze(&cg);
  /* Hoist loop-invariant work (e.g. array pointer/length/type guard) once RA
     bindings are fixed. */
  ra_hoist_invariants(&cg);

  /* Create labels. */
  cg.loop_label = sptasm_newlabel(&cg.asm_);
  cg.epilogue_label = sptasm_newlabel(&cg.asm_);

  /* Generate prologue. */
  gen_prologue(&cg);

  /* Preheader: when register residency is active, load loop-carried live-ins
     into their registers and run their type guards once, before the loop. */
  if (cg.use_ra) {
    for (int i = 0; i < ir->ninst; i++) {
      if (cg.ref_hoist[i]) gen_inst(&cg, i);
    }
  }

  /* Place loop label (entry point for the back-edge). */
  sptasm_place(&cg.asm_, cg.loop_label);

  /* Generate code for each IR instruction (skipping hoisted preheader ones). */
  for (int i = 0; i < ir->ninst; i++) {
    if (cg.use_ra && cg.ref_hoist[i]) continue;
    gen_inst(&cg, i);
  }

  /* Generate exit stubs, one per emitted snapshot. */
  for (int s = 0; s < SPT_JIT_MAX_SNAPSHOTS; s++) {
    if (cg.snap_emitted[s]) gen_exit_stub(&cg, s);
  }

  /* Generate epilogue. */
  gen_epilogue(&cg);

  /* Allocate executable memory and finalize code. */
  size_t code_size = sptasm_size(&cg.asm_);
  if (js->code_buf_used + code_size > js->code_buf_size) {
    /* Not enough space. */
    sptasm_free(&cg.asm_);
    return;
  }

  void *code_ptr = js->code_buf + js->code_buf_used;
  void *result = sptasm_finalize(&cg.asm_, code_ptr, js->code_buf_size - js->code_buf_used);
  if (!result) {
    sptasm_free(&cg.asm_);
    return;
  }

  t->code = result;
  t->code_size = code_size;
  js->code_buf_used += code_size;

  if (js->debug >= 2) {
    fprintf(stderr, "[JIT] code dump (%zu bytes) @ %p:\n", code_size, result);
    for (size_t bi = 0; bi < code_size; bi++) {
      fprintf(stderr, "%02X", ((uint8_t *)result)[bi]);
      fprintf(stderr, ((bi + 1) % 32 == 0) ? "\n" : " ");
    }
    fprintf(stderr, "\n");
  }

  /* Align to 16 bytes for the next trace. */
  js->code_buf_used = (js->code_buf_used + 15) & ~15;

  sptasm_free(&cg.asm_);
}
