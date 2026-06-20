/* declaration.c — textDocument/declaration
**
** Phase 6b: declare 块成员跳到同文件 declare 块内的声明处；
** 普通符号回退到 definition（SPT 无头文件/实现分离概念）。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

cJSON *feature_declaration(const Document *d, LspPos pos, const char *uri, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  cJSON *res = NULL;

  /* 优先：若点击处是 import 引入的符号，且同文件有 declare 块，跳到 declare 声明处。 */
  if (u && ws) {
    SemImportTarget t;
    if (sem_resolve_import_target(u, d, off, &t) && t.symbol_name[0]) {
      SemRef dr;
      if (sem_resolve_declare_member(u, d, t.module_path, t.symbol_name, &dr) && dr.has_def) {
        res = cJSON_CreateObject();
        cJSON_AddStringToObject(res, "uri", uri);
        cJSON_AddItemToObject(res, "range",
                              lsp_range_to_json(doc_range(d, dr.def_start, dr.def_end)));
      }
    }
  }

  /* 回退：普通符号的声明即定义，复用 feature_definition。 */
  if (!res) {
    res = feature_definition(d, pos, uri, ws);
  }

  spt_lsp_unit_free(u);
  return res;
}
