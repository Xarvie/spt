/* symbols.c — textDocument/documentSymbol */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

cJSON *feature_document_symbols(const Document *d) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *arr = sem_document_symbols(u, d);
  spt_lsp_unit_free(u);
  return arr;
}
