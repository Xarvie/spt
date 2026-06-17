/*
** spt_arena.h
** ---------------------------------------------------------------------------
** 线性 Arena 分配器：AST 节点、节点列表与字符串的统一所有权。
**
** 设计要点（见迁移规划书 §5.2 / §5.6）：
**   - 所有 AST 内存都从一个 arena 申请，destroyAst 退化为一次 spt_arena_destroy，
**     O(1)、无递归、无逐节点 free，结构上不可能泄漏。
**   - 在 Lua 的 setjmp/longjmp 错误模型下天然安全：被长跳转绕过的中间分配
**     仍由 arena 统一回收。
**   - spt_arena_alloc 返回的内存清零，使节点的指针字段默认 NULL，
**     与原 C++ AST 的默认成员初始化（`Expr *operand = nullptr;`）行为一致。
** ---------------------------------------------------------------------------
*/
#ifndef SPT_ARENA_H
#define SPT_ARENA_H

#include <stddef.h>

typedef struct SptArena SptArena;

/* 创建 arena。block_size 为单块字节数，传 0 使用默认值（64 KiB）。
** 内存不足返回 NULL。 */
SptArena *spt_arena_create(size_t block_size);

/* 申请 size 字节，按 max_align_t 对齐，内容清零。失败返回 NULL。 */
void *spt_arena_alloc(SptArena *a, size_t size);

/* 复制以 NUL 结尾的字符串到 arena，返回新副本（永不为 NULL，除非 OOM）。 */
char *spt_arena_strdup(SptArena *a, const char *s);

/* 复制 s 的前 n 字节并补 NUL。可含嵌入 NUL（用于字符串字面量）。 */
char *spt_arena_strndup(SptArena *a, const char *s, size_t n);

/* 释放整个 arena 及其全部分配。a 可为 NULL。 */
void spt_arena_destroy(SptArena *a);

/* 统计：已分配的有效字节数（不含块头/对齐填充），用于诊断。 */
size_t spt_arena_bytes_used(const SptArena *a);

#endif /* SPT_ARENA_H */
