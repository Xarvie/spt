/* prepare_rename.c — textDocument/prepareRename
**
** 校验光标处是可重命名标识符：返回 {range, placeholder} 供客户端进入重命名态；
** 不可重命名（关键字/declare 外部符号/无定义）返回 null，客户端据此禁用重命名。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

cJSON *feature_prepare_rename(const Document *d, LspPos pos) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
  cJSON *res = NULL;
  if (r.found && r.has_def && !r.is_ambient) {
    res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, r.use_start, r.use_end)));
    cJSON_AddStringToObject(res, "placeholder", r.name);
  }
  spt_lsp_unit_free(u);
  return res;
}
