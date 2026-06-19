/* definition.c — textDocument/definition
**
** 单文件：sem_resolve 给出当前文件内的定义。
** 跨文件（Phase 1）：若点击处是 import 引入的名字（具名导入 X / 命名空间成员 m.X），
** 解析目标模块路径，在目标文件的导出符号中查找定义，返回目标文件 Location。
** 降级：路径解析失败/目标无此导出 -> 回退到当前文件结果（可能为 null）。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

#include <string.h>

cJSON *feature_definition(const Document *d, LspPos pos, const char *uri, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
  cJSON *res = NULL;

  /* 跨文件 import 跳转：覆盖当前文件的（指向 import 语句的）局部结果。 */
  if (ws && u) {
    SemImportTarget t;
    if (sem_resolve_import_target(u, d, off, &t) && t.symbol_name[0]) {
      char tgt_uri[4096];
      if (workspace_resolve_module(ws, uri, t.module_path, tgt_uri, sizeof tgt_uri)) {
        char tgt_path[4096];
        spt_uri_to_path(tgt_uri, tgt_path, sizeof tgt_path);
        WsUnit wu = workspace_get_unit(ws, tgt_path);
        if (wu.unit && wu.doc) {
          SemRef xr;
          if (sem_resolve_export(wu.unit, wu.doc, t.symbol_name, &xr) && xr.has_def) {
            res = cJSON_CreateObject();
            cJSON_AddStringToObject(res, "uri", tgt_uri);
            cJSON_AddItemToObject(res, "range",
                                  lsp_range_to_json(doc_range(wu.doc, xr.def_start, xr.def_end)));
          }
        }
      }
      /* 跨文件未命中（模块无 .spt 源码，如 C 绑定）：回退到同文件 declare 块成员（README §13）。 */
      if (!res) {
        SemRef dr;
        if (sem_resolve_declare_member(u, d, t.module_path, t.symbol_name, &dr) && dr.has_def) {
          res = cJSON_CreateObject();
          cJSON_AddStringToObject(res, "uri", uri);
          cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, dr.def_start, dr.def_end)));
        }
      }
    }
  }

  /* 未跨文件命中：回退到当前文件结果。 */
  if (!res && r.found && r.has_def) {
    res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "uri", uri);
    cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, r.def_start, r.def_end)));
  }

  spt_lsp_unit_free(u);
  return res;
}
