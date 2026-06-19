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
  int is_ambient;     /* 定义属于 declare 外部符号（不可重命名/不可改） */

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

/* Phase 2: 推断接收者类型，列出该类型的成员。成功返回 1（已通过 cb 输出），失败返回 0。
   recv_name 为接收者标识符名，dot_off 为点号字节位置。 */
int sem_members_of_receiver(const SptLspUnit *u, const Document *d,
                            const char *recv_name, size_t dot_off, SemSymCb cb, void *ctx);

/* 包含 off 的最内层函数声明（NODE_FUNCTION_DECL），无则 NULL。 */
const AstNode *sem_enclosing_function(const SptLspUnit *u, const Document *d, size_t off);

/* 按名查找函数（顶层/方法/declare 成员），用于 signature help。 */
const AstNode *sem_find_function(const SptLspUnit *u, const char *name);

/* ===========================================================================
** 跨文件 import 解析（Phase 1）
** ========================================================================= */

/* import 目标解析结果：点击处标识符若由 import 引入，给出目标模块名与目标符号名。 */
typedef struct {
  int found;
  char module_path[256];  /* 目标模块名（已去引号，来自 import 语句） */
  char symbol_name[256];  /* 目标符号名（在目标文件中查找的名字）；
                             空串表示点击的是命名空间别名 m 本身（无具体符号） */
  int is_namespace_self;  /* 1 = 点击的是 `import * as m` 的 m 本身 */
} SemImportTarget;

/* 解析 byte_off 处标识符是否由 import 引入。
   - 具名导入 import { X } from "mod"（点击 X 或其别名 Y）：module_path=mod, symbol_name=X(原始名)
   - 命名空间 import * as m from "mod"（点击 m）：module_path=mod, symbol_name="", is_namespace_self=1
   - m.X 成员访问（点击 X，接收者 m 是命名空间别名）：module_path=mod, symbol_name=X
   找到返回 1。 */
int sem_resolve_import_target(const SptLspUnit *u, const Document *d, size_t byte_off,
                              SemImportTarget *out);

/* 在目标文件 unit 中按名查找导出符号（is_exported && is_module_root）。
   找到则填充 out 的定义部分（def_start/def_end/kind/is_ambient/detail/doc，基于目标文档 d），
   并置 found=1。位置用字节偏移。返回 1=找到。 */
int sem_resolve_export(const SptLspUnit *u, const Document *d, const char *name, SemRef *out);

/* 在当前文件的 `declare from "module_path" { ... }` 块中按名查找成员。
   用于 import { X } from "mod" 且 mod 无 .spt 源码（C 绑定模块）时，
   跳转到同文件的 declare 声明处（README §13）。找到返回 1 并填充 out。 */
int sem_resolve_declare_member(const SptLspUnit *u, const Document *d,
                               const char *module_path, const char *symbol_name, SemRef *out);

/* 枚举目标文件所有导出符号（用于命名空间导入 m. 的成员补全）。
   detail 可为 ""。 */
typedef void (*SemExportCb)(void *ctx, const char *name, int kind, const char *detail);
void sem_all_exports(const SptLspUnit *u, SemExportCb cb, void *ctx);

/* 若 name 是命名空间导入别名（import * as name from "..."），返回 1 并写 module_path。
   用于成员补全：m. 后列出目标模块的导出符号。 */
int sem_namespace_import_path(const SptLspUnit *u, const char *name, char *module_path, size_t cap);

/* Phase 3: 若 name 是具名导入绑定（import { name } from "mod"），返回 1 并写 module_path。
   用于跨文件签名帮助等按名查找场景。 */
int sem_import_binding_path(const SptLspUnit *u, const char *name, char *module_path, size_t cap);

#endif /* SPT_LSP_SEMANTIC_H */
