/*
 * object.c — String interning, the per-type destructor, and the List/Map/
 * function operations.
 *
 * List is the SPT-distinctive container: 0-based, dense, with a logical length
 * (alen) separate from physical capacity (asize). Indexing is a bounds check
 * plus a load — no array/hash dispatch, no key-type check, no metatable lookup.
 * Map is a small open-addressing table for arbitrary keys.
 */
#include "spt/mem.h"
#include <string.h>

/* ================================================================== */
/* String interning                                                    */
/* ================================================================== */
static uint32_t str_hash(const char *s, size_t len) {
  uint32_t h = 2166136261u;                 /* FNV-1a */
  for (size_t i = 0; i < len; i++) { h ^= (uint8_t)s[i]; h *= 16777619u; }
  return h;
}

static void strtab_grow(spt_State *L) {
  StrTab *st = &L->G->strtab;
  uint32_t newn = st->nbuckets * 2;
  String **nb = (String **)spt_alloc(L, newn * sizeof(String *));
  for (uint32_t i = 0; i < newn; i++) nb[i] = NULL;
  for (uint32_t i = 0; i < st->nbuckets; i++) {
    String *p = st->buckets[i];
    while (p) {
      String *nx = p->snext;
      uint32_t b = p->hash & (newn - 1);
      p->snext = nb[b]; nb[b] = p; p = nx;
    }
  }
  spt_free(L, st->buckets, st->nbuckets * sizeof(String *));
  st->buckets = nb; st->nbuckets = newn;
}

static void strtab_remove(spt_State *L, String *s) {
  StrTab *st = &L->G->strtab;
  uint32_t b = s->hash & (st->nbuckets - 1);
  String **pp = &st->buckets[b];
  while (*pp) {
    if (*pp == s) { *pp = s->snext; st->count--; return; }
    pp = &(*pp)->snext;
  }
}

String *spt_str_newlen(spt_State *L, const char *s, size_t len) {
  StrTab *st = &L->G->strtab;
  uint32_t h = str_hash(s, len);
  uint32_t b = h & (st->nbuckets - 1);
  for (String *p = st->buckets[b]; p; p = p->snext)
    if (p->len == len && memcmp(p->data, s, len) == 0) return p;   /* already interned */

  String *str = (String *)spt_newobj(L, SPT_TSTRING, sizeof(String) + len + 1);
  str->hash = h; str->len = (uint32_t)len;
  memcpy(str->data, s, len); str->data[len] = '\0';
  str->snext = st->buckets[b]; st->buckets[b] = str;
  st->count++;
  if (st->count > st->nbuckets) strtab_grow(L);
  return str;
}

String *spt_str_new(spt_State *L, const char *s) { return spt_str_newlen(L, s, strlen(s)); }
int     spt_str_eq(const String *a, const String *b) { return a == b; }   /* interned */

/* ================================================================== */
/* Destructor (called by the sweep phase)                              */
/* ================================================================== */
void spt_free_object(spt_State *L, GCObject *o) {
  switch (o->tt) {
    case SPT_TSTRING: {
      String *s = (String *)o;
      strtab_remove(L, s);
      spt_free(L, s, sizeof(String) + s->len + 1);
      break;
    }
    case SPT_TLIST: {
      Table *t = (Table *)o;
      spt_free(L, t->array, t->asize * sizeof(TValue));
      spt_free(L, t, sizeof(Table));
      break;
    }
    case SPT_TMAP: {
      Table *t = (Table *)o;
      spt_free(L, t->node, t->nsize * sizeof(MNode));
      spt_free(L, t, sizeof(Table));
      break;
    }
    case SPT_TCLOSURE: {
      Closure *c = (Closure *)o;
      spt_free(L, c, sizeof(Closure) + c->nups * sizeof(Upval *));
      break;
    }
    case SPT_TCFUNC: {
      CFunc *c = (CFunc *)o;
      spt_free(L, c, sizeof(CFunc) + c->nups * sizeof(TValue));
      break;
    }
    case SPT_TPROTO: {
      Proto *p = (Proto *)o;
      spt_free(L, p->code,   p->ncode * sizeof(Instr));
      spt_free(L, p->k,      p->nk    * sizeof(TValue));
      spt_free(L, p->p,      p->np    * sizeof(Proto *));
      spt_free(L, p->upvals, p->nups  * sizeof(UpvalDesc));
      spt_free(L, p, sizeof(Proto));
      break;
    }
    case SPT_TUPVAL:
      spt_free(L, o, sizeof(Upval));
      break;
    default:
      break;
  }
}

/* ================================================================== */
/* List                                                                */
/* ================================================================== */
Table *spt_list_new(spt_State *L, uint32_t hint) {
  Table *t = (Table *)spt_newobj(L, SPT_TLIST, sizeof(Table));
  t->mode = TMODE_LIST;
  t->array = hint ? (TValue *)spt_alloc(L, hint * sizeof(TValue)) : NULL;
  t->asize = hint; t->alen = 0;
  t->node = NULL; t->nsize = 0; t->nused = 0;
  t->metatable = NULL;
  return t;
}

static void list_reserve(spt_State *L, Table *t, uint32_t need) {
  if (need <= t->asize) return;
  uint32_t cap = t->asize ? t->asize * 2 : 4;
  if (cap < need) cap = need;
  t->array = (TValue *)spt_realloc(L, t->array,
                                   t->asize * sizeof(TValue), cap * sizeof(TValue));
  t->asize = cap;
}

void spt_list_push(spt_State *L, Table *t, const TValue *v) {
  list_reserve(L, t, t->alen + 1);
  setobj(&t->array[t->alen], v);
  t->alen++;
  spt_barrier(L, (GCObject *)t, v);
}

int spt_list_get(Table *t, spt_Integer idx, TValue *out) {
  if (SPT_UNLIKELY(idx < 0 || (uint64_t)idx >= t->alen)) return -1;   /* OOB */
  setobj(out, &t->array[idx]);
  return 0;
}

int spt_list_set(spt_State *L, Table *t, spt_Integer idx, const TValue *v) {
  if (idx >= 0 && (uint64_t)idx < t->alen) {           /* in range: overwrite  */
    setobj(&t->array[idx], v);
    spt_barrier(L, (GCObject *)t, v);
    return 0;
  }
  if (idx == (spt_Integer)t->alen) {                   /* at end: append       */
    spt_list_push(L, t, v);
    return 0;
  }
  return -1;                                           /* negative / past end  */
}

/* ================================================================== */
/* Map (open addressing, linear probing)                               */
/* ================================================================== */
Table *spt_map_new(spt_State *L) {
  Table *t = (Table *)spt_newobj(L, SPT_TMAP, sizeof(Table));
  t->mode = TMODE_MAP;
  t->array = NULL; t->asize = 0; t->alen = 0;
  t->node = NULL; t->nsize = 0; t->nused = 0;
  t->metatable = NULL;
  return t;
}

static uint32_t value_hash(const TValue *k) {
  switch (k->tt) {
    case SPT_TINT:   { uint64_t x = (uint64_t)k->v.i; return (uint32_t)(x ^ (x >> 32)); }
    case SPT_TFLOAT: { uint64_t x; memcpy(&x, &k->v.n, 8); return (uint32_t)(x ^ (x >> 32)); }
    case SPT_TBOOL:  return (uint32_t)(k->v.b ? 1 : 0);
    case SPT_TSTRING:return strvalue(k)->hash;
    default:         return 0;
  }
}

/* Map-key equality. Distinct tags are distinct keys (int 1 and float 1.0 are
 * different keys) — simple and predictable for the foundation. */
static int value_key_eq(const TValue *a, const TValue *b) {
  if (a->tt != b->tt) return 0;
  switch (a->tt) {
    case SPT_TINT:    return a->v.i == b->v.i;
    case SPT_TFLOAT:  return a->v.n == b->v.n;
    case SPT_TBOOL:   return a->v.b == b->v.b;
    case SPT_TSTRING: return a->v.gc == b->v.gc;   /* interned → pointer eq */
    default:          return 0;
  }
}

static uint32_t map_slot(Table *t, const TValue *key, uint32_t h) {
  uint32_t mask = t->nsize - 1;
  uint32_t i = h & mask;
  while (!ttisnull(&t->node[i].key)) {
    if (value_key_eq(&t->node[i].key, key)) break;
    i = (i + 1) & mask;
  }
  return i;
}

static void map_resize(spt_State *L, Table *t, uint32_t newsize) {
  MNode *old = t->node; uint32_t oldsize = t->nsize;
  t->node = (MNode *)spt_alloc(L, newsize * sizeof(MNode));
  for (uint32_t i = 0; i < newsize; i++) setnull(&t->node[i].key);
  t->nsize = newsize; t->nused = 0;
  for (uint32_t i = 0; i < oldsize; i++) {
    if (!ttisnull(&old[i].key)) {
      uint32_t s = map_slot(t, &old[i].key, value_hash(&old[i].key));
      t->node[s] = old[i]; t->nused++;
    }
  }
  spt_free(L, old, oldsize * sizeof(MNode));
}

void spt_map_set(spt_State *L, Table *t, const TValue *key, const TValue *v) {
  if (ttisnull(key)) return;                       /* null key is not storable */
  if (t->nsize == 0) map_resize(L, t, 8);
  else if ((t->nused + 1) * 10 >= t->nsize * 7) map_resize(L, t, t->nsize * 2);

  uint32_t i = map_slot(t, key, value_hash(key));
  if (ttisnull(&t->node[i].key)) {                 /* fresh slot */
    setobj(&t->node[i].key, key);
    t->nused++;
  }
  setobj(&t->node[i].val, v);
  spt_barrier(L, (GCObject *)t, v);
  spt_barrier(L, (GCObject *)t, key);
}

int spt_map_get(Table *t, const TValue *key, TValue *out) {
  if (t->nsize == 0 || ttisnull(key)) { setnull(out); return 0; }
  uint32_t i = map_slot(t, key, value_hash(key));
  if (ttisnull(&t->node[i].key)) { setnull(out); return 0; }
  setobj(out, &t->node[i].val);
  return 1;
}

spt_Integer spt_obj_len(const TValue *o) {
  if (ttislist(o))   return (spt_Integer)tblvalue(o)->alen;
  if (ttisstring(o)) return (spt_Integer)strvalue(o)->len;
  return 0;                                          /* Map and others: 0 */
}

/* ================================================================== */
/* Function objects                                                    */
/* ================================================================== */
Proto *spt_proto_new(spt_State *L) {
  Proto *p = (Proto *)spt_newobj(L, SPT_TPROTO, sizeof(Proto));
  p->numparams = 0; p->is_vararg = 0; p->maxstack = 2;
  p->code = NULL; p->ncode = 0;
  p->k = NULL; p->nk = 0;
  p->p = NULL; p->np = 0;
  p->upvals = NULL; p->nups = 0;
  p->source = NULL;
  p->jit_entry = NULL; p->call_count = 0;
  return p;
}

Closure *spt_closure_new(spt_State *L, Proto *p) {
  Closure *c = (Closure *)spt_newobj(L, SPT_TCLOSURE,
                                     sizeof(Closure) + p->nups * sizeof(Upval *));
  c->p = p; c->nups = p->nups;
  for (uint32_t i = 0; i < c->nups; i++) c->ups[i] = NULL;
  return c;
}

CFunc *spt_cfunc_new(spt_State *L, spt_CFunction fn, int nups) {
  CFunc *c = (CFunc *)spt_newobj(L, SPT_TCFUNC,
                                 sizeof(CFunc) + (size_t)nups * sizeof(TValue));
  c->fn = fn; c->nups = (uint32_t)nups;
  for (int i = 0; i < nups; i++) setnull(&c->ups[i]);
  return c;
}
