/* rename.c — textDocument/rename -> WorkspaceEdit */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

typedef struct { cJSON *edits; const Document *d; const char *new_name; } RenCtx;
static void ren_cb(void *ctx, size_t s, size_t e) {
  RenCtx *c = (RenCtx *)ctx;
  cJSON *ed = cJSON_CreateObject();
  cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(c->d, s, e)));
  cJSON_AddStringToObject(ed, "newText", c->new_name);
  cJSON_AddItemToArray(c->edits, ed);
}

cJSON *feature_rename(const Document *d, LspPos pos, const char *uri, const char *new_name) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
  cJSON *res = NULL;
  if (r.found) {
    cJSON *edits = cJSON_CreateArray();
    RenCtx c = {edits, d, new_name};
    sem_references(u, d, off, 1, ren_cb, &c);
    res = cJSON_CreateObject();
    cJSON *changes = cJSON_CreateObject();
    cJSON_AddItemToObject(changes, uri, edits);
    cJSON_AddItemToObject(res, "changes", changes);
  }
  spt_lsp_unit_free(u);
  return res;
}
