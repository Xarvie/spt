/* hover.c — textDocument/hover
**
** 单文件：sem_resolve 给出当前文件定义的签名/文档。
** 跨文件（Phase 1）：若点击处是 import 引入的名字，显示目标文件导出定义的签名/文档。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

#include <stdio.h>
#include <string.h>

cJSON *feature_hover(const Document *d, LspPos pos, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);

  /* 跨文件 import：用目标文件导出定义的 detail/doc 覆盖。 */
  if (ws && u) {
    SemImportTarget t;
    if (sem_resolve_import_target(u, d, off, &t) && t.symbol_name[0]) {
      int overridden = 0;
      char tgt_uri[4096];
      if (workspace_resolve_module(ws, d->uri ? d->uri : "", t.module_path, tgt_uri, sizeof tgt_uri)) {
        char tgt_path[4096];
        spt_uri_to_path(tgt_uri, tgt_path, sizeof tgt_path);
        WsUnit wu = workspace_get_unit(ws, tgt_path);
        if (wu.unit && wu.doc) {
          SemRef xr;
          if (sem_resolve_export(wu.unit, wu.doc, t.symbol_name, &xr)) {
            r.detail[0] = '\0';
            if (xr.detail[0]) snprintf(r.detail, sizeof r.detail, "%s", xr.detail);
            r.doc[0] = '\0';
            if (xr.doc[0]) snprintf(r.doc, sizeof r.doc, "%s", xr.doc);
            overridden = 1;
          }
        }
      }
      /* 跨文件未命中（C 绑定模块无 .spt 源码）：回退到同文件 declare 块成员签名（README §13）。 */
      if (!overridden) {
        SemRef dr;
        if (sem_resolve_declare_member(u, d, t.module_path, t.symbol_name, &dr)) {
          r.detail[0] = '\0';
          if (dr.detail[0]) snprintf(r.detail, sizeof r.detail, "%s", dr.detail);
          r.doc[0] = '\0';
          if (dr.doc[0]) snprintf(r.doc, sizeof r.doc, "%s", dr.doc);
        }
      }
    }
  }

  cJSON *res = NULL;
  if (r.found && r.detail[0]) {
    char value[2048];
    int n = snprintf(value, sizeof value, "```spt\n%s\n```", r.detail);
    if (r.doc[0] && n > 0 && (size_t)n < sizeof value)
      snprintf(value + n, sizeof value - (size_t)n, "\n\n---\n%s", r.doc);
    res = cJSON_CreateObject();
    cJSON *contents = cJSON_CreateObject();
    cJSON_AddStringToObject(contents, "kind", "markdown");
    cJSON_AddStringToObject(contents, "value", value);
    cJSON_AddItemToObject(res, "contents", contents);
    cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, r.use_start, r.use_end)));
  }
  spt_lsp_unit_free(u);
  return res;
}
