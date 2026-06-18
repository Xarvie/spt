/*
 * vm.c — The bytecode interpreter and the call machinery.
 *
 * Dispatch is direct-threaded (computed goto) on GCC/Clang and a switch on
 * other toolchains — the same strategy PUC-Lua uses, which is what lets the
 * "no slower than Lua" floor hold.
 *
 * Calls follow the Slot-0 receiver convention:
 *     R[A] = callable, R[A+1] = receiver, R[A+2..] = arguments.
 * The callee frame's register 0 is therefore the receiver, and named
 * parameters occupy registers 1..numparams.
 *
 * Nested calls use the C stack (a "hard" call) for clarity in this milestone;
 * converting OP_CALL into a non-recursive re-dispatch is a later optimisation.
 */
#include "spt/mem.h"
#include "compiler.h"   /* Type enum — used by OP_CAST */
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>

#ifdef SPT_HAS_JIT
#include "jit/jit.h"
#endif

const char *const spt_opnames[OP_NUM_OPCODES] = {
  "MOVE","LOADK","LOADINT","LOADBOOL","LOADNULL",
  "IADD","ISUB","IMUL","IDIV","IMOD","FADD","FSUB","FMUL","FDIV",
  "ADD","SUB","MUL","DIV","MOD","NEG",
  "EQ","LT","LE","NOT","JMP","TEST","TESTSET","CONCAT","LEN",
  "NEWLIST","GETLIST","SETLIST","LISTPUSH","NEWMAP","GETMAP","SETMAP",
  "GETINDEX","SETINDEX","CAST","GETGLOBAL","SETGLOBAL","GETUPVAL","SETUPVAL",
  "CLOSURE","CALL","RETURN","HALT"
};

/* ================================================================== */
/* Upvalues                                                            */
/* ================================================================== */
static Upval *find_upval(spt_State *L, TValue *level) {
  Upval **pp = &L->openupval;
  while (*pp && (*pp)->v > level) pp = &(*pp)->unext;
  if (*pp && (*pp)->v == level) return *pp;
  Upval *uv = (Upval *)spt_newobj(L, SPT_TUPVAL, sizeof(Upval));
  uv->v = level; uv->closed.tt = SPT_TNULL; uv->unext = *pp;
  *pp = uv;
  return uv;
}

static void close_upvals(spt_State *L, TValue *level) {
  while (L->openupval && L->openupval->v >= level) {
    Upval *uv = L->openupval;
    L->openupval = uv->unext;
    setobj(&uv->closed, uv->v);
    uv->v = &uv->closed;
  }
}

/* ================================================================== */
/* Arithmetic / comparison helpers (generic, runtime-typed)            */
/* ================================================================== */
static void arith(spt_State *L, int op, TValue *ra, const TValue *rb, const TValue *rc) {
  if (ttisint(rb) && ttisint(rc)) {
    spt_Integer x = ivalue(rb), y = ivalue(rc), r;
    switch (op) {
      case OP_ADD: r = x + y; break;
      case OP_SUB: r = x - y; break;
      case OP_MUL: r = x * y; break;
      case OP_MOD:
        if (y == 0) spt_runtime_error(L, "integer modulo by zero");
        r = x % y; break;
      case OP_DIV: setflt(ra, (spt_Number)x / (spt_Number)y); return;  /* '/' is float */
      default: r = 0;
    }
    setint(ra, r); return;
  }
  if (ttisnumber(rb) && ttisnumber(rc)) {
    spt_Number x = ttisint(rb) ? (spt_Number)ivalue(rb) : fltvalue(rb);
    spt_Number y = ttisint(rc) ? (spt_Number)ivalue(rc) : fltvalue(rc), r;
    switch (op) {
      case OP_ADD: r = x + y; break;
      case OP_SUB: r = x - y; break;
      case OP_MUL: r = x * y; break;
      case OP_DIV: r = x / y; break;
      case OP_MOD: r = x - floor(x / y) * y; break;
      default: r = 0;
    }
    setflt(ra, r); return;
  }
  spt_runtime_error(L, "attempt to perform arithmetic on a non-number value");
}

static int val_eq(const TValue *a, const TValue *b) {
  if (a->tt == b->tt) {
    switch (a->tt) {
      case SPT_TNULL:  return 1;
      case SPT_TBOOL:  return a->v.b == b->v.b;
      case SPT_TINT:   return a->v.i == b->v.i;
      case SPT_TFLOAT: return a->v.n == b->v.n;
      default:         return a->v.gc == b->v.gc;
    }
  }
  if (ttisnumber(a) && ttisnumber(b)) {
    spt_Number x = ttisint(a) ? (spt_Number)ivalue(a) : fltvalue(a);
    spt_Number y = ttisint(b) ? (spt_Number)ivalue(b) : fltvalue(b);
    return x == y;
  }
  return 0;
}

static int val_lt(spt_State *L, const TValue *a, const TValue *b) {
  if (ttisnumber(a) && ttisnumber(b)) {
    spt_Number x = ttisint(a) ? (spt_Number)ivalue(a) : fltvalue(a);
    spt_Number y = ttisint(b) ? (spt_Number)ivalue(b) : fltvalue(b);
    return x < y;
  }
  if (ttisstring(a) && ttisstring(b))
    return strcmp(str_cstr(strvalue(a)), str_cstr(strvalue(b))) < 0;
  spt_runtime_error(L, "attempt to compare incompatible values");
  return 0;
}

static int val_le(spt_State *L, const TValue *a, const TValue *b) {
  if (ttisnumber(a) && ttisnumber(b)) {
    spt_Number x = ttisint(a) ? (spt_Number)ivalue(a) : fltvalue(a);
    spt_Number y = ttisint(b) ? (spt_Number)ivalue(b) : fltvalue(b);
    return x <= y;
  }
  if (ttisstring(a) && ttisstring(b))
    return strcmp(str_cstr(strvalue(a)), str_cstr(strvalue(b))) <= 0;
  spt_runtime_error(L, "attempt to compare incompatible values");
  return 0;
}

/* Coerce a scalar to text for CONCAT. Returns length; writes into buf. */
static const char *concat_cstr(const TValue *v, char *buf, size_t bufsz, size_t *len) {
  switch (v->tt) {
    case SPT_TSTRING: *len = strvalue(v)->len; return str_cstr(strvalue(v));
    case SPT_TINT:    *len = (size_t)snprintf(buf, bufsz, "%lld", (long long)ivalue(v)); return buf;
    case SPT_TFLOAT:  *len = (size_t)snprintf(buf, bufsz, "%.14g", fltvalue(v));         return buf;
    case SPT_TBOOL:   { const char *s = bvalue(v) ? "true" : "false"; *len = strlen(s); return s; }
    case SPT_TNULL:   *len = 4; return "null";
    default:          *len = 0; return "";
  }
}

/* ================================================================== */
/* JIT runtime helpers (called from generated native code)             */
/* ================================================================== */
#ifdef SPT_HAS_JIT
int  spt_val_eq(const TValue *a, const TValue *b)                       { return val_eq(a, b); }
int  spt_val_lt_rt(spt_State *L, const TValue *a, const TValue *b)      { return val_lt(L, a, b); }
int  spt_val_le_rt(spt_State *L, const TValue *a, const TValue *b)      { return val_le(L, a, b); }
int  spt_truthy_v(const TValue *v)                                     { return spt_truthy(v); }

void spt_jit_do_return(spt_State *L, int a, int nret) {
  CallInfo *ci = L->ci;
  TValue *base = ci->base, *func = ci->func;
  close_upvals(L, base);
  for (int i = 0; i < nret; i++) setobj(func + i, &base[a + i]);
  L->top = func + nret;
}
#endif

/* ================================================================== */
/* Calls                                                               */
/* ================================================================== */
SPT_API int spt_execute(spt_State *L);

/* Invoke the callable at stack index `funcidx`. The receiver is at funcidx+1
 * and `nargs` real arguments follow. On return, results occupy funcidx.. and
 * L->top == funcidx + (nresults wanted, or actual if multret). */
static void do_call(spt_State *L, ptrdiff_t funcidx, int nargs, int wantres) {
  TValue *func = L->stack + funcidx;

  if (ttiscfunc(func)) {
    CFunc *cf = cfvalue(func);
    CallInfo ci;
    ci.func = func; ci.base = func + 1; ci.top = L->top;
    ci.prev = L->ci; ci.nresults = wantres; ci.savedpc = NULL;
    L->ci = &ci;
    spt_checkstack(L, SPT_MIN_STACK);

    int n = cf->fn(L);                          /* results pushed at L->top  */
    TValue *res = L->top - n;
    func = L->stack + funcidx;
    int nres = (wantres < 0) ? n : wantres;
    for (int i = 0; i < nres; i++) {
      if (i < n) setobj(func + i, res + i);
      else setnull(func + i);
    }
    L->top = func + nres;
    L->ci = ci.prev;
    return;
  }

  if (ttisclosure(func)) {
    Closure *cl = clvalue(func);
    Proto *p = cl->p;
    spt_checkstack(L, p->maxstack + SPT_MIN_STACK);
    func = L->stack + funcidx;
    TValue *fbase = func + 1;                    /* register 0 = receiver     */
    /* Null every register after the passed arguments: this defaults missing
     * parameters and, crucially, clears any stale values left by a previous
     * frame so the GC never sees a dangling pointer in an unused slot. */
    for (int r = 1 + nargs; r < p->maxstack; r++) setnull(&fbase[r]);

    CallInfo ci;
    ci.func = func; ci.base = fbase; ci.top = fbase + p->maxstack;
    ci.prev = L->ci; ci.nresults = wantres; ci.savedpc = p->code;
    L->ci = &ci;
    L->top = ci.top;

#ifdef SPT_HAS_JIT
    if (p->jit_entry) {
      spt_jit_enter(L, p);                       /* native frame; results at func.. */
    } else {
      if (++p->call_count == SPT_JIT_THRESHOLD) spt_jit_try_compile(L, p);
      (void)spt_execute(L);
    }
#else
    (void)spt_execute(L);                        /* runs to RETURN; results at func.. */
#endif

    int nret = (int)((L->top) - (L->stack + funcidx));  /* RETURN set top=func+nret */
    int nres = (wantres < 0) ? nret : wantres;
    func = L->stack + funcidx;
    for (int i = nret; i < nres; i++) setnull(func + i);
    L->top = func + nres;
    L->ci = ci.prev;
    return;
  }

  spt_runtime_error(L, "attempt to call a non-callable value");
}

/* ================================================================== */
/* Explicit type conversion (OP_CAST)                                  */
/* ================================================================== */
/* Cast the value in `ra` to the target Type `target` (one of TY_INT,
 * TY_FLOAT, TY_STRING, TY_BOOL). This is the dynamic→typed boundary: it
 * performs a runtime check and a real conversion, raising an error when the
 * value cannot be meaningfully represented in the target type. */
static void do_cast(spt_State *L, TValue *ra, Type target) {
  switch (target) {
    case TY_INT: {
      switch (ra->tt) {
        case SPT_TINT:                                    break;  /* identity */
        case SPT_TFLOAT: setint(ra, (spt_Integer)fltvalue(ra)); break;
        case SPT_TBOOL:  setint(ra, bvalue(ra) ? 1 : 0);        break;
        case SPT_TSTRING: {
          char *end; const char *s = str_cstr(strvalue(ra));
          long long v = strtoll(s, &end, 10);
          if (*s == '\0' || *end != '\0')
            spt_runtime_error(L, "cannot cast string \"%s\" to int", s);
          setint(ra, (spt_Integer)v);
          break;
        }
        default:
          spt_runtime_error(L, "cannot cast %s to int", spt_opnames[ra->tt]);
      }
      break;
    }
    case TY_FLOAT: {
      switch (ra->tt) {
        case SPT_TINT:   setflt(ra, (spt_Number)ivalue(ra));    break;
        case SPT_TFLOAT:                                         break;  /* identity */
        case SPT_TBOOL:  setflt(ra, bvalue(ra) ? 1.0 : 0.0);    break;
        case SPT_TSTRING: {
          char *end; const char *s = str_cstr(strvalue(ra));
          double v = strtod(s, &end);
          if (*s == '\0' || *end != '\0')
            spt_runtime_error(L, "cannot cast string \"%s\" to float", s);
          setflt(ra, (spt_Number)v);
          break;
        }
        default:
          spt_runtime_error(L, "cannot cast %s to float", spt_opnames[ra->tt]);
      }
      break;
    }
    case TY_STRING: {
      char buf[64];
      switch (ra->tt) {
        case SPT_TINT: {
          int n = snprintf(buf, sizeof buf, "%lld", (long long)ivalue(ra));
          String *s = spt_str_newlen(L, buf, (size_t)n);
          setgco(ra, (GCObject *)s, SPT_TSTRING);
          break;
        }
        case SPT_TFLOAT: {
          int n = snprintf(buf, sizeof buf, "%.14g", fltvalue(ra));
          String *s = spt_str_newlen(L, buf, (size_t)n);
          setgco(ra, (GCObject *)s, SPT_TSTRING);
          break;
        }
        case SPT_TBOOL: {
          const char *t = bvalue(ra) ? "true" : "false";
          String *s = spt_str_newlen(L, t, strlen(t));
          setgco(ra, (GCObject *)s, SPT_TSTRING);
          break;
        }
        case SPT_TSTRING: break;                              /* identity */
        case SPT_TNULL: {
          String *s = spt_str_newlen(L, "null", 4);
          setgco(ra, (GCObject *)s, SPT_TSTRING);
          break;
        }
        default:
          spt_runtime_error(L, "cannot cast %s to string", spt_opnames[ra->tt]);
      }
      break;
    }
    case TY_BOOL: {
      switch (ra->tt) {
        case SPT_TINT:   setbool(ra, ivalue(ra) != 0);        break;
        case SPT_TFLOAT: setbool(ra, fltvalue(ra) != 0.0);    break;
        case SPT_TBOOL:                                        break;  /* identity */
        case SPT_TNULL:  setbool(ra, 0);                       break;
        default:
          spt_runtime_error(L, "cannot cast %s to bool", spt_opnames[ra->tt]);
      }
      break;
    }
    default:
      spt_runtime_error(L, "invalid cast target type %d", (int)target);
  }
}

/* ================================================================== */
/* The dispatch loop                                                   */
/* ================================================================== */
#define R(i)   (base[i])
#define K(i)   (k[i])

#ifdef SPT_USE_COMPUTED_GOTO
#  define VM_DISPATCH()  goto *disptab[SPT_OPCODE(inst = *pc++)]
#  define VM_CASE(op)    OPL_##op
#  define VM_NEXT()      do { spt_gc_maybe(L); VM_DISPATCH(); } while (0)
#else
#  define VM_CASE(op)    case op
#  define VM_NEXT()      goto vm_loop
#endif

int spt_execute(spt_State *L) {
  CallInfo *ci = L->ci;
  Closure  *cl = clvalue(ci->func);
  Proto    *p  = cl->p;
  const Instr *pc = ci->savedpc;
  TValue   *base = ci->base;
  TValue   *k    = p->k;
  Instr     inst;

#ifdef SPT_USE_COMPUTED_GOTO
  static const void *const disptab[OP_NUM_OPCODES] = {
    [OP_MOVE]=&&OPL_OP_MOVE, [OP_LOADK]=&&OPL_OP_LOADK, [OP_LOADINT]=&&OPL_OP_LOADINT,
    [OP_LOADBOOL]=&&OPL_OP_LOADBOOL, [OP_LOADNULL]=&&OPL_OP_LOADNULL,
    [OP_IADD]=&&OPL_OP_IADD, [OP_ISUB]=&&OPL_OP_ISUB, [OP_IMUL]=&&OPL_OP_IMUL,
    [OP_IDIV]=&&OPL_OP_IDIV, [OP_IMOD]=&&OPL_OP_IMOD,
    [OP_FADD]=&&OPL_OP_FADD, [OP_FSUB]=&&OPL_OP_FSUB, [OP_FMUL]=&&OPL_OP_FMUL, [OP_FDIV]=&&OPL_OP_FDIV,
    [OP_ADD]=&&OPL_OP_ADD, [OP_SUB]=&&OPL_OP_SUB, [OP_MUL]=&&OPL_OP_MUL,
    [OP_DIV]=&&OPL_OP_DIV, [OP_MOD]=&&OPL_OP_MOD, [OP_NEG]=&&OPL_OP_NEG,
    [OP_EQ]=&&OPL_OP_EQ, [OP_LT]=&&OPL_OP_LT, [OP_LE]=&&OPL_OP_LE, [OP_NOT]=&&OPL_OP_NOT,
    [OP_JMP]=&&OPL_OP_JMP, [OP_TEST]=&&OPL_OP_TEST, [OP_TESTSET]=&&OPL_OP_TESTSET,
    [OP_CONCAT]=&&OPL_OP_CONCAT, [OP_LEN]=&&OPL_OP_LEN,
    [OP_NEWLIST]=&&OPL_OP_NEWLIST, [OP_GETLIST]=&&OPL_OP_GETLIST,
    [OP_SETLIST]=&&OPL_OP_SETLIST, [OP_LISTPUSH]=&&OPL_OP_LISTPUSH,
    [OP_NEWMAP]=&&OPL_OP_NEWMAP, [OP_GETMAP]=&&OPL_OP_GETMAP, [OP_SETMAP]=&&OPL_OP_SETMAP,
    [OP_GETINDEX]=&&OPL_OP_GETINDEX, [OP_SETINDEX]=&&OPL_OP_SETINDEX,
    [OP_CAST]=&&OPL_OP_CAST,
    [OP_GETGLOBAL]=&&OPL_OP_GETGLOBAL, [OP_SETGLOBAL]=&&OPL_OP_SETGLOBAL,
    [OP_GETUPVAL]=&&OPL_OP_GETUPVAL, [OP_SETUPVAL]=&&OPL_OP_SETUPVAL,
    [OP_CLOSURE]=&&OPL_OP_CLOSURE, [OP_CALL]=&&OPL_OP_CALL,
    [OP_RETURN]=&&OPL_OP_RETURN, [OP_HALT]=&&OPL_OP_HALT,
  };
  VM_DISPATCH();
#else
vm_loop:
  spt_gc_maybe(L);
  inst = *pc++;
  switch (SPT_OPCODE(inst)) {
#endif

  VM_CASE(OP_MOVE): {
    setobj(&R(SPT_GETA(inst)), &R(SPT_GETB(inst)));
    VM_NEXT();
  }
  VM_CASE(OP_LOADK): {
    setobj(&R(SPT_GETA(inst)), &K(SPT_GETBX(inst)));
    VM_NEXT();
  }
  VM_CASE(OP_LOADINT): {
    setint(&R(SPT_GETA(inst)), SPT_GETSBX(inst));
    VM_NEXT();
  }
  VM_CASE(OP_LOADBOOL): {
    setbool(&R(SPT_GETA(inst)), (int)SPT_GETB(inst));
    VM_NEXT();
  }
  VM_CASE(OP_LOADNULL): {
    int a = SPT_GETA(inst), b = SPT_GETB(inst);
    for (int i = 0; i <= b; i++) setnull(&R(a + i));
    VM_NEXT();
  }

  /* ---- typed integer arithmetic (no tag dispatch) ---- */
  VM_CASE(OP_IADD): { setint(&R(SPT_GETA(inst)), ivalue(&R(SPT_GETB(inst))) + ivalue(&R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_ISUB): { setint(&R(SPT_GETA(inst)), ivalue(&R(SPT_GETB(inst))) - ivalue(&R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_IMUL): { setint(&R(SPT_GETA(inst)), ivalue(&R(SPT_GETB(inst))) * ivalue(&R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_IDIV): {
    spt_Integer y = ivalue(&R(SPT_GETC(inst)));
    if (SPT_UNLIKELY(y == 0)) spt_runtime_error(L, "integer division by zero");
    setint(&R(SPT_GETA(inst)), ivalue(&R(SPT_GETB(inst))) / y); VM_NEXT();
  }
  VM_CASE(OP_IMOD): {
    spt_Integer y = ivalue(&R(SPT_GETC(inst)));
    if (SPT_UNLIKELY(y == 0)) spt_runtime_error(L, "integer modulo by zero");
    setint(&R(SPT_GETA(inst)), ivalue(&R(SPT_GETB(inst))) % y); VM_NEXT();
  }

  /* ---- typed float arithmetic ---- */
  VM_CASE(OP_FADD): { setflt(&R(SPT_GETA(inst)), fltvalue(&R(SPT_GETB(inst))) + fltvalue(&R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_FSUB): { setflt(&R(SPT_GETA(inst)), fltvalue(&R(SPT_GETB(inst))) - fltvalue(&R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_FMUL): { setflt(&R(SPT_GETA(inst)), fltvalue(&R(SPT_GETB(inst))) * fltvalue(&R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_FDIV): { setflt(&R(SPT_GETA(inst)), fltvalue(&R(SPT_GETB(inst))) / fltvalue(&R(SPT_GETC(inst)))); VM_NEXT(); }

  /* ---- generic arithmetic ---- */
  VM_CASE(OP_ADD): { arith(L, OP_ADD, &R(SPT_GETA(inst)), &R(SPT_GETB(inst)), &R(SPT_GETC(inst))); VM_NEXT(); }
  VM_CASE(OP_SUB): { arith(L, OP_SUB, &R(SPT_GETA(inst)), &R(SPT_GETB(inst)), &R(SPT_GETC(inst))); VM_NEXT(); }
  VM_CASE(OP_MUL): { arith(L, OP_MUL, &R(SPT_GETA(inst)), &R(SPT_GETB(inst)), &R(SPT_GETC(inst))); VM_NEXT(); }
  VM_CASE(OP_DIV): { arith(L, OP_DIV, &R(SPT_GETA(inst)), &R(SPT_GETB(inst)), &R(SPT_GETC(inst))); VM_NEXT(); }
  VM_CASE(OP_MOD): { arith(L, OP_MOD, &R(SPT_GETA(inst)), &R(SPT_GETB(inst)), &R(SPT_GETC(inst))); VM_NEXT(); }
  VM_CASE(OP_NEG): {
    TValue *rb = &R(SPT_GETB(inst));
    if (ttisint(rb)) setint(&R(SPT_GETA(inst)), -ivalue(rb));
    else if (ttisfloat(rb)) setflt(&R(SPT_GETA(inst)), -fltvalue(rb));
    else spt_runtime_error(L, "attempt to negate a non-number value");
    VM_NEXT();
  }

  /* ---- comparison / logic ---- */
  VM_CASE(OP_EQ):  { setbool(&R(SPT_GETA(inst)), val_eq(&R(SPT_GETB(inst)), &R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_LT):  { setbool(&R(SPT_GETA(inst)), val_lt(L, &R(SPT_GETB(inst)), &R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_LE):  { setbool(&R(SPT_GETA(inst)), val_le(L, &R(SPT_GETB(inst)), &R(SPT_GETC(inst)))); VM_NEXT(); }
  VM_CASE(OP_NOT): { setbool(&R(SPT_GETA(inst)), spt_falsy(&R(SPT_GETB(inst)))); VM_NEXT(); }

  /* ---- control flow ---- */
  VM_CASE(OP_JMP): { pc += SPT_GETSBX(inst); VM_NEXT(); }
  VM_CASE(OP_TEST): {
    int cond = spt_truthy(&R(SPT_GETA(inst)));
    if (cond != (int)SPT_GETC(inst)) pc++;     /* skip the following JMP/op  */
    VM_NEXT();
  }
  VM_CASE(OP_TESTSET): {
    TValue *rb = &R(SPT_GETB(inst));
    if (spt_truthy(rb) == (int)SPT_GETC(inst)) setobj(&R(SPT_GETA(inst)), rb);
    else pc++;
    VM_NEXT();
  }

  /* ---- string ---- */
  VM_CASE(OP_CONCAT): {
    char ba[48], bb[48]; size_t la, lb;
    const char *sa = concat_cstr(&R(SPT_GETB(inst)), ba, sizeof ba, &la);
    const char *sb = concat_cstr(&R(SPT_GETC(inst)), bb, sizeof bb, &lb);
    char *tmp = (char *)spt_alloc(L, la + lb);
    memcpy(tmp, sa, la); memcpy(tmp + la, sb, lb);
    String *s = spt_str_newlen(L, tmp, la + lb);
    spt_free(L, tmp, la + lb);
    setgco(&R(SPT_GETA(inst)), (GCObject *)s, SPT_TSTRING);
    VM_NEXT();
  }
  VM_CASE(OP_LEN): {
    setint(&R(SPT_GETA(inst)), spt_obj_len(&R(SPT_GETB(inst))));
    VM_NEXT();
  }

  /* ---- List (0-based, bounds-checked) ---- */
  VM_CASE(OP_NEWLIST): {
    Table *t = spt_list_new(L, SPT_GETB(inst));
    setgco(&R(SPT_GETA(inst)), (GCObject *)t, SPT_TLIST);
    VM_NEXT();
  }
  VM_CASE(OP_GETLIST): {
    TValue *tb = &R(SPT_GETB(inst)), *ix = &R(SPT_GETC(inst));
    if (SPT_UNLIKELY(!ttislist(tb))) spt_runtime_error(L, "attempt to index a non-list value");
    if (SPT_UNLIKELY(!ttisint(ix)))  spt_runtime_error(L, "list index must be an integer");
    if (spt_list_get(tblvalue(tb), ivalue(ix), &R(SPT_GETA(inst))) < 0)
      spt_runtime_error(L, "list index %lld out of bounds (length %lld)",
                        (long long)ivalue(ix), (long long)tblvalue(tb)->alen);
    VM_NEXT();
  }
  VM_CASE(OP_SETLIST): {
    TValue *tb = &R(SPT_GETA(inst)), *ix = &R(SPT_GETB(inst));
    if (SPT_UNLIKELY(!ttislist(tb))) spt_runtime_error(L, "attempt to index a non-list value");
    if (SPT_UNLIKELY(!ttisint(ix)))  spt_runtime_error(L, "list index must be an integer");
    if (spt_list_set(L, tblvalue(tb), ivalue(ix), &R(SPT_GETC(inst))) < 0)
      spt_runtime_error(L, "list index %lld out of bounds (length %lld)",
                        (long long)ivalue(ix), (long long)tblvalue(tb)->alen);
    VM_NEXT();
  }
  VM_CASE(OP_LISTPUSH): {
    TValue *tb = &R(SPT_GETA(inst));
    if (SPT_UNLIKELY(!ttislist(tb))) spt_runtime_error(L, "attempt to push to a non-list value");
    spt_list_push(L, tblvalue(tb), &R(SPT_GETB(inst)));
    VM_NEXT();
  }

  /* ---- Map ---- */
  VM_CASE(OP_NEWMAP): {
    Table *t = spt_map_new(L);
    setgco(&R(SPT_GETA(inst)), (GCObject *)t, SPT_TMAP);
    VM_NEXT();
  }
  VM_CASE(OP_GETMAP): {
    TValue *tb = &R(SPT_GETB(inst));
    if (SPT_UNLIKELY(!ttismap(tb))) spt_runtime_error(L, "attempt to index a non-map value");
    spt_map_get(tblvalue(tb), &R(SPT_GETC(inst)), &R(SPT_GETA(inst)));
    VM_NEXT();
  }
  VM_CASE(OP_SETMAP): {
    TValue *tb = &R(SPT_GETA(inst));
    if (SPT_UNLIKELY(!ttismap(tb))) spt_runtime_error(L, "attempt to index a non-map value");
    spt_map_set(L, tblvalue(tb), &R(SPT_GETB(inst)), &R(SPT_GETC(inst)));
    VM_NEXT();
  }

  /* ---- generic index (List | Map | String) ---- */
  VM_CASE(OP_GETINDEX): {
    TValue *tb = &R(SPT_GETB(inst)), *ix = &R(SPT_GETC(inst)), *ra = &R(SPT_GETA(inst));
    if (ttislist(tb)) {
      if (!ttisint(ix)) spt_runtime_error(L, "list index must be an integer");
      if (spt_list_get(tblvalue(tb), ivalue(ix), ra) < 0)
        spt_runtime_error(L, "list index out of bounds");
    } else if (ttismap(tb)) {
      spt_map_get(tblvalue(tb), ix, ra);
    } else {
      spt_runtime_error(L, "attempt to index a non-indexable value");
    }
    VM_NEXT();
  }
  VM_CASE(OP_SETINDEX): {
    TValue *tb = &R(SPT_GETA(inst)), *ix = &R(SPT_GETB(inst)), *rv = &R(SPT_GETC(inst));
    if (ttislist(tb)) {
      if (!ttisint(ix)) spt_runtime_error(L, "list index must be an integer");
      if (spt_list_set(L, tblvalue(tb), ivalue(ix), rv) < 0)
        spt_runtime_error(L, "list index out of bounds");
    } else if (ttismap(tb)) {
      spt_map_set(L, tblvalue(tb), ix, rv);
    } else {
      spt_runtime_error(L, "attempt to index a non-indexable value");
    }
    VM_NEXT();
  }

  /* ---- explicit type conversion ---- */
  VM_CASE(OP_CAST): {
    do_cast(L, &R(SPT_GETA(inst)), (Type)SPT_GETB(inst));
    VM_NEXT();
  }

  /* ---- globals ---- */
  VM_CASE(OP_GETGLOBAL): {
    spt_map_get(L->G->globals, &K(SPT_GETBX(inst)), &R(SPT_GETA(inst)));
    VM_NEXT();
  }
  VM_CASE(OP_SETGLOBAL): {
    spt_map_set(L, L->G->globals, &K(SPT_GETBX(inst)), &R(SPT_GETA(inst)));
    VM_NEXT();
  }

  /* ---- upvalues ---- */
  VM_CASE(OP_GETUPVAL): {
    setobj(&R(SPT_GETA(inst)), cl->ups[SPT_GETB(inst)]->v);
    VM_NEXT();
  }
  VM_CASE(OP_SETUPVAL): {
    Upval *uv = cl->ups[SPT_GETB(inst)];
    setobj(uv->v, &R(SPT_GETA(inst)));
    spt_barrier(L, (GCObject *)uv, uv->v);
    VM_NEXT();
  }

  /* ---- functions ---- */
  VM_CASE(OP_CLOSURE): {
    Proto *np = p->p[SPT_GETBX(inst)];
    Closure *ncl = spt_closure_new(L, np);
    for (uint32_t u = 0; u < np->nups; u++) {
      ncl->ups[u] = np->upvals[u].in_stack
                  ? find_upval(L, base + np->upvals[u].idx)
                  : cl->ups[np->upvals[u].idx];
    }
    setgco(&R(SPT_GETA(inst)), (GCObject *)ncl, SPT_TCLOSURE);
    VM_NEXT();
  }
  VM_CASE(OP_CALL): {
    int a = SPT_GETA(inst), b = SPT_GETB(inst), c = SPT_GETC(inst);
    TValue *func = &R(a);
    if (b) L->top = func + b + 1;                   /* delimit receiver+args   */
    int nargs   = b ? (b - 1) : (int)(L->top - func - 2);  /* exclude receiver */
    int wantres = c ? (c - 1) : -1;
    ci->savedpc = pc;                              /* publish resume point    */
    do_call(L, func - L->stack, nargs, wantres);
    base = ci->base;                               /* stack may have moved     */
    L->top = ci->top;                              /* restore full frame extent */
    VM_NEXT();
  }
  VM_CASE(OP_RETURN): {
    int a = SPT_GETA(inst), b = SPT_GETB(inst);
    int nret = b ? (b - 1) : (int)(L->top - &R(a));
    TValue *func = ci->func;
    close_upvals(L, base);
    for (int i = 0; i < nret; i++) setobj(func + i, &R(a + i));
    L->top = func + nret;
    return nret;
  }
  VM_CASE(OP_HALT): {
    close_upvals(L, base);
    L->top = ci->func;
    return 0;
  }

#ifndef SPT_USE_COMPUTED_GOTO
  default:
    spt_runtime_error(L, "illegal opcode %u", (unsigned)SPT_OPCODE(inst));
  }
  return 0;  /* unreachable — spt_runtime_error longjmps */
#endif
}

/* ================================================================== */
/* Public entry                                                        */
/* ================================================================== */
int spt_call(spt_State *L, int nargs, int nresults) {
  jmp_buf jb; void *save = L->errjmp; L->errjmp = &jb;
  ptrdiff_t funcidx = (L->top - nargs - 2) - L->stack;   /* callable,recv,args */
  int status = 0;
  if (setjmp(jb) == 0) {
    do_call(L, funcidx, nargs, nresults);
  } else {
    status = 1;            /* runtime error; L->errmsg holds the message */
  }
  L->errjmp = save;
  return status;
}
