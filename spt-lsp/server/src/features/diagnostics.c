/*
** diagnostics.c — 容错解析 -> LSP 诊断。
*/
#include "diagnostics.h"

#include "protocol.h"
#include "spt_lsp_bridge.h"

/* 找前端坐标 (line1,col1) 处起始的 token，返回其字节长度；无则 0。 */
static int token_byte_len_at(const SptLspUnit *u, int line1, int col1) {
  for (int i = 0; i < u->token_count; i++) {
    if (u->tokens[i].line == line1 && u->tokens[i].column == col1)
      return u->tokens[i].length;
  }
  return 0;
}

cJSON *diagnostics_compute(const Document *d) {
  cJSON *arr = cJSON_CreateArray();

  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  if (u) {
    for (int i = 0; i < u->diag_count; i++) {
      LspPos start = doc_pos_from_frontend(d, u->diags[i].line, u->diags[i].column);
      size_t sb = doc_offset_at(d, start);
      int tl = token_byte_len_at(u, u->diags[i].line, u->diags[i].column);
      size_t eb = sb + (tl > 0 ? (size_t)tl : 1);
      if (eb > d->text_len)
        eb = d->text_len;
      LspPos end = doc_pos_at(d, eb);
      LspRange r = {start, end};

      cJSON *diag = cJSON_CreateObject();
      cJSON_AddItemToObject(diag, "range", lsp_range_to_json(r));
      cJSON_AddNumberToObject(diag, "severity", LSP_SEV_ERROR);
      cJSON_AddStringToObject(diag, "source", "spt");
      cJSON_AddStringToObject(diag, "message", u->diags[i].message ? u->diags[i].message : "");
      cJSON_AddItemToArray(arr, diag);
    }
    spt_lsp_unit_free(u);
  }

  cJSON *params = cJSON_CreateObject();
  cJSON_AddStringToObject(params, "uri", d->uri);
  cJSON_AddNumberToObject(params, "version", d->version);
  cJSON_AddItemToObject(params, "diagnostics", arr);
  return params;
}
