/*
 * spt/value.h — The tagged value representation.
 *
 * Every SPT value is a 16-byte `TValue`: an 8-byte payload union plus a 1-byte
 * type tag (padded to 8). This mirrors PUC-Lua's proven layout — simple,
 * portable, and trivial to pass across the C boundary — rather than NaN-boxing,
 * which trades portability and debuggability for size. NaN-boxing can be added
 * later behind this same interface if the 8-byte saving is ever needed.
 *
 * Collectable types (tag >= SPT_TSTRING) carry a GCObject* in `v.gc`; the rest
 * are stored inline by value.
 */
#ifndef SPT_VALUE_H
#define SPT_VALUE_H

#include "conf.h"

typedef struct GCObject GCObject;
typedef struct String   String;
typedef struct Table    Table;
typedef struct Proto    Proto;
typedef struct Closure  Closure;
typedef struct CFunc    CFunc;
typedef struct spt_State spt_State;

/* Type tags. Order matters: SPT_TSTRING is the first collectable tag, so the
 * `iscollectable` test is a single comparison. */
typedef enum {
  SPT_TNULL = 0,
  SPT_TBOOL,
  SPT_TINT,
  SPT_TFLOAT,
  SPT_TLIGHTPTR,   /* opaque C pointer (light userdata) — non-collectable    */
  /* --- collectable below this line --- */
  SPT_TSTRING,
  SPT_TLIST,       /* Table in list mode  (0-based, dense, bounds-checked)   */
  SPT_TMAP,        /* Table in map  mode  (arbitrary keys)                   */
  SPT_TCLOSURE,    /* SPT function (bytecode closure)                        */
  SPT_TCFUNC,      /* C function closure (host interop, builtins)            */
  /* internal collectable objects — never stored in a TValue */
  SPT_TPROTO,
  SPT_TUPVAL,
  SPT_TNUMTAGS
} spt_VType;

#define SPT_FIRST_GC_TAG  SPT_TSTRING

typedef union {
  spt_Integer i;
  spt_Number  n;
  int         b;     /* boolean: 0 / 1            */
  void       *p;     /* light C pointer           */
  GCObject   *gc;    /* collectable object        */
} Value;

typedef struct {
  Value   v;
  uint8_t tt;        /* spt_VType */
} TValue;

/* --- tag inspection ------------------------------------------------ */
#define ttype(o)          ((o)->tt)
#define ttisnull(o)       (ttype(o) == SPT_TNULL)
#define ttisbool(o)       (ttype(o) == SPT_TBOOL)
#define ttisint(o)        (ttype(o) == SPT_TINT)
#define ttisfloat(o)      (ttype(o) == SPT_TFLOAT)
#define ttisnumber(o)     (ttisint(o) || ttisfloat(o))
#define ttisstring(o)     (ttype(o) == SPT_TSTRING)
#define ttislist(o)       (ttype(o) == SPT_TLIST)
#define ttismap(o)        (ttype(o) == SPT_TMAP)
#define ttistable(o)      (ttislist(o) || ttismap(o))
#define ttisclosure(o)    (ttype(o) == SPT_TCLOSURE)
#define ttiscfunc(o)      (ttype(o) == SPT_TCFUNC)
#define ttiscallable(o)   (ttisclosure(o) || ttiscfunc(o))
#define iscollectable(o)  (ttype(o) >= SPT_FIRST_GC_TAG)

/* --- payload extraction (call only after a matching tag check) ----- */
#define ivalue(o)    ((o)->v.i)
#define fltvalue(o)  ((o)->v.n)
#define bvalue(o)    ((o)->v.b)
#define pvalue(o)    ((o)->v.p)
#define gcvalue(o)   ((o)->v.gc)
#define strvalue(o)  ((String  *)(o)->v.gc)
#define tblvalue(o)  ((Table   *)(o)->v.gc)
#define clvalue(o)   ((Closure *)(o)->v.gc)
#define cfvalue(o)   ((CFunc   *)(o)->v.gc)

/* Truthiness: only null and false are falsy (Lua semantics). */
#define spt_falsy(o)  (ttisnull(o) || (ttisbool(o) && bvalue(o) == 0))
#define spt_truthy(o) (!spt_falsy(o))

/* --- value construction (write into an existing slot) -------------- */
SPT_INLINE void setnull(TValue *o)              { o->tt = SPT_TNULL; o->v.i = 0; }
SPT_INLINE void setbool(TValue *o, int x)       { o->tt = SPT_TBOOL; o->v.b = (x != 0); }
SPT_INLINE void setint(TValue *o, spt_Integer x){ o->tt = SPT_TINT;  o->v.i = x; }
SPT_INLINE void setflt(TValue *o, spt_Number x) { o->tt = SPT_TFLOAT;o->v.n = x; }
SPT_INLINE void setptr(TValue *o, void *x)      { o->tt = SPT_TLIGHTPTR; o->v.p = x; }
SPT_INLINE void setgco(TValue *o, GCObject *x, uint8_t tag){ o->tt = tag; o->v.gc = x; }
SPT_INLINE void setobj(TValue *dst, const TValue *src){ *dst = *src; }

#endif /* SPT_VALUE_H */
