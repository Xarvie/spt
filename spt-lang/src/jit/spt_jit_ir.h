/*
** spt_jit_ir.h — SSA IR for the SPT Trace JIT
**
** The IR is a linear array of instructions in SSA form. Each instruction
** has an opcode, a type, and up to two operand references (indices into
** the IR array). Constants and phi nodes are also IR instructions.
**
** Types are tracked at record time for specialization. The code generator
** uses types to select the correct native instructions (e.g., integer add
** vs. float add).
*/
#ifndef SPT_JIT_IR_H
#define SPT_JIT_IR_H

#include "spt_jit.h"

/* =====================================================================
** IR Types — SPT value types tracked during trace recording
** ===================================================================== */

typedef enum {
  SPTT_NIL = 0,     /* nil */
  SPTT_FALSE,       /* false (distinct from nil for boolean ops) */
  SPTT_TRUE,        /* true */
  SPTT_INT,         /* lua_Integer (64-bit) */
  SPTT_FLT,         /* lua_Number (double) */
  SPTT_STR,         /* TString* */
  SPTT_ARR,         /* Table* (LUA_VARRAY, List) */
  SPTT_TAB,         /* Table* (LUA_VTABLE, Map) */
  SPTT_FUNC,        /* closure or C function */
  SPTT_UD,          /* userdata */
  SPTT_ANY,         /* unknown / polymorphic */
  SPTT_DEAD = 0xff  /* marker: instruction is dead (DCE) */
} SPTType;

/* Check if a type is a number (int or float). */
#define sptt_isnum(t) ((t) == SPTT_INT || (t) == SPTT_FLT)
/* Check if a type is a table (array or map). */
#define sptt_istab(t) ((t) == SPTT_ARR || (t) == SPTT_TAB)
/* Check if a type is a GC object. */
#define sptt_isgc(t) ((t) == SPTT_STR || sptt_istab(t) || (t) == SPTT_FUNC || (t) == SPTT_UD)

/* =====================================================================
** IR Opcodes
** ===================================================================== */

typedef enum {
  /* Constants */
  SPTIR_NIL = 0,      /* nil constant */
  SPTIR_FALSE,        /* false constant */
  SPTIR_TRUE,         /* true constant */
  SPTIR_KINT,         /* integer constant: aux = int64 value */
  SPTIR_KFLT,         /* float constant: aux = double value (in union) */
  SPTIR_KSTR,         /* string constant: aux = TString* */
  SPTIR_KPTR,         /* raw pointer constant: aux = void* */
  SPTIR_KGC,          /* GC object constant: aux = GCObject* */

  /* Stack operations */
  SPTIR_SLOAD,        /* load from stack slot: aux = slot index, op1 = type */
  SPTIR_SSTORE,       /* store to stack slot: op1 = slot, op2 = value ref */

  /* Upvalue operations */
  SPTIR_ULOAD,        /* load upvalue: aux = upvalue index */
  SPTIR_USTORE,       /* store upvalue: op1 = index, op2 = value ref */

  /* Arithmetic (numeric) */
  SPTIR_ADD,          /* op1 + op2 */
  SPTIR_SUB,          /* op1 - op2 */
  SPTIR_MUL,          /* op1 * op2 */
  SPTIR_DIV,          /* op1 / op2 (float div) */
  SPTIR_MOD,          /* op1 % op2 */
  SPTIR_IDIV,         /* op1 // op2 (floor div) */
  SPTIR_POW,          /* op1 ^ op2 */
  SPTIR_NEG,          /* -op1 (unary minus) */
  SPTIR_FMATH,        /* libm unary math call: op1 = float arg, aux = double(*)(double)
                         libm function pointer. Result is float. Emits a C call
                         (disables RA for the trace). Used for math.sqrt/sin/... */

  /* Bitwise (integer) */
  SPTIR_BAND,         /* op1 & op2 */
  SPTIR_BOR,          /* op1 | op2 */
  SPTIR_BXOR,         /* op1 ~ op2 */
  SPTIR_BNOT,         /* ~op1 */
  SPTIR_SHL,          /* op1 << op2 */
  SPTIR_SHR,          /* op1 >> op2 */

  /* Comparisons (produce boolean) */
  SPTIR_EQ,           /* op1 == op2 */
  SPTIR_NE,           /* op1 != op2 */
  SPTIR_LT,           /* op1 < op2 */
  SPTIR_LE,           /* op1 <= op2 */
  SPTIR_GT,           /* op1 > op2 (flipped LT) */
  SPTIR_GE,           /* op1 >= op2 (flipped LE) */

  /* Logic */
  SPTIR_NOT,          /* not op1 */

  /* Table / Array access */
  SPTIR_GETI,         /* array[int]: op1 = array, op2 = index. Guards type & bounds. */
  SPTIR_SETI,         /* array[int] = val: op1 = array, op2 = index, aux = value ref */
  SPTIR_GETFIELD,     /* map[str]: op1 = map, aux = key (TString*). */
  SPTIR_SETFIELD,     /* map[str] = val: op1 = map, aux = key, op2 = value ref */
  SPTIR_GETTABUP,     /* upvalue[str]: op1 = upval ref, aux = key */
  SPTIR_LEN,          /* #op1 (length) */

  /* Type conversions */
  SPTIR_TOFLT,        /* int -> float */
  SPTIR_TOINT,        /* float -> int (with mode in aux) */

  /* Control flow */
  SPTIR_GUARD,        /* type/bounds guard: op1 = value, aux = guard type.
                         If guard fails, take side exit. */
  SPTIR_GUARD_LT,     /* guard op1 < aux (bounds check) */
  SPTIR_GUARD_LE,     /* guard op1 <= aux */
  SPTIR_GUARD_EQ,     /* guard op1 == aux (constant) */
  SPTIR_GUARD_CFUNC,  /* guard table[key] == expected C closure: op1 = table ref,
                         op2 = KPTR ref holding the key TString*, aux = expected
                         GCObject* (the CClosure). Looks up key in the table and
                         side-exits unless the found value equals expected. Pins
                         a math.* method to its known function before an FMATH. */
  SPTIR_GUARD_T,      /* guard type of op1 == aux (SPTType) */
  SPTIR_GUARD_ULT,    /* guard (unsigned)op1 < aux (constant); used for shift
                         counts so out-of-range counts side-exit */

  SPTIR_EXIT,         /* unconditional side exit (aux = exit index) */
  SPTIR_LOOP,         /* loop back-edge marker (aux = loop start IR index) */
  SPTIR_PHI,          /* phi node: op1 = pre-loop value, op2 = loop value */
  SPTIR_CALL,         /* C function call: aux = function ptr, op1 = args ref */
  SPTIR_RETURN,       /* return from trace: op1 = value ref (or -1 for void) */

  SPTIR_CMPSET,       /* (op1 cmp op2) -> 0/1 integer; aux = comparison SPTIROp.
                         Like a comparison but materializes a 0/1 value instead of
                         guarding. Used by if-conversion to build branchless
                         select = else + (then-else)*cond. Integer operands only. */
  SPTIR_FCMPMASK,     /* (op1 cmp op2) on doubles -> all-ones/all-zeros bitmask;
                         aux = comparison SPTIROp (EQ/NE/LT/LE only; GT/GE are
                         emitted as swapped LT/LE). Float if-conversion. */
  SPTIR_ICMPMASK,     /* (op1 cmp op2) on integers -> all-ones/all-zeros bitmask
                         lifted into an XMM; aux = comparison SPTIROp. Lets an
                         integer-conditioned branch with float arms use FSELECT
                         (the integer values are compared exactly, no rounding). */
  SPTIR_FSELECT,      /* bit-exact float select: op1=then, op2=else, aux=mask ref
                         (an FCMPMASK). result = (then & mask) | (else & ~mask).
                         Not arithmetic, so the chosen operand is reproduced
                         bit-for-bit (float select via B+(A-B)*c is NOT exact). */

  SPTIR_NOP,          /* no-op (placeholder) */
  SPTIR_MAX
} SPTIROp;

/* =====================================================================
** IR Instruction
** ===================================================================== */

/*
** Each IR instruction is 32 bytes:
**   op    (2 bytes) - opcode
**   type  (1 byte)  - result type
**   flags (1 byte)  - misc flags
**   op1   (4 bytes) - operand 1 (IR ref, or -1)
**   op2   (4 bytes) - operand 2 (IR ref, or -1)
**   aux   (8 bytes) - auxiliary data (constant value, slot index, etc.)
**   prev  (4 bytes) - previous def of same register (for SSA chain)
**   pad   (4 bytes)
*/
struct SPTIRInst {
  uint16_t op;
  uint8_t type;
  uint8_t flags;
  int32_t op1;
  int32_t op2;
  int64_t aux;
  int32_t prev;
  int32_t snap_idx;   /* for guard instructions: index of associated snapshot;
                         -1 for non-guards. Lets codegen pair a guard with its
                         snapshot independent of emission order (needed for
                         hoisting guards into a loop preheader). */
};

/* IR flags */
#define SPTIRF_CONST   0x01  /* constant instruction (no operands) */
#define SPTIRF_GUARD   0x02  /* this instruction is a guard */
#define SPTIRF_DEAD    0x04  /* marked dead by DCE */
#define SPTIRF_PHI     0x08  /* phi node */
#define SPTIRF_SNAP    0x10  /* snapshot follows this instruction */

/* Invalid IR reference. */
#define SPTIR_NULL (-1)

/* =====================================================================
** Snapshot — records interpreter state at a guard point
** ===================================================================== */

/*
** A snapshot maps each live stack slot to the IR ref that holds its
** current value. When a guard fails, the exit handler uses the snapshot
** to restore all stack slots before returning to the interpreter.
*/
struct SPTSnapshot {
  int32_t exit_id;        /* index into exit table */
  int32_t pc_offset;      /* PC offset from trace start (to restore savedpc) */
  int32_t nslots;         /* number of live slots */
  int32_t slot_map[0];    /* nslots entries: IR ref for each slot, or SPTIR_NULL */
};

/* =====================================================================
** Exit Info — metadata for a side exit
** ===================================================================== */

struct SPTExitInfo {
  int32_t snapshot_idx;   /* index into trace's snapshot array */
  const Instruction *target_pc;  /* PC to resume at in interpreter */
  void *code_offset;      /* offset in compiled code for this exit's epilogue */
};

/* =====================================================================
** IR Builder — builds and optimizes IR during trace recording
** ===================================================================== */

typedef struct SPTIRBuilder {
  SPTIRInst *insts;       /* instruction array */
  int ninst;              /* number of instructions */
  int cap;                /* capacity */

  /* Register -> IR ref mapping (current SSA value for each register) */
  int32_t reg_map[256];   /* indexed by register number */
  SPTType reg_type[256];  /* type of each register */

  /* Snapshot management */
  SPTSnapshot **snaps;    /* snapshot array */
  int nsnaps;
  int snap_cap;

  /* Exit PCs: for each snapshot, the interpreter PC to resume at. */
  const Instruction *exit_pcs[256];

  /* Stack slot tracking */
  int maxslot;            /* highest stack slot used */

  /* Loop info */
  int loop_start;         /* IR index where loop starts */
  int loop_end;           /* IR index where loop ends (back-edge) */
  int32_t loop_reg_map[256]; /* register values at loop start (for phi) */
  int have_loop;          /* whether we've seen the loop back-edge */
} SPTIRBuilder;

/* IR builder API */
void sptir_init(SPTIRBuilder *b);
void sptir_free(SPTIRBuilder *b);
void sptir_reset(SPTIRBuilder *b);

/* Emit an IR instruction and return its ref. */
int sptir_emit(SPTIRBuilder *b, SPTIROp op, SPTType type, int32_t op1, int32_t op2, int64_t aux);

/* Emit a constant. */
int sptir_kint(SPTIRBuilder *b, int64_t val);
int sptir_kflt(SPTIRBuilder *b, double val);
int sptir_kstr(SPTIRBuilder *b, void *str);
int sptir_kptr(SPTIRBuilder *b, void *ptr);

/* Emit a guard and create a snapshot. */
int sptir_guard(SPTIRBuilder *b, SPTIROp gop, int32_t val_ref, int64_t aux,
                const Instruction *exit_pc);

/* Emit an unconditional side exit to the interpreter at exit_pc (closes a side
   trace at a back-edge it cannot loop on). Creates a snapshot for restoration. */
int sptir_exit(SPTIRBuilder *b, const Instruction *exit_pc);

/* Create a snapshot of current register state. */
int sptir_snapshot(SPTIRBuilder *b, const Instruction *exit_pc);

/* Optimization passes. */
void sptir_optimize(SPTIRBuilder *b);

/* Mark the loop back-edge and insert phi nodes. */
void sptir_loop(SPTIRBuilder *b);

/* Get the type of an IR ref. */
SPTType sptir_type(const SPTIRBuilder *b, int ref);

/* Debug: print the IR to stderr. */
void sptir_dump(const SPTIRBuilder *b, const char *title);

/* Get the instruction at a ref. */
static inline SPTIRInst *sptir_get(const SPTIRBuilder *b, int ref) {
  return (ref >= 0 && ref < b->ninst) ? &b->insts[ref] : NULL;
}

#endif /* SPT_JIT_IR_H */
