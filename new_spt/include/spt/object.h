/*
 * spt/object.h — Heap object layout.
 *
 * Every collectable object begins with `GCHeader`. The header carries the
 * colour + age bits a generational collector needs, so the object model is
 * already shaped for the generational GC even though the first collector
 * milestone is a stop-the-world mark/sweep (the generational layer partitions
 * the same object lists and reuses the same bits).
 *
 *   Table is a single struct in two modes:
 *     - LIST: a 0-based, dense, bounds-checked array. `array`/`asize`/`alen`.
 *             This is SPT's differentiator and the JIT/interpreter fast path.
 *     - MAP : an open-addressing hash for arbitrary keys. `node`/`nsize`.
 *   A value never silently migrates between modes; the mode is fixed at birth.
 */
#ifndef SPT_OBJECT_H
#define SPT_OBJECT_H

#include "value.h"

/* ------------------------------------------------------------------ */
/* GC header + colour/age bits                                         */
/* ------------------------------------------------------------------ */
/* marked byte layout:
 *   bit0  WHITE0      } tri-colour mark state. One white is "current",
 *   bit1  WHITE1      } the other is "other"; flipping avoids a clear pass.
 *   bit2  BLACK
 *   bits3-5  AGE      generational age (NEW..OLD); unused by mark/sweep,
 *                     reserved so the generational upgrade is non-breaking.
 *   bit6  FIXED       never collected (e.g. interned metanames)
 */
#define GC_WHITE0  (1u << 0)
#define GC_WHITE1  (1u << 1)
#define GC_BLACK   (1u << 2)
#define GC_AGE_MASK (7u << 3)
#define GC_FIXED   (1u << 6)
#define GC_WHITEBITS (GC_WHITE0 | GC_WHITE1)

#define GCHeader  GCObject *next; uint8_t tt; uint8_t marked

struct GCObject { GCHeader; };

#define iswhite(o)   ((o)->marked & GC_WHITEBITS)
#define isblack(o)   ((o)->marked & GC_BLACK)
#define isfixed(o)   ((o)->marked & GC_FIXED)

/* ------------------------------------------------------------------ */
/* String — immutable, interned, length-prefixed                       */
/* ------------------------------------------------------------------ */
struct String {
  GCHeader;
  uint32_t hash;
  uint32_t len;
  String  *snext;       /* chain within the intern table bucket          */
  char     data[];      /* `len` bytes + a trailing NUL                   */
};

#define str_cstr(s)  ((const char *)(s)->data)

/* ------------------------------------------------------------------ */
/* Table — List mode or Map mode                                       */
/* ------------------------------------------------------------------ */
#define TMODE_LIST  1
#define TMODE_MAP   2

typedef struct MNode {
  TValue key;           /* SPT_TNULL key marks an empty slot             */
  TValue val;
} MNode;

struct Table {
  GCHeader;
  uint8_t  mode;        /* TMODE_LIST | TMODE_MAP                         */
  TValue  *array;       /* LIST: storage (0-based)                        */
  uint32_t asize;       /* LIST: physical capacity                       */
  uint32_t alen;        /* LIST: logical length (loglen)                 */
  MNode   *node;        /* MAP: open-addressing slots (power-of-two)      */
  uint32_t nsize;       /* MAP: slot count                               */
  uint32_t nused;       /* MAP: occupied slots                           */
  Table   *metatable;
};

/* ------------------------------------------------------------------ */
/* Function objects                                                    */
/* ------------------------------------------------------------------ */
typedef struct UpvalDesc {
  uint8_t in_stack;     /* 1: capture caller register; 0: caller upvalue */
  uint8_t idx;
} UpvalDesc;

struct Proto {
  GCHeader;
  uint8_t   numparams;  /* named parameters (excludes the Slot-0 receiver)*/
  uint8_t   is_vararg;
  uint8_t   maxstack;   /* registers the frame needs                     */
  uint32_t *code;       uint32_t ncode;
  TValue   *k;          uint32_t nk;    /* constant pool                 */
  Proto   **p;          uint32_t np;    /* nested prototypes             */
  UpvalDesc *upvals;    uint32_t nups;
  String   *source;     /* chunk name, for diagnostics                   */

  /* JIT bookkeeping. `jit_entry` is the compiled native entry or NULL;
   * the field exists in every build for a stable layout but is only ever
   * read/written when SPT_HAS_JIT is set. */
  void     *jit_entry;
  uint32_t  call_count;
};

typedef struct Upval {
  GCHeader;
  TValue *v;            /* points into the stack while open, to `closed`  */
  TValue  closed;       /* when closed                                    */
  struct Upval *unext;  /* next open upvalue (stack-address order)         */
} Upval;

struct Closure {        /* SPT bytecode closure                          */
  GCHeader;
  Proto  *p;
  uint32_t nups;
  Upval  *ups[];        /* flexible array of upvalue pointers            */
};

typedef int (*spt_CFunction)(spt_State *L);

struct CFunc {          /* C closure: host function + by-value upvalues   */
  GCHeader;
  spt_CFunction fn;
  uint32_t nups;
  TValue   ups[];       /* flexible array of C upvalues                  */
};

/* ------------------------------------------------------------------ */
/* Constructors & operations (implemented in object.c)                 */
/* ------------------------------------------------------------------ */
SPT_API String *spt_str_newlen(spt_State *L, const char *s, size_t len);
SPT_API String *spt_str_new(spt_State *L, const char *s);   /* NUL-terminated */
SPT_API int     spt_str_eq(const String *a, const String *b);

SPT_API Table  *spt_list_new(spt_State *L, uint32_t hint);
SPT_API Table  *spt_map_new(spt_State *L);
SPT_API void    spt_list_push(spt_State *L, Table *t, const TValue *v);
SPT_API int     spt_list_get(Table *t, spt_Integer idx, TValue *out);   /* 0 ok, <0 OOB */
SPT_API int     spt_list_set(spt_State *L, Table *t, spt_Integer idx, const TValue *v);
SPT_API void    spt_map_set(spt_State *L, Table *t, const TValue *key, const TValue *v);
SPT_API int     spt_map_get(Table *t, const TValue *key, TValue *out);  /* 1 found, 0 not */
SPT_API spt_Integer spt_obj_len(const TValue *o);  /* List: alen; String: len; Map: 0 */

SPT_API Proto   *spt_proto_new(spt_State *L);
SPT_API Closure *spt_closure_new(spt_State *L, Proto *p);
SPT_API CFunc   *spt_cfunc_new(spt_State *L, spt_CFunction fn, int nups);

#endif /* SPT_OBJECT_H */
