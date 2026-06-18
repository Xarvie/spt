/*
 * spt/state.h — Execution state.
 *
 * The state is split, as in PUC-Lua, into:
 *   - spt_Global : process-wide data shared by all threads/coroutines —
 *                  the GC, the string intern table, the globals map.
 *   - spt_State  : a single thread of execution — its value stack and its
 *                  chain of call frames.
 * The split is what makes stackful coroutines and a shared heap possible later.
 */
#ifndef SPT_STATE_H
#define SPT_STATE_H

#include "object.h"
#include "opcodes.h"

/* A call frame. With the Slot-0 convention, base[0] is the receiver and
 * base[1..numparams] are the named arguments / locals. */
typedef struct CallInfo {
  TValue      *func;      /* the callable being invoked (R[A] of the CALL)  */
  TValue      *base;      /* register 0 of this frame (== receiver slot)    */
  TValue      *top;       /* first free slot above this frame               */
  const Instr *savedpc;   /* resume point in the caller's Proto             */
  int          nresults;  /* results the caller expects (-1 => multret)     */
  struct CallInfo *prev;
} CallInfo;

/* String intern table (open-chained). */
typedef struct StrTab {
  String  **buckets;
  uint32_t  nbuckets;     /* power of two                                   */
  uint32_t  count;
} StrTab;

typedef struct spt_Global {
  /* allocator hook (so hosts can supply their own) */
  void *(*realloc_fn)(void *ud, void *ptr, size_t osize, size_t nsize);
  void  *alloc_ud;

  /* --- garbage collector --- */
  GCObject *allgc;        /* singly-linked list of every live object        */
  size_t    bytes;        /* live bytes (approx)                            */
  size_t    gc_threshold; /* collect when bytes exceeds this                */
  uint8_t   current_white;/* which white tag marks live-but-unscanned       */
  int       gc_disabled;  /* >0 suppresses collection (re-entrancy guard)   */

  StrTab    strtab;       /* interned strings                               */
  Table    *globals;      /* the _G map                                     */
  spt_State *main;        /* main thread                                    */

#ifdef SPT_HAS_JIT
  void     *jit;          /* opaque spt_Jit* (see jit/jit.h)                */
#endif
} spt_Global;

struct spt_State {
  spt_Global *G;

  TValue   *stack;        /* value stack base                               */
  TValue   *stack_last;   /* last usable slot                               */
  TValue   *top;          /* first free slot                                */
  uint32_t  stacksize;

  CallInfo *ci;           /* current frame                                  */
  CallInfo  base_ci;      /* frame for the entry C call                     */

  Upval    *openupval;    /* open upvalues, stack-order                     */
  const char *errmsg;     /* last runtime error (NULL if none)              */
  void     *errjmp;       /* current setjmp recovery point (jmp_buf*)       */
};

/* --- lifecycle --- */
SPT_API spt_State *spt_newstate(void);
SPT_API void       spt_close(spt_State *L);

/* --- stack helpers --- */
SPT_API void    spt_checkstack(spt_State *L, int n);
SPT_INLINE TValue *spt_top(spt_State *L) { return L->top; }
SPT_INLINE void  spt_push(spt_State *L, const TValue *v) { setobj(L->top, v); L->top++; }
SPT_INLINE void  spt_pop(spt_State *L, int n) { L->top -= n; }

/* --- execution --- */
/* Run a closure already pushed with its receiver+args on the stack. */
SPT_API int  spt_call(spt_State *L, int nargs, int nresults);
/* Interpreter loop entry (executes the frame in L->ci). */
SPT_API int  spt_execute(spt_State *L);

/* --- errors --- */
SPT_API void spt_runtime_error(spt_State *L, const char *fmt, ...);

#endif /* SPT_STATE_H */
