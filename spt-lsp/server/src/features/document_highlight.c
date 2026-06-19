/* document_highlight.c — textDocument/documentHighlight
**
** 复用 sem_references（includeDeclaration=1）收集同指代标识符的所有出现，
** 返回 DocumentHighlight[]。v1 全返 Text（kind=1），不区分读写。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

typedef struct { cJSON *arr; const Document *d; } HlCtx;
static void hl_cb(void *ctx, size_t start, size_t end) {
  HlCtx *c = (HlCtx *)ctx;
  cJSON *it = cJSON_CreateObject();
  cJSON_AddItemToObject(it, "range", lsp_range_to_json(doc_range(c->d, start, end)));
  cJSON_AddNumberToObject(it, "kind", 1); /* DocumentHighlightKind.Text */
  cJSON_AddItemToArray(c->arr, it);
}

cJSON *feature_document_highlight(const Document *d, LspPos pos) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  cJSON *arr = cJSON_CreateArray();
  HlCtx c = {arr, d};
  sem_references(u, d, off, 1, hl_cb, &c);
  spt_lsp_unit_free(u);
  return arr;
}
