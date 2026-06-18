/*
 * spt/mem.h — Allocation and garbage collection interface.
 *
 * All heap traffic funnels through spt_realloc so the GC can account bytes and
 * a host can install its own allocator. Newly allocated GC objects are threaded
 * onto the global object list and tinted the current white.
 *
 * Collector status: the implemented collector is a correct stop-the-world
 * tri-colour mark/sweep. The object headers already carry the age bits a
 * generational collector needs; the generational upgrade partitions the same
 * object list and reuses the same write barrier — see ROADMAP in README.
 */
#ifndef SPT_MEM_H
#define SPT_MEM_H

#include "state.h"

/* Raw (un-tracked) allocation through the global allocator hook. */
SPT_API void *spt_realloc(spt_State *L, void *ptr, size_t osize, size_t nsize);
SPT_API void *spt_alloc(spt_State *L, size_t size);
SPT_API void  spt_free(spt_State *L, void *ptr, size_t size);

/* Allocate a GC object of `size` bytes with type tag `tag`, link it onto the
 * global list, and tint it the current white. */
SPT_API GCObject *spt_newobj(spt_State *L, uint8_t tag, size_t size);

/* Reclaim a single object (per-type destructor; implemented in object.c). */
SPT_API void spt_free_object(spt_State *L, GCObject *o);

/* Run one full collection cycle. Safe to call only at instruction boundaries. */
SPT_API void   spt_gc_collect(spt_State *L);
/* Collect if the live-bytes threshold has been crossed. */
SPT_API void   spt_gc_maybe(spt_State *L);
SPT_API size_t spt_gc_count(spt_State *L);   /* number of live objects        */

/*
 * Write barrier. Must be invoked after storing a collectable value into an
 * already-allocated container, so the incremental/generational invariant
 * ("no black object points at a white one") is preserved. For the current
 * stop-the-world collector it is a cheap no-op recorded here to keep every
 * mutation site correct ahead of the generational upgrade.
 */
SPT_INLINE void spt_barrier(spt_State *L, GCObject *container, const TValue *stored) {
  (void)L; (void)container; (void)stored;
  /* generational/incremental implementation slots in here */
}

#endif /* SPT_MEM_H */
