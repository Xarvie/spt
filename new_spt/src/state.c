/*
 * state.c — State lifecycle, the growable value stack, and error handling.
 *
 * Errors use setjmp/longjmp: spt_call installs a recovery point and any
 * spt_runtime_error below it unwinds back, turning a runtime fault (e.g. a
 * List out-of-bounds access) into a clean error return rather than a crash.
 *
 * The stack grows by reallocation; because call frames and open upvalues hold
 * raw pointers into it, every grow relocates them by the move delta. Callers
 * that may trigger a grow save/restore stack positions as indices.
 */
#include "spt/mem.h"
#include "jit/jit.h"
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <setjmp.h>

/* ------------------------------------------------------------------ */
/* Default allocator                                                   */
/* ------------------------------------------------------------------ */
static void *default_realloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud; (void)osize;
  if (nsize == 0) { free(ptr); return NULL; }
  return realloc(ptr, nsize);
}

/* ------------------------------------------------------------------ */
/* Errors                                                              */
/* ------------------------------------------------------------------ */
void spt_runtime_error(spt_State *L, const char *fmt, ...) {
  static char buf[512];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  L->errmsg = buf;
  if (L->errjmp) longjmp(*(jmp_buf *)L->errjmp, 1);
  fprintf(stderr, "spt: unprotected error: %s\n", buf);
  abort();
}

/* ------------------------------------------------------------------ */
/* Growable stack                                                      */
/* ------------------------------------------------------------------ */
static void relocate_stack(spt_State *L, TValue *oldstack) {
  ptrdiff_t d = L->stack - oldstack;
  L->top += d;
  for (CallInfo *ci = L->ci; ci; ci = ci->prev) {
    ci->func += d; ci->base += d; ci->top += d;
  }
  for (Upval *uv = L->openupval; uv; uv = uv->unext) uv->v += d;
}

void spt_checkstack(spt_State *L, int n) {
  if (SPT_LIKELY(L->top + n <= L->stack_last)) return;
  uint32_t need = (uint32_t)(L->top - L->stack) + (uint32_t)n + SPT_MIN_STACK;
  uint32_t newsize = L->stacksize;
  while (newsize < need) newsize *= 2;
  TValue *old = L->stack;
  L->stack = (TValue *)spt_realloc(L, L->stack,
                                   (size_t)L->stacksize * sizeof(TValue),
                                   (size_t)newsize * sizeof(TValue));
  L->stacksize = newsize;
  L->stack_last = L->stack + newsize - 1;
  relocate_stack(L, old);
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                           */
/* ------------------------------------------------------------------ */
spt_State *spt_newstate(void) {
  spt_Global *G = (spt_Global *)calloc(1, sizeof(spt_Global));
  spt_State  *L = (spt_State  *)calloc(1, sizeof(spt_State));
  if (!G || !L) { free(G); free(L); return NULL; }

  G->realloc_fn = default_realloc; G->alloc_ud = NULL;
  G->allgc = NULL; G->bytes = 0;
  G->gc_threshold = (size_t)SPT_GC_PAUSE_KB * 1024;
  G->current_white = GC_WHITE0; G->gc_disabled = 0;
  G->main = L;
  L->G = G;

  /* string intern table */
  G->strtab.nbuckets = 64; G->strtab.count = 0;
  G->strtab.buckets = (String **)spt_alloc(L, 64 * sizeof(String *));
  for (int i = 0; i < 64; i++) G->strtab.buckets[i] = NULL;

  /* value stack */
  L->stacksize = SPT_STACK_INIT;
  L->stack = (TValue *)spt_alloc(L, SPT_STACK_INIT * sizeof(TValue));
  L->top = L->stack;
  L->stack_last = L->stack + SPT_STACK_INIT - 1;
  for (uint32_t i = 0; i < L->stacksize; i++) setnull(&L->stack[i]);

  /* base call frame (the entry C frame) */
  L->base_ci.func = L->stack;
  L->base_ci.base = L->stack;
  L->base_ci.top  = L->stack;
  L->base_ci.savedpc = NULL;
  L->base_ci.nresults = 0;
  L->base_ci.prev = NULL;
  L->ci = &L->base_ci;

  L->openupval = NULL;
  L->errmsg = NULL;
  L->errjmp = NULL;

  /* globals map (created after stack exists so allocation is accounted) */
  G->globals = spt_map_new(L);

  spt_jit_init(L);   /* no-op in interpreter-only builds */

  return L;
}

void spt_close(spt_State *L) {
  spt_Global *G = L->G;
  spt_jit_shutdown(L);   /* no-op in interpreter-only builds */
  /* free every live object regardless of reachability */
  GCObject *o = G->allgc;
  while (o) { GCObject *nx = o->next; spt_free_object(L, o); o = nx; }
  spt_free(L, G->strtab.buckets, G->strtab.nbuckets * sizeof(String *));
  spt_free(L, L->stack, (size_t)L->stacksize * sizeof(TValue));
  free(G);
  free(L);
}
