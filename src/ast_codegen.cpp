/*
** ast_codegen.cpp
** AST-to-Lua 5.5 bytecode compiler implementation.
**
** This module replaces Lua's recursive-descent parser (lparser.c) with
** an AST walker that emits identical bytecode through the lcode.h API.
** The overall structure mirrors lparser.c closely: we maintain a
** FuncState / BlockCnt / expdesc chain and call luaK_* helpers to
** produce instructions.
**
** Design notes
** ============
** 1.  The AST is a C++ class hierarchy (see ast.h).  This file is
**     compiled as C++ but wraps Lua's C headers with extern "C".
** 2.  Every public symbol uses the prefix "astY_" to parallel "luaY_".
** 3.  Error reporting goes through luaK_semerror / luaX_syntaxerror
**     equivalents that ultimately call luaD_throw.
** 4.  Memory allocation for Proto/TString/etc. uses normal Lua GC
**     allocation (luaM_*, luaS_*, luaF_*, luaH_*).  Nothing is
**     malloc'd outside Lua.
*/

/*=======================================================================
 * Includes
 *=====================================================================*/

#include "ast_codegen.h"
#include <cassert>
#include <cstring>
#include <string>
#include <vector>

/* Lua C headers — wrapped for C++ */
extern "C" {
#include "lcode.h"
#include "ldebug.h"
#include "ldo.h"
#include "lfunc.h"
#include "lgc.h"
#include "llex.h"
#include "lmem.h"
#include "lobject.h"
#include "lopcodes.h"
#include "lparser.h"
#include "lstate.h"
#include "lstring.h"
#include "ltable.h"
#include "lua.h"
}

/*=======================================================================
 * Forward declarations
 *=====================================================================*/

struct CompileCtx; /* per-compilation context (replaces LexState role) */

static void compile_block(CompileCtx *C, BlockNode *block);
static void compile_statement(CompileCtx *C, Statement *stmt);
static void compile_expression(CompileCtx *C, Expression *expr, expdesc *e);
static void compile_exprlist(CompileCtx *C, const std::vector<Expression *> &list, expdesc *last);
static int compile_exprlist_n(CompileCtx *C, const std::vector<Expression *> &list, expdesc *last);

/*=======================================================================
 * Compile Context
 *=====================================================================*/

struct CompileCtx {
  lua_State *L;
  LexState ls;     /* 伪造的 LexState，用于兼容 lcode.c */
  FuncState *fs;   /* current function being compiled          */
  Dyndata *dyd;    /* dynamic data (actvar, gotos, labels)     */
  TString *source; /* source name (for debug info)             */
  TString *envn;   /* environment name — normally "_ENV"       */
  TString *brkn;   /* break label name                         */
  TString *contn;  /* continue label name                      */
  int linenumber;  /* current line (updated from AST locs)    */
};

/*-----------------------------------------------------------------------
 * Helpers — line tracking
 *---------------------------------------------------------------------*/
static void setline(CompileCtx *C, const SourceLocation &loc) {
  if (loc.line > 0)
    C->linenumber = loc.line;
}

static void setline_node(CompileCtx *C, AstNode *n) {
  if (n)
    setline(C, n->location);
}

/*-----------------------------------------------------------------------
 * Helpers — error reporting
 *---------------------------------------------------------------------*/
static l_noret compile_error(CompileCtx *C, const char *msg) {
  luaO_pushfstring(C->L, "%s:%d: %s", getstr(C->source), C->linenumber, msg);
  luaD_throw(C->L, LUA_ERRSYNTAX);
}

static l_noret compile_errorf(CompileCtx *C, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  const char *inner = luaO_pushvfstring(C->L, fmt, ap);
  va_end(ap);
  luaO_pushfstring(C->L, "%s:%d: %s", getstr(C->source), C->linenumber, inner);
  luaD_throw(C->L, LUA_ERRSYNTAX);
}

/*-----------------------------------------------------------------------
 * TString helpers
 *---------------------------------------------------------------------*/
static TString *mkstr(CompileCtx *C, const std::string &s) {
  return luaS_newlstr(C->L, s.c_str(), s.size());
}

static TString *mkstr(CompileCtx *C, const char *s) { return luaS_new(C->L, s); }

static void init_exp(expdesc *e, expkind k, int i) {
  e->f = e->t = NO_JUMP;
  e->k = k;
  e->u.info = i;
}

/*=======================================================================
 * Block management
 *=====================================================================*/

static void ast_enterblock(CompileCtx *C, FuncState *fs, BlockCnt *bl, lu_byte isloop) {
  bl->isloop = isloop;
  bl->nactvar = fs->nactvar;
  bl->firstlabel = C->dyd->label.n;
  bl->firstgoto = C->dyd->gt.n;
  bl->upval = 0;
  bl->insidetbc = (fs->bl != NULL && fs->bl->insidetbc);
  bl->previous = fs->bl;
  fs->bl = bl;
  lua_assert(fs->freereg == luaY_nvarstack(fs));
}

static void ast_leaveblock(CompileCtx *C, FuncState *fs) {
  BlockCnt *bl = fs->bl;
  lu_byte stklevel = luaY_nvarstack(fs);

  {
    int nvar = bl->nactvar;
    int reglev = 0;
    for (int i = nvar - 1; i >= 0; i--) {
      Vardesc *vd = &C->dyd->actvar.arr[fs->firstlocal + i];
      if (varinreg(vd)) {
        reglev = vd->vd.ridx + 1;
        break;
      }
    }
    stklevel = cast_byte(reglev);
  }

  if (bl->previous && bl->upval)
    luaK_codeABC(fs, OP_CLOSE, stklevel, 0, 0);

  fs->freereg = stklevel;

  /* remove vars from scope */
  C->dyd->actvar.n -= (fs->nactvar - bl->nactvar);
  while (fs->nactvar > bl->nactvar) {
    fs->nactvar--;
    Vardesc *vd = &C->dyd->actvar.arr[fs->firstlocal + fs->nactvar];
    if (varinreg(vd)) {
      int idx = vd->vd.pidx;
      if (idx >= 0 && idx < fs->ndebugvars)
        fs->f->locvars[idx].endpc = fs->pc;
    }
  }
  lua_assert(bl->nactvar == fs->nactvar);

  /* pending breaks → label "break" */
  if (bl->isloop == 2) {
    Labellist *ll = &C->dyd->label;
    int n = ll->n;
    luaM_growvector(C->L, ll->arr, n, ll->size, Labeldesc, SHRT_MAX, "labels/gotos");
    ll->arr[n].name = C->brkn;
    ll->arr[n].line = C->linenumber;
    ll->arr[n].nactvar = fs->nactvar;
    ll->arr[n].close = 0;
    ll->arr[n].pc = luaK_getlabel(fs);
    ll->n = n + 1;
  }

  /* solve gotos against labels in this block */
  {
    Labellist *gl = &C->dyd->gt;
    int igt = bl->firstgoto;
    while (igt < gl->n) {
      Labeldesc *gt = &gl->arr[igt];
      Labeldesc *found = NULL;
      for (int ilb = bl->firstlabel; ilb < C->dyd->label.n; ilb++) {
        if (gt->name == C->dyd->label.arr[ilb].name) {
          found = &C->dyd->label.arr[ilb];
          break;
        }
      }
      if (found) {
        luaK_patchlist(fs, gt->pc, found->pc);
        for (int j = igt; j < gl->n - 1; j++)
          gl->arr[j] = gl->arr[j + 1];
        gl->n--;
      } else {
        if (bl->upval)
          gt->close = 1;
        gt->nactvar = bl->nactvar;
        igt++;
      }
    }
    C->dyd->label.n = bl->firstlabel;
  }

  if (bl->previous == NULL) {
    if (bl->firstgoto < C->dyd->gt.n)
      compile_error(C, "undefined label");
  }
  fs->bl = bl->previous;
}

/*=======================================================================
 * Variable management
 *=====================================================================*/

static Vardesc *ast_getvar(CompileCtx *C, FuncState *fs, int vidx) {
  return &C->dyd->actvar.arr[fs->firstlocal + vidx];
}

static short ast_registerlocalvar(CompileCtx *C, FuncState *fs, TString *varname) {
  Proto *f = fs->f;
  int oldsize = f->sizelocvars;
  luaM_growvector(C->L, f->locvars, fs->ndebugvars, f->sizelocvars, LocVar, SHRT_MAX,
                  "local variables");
  while (oldsize < f->sizelocvars)
    f->locvars[oldsize++].varname = NULL;
  f->locvars[fs->ndebugvars].varname = varname;
  f->locvars[fs->ndebugvars].startpc = fs->pc;
  luaC_objbarrier(C->L, f, varname);
  return fs->ndebugvars++;
}

static int ast_new_var(CompileCtx *C, TString *name, lu_byte kind) {
  lua_State *L = C->L;
  FuncState *fs = C->fs;
  Dyndata *dyd = C->dyd;
  luaM_growvector(L, dyd->actvar.arr, dyd->actvar.n + 1, dyd->actvar.size, Vardesc, SHRT_MAX,
                  "variable declarations");
  Vardesc *var = &dyd->actvar.arr[dyd->actvar.n++];
  var->vd.kind = kind;
  var->vd.name = name;
  return dyd->actvar.n - 1 - fs->firstlocal;
}

static int ast_new_localvar(CompileCtx *C, TString *name) { return ast_new_var(C, name, VDKREG); }

static void ast_adjustlocalvars(CompileCtx *C, int nvars) {
  FuncState *fs = C->fs;
  int reglev = (int)luaY_nvarstack(fs);
  for (int i = 0; i < nvars; i++) {
    int vidx = fs->nactvar++;
    Vardesc *var = ast_getvar(C, fs, vidx);
    var->vd.ridx = cast_byte(reglev++);
    var->vd.pidx = ast_registerlocalvar(C, fs, var->vd.name);
    luaY_checklimit(fs, reglev, 200, "local variables");
  }
}

/*-----------------------------------------------------------------------
 * Variable lookup
 *---------------------------------------------------------------------*/

static int ast_searchvar(CompileCtx *C, FuncState *fs, TString *n, expdesc *var) {
  for (int i = (int)fs->nactvar - 1; i >= 0; i--) {
    Vardesc *vd = ast_getvar(C, fs, i);
    if (varglobal(vd)) {
      if (vd->vd.name == NULL) {
        if (var->u.info < 0)
          var->u.info = fs->firstlocal + i;
      } else {
        if (n == vd->vd.name) {
          init_exp(var, VGLOBAL, fs->firstlocal + i);
          return VGLOBAL;
        } else if (var->u.info == -1)
          var->u.info = -2;
      }
    } else if (n == vd->vd.name) {
      if (vd->vd.kind == RDKCTC) {
        init_exp(var, VCONST, fs->firstlocal + i);
      } else {
        var->f = var->t = NO_JUMP;
        var->k = VLOCAL;
        var->u.var.vidx = cast_short(i);
        var->u.var.ridx = vd->vd.ridx;
        if (vd->vd.kind == RDKVAVAR)
          var->k = VVARGVAR;
      }
      return (int)var->k;
    }
  }
  return -1;
}

static int ast_searchupvalue(FuncState *fs, TString *name) {
  Upvaldesc *up = fs->f->upvalues;
  for (int i = 0; i < fs->nups; i++) {
    if (up[i].name == name)
      return i;
  }
  return -1;
}

static void ast_markupval(FuncState *fs, int level) {
  BlockCnt *bl = fs->bl;
  while (bl->nactvar > level)
    bl = bl->previous;
  bl->upval = 1;
  fs->needclose = 1;
}

static int ast_newupvalue(CompileCtx *C, FuncState *fs, TString *name, expdesc *v) {
  Proto *f = fs->f;
  int oldsize = f->sizeupvalues;
  luaY_checklimit(fs, fs->nups + 1, 255, "upvalues");
  luaM_growvector(C->L, f->upvalues, fs->nups, f->sizeupvalues, Upvaldesc, 255, "upvalues");
  while (oldsize < f->sizeupvalues)
    f->upvalues[oldsize++].name = NULL;
  Upvaldesc *up = &f->upvalues[fs->nups];
  FuncState *prev = fs->prev;
  if (v->k == VLOCAL) {
    up->instack = 1;
    up->idx = v->u.var.ridx;
    up->kind = ast_getvar(C, prev, v->u.var.vidx)->vd.kind;
  } else {
    up->instack = 0;
    up->idx = cast_byte(v->u.info);
    up->kind = prev->f->upvalues[v->u.info].kind;
  }
  up->name = name;
  luaC_objbarrier(C->L, f, name);
  return fs->nups++;
}

static void ast_singlevaraux(CompileCtx *C, FuncState *fs, TString *n, expdesc *var, int base) {
  int v = ast_searchvar(C, fs, n, var);
  if (v >= 0) {
    if (!base) {
      if (var->k == VVARGVAR)
        luaK_vapar2local(fs, var);
      if (var->k == VLOCAL)
        ast_markupval(fs, var->u.var.vidx);
    }
  } else {
    int idx = ast_searchupvalue(fs, n);
    if (idx < 0) {
      if (fs->prev != NULL)
        ast_singlevaraux(C, fs->prev, n, var, 0);
      if (var->k == VLOCAL || var->k == VUPVAL)
        idx = ast_newupvalue(C, fs, n, var);
      else
        return;
    }
    init_exp(var, VUPVAL, idx);
  }
}

static void ast_buildglobal(CompileCtx *C, TString *varname, expdesc *var) {
  FuncState *fs = C->fs;
  expdesc key;
  init_exp(var, VGLOBAL, -1);
  ast_singlevaraux(C, fs, C->envn, var, 1);
  if (var->k == VGLOBAL)
    compile_errorf(C, "%s is global when accessing variable '%s'", LUA_ENV, getstr(varname));
  luaK_exp2anyregup(fs, var);
  key.f = key.t = NO_JUMP;
  key.k = VKSTR;
  key.u.strval = varname;
  luaK_indexed(fs, var, &key);
}

static void ast_buildvar(CompileCtx *C, TString *varname, expdesc *var) {
  FuncState *fs = C->fs;
  init_exp(var, VGLOBAL, -1);
  ast_singlevaraux(C, fs, varname, var, 1);
  if (var->k == VGLOBAL) {
    int info = var->u.info;
    if (info == -2)
      compile_errorf(C, "variable '%s' not declared", getstr(varname));
    ast_buildglobal(C, varname, var);
    if (info != -1 && C->dyd->actvar.arr[info].vd.kind == GDKCONST)
      var->u.ind.ro = 1;
  }
}

static void ast_singlevar(CompileCtx *C, const std::string &name, expdesc *var) {
  TString *ts = mkstr(C, name);
  ast_buildvar(C, ts, var);
}

/*-----------------------------------------------------------------------
 * Read-only check
 *---------------------------------------------------------------------*/
static void ast_check_readonly(CompileCtx *C, expdesc *e) {
  FuncState *fs = C->fs;
  TString *varname = NULL;
  switch (e->k) {
  case VCONST:
    varname = C->dyd->actvar.arr[e->u.info].vd.name;
    break;
  case VLOCAL:
  case VVARGVAR: {
    Vardesc *vd = ast_getvar(C, fs, e->u.var.vidx);
    if (vd->vd.kind != VDKREG)
      varname = vd->vd.name;
    break;
  }
  case VUPVAL: {
    Upvaldesc *up = &fs->f->upvalues[e->u.info];
    if (up->kind != VDKREG)
      varname = up->name;
    break;
  }
  case VINDEXUP:
  case VINDEXSTR:
  case VINDEXED:
    if (e->u.ind.ro)
      varname = tsvalue(&fs->f->k[e->u.ind.keystr]);
    break;
  default:
    break;
  }
  if (varname)
    compile_errorf(C, "attempt to assign to const variable '%s'", getstr(varname));
}

/*=======================================================================
 * Function state management
 *=====================================================================*/

static Proto *ast_addprototype(CompileCtx *C) {
  Proto *clp;
  lua_State *L = C->L;
  FuncState *fs = C->fs;
  Proto *f = fs->f;
  if (fs->np >= f->sizep) {
    int oldsize = f->sizep;
    luaM_growvector(L, f->p, fs->np, f->sizep, Proto *, MAXARG_Bx, "functions");
    while (oldsize < f->sizep)
      f->p[oldsize++] = NULL;
  }
  f->p[fs->np++] = clp = luaF_newproto(L);
  luaC_objbarrier(L, f, clp);
  return clp;
}

static void ast_open_func(CompileCtx *C, FuncState *fs, BlockCnt *bl) {
  lua_State *L = C->L;
  Proto *f = fs->f;
  fs->prev = C->fs;
  fs->ls = &C->ls;
  C->fs = fs;
  fs->pc = 0;
  fs->previousline = f->linedefined;
  fs->iwthabs = 0;
  fs->lasttarget = 0;
  fs->freereg = 0;
  fs->nk = 0;
  fs->nabslineinfo = 0;
  fs->np = 0;
  fs->nups = 0;
  fs->ndebugvars = 0;
  fs->nactvar = 0;
  fs->needclose = 0;
  fs->firstlocal = C->dyd->actvar.n;
  fs->firstlabel = C->dyd->label.n;
  fs->bl = NULL;
  f->source = C->source;
  luaC_objbarrier(L, f, f->source);
  f->maxstacksize = 2;
  fs->kcache = luaH_new(L);
  sethvalue2s(L, L->top.p, fs->kcache);
  luaD_inctop(L);
  ast_enterblock(C, fs, bl, 0);
}

static void ast_close_func(CompileCtx *C) {
  lua_State *L = C->L;
  FuncState *fs = C->fs;
  Proto *f = fs->f;
  luaK_ret(fs, luaY_nvarstack(fs), 0);
  ast_leaveblock(C, fs);
  lua_assert(fs->bl == NULL);
  luaK_finish(fs);
  luaM_shrinkvector(L, f->code, f->sizecode, fs->pc, Instruction);
  luaM_shrinkvector(L, f->lineinfo, f->sizelineinfo, fs->pc, ls_byte);
  luaM_shrinkvector(L, f->abslineinfo, f->sizeabslineinfo, fs->nabslineinfo, AbsLineInfo);
  luaM_shrinkvector(L, f->k, f->sizek, fs->nk, TValue);
  luaM_shrinkvector(L, f->p, f->sizep, fs->np, Proto *);
  luaM_shrinkvector(L, f->locvars, f->sizelocvars, fs->ndebugvars, LocVar);
  luaM_shrinkvector(L, f->upvalues, f->sizeupvalues, fs->nups, Upvaldesc);
  C->fs = fs->prev;
  L->top.p--;
  luaC_checkGC(L);
}

static void ast_codeclosure(CompileCtx *C, expdesc *v) {
  FuncState *fs = C->fs; /* ast_close_func already restored C->fs to the enclosing function */
  init_exp(v, VRELOC, luaK_codeABx(fs, OP_CLOSURE, 0, fs->np - 1));
  luaK_exp2nextreg(fs, v);
}

/*=======================================================================
 * Adjust assign
 *=====================================================================*/

#define hasmultret(k) ((k) == VCALL || (k) == VVARARG)

static void ast_adjust_assign(CompileCtx *C, int nvars, int nexps, expdesc *e) {
  FuncState *fs = C->fs;
  int needed = nvars - nexps;
  luaK_checkstack(fs, needed);
  if (hasmultret(e->k)) {
    int extra = needed + 1;
    if (extra < 0)
      extra = 0;
    luaK_setreturns(fs, e, extra);
  } else {
    if (e->k != VVOID)
      luaK_exp2nextreg(fs, e);
    if (needed > 0)
      luaK_nil(fs, fs->freereg, needed);
  }
  if (needed > 0)
    luaK_reserveregs(fs, needed);
  else
    fs->freereg = cast_byte(fs->freereg + needed);
}

/*=======================================================================
 * Helper: compile function parameters (shared by lambda, func decl, class methods)
 *
 * Reads ParameterDeclNode list and the isVariadic flag from the
 * enclosing LambdaNode / FunctionDeclNode.
 *=====================================================================*/
static void compile_params(CompileCtx *C, FuncState *new_fs,
                           const std::vector<ParameterDeclNode *> &params, bool isVariadic,
                           bool isMethod) {
  /*------------------------------------------------------------
   * Implicit 'self' parameter — ALWAYS occupies Slot 0.
   *
   * Every function receives its Receiver in the first stack
   * slot.  The caller is responsible for pushing it (see
   * compile_funcall / compile_new_expr / etc.).
   *
   * For class instance methods  →  self = the object instance
   * For plain global calls      →  self = _ENV / Module
   * For closure calls           →  self = the closure itself
   * For obj.method() / obj:m()  →  self = obj
   *-----------------------------------------------------------*/
  const char *recName = isMethod ? "self" : "(receiver)";
  TString *selfname = mkstr(C, recName);
  ast_new_localvar(C, selfname);
  ast_adjustlocalvars(C, 1); /* Slot 0 is now occupied */

  /* User-declared parameters — start from Slot 1 */
  int nparams = 0;
  for (auto *p : params) {
    TString *pname = mkstr(C, p->name);
    ast_new_localvar(C, pname);
    nparams++;
  }
  ast_adjustlocalvars(C, nparams);

  /* numparams includes self */
  new_fs->f->numparams = cast_byte(C->fs->nactvar);

  if (isVariadic) {
    TString *vaname = mkstr(C, "(vararg table)");
    ast_new_var(C, vaname, RDKVAVAR);
    new_fs->f->flag |= PF_VAHID;
    luaK_codeABC(C->fs, OP_VARARGPREP, new_fs->f->numparams, 0, 0);
    ast_adjustlocalvars(C, 1);
  }
  luaK_reserveregs(C->fs, C->fs->nactvar);
}

/*=======================================================================
 * Continue resolution helper
 *
 * Patches all pending "(continue)" gotos that belong to the current
 * loop block so they jump to 'target'.  Must be called while the loop
 * block is still the active block (fs->bl).
 *=====================================================================*/
static void resolve_continues(CompileCtx *C, FuncState *fs, int target) {
  Labellist *gl = &C->dyd->gt;
  int igt = fs->bl->firstgoto;
  while (igt < gl->n) {
    if (gl->arr[igt].name == C->contn) {
      luaK_patchlist(fs, gl->arr[igt].pc, target);
      /* remove resolved goto by shifting the rest down */
      for (int j = igt; j < gl->n - 1; j++)
        gl->arr[j] = gl->arr[j + 1];
      gl->n--;
    } else {
      igt++;
    }
  }
}

/*=======================================================================
 * Expression compilation
 *=====================================================================*/

/*-----------------------------------------------------------------------
 * Literals
 *---------------------------------------------------------------------*/
static void compile_literal_int(CompileCtx *C, LiteralIntNode *n, expdesc *e) {
  init_exp(e, VKINT, 0);
  e->u.ival = (lua_Integer)n->value;
}

static void compile_literal_float(CompileCtx *C, LiteralFloatNode *n, expdesc *e) {
  init_exp(e, VKFLT, 0);
  e->u.nval = (lua_Number)n->value;
}

static void compile_literal_string(CompileCtx *C, LiteralStringNode *n, expdesc *e) {
  e->f = e->t = NO_JUMP;
  e->k = VKSTR;
  e->u.strval = mkstr(C, n->value);
}

static void compile_literal_bool(CompileCtx *C, LiteralBoolNode *n, expdesc *e) {
  init_exp(e, n->value ? VTRUE : VFALSE, 0);
}

static void compile_literal_null(CompileCtx *C, LiteralNullNode *n, expdesc *e) {
  (void)n;
  init_exp(e, VNIL, 0);
}

/*-----------------------------------------------------------------------
 * Identifier
 *---------------------------------------------------------------------*/
static void compile_identifier(CompileCtx *C, IdentifierNode *n, expdesc *e) {
  ast_singlevar(C, n->name, e);
}

/*-----------------------------------------------------------------------
 * Unary operations
 *---------------------------------------------------------------------*/
static void compile_unary(CompileCtx *C, UnaryOpNode *n, expdesc *e) {
  setline(C, n->location);
  compile_expression(C, n->operand, e);

  UnOpr uop;
  switch (n->op) {
  case OperatorKind::NEGATE:
    uop = OPR_MINUS;
    break;
  case OperatorKind::NOT:
    uop = OPR_NOT;
    break;
  case OperatorKind::BW_NOT:
    uop = OPR_BNOT;
    break;
  case OperatorKind::LENGTH:
    uop = OPR_LEN;
    break;
  default:
    compile_error(C, "unknown unary operator");
    return;
  }
  luaK_prefix(C->fs, uop, e, C->linenumber);
}

/*-----------------------------------------------------------------------
 * Binary operations
 *---------------------------------------------------------------------*/
static BinOpr ast_binopr(OperatorKind op) {
  switch (op) {
  case OperatorKind::ADD:
    return OPR_ADD;
  case OperatorKind::SUB:
    return OPR_SUB;
  case OperatorKind::MUL:
    return OPR_MUL;
  case OperatorKind::MOD:
    return OPR_MOD;
  case OperatorKind::DIV:
    return OPR_DIV;
  case OperatorKind::IDIV:
    return OPR_IDIV;
  case OperatorKind::BW_AND:
    return OPR_BAND;
  case OperatorKind::BW_OR:
    return OPR_BOR;
  case OperatorKind::BW_XOR:
    return OPR_BXOR;
  case OperatorKind::BW_LSHIFT:
    return OPR_SHL;
  case OperatorKind::BW_RSHIFT:
    return OPR_SHR;
  case OperatorKind::CONCAT:
    return OPR_CONCAT;
  case OperatorKind::EQ:
    return OPR_EQ;
  case OperatorKind::NE:
    return OPR_NE;
  case OperatorKind::LT:
    return OPR_LT;
  case OperatorKind::LE:
    return OPR_LE;
  case OperatorKind::GT:
    return OPR_GT;
  case OperatorKind::GE:
    return OPR_GE;
  case OperatorKind::AND:
    return OPR_AND;
  case OperatorKind::OR:
    return OPR_OR;
  default:
    return OPR_NOBINOPR;
  }
}

static void compile_binary(CompileCtx *C, BinaryOpNode *n, expdesc *e) {
  BinOpr opr = ast_binopr(n->op);
  if (opr == OPR_NOBINOPR)
    compile_error(C, "unknown binary operator");

  setline(C, n->location);
  compile_expression(C, n->left, e);
  luaK_infix(C->fs, opr, e);

  expdesc e2;
  compile_expression(C, n->right, &e2);
  luaK_posfix(C->fs, opr, e, &e2, C->linenumber);
}

/*-----------------------------------------------------------------------
 * Function call — Unified Receiver Convention
 *
 * Stack layout at OP_CALL:
 *   R(base)   = function
 *   R(base+1) = Receiver  ← implicit first argument (always present)
 *   R(base+2) = arg1
 *   R(base+3) = arg2 ...
 *
 * Receiver selection by call pattern:
 *   obj:method(args)   →  Receiver = obj            (OP_SELF)
 *   obj.method(args)   →  Receiver = obj            (OP_SELF)
 *   arr[i](args)       →  Receiver = nil            (push nil)
 *   name(args)         →  Receiver = nil            (push nil)
 *   expr(args)         →  Receiver = nil            (push nil)
 *
 * ALL functions receive 'self' as the first parameter.
 * For non-method calls, self = nil.
 *---------------------------------------------------------------------*/
static void compile_funcall(CompileCtx *C, FunctionCallNode *n, expdesc *e, int nresults) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  Expression *funcExpr = n->functionExpr;

  /*---------- Scenario: obj:method(args) — MemberLookupNode ----------*/
  // 1. obj:method() 形式 (保持不变)
  if (funcExpr->nodeType == NodeType::MEMBER_LOOKUP) {
    MemberLookupNode *ml = static_cast<MemberLookupNode *>(funcExpr);
    compile_expression(C, ml->objectExpr, e);
    luaK_exp2anyregup(fs, e);
    expdesc key;
    key.f = key.t = NO_JUMP;
    key.k = VKSTR;
    key.u.strval = mkstr(C, ml->memberName);
    luaK_self(fs, e, &key);

    // 2. obj.method() 形式 (保持不变，已支持传递 obj)
  } else if (funcExpr->nodeType == NodeType::MEMBER_ACCESS) {
    MemberAccessNode *ma = static_cast<MemberAccessNode *>(funcExpr);
    compile_expression(C, ma->objectExpr, e);
    luaK_exp2anyregup(fs, e);
    expdesc key;
    key.f = key.t = NO_JUMP;
    key.k = VKSTR;
    key.u.strval = mkstr(C, ma->memberName);
    luaK_self(fs, e, &key);

    // 3. [新增] obj[key]() 形式 (这里是这次的核心修改)
    // 这会让 a[3]() 调用时，将 a 传给 self
  } else if (funcExpr->nodeType == NodeType::INDEX_ACCESS) {
    IndexAccessNode *ia = static_cast<IndexAccessNode *>(funcExpr);

    // (A) 编译对象 a
    compile_expression(C, ia->arrayExpr, e);
    luaK_exp2anyregup(fs, e);

    // (B) 编译索引 key
    expdesc key;
    compile_expression(C, ia->indexExpr, &key);
    luaK_exp2val(fs, &key); // 确保 key 是数值/常量/寄存器

    // (C) 生成 OP_SELF 指令
    // 效果: R(func) = a[key];  R(self) = a;
    luaK_self(fs, e, &key);

    // 4. 普通函数调用 a() 或 (expr)()
    // 修改为直接 push nil，不再 push _ENV 或 duplicate closure
  } else {
    compile_expression(C, funcExpr, e);
    luaK_exp2nextreg(fs, e);

    // 显式加载 nil 到下一个寄存器作为 receiver
    luaK_nil(fs, fs->freereg, 1);
    luaK_reserveregs(fs, 1);
  }

  /*---------- Compile user arguments (R(base+2) onward) --------------*/
  expdesc args;
  int nparams;
  if (n->arguments.empty()) {
    /* No user arguments, but Receiver is already at R(base+1).
       nparams = freereg - (base+1) = 1 (the Receiver). */
    nparams = fs->freereg - (e->u.info + 1);
  } else {
    int nargs = compile_exprlist_n(C, n->arguments, &args);
    if (hasmultret(args.k)) {
      luaK_setmultret(fs, &args);
      nparams = LUA_MULTRET;
    } else {
      if (args.k != VVOID)
        luaK_exp2nextreg(fs, &args);
      /* nparams counts Receiver + user args */
      nparams = fs->freereg - (e->u.info + 1);
    }
  }

  /*---------- Emit OP_CALL -------------------------------------------*/
  lua_assert(e->k == VNONRELOC);
  int base = e->u.info;
  /* nparams already includes the Receiver (counted as an argument) */
  init_exp(e, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams + 1, nresults + 1));
  luaK_fixline(fs, C->linenumber);
  fs->freereg = cast_byte(base + 1);
}

/*-----------------------------------------------------------------------
 * Member access:  obj.field
 *---------------------------------------------------------------------*/
static void compile_member_access(CompileCtx *C, MemberAccessNode *n, expdesc *e) {
  setline(C, n->location);
  compile_expression(C, n->objectExpr, e);
  luaK_exp2anyregup(C->fs, e);
  expdesc key;
  key.f = key.t = NO_JUMP;
  key.k = VKSTR;
  key.u.strval = mkstr(C, n->memberName);
  luaK_indexed(C->fs, e, &key);
}

/*-----------------------------------------------------------------------
 * Member lookup:  obj:method  → OP_SELF
 *---------------------------------------------------------------------*/
static void compile_member_lookup(CompileCtx *C, MemberLookupNode *n, expdesc *e) {
  setline(C, n->location);
  compile_expression(C, n->objectExpr, e);
  luaK_exp2anyregup(C->fs, e);
  expdesc key;
  key.f = key.t = NO_JUMP;
  key.k = VKSTR;
  key.u.strval = mkstr(C, n->memberName);
  luaK_self(C->fs, e, &key);
}

/*-----------------------------------------------------------------------
 * Index access:  arr[idx]
 *---------------------------------------------------------------------*/
static void compile_index_access(CompileCtx *C, IndexAccessNode *n, expdesc *e) {
  setline(C, n->location);
  compile_expression(C, n->arrayExpr, e);
  luaK_exp2anyregup(C->fs, e);
  expdesc key;
  compile_expression(C, n->indexExpr, &key);
  luaK_exp2val(C->fs, &key);
  luaK_indexed(C->fs, e, &key);
}

/*-----------------------------------------------------------------------
 * Lambda / anonymous function
 *---------------------------------------------------------------------*/
static void compile_lambda(CompileCtx *C, LambdaNode *n, expdesc *e) {
  FuncState new_fs;
  BlockCnt bl;
  setline(C, n->location);

  new_fs.f = ast_addprototype(C);
  new_fs.f->linedefined = C->linenumber;
  ast_open_func(C, &new_fs, &bl);

  /* Parameters — vararg is signaled by n->isVariadic, not per-param */
  compile_params(C, &new_fs, n->params, n->isVariadic, false);

  /* Body */
  if (n->body) {
    if (n->body->nodeType == NodeType::BLOCK) {
      compile_block(C, static_cast<BlockNode *>(n->body));
    } else {
      compile_statement(C, static_cast<Statement *>(n->body));
    }
  }

  new_fs.f->lastlinedefined = C->linenumber;
  ast_close_func(C);
  ast_codeclosure(C, e);
}

/*-----------------------------------------------------------------------
 * List literal  → array constructor (using OP_NEWLIST)
 * NOTE: VM uses 0-based table indexing — handled in VM/luaK_setlist
 *---------------------------------------------------------------------*/
static void compile_list_literal(CompileCtx *C, LiteralListNode *n, expdesc *e) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  int pc = luaK_codevABCk(fs, OP_NEWLIST, 0, 0, 0, 0);
  luaK_code(fs, 0); /* extra arg */

  init_exp(e, VNONRELOC, fs->freereg);
  luaK_reserveregs(fs, 1);

  int na = 0;
  for (size_t i = 0; i < n->elements.size(); i++) {
    expdesc val;
    compile_expression(C, n->elements[i], &val);
    luaK_exp2nextreg(fs, &val);
    na++;
    if (na >= MAXARG_vC) {
      luaK_setlist(fs, e->u.info, (int)i + 1 - na, na);
      na = 0;
    }
  }
  if (na > 0) {
    luaK_setlist(fs, e->u.info, (int)n->elements.size() - na, na);
  }
  luaK_setlistsize(fs, pc, e->u.info, (int)n->elements.size());
}

/*-----------------------------------------------------------------------
 * Map literal  → table constructor (hash part)
 *---------------------------------------------------------------------*/
static void compile_map_literal(CompileCtx *C, LiteralMapNode *n, expdesc *e) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  int pc = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
  luaK_code(fs, 0);

  init_exp(e, VNONRELOC, fs->freereg);
  luaK_reserveregs(fs, 1);

  for (auto *entry : n->entries) {
    lu_byte reg = fs->freereg;
    expdesc tab = *e;
    expdesc key;

    // Compile the key expression directly - visitor handles string conversion for shorthand syntax
    compile_expression(C, entry->key, &key);
    luaK_exp2val(fs, &key);

    luaK_indexed(fs, &tab, &key);
    expdesc val;
    compile_expression(C, entry->value, &val);
    luaK_storevar(fs, &tab, &val);
    fs->freereg = reg;
  }

  luaK_settablesize(fs, pc, e->u.info, 0, (int)n->entries.size());
}

/*-----------------------------------------------------------------------
 * New expression:  new ClassName(args)
 *
 * The class table itself acts as the constructor (via __call or
 * direct protocol).
 *
 * Stack layout:
 *   R(base)   = ClassName (function / callable)
 *   R(base+1) = nil (Receiver — consistent with non-method calls)
 *   R(base+2) = arg1 ...
 *---------------------------------------------------------------------*/
static void compile_new_expr(CompileCtx *C, NewExpressionNode *n, expdesc *e) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  if (n->classType) {
    UserType *ut = dynamic_cast<UserType *>(n->classType);
    if (ut && !ut->qualifiedNameParts.empty()) {
      ast_singlevar(C, ut->qualifiedNameParts[0], e);
      luaK_exp2nextreg(fs, e);
      for (size_t i = 1; i < ut->qualifiedNameParts.size(); i++) {
        luaK_exp2anyregup(fs, e);
        expdesc key;
        key.f = key.t = NO_JUMP;
        key.k = VKSTR;
        key.u.strval = mkstr(C, ut->qualifiedNameParts[i]);
        luaK_indexed(fs, e, &key);
        luaK_exp2nextreg(fs, e);
      }
    } else {
      compile_error(C, "invalid type in new expression");
    }
  }

  /* Push nil as Receiver */
  {
    expdesc nil_exp;
    init_exp(&nil_exp, VNIL, 0);
    luaK_exp2nextreg(fs, &nil_exp);
  }

  /* Compile arguments */
  expdesc args;
  int nparams;
  if (n->arguments.empty()) {
    /* No user args, but nil Receiver at R(base+1). */
    nparams = fs->freereg - (e->u.info + 1);
  } else {
    nparams = compile_exprlist_n(C, n->arguments, &args);
    if (hasmultret(args.k)) {
      luaK_setmultret(fs, &args);
      nparams = LUA_MULTRET;
    } else {
      if (args.k != VVOID)
        luaK_exp2nextreg(fs, &args);
      nparams = fs->freereg - (e->u.info + 1);
    }
  }

  /* nparams includes the nil Receiver */
  int base = e->u.info;
  init_exp(e, VCALL, luaK_codeABC(fs, OP_CALL, base, nparams + 1, 2));
  luaK_fixline(fs, C->linenumber);
  fs->freereg = cast_byte(base + 1);
}

/*-----------------------------------------------------------------------
 * This expression  → identifier "self"
 *---------------------------------------------------------------------*/
static void compile_this(CompileCtx *C, ThisExpressionNode *n, expdesc *e) {
  setline(C, n->location);
  ast_singlevar(C, "self", e);
}

/*-----------------------------------------------------------------------
 * Varargs:  ...
 *---------------------------------------------------------------------*/
static void compile_varargs(CompileCtx *C, VarArgsNode *n, expdesc *e) {
  FuncState *fs = C->fs;
  setline(C, n->location);
  if (!isvararg(fs->f))
    compile_error(C, "cannot use '...' outside a vararg function");
  init_exp(e, VVARARG, luaK_codeABC(fs, OP_VARARG, 0, fs->f->numparams, 1));
}

/*-----------------------------------------------------------------------
 * Main expression dispatch
 *---------------------------------------------------------------------*/
static void compile_expression(CompileCtx *C, Expression *expr, expdesc *e) {
  if (!expr) {
    init_exp(e, VVOID, 0);
    return;
  }
  setline_node(C, expr);

  switch (expr->nodeType) {
  case NodeType::LITERAL_INT:
    compile_literal_int(C, static_cast<LiteralIntNode *>(expr), e);
    break;
  case NodeType::LITERAL_FLOAT:
    compile_literal_float(C, static_cast<LiteralFloatNode *>(expr), e);
    break;
  case NodeType::LITERAL_STRING:
    compile_literal_string(C, static_cast<LiteralStringNode *>(expr), e);
    break;
  case NodeType::LITERAL_BOOL:
    compile_literal_bool(C, static_cast<LiteralBoolNode *>(expr), e);
    break;
  case NodeType::LITERAL_NULL:
    compile_literal_null(C, static_cast<LiteralNullNode *>(expr), e);
    break;
  case NodeType::IDENTIFIER:
    compile_identifier(C, static_cast<IdentifierNode *>(expr), e);
    break;
  case NodeType::UNARY_OP:
    compile_unary(C, static_cast<UnaryOpNode *>(expr), e);
    break;
  case NodeType::BINARY_OP:
    compile_binary(C, static_cast<BinaryOpNode *>(expr), e);
    break;
  case NodeType::FUNCTION_CALL:
    compile_funcall(C, static_cast<FunctionCallNode *>(expr), e, 1);
    break;
  case NodeType::MEMBER_ACCESS:
    compile_member_access(C, static_cast<MemberAccessNode *>(expr), e);
    break;
  case NodeType::MEMBER_LOOKUP:
    compile_member_lookup(C, static_cast<MemberLookupNode *>(expr), e);
    break;
  case NodeType::INDEX_ACCESS:
    compile_index_access(C, static_cast<IndexAccessNode *>(expr), e);
    break;
  case NodeType::LAMBDA:
    compile_lambda(C, static_cast<LambdaNode *>(expr), e);
    break;
  case NodeType::LITERAL_LIST:
    compile_list_literal(C, static_cast<LiteralListNode *>(expr), e);
    break;
  case NodeType::LITERAL_MAP:
    compile_map_literal(C, static_cast<LiteralMapNode *>(expr), e);
    break;
  case NodeType::NEW_EXPRESSION:
    compile_new_expr(C, static_cast<NewExpressionNode *>(expr), e);
    break;
  case NodeType::THIS_EXPRESSION:
    compile_this(C, static_cast<ThisExpressionNode *>(expr), e);
    break;
  case NodeType::VAR_ARGS:
    compile_varargs(C, static_cast<VarArgsNode *>(expr), e);
    break;
  default:
    compile_errorf(C, "unsupported expression node type %d", (int)expr->nodeType);
    break;
  }
}

/*-----------------------------------------------------------------------
 * Expression list
 *---------------------------------------------------------------------*/
static int compile_exprlist_n(CompileCtx *C, const std::vector<Expression *> &list, expdesc *last) {
  int n = (int)list.size();
  if (n == 0) {
    init_exp(last, VVOID, 0);
    return 0;
  }
  for (int i = 0; i < n - 1; i++) {
    expdesc tmp;
    compile_expression(C, list[i], &tmp);
    luaK_exp2nextreg(C->fs, &tmp);
  }
  compile_expression(C, list[n - 1], last);
  return n;
}

static void compile_exprlist(CompileCtx *C, const std::vector<Expression *> &list, expdesc *last) {
  compile_exprlist_n(C, list, last);
}

/*=======================================================================
 * Statement compilation
 *=====================================================================*/

/*-----------------------------------------------------------------------
 * Block
 *---------------------------------------------------------------------*/
static void compile_block(CompileCtx *C, BlockNode *block) {
  if (!block)
    return;
  FuncState *fs = C->fs;
  BlockCnt bl;
  ast_enterblock(C, fs, &bl, 0);
  for (auto *stmt : block->statements) {
    compile_statement(C, stmt);
    lua_assert(fs->f->maxstacksize >= fs->freereg && fs->freereg >= luaY_nvarstack(fs));
    fs->freereg = luaY_nvarstack(fs);
  }
  ast_leaveblock(C, fs);
}

/*-----------------------------------------------------------------------
 * Expression statement
 *---------------------------------------------------------------------*/
static void compile_expr_stmt(CompileCtx *C, ExpressionStatementNode *n) {
  setline(C, n->location);
  FuncState *fs = C->fs;

  if (n->expression->nodeType == NodeType::FUNCTION_CALL) {
    expdesc e;
    compile_funcall(C, static_cast<FunctionCallNode *>(n->expression), &e, 0);
    Instruction *inst = &fs->f->code[e.u.info];
    SETARG_C(*inst, 1);
  } else {
    expdesc e;
    compile_expression(C, n->expression, &e);
    luaK_exp2nextreg(fs, &e);
  }
}

/*-----------------------------------------------------------------------
 * Variable declaration
 *---------------------------------------------------------------------*/
static void compile_var_decl(CompileCtx *C, VariableDeclNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  TString *varname = mkstr(C, n->name);
  lu_byte kind = n->isConst ? RDKCONST : VDKREG;

  if (n->isGlobal) {
    kind = n->isConst ? GDKCONST : GDKREG;
    int vidx = ast_new_var(C, varname, kind);
    fs->nactvar++;

    if (n->initializer) {
      expdesc var;
      ast_buildglobal(C, varname, &var);
      expdesc val;
      compile_expression(C, n->initializer, &val);
      luaK_storevar(fs, &var, &val);
    }
  } else {
    int vidx = ast_new_var(C, varname, kind);

    if (n->initializer) {
      expdesc e;
      compile_expression(C, n->initializer, &e);

      Vardesc *var = &C->dyd->actvar.arr[fs->firstlocal + vidx];
      if (kind == RDKCONST && luaK_exp2const(fs, &e, &var->k)) {
        var->vd.kind = RDKCTC;
        fs->nactvar++;
        return;
      }

      luaK_exp2nextreg(fs, &e);
    } else {
      luaK_nil(fs, fs->freereg, 1);
      luaK_reserveregs(fs, 1);
    }
    ast_adjustlocalvars(C, 1);
  }
}

/*-----------------------------------------------------------------------
 * Multi-variable declaration  (vars a, b, c = expr)
 *
 * ast.h: MutiVariableDeclarationNode has:
 *   std::vector<MultiDeclVariableInfo> variables;  // .name, .isGlobal, .isConst
 *   Expression *initializer;
 *   bool isExported;
 *---------------------------------------------------------------------*/
static void compile_multi_var_decl(CompileCtx *C, MutiVariableDeclarationNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  int nvars = (int)n->variables.size();

  /* Determine if these are global or local from the first variable's flags.
     In practice all variables in a multi-decl share the same scope. */
  bool anyGlobal = false;
  for (auto &vi : n->variables) {
    lu_byte kind;
    if (vi.isGlobal) {
      anyGlobal = true;
      kind = vi.isConst ? GDKCONST : GDKREG;
    } else {
      kind = vi.isConst ? RDKCONST : VDKREG;
    }
    TString *ts = mkstr(C, vi.name);
    ast_new_var(C, ts, kind);
  }

  if (anyGlobal) {
    /* For globals, we need to bump nactvar first so global vars are visible,
       then compile the initializer, adjust the stack, and store each value
       into the global table via _ENV. */
    fs->nactvar = cast_short(fs->nactvar + nvars);

    if (n->initializer) {
      expdesc e;
      compile_expression(C, n->initializer, &e);
      ast_adjust_assign(C, nvars, 1, &e);
    } else {
      expdesc e;
      e.k = VVOID;
      ast_adjust_assign(C, nvars, 0, &e);
    }

    /* Store each value from the stack into the corresponding global */
    for (int i = nvars - 1; i >= 0; i--) {
      expdesc var;
      TString *ts = mkstr(C, n->variables[i].name);
      ast_buildglobal(C, ts, &var);
      expdesc src;
      init_exp(&src, VNONRELOC, --fs->freereg);
      luaK_storevar(fs, &var, &src);
    }
  } else {
    expdesc e;
    if (n->initializer) {
      compile_expression(C, n->initializer, &e);
      ast_adjust_assign(C, nvars, 1, &e);
    } else {
      e.k = VVOID;
      ast_adjust_assign(C, nvars, 0, &e);
    }
    ast_adjustlocalvars(C, nvars);
  }
}

/*-----------------------------------------------------------------------
 * Assignment  (a, b = expr1, expr2)
 *---------------------------------------------------------------------*/
static void compile_assignment(CompileCtx *C, AssignmentNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  int nlvals = (int)n->lvalues.size();
  int nrvals = (int)n->rvalues.size();

  std::vector<expdesc> lhs(nlvals);
  for (int i = 0; i < nlvals; i++) {
    compile_expression(C, n->lvalues[i], &lhs[i]);
    if (!vkisvar(lhs[i].k))
      compile_error(C, "invalid assignment target");
    ast_check_readonly(C, &lhs[i]);
  }

  if (nlvals == 1 && nrvals == 1) {
    expdesc val;
    compile_expression(C, n->rvalues[0], &val);
    luaK_setoneret(fs, &val);
    luaK_storevar(fs, &lhs[0], &val);
  } else {
    expdesc lastval;
    int nexps = compile_exprlist_n(C, n->rvalues, &lastval);
    if (nexps != nlvals)
      ast_adjust_assign(C, nlvals, nexps, &lastval);
    else {
      luaK_exp2nextreg(fs, &lastval);
    }
    for (int i = nlvals - 1; i >= 0; i--) {
      expdesc src;
      init_exp(&src, VNONRELOC, --fs->freereg);
      luaK_storevar(fs, &lhs[i], &src);
    }
  }
}

/*-----------------------------------------------------------------------
 * Update assignment  (a += expr, a -= expr, etc.)
 *
 * ast.h OperatorKind: ASSIGN_ADD, ASSIGN_SUB, ASSIGN_MUL, ASSIGN_DIV,
 *   ASSIGN_IDIV, ASSIGN_MOD, ASSIGN_CONCAT,
 *   ASSIGN_BW_AND, ASSIGN_BW_OR, ASSIGN_BW_XOR,
 *   ASSIGN_BW_LSHIFT, ASSIGN_BW_RSHIFT
 *---------------------------------------------------------------------*/
static void compile_update_assignment(CompileCtx *C, UpdateAssignmentNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  OperatorKind binop;
  switch (n->op) {
  case OperatorKind::ASSIGN_ADD:
    binop = OperatorKind::ADD;
    break;
  case OperatorKind::ASSIGN_SUB:
    binop = OperatorKind::SUB;
    break;
  case OperatorKind::ASSIGN_MUL:
    binop = OperatorKind::MUL;
    break;
  case OperatorKind::ASSIGN_DIV:
    binop = OperatorKind::DIV;
    break;
  case OperatorKind::ASSIGN_IDIV:
    binop = OperatorKind::IDIV;
    break;
  case OperatorKind::ASSIGN_MOD:
    binop = OperatorKind::MOD;
    break;
  case OperatorKind::ASSIGN_CONCAT:
    binop = OperatorKind::CONCAT;
    break;
  case OperatorKind::ASSIGN_BW_AND:
    binop = OperatorKind::BW_AND;
    break;
  case OperatorKind::ASSIGN_BW_OR:
    binop = OperatorKind::BW_OR;
    break;
  case OperatorKind::ASSIGN_BW_XOR:
    binop = OperatorKind::BW_XOR;
    break;
  case OperatorKind::ASSIGN_BW_LSHIFT:
    binop = OperatorKind::BW_LSHIFT;
    break;
  case OperatorKind::ASSIGN_BW_RSHIFT:
    binop = OperatorKind::BW_RSHIFT;
    break;
  default:
    compile_error(C, "unknown update assignment operator");
    return;
  }

  expdesc lhs;
  compile_expression(C, n->lvalue, &lhs);
  if (!vkisvar(lhs.k))
    compile_error(C, "invalid update assignment target");
  ast_check_readonly(C, &lhs);

  expdesc src = lhs;
  luaK_exp2anyreg(fs, &src);

  BinOpr opr = ast_binopr(binop);
  luaK_infix(fs, opr, &src);

  expdesc rhs;
  compile_expression(C, n->rvalue, &rhs);
  luaK_posfix(fs, opr, &src, &rhs, C->linenumber);

  luaK_exp2nextreg(fs, &src);

  expdesc storeval;
  init_exp(&storeval, VNONRELOC, fs->freereg - 1);
  luaK_storevar(fs, &lhs, &storeval);
}

/*-----------------------------------------------------------------------
 * If statement
 *---------------------------------------------------------------------*/
static void compile_if(CompileCtx *C, IfStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);
  int escapelist = NO_JUMP;

  {
    expdesc cond;
    compile_expression(C, n->condition, &cond);
    if (cond.k == VNIL)
      cond.k = VFALSE;
    luaK_goiftrue(fs, &cond);
    int condtrue = cond.f;

    if (n->thenBlock)
      compile_block(C, n->thenBlock);

    if (!n->elseIfClauses.empty() || n->elseBlock)
      luaK_concat(fs, &escapelist, luaK_jump(fs));

    luaK_patchtohere(fs, condtrue);
  }

  for (auto *clause : n->elseIfClauses) {
    setline(C, clause->location);
    expdesc cond;
    compile_expression(C, clause->condition, &cond);
    if (cond.k == VNIL)
      cond.k = VFALSE;
    luaK_goiftrue(fs, &cond);
    int condtrue = cond.f;

    if (clause->body)
      compile_block(C, clause->body);

    if (clause != n->elseIfClauses.back() || n->elseBlock)
      luaK_concat(fs, &escapelist, luaK_jump(fs));

    luaK_patchtohere(fs, condtrue);
  }

  if (n->elseBlock) {
    compile_block(C, n->elseBlock);
  }

  luaK_patchtohere(fs, escapelist);
}

/*-----------------------------------------------------------------------
 * While statement
 *---------------------------------------------------------------------*/
static void compile_while(CompileCtx *C, WhileStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  int whileinit = luaK_getlabel(fs);

  expdesc cond;
  compile_expression(C, n->condition, &cond);
  if (cond.k == VNIL)
    cond.k = VFALSE;
  luaK_goiftrue(fs, &cond);
  int condexit = cond.f;

  BlockCnt bl;
  ast_enterblock(C, fs, &bl, 1);
  if (n->body)
    compile_block(C, n->body);
  /* resolve continue → jump back to condition */
  resolve_continues(C, fs, whileinit);
  luaK_jumpto(fs, whileinit);
  ast_leaveblock(C, fs);

  luaK_patchtohere(fs, condexit);
}

/*-----------------------------------------------------------------------
 * Numeric for statement  (Lua-style)
 *
 * ast.h: ForNumericStatementNode has:
 *   std::string varName;
 *   AstType *typeAnnotation;    // nullable (untyped)
 *   Expression *startExpr;
 *   Expression *endExpr;
 *   Expression *stepExpr;       // nullable (default 1)
 *   BlockNode *body;
 *
 * Bytecode layout (matches Lua 5.5 numeric for):
 *   R[base+0] = start (for index)
 *   R[base+1] = limit (for limit)
 *   R[base+2] = step  (for step)
 *   R[base+3] = user variable
 *   FORPREP  base, offset    -- init & skip to FORLOOP
 *     <body>
 *   FORLOOP  base, offset    -- increment, test, loop back
 *---------------------------------------------------------------------*/
static void compile_for_numeric(CompileCtx *C, ForNumericStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  BlockCnt outerbl;
  ast_enterblock(C, fs, &outerbl, 1);

  int base = fs->freereg;

  /* 2 internal hidden variables (matches Lua 5.5 fornum) */
  TString *s_state = mkstr(C, "(for state)");
  ast_new_localvar(C, s_state); /* R[base+0]: for index */
  ast_new_localvar(C, s_state); /* R[base+1]: for limit */

  /* 1 user loop variable (const — user may not reassign it) */
  TString *vname = mkstr(C, n->varName);
  ast_new_var(C, vname, RDKCONST);

  /* Compile start → R[base+0] */
  {
    expdesc e;
    compile_expression(C, n->startExpr, &e);
    luaK_exp2nextreg(fs, &e);
  }

  /* Compile limit → R[base+1] */
  {
    expdesc e;
    compile_expression(C, n->endExpr, &e);
    luaK_exp2nextreg(fs, &e);
  }

  /* Compile step → R[base+2] (default 1 if omitted) */
  if (n->stepExpr) {
    expdesc e;
    compile_expression(C, n->stepExpr, &e);
    luaK_exp2nextreg(fs, &e);
  } else {
    luaK_int(fs, fs->freereg, 1);
    luaK_reserveregs(fs, 1);
  }

  /* Activate 2 internal variables (step is consumed by FORPREP) */
  ast_adjustlocalvars(C, 2);

  /* OP_FORPREP */
  int prep = luaK_codeABx(fs, OP_FORPREP, base, 0);
  fs->freereg--; /* FORPREP removes the step from the stack */

  /* Body block */
  {
    BlockCnt bodybl;
    ast_enterblock(C, fs, &bodybl, 0);
    ast_adjustlocalvars(C, 1); /* activate user loop variable */
    luaK_reserveregs(fs, 1);

    if (n->body)
      compile_block(C, n->body);

    ast_leaveblock(C, fs);
  }

  /* resolve continue → jump to FORLOOP */
  resolve_continues(C, fs, luaK_getlabel(fs));

  /* Fix FORPREP jump: forward past loop (to FORLOOP position) */
  {
    int forloop_pos = luaK_getlabel(fs);
    int prep_offset = forloop_pos - (prep + 1);
    SETARG_Bx(fs->f->code[prep], (unsigned int)prep_offset);
  }

  /* OP_FORLOOP */
  int endfor = luaK_codeABx(fs, OP_FORLOOP, base, 0);

  /* Fix FORLOOP jump: backward to body start */
  {
    int endfor_offset = endfor - prep;
    SETARG_Bx(fs->f->code[endfor], (unsigned int)endfor_offset);
  }
  luaK_fixline(fs, C->linenumber);

  ast_leaveblock(C, fs); /* outer block */
}

/*-----------------------------------------------------------------------
 * For-each statement
 *
 * ast.h: ForEachStatementNode has:
 *   std::vector<ParameterDeclNode *> loopVariables;   // each has .name
 *   std::vector<Expression *> iterableExprs;
 *   BlockNode *body;
 *---------------------------------------------------------------------*/
/* ========================================================================
 * COMPILER FIXES FOR GENERIC FOR LOOP WITH RECEIVER CALLING CONVENTION
 * ======================================================================== */

/*
 * Replace the compile_for_each function in ast_codegen.cpp
 *
 * Key Changes:
 * 1. Need to reserve extra stack space for the receiver parameter
 * 2. checkstack needs to account for 3 extra slots (receiver + func + state + control)
 */

/* ========================================================================
 * COMPILER FIXES FOR GENERIC FOR LOOP WITH RECEIVER CALLING CONVENTION
 * ======================================================================== */

/*
 * Replace the compile_for_each function in ast_codegen.cpp
 *
 * Key Changes:
 * 1. Need to reserve extra stack space for the receiver parameter
 * 2. checkstack needs to account for 3 extra slots (receiver + func + state + control)
 */

static void compile_for_each(CompileCtx *C, ForEachStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  BlockCnt outerbl;
  ast_enterblock(C, fs, &outerbl, 1);

  int base = fs->freereg;

  /* 3 internal hidden variables (matches Lua 5.5 forlist) */
  TString *s_state = mkstr(C, "(for state)");
  ast_new_localvar(C, s_state); /* R[base+0]: iterator function */
  ast_new_localvar(C, s_state); /* R[base+1]: state */
  ast_new_localvar(C, s_state); /* R[base+2]: closing var */

  /* User-declared loop variables: first is control (RDKCONST), rest normal */
  int nvars = (int)n->loopVariables.size();
  for (int i = 0; i < nvars; i++) {
    TString *vname = mkstr(C, n->loopVariables[i]->name);
    if (i == 0)
      ast_new_var(C, vname, RDKCONST);
    else
      ast_new_localvar(C, vname);
  }

  /* Compile iterator expressions (expect up to 4: func, state, close, init) */
  expdesc e;
  int nexps = compile_exprlist_n(C, n->iterableExprs, &e);
  ast_adjust_assign(C, 4, nexps, &e);

  /* Activate 3 internal variables (not 4 — control var is activated in body) */
  ast_adjustlocalvars(C, 3);

  /* Mark closing variable (3rd internal var) as to-be-closed */
  {
    BlockCnt *bl = fs->bl;
    bl->upval = 1;
    bl->insidetbc = 1;
    fs->needclose = 1;
  }

  /*
   * CRITICAL FIX: Need extra space for receiver calling convention
   * Original: needs 2 extra slots (state + control to call iterator)
   * With receiver: needs 3 extra slots (receiver + state + control)
   * So we check for 3 instead of 2
   */
  luaK_checkstack(fs, 3); /* extra space to call iterator with receiver */

  /* OP_TFORPREP */
  int prep = luaK_codeABx(fs, OP_TFORPREP, base, 0);
  fs->freereg--; /* TFORPREP removes one register from the stack */

  /* Body block: activate user loop variables (control + others) */
  {
    BlockCnt bodybl;
    ast_enterblock(C, fs, &bodybl, 0);
    ast_adjustlocalvars(C, nvars);
    luaK_reserveregs(fs, nvars);

    if (n->body)
      compile_block(C, n->body);

    ast_leaveblock(C, fs);
  }

  /* resolve continue → jump to iterator call (TFORCALL) */
  resolve_continues(C, fs, luaK_getlabel(fs));

  /* Fix TFORPREP jump: forward to TFORCALL position */
  {
    int dest = luaK_getlabel(fs);
    int offset = dest - (prep + 1);
    SETARG_Bx(fs->f->code[prep], (unsigned int)offset);
  }

  /* OP_TFORCALL */
  luaK_codeABC(fs, OP_TFORCALL, base, 0, nvars);
  luaK_fixline(fs, C->linenumber);

  /* OP_TFORLOOP */
  int endfor = luaK_codeABx(fs, OP_TFORLOOP, base, 0);

  /* Fix TFORLOOP jump: backward to body start (prep + 1) */
  {
    int offset = endfor - prep;
    SETARG_Bx(fs->f->code[endfor], (unsigned int)offset);
  }
  luaK_fixline(fs, C->linenumber);

  ast_leaveblock(C, fs);
}

/*-----------------------------------------------------------------------
 * Return statement
 *---------------------------------------------------------------------*/
static void compile_return(CompileCtx *C, ReturnStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  int first = (int)luaY_nvarstack(fs);
  int nret;

  if (n->returnValue.empty()) {
    nret = 0;
  } else {
    expdesc e;
    nret = compile_exprlist_n(C, n->returnValue, &e);
    if (hasmultret(e.k)) {
      luaK_setmultret(fs, &e);
      if (e.k == VCALL && nret == 1 && !fs->bl->insidetbc) {
        SET_OPCODE(fs->f->code[e.u.info], OP_TAILCALL);
      }
      nret = LUA_MULTRET;
    } else {
      if (nret == 1) {
        first = luaK_exp2anyreg(fs, &e);
      } else {
        luaK_exp2nextreg(fs, &e);
        lua_assert(nret == fs->freereg - first);
      }
    }
  }
  luaK_ret(fs, first, nret);
}

/*-----------------------------------------------------------------------
 * Break
 *---------------------------------------------------------------------*/
static void compile_break(CompileCtx *C, BreakStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  BlockCnt *bl;
  for (bl = fs->bl; bl != NULL; bl = bl->previous) {
    if (bl->isloop)
      goto found;
  }
  compile_error(C, "break outside loop");
found:
  bl->isloop = 2;

  int pc = luaK_jump(fs);

  Labellist *gl = &C->dyd->gt;
  int idx = gl->n;
  luaM_growvector(C->L, gl->arr, idx, gl->size, Labeldesc, SHRT_MAX, "labels/gotos");
  gl->arr[idx].name = C->brkn;
  gl->arr[idx].line = C->linenumber;
  gl->arr[idx].nactvar = fs->nactvar;
  gl->arr[idx].close = 0;
  gl->arr[idx].pc = pc;
  gl->n = idx + 1;
}

/*-----------------------------------------------------------------------
 * Continue
 *---------------------------------------------------------------------*/
static void compile_continue(CompileCtx *C, ContinueStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  BlockCnt *bl;
  for (bl = fs->bl; bl != NULL; bl = bl->previous) {
    if (bl->isloop)
      goto found;
  }
  compile_error(C, "continue outside loop");
found:

  int pc = luaK_jump(fs);

  Labellist *gl = &C->dyd->gt;
  int idx = gl->n;
  luaM_growvector(C->L, gl->arr, idx, gl->size, Labeldesc, SHRT_MAX, "labels/gotos");
  gl->arr[idx].name = C->contn;
  gl->arr[idx].line = C->linenumber;
  gl->arr[idx].nactvar = fs->nactvar;
  gl->arr[idx].close = 0;
  gl->arr[idx].pc = pc;
  gl->n = idx + 1;
}

/*-----------------------------------------------------------------------
 * Function declaration
 *---------------------------------------------------------------------*/
static void compile_func_decl(CompileCtx *C, FunctionDeclNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  TString *fname = mkstr(C, n->name);
  bool isGlobal = n->isGlobalDecl;
  bool isConst = n->isConst;

  if (isGlobal) {
    lu_byte kind = isConst ? GDKCONST : GDKREG;
    ast_new_var(C, fname, kind);
    fs->nactvar++;

    expdesc var;
    ast_buildglobal(C, fname, &var);

    expdesc b;
    {
      FuncState new_fs;
      BlockCnt bl;
      new_fs.f = ast_addprototype(C);
      new_fs.f->linedefined = C->linenumber;
      ast_open_func(C, &new_fs, &bl);

      compile_params(C, &new_fs, n->params, n->isVariadic, false);

      if (n->body)
        compile_block(C, n->body);

      new_fs.f->lastlinedefined = C->linenumber;
      ast_close_func(C);
    }
    ast_codeclosure(C, &b);
    luaK_storevar(fs, &var, &b);
    luaK_fixline(fs, n->location.line);
  } else {
    /* Local function */
    int fvar = fs->nactvar;
    if (isConst)
      ast_new_var(C, fname, RDKCONST);
    else
      ast_new_localvar(C, fname);
    ast_adjustlocalvars(C, 1); /* enter scope before compiling body */

    expdesc b;
    {
      FuncState new_fs;
      BlockCnt bl;
      new_fs.f = ast_addprototype(C);
      new_fs.f->linedefined = C->linenumber;
      ast_open_func(C, &new_fs, &bl);

      compile_params(C, &new_fs, n->params, n->isVariadic, false);

      if (n->body)
        compile_block(C, n->body);

      new_fs.f->lastlinedefined = C->linenumber;
      ast_close_func(C);
    }
    ast_codeclosure(C, &b);

    Vardesc *fvd = ast_getvar(C, fs, fvar);
    if (varinreg(fvd)) {
      int pidx = fvd->vd.pidx;
      if (pidx >= 0 && pidx < fs->ndebugvars)
        fs->f->locvars[pidx].startpc = fs->pc;
    }
  }
}

/*-----------------------------------------------------------------------
 * Class declaration
 *---------------------------------------------------------------------*/
static void compile_class_decl(CompileCtx *C, ClassDeclNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  TString *clsname = mkstr(C, n->name);

  /* Create local: ClassName = {} */
  int vidx = ast_new_localvar(C, clsname);
  {
    int pc = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
    luaK_code(fs, 0);
    expdesc tbl;
    init_exp(&tbl, VNONRELOC, fs->freereg);
    luaK_reserveregs(fs, 1);
    luaK_settablesize(fs, pc, tbl.u.info, 0, 0);
  }
  ast_adjustlocalvars(C, 1);

  /* Set __index = self */
  {
    expdesc cls;
    ast_singlevar(C, n->name, &cls);
    luaK_exp2anyregup(fs, &cls);

    expdesc key;
    key.f = key.t = NO_JUMP;
    key.k = VKSTR;
    key.u.strval = mkstr(C, "__index");
    luaK_indexed(fs, &cls, &key);

    expdesc val;
    ast_singlevar(C, n->name, &val);
    luaK_storevar(fs, &cls, &val);
  }

  /* Compile members */
  for (auto *member : n->members) {
    if (!member->memberDeclaration)
      continue;

    bool isStatic = member->isStatic;
    AstNode *decl = member->memberDeclaration;

    if (decl->nodeType == NodeType::FUNCTION_DECL) {
      FunctionDeclNode *fdecl = static_cast<FunctionDeclNode *>(decl);

      expdesc cls;
      ast_singlevar(C, n->name, &cls);
      luaK_exp2anyregup(fs, &cls);

      expdesc key;
      key.f = key.t = NO_JUMP;
      key.k = VKSTR;
      key.u.strval = mkstr(C, fdecl->name);
      luaK_indexed(fs, &cls, &key);

      expdesc b;
      {
        FuncState new_fs;
        BlockCnt bl;
        new_fs.f = ast_addprototype(C);
        new_fs.f->linedefined = C->linenumber;
        ast_open_func(C, &new_fs, &bl);

        /* compile_params now adds implicit 'self' for ALL functions,
           so no special handling needed for instance methods. */

        /* Method parameters — use fdecl->isVariadic */
        compile_params(C, &new_fs, fdecl->params, fdecl->isVariadic, !isStatic);

        if (fdecl->body)
          compile_block(C, fdecl->body);

        new_fs.f->lastlinedefined = C->linenumber;
        ast_close_func(C);
      }
      ast_codeclosure(C, &b);
      luaK_storevar(fs, &cls, &b);

    } else if (decl->nodeType == NodeType::VARIABLE_DECL) {
      VariableDeclNode *vdecl = static_cast<VariableDeclNode *>(decl);
      if (vdecl->initializer) {
        expdesc cls;
        ast_singlevar(C, n->name, &cls);
        luaK_exp2anyregup(fs, &cls);

        expdesc key;
        key.f = key.t = NO_JUMP;
        key.k = VKSTR;
        key.u.strval = mkstr(C, vdecl->name);
        luaK_indexed(fs, &cls, &key);

        expdesc val;
        compile_expression(C, vdecl->initializer, &val);
        luaK_storevar(fs, &cls, &val);
      }
    }
    fs->freereg = luaY_nvarstack(fs);
  }
}

/*-----------------------------------------------------------------------
 * Import statements
 *---------------------------------------------------------------------*/
static void compile_import_namespace(CompileCtx *C, ImportNamespaceNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  /* local alias = require("modulePath") */
  TString *alias = mkstr(C, n->alias);
  ast_new_localvar(C, alias);

  expdesc req;
  ast_singlevar(C, "require", &req);
  luaK_exp2nextreg(fs, &req);

  /* Push _ENV as Receiver (Scenario A: global function call) */
  expdesc env;
  ast_buildvar(C, C->envn, &env);
  luaK_exp2nextreg(fs, &env);

  expdesc arg;
  arg.f = arg.t = NO_JUMP;
  arg.k = VKSTR;
  arg.u.strval = mkstr(C, n->modulePath);
  luaK_exp2nextreg(fs, &arg);

  /* nparams = 2 (Receiver + modulePath), B = 3 */
  int base = req.u.info;
  init_exp(&req, VCALL, luaK_codeABC(fs, OP_CALL, base, 3, 2));
  luaK_fixline(fs, C->linenumber);
  fs->freereg = cast_byte(base + 1);

  ast_adjustlocalvars(C, 1);
}

static void compile_import_named(CompileCtx *C, ImportNamedNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  /* local __tmp = require("modulePath") */
  TString *tmpname = mkstr(C, "(import tmp)");
  ast_new_localvar(C, tmpname);

  expdesc req;
  ast_singlevar(C, "require", &req);
  luaK_exp2nextreg(fs, &req);

  /* Push _ENV as Receiver (Scenario A: global function call) */
  expdesc env;
  ast_buildvar(C, C->envn, &env);
  luaK_exp2nextreg(fs, &env);

  expdesc arg;
  arg.f = arg.t = NO_JUMP;
  arg.k = VKSTR;
  arg.u.strval = mkstr(C, n->modulePath);
  luaK_exp2nextreg(fs, &arg);

  /* nparams = 2 (Receiver + modulePath), B = 3 */
  int base = req.u.info;
  init_exp(&req, VCALL, luaK_codeABC(fs, OP_CALL, base, 3, 2));
  luaK_fixline(fs, C->linenumber);
  fs->freereg = cast_byte(base + 1);
  ast_adjustlocalvars(C, 1);

  /* For each specifier: local name = __tmp.originalName */
  for (auto *spec : n->specifiers) {
    /* getLocalName() returns alias if present, otherwise importedName */
    TString *localname = mkstr(C, spec->getLocalName());
    ast_new_localvar(C, localname);

    expdesc tmp;
    ast_singlevar(C, "(import tmp)", &tmp);
    luaK_exp2anyregup(fs, &tmp);

    expdesc key;
    key.f = key.t = NO_JUMP;
    key.k = VKSTR;
    key.u.strval = mkstr(C, spec->importedName);
    luaK_indexed(fs, &tmp, &key);
    luaK_exp2nextreg(fs, &tmp);

    ast_adjustlocalvars(C, 1);
  }
}

/*-----------------------------------------------------------------------
 * Defer statement
 *---------------------------------------------------------------------*/
static void compile_defer(CompileCtx *C, DeferStatementNode *n) {
  FuncState *fs = C->fs;
  setline(C, n->location);

  /* Create the deferred closure — now has implicit 'self' via compile_params.
     The VM calls __close(obj, err), so self = obj. */
  expdesc closure;
  {
    FuncState new_fs;
    BlockCnt bl;
    new_fs.f = ast_addprototype(C);
    new_fs.f->linedefined = C->linenumber;
    ast_open_func(C, &new_fs, &bl);

    /* Use compile_params with empty param list — adds implicit self */
    std::vector<ParameterDeclNode *> empty_params;
    compile_params(C, &new_fs, empty_params, false, false);

    if (n->body)
      compile_block(C, n->body);

    new_fs.f->lastlinedefined = C->linenumber;
    ast_close_func(C);
  }
  ast_codeclosure(C, &closure);
  luaK_exp2nextreg(fs, &closure);

  /* Wrap: setmetatable({}, {__close = <closure>}) */
  {
    /* Load setmetatable */
    expdesc sm;
    ast_singlevar(C, "setmetatable", &sm);
    luaK_exp2nextreg(fs, &sm);

    /* Push _ENV as Receiver (Scenario A: global function call) */
    expdesc env;
    ast_buildvar(C, C->envn, &env);
    luaK_exp2nextreg(fs, &env);

    /* First arg: {} */
    int pc1 = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
    luaK_code(fs, 0);
    expdesc empty;
    init_exp(&empty, VNONRELOC, fs->freereg);
    luaK_reserveregs(fs, 1);
    luaK_settablesize(fs, pc1, empty.u.info, 0, 0);

    /* Second arg: {__close = <closure>} */
    int pc2 = luaK_codevABCk(fs, OP_NEWTABLE, 0, 0, 0, 0);
    luaK_code(fs, 0);
    expdesc mt;
    init_exp(&mt, VNONRELOC, fs->freereg);
    luaK_reserveregs(fs, 1);

    /* mt.__close = closure */
    expdesc tab = mt;
    expdesc mkey;
    mkey.f = mkey.t = NO_JUMP;
    mkey.k = VKSTR;
    mkey.u.strval = mkstr(C, "__close");
    luaK_indexed(fs, &tab, &mkey);
    luaK_storevar(fs, &tab, &closure);
    luaK_settablesize(fs, pc2, mt.u.info, 0, 1);

    /* Call setmetatable(_ENV, {}, mt)
       nparams = 3 (Receiver + 2 args), B = 4 */
    int smbase = sm.u.info;
    init_exp(&sm, VCALL, luaK_codeABC(fs, OP_CALL, smbase, 4, 2));
    luaK_fixline(fs, C->linenumber);
    fs->freereg = cast_byte(smbase + 1);
  }

  /* Create to-be-closed local */
  TString *defername = mkstr(C, "(defer)");
  ast_new_var(C, defername, RDKTOCLOSE);
  ast_adjustlocalvars(C, 1);

  /* Mark to-be-closed */
  {
    BlockCnt *bl = fs->bl;
    bl->upval = 1;
    bl->insidetbc = 1;
    fs->needclose = 1;
  }
  lu_byte tbclevel = luaY_nvarstack(fs) - 1;
  luaK_codeABC(fs, OP_TBC, tbclevel, 0, 0);
}

/*-----------------------------------------------------------------------
 * Statement dispatch
 *---------------------------------------------------------------------*/
static void compile_statement(CompileCtx *C, Statement *stmt) {
  if (!stmt)
    return;
  setline_node(C, stmt);
  FuncState *fs = C->fs;

  switch (stmt->nodeType) {
  case NodeType::BLOCK:
    compile_block(C, static_cast<BlockNode *>(stmt));
    break;
  case NodeType::EXPRESSION_STATEMENT:
    compile_expr_stmt(C, static_cast<ExpressionStatementNode *>(stmt));
    break;
  case NodeType::VARIABLE_DECL:
    compile_var_decl(C, static_cast<VariableDeclNode *>(stmt));
    break;
  case NodeType::MUTI_VARIABLE_DECL:
    compile_multi_var_decl(C, static_cast<MutiVariableDeclarationNode *>(stmt));
    break;
  case NodeType::ASSIGNMENT:
    compile_assignment(C, static_cast<AssignmentNode *>(stmt));
    break;
  case NodeType::UPDATE_ASSIGNMENT:
    compile_update_assignment(C, static_cast<UpdateAssignmentNode *>(stmt));
    break;
  case NodeType::IF_STATEMENT:
    compile_if(C, static_cast<IfStatementNode *>(stmt));
    break;
  case NodeType::WHILE_STATEMENT:
    compile_while(C, static_cast<WhileStatementNode *>(stmt));
    break;
  case NodeType::FOR_NUMERIC_STATEMENT:
    compile_for_numeric(C, static_cast<ForNumericStatementNode *>(stmt));
    break;
  case NodeType::FOR_EACH_STATEMENT:
    compile_for_each(C, static_cast<ForEachStatementNode *>(stmt));
    break;
  case NodeType::RETURN_STATEMENT:
    compile_return(C, static_cast<ReturnStatementNode *>(stmt));
    break;
  case NodeType::BREAK_STATEMENT:
    compile_break(C, static_cast<BreakStatementNode *>(stmt));
    break;
  case NodeType::CONTINUE_STATEMENT:
    compile_continue(C, static_cast<ContinueStatementNode *>(stmt));
    break;
  case NodeType::FUNCTION_DECL:
    compile_func_decl(C, static_cast<FunctionDeclNode *>(stmt));
    break;
  case NodeType::CLASS_DECL:
    compile_class_decl(C, static_cast<ClassDeclNode *>(stmt));
    break;
  case NodeType::IMPORT_NAMESPACE:
    compile_import_namespace(C, static_cast<ImportNamespaceNode *>(stmt));
    break;
  case NodeType::IMPORT_NAMED:
    compile_import_named(C, static_cast<ImportNamedNode *>(stmt));
    break;
  case NodeType::DEFER_STATEMENT:
    compile_defer(C, static_cast<DeferStatementNode *>(stmt));
    break;
  default:
    compile_errorf(C, "unsupported statement type %d", (int)stmt->nodeType);
    break;
  }

  lua_assert(fs->f->maxstacksize >= fs->freereg && fs->freereg >= luaY_nvarstack(fs));
  fs->freereg = luaY_nvarstack(fs);
}

/*=======================================================================
 * Main entry point
 *=====================================================================*/

static void ast_mainfunc(CompileCtx *C, FuncState *fs, AstNode *root) {
  BlockCnt bl;
  ast_open_func(C, fs, &bl);

  /* Main function is always vararg */
  fs->f->flag |= PF_VAHID;
  luaK_codeABC(fs, OP_VARARGPREP, 0, 0, 0);

  /* Set up _ENV upvalue */
  Upvaldesc *env = &fs->f->upvalues[0];
  {
    Proto *f = fs->f;
    int oldsize = f->sizeupvalues;
    luaY_checklimit(fs, fs->nups + 1, 255, "upvalues");
    luaM_growvector(C->L, f->upvalues, fs->nups, f->sizeupvalues, Upvaldesc, 255, "upvalues");
    while (oldsize < f->sizeupvalues)
      f->upvalues[oldsize++].name = NULL;
    env = &f->upvalues[fs->nups++];
  }
  env->instack = 1;
  env->idx = 0;
  env->kind = VDKREG;
  env->name = C->envn;
  luaC_objbarrier(C->L, fs->f, env->name);

  /* Root must be a BlockNode */
  BlockNode *block = nullptr;
  if (root->nodeType == NodeType::BLOCK) {
    block = static_cast<BlockNode *>(root);
  } else {
    compile_error(C, "root AST node must be a BlockNode");
  }

  for (auto *stmt : block->statements) {
    compile_statement(C, stmt);
    C->fs->freereg = luaY_nvarstack(C->fs);
  }

  ast_close_func(C);
}

extern "C" LClosure *astY_compile(lua_State *L, AstNode *root, Dyndata *dyd, const char *name) {
  CompileCtx ctx{};
  FuncState funcstate = {};

  LClosure *cl = luaF_newLclosure(L, 1);
  setclLvalue2s(L, L->top.p, cl);
  luaD_inctop(L);

  ctx.L = L;
  ctx.ls.L = L; // 关键：lcode.c 需要通过 fs->ls->L 访问 L
  ctx.ls.dyd = dyd;
  ctx.fs = NULL;
  ctx.dyd = dyd;

  ctx.source = luaS_new(L, name);
  ctx.ls.source = ctx.source; /* lcode.c may access fs->ls->source */
  ctx.envn = luaS_newliteral(L, LUA_ENV);
  ctx.brkn = luaS_newliteral(L, "break");
  ctx.contn = luaS_newliteral(L, "(continue)");
  ctx.linenumber = 1;

  funcstate.f = cl->p = luaF_newproto(L);
  luaC_objbarrier(L, cl, cl->p);
  funcstate.f->source = ctx.source;
  luaC_objbarrier(L, funcstate.f, funcstate.f->source);

  dyd->actvar.n = dyd->gt.n = dyd->label.n = 0;

  ast_mainfunc(&ctx, &funcstate, root);

  lua_assert(!funcstate.prev && funcstate.nups == 1 && !ctx.fs);
  lua_assert(dyd->actvar.n == 0 && dyd->gt.n == 0 && dyd->label.n == 0);

  return cl;
}

extern "C" Proto *astY_compileFunction(lua_State *L, FuncState *parent_fs, Dyndata *dyd,
                                       AstNode *funcNode, const char *name) {
  CompileCtx ctx;
  ctx.L = L;
  ctx.ls.L = L;
  ctx.ls.dyd = dyd;
  ctx.fs = parent_fs;
  ctx.dyd = dyd;
  ctx.source = luaS_new(L, name);
  ctx.ls.source = ctx.source; /* lcode.c may access fs->ls->source */
  ctx.envn = luaS_newliteral(L, LUA_ENV);
  ctx.brkn = luaS_newliteral(L, "break");
  ctx.contn = luaS_newliteral(L, "(continue)");
  ctx.linenumber = 1;

  if (funcNode->nodeType == NodeType::LAMBDA) {
    expdesc e;
    compile_lambda(&ctx, static_cast<LambdaNode *>(funcNode), &e);
    return parent_fs->f->p[parent_fs->np - 1];
  } else if (funcNode->nodeType == NodeType::FUNCTION_DECL) {
    compile_func_decl(&ctx, static_cast<FunctionDeclNode *>(funcNode));
    return parent_fs->f->p[parent_fs->np - 1];
  }
  return NULL;
}