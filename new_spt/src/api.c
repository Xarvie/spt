/*
 * api.c — The embedding API.
 *
 * Stack indices are 1-based relative to the current frame, matching the Slot-0
 * convention: index 1 is the receiver, index 2 the first real argument. The
 * spt_arg_* helpers add the +1 for you, so spt_arg_int(L, 1) is the first
 * argument a host function actually cares about.
 */
#include "spt.h"
#include <string.h>

#define SLOT(L, idx)  (&(L)->ci->base[(idx) - 1])

/* ---- pushing ---- */
void spt_pushnull (spt_State *L)               { spt_checkstack(L, 1); setnull(L->top);            L->top++; }
void spt_pushbool (spt_State *L, int b)        { spt_checkstack(L, 1); setbool(L->top, b);         L->top++; }
void spt_pushint  (spt_State *L, spt_Integer i){ spt_checkstack(L, 1); setint(L->top, i);          L->top++; }
void spt_pushfloat(spt_State *L, spt_Number n) { spt_checkstack(L, 1); setflt(L->top, n);          L->top++; }

void spt_pushstring(spt_State *L, const char *s) {
  spt_checkstack(L, 1);
  String *str = spt_str_new(L, s);
  setgco(L->top, (GCObject *)str, SPT_TSTRING);
  L->top++;
}

void spt_pushcfunction(spt_State *L, spt_CFunction fn) {
  spt_checkstack(L, 1);
  CFunc *cf = spt_cfunc_new(L, fn, 0);
  setgco(L->top, (GCObject *)cf, SPT_TCFUNC);
  L->top++;
}

void spt_pushvalue(spt_State *L, int idx) {
  spt_checkstack(L, 1);
  setobj(L->top, SLOT(L, idx));
  L->top++;
}

/* ---- reading ---- */
int          spt_isnull (spt_State *L, int idx) { return ttisnull(SLOT(L, idx)); }
int          spt_isbool (spt_State *L, int idx) { return ttisbool(SLOT(L, idx)); }
spt_Integer  spt_toint  (spt_State *L, int idx) {
  TValue *o = SLOT(L, idx);
  if (ttisint(o))   return ivalue(o);
  if (ttisfloat(o)) return (spt_Integer)fltvalue(o);
  return 0;
}
spt_Number   spt_tofloat(spt_State *L, int idx) {
  TValue *o = SLOT(L, idx);
  if (ttisfloat(o)) return fltvalue(o);
  if (ttisint(o))   return (spt_Number)ivalue(o);
  return 0.0;
}
int          spt_tobool (spt_State *L, int idx) { return spt_truthy(SLOT(L, idx)); }
const char  *spt_tostring(spt_State *L, int idx) {
  TValue *o = SLOT(L, idx);
  return ttisstring(o) ? str_cstr(strvalue(o)) : NULL;
}
int          spt_gettop (spt_State *L) { return (int)(L->top - L->ci->base); }

/* ---- arguments (Slot-0 aware: arg n == stack index n+1) ---- */
spt_Integer  spt_arg_int   (spt_State *L, int n) { return spt_toint   (L, n + 1); }
spt_Number   spt_arg_float (spt_State *L, int n) { return spt_tofloat (L, n + 1); }
const char  *spt_arg_string(spt_State *L, int n) { return spt_tostring(L, n + 1); }

/* ---- globals ---- */
void spt_setglobal(spt_State *L, const char *name) {
  String *key = spt_str_new(L, name);
  TValue tk; setgco(&tk, (GCObject *)key, SPT_TSTRING);
  spt_map_set(L, L->G->globals, &tk, L->top - 1);
  L->top--;
}

void spt_getglobal(spt_State *L, const char *name) {
  String *key = spt_str_new(L, name);
  TValue tk; setgco(&tk, (GCObject *)key, SPT_TSTRING);
  spt_checkstack(L, 1);
  spt_map_get(L->G->globals, &tk, L->top);
  L->top++;
}

void spt_register(spt_State *L, const char *name, spt_CFunction fn) {
  spt_pushcfunction(L, fn);
  spt_setglobal(L, name);
}
