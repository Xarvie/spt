/* type_definition.c — textDocument/typeDefinition
**
** Phase 6a: 跳转到变量/参数类型注解对应的 class_decl。
** 复用 sem_type_definition（基于 Phase 2 的 infer_class_from_def）。
** 推导失败（无类型注解/内建类型）→ 返回 null，客户端回退到 definition。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

cJSON *feature_type_definition(const Document *d, LspPos pos) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r;
  cJSON *res = NULL;

  if (sem_type_definition(u, d, off, &r) && r.has_def) {
    res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, r.def_start, r.def_end)));
  }

  spt_lsp_unit_free(u);
  return res;
}
