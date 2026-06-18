/*
** semantic.h — 语义层：基于前端 AST 的符号收集与名字解析。
**
** 设计取舍（务实而稳健，覆盖绝大多数真实用例）：
**   - 文档符号：直接遍历顶层声明（函数/类+成员/变量/import/declare），层级化输出。
**   - 名字解析：先找点击处的标识符 token；
**       · 成员访问（前驱为 '.' / ':'）-> 在全文件的类成员中按名查找；
**       · 普通名字 -> 优先「所在函数的参数/局部」，再「文件级定义」(函数/类/变量/import)。
**     采用提升式解析（忽略同作用域内文本先后），最贴合编辑器中的跳转预期。
**   - 位置统一用字节偏移；与 Document 的行索引互转。
*/
#ifndef SPT_LSP_SEMANTIC_H
#define SPT_LSP_SEMANTIC_H

#include "cJSON.h"
#include "documents.h"
#include "spt_lsp_bridge.h"

/* 一次解析结果（解析点击处标识符所指向的定义）。位置统一用字节偏移。 */
typedef struct {
  int found;       /* 是否定位到一个标识符 */
  char name[256];  /* 标识符文本 */
  int is_member;   /* 是否作为成员访问解析 */

  int has_def;        /* 是否找到定义位置 */
  size_t def_start;   /* 定义名字的起始字节偏移 */
  size_t def_end;     /* 定义名字的结束字节偏移 */
  int kind;           /* LSP SymbolKind */

  char detail[512]; /* 悬浮签名/类型 */
  char doc[1024];   /* 文档注释（若有） */

  size_t use_start; /* 点击处标识符的起始字节偏移 */
  size_t use_end;   /* 结束字节偏移 */
} SemRef;

/* 文档符号（textDocument/documentSymbol 的 DocumentSymbol[]）。调用方拥有返回值。 */
cJSON *sem_document_symbols(const SptLspUnit *u, const Document *d);

/* 解析 byte_off 处标识符所指向的定义。返回是否找到标识符（found）。 */
SemRef sem_resolve(const SptLspUnit *u, const Document *d, size_t byte_off);

/* 收集与 byte_off 处标识符同一指代的所有出现（字节区间），写入回调。
** 返回出现次数。include_decl 为是否包含定义本身。 */
int sem_references(const SptLspUnit *u, const Document *d, size_t byte_off, int include_decl,
                   void (*cb)(void *ctx, size_t start, size_t end), void *ctx);

/* 类型注解 -> 可读字符串（写入 out，至多 cap-1 字节）。 */
void sem_type_string(const AstNode *type, char *out, size_t cap);

/* 符号枚举回调（补全用）。detail 可为 ""。 */
typedef void (*SemSymCb)(void *ctx, const char *name, int kind, const char *detail);

/* 枚举 off 处可见的符号（所在函数参数/局部 + 文件级）。 */
void sem_visible_symbols(const SptLspUnit *u, const Document *d, size_t off, SemSymCb cb, void *ctx);

/* 枚举全文件可作为成员补全的名字（类成员 + declare 模块成员）。 */
void sem_all_members(const SptLspUnit *u, SemSymCb cb, void *ctx);

/* 包含 off 的最内层函数声明（NODE_FUNCTION_DECL），无则 NULL。 */
const AstNode *sem_enclosing_function(const SptLspUnit *u, const Document *d, size_t off);

/* 按名查找函数（顶层/方法/declare 成员），用于 signature help。 */
const AstNode *sem_find_function(const SptLspUnit *u, const char *name);

#endif /* SPT_LSP_SEMANTIC_H */
