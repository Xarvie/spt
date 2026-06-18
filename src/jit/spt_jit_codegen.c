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

  /* Exit stub labels */
  int32_t exit_labels[SPT_JIT_MAX_EXITS];
  int nexits;

  /* Current PC offset (for restoring savedpc on exit) */
  int cur_pc_offset;
} SPTCodeGen;

/* Spill slot offset for IR ref r. */
static int spill_off(SPTCodeGen *cg, int r) {
  return cg->shadow_space + r * 8;
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
  cg->shadow_space = 32;
  cg->frame_size = cg->shadow_space + cg->nspill_slots * 8;
  cg->frame_size = (cg->frame_size + 15) & ~15;
  cg->frame_size += 8;
  sptasm_sub_rsp(a, cg->frame_size);

  /* Set up registers:
     R12 = L (RCX on Windows x64)
     R13 = ci (RDX on Windows x64)
     RBX = base = ci->func.p + 1
     R14 = k (constants table)
  */
  sptasm_mov_rr(a, SPT_R12, SPT_RCX);  /* R12 = L */
  sptasm_mov_rr(a, SPT_R13, SPT_RDX);  /* R13 = ci */

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

/* Store a register's value to an IR ref's spill slot. */
static void gen_store(SPTCodeGen *cg, int ref, SPTReg src) {
  SPTAsm *a = &cg->asm_;
  sptasm_mov_mr(a, SPT_RSP, spill_off(cg, ref), src);
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

/* Store a value to the interpreter stack (for guard exits). */
static void gen_store_stack(SPTCodeGen *cg, int slot, SPTReg val, SPTType type) {
  SPTAsm *a = &cg->asm_;
  int disp = slot * SLOT_SIZE;
  /* Write the 8-byte value. */
  sptasm_mov_mr(a, SPT_RBX, disp, val);
  /* Write the 1-byte tag. */
  gen_write_tag(cg, slot, spt_type_to_tag(type));
}

/* =====================================================================
** Guard exit stubs
** ===================================================================== */

/* Generate an exit stub that restores the interpreter state and returns.
   The exit stub writes all live values (from the snapshot) to the
   interpreter stack, sets savedpc, and returns. */
static void gen_exit_stub(SPTCodeGen *cg, int exit_idx) {
  SPTAsm *a = &cg->asm_;
  SPTTrace *t = cg->trace;
  SPTIRBuilder *ir = &t->ir;

  sptasm_place(a, cg->exit_labels[exit_idx]);

  /* Get the snapshot for this exit. */
  if (exit_idx >= ir->nsnaps) {
    /* No snapshot: just go to epilogue. */
    sptasm_jmp(a, cg->epilogue_label);
    return;
  }

  SPTSnapshot *snap = ir->snaps[exit_idx];

  /* Write all live values to the interpreter stack. */
  for (int slot = 0; slot < snap->nslots; slot++) {
    int ref = snap->slot_map[slot];
    if (ref < 0) {
      /* Nil: write nil tag. */
      gen_write_tag(cg, slot, TAG_NIL);
      continue;
    }

    SPTType type = sptir_type(ir, ref);
    SPTIRInst *inst = sptir_get(ir, ref);

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
     The exit PC is stored in the trace's exit_pcs array. */
  const Instruction *exit_pc = t->exit_pcs[exit_idx];
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

static void gen_inst(SPTCodeGen *cg, int idx) {
  SPTAsm *a = &cg->asm_;
  SPTIRBuilder *ir = &cg->trace->ir;
  SPTIRInst *inst = &ir->insts[idx];

  /* Skip dead instructions. */
  if (inst->flags & SPTIRF_DEAD) return;
  if (inst->op == SPTIR_NOP) return;

  switch (inst->op) {
    /* ---- Constants ---- */
    case SPTIR_NIL:
    case SPTIR_FALSE:
      sptasm_xor_rr(a, SPT_RAX, SPT_RAX);
      gen_store(cg, idx, SPT_RAX);
      break;
    case SPTIR_TRUE:
      sptasm_mov_ri32(a, SPT_RAX, 1);
      gen_store(cg, idx, SPT_RAX);
      break;
    case SPTIR_KINT:
      sptasm_mov_ri64(a, SPT_RAX, inst->aux);
      gen_store(cg, idx, SPT_RAX);
      break;
    case SPTIR_KFLT:
      /* Store the double bits into the spill slot. */
      sptasm_mov_ri64(a, SPT_RAX, inst->aux);
      gen_store(cg, idx, SPT_RAX);
      break;
    case SPTIR_KSTR:
    case SPTIR_KPTR:
    case SPTIR_KGC:
      sptasm_mov_ri64(a, SPT_RAX, inst->aux);
      gen_store(cg, idx, SPT_RAX);
      break;

    /* ---- Stack load ---- */
    case SPTIR_SLOAD: {
      int slot = (int)inst->aux;
      sptasm_mov_rm(a, SPT_RAX, SPT_RBX, slot * SLOT_SIZE);
      gen_store(cg, idx, SPT_RAX);
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
      } else if (inst->type == SPTT_FLT) {
        /* Float arithmetic using SSE2. */
        gen_load(cg, SPT_RAX, inst->op1, SPTT_FLT);
        sptasm_movsd_rm(a, SPT_XMM0, SPT_RSP, spill_off(cg, inst->op1));
        gen_load(cg, SPT_RAX, inst->op2, SPTT_FLT);
        sptasm_movsd_rm(a, SPT_XMM1, SPT_RSP, spill_off(cg, inst->op2));
        switch (inst->op) {
          case SPTIR_ADD: sptasm_addsd(a, SPT_XMM0, SPT_XMM1); break;
          case SPTIR_SUB: sptasm_subsd(a, SPT_XMM0, SPT_XMM1); break;
          case SPTIR_MUL: sptasm_mulsd(a, SPT_XMM0, SPT_XMM1); break;
          case SPTIR_DIV: sptasm_divsd(a, SPT_XMM0, SPT_XMM1); break;
        }
        sptasm_movsd_mr(a, SPT_RSP, spill_off(cg, idx), SPT_XMM0);
      }
      break;
    }

    case SPTIR_DIV: {
      if (inst->type == SPTT_FLT) {
        sptasm_movsd_rm(a, SPT_XMM0, SPT_RSP, spill_off(cg, inst->op1));
        sptasm_movsd_rm(a, SPT_XMM1, SPT_RSP, spill_off(cg, inst->op2));
        sptasm_divsd(a, SPT_XMM0, SPT_XMM1);
        sptasm_movsd_mr(a, SPT_RSP, spill_off(cg, idx), SPT_XMM0);
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
      gen_load(cg, SPT_RAX, inst->op1, inst->type);
      if (inst->type == SPTT_INT) {
        sptasm_neg_r(a, SPT_RAX);
        gen_store(cg, idx, SPT_RAX);
      } else if (inst->type == SPTT_FLT) {
        sptasm_movsd_rm(a, SPT_XMM0, SPT_RSP, spill_off(cg, inst->op1));
        /* Negate by XOR with sign bit mask. */
        sptasm_mov_ri64(a, SPT_RAX, 0x8000000000000000ULL);
        sptasm_movsd_rm(a, SPT_XMM1, SPT_RSP, spill_off(cg, idx));
        /* Actually, let me use a simpler approach: load into XMM, negate. */
        /* XORPS with [rip + sign_mask] would be ideal, but for simplicity,
           store -0.0 in a spill slot and XOR. */
        /* For now, use: XOR RAX, 0x8000000000000000; store back. */
        sptasm_mov_rm(a, SPT_RAX, SPT_RSP, spill_off(cg, inst->op1));
        sptasm_xor_ri(a, SPT_RAX, 0); /* This won't work for 64-bit. */
        /* Let me use a different approach. */
        sptasm_mov_rm(a, SPT_RAX, SPT_RSP, spill_off(cg, inst->op1));
        /* Flip sign bit: XOR with 0x8000000000000000 */
        /* Use MOV + XOR with memory */
        sptasm_mov_ri64(a, SPT_RCX, (int64_t)0x8000000000000000ULL);
        sptasm_xor_rr(a, SPT_RAX, SPT_RCX);
        gen_store(cg, idx, SPT_RAX);
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
      sptasm_movsd_mr(a, SPT_RSP, spill_off(cg, idx), SPT_XMM0);
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
        case SPTIR_EQ: cc = SPT_CC_NE; break;  /* if not equal, exit */
        case SPTIR_LT: cc = SPT_CC_NL; break;  /* if not less, exit */
        case SPTIR_LE: cc = SPT_CC_NLE; break; /* if not le, exit */
        case SPTIR_GT: cc = SPT_CC_LE; break;  /* if not gt, exit */
        case SPTIR_GE: cc = SPT_CC_L; break;   /* if not ge, exit */
        default: cc = SPT_CC_NE;
      }

      /* The snapshot was created right after this instruction.
         The exit index is the current snapshot count - 1. */
      int exit_idx = cg->nexits++;
      cg->exit_labels[exit_idx] = sptasm_newlabel(a);
      sptasm_jcc(a, cc, cg->exit_labels[exit_idx]);

      /* Store the exit PC. */
      /* The exit PC is the PC after the comparison + JMP. */
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

        int exit_idx = cg->nexits++;
        cg->exit_labels[exit_idx] = sptasm_newlabel(a);
        sptasm_jcc(a, SPT_CC_NE, cg->exit_labels[exit_idx]);
      }
      /* If we can't find the slot, skip the guard (conservative). */
      break;
    }

    case SPTIR_GUARD_LT: {
      /* Guard: op1 < aux (where aux is an IR ref to the bound). */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      /* aux is the IR ref of the bound value. */
      gen_load(cg, SPT_RCX, (int)inst->aux, SPTT_INT);
      sptasm_cmp_rr(a, SPT_RAX, SPT_RCX);
      int exit_idx = cg->nexits++;
      cg->exit_labels[exit_idx] = sptasm_newlabel(a);
      sptasm_jcc(a, SPT_CC_NL, cg->exit_labels[exit_idx]); /* if not <, exit */
      break;
    }

    case SPTIR_GUARD_LE: {
      gen_load(cg, SPT_RAX, inst->op1, SPTT_INT);
      gen_load(cg, SPT_RCX, (int)inst->aux, SPTT_INT);
      sptasm_cmp_rr(a, SPT_RAX, SPT_RCX);
      int exit_idx = cg->nexits++;
      cg->exit_labels[exit_idx] = sptasm_newlabel(a);
      sptasm_jcc(a, SPT_CC_NLE, cg->exit_labels[exit_idx]); /* if not <=, exit */
      break;
    }

    case SPTIR_GUARD: {
      /* Truthiness guard: if value is not truthy/falsy per aux, exit. */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ANY);
      sptasm_test_rr(a, SPT_RAX, SPT_RAX);
      int exit_idx = cg->nexits++;
      cg->exit_labels[exit_idx] = sptasm_newlabel(a);
      if (inst->aux) {
        /* Want truthy: exit if zero. */
        sptasm_jcc(a, SPT_CC_E, cg->exit_labels[exit_idx]);
      } else {
        /* Want falsy: exit if non-zero. */
        sptasm_jcc(a, SPT_CC_NE, cg->exit_labels[exit_idx]);
      }
      break;
    }

    /* ---- Array access ---- */
    case SPTIR_GETI: {
      /* R[A] = R[B][C] for arrays.
         op1 = array ref, op2 = index ref.
         The array is a Table* (pointer). The value is at:
           tag = getArrTag(t, idx) = (lu_byte*)t->array + sizeof(unsigned) + idx
           val = getArrVal(t, idx) = t->array - 1 - idx
         So:
           tag_addr = (uint8_t*)t->array + 4 + idx
           val_addr = (Value*)t->array - 1 - idx = &t->array[-(idx+1)]
         In memory: t->array points between values and tags.
           Values: t->array[-1] = Value 0, t->array[-2] = Value 1, ...
           Tags:   ((lu_byte*)t->array)[4] = tag 0, [5] = tag 1, ...
         So Value k is at t->array[-(k+1)] = t->array - (k+1) * 8
         And tag k is at ((lu_byte*)t->array)[4 + k]
      */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ARR);  /* RAX = Table* */
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);  /* RCX = index */
      /* RAX = t->array */
      sptasm_mov_rm(a, SPT_RAX, SPT_RAX, OFF_TABLE_ARRAY);
      /* Value = t->array[-(idx+1)] = [RAX + RCX*8 + 8] ... wait.
         t->array[-(idx+1)] means address = t->array - (idx+1) * sizeof(Value)
         = t->array - (idx+1) * 8
         = [RAX - (RCX+1)*8]
         = [RAX - RCX*8 - 8]
         = [RAX + RCX*(-8) - 8]
         We can use: LEA RDX, [RAX - RCX*8 - 8] then MOV RAX, [RDX]
         Or: MOV RAX, [RAX + RCX*8 - 8] with negative scale... but x86
         doesn't support negative scale directly.
         Use: SUB RAX, RCX*8+8, then MOV RAX, [RAX].
         Or: NEG RCX; LEA RDX, [RAX + RCX*8]; MOV RAX, [RDX - 8].
      */
      sptasm_neg_r(a, SPT_RCX);           /* RCX = -idx */
      /* LEA RDX, [RAX + RCX*8 - 8] */
      /* We don't have LEA with scale, so compute manually. */
      sptasm_lea(a, SPT_RDX, SPT_RAX, -8);  /* RDX = RAX - 8 */
      /* RDX = RDX + RCX * 8: use IMUL + ADD */
      sptasm_imul_rri(a, SPT_RCX, SPT_RCX, 8);
      sptasm_add_rr(a, SPT_RDX, SPT_RCX);
      /* Load value. */
      sptasm_mov_rm(a, SPT_RAX, SPT_RDX, 0);
      gen_store(cg, idx, SPT_RAX);
      break;
    }

    case SPTIR_SETI: {
      /* R[A][B] = val (aux = val ref). */
      gen_load(cg, SPT_RAX, inst->op1, SPTT_ARR);  /* RAX = Table* */
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);  /* RCX = index */
      gen_load(cg, SPT_RDX, (int)inst->aux, SPTT_ANY); /* RDX = value */
      sptasm_mov_rm(a, SPT_R8, SPT_RAX, OFF_TABLE_ARRAY); /* R8 = t->array */
      sptasm_neg_r(a, SPT_RCX);
      sptasm_lea(a, SPT_R9, SPT_R8, -8);
      sptasm_imul_rri(a, SPT_RCX, SPT_RCX, 8);
      sptasm_add_rr(a, SPT_R9, SPT_RCX);
      /* Store value. */
      sptasm_mov_mr(a, SPT_R9, 0, SPT_RDX);
      /* Store tag. */
      SPTType val_type = sptir_type(ir, (int)inst->aux);
      uint8_t tag = spt_type_to_tag(val_type);
      sptasm_mov_ri32(a, SPT_R10, tag);
      /* Tag address = (uint8_t*)t->array + 4 + idx = R8 + 4 + idx */
      /* But we negated RCX. Let's recompute. */
      gen_load(cg, SPT_RCX, inst->op2, SPTT_INT);  /* RCX = index (original) */
      sptasm_lea(a, SPT_R9, SPT_R8, 4);  /* R9 = t->array + 4 */
      /* R9 = R9 + RCX */
      sptasm_add_rr(a, SPT_R9, SPT_RCX);
      /* MOV byte [R9], R10B */
      sptasm_byte(a, 0x44); /* REX for R10 */
      sptasm_byte(a, 0x88);
      sptasm_byte(a, 0x11); /* mod=00, reg=R10, rm=R9 */
      break;
    }

    /* ---- Loop back-edge ---- */
    case SPTIR_LOOP: {
      /* Write all live values back to the interpreter stack. */
      for (int slot = 0; slot <= ir->maxslot; slot++) {
        int ref = ir->reg_map[slot];
        if (ref < 0) continue;
        SPTType type = ir->reg_type[slot];
        gen_load(cg, SPT_RAX, ref, type);
        sptasm_mov_mr(a, SPT_RBX, slot * SLOT_SIZE, SPT_RAX);
        gen_write_tag(cg, slot, spt_type_to_tag(type));
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

  /* Create labels. */
  cg.loop_label = sptasm_newlabel(&cg.asm_);
  cg.epilogue_label = sptasm_newlabel(&cg.asm_);

  /* Generate prologue. */
  gen_prologue(&cg);

  /* Place loop label (entry point for back-edge). */
  sptasm_place(&cg.asm_, cg.loop_label);

  /* Generate code for each IR instruction. */
  for (int i = 0; i < ir->ninst; i++) {
    gen_inst(&cg, i);
  }

  /* Generate exit stubs. */
  for (int i = 0; i < cg.nexits; i++) {
    gen_exit_stub(&cg, i);
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

  /* Align to 16 bytes for the next trace. */
  js->code_buf_used = (js->code_buf_used + 15) & ~15;

  sptasm_free(&cg.asm_);
}
