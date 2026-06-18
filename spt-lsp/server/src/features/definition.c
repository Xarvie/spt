/* definition.c — textDocument/definition */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

cJSON *feature_definition(const Document *d, LspPos pos, const char *uri) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
  cJSON *res = NULL;
  if (r.found && r.has_def) {
    res = cJSON_CreateObject();
    cJSON_AddStringToObject(res, "uri", uri);
    cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, r.def_start, r.def_end)));
  }
  spt_lsp_unit_free(u);
  return res;
}
