/*
 * jit/jit_rt.h — Runtime helpers invoked from JIT-generated native code.
 *
 * The MIR lowering inlines the hot, type-stable operations (typed arithmetic,
 * loads, moves, branches) directly as native instructions. Operations whose
 * semantics must exactly match the interpreter across all value types — value
 * comparison, truthiness, and the return sequence (which also closes upvalues)
 * — are emitted as calls to these small C functions, defined in vm.c.
 *
 * Only present in JIT builds.
 */
#ifndef SPT_JIT_RT_H
#define SPT_JIT_RT_H

#include "spt/state.h"

#ifdef SPT_HAS_JIT

SPT_API int  spt_val_eq(const TValue *a, const TValue *b);
SPT_API int  spt_val_lt_rt(spt_State *L, const TValue *a, const TValue *b);
SPT_API int  spt_val_le_rt(spt_State *L, const TValue *a, const TValue *b);
SPT_API int  spt_truthy_v(const TValue *v);
SPT_API void spt_jit_do_return(spt_State *L, int a, int nret);

/* Helpers for opcodes whose semantics require runtime type dispatch or
 * side-effects that are not worth inlining into native code. */
SPT_API void spt_jit_do_neg(spt_State *L, int a, int b);   /* OP_NEG */
SPT_API void spt_jit_do_cast(spt_State *L, int a, int target); /* OP_CAST */

#endif /* SPT_HAS_JIT */
#endif /* SPT_JIT_RT_H */
