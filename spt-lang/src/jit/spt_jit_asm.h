/*
** spt_jit_asm.h — x86-64 Macro Assembler for the SPT Trace JIT
**
** A minimal x86-64 instruction encoder sufficient for JIT code generation.
** Supports integer and SSE2 floating-point operations, conditional jumps,
** and memory addressing relative to a base register.
**
** Target: Windows x64 and System V x64 (Linux/macOS).
** The JIT code uses a custom calling convention:
**   - R12 = lua_State *L
**   - RBX = base (stack slot 0, i.e., ci->func.p + 1)
**   - R13 = constants table (TValue *k)
**   - R14 = upvalue array or trace exit handler
**   - R15 = CallInfo *ci
**   - RAX, RCX, RDX, R8-R11 = value registers (scratch)
**   - XMM0-XMM5 = float value registers
*/
#ifndef SPT_JIT_ASM_H
#define SPT_JIT_ASM_H

#include <stdint.h>
#include <stddef.h>
#include "spt_jit.h"

/* =====================================================================
** x86-64 Registers
** ===================================================================== */

typedef enum {
  SPT_RAX = 0,
  SPT_RCX = 1,
  SPT_RDX = 2,
  SPT_RBX = 3,
  SPT_RSP = 4,
  SPT_RBP = 5,
  SPT_RSI = 6,
  SPT_RDI = 7,
  SPT_R8  = 8,
  SPT_R9  = 9,
  SPT_R10 = 10,
  SPT_R11 = 11,
  SPT_R12 = 12,
  SPT_R13 = 13,
  SPT_R14 = 14,
  SPT_R15 = 15,
} SPTReg;

/* =====================================================================
** Platform ABI (integer argument registers + shadow space)
**
** Windows x64:  args in RCX, RDX, R8, R9; 32-byte shadow space required.
** System V x64: args in RDI, RSI, RDX, RCX; no shadow space (128B red zone).
**
** The JIT trace entry has signature void(lua_State *L, CallInfo *ci), so
** only ARG0/ARG1 matter for the prologue; ARG2/ARG3 are used when the
** trace emits a C call (luaD_call etc.).
** ===================================================================== */
#if defined(_WIN32)
#  define SPT_ABI_ARG0   SPT_RCX
#  define SPT_ABI_ARG1   SPT_RDX
#  define SPT_ABI_ARG2   SPT_R8
#  define SPT_ABI_ARG3   SPT_R9
#  define SPT_ABI_SHADOW 32
#  define SPT_ABI_WIN64  1
#else
#  define SPT_ABI_ARG0   SPT_RDI
#  define SPT_ABI_ARG1   SPT_RSI
#  define SPT_ABI_ARG2   SPT_RDX
#  define SPT_ABI_ARG3   SPT_RCX
#  define SPT_ABI_SHADOW 0
#  define SPT_ABI_WIN64  0
#endif

/* XMM registers (encoded as 0-15) */
typedef enum {
  SPT_XMM0 = 0,
  SPT_XMM1 = 1,
  SPT_XMM2 = 2,
  SPT_XMM3 = 3,
  SPT_XMM4 = 4,
  SPT_XMM5 = 5,
  SPT_XMM6 = 6,
  SPT_XMM7 = 7,
} SPTXmmReg;

/* Condition codes for Jcc/SETcc */
typedef enum {
  SPT_CC_O  = 0x0,  /* overflow */
  SPT_CC_NO = 0x1,
  SPT_CC_B  = 0x2,  /* below (unsigned <) */
  SPT_CC_NB = 0x3,
  SPT_CC_E  = 0x4,  /* equal (==) */
  SPT_CC_NE = 0x5,
  SPT_CC_BE = 0x6,  /* below or equal (unsigned <=) */
  SPT_CC_NBE= 0x7,
  SPT_CC_S  = 0x8,  /* sign */
  SPT_CC_NS = 0x9,
  SPT_CC_P  = 0xa,  /* parity */
  SPT_CC_NP = 0xb,
  SPT_CC_L  = 0xc,  /* less (signed <) */
  SPT_CC_NL = 0xd,
  SPT_CC_LE = 0xe,  /* less or equal (signed <=) */
  SPT_CC_NLE= 0xf   /* greater (signed >) */
} SPTCC;

/* =====================================================================
** Assembler
** ===================================================================== */

struct SPTAsm {
  uint8_t *code;        /* code buffer */
  size_t size;          /* current code size */
  size_t cap;           /* buffer capacity */

  /* Pending label/relocation management */
  int32_t *labels;      /* label -> code offset */
  int nlabels;
  int label_cap;

  /* Relocations: patches to apply after code is finalized */
  struct SPTReloc {
    size_t code_offset;  /* where to patch */
    int32_t label;       /* target label */
    int type;            /* 0 = rel32, 1 = abs64 */
  } *relocs;
  int nrelocs;
  int reloc_cap;
};

/* Assembler API */
void sptasm_init(SPTAsm *a, size_t initial_cap);
void sptasm_free(SPTAsm *a);
void sptasm_reset(SPTAsm *a);

/* Get pointer to generated code. */
static inline const uint8_t *sptasm_code(const SPTAsm *a) { return a->code; }
static inline size_t sptasm_size(const SPTAsm *a) { return a->size; }

/* Labels */
int32_t sptasm_label(SPTAsm *a);  /* create and place a label */
int32_t sptasm_newlabel(SPTAsm *a);  /* create without placing */
void sptasm_place(SPTAsm *a, int32_t label);  /* place label at current position */

/* ---- Raw byte emission ---- */
void sptasm_byte(SPTAsm *a, uint8_t b);
void sptasm_word(SPTAsm *a, uint16_t w);
void sptasm_dword(SPTAsm *a, uint32_t d);
void sptasm_qword(SPTAsm *a, uint64_t q);

/* ---- REX prefix ---- */
void sptasm_rex(SPTAsm *a, int w, int r, int x, int b);

/* ---- Integer instructions ---- */

/* MOV reg, reg */
void sptasm_mov_rr(SPTAsm *a, SPTReg dst, SPTReg src);
/* MOV reg, imm64 */
void sptasm_mov_ri64(SPTAsm *a, SPTReg dst, int64_t imm);
/* MOV reg, imm32 (sign-extended) */
void sptasm_mov_ri32(SPTAsm *a, SPTReg dst, int32_t imm);
/* MOV reg, [base + disp] */
void sptasm_mov_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp);
void sptasm_mov_rm32(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp);
/* MOV [base + disp], reg */
void sptasm_mov_mr(SPTAsm *a, SPTReg base, int32_t disp, SPTReg src);
/* MOV [base + index*scale + disp], reg */
void sptasm_mov_mrs(SPTAsm *a, SPTReg base, SPTReg index, int scale, int32_t disp, SPTReg src);
/* MOV reg, [base + index*scale + disp] */
void sptasm_mov_rms(SPTAsm *a, SPTReg dst, SPTReg base, SPTReg index, int scale, int32_t disp);

/* LEA reg, [base + disp] */
void sptasm_lea(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp);

/* ADD/SUB/AND/OR/XOR reg, reg */
void sptasm_add_rr(SPTAsm *a, SPTReg dst, SPTReg src);
void sptasm_sub_rr(SPTAsm *a, SPTReg dst, SPTReg src);
void sptasm_and_rr(SPTAsm *a, SPTReg dst, SPTReg src);
void sptasm_or_rr(SPTAsm *a, SPTReg dst, SPTReg src);
void sptasm_xor_rr(SPTAsm *a, SPTReg dst, SPTReg src);

/* ADD/SUB/AND/OR/XOR reg, imm32 */
void sptasm_add_ri(SPTAsm *a, SPTReg dst, int32_t imm);
void sptasm_sub_ri(SPTAsm *a, SPTReg dst, int32_t imm);
void sptasm_and_ri(SPTAsm *a, SPTReg dst, int32_t imm);
void sptasm_or_ri(SPTAsm *a, SPTReg dst, int32_t imm);
void sptasm_xor_ri(SPTAsm *a, SPTReg dst, int32_t imm);

/* IMUL reg, reg (signed multiply) */
void sptasm_imul_rr(SPTAsm *a, SPTReg dst, SPTReg src);
/* IMUL reg, reg, imm32 */
void sptasm_imul_rri(SPTAsm *a, SPTReg dst, SPTReg src, int32_t imm);

/* IDIV reg (divides RDX:RAX by reg; quotient in RAX, remainder in RDX) */
void sptasm_idiv_r(SPTAsm *a, SPTReg src);
/* DIV reg (unsigned) */
void sptasm_div_r(SPTAsm *a, SPTReg src);

/* CQO (sign-extend RAX into RDX:RAX) */
void sptasm_cqo(SPTAsm *a);
/* CDQ (sign-extend EAX into EDX) */
void sptasm_cdq(SPTAsm *a);

/* NEG/NOT reg */
void sptasm_neg_r(SPTAsm *a, SPTReg r);
void sptasm_not_r(SPTAsm *a, SPTReg r);

/* SHL/SHR/SAR reg, cl */
void sptasm_shl_cl(SPTAsm *a, SPTReg r);
void sptasm_shr_cl(SPTAsm *a, SPTReg r);
void sptasm_sar_cl(SPTAsm *a, SPTReg r);
/* SHL/SHR/SAR reg, imm8 */
void sptasm_shl_ri(SPTAsm *a, SPTReg r, uint8_t count);
void sptasm_shr_ri(SPTAsm *a, SPTReg r, uint8_t count);
void sptasm_sar_ri(SPTAsm *a, SPTReg r, uint8_t count);

/* CMP reg, reg */
void sptasm_cmp_rr(SPTAsm *a, SPTReg r1, SPTReg r2);
/* CMP reg, imm32 */
void sptasm_cmp_ri(SPTAsm *a, SPTReg r, int32_t imm);
/* TEST reg, reg */
void sptasm_test_rr(SPTAsm *a, SPTReg r1, SPTReg r2);

/* SETcc reg8 (set byte on condition) */
void sptasm_setcc(SPTAsm *a, SPTCC cc, SPTReg r);
/* MOVZX reg, reg8 (zero-extend byte to 64-bit) */
void sptasm_movzx_r8(SPTAsm *a, SPTReg dst, SPTReg src);
/* MOVZX reg, byte [base+disp] (zero-extend a memory byte to 64-bit) */
void sptasm_movzx_rm8(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp);

/* ---- Control flow ---- */

/* JMP label (rel32) */
void sptasm_jmp(SPTAsm *a, int32_t label);
/* Jcc label (rel32) */
void sptasm_jcc(SPTAsm *a, SPTCC cc, int32_t label);
/* JMP reg (indirect) */
void sptasm_jmp_r(SPTAsm *a, SPTReg r);
/* CALL reg */
void sptasm_call_r(SPTAsm *a, SPTReg r);
/* RET */
void sptasm_ret(SPTAsm *a);

/* ---- Stack frame ---- */

/* PUSH/POP reg */
void sptasm_push(SPTAsm *a, SPTReg r);
void sptasm_pop(SPTAsm *a, SPTReg r);
/* SUB RSP, imm32 */
void sptasm_sub_rsp(SPTAsm *a, int32_t imm);
/* ADD RSP, imm32 */
void sptasm_add_rsp(SPTAsm *a, int32_t imm);

/* ---- SSE2 floating-point instructions ---- */

/* MOVSD xmm, [base + disp] */
void sptasm_movsd_rm(SPTAsm *a, SPTXmmReg dst, SPTReg base, int32_t disp);
/* MOVSD [base + disp], xmm */
void sptasm_movsd_mr(SPTAsm *a, SPTReg base, int32_t disp, SPTXmmReg src);
/* MOVSD xmm, xmm */
void sptasm_movsd_rr(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
/* ADDSD xmm, xmm */
void sptasm_addsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
/* SUBSD xmm, xmm */
void sptasm_subsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
/* MULSD xmm, xmm */
void sptasm_mulsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
/* DIVSD xmm, xmm */
void sptasm_divsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
/* XORPS xmm, xmm (zero) */
void sptasm_xorps(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
void sptasm_andpd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
void sptasm_andnpd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
void sptasm_orpd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src);
void sptasm_cmpsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src, uint8_t imm8);
/* UCOMISD xmm, xmm (compare) */
void sptasm_ucomisd(SPTAsm *a, SPTXmmReg r1, SPTXmmReg r2);
/* CVTSI2SD xmm, reg (int to float) */
void sptasm_cvtsi2sd(SPTAsm *a, SPTXmmReg dst, SPTReg src);
/* CVTSD2SI reg, xmm (float to int, truncation) */
void sptasm_cvtsd2si(SPTAsm *a, SPTReg dst, SPTXmmReg src);
void sptasm_movq_xmm_to_gpr(SPTAsm *a, SPTReg dst, SPTXmmReg src);
void sptasm_movq_gpr_to_xmm(SPTAsm *a, SPTXmmReg dst, SPTReg src);
/* CVTSS2SD / CVTSD2SS not needed; we use double everywhere */

/* ---- Memory management for executable code ---- */

/* Allocate executable memory of given size. Returns pointer or NULL. */
void *sptjit_alloc_exec(size_t size);
/* Free executable memory. */
void sptjit_free_exec(void *ptr, size_t size);
/* Set memory permissions to read/write (for patching). */
void sptjit_protect_rw(void *ptr, size_t size);
/* Set memory permissions to read/execute. */
void sptjit_protect_rx(void *ptr, size_t size);

/* Finalize: apply relocations and make code executable.
   Returns pointer to executable code, or NULL on failure. */
void *sptasm_finalize(SPTAsm *a, void *exec_mem, size_t exec_size);

#endif /* SPT_JIT_ASM_H */
