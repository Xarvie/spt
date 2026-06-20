/* document_link.c — textDocument/documentLink
**
** Phase 6c: import 语句中的 "mod" 字符串字面量 → 可点击链接到目标文件。
** 遍历 token 数组找 `from "..."` 模式，用 workspace_resolve_module 解析目标路径。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"
#include "spt_token.h"
#include "workspace.h"

#include <string.h>

cJSON *feature_document_link(const Document *d, const char *uri, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *arr = cJSON_CreateArray();
  if (!u) return arr;

  for (int i = 0; i < u->token_count; i++) {
    if (u->tokens[i].kind != TOK_STRING_LITERAL) continue;
    /* 前一个 token 应为 TOK_FROM（import ... from "mod"）。 */
    if (i == 0 || u->tokens[i - 1].kind != TOK_FROM) continue;

    const SptToken *t = &u->tokens[i];
    /* 提取模块路径（去引号）。 */
    char mod_path[256];
    size_t tl = (size_t)t->length;
    if (tl < 2) continue; /* 至少一对引号 */
    size_t pl = tl - 2;
    if (pl >= sizeof mod_path) pl = sizeof mod_path - 1;
    memcpy(mod_path, t->lexeme + 1, pl);
    mod_path[pl] = '\0';

    /* 解析目标文件 URI。 */
    char tgt_uri[4096];
    if (!workspace_resolve_module(ws, uri, mod_path, tgt_uri, sizeof tgt_uri)) continue;

    /* 生成 DocumentLink { range, target }。 */
    int li = t->line - 1;
    if (li < 0) li = 0;
    if (li >= d->line_count) continue;
    size_t s = d->line_starts[li] + (size_t)(t->column > 0 ? t->column - 1 : 0);
    size_t e = s + (size_t)t->length;

    cJSON *link = cJSON_CreateObject();
    cJSON_AddItemToObject(link, "range", lsp_range_to_json(doc_range(d, s, e)));
    cJSON_AddStringToObject(link, "target", tgt_uri);
    cJSON_AddItemToArray(arr, link);
  }

  spt_lsp_unit_free(u);
  return arr;
}
