/*
** workspace.h — 工作区级跨文件符号索引（workspace/symbol）。
**
** 在 initialize 时记录根目录（来自 rootUri / workspaceFolders）。索引阶段递归扫描
** 根目录下的 *.spt，逐个容错解析并复用 sem_document_symbols 收集顶层与类成员符号，
** 摊平为带 Location 的 SymbolInformation 备查。查询按子串（不区分大小写）过滤。
**
** 取舍：索引优先用打开文档（未保存改动）的文本覆盖磁盘内容；didOpen/didChange/didClose
** 标记索引脏，下次 workspace/symbol 查询时懒重建。
*/
#ifndef SPT_LSP_WORKSPACE_H
#define SPT_LSP_WORKSPACE_H

#include "cJSON.h"
#include "documents.h"
#include "protocol.h"
#include "spt_lsp_bridge.h"

#include <stddef.h>

typedef struct {
  char *name;
  int kind;          /* LSP SymbolKind */
  char *uri;         /* 文件 URI */
  LspRange range;    /* 名字所在区间（selectionRange） */
  char *container;   /* 所属类名，可空 */
} WsSymbol;

/* 目标文件解析结果：unit（容错解析，arena 存活）+ doc（行索引，用于位置换算）。
   doc 指向 overlay 的 Document（不拥有）或 cache 拥有的 temp Document。 */
typedef struct {
  SptLspUnit *unit;       /* 可空（解析失败）；arena 拥有 AST/tokens/source */
  const Document *doc;    /* 可空；与 unit 配套，用于 byte<->LSP 换算 */
} WsUnit;

typedef struct {
  char **roots;
  int root_count;
  WsSymbol *syms;
  int sym_count, sym_cap;
  int indexed;
  int dirty;            /* 打开文档变更后置位，下次查询时重建索引 */
  const DocStore *overlay; /* 打开文档覆盖层：索引时优先用打开文档的文本，可空 */
  /* 目标文件解析缓存（跨文件 import 解析用）。dirty 时整体失效。 */
  struct {
    char *path;          /* 拥有，缓存键（本地路径） */
    SptLspUnit *unit;    /* 拥有；disk 文件的解析结果 */
    Document *temp_doc;  /* 拥有；disk 文件的临时 Document（非 overlay 时） */
    int parsing;         /* 正在解析标记（防环） */
  } *units;
  int unit_count, unit_cap;
} Workspace;

void workspace_init(Workspace *ws);
void workspace_free(Workspace *ws);

/* 由 file:// URI 增加一个根目录（忽略非 file URI）。 */
void workspace_add_root_uri(Workspace *ws, const char *root_uri);
/* 直接增加一个文件系统根目录路径。 */
void workspace_add_root_path(Workspace *ws, const char *path);

/* 设置打开文档覆盖层（索引时优先用打开文档的文本而非磁盘）。 */
void workspace_set_overlay(Workspace *ws, const DocStore *overlay);

/* 标记索引脏（打开文档变更后调用），下次查询时懒重建；同时清空目标文件缓存。 */
void workspace_mark_dirty(Workspace *ws);

/* 扫描根目录并建立符号索引（可重复调用以重建）。 */
void workspace_index(Workspace *ws);

/* workspace/symbol：返回匹配 query（子串，空串=全部）的 SymbolInformation[]。 */
cJSON *workspace_symbols(Workspace *ws, const char *query);

/* 跨文件 import 解析：由 from_uri（当前文件 URI）+ module_name 解析目标文件 URI。
   写入 out_uri（需至少 4096 字节）。返回 1=解析成功，0=未找到。 */
int workspace_resolve_module(Workspace *ws, const char *from_uri, const char *module_name,
                             char *out_uri, size_t cap);

/* 取目标文件的解析结果（带缓存）。path 为本地路径。
   - 若该文件在 overlay 中打开：用 overlay 文本解析（不缓存，每次重解析）。
   - 否则：读磁盘 + 解析 + 缓存（按 path），后续命中直接返回。
   - 防环：解析中再次请求同 path 返回 {NULL,NULL}。
   返回的 unit/doc 在下次 workspace_mark_dirty 前有效。 */
WsUnit workspace_get_unit(Workspace *ws, const char *path);

/* 工具：file:// URI <-> 本地路径（最小百分号解码/编码）。out 需足够大。 */
void spt_uri_to_path(const char *uri, char *out, size_t cap);
void spt_path_to_uri(const char *path, char *out, size_t cap);

#endif /* SPT_LSP_WORKSPACE_H */
