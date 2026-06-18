/*
** workspace.h — 工作区级跨文件符号索引（workspace/symbol）。
**
** 在 initialize 时记录根目录（来自 rootUri / workspaceFolders）。索引阶段递归扫描
** 根目录下的 *.spt，逐个容错解析并复用 sem_document_symbols 收集顶层与类成员符号，
** 摊平为带 Location 的 SymbolInformation 备查。查询按子串（不区分大小写）过滤。
**
** 取舍：索引读磁盘文件内容（未合并未保存的打开文档）；v1 足够，后续可让打开文档覆盖。
*/
#ifndef SPT_LSP_WORKSPACE_H
#define SPT_LSP_WORKSPACE_H

#include "cJSON.h"
#include "protocol.h"

#include <stddef.h>

typedef struct {
  char *name;
  int kind;          /* LSP SymbolKind */
  char *uri;         /* 文件 URI */
  LspRange range;    /* 名字所在区间（selectionRange） */
  char *container;   /* 所属类名，可空 */
} WsSymbol;

typedef struct {
  char **roots;
  int root_count;
  WsSymbol *syms;
  int sym_count, sym_cap;
  int indexed;
} Workspace;

void workspace_init(Workspace *ws);
void workspace_free(Workspace *ws);

/* 由 file:// URI 增加一个根目录（忽略非 file URI）。 */
void workspace_add_root_uri(Workspace *ws, const char *root_uri);
/* 直接增加一个文件系统根目录路径。 */
void workspace_add_root_path(Workspace *ws, const char *path);

/* 扫描根目录并建立符号索引（可重复调用以重建）。 */
void workspace_index(Workspace *ws);

/* workspace/symbol：返回匹配 query（子串，空串=全部）的 SymbolInformation[]。 */
cJSON *workspace_symbols(Workspace *ws, const char *query);

/* 工具：file:// URI <-> 本地路径（最小百分号解码/编码）。out 需足够大。 */
void spt_uri_to_path(const char *uri, char *out, size_t cap);
void spt_path_to_uri(const char *path, char *out, size_t cap);

#endif /* SPT_LSP_WORKSPACE_H */
