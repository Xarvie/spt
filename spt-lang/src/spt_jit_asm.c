/*
** spt_jit_asm.c — x86-64 Macro Assembler Implementation
**
** Encodes x86-64 instructions into a code buffer. Supports integer ops,
** SSE2 float ops, memory addressing, conditional jumps, and labels.
*/
#include "spt_jit_asm.h"
#include <stdlib.h>
#include <string.h>

/* Windows vs POSIX */
#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

/* =====================================================================
** Assembler init/free
** ===================================================================== */

void sptasm_init(SPTAsm *a, size_t initial_cap) {
  memset(a, 0, sizeof(*a));
  if (initial_cap < 256)
    initial_cap = 256;
  a->code = (uint8_t *)malloc(initial_cap);
  a->cap = initial_cap;
  a->label_cap = 64;
  a->labels = (int32_t *)calloc(a->label_cap, sizeof(int32_t));
  a->reloc_cap = 64;
  a->relocs = (struct SPTReloc *)calloc(a->reloc_cap, sizeof(struct SPTReloc));
  for (int i = 0; i < a->label_cap; i++)
    a->labels[i] = -1;
}

void sptasm_free(SPTAsm *a) {
  free(a->code);
  free(a->labels);
  free(a->relocs);
  memset(a, 0, sizeof(*a));
}

void sptasm_reset(SPTAsm *a) {
  a->size = 0;
  a->nlabels = 0;
  a->nrelocs = 0;
  for (int i = 0; i < a->label_cap; i++)
    a->labels[i] = -1;
}

/* Ensure code buffer has space. */
static void asm_ensure(SPTAsm *a, size_t need) {
  if (a->size + need <= a->cap)
    return;
  while (a->cap < a->size + need)
    a->cap *= 2;
  a->code = (uint8_t *)realloc(a->code, a->cap);
}

/* =====================================================================
** Raw emission
** ===================================================================== */

void sptasm_byte(SPTAsm *a, uint8_t b) {
  asm_ensure(a, 1);
  a->code[a->size++] = b;
}

void sptasm_word(SPTAsm *a, uint16_t w) {
  asm_ensure(a, 2);
  a->code[a->size++] = (uint8_t)(w & 0xff);
  a->code[a->size++] = (uint8_t)(w >> 8);
}

void sptasm_dword(SPTAsm *a, uint32_t d) {
  asm_ensure(a, 4);
  a->code[a->size++] = (uint8_t)(d & 0xff);
  a->code[a->size++] = (uint8_t)(d >> 8);
  a->code[a->size++] = (uint8_t)(d >> 16);
  a->code[a->size++] = (uint8_t)(d >> 24);
}

void sptasm_qword(SPTAsm *a, uint64_t q) {
  asm_ensure(a, 8);
  for (int i = 0; i < 8; i++)
    a->code[a->size++] = (uint8_t)(q >> (i * 8));
}

/* =====================================================================
** Labels and relocations
** ===================================================================== */

int32_t sptasm_newlabel(SPTAsm *a) {
  int32_t l = a->nlabels++;
  if (l >= a->label_cap) {
    a->label_cap *= 2;
    a->labels = (int32_t *)realloc(a->labels, a->label_cap * sizeof(int32_t));
    for (int i = l; i < a->label_cap; i++)
      a->labels[i] = -1;
  }
  a->labels[l] = -1;
  return l;
}

int32_t sptasm_label(SPTAsm *a) {
  int32_t l = sptasm_newlabel(a);
  sptasm_place(a, l);
  return l;
}

void sptasm_place(SPTAsm *a, int32_t label) {
  if (label < 0 || label >= a->nlabels)
    return;
  a->labels[label] = (int32_t)a->size;
}

/* Add a relocation. */
static void add_reloc(SPTAsm *a, int type, int32_t label) {
  if (a->nrelocs >= a->reloc_cap) {
    a->reloc_cap *= 2;
    a->relocs = (struct SPTReloc *)realloc(a->relocs, a->reloc_cap * sizeof(struct SPTReloc));
  }
  a->relocs[a->nrelocs].code_offset = a->size;
  a->relocs[a->nrelocs].label = label;
  a->relocs[a->nrelocs].type = type;
  a->nrelocs++;
}

/* =====================================================================
** REX prefix and ModR/M encoding
** ===================================================================== */

void sptasm_rex(SPTAsm *a, int w, int r, int x, int b) {
  uint8_t rex = 0x40;
  if (w)
    rex |= 0x08;
  if (r)
    rex |= 0x04;
  if (x)
    rex |= 0x02;
  if (b)
    rex |= 0x01;
  /* Only emit REX if needed. */
  if (rex != 0x40 || w)
    sptasm_byte(a, rex);
}

/* Emit REX.W for 64-bit operations, with reg and rm extension bits. */
static void emit_rex_rm(SPTAsm *a, int w, SPTReg reg, SPTReg rm) {
  int r = (reg >> 3) & 1;
  int b = (rm >> 3) & 1;
  sptasm_rex(a, w, r, 0, b);
}

/* Emit ModR/M byte. */
static void emit_modrm(SPTAsm *a, int mod, int reg, int rm) {
  sptasm_byte(a, (uint8_t)((mod << 6) | ((reg & 7) << 3) | (rm & 7)));
}

/* Emit SIB byte. */
static void emit_sib(SPTAsm *a, int scale, int index, int base) {
  sptasm_byte(a, (uint8_t)((scale << 6) | ((index & 7) << 3) | (base & 7)));
}

/* Encode [base + disp] memory operand. Handles RSP/R12 (needs SIB) and
   RBP/R13 (needs disp8 even for 0 displacement). */
static void emit_mem(SPTAsm *a, SPTReg reg, SPTReg base, int32_t disp) {
  int mod;
  int need_sib = (base & 7) == 4;  /* RSP/R12 */
  int need_disp = (base & 7) == 5; /* RBP/R13: needs displacement */

  if (disp == 0 && !need_disp) {
    mod = 0;
  } else if (disp >= -128 && disp <= 127) {
    mod = 1;
  } else {
    mod = 2;
  }

  emit_modrm(a, mod, reg, base);

  if (need_sib) {
    emit_sib(a, 0, 4, base); /* scale=1, index=none(RSP), base */
  }

  if (mod == 1) {
    sptasm_byte(a, (uint8_t)(disp & 0xff));
  } else if (mod == 2) {
    sptasm_dword(a, (uint32_t)disp);
  }
}

/* Encode [base + index*scale + disp] memory operand with SIB.
   Used by mov_rms/mov_mrs which emit their own REX and opcode. */

/* =====================================================================
** Integer instructions
** ===================================================================== */

void sptasm_mov_rr(SPTAsm *a, SPTReg dst, SPTReg src) {
  /* MOV r/m64, r64 (0x89): reg field = src, rm field = dst.
     REX.R extends reg (src), REX.B extends rm (dst). */
  emit_rex_rm(a, 1, src, dst);
  sptasm_byte(a, 0x89); /* MOV r/m64, r64 */
  emit_modrm(a, 3, src, dst);
}

void sptasm_mov_ri64(SPTAsm *a, SPTReg dst, int64_t imm) {
  /* Use sign-extended MOV r/m64, imm32 (7 bytes) when the value fits in int32,
     instead of MOV r64, imm64 (10 bytes). This saves 3 bytes per constant load
     and improves instruction density for KINT-heavy traces (e.g. array index
     constants 0..7 in unrolled GETI sequences). */
  if (imm == (int64_t)(int32_t)imm) {
    sptasm_rex(a, 1, 0, 0, (dst >> 3) & 1); /* REX.W for 64-bit sign-extension */
    sptasm_byte(a, 0xC7);                   /* MOV r/m64, imm32 */
    emit_modrm(a, 3, 0, dst);
    sptasm_dword(a, (uint32_t)(int32_t)imm);
  } else {
    sptasm_rex(a, 1, 0, 0, (dst >> 3) & 1);
    sptasm_byte(a, 0xB8 + (dst & 7)); /* MOV r64, imm64 */
    sptasm_qword(a, (uint64_t)imm);
  }
}

void sptasm_mov_ri32(SPTAsm *a, SPTReg dst, int32_t imm) {
  sptasm_rex(a, 0, 0, 0, (dst >> 3) & 1);
  sptasm_byte(a, 0xC7); /* MOV r/m32, imm32 (use 32-bit to avoid sign-extension issues) */
  emit_modrm(a, 3, 0, dst);
  sptasm_dword(a, (uint32_t)imm);
}

void sptasm_mov_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  emit_rex_rm(a, 1, dst, base);
  sptasm_byte(a, 0x8B); /* MOV r64, r/m64 */
  emit_mem(a, dst, base, disp);
}

/* 32-bit load: MOV r32, r/m32. In x86-64 this zero-extends into the full
   64-bit destination, so it is the correct way to read a 4-byte field (e.g.
   Table.loglen) without dragging in the adjacent 4 bytes. */
void sptasm_mov_rm32(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  emit_rex_rm(a, 0, dst, base);
  sptasm_byte(a, 0x8B);
  emit_mem(a, dst, base, disp);
}

void sptasm_mov_mr(SPTAsm *a, SPTReg base, int32_t disp, SPTReg src) {
  emit_rex_rm(a, 1, src, base);
  sptasm_byte(a, 0x89); /* MOV r/m64, r64 */
  emit_mem(a, src, base, disp);
}

void sptasm_mov_mrs(SPTAsm *a, SPTReg base, SPTReg index, int scale, int32_t disp, SPTReg src) {
  int r = (src >> 3) & 1;
  int x = (index >> 3) & 1;
  int b = (base >> 3) & 1;
  sptasm_rex(a, 1, r, x, b);
  sptasm_byte(a, 0x89); /* MOV r/m64, r64 */

  int mod;
  if (disp == 0 && (base & 7) != 5)
    mod = 0;
  else if (disp >= -128 && disp <= 127)
    mod = 1;
  else
    mod = 2;

  emit_modrm(a, mod, src, 4); /* SIB */
  emit_sib(a, scale, index, base);

  if (mod == 1)
    sptasm_byte(a, (uint8_t)(disp & 0xff));
  else if (mod == 2)
    sptasm_dword(a, (uint32_t)disp);
}

/* The mov_mrs/mov_rms need a different approach. Let me redo them. */
void sptasm_mov_rms(SPTAsm *a, SPTReg dst, SPTReg base, SPTReg index, int scale, int32_t disp) {
  int r = (dst >> 3) & 1;
  int x = (index >> 3) & 1;
  int b = (base >> 3) & 1;
  sptasm_rex(a, 1, r, x, b);
  sptasm_byte(a, 0x8B); /* MOV r64, r/m64 */

  int mod;
  if (disp == 0 && (base & 7) != 5)
    mod = 0;
  else if (disp >= -128 && disp <= 127)
    mod = 1;
  else
    mod = 2;

  emit_modrm(a, mod, dst, 4); /* SIB */
  emit_sib(a, scale, index, base);

  if (mod == 1)
    sptasm_byte(a, (uint8_t)(disp & 0xff));
  else if (mod == 2)
    sptasm_dword(a, (uint32_t)disp);
}

void sptasm_lea(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  emit_rex_rm(a, 1, dst, base);
  sptasm_byte(a, 0x8D); /* LEA r64, m */
  emit_mem(a, dst, base, disp);
}

/* Generic r/r arithmetic: opcode is 2 bytes (0x00-0x3F range for standard ops).
   We use the 0xXX + rd form: opcode byte, then ModR/M with reg in reg field. */
static void arith_rr(SPTAsm *a, uint8_t op, SPTReg dst, SPTReg src) {
  /* For ADD(01), OR(09), AND(21), SUB(29), XOR(31): opcode = base, reg=src, rm=dst */
  emit_rex_rm(a, 1, src, dst);
  sptasm_byte(a, op);
  emit_modrm(a, 3, src, dst);
}

void sptasm_add_rr(SPTAsm *a, SPTReg dst, SPTReg src) { arith_rr(a, 0x01, dst, src); }

void sptasm_or_rr(SPTAsm *a, SPTReg dst, SPTReg src) { arith_rr(a, 0x09, dst, src); }

void sptasm_and_rr(SPTAsm *a, SPTReg dst, SPTReg src) { arith_rr(a, 0x21, dst, src); }

void sptasm_sub_rr(SPTAsm *a, SPTReg dst, SPTReg src) { arith_rr(a, 0x29, dst, src); }

void sptasm_xor_rr(SPTAsm *a, SPTReg dst, SPTReg src) { arith_rr(a, 0x31, dst, src); }

/* Generic r/m arithmetic: dst = dst <op> [base+disp].
   Uses the "r, r/m" direction (opcode + 2 from the "r/m, r" form) so the
   memory operand is the source. This enables a single memory-source
   instruction (e.g. add r15, [rsp+0x170]) instead of load+op (mov+add),
   saving one instruction and enabling micro-fusion. */
static void arith_rm(SPTAsm *a, uint8_t op, SPTReg dst, SPTReg base, int32_t disp) {
  emit_rex_rm(a, 1, dst, base);
  sptasm_byte(a, op);
  emit_mem(a, dst, base, disp);
}

void sptasm_add_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  arith_rm(a, 0x03, dst, base, disp);
}

void sptasm_or_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  arith_rm(a, 0x0B, dst, base, disp);
}

void sptasm_and_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  arith_rm(a, 0x23, dst, base, disp);
}

void sptasm_sub_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  arith_rm(a, 0x2B, dst, base, disp);
}

void sptasm_xor_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  arith_rm(a, 0x33, dst, base, disp);
}

void sptasm_imul_rm(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  emit_rex_rm(a, 1, dst, base);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xAF); /* IMUL r64, r/m64 */
  emit_mem(a, dst, base, disp);
}

/* Generic r/imm32 arithmetic: 0x81 /digit, imm32. */
static void arith_ri(SPTAsm *a, uint8_t subop, SPTReg dst, int32_t imm) {
  sptasm_rex(a, 1, 0, 0, (dst >> 3) & 1);
  if (imm >= -128 && imm <= 127) {
    sptasm_byte(a, 0x83); /* op r/m64, imm8 */
    emit_modrm(a, 3, subop, dst);
    sptasm_byte(a, (uint8_t)(imm & 0xff));
  } else {
    sptasm_byte(a, 0x81); /* op r/m64, imm32 */
    emit_modrm(a, 3, subop, dst);
    sptasm_dword(a, (uint32_t)imm);
  }
}

void sptasm_add_ri(SPTAsm *a, SPTReg dst, int32_t imm) { arith_ri(a, 0, dst, imm); }

void sptasm_or_ri(SPTAsm *a, SPTReg dst, int32_t imm) { arith_ri(a, 1, dst, imm); }

void sptasm_and_ri(SPTAsm *a, SPTReg dst, int32_t imm) { arith_ri(a, 4, dst, imm); }

void sptasm_sub_ri(SPTAsm *a, SPTReg dst, int32_t imm) { arith_ri(a, 5, dst, imm); }

void sptasm_xor_ri(SPTAsm *a, SPTReg dst, int32_t imm) { arith_ri(a, 6, dst, imm); }

void sptasm_imul_rr(SPTAsm *a, SPTReg dst, SPTReg src) {
  emit_rex_rm(a, 1, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xAF); /* IMUL r64, r/m64 */
  emit_modrm(a, 3, dst, src);
}

void sptasm_imul_rri(SPTAsm *a, SPTReg dst, SPTReg src, int32_t imm) {
  emit_rex_rm(a, 1, dst, src);
  if (imm >= -128 && imm <= 127) {
    sptasm_byte(a, 0x6B); /* IMUL r64, r/m64, imm8 */
    emit_modrm(a, 3, dst, src);
    sptasm_byte(a, (uint8_t)(imm & 0xff));
  } else {
    sptasm_byte(a, 0x69); /* IMUL r64, r/m64, imm32 */
    emit_modrm(a, 3, dst, src);
    sptasm_dword(a, (uint32_t)imm);
  }
}

void sptasm_idiv_r(SPTAsm *a, SPTReg src) {
  sptasm_rex(a, 1, 0, 0, (src >> 3) & 1);
  sptasm_byte(a, 0xF7); /* IDIV r/m64 */
  emit_modrm(a, 3, 7, src);
}

void sptasm_div_r(SPTAsm *a, SPTReg src) {
  sptasm_rex(a, 1, 0, 0, (src >> 3) & 1);
  sptasm_byte(a, 0xF7); /* DIV r/m64 (unsigned) */
  emit_modrm(a, 3, 6, src);
}

void sptasm_cqo(SPTAsm *a) {
  sptasm_byte(a, 0x48);
  sptasm_byte(a, 0x99); /* CQO */
}

void sptasm_cdq(SPTAsm *a) { sptasm_byte(a, 0x99); /* CDQ */ }

void sptasm_neg_r(SPTAsm *a, SPTReg r) {
  sptasm_rex(a, 1, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0xF7);
  emit_modrm(a, 3, 3, r);
}

void sptasm_not_r(SPTAsm *a, SPTReg r) {
  sptasm_rex(a, 1, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0xF7);
  emit_modrm(a, 3, 2, r);
}

/* Shift by CL: 0xD3 /digit */
static void shift_cl(SPTAsm *a, uint8_t subop, SPTReg r) {
  sptasm_rex(a, 1, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0xD3);
  emit_modrm(a, 3, subop, r);
}

/* Shift by imm8: 0xC1 /digit, imm8 */
static void shift_ri(SPTAsm *a, uint8_t subop, SPTReg r, uint8_t count) {
  sptasm_rex(a, 1, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0xC1);
  emit_modrm(a, 3, subop, r);
  sptasm_byte(a, count);
}

void sptasm_shl_cl(SPTAsm *a, SPTReg r) { shift_cl(a, 4, r); }

void sptasm_shr_cl(SPTAsm *a, SPTReg r) { shift_cl(a, 5, r); }

void sptasm_sar_cl(SPTAsm *a, SPTReg r) { shift_cl(a, 7, r); }

void sptasm_shl_ri(SPTAsm *a, SPTReg r, uint8_t c) { shift_ri(a, 4, r, c); }

void sptasm_shr_ri(SPTAsm *a, SPTReg r, uint8_t c) { shift_ri(a, 5, r, c); }

void sptasm_sar_ri(SPTAsm *a, SPTReg r, uint8_t c) { shift_ri(a, 7, r, c); }

void sptasm_cmp_rr(SPTAsm *a, SPTReg r1, SPTReg r2) {
  emit_rex_rm(a, 1, r2, r1);
  sptasm_byte(a, 0x39); /* CMP r/m64, r64 */
  emit_modrm(a, 3, r2, r1);
}

void sptasm_cmp_ri(SPTAsm *a, SPTReg r, int32_t imm) {
  sptasm_rex(a, 1, 0, 0, (r >> 3) & 1);
  if (imm >= -128 && imm <= 127) {
    sptasm_byte(a, 0x83);
    emit_modrm(a, 3, 7, r);
    sptasm_byte(a, (uint8_t)(imm & 0xff));
  } else {
    sptasm_byte(a, 0x81);
    emit_modrm(a, 3, 7, r);
    sptasm_dword(a, (uint32_t)imm);
  }
}

void sptasm_test_rr(SPTAsm *a, SPTReg r1, SPTReg r2) {
  emit_rex_rm(a, 1, r2, r1);
  sptasm_byte(a, 0x85); /* TEST r/m64, r64 */
  emit_modrm(a, 3, r2, r1);
}

void sptasm_setcc(SPTAsm *a, SPTCC cc, SPTReg r) {
  sptasm_rex(a, 0, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x90 + (uint8_t)cc); /* SETcc r/m8 */
  emit_modrm(a, 3, 0, r);
}

void sptasm_movzx_r8(SPTAsm *a, SPTReg dst, SPTReg src) {
  sptasm_rex(a, 1, (dst >> 3) & 1, 0, (src >> 3) & 1);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xB6); /* MOVZX r64, r/m8 */
  emit_modrm(a, 3, dst, src);
}

void sptasm_movzx_rm8(SPTAsm *a, SPTReg dst, SPTReg base, int32_t disp) {
  /* MOVZX r32, byte [base+disp]: 0F B6 /r. No REX.W -> 32-bit dst, which
     zero-extends to the full 64-bit register (the byte is 0..255). */
  emit_rex_rm(a, 0, dst, base);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xB6);
  emit_mem(a, dst, base, disp);
}

/* MOVZX r32, byte [base + index*scale + disp]: 0F B6 /r with SIB.
   Used by GETI tag guards: movzx edx, byte[array + idx*1 + 4]. */
void sptasm_movzx_rm8s(SPTAsm *a, SPTReg dst, SPTReg base, SPTReg index, int scale, int32_t disp) {
  int r = (dst >> 3) & 1;
  int x = (index >> 3) & 1;
  int b = (base >> 3) & 1;
  sptasm_rex(a, 0, r, x, b);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xB6);
  int mod;
  if (disp == 0 && (base & 7) != 5)
    mod = 0;
  else if (disp >= -128 && disp <= 127)
    mod = 1;
  else
    mod = 2;
  emit_modrm(a, mod, dst, 4);
  emit_sib(a, scale, index, base);
  if (mod == 1)
    sptasm_byte(a, (uint8_t)(disp & 0xff));
  else if (mod == 2)
    sptasm_dword(a, (uint32_t)disp);
}

/* =====================================================================
** Control flow
** ===================================================================== */

void sptasm_jmp(SPTAsm *a, int32_t label) {
  sptasm_byte(a, 0xE9); /* JMP rel32 */
  add_reloc(a, 0, label);
  sptasm_dword(a, 0); /* placeholder */
}

void sptasm_jcc(SPTAsm *a, SPTCC cc, int32_t label) {
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x80 + (uint8_t)cc); /* Jcc rel32 */
  add_reloc(a, 0, label);
  sptasm_dword(a, 0);
}

void sptasm_jmp_r(SPTAsm *a, SPTReg r) {
  sptasm_rex(a, 0, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0xFF); /* JMP r/m64 */
  emit_modrm(a, 3, 4, r);
}

void sptasm_call_r(SPTAsm *a, SPTReg r) {
  sptasm_rex(a, 0, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0xFF); /* CALL r/m64 */
  emit_modrm(a, 3, 2, r);
}

void sptasm_ret(SPTAsm *a) { sptasm_byte(a, 0xC3); }

void sptasm_push(SPTAsm *a, SPTReg r) {
  sptasm_rex(a, 0, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0x50 + (r & 7));
}

void sptasm_pop(SPTAsm *a, SPTReg r) {
  sptasm_rex(a, 0, 0, 0, (r >> 3) & 1);
  sptasm_byte(a, 0x58 + (r & 7));
}

void sptasm_sub_rsp(SPTAsm *a, int32_t imm) { sptasm_sub_ri(a, SPT_RSP, imm); }

void sptasm_add_rsp(SPTAsm *a, int32_t imm) { sptasm_add_ri(a, SPT_RSP, imm); }

/* =====================================================================
** SSE2 floating-point instructions
** ===================================================================== */

/* Emit REX for XMM register + memory/register operand.
   For XMM, the high bit goes into REX.R. */
static void emit_rex_xmm(SPTAsm *a, int w, SPTXmmReg xmm, SPTReg rm) {
  int r = (xmm >> 3) & 1;
  int b = (rm >> 3) & 1;
  sptasm_rex(a, w, r, 0, b);
}

static void emit_rex_xmm_xmm(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) {
  int r = (dst >> 3) & 1;
  int b = (src >> 3) & 1;
  sptasm_rex(a, 0, r, 0, b);
}

void sptasm_movsd_rm(SPTAsm *a, SPTXmmReg dst, SPTReg base, int32_t disp) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm(a, 0, dst, base);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x10); /* MOVSD xmm, m64 */
  emit_mem(a, (SPTReg)dst, base, disp);
}

void sptasm_movsd_mr(SPTAsm *a, SPTReg base, int32_t disp, SPTXmmReg src) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm(a, 0, src, base);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x11); /* MOVSD m64, xmm */
  emit_mem(a, (SPTReg)src, base, disp);
}

void sptasm_movsd_rr(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm_xmm(a, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x10); /* MOVSD xmm, xmm */
  emit_modrm(a, 3, dst, src);
}

/* Move the low 64 bits of an XMM register into a GPR (movq r64, xmm).
   66 REX.W 0F 7E /r, with the XMM in the reg field and the GPR in r/m. */
void sptasm_movq_xmm_to_gpr(SPTAsm *a, SPTReg dst, SPTXmmReg src) {
  sptasm_byte(a, 0x66);
  emit_rex_xmm(a, 1, src, dst);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x7E); /* MOVQ r/m64, xmm */
  emit_modrm(a, 3, src, dst);
}

/* Move a GPR's 64 bits into the low 64 of an XMM (movq xmm, r64).
   66 REX.W 0F 6E /r, with the XMM in the reg field and the GPR in r/m.
   Used to lift an integer all-ones/all-zeros mask into an XMM for FSELECT. */
void sptasm_movq_gpr_to_xmm(SPTAsm *a, SPTXmmReg dst, SPTReg src) {
  sptasm_byte(a, 0x66);
  emit_rex_xmm(a, 1, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x6E); /* MOVQ xmm, r/m64 */
  emit_modrm(a, 3, dst, src);
}

static void sse2_op_rr(SPTAsm *a, uint8_t opcode, SPTXmmReg dst, SPTXmmReg src) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm_xmm(a, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, opcode);
  emit_modrm(a, 3, dst, src);
}

void sptasm_addsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) { sse2_op_rr(a, 0x58, dst, src); }

void sptasm_subsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) { sse2_op_rr(a, 0x5C, dst, src); }

void sptasm_mulsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) { sse2_op_rr(a, 0x59, dst, src); }

void sptasm_divsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) { sse2_op_rr(a, 0x5E, dst, src); }

void sptasm_xorps(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) {
  emit_rex_xmm_xmm(a, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x57); /* XORPS (no mandatory prefix) */
  emit_modrm(a, 3, dst, src);
}

/* Packed-double bitwise ops (0x66 0x0F .. /r). Used to build a bit-exact float
   select: result = (then & mask) | (else & ~mask). We operate on the full 128
   bits but only the low 64 (the scalar double) are ever read back. */
static void sse2_op66_rr(SPTAsm *a, uint8_t opcode, SPTXmmReg dst, SPTXmmReg src) {
  sptasm_byte(a, 0x66);
  emit_rex_xmm_xmm(a, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, opcode);
  emit_modrm(a, 3, dst, src);
}

void sptasm_andpd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) { sse2_op66_rr(a, 0x54, dst, src); }

void sptasm_andnpd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) {
  sse2_op66_rr(a, 0x55, dst, src);
} /* dst = ~dst & src */

void sptasm_orpd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src) { sse2_op66_rr(a, 0x56, dst, src); }

/* CMPSD xmm1, xmm2, imm8 (0xF2 0x0F 0xC2 /r ib): low 64 of dst = all-ones if
   (dst CMP src) else all-zeros; predicate imm8: 0=EQ 1=LT 2=LE 4=NEQ (all the
   ordered/quiet senses that match Lua's NaN behavior). */
void sptasm_cmpsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src, uint8_t imm8) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm_xmm(a, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0xC2);
  emit_modrm(a, 3, dst, src);
  sptasm_byte(a, imm8);
}

void sptasm_ucomisd(SPTAsm *a, SPTXmmReg r1, SPTXmmReg r2) {
  sptasm_byte(a, 0x66);
  emit_rex_xmm_xmm(a, r1, r2);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x2E); /* UCOMISD */
  emit_modrm(a, 3, r1, r2);
}

void sptasm_cvtsi2sd(SPTAsm *a, SPTXmmReg dst, SPTReg src) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm(a, 1, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x2A); /* CVTSI2SD xmm, r/m64 */
  emit_modrm(a, 3, dst, src);
}

void sptasm_cvtsd2si(SPTAsm *a, SPTReg dst, SPTXmmReg src) {
  sptasm_byte(a, 0xF2);
  emit_rex_xmm(a, 1, src, dst);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x2D); /* CVTSD2SI r64, xmm */
  emit_modrm(a, 3, dst, src);
}

/* ROUNDSD xmm, xmm, imm8 (SSE4.1): round a double to an integral double using
   the imm8 rounding mode. imm8 = 0x09 -> floor (toward -inf, inexact masked),
   0x0A -> ceil (toward +inf). Encoding: 66 0F 3A 0B /r ib. */
void sptasm_roundsd(SPTAsm *a, SPTXmmReg dst, SPTXmmReg src, uint8_t imm8) {
  sptasm_byte(a, 0x66);
  emit_rex_xmm_xmm(a, dst, src);
  sptasm_byte(a, 0x0F);
  sptasm_byte(a, 0x3A);
  sptasm_byte(a, 0x0B);
  emit_modrm(a, 3, dst, src);
  sptasm_byte(a, imm8);
}

/* =====================================================================
** Executable memory management
** ===================================================================== */

void *sptjit_alloc_exec(size_t size) {
#if defined(_WIN32)
  return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
  void *p =
      mmap(NULL, size, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  return (p == MAP_FAILED) ? NULL : p;
#endif
}

void sptjit_free_exec(void *ptr, size_t size) {
  if (!ptr)
    return;
#if defined(_WIN32)
  VirtualFree(ptr, 0, MEM_RELEASE);
  (void)size;
#else
  munmap(ptr, size);
#endif
}

void sptjit_protect_rw(void *ptr, size_t size) {
#if defined(_WIN32)
  DWORD old;
  VirtualProtect(ptr, size, PAGE_READWRITE, &old);
#else
  mprotect(ptr, size, PROT_READ | PROT_WRITE);
#endif
}

void sptjit_protect_rx(void *ptr, size_t size) {
#if defined(_WIN32)
  DWORD old;
  VirtualProtect(ptr, size, PAGE_EXECUTE_READ, &old);
#else
  mprotect(ptr, size, PROT_READ | PROT_EXEC);
#endif
}

/* =====================================================================
** Finalize: apply relocations and copy to executable memory
** ===================================================================== */

void *sptasm_finalize(SPTAsm *a, void *exec_mem, size_t exec_size) {
  if (!exec_mem || a->size > exec_size)
    return NULL;

  /* Copy code to executable memory. */
  memcpy(exec_mem, a->code, a->size);

  /* Apply relocations. */
  uint8_t *code = (uint8_t *)exec_mem;
  for (int i = 0; i < a->nrelocs; i++) {
    struct SPTReloc *r = &a->relocs[i];
    if (r->label < 0 || r->label >= a->nlabels)
      continue;
    int32_t target = a->labels[r->label];
    if (target < 0)
      continue;

    if (r->type == 0) {
      /* rel32: target = target - (code_offset + 4) */
      int32_t rel = target - (int32_t)(r->code_offset + 4);
      uint8_t *p = code + r->code_offset - 4; /* code_offset is after the 4 bytes */
      /* Actually, code_offset was recorded at the position AFTER emitting the
         4-byte placeholder. So the placeholder starts at code_offset - 4. */
      /* Wait, let me re-check. In sptasm_jmp, we:
         1. emit 0xE9 (1 byte)
         2. add_reloc (records current size = offset of the 4-byte placeholder)
         3. emit 4 bytes of 0
         So code_offset points to the START of the 4-byte field.
         The rel32 is relative to the END of the instruction (code_offset + 4).
      */
      p = code + r->code_offset;
      p[0] = (uint8_t)(rel & 0xff);
      p[1] = (uint8_t)(rel >> 8);
      p[2] = (uint8_t)(rel >> 16);
      p[3] = (uint8_t)(rel >> 24);
    } else {
      /* abs64 */
      uint8_t *p = code + r->code_offset;
      uint64_t addr = (uint64_t)(code + target);
      for (int j = 0; j < 8; j++)
        p[j] = (uint8_t)(addr >> (j * 8));
    }
  }

  return exec_mem;
}
