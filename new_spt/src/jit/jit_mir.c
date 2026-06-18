/*
 * jit/jit_mir.c — MIR-backed JIT with bytecode -> MIR-IR lowering.
 *
 * For a hot prototype, emit_proto() builds a MIR function
 *
 *     int spt_fn_N(spt_State *L)
 *
 * that loads the frame base once (L->ci->base) and then translates each
 * bytecode instruction. Type-stable, hot operations are emitted as straight
 * native code with no calls:
 *
 *     OP_IADD R[a]=R[b]+R[c]   ->   MOV t0,[base + b*16]
 *                                    MOV t1,[base + c*16]
 *                                    ADD t0,t0,t1
 *                                    MOV [base + a*16],t0
 *                                    MOV [base + a*16 + 8], <SPT_TINT>
 *
 * This is exactly the speedup the typed/List opcodes were designed to expose:
 * no runtime tag dispatch on the hot path. Operations that must match the
 * interpreter across all value types (comparisons, truthiness, the return
 * sequence) are emitted as calls to the helpers in jit_rt.h.
 *
 * Coverage is intentionally staged: emit_proto pre-scans the bytecode and, if
 * it sees an opcode it does not yet lower, it BAILS — Proto::jit_entry stays
 * NULL and the always-correct interpreter keeps running that prototype. New
 * opcodes can be added to the lowering incrementally without risk.
 *
 * Compiled only in JIT builds (SPT_HAS_JIT).
 */
#include "jit/jit.h"

#ifdef SPT_HAS_JIT

#include "jit/jit_rt.h"
#include "spt/mem.h"
#include <stdarg.h>
#include "mir.h"
#include "mir-gen.h"
#include <stdlib.h>
#include <stdio.h>
#include <stddef.h>
#include <stdint.h>

struct spt_Jit {
  MIR_context_t ctx;
  int           enabled;
  unsigned      compiled;
};

/* TValue field geometry, taken from the real structs (never hard-coded). */
#define TV_SZ   ((MIR_disp_t)sizeof(TValue))
#define TT_OFF  ((MIR_disp_t)offsetof(TValue, tt))

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
void spt_jit_init(spt_State *L) {
  spt_Jit *j = (spt_Jit *)calloc(1, sizeof(*j));
  j->ctx = MIR_init();
  MIR_gen_init(j->ctx);
  MIR_gen_set_optimize_level(j->ctx, SPT_JIT_OPT_LEVEL);
  j->enabled  = 1;
  j->compiled = 0;
  L->G->jit = j;
}

void spt_jit_shutdown(spt_State *L) {
  spt_Jit *j = (spt_Jit *)L->G->jit;
  if (!j) return;
  MIR_gen_finish(j->ctx);
  MIR_finish(j->ctx);
  free(j);
  L->G->jit = NULL;
}

void spt_jit_set_enabled(spt_State *L, int enabled) {
  spt_Jit *j = (spt_Jit *)L->G->jit;
  if (j) j->enabled = (enabled != 0);
}

int spt_jit_is_enabled(spt_State *L) {
  spt_Jit *j = (spt_Jit *)L->G->jit;
  return j ? j->enabled : 0;
}

/* ------------------------------------------------------------------ */
/* Lowering                                                            */
/* ------------------------------------------------------------------ */

/* Is this opcode covered by the current lowering? */
static int op_supported(spt_OpCode op, Instr inst) {
  switch (op) {
    case OP_MOVE: case OP_LOADK: case OP_LOADINT:
    case OP_LOADBOOL: case OP_LOADNULL:
    case OP_IADD: case OP_ISUB: case OP_IMUL:
    case OP_IDIV: case OP_IMOD:
    case OP_FADD: case OP_FSUB: case OP_FMUL: case OP_FDIV:
    case OP_NEG: case OP_NOT: case OP_CAST:
    case OP_CONCAT: case OP_LEN:
    case OP_LT:   case OP_LE:   case OP_EQ:
    case OP_JMP:  case OP_TEST:
    /* generic arithmetic (runtime-typed) */
    case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD:
    /* globals + upvalues */
    case OP_GETGLOBAL: case OP_SETGLOBAL:
    case OP_GETUPVAL:  case OP_SETUPVAL:
    /* indexing + data structures */
    case OP_GETINDEX: case OP_SETINDEX:
    case OP_NEWLIST:  case OP_LISTPUSH:  case OP_NEWMAP:
    /* calls + closures (stack-frame management + upvalue binding) */
    case OP_CLOSURE:  case OP_CALL:
      return 1;
    case OP_RETURN:
      return SPT_GETB(inst) != 0;   /* multret form not lowered yet */
    default:
      return 0;
  }
}

/* Memory operands for register A's value field and tag byte. */
#define MEM_VAL(base, slot)  MIR_new_mem_op(c, MIR_T_I64, (MIR_disp_t)(slot)*TV_SZ,          (base), 0, 0)
#define MEM_FLT(base, slot)  MIR_new_mem_op(c, MIR_T_D,   (MIR_disp_t)(slot)*TV_SZ,          (base), 0, 0)
#define MEM_TT(base, slot)   MIR_new_mem_op(c, MIR_T_U8,  (MIR_disp_t)(slot)*TV_SZ + TT_OFF, (base), 0, 0)
#define ROP(r)  MIR_new_reg_op(c, (r))
#define IOP(i)  MIR_new_int_op(c, (int64_t)(i))
#define EMIT(insn)  MIR_append_insn(c, fitem, (insn))
#define INSN(...)   MIR_new_insn(c, __VA_ARGS__)

/* Build, load, link and generate native code for `p`; return the entry
 * pointer, or NULL to bail (leaving the prototype on the interpreter). */
static void *compile_proto(MIR_context_t c, Proto *p, unsigned id) {
  for (uint32_t i = 0; i < p->ncode; i++)
    if (!op_supported(SPT_OPCODE(p->code[i]), p->code[i])) return NULL;

  char name[32];
  snprintf(name, sizeof name, "spt_fn_%u", id);

  /* call signatures for the runtime helpers */
  MIR_type_t res_i64[1] = { MIR_T_I64 };
  MIR_var_t  v_cmp3[3]  = { {MIR_T_P,"L",0}, {MIR_T_P,"a",0}, {MIR_T_P,"b",0} };
  MIR_var_t  v_eq[2]    = { {MIR_T_P,"a",0}, {MIR_T_P,"b",0} };
  MIR_var_t  v_tru[1]   = { {MIR_T_P,"v",0} };
  MIR_var_t  v_ret[3]   = { {MIR_T_P,"L",0}, {MIR_T_I64,"a",0}, {MIR_T_I64,"n",0} };
  MIR_var_t  v_cc[4]    = { {MIR_T_P,"L",0}, {MIR_T_I64,"a",0}, {MIR_T_I64,"b",0}, {MIR_T_I64,"c",0} };
  MIR_var_t  v_2[2]     = { {MIR_T_P,"L",0}, {MIR_T_I64,"a",0} };

  char pn[7][24];
  snprintf(pn[0], 24, "p_cmp3_%u", id);
  snprintf(pn[1], 24, "p_eq_%u",   id);
  snprintf(pn[2], 24, "p_tru_%u",  id);
  snprintf(pn[3], 24, "p_ret_%u",  id);
  snprintf(pn[4], 24, "p_cc_%u",   id);
  snprintf(pn[5], 24, "p_2_%u",    id);

  MIR_module_t mod = MIR_new_module(c, name);
  MIR_item_t proto_cmp3 = MIR_new_proto_arr(c, pn[0], 1, res_i64, 3, v_cmp3);
  MIR_item_t proto_eq   = MIR_new_proto_arr(c, pn[1], 1, res_i64, 2, v_eq);
  MIR_item_t proto_tru  = MIR_new_proto_arr(c, pn[2], 1, res_i64, 1, v_tru);
  MIR_item_t proto_ret  = MIR_new_proto_arr(c, pn[3], 0, NULL,    3, v_ret);
  MIR_item_t proto_cc   = MIR_new_proto_arr(c, pn[4], 0, NULL,    4, v_cc);
  MIR_item_t proto_2    = MIR_new_proto_arr(c, pn[5], 0, NULL,    2, v_2);

  MIR_var_t fn_args[1] = { {MIR_T_P, "L", 0} };
  MIR_item_t fitem = MIR_new_func_arr(c, name, 1, res_i64, 1, fn_args);
  MIR_func_t func = MIR_get_item_func(c, fitem);

  MIR_reg_t vL    = MIR_reg(c, "L", func);
  MIR_reg_t vbase = MIR_new_func_reg(c, func, MIR_T_I64, "base");
  MIR_reg_t vci   = MIR_new_func_reg(c, func, MIR_T_I64, "ci");
  MIR_reg_t t0    = MIR_new_func_reg(c, func, MIR_T_I64, "t0");
  MIR_reg_t t1    = MIR_new_func_reg(c, func, MIR_T_I64, "t1");
  MIR_reg_t tr    = MIR_new_func_reg(c, func, MIR_T_I64, "tr");
  MIR_reg_t tf    = MIR_new_func_reg(c, func, MIR_T_I64, "tf");
  MIR_reg_t pa    = MIR_new_func_reg(c, func, MIR_T_I64, "pa");
  MIR_reg_t pb    = MIR_new_func_reg(c, func, MIR_T_I64, "pb");
  MIR_reg_t vd0   = MIR_new_func_reg(c, func, MIR_T_D,   "vd0");
  MIR_reg_t vd1   = MIR_new_func_reg(c, func, MIR_T_D,   "vd1");

  /* prologue: base = L->ci->base */
  EMIT(INSN(MIR_MOV, ROP(vci),
            MIR_new_mem_op(c, MIR_T_I64, (MIR_disp_t)offsetof(spt_State, ci), vL, 0, 0)));
  EMIT(INSN(MIR_MOV, ROP(vbase),
            MIR_new_mem_op(c, MIR_T_I64, (MIR_disp_t)offsetof(CallInfo, base), vci, 0, 0)));

  /* one MIR label per bytecode index, for branch targets */
  MIR_insn_t *labels = (MIR_insn_t *)malloc(p->ncode * sizeof(MIR_insn_t));
  for (uint32_t i = 0; i < p->ncode; i++) labels[i] = MIR_new_label(c);

  for (uint32_t i = 0; i < p->ncode; i++) {
    Instr inst = p->code[i];
    int A = (int)SPT_GETA(inst), B = (int)SPT_GETB(inst), Cc = (int)SPT_GETC(inst);
    EMIT(labels[i]);

    switch (SPT_OPCODE(inst)) {
      case OP_MOVE:
        EMIT(INSN(MIR_MOV, ROP(t0), MEM_VAL(vbase, B)));
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(t0)));
        EMIT(INSN(MIR_MOV, ROP(t1), MEM_TT(vbase, B)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;

      case OP_LOADINT:
        EMIT(INSN(MIR_MOV, ROP(t0), IOP(SPT_GETSBX(inst))));
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(t0)));
        EMIT(INSN(MIR_MOV, ROP(t1), IOP(SPT_TINT)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;

      case OP_LOADK: {
        TValue *kp = &p->k[SPT_GETBX(inst)];
        EMIT(INSN(MIR_MOV, ROP(pa), IOP((intptr_t)kp)));
        EMIT(INSN(MIR_MOV, ROP(t0), MIR_new_mem_op(c, MIR_T_I64, 0,      pa, 0, 0)));
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(t0)));
        EMIT(INSN(MIR_MOV, ROP(t1), MIR_new_mem_op(c, MIR_T_U8,  TT_OFF, pa, 0, 0)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;
      }

      case OP_IADD: case OP_ISUB: case OP_IMUL:
      case OP_IDIV: case OP_IMOD: {
        MIR_insn_code_t mop = (SPT_OPCODE(inst) == OP_IADD) ? MIR_ADD
                            : (SPT_OPCODE(inst) == OP_ISUB) ? MIR_SUB
                            : (SPT_OPCODE(inst) == OP_IMUL) ? MIR_MUL
                            : (SPT_OPCODE(inst) == OP_IDIV) ? MIR_DIV : MIR_MOD;
        EMIT(INSN(MIR_MOV, ROP(t0), MEM_VAL(vbase, B)));
        EMIT(INSN(MIR_MOV, ROP(t1), MEM_VAL(vbase, Cc)));
        EMIT(INSN(mop,     ROP(t0), ROP(t0), ROP(t1)));
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(t0)));
        EMIT(INSN(MIR_MOV, ROP(t1), IOP(SPT_TINT)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;
      }

      case OP_FADD: case OP_FSUB: case OP_FMUL: case OP_FDIV: {
        MIR_insn_code_t mop = (SPT_OPCODE(inst) == OP_FADD) ? MIR_DADD
                            : (SPT_OPCODE(inst) == OP_FSUB) ? MIR_DSUB
                            : (SPT_OPCODE(inst) == OP_FMUL) ? MIR_DMUL : MIR_DDIV;
        EMIT(INSN(MIR_DMOV, ROP(vd0), MEM_FLT(vbase, B)));
        EMIT(INSN(MIR_DMOV, ROP(vd1), MEM_FLT(vbase, Cc)));
        EMIT(INSN(mop,      ROP(vd0), ROP(vd0), ROP(vd1)));
        EMIT(INSN(MIR_DMOV, MEM_FLT(vbase, A), ROP(vd0)));
        EMIT(INSN(MIR_MOV,  ROP(t1), IOP(SPT_TFLOAT)));
        EMIT(INSN(MIR_MOV,  MEM_TT(vbase, A), ROP(t1)));
        break;
      }

      case OP_LOADBOOL: {
        EMIT(INSN(MIR_MOV, ROP(t0), IOP(SPT_GETB(inst))));
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(t0)));
        EMIT(INSN(MIR_MOV, ROP(t1), IOP(SPT_TBOOL)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;
      }

      case OP_LOADNULL: {
        int b = B;
        for (int j = 0; j <= b; j++) {
          EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A + j), IOP(0)));
          EMIT(INSN(MIR_MOV, ROP(t1), IOP(SPT_TNULL)));
          EMIT(INSN(MIR_MOV, MEM_TT(vbase, A + j), ROP(t1)));
        }
        break;
      }

      case OP_NEG: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_neg)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }

      case OP_NOT: {
        EMIT(INSN(MIR_ADD, ROP(pa), ROP(vbase), IOP((MIR_disp_t)B * TV_SZ)));
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_truthy_v)));
        EMIT(MIR_new_call_insn(c, 4, MIR_new_ref_op(c, proto_tru), ROP(tf),
                               ROP(tr), ROP(pa)));
        EMIT(INSN(MIR_XOR, ROP(tr), ROP(tr), IOP(1)));   /* !truthy = falsy */
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(tr)));
        EMIT(INSN(MIR_MOV, ROP(t1), IOP(SPT_TBOOL)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;
      }

      case OP_CAST: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_cast)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }

      case OP_CONCAT: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_concat)));
        EMIT(MIR_new_call_insn(c, 6, MIR_new_ref_op(c, proto_cc), ROP(tf),
                               ROP(vL), IOP(A), IOP(B), IOP(Cc)));
        break;
      }

      case OP_LEN: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_len)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }

      /* ---- generic arithmetic (runtime-typed operands) ---- */
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_MOD: {
        void *fn = (SPT_OPCODE(inst) == OP_ADD) ? (void *)&spt_jit_do_add
                 : (SPT_OPCODE(inst) == OP_SUB) ? (void *)&spt_jit_do_sub
                 : (SPT_OPCODE(inst) == OP_MUL) ? (void *)&spt_jit_do_mul
                 : (SPT_OPCODE(inst) == OP_DIV) ? (void *)&spt_jit_do_div
                                                : (void *)&spt_jit_do_mod;
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)fn)));
        EMIT(MIR_new_call_insn(c, 6, MIR_new_ref_op(c, proto_cc), ROP(tf),
                               ROP(vL), IOP(A), IOP(B), IOP(Cc)));
        break;
      }

      /* ---- globals ---- */
      case OP_GETGLOBAL: {
        const TValue *key = &p->k[SPT_GETBX(inst)];
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_getglobal)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP((intptr_t)key)));
        break;
      }
      case OP_SETGLOBAL: {
        const TValue *key = &p->k[SPT_GETBX(inst)];
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_setglobal)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP((intptr_t)key)));
        break;
      }

      /* ---- upvalues ---- */
      case OP_GETUPVAL: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_getupval)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }
      case OP_SETUPVAL: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_setupval)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }

      /* ---- indexing ---- */
      case OP_GETINDEX: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_getindex)));
        EMIT(MIR_new_call_insn(c, 6, MIR_new_ref_op(c, proto_cc), ROP(tf),
                               ROP(vL), IOP(A), IOP(B), IOP(Cc)));
        break;
      }
      case OP_SETINDEX: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_setindex)));
        EMIT(MIR_new_call_insn(c, 6, MIR_new_ref_op(c, proto_cc), ROP(tf),
                               ROP(vL), IOP(A), IOP(B), IOP(Cc)));
        break;
      }

      /* ---- list / map construction ---- */
      case OP_NEWLIST: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_newlist)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }
      case OP_LISTPUSH: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_listpush)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(B)));
        break;
      }
      case OP_NEWMAP: {
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_newmap)));
        EMIT(MIR_new_call_insn(c, 4, MIR_new_ref_op(c, proto_2), ROP(tf),
                               ROP(vL), IOP(A)));
        break;
      }

      case OP_EQ: case OP_LT: case OP_LE: {
        EMIT(INSN(MIR_ADD, ROP(pa), ROP(vbase), IOP((MIR_disp_t)B  * TV_SZ)));
        EMIT(INSN(MIR_ADD, ROP(pb), ROP(vbase), IOP((MIR_disp_t)Cc * TV_SZ)));
        if (SPT_OPCODE(inst) == OP_EQ) {
          EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_val_eq)));
          EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_eq), ROP(tf),
                                 ROP(tr), ROP(pa), ROP(pb)));
        } else {
          void *fn = (SPT_OPCODE(inst) == OP_LT) ? (void *)&spt_val_lt_rt
                                                 : (void *)&spt_val_le_rt;
          EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)fn)));
          EMIT(MIR_new_call_insn(c, 6, MIR_new_ref_op(c, proto_cmp3), ROP(tf),
                                 ROP(tr), ROP(vL), ROP(pa), ROP(pb)));
        }
        EMIT(INSN(MIR_MOV, MEM_VAL(vbase, A), ROP(tr)));
        EMIT(INSN(MIR_MOV, ROP(t1), IOP(SPT_TBOOL)));
        EMIT(INSN(MIR_MOV, MEM_TT(vbase, A), ROP(t1)));
        break;
      }

      case OP_TEST: {
        /* if truthy(R[A]) != C, skip the next instruction */
        uint32_t skip = i + 2;
        EMIT(INSN(MIR_ADD, ROP(pa), ROP(vbase), IOP((MIR_disp_t)A * TV_SZ)));
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_truthy_v)));
        EMIT(MIR_new_call_insn(c, 4, MIR_new_ref_op(c, proto_tru), ROP(tf),
                               ROP(tr), ROP(pa)));
        if (skip < p->ncode)
          EMIT(INSN(MIR_BNE, MIR_new_label_op(c, labels[skip]), ROP(tr), IOP(Cc)));
        break;
      }

      case OP_JMP: {
        uint32_t target = (i + 1) + (uint32_t)SPT_GETSBX(inst);
        EMIT(INSN(MIR_JMP, MIR_new_label_op(c, labels[target])));
        break;
      }

      /* ---- closures ---- */
      case OP_CLOSURE: {
        /* The nested prototype is fixed at compile time, so it travels as a
         * constant pointer (it is already GC-rooted via the parent Proto's
         * `p[]` array). The helper allocates the closure and wires up its
         * upvalues — capturing this frame's registers or inheriting from the
         * running closure as each UpvalDesc dictates. */
        Proto *np = p->p[SPT_GETBX(inst)];
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_closure)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP((intptr_t)np)));
        break;
      }

      /* ---- calls ---- */
      case OP_CALL: {
        /* spt_jit_do_call replicates the interpreter's OP_CALL (delimit args,
         * recurse via do_call, restore the frame extent). It can relocate the
         * value stack through a nested spt_checkstack, which invalidates every
         * raw stack pointer the prologue cached — so we reload `base` from the
         * (stable) CallInfo immediately afterwards. `ci` itself never moves;
         * only its `base` field is rewritten by relocate_stack. */
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_call)));
        EMIT(MIR_new_call_insn(c, 6, MIR_new_ref_op(c, proto_cc), ROP(tf),
                               ROP(vL), IOP(A), IOP(B), IOP(Cc)));
        EMIT(INSN(MIR_MOV, ROP(vbase),
                  MIR_new_mem_op(c, MIR_T_I64,
                                 (MIR_disp_t)offsetof(CallInfo, base), vci, 0, 0)));
        break;
      }

      case OP_RETURN: {
        int nret = B - 1;
        EMIT(INSN(MIR_MOV, ROP(tf), IOP((intptr_t)&spt_jit_do_return)));
        EMIT(MIR_new_call_insn(c, 5, MIR_new_ref_op(c, proto_ret), ROP(tf),
                               ROP(vL), IOP(A), IOP(nret)));
        EMIT(MIR_new_ret_insn(c, 1, IOP(0)));
        break;
      }

      default:
        free(labels);
        return NULL;   /* unreachable: pre-scan already guaranteed support */
    }
  }

  EMIT(MIR_new_ret_insn(c, 1, IOP(0)));   /* safety terminator */
  free(labels);

  MIR_finish_func(c);
  MIR_finish_module(c);

  MIR_load_module(c, mod);
  MIR_link(c, MIR_set_gen_interface, NULL);
  return MIR_gen(c, fitem);
}

void spt_jit_try_compile(spt_State *L, Proto *p) {
  spt_Jit *j = (spt_Jit *)L->G->jit;
  if (!j || !j->enabled || p->jit_entry) return;

  void *code = compile_proto(j->ctx, p, j->compiled);
  p->jit_entry = code;            /* NULL on bail -> interpreter keeps running */
  if (code) j->compiled++;
}

void spt_jit_enter(spt_State *L, Proto *p) {
  typedef int (*spt_native_fn)(spt_State *);
  ((spt_native_fn)p->jit_entry)(L);
}

#endif /* SPT_HAS_JIT */
