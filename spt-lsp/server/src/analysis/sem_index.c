/*
** sem_index.c — 语义层符号哈希索引实现（Phase 5a）。
**
** 开放寻址哈希（djb2 散列 + 线性探测），负载因子 > 0.7 时扩容。
** 三张表：defs（文件级定义）、classes（类名）、funcs（函数/方法名）。
**
** 不拥有任何内存——name 指向 AST arena，node 指向 AST 节点。
** 降级：索引未命中时调用方回退线性扫描（零回归）。
*/
#include "sem_index.h"

#include <stdlib.h>
#include <string.h>

/* ---- djb2 散列 ---- */
static unsigned sem_hash_str(const char *s) {
  unsigned h = 5381;
  for (; *s; s++) h = h * 33 + (unsigned char)*s;
  return h;
}

/* ---- 通用开放寻址哈希 ---- */
static void hash_init(SemHash *h, int cap) {
  h->capacity = cap;
  h->count = 0;
  h->slots = (SemSlot *)calloc((size_t)cap, sizeof(SemSlot));
}

static void hash_free(SemHash *h) {
  free(h->slots);
  h->slots = NULL;
  h->capacity = h->count = 0;
}

/* 插入：同名覆盖（后插入者胜，匹配 sem_find_function 的"后声明优先"近似）。
   name 为 NULL 的槽位视为空。 */
static void hash_insert(SemHash *h, const char *name, const AstNode *node, int kind) {
  if (!name) return;
  if (h->count * 10 >= h->capacity * 7) {
    /* 扩容：重新插入所有已有条目。 */
    SemSlot *old = h->slots;
    int oldcap = h->capacity;
    hash_init(h, h->capacity * 2);
    for (int i = 0; i < oldcap; i++) {
      if (old[i].name) {
        unsigned k = sem_hash_str(old[i].name) & (unsigned)(h->capacity - 1);
        while (h->slots[k].name) k = (k + 1) & (h->capacity - 1);
        h->slots[k] = old[i];
        h->count++;
      }
    }
    free(old);
  }
  unsigned k = sem_hash_str(name) & (unsigned)(h->capacity - 1);
  while (h->slots[k].name) {
    if (strcmp(h->slots[k].name, name) == 0) {
      /* 同名覆盖。 */
      h->slots[k].node = node;
      h->slots[k].kind = kind;
      return;
    }
    k = (k + 1) & (h->capacity - 1);
  }
  h->slots[k].name = name;
  h->slots[k].node = node;
  h->slots[k].kind = kind;
  h->count++;
}

static const SemSlot *hash_lookup(const SemHash *h, const char *name) {
  if (!h->slots || !name) return NULL;
  unsigned k = sem_hash_str(name) & (unsigned)(h->capacity - 1);
  while (h->slots[k].name) {
    if (strcmp(h->slots[k].name, name) == 0) return &h->slots[k];
    k = (k + 1) & (h->capacity - 1);
  }
  return NULL;
}

/* ---- LSP SymbolKind 常量（避免依赖 protocol.h） ---- */
#define SK_FUNCTION 12
#define SK_CLASS 5
#define SK_METHOD 6
#define SK_FIELD 8
#define SK_VARIABLE 13
#define SK_CONSTANT 14
#define SK_MODULE 2

/* ---- 从 AST 构建索引 ---- */
static void index_class_members(SemIndex *idx, const AstNode *cls) {
  const AstList *m = &cls->u.class_decl.members;
  for (int i = 0; i < m->count; i++) {
    AstNode *decl = m->items[i]->u.class_member.member_declaration;
    if (!decl) continue;
    if (decl->type == NODE_FUNCTION_DECL && decl->u.func_decl.name)
      hash_insert(&idx->funcs, decl->u.func_decl.name, decl, SK_METHOD);
  }
}

SemIndex *sem_index_build(const AstNode *root) {
  if (!root || root->type != NODE_BLOCK) return NULL;
  SemIndex *idx = (SemIndex *)calloc(1, sizeof *idx);
  hash_init(&idx->defs, 64);
  hash_init(&idx->classes, 32);
  hash_init(&idx->funcs, 64);
  const AstList *st = &root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    switch (s->type) {
    case NODE_FUNCTION_DECL:
      if (s->u.func_decl.name) {
        hash_insert(&idx->defs, s->u.func_decl.name, s, SK_FUNCTION);
        hash_insert(&idx->funcs, s->u.func_decl.name, s, SK_FUNCTION);
      }
      break;
    case NODE_CLASS_DECL:
      if (s->u.class_decl.name) {
        hash_insert(&idx->defs, s->u.class_decl.name, s, SK_CLASS);
        hash_insert(&idx->classes, s->u.class_decl.name, s, SK_CLASS);
      }
      index_class_members(idx, s);
      break;
    case NODE_VARIABLE_DECL:
      if (s->u.var_decl.name)
        hash_insert(&idx->defs, s->u.var_decl.name, s,
                    s->u.var_decl.is_const ? SK_CONSTANT : SK_VARIABLE);
      break;
    case NODE_MUTI_VARIABLE_DECL:
      for (int k = 0; k < s->u.muti_var.count; k++)
        if (s->u.muti_var.vars[k].name)
          hash_insert(&idx->defs, s->u.muti_var.vars[k].name, s, SK_VARIABLE);
      break;
    case NODE_IMPORT_NAMESPACE:
      if (s->u.import_ns.alias)
        hash_insert(&idx->defs, s->u.import_ns.alias, s, SK_MODULE);
      break;
    case NODE_IMPORT_NAMED: {
      const AstList *sp = &s->u.import_named.specifiers;
      for (int k = 0; k < sp->count; k++) {
        AstNode *spec = sp->items[k];
        const char *nm = spec->u.import_spec.alias ? spec->u.import_spec.alias
                                                   : spec->u.import_spec.imported_name;
        if (nm) hash_insert(&idx->defs, nm, spec, SK_VARIABLE);
      }
      break;
    }
    case NODE_DECLARE_MODULE: {
      const AstList *mm = &s->u.declare_module.members;
      for (int k = 0; k < mm->count; k++) {
        AstNode *m = mm->items[k];
        if (m->type == NODE_CLASS_DECL && m->u.class_decl.name)
          hash_insert(&idx->classes, m->u.class_decl.name, m, SK_CLASS);
        if (m->type == NODE_FUNCTION_DECL && m->u.func_decl.name)
          hash_insert(&idx->funcs, m->u.func_decl.name, m, SK_FUNCTION);
      }
      break;
    }
    default: break;
    }
  }
  return idx;
}

void sem_index_free(SemIndex *idx) {
  if (!idx) return;
  hash_free(&idx->defs);
  hash_free(&idx->classes);
  hash_free(&idx->funcs);
  free(idx);
}

const SemSlot *sem_index_lookup_def(const SemIndex *idx, const char *name) {
  return idx ? hash_lookup(&idx->defs, name) : NULL;
}

const AstNode *sem_index_lookup_class(const SemIndex *idx, const char *name) {
  const SemSlot *s = idx ? hash_lookup(&idx->classes, name) : NULL;
  return s ? s->node : NULL;
}

const AstNode *sem_index_lookup_func(const SemIndex *idx, const char *name) {
  const SemSlot *s = idx ? hash_lookup(&idx->funcs, name) : NULL;
  return s ? s->node : NULL;
}
