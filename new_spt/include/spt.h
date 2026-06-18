/*
 * spt.h — Public embedding API.
 *
 * A small, Lua-style stack API: the host pushes values, calls, and reads
 * results through an explicit value stack. This is the proven shape for clean
 * C interop and is what a higher-level C++ binding (sptxx) would wrap.
 *
 * Slot-0 receiver convention is honoured: inside a C function registered with
 * spt_register, argument 1 is the receiver and the first real argument is at
 * index 2 (see spt_arg_*).
 */
#ifndef SPT_H
#define SPT_H

#include "spt/state.h"
#include "spt/mem.h"

/* --- pushing values onto the stack --- */
SPT_API void spt_pushnull(spt_State *L);
SPT_API void spt_pushbool(spt_State *L, int b);
SPT_API void spt_pushint(spt_State *L, spt_Integer i);
SPT_API void spt_pushfloat(spt_State *L, spt_Number n);
SPT_API void spt_pushstring(spt_State *L, const char *s);
SPT_API void spt_pushcfunction(spt_State *L, spt_CFunction fn);

/* --- reading values (by absolute 1-based stack index) --- */
SPT_API int          spt_isnull(spt_State *L, int idx);
SPT_API int          spt_isbool(spt_State *L, int idx);
SPT_API spt_Integer  spt_toint(spt_State *L, int idx);
SPT_API spt_Number   spt_tofloat(spt_State *L, int idx);
SPT_API int          spt_tobool(spt_State *L, int idx);
SPT_API const char  *spt_tostring(spt_State *L, int idx);
SPT_API int          spt_gettop(spt_State *L);

/* --- arguments inside a registered C function (Slot-0 aware) --- */
/* arg n (1-based, where n=1 is the first *real* argument == stack index 2). */
SPT_API spt_Integer  spt_arg_int(spt_State *L, int n);
SPT_API spt_Number   spt_arg_float(spt_State *L, int n);
SPT_API const char  *spt_arg_string(spt_State *L, int n);

/* --- globals --- */
SPT_API void spt_setglobal(spt_State *L, const char *name);   /* pops a value */
SPT_API void spt_getglobal(spt_State *L, const char *name);   /* pushes value */
SPT_API void spt_register(spt_State *L, const char *name, spt_CFunction fn);

/* --- compile SPT source text into a callable closure (pushed on success) ---
 * Returns 0 on success (a closure is left on the stack), non-zero on a compile
 * error (nothing pushed; the message is available via the state's last error). */
SPT_API int spt_load(spt_State *L, const char *src, const char *chunkname);

/* --- JIT control (declared only in JIT builds) --- */
#ifdef SPT_HAS_JIT
SPT_API void spt_jit_set_enabled(spt_State *L, int enabled);
SPT_API int  spt_jit_is_enabled(spt_State *L);
SPT_API void spt_jit_try_compile(spt_State *L, Proto *p);  /* force-compile a Proto */
#endif

#endif /* SPT_H */
