/* hover.c — textDocument/hover */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

#include <stdio.h>
#include <string.h>

cJSON *feature_hover(const Document *d, LspPos pos) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
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
