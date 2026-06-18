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
SPT_API void spt_jit_do_concat(spt_State *L, int a, int b, int c); /* OP_CONCAT */

/* Globals — `key` points into the Proto's constant table (known at JIT time). */
SPT_API void spt_jit_do_getglobal(spt_State *L, int a, const TValue *key);
SPT_API void spt_jit_do_setglobal(spt_State *L, int a, const TValue *key);

/* Upvalues — the closure is read from ci->func at runtime. */
SPT_API void spt_jit_do_getupval(spt_State *L, int a, int b);
SPT_API void spt_jit_do_setupval(spt_State *L, int a, int b);

/* Length operator. */
SPT_API void spt_jit_do_len(spt_State *L, int a, int b);

/* Generic indexing (List | Map dispatch). */
SPT_API void spt_jit_do_getindex(spt_State *L, int a, int b, int c);
SPT_API void spt_jit_do_setindex(spt_State *L, int a, int b, int c);

/* List / Map construction. */
SPT_API void spt_jit_do_newlist(spt_State *L, int a, int hint);
SPT_API void spt_jit_do_listpush(spt_State *L, int a, int b);
SPT_API void spt_jit_do_newmap(spt_State *L, int a);

/* Generic arithmetic (runtime-typed operands). */
SPT_API void spt_jit_do_add(spt_State *L, int a, int b, int c);
SPT_API void spt_jit_do_sub(spt_State *L, int a, int b, int c);
SPT_API void spt_jit_do_mul(spt_State *L, int a, int b, int c);
SPT_API void spt_jit_do_div(spt_State *L, int a, int b, int c);
SPT_API void spt_jit_do_mod(spt_State *L, int a, int b, int c);

#endif /* SPT_HAS_JIT */
#endif /* SPT_JIT_RT_H */
