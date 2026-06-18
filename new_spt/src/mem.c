/*
 * mem.c — Allocation accounting + the mark/sweep collector.
 *
 * The collector is stop-the-world and only runs at instruction boundaries
 * (driven from the interpreter loop via spt_gc_maybe, or explicitly via
 * spt_gc_collect). Object headers already carry the generational age bits;
 * upgrading to a generational collector means partitioning `allgc` into young
 * and old lists and making the write barrier (spt_barrier) record old->young
 * edges — the mutator-side call sites are already in place.
 */
#include "spt/mem.h"
#include <stdlib.h>

/* ------------------------------------------------------------------ */
/* Raw allocation                                                      */
/* ------------------------------------------------------------------ */
void *spt_realloc(spt_State *L, void *ptr, size_t osize, size_t nsize) {
  spt_Global *G = L->G;
  void *np = G->realloc_fn(G->alloc_ud, ptr, osize, nsize);
  if (SPT_UNLIKELY(np == NULL && nsize > 0)) {
    spt_runtime_error(L, "out of memory");
    return NULL;
  }
  G->bytes += nsize - osize;
  return np;
}

void *spt_alloc(spt_State *L, size_t size) { return spt_realloc(L, NULL, 0, size); }
void  spt_free(spt_State *L, void *ptr, size_t size) { spt_realloc(L, ptr, size, 0); }

GCObject *spt_newobj(spt_State *L, uint8_t tag, size_t size) {
  GCObject *o = (GCObject *)spt_alloc(L, size);
  o->tt = tag;
  o->marked = L->G->current_white;   /* born white (collectible candidate) */
  o->next = L->G->allgc;
  L->G->allgc = o;
  return o;
}

/* ------------------------------------------------------------------ */
/* Marking                                                             */
/* ------------------------------------------------------------------ */
static void mark_object(spt_State *L, GCObject *o);

SPT_INLINE void mark_value(spt_State *L, const TValue *v) {
  if (iscollectable(v)) mark_object(L, gcvalue(v));
}

static void mark_object(spt_State *L, GCObject *o) {
  if (o == NULL || isblack(o)) return;
  o->marked = (o->marked & ~GC_WHITEBITS) | GC_BLACK;

  switch (o->tt) {
    case SPT_TSTRING:
      break;
    case SPT_TLIST: {
      Table *t = (Table *)o;
      for (uint32_t i = 0; i < t->alen; i++) mark_value(L, &t->array[i]);
      if (t->metatable) mark_object(L, (GCObject *)t->metatable);
      break;
    }
    case SPT_TMAP: {
      Table *t = (Table *)o;
      for (uint32_t i = 0; i < t->nsize; i++) {
        MNode *n = &t->node[i];
        if (!ttisnull(&n->key)) { mark_value(L, &n->key); mark_value(L, &n->val); }
      }
      if (t->metatable) mark_object(L, (GCObject *)t->metatable);
      break;
    }
    case SPT_TCLOSURE: {
      Closure *c = (Closure *)o;
      mark_object(L, (GCObject *)c->p);
      for (uint32_t i = 0; i < c->nups; i++) mark_object(L, (GCObject *)c->ups[i]);
      break;
    }
    case SPT_TCFUNC: {
      CFunc *c = (CFunc *)o;
      for (uint32_t i = 0; i < c->nups; i++) mark_value(L, &c->ups[i]);
      break;
    }
    case SPT_TPROTO: {
      Proto *p = (Proto *)o;
      for (uint32_t i = 0; i < p->nk; i++) mark_value(L, &p->k[i]);
      for (uint32_t i = 0; i < p->np; i++) mark_object(L, (GCObject *)p->p[i]);
      if (p->source) mark_object(L, (GCObject *)p->source);
      break;
    }
    case SPT_TUPVAL: {
      Upval *uv = (Upval *)o;
      mark_value(L, uv->v);   /* open: a stack slot; closed: &uv->closed */
      break;
    }
    default:
      break;
  }
}

static void mark_roots(spt_State *L) {
  spt_Global *G = L->G;
  for (TValue *s = L->stack; s < L->top; s++) mark_value(L, s);
  if (G->globals) mark_object(L, (GCObject *)G->globals);
  /* Upvalues are reached transitively through the closures that hold them. */
}

/* ------------------------------------------------------------------ */
/* Collection                                                          */
/* ------------------------------------------------------------------ */
void spt_gc_collect(spt_State *L) {
  spt_Global *G = L->G;
  if (G->gc_disabled) return;
  G->gc_disabled++;

  mark_roots(L);

  GCObject **p = &G->allgc;
  while (*p) {
    GCObject *o = *p;
    if (isblack(o) || isfixed(o)) {
      o->marked &= ~GC_BLACK;        /* survive: repaint white for next cycle */
      p = &o->next;
    } else {
      *p = o->next;                  /* dead: unlink and reclaim              */
      spt_free_object(L, o);
    }
  }

  G->gc_threshold = G->bytes + (size_t)SPT_GC_PAUSE_KB * 1024;
  G->gc_disabled--;
}

void spt_gc_maybe(spt_State *L) {
  if (SPT_UNLIKELY(L->G->bytes > L->G->gc_threshold)) spt_gc_collect(L);
}

size_t spt_gc_count(spt_State *L) {
  size_t n = 0;
  for (GCObject *o = L->G->allgc; o; o = o->next) n++;
  return n;
}
