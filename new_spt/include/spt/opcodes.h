/*
 * spt/opcodes.h — The bytecode instruction set.
 *
 * Because SPT is a clean rewrite, the ISA is designed for SPT rather than for
 * Lua compatibility. Two design choices buy interpreter speed that stock Lua
 * cannot get:
 *
 *   1. Typed arithmetic ops (OP_IADD, OP_FADD, ...). When the codegen knows
 *      both operands are `int` (or `float`) from the static type annotations,
 *      it emits a typed op that skips all runtime tag dispatch and coercion.
 *
 *   2. Dedicated List ops (OP_GETLIST/OP_SETLIST). A List is statically known
 *      to be 0-based and dense with no hash part and no metatable lookup on the
 *      hot path, so indexing is a single bounds check + load — none of the
 *      array/hash/integer-key dispatch a unified table forces.
 *
 * Encoding: one 32-bit word, byte-aligned for readability/debuggability.
 *
 *      31      24 23      16 15       8 7        0
 *     +----------+----------+----------+----------+
 *     |    C     |    B     |    A     |    op     |
 *     +----------+----------+----------+----------+
 *
 *   ABC form : op, A, B, C   each 8 bits
 *   ABx form : op, A, Bx     Bx = (C<<8)|B  (16 bits, unsigned)
 *   AsBx form: op, A, sBx    sBx = Bx - 32768 (signed; jumps)
 */
#ifndef SPT_OPCODES_H
#define SPT_OPCODES_H

#include "conf.h"

typedef uint32_t Instr;

#define SPT_OPCODE(i)  ((spt_OpCode)((i) & 0xFFu))
#define SPT_GETA(i)    (((i) >> 8)  & 0xFFu)
#define SPT_GETB(i)    (((i) >> 16) & 0xFFu)
#define SPT_GETC(i)    (((i) >> 24) & 0xFFu)
#define SPT_GETBX(i)   (((i) >> 16) & 0xFFFFu)
#define SPT_GETSBX(i)  ((int)SPT_GETBX(i) - 32768)

#define SPT_MK_ABC(op,a,b,c) \
  ((Instr)(op) | ((Instr)(a) << 8) | ((Instr)(b) << 16) | ((Instr)(c) << 24))
#define SPT_MK_ABx(op,a,bx) \
  ((Instr)(op) | ((Instr)(a) << 8) | (((Instr)(bx) & 0xFFFFu) << 16))
#define SPT_MK_AsBx(op,a,sbx) \
  SPT_MK_ABx(op, a, (uint16_t)((int)(sbx) + 32768))

typedef enum {
  OP_MOVE,        /* A B      R[A] = R[B]                                  */
  OP_LOADK,       /* A Bx     R[A] = K[Bx]                                 */
  OP_LOADINT,     /* A sBx    R[A] = (int) sBx        (small immediate)    */
  OP_LOADBOOL,    /* A B      R[A] = (bool) B                              */
  OP_LOADNULL,    /* A B      R[A..A+B] = null                             */

  /* Typed arithmetic — no tag dispatch (codegen proves operand types).   */
  OP_IADD, OP_ISUB, OP_IMUL, OP_IDIV, OP_IMOD,   /* int   R[A]=R[B] op R[C]*/
  OP_FADD, OP_FSUB, OP_FMUL, OP_FDIV,            /* float R[A]=R[B] op R[C]*/

  /* Generic arithmetic — runtime-typed operands (int/float mixing).      */
  OP_ADD, OP_SUB, OP_MUL, OP_DIV, OP_MOD, OP_NEG,

  /* Comparison & logic — R[A] = boolean result.                          */
  OP_EQ, OP_LT, OP_LE, OP_NOT,

  /* Control flow.                                                        */
  OP_JMP,         /* sBx     pc += sBx                                     */
  OP_TEST,        /* A C     if truthy(R[A]) != C then pc++  (skip next)   */
  OP_TESTSET,     /* A B C   if truthy(R[B])==C: R[A]=R[B] else pc++       */

  /* String.                                                              */
  OP_CONCAT,      /* A B C   R[A] = R[B] .. R[C]                           */
  OP_LEN,         /* A B     R[A] = #R[B]                                  */

  /* List — 0-based, bounds-checked, no hash, no metatable on hot path.   */
  OP_NEWLIST,     /* A B     R[A] = new list (capacity hint B)            */
  OP_GETLIST,     /* A B C   R[A] = R[B][R[C]]                            */
  OP_SETLIST,     /* A B C   R[A][R[B]] = R[C]                            */
  OP_LISTPUSH,    /* A B     append R[B] to list R[A]                     */

  /* Map — arbitrary keys.                                                */
  OP_NEWMAP,      /* A       R[A] = new map                               */
  OP_GETMAP,      /* A B C   R[A] = R[B][R[C]]                            */
  OP_SETMAP,      /* A B C   R[A][R[B]] = R[C]                            */

  /* Generic index — dynamic dispatch (List | Map | String).             */
  OP_GETINDEX,    /* A B C   R[A] = R[B][R[C]]                            */
  OP_SETINDEX,    /* A B C   R[A][R[B]] = R[C]                            */

  /* Globals (a Map held by the state).                                   */
  OP_GETGLOBAL,   /* A Bx    R[A] = _G[K[Bx]]                              */
  OP_SETGLOBAL,   /* A Bx    _G[K[Bx]] = R[A]                              */

  /* Upvalues.                                                            */
  OP_GETUPVAL,    /* A B     R[A] = Up[B]                                  */
  OP_SETUPVAL,    /* A B     Up[B] = R[A]                                  */

  /* Functions. Slot-0 receiver convention:                               */
  /*   R[A] = callable, R[A+1] = receiver, R[A+2..] = args.               */
  OP_CLOSURE,     /* A Bx    R[A] = closure(Proto[Bx])                     */
  OP_CALL,        /* A B C   call R[A]; B = 1+nargs values; C = 1+nresults */
                  /*          (B==0 or C==0 => use stack top / multret)    */
  OP_RETURN,      /* A B     return B-1 values from R[A]  (B==0 => to top) */

  OP_HALT,        /* stop execution (top-level chunk end)                 */

  OP_NUM_OPCODES
} spt_OpCode;

/* Human-readable names, indexed by opcode (defined in vm.c). */
SPT_API const char *const spt_opnames[OP_NUM_OPCODES];

#endif /* SPT_OPCODES_H */
