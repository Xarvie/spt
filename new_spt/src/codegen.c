/*
 * codegen.c — AST -> bytecode Proto, plus the AST arena and the public
 * spt_load() entry point.
 *
 * Register model (a small stack machine over the register file):
 *   - register 0 is the Slot-0 receiver; parameters occupy 1..numparams.
 *   - locals are function-scoped and occupy the low registers [1..nactive].
 *   - `freereg` is the top of the temporary stack; every expression nets +1
 *     register (its result), freeing any temporaries it used above.
 *
 * Name resolution: a NAME is a local if declared in the function, otherwise a
 * global. Plain `NAME = e` declares/updates a local; `global NAME = e` writes a
 * global; `function NAME(...)` binds a global so functions are callable anywhere
 * (including recursively). Capturing outer locals (upvalues) is a later step.
 */
#include "compiler.h"
#include "spt/mem.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>

/* ================================================================== */
/* AST arena                                                           */
/* ================================================================== */
struct Arena { struct Arena *next; size_t used, cap; char buf[]; };
#define ARENA_BLOCK (16 * 1024)

void *arena_alloc(AstArena *a, size_t n) {
  n = (n + 15) & ~(size_t)15;
  Arena *h = a->head;
  if (!h || h->used + n > h->cap) {
    size_t cap = n > ARENA_BLOCK ? n : ARENA_BLOCK;
    Arena *blk = (Arena *)malloc(sizeof(Arena) + cap);
    blk->next = a->head; blk->used = 0; blk->cap = cap;
    a->head = blk; h = blk;
  }
  void *p = h->buf + h->used;
  h->used += n;
  return p;
}

void arena_free(AstArena *a) {
  Arena *b = a->head;
  while (b) { Arena *n = b->next; free(b); b = n; }
  a->head = NULL;
}

/* ================================================================== */
/* FuncState                                                           */
/* ================================================================== */

/* Shared across all functions in one compile: declared function signatures,
 * so a call to a function with a `-> T` return type infers type T. */
typedef struct { const char *s; int len; Type ret; } FnSig;
typedef struct { FnSig sigs[128]; int n; } CompileCtx;

#define SPT_MAXUPVALS 64
#define SPT_MAXLOOPS  16      /* nesting depth of loops                */
#define SPT_MAXBREAKS 64      /* break jumps per loop                  */

typedef struct {
  int cont_pc;                /* pc of loop top (continue target)      */
  int breaks[SPT_MAXBREAKS];  /* forward jump positions to patch       */
  int nbreaks;
} LoopFrame;

typedef struct FuncState {
  spt_State *L;
  Instr  *code;    int ncode,  codecap;
  TValue *k;       int nk,     kcap;
  Proto **protos;  int nprotos, protocap;
  struct { const char *s; int len; Type type; uint8_t is_const; } locals[SPT_MAXREGS];
  struct { uint8_t in_stack, idx; const char *s; int len; Type type; uint8_t is_const; } upvals[SPT_MAXUPVALS];
  int nups;
  int nactive;     /* active locals (registers 1..nactive)              */
  int freereg;     /* next free temporary register                      */
  int maxstack;    /* high-water register count                         */
  int numparams;
  CompileCtx *ctx;
  struct FuncState *parent;   /* enclosing function (NULL for the chunk)  */
  LoopFrame loops[SPT_MAXLOOPS];
  int nloops;                  /* current loop nesting depth              */
  char  *err; size_t errsz; jmp_buf *jb;
} FuncState;

static void cg_error(FuncState *fs, const char *msg) {
  snprintf(fs->err, fs->errsz, "%s", msg);
  longjmp(*fs->jb, 1);
}

static void cg_errorf(FuncState *fs, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  vsnprintf(fs->err, fs->errsz, fmt, ap);
  va_end(ap);
  longjmp(*fs->jb, 1);
}

static const char *type_name(Type t) {
  switch (t) {
    case TY_INT: return "int";      case TY_FLOAT: return "float";
    case TY_STRING: return "string"; case TY_BOOL: return "bool";
    case TY_NULL: return "null";    case TY_LIST: return "list";
    case TY_MAP: return "map";      default: return "dynamic";
  }
}

/* Sound assignability: dynamic accepts anything; otherwise types must match
 * exactly (no implicit conversions without an explicit cast — a later step). */
static int assignable(Type decl, Type val) {
  return decl == TY_DYN || decl == val;
}

static Type ctx_ret(CompileCtx *c, const char *s, int len) {
  if (!c) return TY_DYN;
  for (int i = 0; i < c->n; i++)
    if (c->sigs[i].len == len && memcmp(c->sigs[i].s, s, (size_t)len) == 0)
      return c->sigs[i].ret;
  return TY_DYN;
}

static void emit(FuncState *fs, Instr i) {
  if (fs->ncode == fs->codecap) {
    fs->codecap = fs->codecap ? fs->codecap * 2 : 32;
    fs->code = (Instr *)realloc(fs->code, (size_t)fs->codecap * sizeof(Instr));
  }
  fs->code[fs->ncode++] = i;
}

static int reserve(FuncState *fs) {
  int r = fs->freereg++;
  if (fs->freereg > fs->maxstack) fs->maxstack = fs->freereg;
  if (fs->freereg >= SPT_MAXREGS) cg_error(fs, "function too complex (out of registers)");
  return r;
}
static void setfree(FuncState *fs, int r) { fs->freereg = r; }

static int kadd(FuncState *fs, const TValue *v) {
  for (int i = 0; i < fs->nk; i++) {           /* dedup */
    const TValue *e = &fs->k[i];
    if (e->tt != v->tt) continue;
    if ((e->tt == SPT_TINT && e->v.i == v->v.i) ||
        (e->tt == SPT_TFLOAT && e->v.n == v->v.n) ||
        (e->tt == SPT_TSTRING && e->v.gc == v->v.gc)) return i;
  }
  if (fs->nk >= 65535) cg_error(fs, "too many constants");
  if (fs->nk == fs->kcap) {
    fs->kcap = fs->kcap ? fs->kcap * 2 : 16;
    fs->k = (TValue *)realloc(fs->k, (size_t)fs->kcap * sizeof(TValue));
  }
  fs->k[fs->nk] = *v;
  return fs->nk++;
}
static int k_int(FuncState *fs, int64_t v) { TValue t; setint(&t, v); return kadd(fs, &t); }
static int k_flt(FuncState *fs, double v)  { TValue t; setflt(&t, v); return kadd(fs, &t); }
static int k_str(FuncState *fs, const char *s, int len) {
  String *str = spt_str_newlen(fs->L, s, (size_t)len);
  TValue t; setgco(&t, (GCObject *)str, SPT_TSTRING);
  return kadd(fs, &t);
}

static int find_local(FuncState *fs, const char *s, int len) {
  for (int i = fs->nactive - 1; i >= 0; i--)
    if (fs->locals[i].len == len && memcmp(fs->locals[i].s, s, (size_t)len) == 0)
      return 1 + i;   /* register index */
  return -1;
}

static int find_upval_slot(FuncState *fs, const char *s, int len) {
  for (int i = 0; i < fs->nups; i++)
    if (fs->upvals[i].len == len && memcmp(fs->upvals[i].s, s, (size_t)len) == 0) return i;
  return -1;
}

static int add_upval(FuncState *fs, uint8_t in_stack, uint8_t idx,
                     const char *s, int len, Type ty, uint8_t isc) {
  int e = find_upval_slot(fs, s, len);
  if (e >= 0) return e;
  if (fs->nups >= SPT_MAXUPVALS) cg_error(fs, "too many captured variables");
  e = fs->nups++;
  fs->upvals[e].in_stack = in_stack; fs->upvals[e].idx = idx;
  fs->upvals[e].s = s; fs->upvals[e].len = len;
  fs->upvals[e].type = ty; fs->upvals[e].is_const = isc;
  return e;
}

/* Resolve `name` as an upvalue of `fs`, capturing through every intervening
 * function. Returns the upvalue index in `fs`, or -1 if `name` is not an
 * enclosing local/upvalue (i.e. it must be a global). */
static int resolve_upval(FuncState *fs, const char *s, int len) {
  if (!fs->parent) return -1;
  int existing = find_upval_slot(fs, s, len);
  if (existing >= 0) return existing;
  int slot = find_local(fs->parent, s, len);          /* parent's local register */
  if (slot >= 0)
    return add_upval(fs, 1, (uint8_t)slot, s, len,
                     fs->parent->locals[slot - 1].type, fs->parent->locals[slot - 1].is_const);
  int puv = resolve_upval(fs->parent, s, len);        /* parent's upvalue */
  if (puv >= 0)
    return add_upval(fs, 0, (uint8_t)puv, s, len,
                     fs->parent->upvals[puv].type, fs->parent->upvals[puv].is_const);
  return -1;
}

/* Read-only type of a name (local / upvalue / global) for inference. */
static Type name_type(FuncState *fs, const char *s, int len) {
  int slot = find_local(fs, s, len);
  if (slot >= 0) return fs->locals[slot - 1].type;
  for (FuncState *p = fs; p; p = p->parent) {
    int e = find_upval_slot(p, s, len);
    if (e >= 0) return p->upvals[e].type;
    if (p->parent) {
      int psl = find_local(p->parent, s, len);
      if (psl >= 0) return p->parent->locals[psl - 1].type;
    }
  }
  return TY_DYN;
}

/* jump helpers */
static int  emit_jmp(FuncState *fs) { emit(fs, SPT_MK_AsBx(OP_JMP, 0, 0)); return fs->ncode - 1; }
static void patch_here(FuncState *fs, int at) {
  int sbx = fs->ncode - (at + 1);
  fs->code[at] = SPT_MK_AsBx(OP_JMP, 0, sbx);
}

/* ================================================================== */
/* Expression codegen — leaves the result in a freshly reserved reg    */
/* ================================================================== */
static int expr_next(FuncState *fs, Node *e);
static void stmt(FuncState *fs, Node *s);
static Proto *finalize(FuncState *fs);
static Proto *compile_function(FuncState *parent, Node *fn);

/* Static type of an expression (compile-time inference). */
static Type infer(FuncState *fs, Node *e) {
  switch (e->kind) {
    case N_INT:   return TY_INT;
    case N_FLOAT: return TY_FLOAT;
    case N_STR:   return TY_STRING;
    case N_BOOL:  return TY_BOOL;
    case N_NULL:  return TY_NULL;
    case N_LIST:  return TY_LIST;
    case N_NAME:
      return name_type(fs, e->u.str.s, e->u.str.len);
    case N_UNOP:
      if (e->u.bin.op == TK_NOT) return TY_BOOL;
      { Type t = infer(fs, e->u.bin.l); return (t == TY_INT || t == TY_FLOAT) ? t : TY_DYN; }
    case N_BINOP: {
      TokenType op = e->u.bin.op;
      if (op == TK_EQ || op == TK_NE || op == TK_LT ||
          op == TK_LE || op == TK_GT || op == TK_GE) return TY_BOOL;
      if (op == TK_DOTDOT) return TY_STRING;
      Type lt = infer(fs, e->u.bin.l), rt = infer(fs, e->u.bin.r);
      if (lt == TY_INT && rt == TY_INT) return TY_INT;
      if (lt == TY_FLOAT && rt == TY_FLOAT && op != TK_PERCENT) return TY_FLOAT;
      return TY_DYN;
    }
    case N_CALL:
      return e->u.call.fn->kind == N_NAME
           ? ctx_ret(fs->ctx, e->u.call.fn->u.str.s, e->u.call.fn->u.str.len)
           : TY_DYN;
    case N_INDEX: return TY_DYN;
    case N_CAST:
      return e->u.cast.target;
    case N_LEN:
      return TY_INT;
    default:      return TY_DYN;
  }
}

/* Emit a binary op, choosing typed opcodes when both operands are provably the
 * same numeric type (these skip all runtime tag dispatch — and the typed int
 * forms are exactly what the JIT can lower). Result lands in `a`. */
static void emit_binop(FuncState *fs, TokenType op, int a, int b, Type lt, Type rt) {
  int ii = (lt == TY_INT && rt == TY_INT);
  int ff = (lt == TY_FLOAT && rt == TY_FLOAT);
  switch (op) {
    case TK_PLUS:    emit(fs, SPT_MK_ABC(ii ? OP_IADD : ff ? OP_FADD : OP_ADD, a, a, b)); break;
    case TK_MINUS:   emit(fs, SPT_MK_ABC(ii ? OP_ISUB : ff ? OP_FSUB : OP_SUB, a, a, b)); break;
    case TK_STAR:    emit(fs, SPT_MK_ABC(ii ? OP_IMUL : ff ? OP_FMUL : OP_MUL, a, a, b)); break;
    case TK_SLASH:   emit(fs, SPT_MK_ABC(ii ? OP_IDIV : ff ? OP_FDIV : OP_DIV, a, a, b)); break;
    case TK_PERCENT: emit(fs, SPT_MK_ABC(ii ? OP_IMOD : OP_MOD, a, a, b)); break;
    case TK_EQ:      emit(fs, SPT_MK_ABC(OP_EQ, a, a, b)); break;
    case TK_NE:      emit(fs, SPT_MK_ABC(OP_EQ, a, a, b));
                     emit(fs, SPT_MK_ABC(OP_NOT, a, a, 0)); break;
    case TK_LT:      emit(fs, SPT_MK_ABC(OP_LT, a, a, b)); break;
    case TK_LE:      emit(fs, SPT_MK_ABC(OP_LE, a, a, b)); break;
    case TK_GT:      emit(fs, SPT_MK_ABC(OP_LT, a, b, a)); break;   /* a>b  == b<a  */
    case TK_GE:      emit(fs, SPT_MK_ABC(OP_LE, a, b, a)); break;   /* a>=b == b<=a */
    case TK_DOTDOT:  emit(fs, SPT_MK_ABC(OP_CONCAT, a, a, b)); break;
    default: cg_error(fs, "internal: bad binary operator");
  }
}

static int expr_next(FuncState *fs, Node *e) {
  switch (e->kind) {
    case N_INT: {
      int r = reserve(fs);
      if (e->u.ival >= -32768 && e->u.ival <= 32767)
        emit(fs, SPT_MK_AsBx(OP_LOADINT, r, (int)e->u.ival));
      else
        emit(fs, SPT_MK_ABx(OP_LOADK, r, k_int(fs, e->u.ival)));
      return r;
    }
    case N_FLOAT: { int r = reserve(fs); emit(fs, SPT_MK_ABx(OP_LOADK, r, k_flt(fs, e->u.fval))); return r; }
    case N_STR:   { int r = reserve(fs); emit(fs, SPT_MK_ABx(OP_LOADK, r, k_str(fs, e->u.str.s, e->u.str.len))); return r; }
    case N_BOOL:  { int r = reserve(fs); emit(fs, SPT_MK_ABC(OP_LOADBOOL, r, e->u.bval, 0)); return r; }
    case N_NULL:  { int r = reserve(fs); emit(fs, SPT_MK_ABC(OP_LOADNULL, r, 0, 0)); return r; }
    case N_NAME: {
      int slot = find_local(fs, e->u.str.s, e->u.str.len);
      int r = reserve(fs);
      if (slot >= 0) emit(fs, SPT_MK_ABC(OP_MOVE, r, slot, 0));
      else {
        int uv = resolve_upval(fs, e->u.str.s, e->u.str.len);
        if (uv >= 0) emit(fs, SPT_MK_ABC(OP_GETUPVAL, r, uv, 0));
        else         emit(fs, SPT_MK_ABx(OP_GETGLOBAL, r, k_str(fs, e->u.str.s, e->u.str.len)));
      }
      return r;
    }
    case N_UNOP: {
      int a = expr_next(fs, e->u.bin.l);
      if (e->u.bin.op == TK_MINUS) emit(fs, SPT_MK_ABC(OP_NEG, a, a, 0));
      else                          emit(fs, SPT_MK_ABC(OP_NOT, a, a, 0));
      return a;
    }
    case N_CAST: {
      int a = expr_next(fs, e->u.cast.e);
      emit(fs, SPT_MK_ABC(OP_CAST, a, (unsigned)e->u.cast.target, 0));
      return a;
    }
    case N_LEN: {
      int a = expr_next(fs, e->u.one.e);
      emit(fs, SPT_MK_ABC(OP_LEN, a, a, 0));
      return a;
    }
    case N_BINOP: {
      Type lt = infer(fs, e->u.bin.l);
      Type rt = infer(fs, e->u.bin.r);
      int a = expr_next(fs, e->u.bin.l);
      int b = expr_next(fs, e->u.bin.r);
      emit_binop(fs, e->u.bin.op, a, b, lt, rt);
      setfree(fs, a + 1);     /* pop b */
      return a;
    }
    case N_INDEX: {
      int t = expr_next(fs, e->u.index.obj);
      int i = expr_next(fs, e->u.index.idx);
      emit(fs, SPT_MK_ABC(OP_GETINDEX, t, t, i));
      setfree(fs, t + 1);
      return t;
    }
    case N_LIST: {
      int r = reserve(fs);
      emit(fs, SPT_MK_ABC(OP_NEWLIST, r, e->u.list.n > 255 ? 255 : e->u.list.n, 0));
      for (int idx = 0; idx < e->u.list.n; idx++) {
        int v = expr_next(fs, e->u.list.elems[idx]);
        emit(fs, SPT_MK_ABC(OP_LISTPUSH, r, v, 0));
        setfree(fs, r + 1);
      }
      return r;
    }
    case N_CALL: {
      int rf = expr_next(fs, e->u.call.fn);      /* callable in rf      */
      int recv = reserve(fs);                     /* receiver = null     */
      emit(fs, SPT_MK_ABC(OP_LOADNULL, recv, 0, 0));
      for (int idx = 0; idx < e->u.call.nargs; idx++)
        (void)expr_next(fs, e->u.call.args[idx]); /* args contiguous     */
      emit(fs, SPT_MK_ABC(OP_CALL, rf, e->u.call.nargs + 1, 2)); /* 1 result */
      setfree(fs, rf + 1);                        /* result in rf        */
      return rf;
    }
    default:
      cg_error(fs, "internal: expression expected");
      return 0;
  }
}

/* ================================================================== */
/* Statement codegen                                                   */
/* ================================================================== */
static void stmt(FuncState *fs, Node *s) {
  switch (s->kind) {
    case N_BLOCK:
      for (int i = 0; i < s->u.list.n; i++) stmt(fs, s->u.list.elems[i]);
      break;

    case N_EXPRSTMT: {
      int r = expr_next(fs, s->u.one.e);
      setfree(fs, r);                 /* discard result */
      break;
    }

    case N_GLOBAL: {
      int r = expr_next(fs, s->u.glob.value);
      emit(fs, SPT_MK_ABx(OP_SETGLOBAL, r, k_str(fs, s->u.glob.s, s->u.glob.len)));
      setfree(fs, fs->nactive + 1);
      break;
    }

    case N_ASSIGN: {
      Node *tgt = s->u.assign.target;
      if (tgt->kind == N_NAME) {
        int slot = find_local(fs, tgt->u.str.s, tgt->u.str.len);
        if (slot >= 0) {                 /* update existing local */
          if (fs->locals[slot - 1].is_const)
            cg_errorf(fs, "line %d: cannot assign to const '%.*s'",
                      s->line, tgt->u.str.len, tgt->u.str.s);
          Type lt = fs->locals[slot - 1].type;
          if (lt != TY_DYN) {
            Type vt = infer(fs, s->u.assign.value);
            if (!assignable(lt, vt))
              cg_errorf(fs, "line %d: type error: cannot assign %s to %s variable '%.*s'",
                        s->line, type_name(vt), type_name(lt), tgt->u.str.len, tgt->u.str.s);
          }
          int r = expr_next(fs, s->u.assign.value);
          emit(fs, SPT_MK_ABC(OP_MOVE, slot, r, 0));
          setfree(fs, fs->nactive + 1);
        } else {                          /* not a local: maybe an upvalue */
          int uv = resolve_upval(fs, tgt->u.str.s, tgt->u.str.len);
          if (uv >= 0) {                   /* assign to a captured variable */
            if (fs->upvals[uv].is_const)
              cg_errorf(fs, "line %d: cannot assign to const '%.*s'",
                        s->line, tgt->u.str.len, tgt->u.str.s);
            Type ut = fs->upvals[uv].type;
            if (ut != TY_DYN) {
              Type vt = infer(fs, s->u.assign.value);
              if (!assignable(ut, vt))
                cg_errorf(fs, "line %d: type error: cannot assign %s to %s variable '%.*s'",
                          s->line, type_name(vt), type_name(ut), tgt->u.str.len, tgt->u.str.s);
            }
            int r = expr_next(fs, s->u.assign.value);
            emit(fs, SPT_MK_ABC(OP_SETUPVAL, r, uv, 0));
            setfree(fs, fs->nactive + 1);
          } else {                          /* declare a new dynamic local */
            int r = expr_next(fs, s->u.assign.value);   /* r == nactive+1 */
            fs->locals[fs->nactive].s = tgt->u.str.s;
            fs->locals[fs->nactive].len = tgt->u.str.len;
            fs->locals[fs->nactive].type = TY_DYN;
            fs->locals[fs->nactive].is_const = 0;
            fs->nactive++;
            (void)r;
          }
        }
      } else {                            /* obj[idx] = value */
        int t = expr_next(fs, tgt->u.index.obj);
        int i = expr_next(fs, tgt->u.index.idx);
        int v = expr_next(fs, s->u.assign.value);
        emit(fs, SPT_MK_ABC(OP_SETINDEX, t, i, v));
        setfree(fs, fs->nactive + 1);
      }
      break;
    }

    case N_VARDECL: {
      Type dty = s->u.vardecl.ty;
      if (dty != TY_DYN) {
        Type ity = infer(fs, s->u.vardecl.value);
        if (!assignable(dty, ity))
          cg_errorf(fs, "line %d: type error: cannot initialize %s '%.*s' with %s",
                    s->line, type_name(dty), s->u.vardecl.len, s->u.vardecl.s, type_name(ity));
      }
      int r = expr_next(fs, s->u.vardecl.value);    /* r == nactive+1 */
      fs->locals[fs->nactive].s = s->u.vardecl.s;
      fs->locals[fs->nactive].len = s->u.vardecl.len;
      fs->locals[fs->nactive].type = dty;
      fs->locals[fs->nactive].is_const = (uint8_t)s->u.vardecl.is_const;
      fs->nactive++;
      (void)r;
      break;
    }

    case N_IF: {
      int r = expr_next(fs, s->u.ctrl.cond);
      emit(fs, SPT_MK_ABC(OP_TEST, r, 0, 0));   /* if true, skip the jump */
      setfree(fs, fs->nactive + 1);
      int j_else = emit_jmp(fs);
      stmt(fs, s->u.ctrl.a);
      if (s->u.ctrl.b) {
        int j_end = emit_jmp(fs);
        patch_here(fs, j_else);
        stmt(fs, s->u.ctrl.b);
        patch_here(fs, j_end);
      } else {
        patch_here(fs, j_else);
      }
      break;
    }

    case N_WHILE: {
      int top = fs->ncode;
      /* push a loop frame so break/continue inside the body can resolve */
      if (fs->nloops >= SPT_MAXLOOPS) cg_error(fs, "too many nested loops");
      LoopFrame *lf = &fs->loops[fs->nloops++];
      lf->cont_pc = top;
      lf->nbreaks = 0;

      int r = expr_next(fs, s->u.ctrl.cond);
      emit(fs, SPT_MK_ABC(OP_TEST, r, 0, 0));
      setfree(fs, fs->nactive + 1);
      int j_exit = emit_jmp(fs);
      stmt(fs, s->u.ctrl.a);
      emit(fs, SPT_MK_AsBx(OP_JMP, 0, top - (fs->ncode + 1)));   /* loop back */
      patch_here(fs, j_exit);

      /* patch all `break` jumps to land here (after the loop) */
      for (int i = 0; i < lf->nbreaks; i++) patch_here(fs, lf->breaks[i]);
      fs->nloops--;
      break;
    }

    case N_BREAK: {
      if (fs->nloops == 0) cg_error(fs, "'break' outside of loop");
      LoopFrame *lf = &fs->loops[fs->nloops - 1];
      if (lf->nbreaks >= SPT_MAXBREAKS) cg_error(fs, "too many breaks in one loop");
      lf->breaks[lf->nbreaks++] = emit_jmp(fs);
      break;
    }

    case N_CONTINUE: {
      if (fs->nloops == 0) cg_error(fs, "'continue' outside of loop");
      LoopFrame *lf = &fs->loops[fs->nloops - 1];
      emit(fs, SPT_MK_AsBx(OP_JMP, 0, lf->cont_pc - (fs->ncode + 1)));
      break;
    }

    case N_RETURN: {
      if (s->u.one.e) {
        int r = expr_next(fs, s->u.one.e);
        emit(fs, SPT_MK_ABC(OP_RETURN, r, 2, 0));   /* return 1 value */
        setfree(fs, fs->nactive + 1);
      } else {
        emit(fs, SPT_MK_ABC(OP_RETURN, 0, 1, 0));   /* return nothing */
      }
      break;
    }

    case N_FUNC: {
      if (fs->parent == NULL) {
        /* top-level function: bound as a global, callable everywhere */
        Proto *np = compile_function(fs, s);
        if (fs->nprotos == fs->protocap) {
          fs->protocap = fs->protocap ? fs->protocap * 2 : 8;
          fs->protos = (Proto **)realloc(fs->protos, (size_t)fs->protocap * sizeof(Proto *));
        }
        int pidx = fs->nprotos;
        fs->protos[fs->nprotos++] = np;
        int r = reserve(fs);
        emit(fs, SPT_MK_ABx(OP_CLOSURE, r, pidx));
        emit(fs, SPT_MK_ABx(OP_SETGLOBAL, r, k_str(fs, s->u.func.s, s->u.func.len)));
        setfree(fs, fs->nactive + 1);
      } else {
        /* nested function: a local closure value that can capture enclosing
         * locals as upvalues and be returned. Declare the name first so the
         * body can refer to it (recursion). */
        int slot = 1 + fs->nactive;            /* the closure's home register */
        fs->locals[fs->nactive].s = s->u.func.s;
        fs->locals[fs->nactive].len = s->u.func.len;
        fs->locals[fs->nactive].type = TY_DYN;
        fs->locals[fs->nactive].is_const = 0;
        fs->nactive++;
        if (slot >= fs->maxstack) fs->maxstack = slot + 1;
        Proto *np = compile_function(fs, s);   /* captures resolve against fs */
        if (fs->nprotos == fs->protocap) {
          fs->protocap = fs->protocap ? fs->protocap * 2 : 8;
          fs->protos = (Proto **)realloc(fs->protos, (size_t)fs->protocap * sizeof(Proto *));
        }
        int pidx = fs->nprotos;
        fs->protos[fs->nprotos++] = np;
        emit(fs, SPT_MK_ABx(OP_CLOSURE, slot, pidx));   /* into its local slot */
        setfree(fs, fs->nactive + 1);
      }
      break;
      break;
    }

    default:
      cg_error(fs, "internal: statement expected");
  }
}

/* ================================================================== */
/* Finalisation: copy temp buffers into a GC Proto                     */
/* ================================================================== */
static Proto *finalize(FuncState *fs) {
  Proto *p = spt_proto_new(fs->L);
  p->numparams = (uint8_t)fs->numparams;
  p->maxstack = (uint8_t)(fs->maxstack < 2 ? 2 : fs->maxstack);

  p->ncode = (uint32_t)fs->ncode;
  p->code = (Instr *)spt_alloc(fs->L, (size_t)fs->ncode * sizeof(Instr));
  memcpy(p->code, fs->code, (size_t)fs->ncode * sizeof(Instr));

  if (fs->nk) {
    p->nk = (uint32_t)fs->nk;
    p->k = (TValue *)spt_alloc(fs->L, (size_t)fs->nk * sizeof(TValue));
    memcpy(p->k, fs->k, (size_t)fs->nk * sizeof(TValue));
  }
  if (fs->nprotos) {
    p->np = (uint32_t)fs->nprotos;
    p->p = (Proto **)spt_alloc(fs->L, (size_t)fs->nprotos * sizeof(Proto *));
    memcpy(p->p, fs->protos, (size_t)fs->nprotos * sizeof(Proto *));
  }
  if (fs->nups) {
    p->nups = (uint32_t)fs->nups;
    p->upvals = (UpvalDesc *)spt_alloc(fs->L, (size_t)fs->nups * sizeof(UpvalDesc));
    for (int i = 0; i < fs->nups; i++) {
      p->upvals[i].in_stack = fs->upvals[i].in_stack;
      p->upvals[i].idx = fs->upvals[i].idx;
    }
  }

  free(fs->code); free(fs->k); free(fs->protos);
  return p;
}

static void fs_init(FuncState *fs, spt_State *L, int numparams, CompileCtx *ctx,
                    char *err, size_t errsz, jmp_buf *jb) {
  fs->L = L;
  fs->code = NULL; fs->ncode = 0; fs->codecap = 0;
  fs->k = NULL; fs->nk = 0; fs->kcap = 0;
  fs->protos = NULL; fs->nprotos = 0; fs->protocap = 0;
  fs->nups = 0;
  fs->nactive = numparams;
  fs->freereg = numparams + 1;        /* reg 0 = receiver, 1..n = params */
  fs->maxstack = fs->freereg;
  fs->numparams = numparams;
  fs->ctx = ctx;
  fs->parent = NULL;
  fs->nloops = 0;
  fs->err = err; fs->errsz = errsz; fs->jb = jb;
}

static Proto *compile_function(FuncState *parent, Node *fn) {
  FuncState fs;
  fs_init(&fs, parent->L, fn->u.func.nparams, parent->ctx, parent->err, parent->errsz, parent->jb);
  fs.parent = parent;
  for (int i = 0; i < fn->u.func.nparams; i++) {
    fs.locals[i].s = fn->u.func.params[i]->u.str.s;
    fs.locals[i].len = fn->u.func.params[i]->u.str.len;
    fs.locals[i].type = fn->u.func.ptypes ? fn->u.func.ptypes[i] : TY_DYN;
    fs.locals[i].is_const = 0;
  }
  stmt(&fs, fn->u.func.body);
  emit(&fs, SPT_MK_ABC(OP_RETURN, 0, 1, 0));   /* implicit `return;` */
  return finalize(&fs);
}

/* Collect top-level function signatures so calls can infer return types. */
static void prescan(CompileCtx *ctx, Node *chunk) {
  ctx->n = 0;
  for (int i = 0; i < chunk->u.list.n; i++) {
    Node *s = chunk->u.list.elems[i];
    if (s->kind == N_FUNC && ctx->n < 128) {
      ctx->sigs[ctx->n].s = s->u.func.s;
      ctx->sigs[ctx->n].len = s->u.func.len;
      ctx->sigs[ctx->n].ret = s->u.func.ret;
      ctx->n++;
    }
  }
}

Proto *spt_codegen(spt_State *L, Node *chunk, const char *chunkname,
                   char *err, size_t errsz) {
  (void)chunkname;
  jmp_buf jb;
  FuncState fs;
  CompileCtx ctx;
  if (setjmp(jb) != 0) {                /* codegen error: free temp buffers */
    free(fs.code); free(fs.k); free(fs.protos);
    return NULL;
  }
  prescan(&ctx, chunk);
  fs_init(&fs, L, 0, &ctx, err, errsz, &jb);
  stmt(&fs, chunk);
  emit(&fs, SPT_MK_ABC(OP_HALT, 0, 0, 0));
  return finalize(&fs);
}

/* ================================================================== */
/* Public: compile source into a pushed closure                        */
/* ================================================================== */
int spt_load(spt_State *L, const char *src, const char *chunkname) {
  static char err[256];
  err[0] = '\0';
  AstArena arena = { NULL };

  Node *chunk = spt_parse(L, &arena, src, err, sizeof err);
  if (!chunk) { arena_free(&arena); L->errmsg = err; return 1; }

  L->G->gc_disabled++;                  /* protos aren't rooted yet */
  Proto *p = spt_codegen(L, chunk, chunkname, err, sizeof err);
  if (p) p->source = spt_str_new(L, chunkname ? chunkname : "=(load)");
  L->G->gc_disabled--;

  arena_free(&arena);
  if (!p) { L->errmsg = err; return 1; }

  Closure *cl = spt_closure_new(L, p);
  spt_checkstack(L, 1);
  setgco(L->top, (GCObject *)cl, SPT_TCLOSURE); L->top++;
  return 0;
}
