/* completion.c — textDocument/completion（关键字 + 可见符号 + 成员） */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

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

cJSON *feature_completion(const Document *d, LspPos pos) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);

  /* 成员上下文：光标前（跳过空白）是否为 '.' 或 ':' */
  int member = 0;
  {
    size_t i = off;
    while (i > 0) {
      char c = d->text[i - 1];
      if (c == ' ' || c == '\t') { i--; continue; }
      member = (c == '.' || c == ':');
      break;
    }
  }

  cJSON *arr = cJSON_CreateArray();
  CompCtx c = {arr};
  if (member) {
    sem_all_members(u, comp_cb, &c);
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
