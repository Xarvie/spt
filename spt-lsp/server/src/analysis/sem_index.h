/*
** sem_index.h — 语义层符号哈希索引（Phase 5a）。
**
** 为单个 SptLspUnit 构建文件级符号的开放寻址哈希，将 sem_find_function /
** find_class_by_name / find_def_by_name 的文件级查找从 O(n) 线性扫描降为 O(1)。
**
** 索引不拥有任何内存——name 指针指向 AST arena，node 指针指向 AST 节点。
** 生命周期：由 sem_index_build 创建，sem_index_free 释放槽位数组。
** 缓存策略由调用方（semantic.c）管理：单条目静态缓存，按 unit 指针命中。
*/
#ifndef SPT_LSP_SEM_INDEX_H
#define SPT_LSP_SEM_INDEX_H

#include "spt_ast.h"

#include <stddef.h>

typedef struct {
  const char *name;    /* 键：符号名（指向 AST arena，不拥有） */
  const AstNode *node; /* 值：定义节点（不拥有） */
  int kind;            /* LSP SymbolKind */
} SemSlot;

typedef struct {
  SemSlot *slots; /* 开放寻址槽位，容量为 2 的幂 */
  int capacity;   /* 槽位总数 */
  int count;      /* 已填充数（不含墓碑） */
} SemHash;

typedef struct {
  SemHash defs;    /* 文件级定义：函数/类/变量/import 名 → 节点 */
  SemHash classes; /* 类名 → class_decl 节点（含 declare 模块内的类） */
  SemHash funcs;   /* 函数/方法名 → func_decl 节点（顶层 + 类方法 + declare 成员） */
} SemIndex;

/* 为 unit 构建索引。返回 NULL 若 root 为空。调用方拥有返回值。 */
SemIndex *sem_index_build(const AstNode *root);

/* 释放索引。idx 可为 NULL。 */
void sem_index_free(SemIndex *idx);

/* 在 defs 表中按名查找。找到返回槽位指针，否则 NULL。 */
const SemSlot *sem_index_lookup_def(const SemIndex *idx, const char *name);

/* 在 classes 表中按名查找 class_decl。找到返回节点，否则 NULL。 */
const AstNode *sem_index_lookup_class(const SemIndex *idx, const char *name);

/* 在 funcs 表中按名查找函数/方法。找到返回节点，否则 NULL。 */
const AstNode *sem_index_lookup_func(const SemIndex *idx, const char *name);

#endif /* SPT_LSP_SEM_INDEX_H */
