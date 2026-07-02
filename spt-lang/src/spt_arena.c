/*
** spt_arena.c — 线性 Arena 分配器实现。
*/
#include "spt_arena.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define SPT_ARENA_DEFAULT_BLOCK (64 * 1024)

/* 对齐到平台最大标量类型（避免 max_align_t，MSVC C11 支持不全）。 */
#define SPT_ARENA_ALIGN                                                                            \
  (sizeof(long double) > sizeof(void (*)()) ? sizeof(long double) : sizeof(void (*)()))

typedef struct SptArenaBlock {
  struct SptArenaBlock *next;
  size_t used;
  size_t cap;
  /* data[] 紧随其后 */
} SptArenaBlock;

struct SptArena {
  SptArenaBlock *head; /* 当前块（链表头，最新块在前） */
  size_t block_size;   /* 普通块的容量 */
  size_t bytes_used;   /* 累计有效字节（统计用） */
};

static size_t align_up(size_t n, size_t align) { return (n + (align - 1)) & ~(align - 1); }

static SptArenaBlock *block_new(size_t cap) {
  SptArenaBlock *b = (SptArenaBlock *)malloc(sizeof(SptArenaBlock) + cap);
  if (!b)
    return NULL;
  b->next = NULL;
  b->used = 0;
  b->cap = cap;
  return b;
}

SptArena *spt_arena_create(size_t block_size) {
  SptArena *a = (SptArena *)malloc(sizeof(SptArena));
  if (!a)
    return NULL;
  a->block_size = block_size ? block_size : SPT_ARENA_DEFAULT_BLOCK;
  a->bytes_used = 0;
  a->head = block_new(a->block_size);
  if (!a->head) {
    free(a);
    return NULL;
  }
  return a;
}

void *spt_arena_alloc(SptArena *a, size_t size) {
  if (!a)
    return NULL;
  if (size == 0)
    size = 1;

  size_t need = align_up(size, SPT_ARENA_ALIGN);
  SptArenaBlock *b = a->head;

  /* 当前块放不下：超大分配单独成块，否则新开一个标准块。 */
  if (b->used + need > b->cap) {
    size_t cap = need > a->block_size ? need : a->block_size;
    SptArenaBlock *nb = block_new(cap);
    if (!nb)
      return NULL;
    /* 大块插入链表头部，标准块仍在头部供后续小分配复用，
    ** 统一插入头部以保证简单与确定性。 */
    nb->next = a->head;
    a->head = nb;
    b = nb;
  }

  char *base = (char *)b + sizeof(SptArenaBlock);
  void *p = base + b->used;
  b->used += need;
  a->bytes_used += size;
  memset(p, 0, size);
  return p;
}

char *spt_arena_strndup(SptArena *a, const char *s, size_t n) {
  char *p = (char *)spt_arena_alloc(a, n + 1);
  if (!p)
    return NULL;
  if (n > 0 && s)
    memcpy(p, s, n);
  p[n] = '\0';
  return p;
}

char *spt_arena_strdup(SptArena *a, const char *s) {
  return spt_arena_strndup(a, s, s ? strlen(s) : 0);
}

void spt_arena_destroy(SptArena *a) {
  if (!a)
    return;
  SptArenaBlock *b = a->head;
  while (b) {
    SptArenaBlock *next = b->next;
    free(b);
    b = next;
  }
  free(a);
}

size_t spt_arena_bytes_used(const SptArena *a) { return a ? a->bytes_used : 0; }
