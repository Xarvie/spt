/* references.c — textDocument/references */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

typedef struct { cJSON *arr; const Document *d; const char *uri; } RefCtx;
static void ref_cb(void *ctx, size_t start, size_t end) {
  RefCtx *c = (RefCtx *)ctx;
  cJSON *loc = cJSON_CreateObject();
  cJSON_AddStringToObject(loc, "uri", c->uri);
  cJSON_AddItemToObject(loc, "range", lsp_range_to_json(doc_range(c->d, start, end)));
  cJSON_AddItemToArray(c->arr, loc);
}

cJSON *feature_references(const Document *d, LspPos pos, const char *uri, int include_decl) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  cJSON *arr = cJSON_CreateArray();
  RefCtx c = { arr, d, uri };
  sem_references(u, d, off, include_decl, ref_cb, &c);
  spt_lsp_unit_free(u);
  return arr;
}
