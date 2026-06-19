/* completion.c — textDocument/completion（关键字 + 可见符号 + 成员）
**
** 成员补全（Phase 1）：若接收者是命名空间导入别名（import * as m from "mod"），
** 列出目标模块的导出符号；否则回退到全文件类成员（现有兜底）。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

#include <ctype.h>
#include <string.h>

static const char *const KEYWORDS[] = {
    "int", "float", "number", "str", "bool", "any", "void", "null", "list", "map",
    "function", "coro", "vars", "if", "else", "while", "for", "break", "continue",
    "return", "defer", "true", "false", "const", "auto", "global", "static",
    "import", "as", "from", "export", "declare", "class"};
static const int NKW = (int)(sizeof(KEYWORDS) / sizeof(KEYWORDS[0]));

static int sym_to_cik(int kind) {
  switch (kind) {
  case LSP_SK_FUNCTION: return LSP_CIK_FUNCTION;
  case LSP_SK_METHOD: return LSP_CIK_METHOD;
  case LSP_SK_CLASS: return LSP_CIK_CLASS;
  case LSP_SK_FIELD: return LSP_CIK_FIELD;
  case LSP_SK_CONSTANT: return LSP_CIK_CONSTANT;
  case LSP_SK_MODULE: return LSP_CIK_MODULE;
  default: return LSP_CIK_VARIABLE;
  }
}

typedef struct { cJSON *arr; } CompCtx;
static void comp_cb(void *ctx, const char *name, int kind, const char *detail) {
  if (!name) return;
  cJSON *it = cJSON_CreateObject();
  cJSON_AddStringToObject(it, "label", name);
  cJSON_AddNumberToObject(it, "kind", sym_to_cik(kind));
  if (detail && detail[0]) cJSON_AddStringToObject(it, "detail", detail);
  cJSON_AddItemToArray(((CompCtx *)ctx)->arr, it);
}

/* 从 dot_pos（点号字节位置）向前取接收者标识符名，写入 out。失败返回 0。 */
static int recv_name(const char *text, size_t dot_pos, char *out, size_t cap) {
  size_t i = dot_pos;
  while (i > 0 && (text[i - 1] == ' ' || text[i - 1] == '\t')) i--;
  size_t end = i;
  while (i > 0) {
    char c = text[i - 1];
    if (isalnum((unsigned char)c) || c == '_') i--;
    else break;
  }
  size_t n = end - i;
  if (n == 0 || n >= cap) return 0;
  memcpy(out, text + i, n);
  out[n] = '\0';
  return 1;
}

cJSON *feature_completion(const Document *d, LspPos pos, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);

  /* 成员上下文：光标前（跳过空白）是否为 '.' 或 ':'。dot_pos 为点号字节位置。 */
  int member = 0;
  size_t dot_pos = 0;
  {
    size_t i = off;
    while (i > 0) {
      char c = d->text[i - 1];
      if (c == ' ' || c == '\t') { i--; continue; }
      member = (c == '.' || c == ':');
      dot_pos = i - 1;
      break;
    }
  }

  cJSON *arr = cJSON_CreateArray();
  CompCtx c = {arr};
  if (member) {
    /* 命名空间导入 m. -> 列目标模块导出。 */
    int handled = 0;
    if (ws && u) {
      char rn[256];
      if (recv_name(d->text, dot_pos, rn, sizeof rn)) {
        char mp[256];
        if (sem_namespace_import_path(u, rn, mp, sizeof mp)) {
          char tgt_uri[4096];
          if (workspace_resolve_module(ws, d->uri ? d->uri : "", mp, tgt_uri, sizeof tgt_uri)) {
            char tgt_path[4096];
            spt_uri_to_path(tgt_uri, tgt_path, sizeof tgt_path);
            WsUnit wu = workspace_get_unit(ws, tgt_path);
            if (wu.unit) {
              sem_all_exports(wu.unit, comp_cb, &c);
              handled = 1;
            }
          }
        }
      }
    }
    /* 兜底：全文件类成员 + declare 模块成员。 */
    if (!handled) sem_all_members(u, comp_cb, &c);
  } else {
    sem_visible_symbols(u, d, off, comp_cb, &c);
    for (int i = 0; i < NKW; i++) {
      cJSON *it = cJSON_CreateObject();
      cJSON_AddStringToObject(it, "label", KEYWORDS[i]);
      cJSON_AddNumberToObject(it, "kind", LSP_CIK_KEYWORD);
      cJSON_AddItemToArray(arr, it);
    }
  }
  spt_lsp_unit_free(u);
  return arr;
}
