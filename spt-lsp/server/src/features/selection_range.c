/* selection_range.c — textDocument/selectionRange
**
** 返回光标处标识符的区间（最内层 SelectionRange）。编辑器据此做"双击选词"式的
** 层级选区。v1 只返回标识符 token 区间；后续可叠加语句/函数/类外层区间。
*/
#include "lsp_features.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"

cJSON *feature_selection_range(const Document *d, LspPos pos) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  cJSON *res = NULL;
  if (u) {
    for (int i = 0; i < u->token_count; i++) {
      const SptToken *t = &u->tokens[i];
      if (t->kind != TOK_IDENTIFIER) continue;
      int l = t->line - 1;
      if (l < 0) l = 0;
      if (l >= d->line_count) continue;
      size_t s = d->line_starts[l] + (size_t)(t->column > 0 ? t->column - 1 : 0);
      size_t e = s + (size_t)t->length;
      if (off >= s && off <= e) {
        res = cJSON_CreateObject();
        cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, s, e)));
        break;
      }
    }
    spt_lsp_unit_free(u);
  }
  if (!res) {
    /* 光标不在标识符上：返回单字符区间（合法降级）。 */
    size_t s = off;
    if (s >= d->text_len) s = d->text_len > 0 ? d->text_len - 1 : 0;
    res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "range", lsp_range_to_json(doc_range(d, s, s)));
  }
  return res;
}
