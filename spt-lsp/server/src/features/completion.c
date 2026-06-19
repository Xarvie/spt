/* completion.c — textDocument/completion（关键字 + 可见符号 + 成员）
**
** 成员补全（Phase 1）：若接收者是命名空间导入别名（import * as m from "mod"），
** 列出目标模块的导出符号；否则回退到全文件类成员（现有兜底）。
**
** Phase 4: snippet 增强——函数符号生成参数占位符调用模板，
** 关键字（class/declare/function/if/while/for/import）生成结构化模板。
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

/* ---- Phase 4: snippet 生成 ---- */

/* 从函数 detail（如 "int add(int a, int b)"）提取参数名，生成调用 snippet。
   输出如 "add(${1:a}, ${2:b})"。失败时返回 0（调用方退化为纯 label）。 */
static int func_call_snippet(const char *name, const char *detail, char *out, size_t cap) {
  if (!detail) return 0;
  /* 找到 '(' */
  const char *lp = strchr(detail, '(');
  if (!lp) return 0;
  /* 提取括号内参数列表 */
  const char *rp = strrchr(lp, ')');
  if (!rp || rp == lp + 1) {
    /* 无参数 */
    snprintf(out, cap, "%s()$0", name);
    return 1;
  }
  /* 解析参数：按 ',' 分割，每个参数形如 "type name"，取最后的标识符为参数名 */
  size_t pos = 0;
  snprintf(out + pos, cap - pos, "%s(", name);
  pos = strlen(out);

  int idx = 1;
  const char *p = lp + 1;
  while (p < rp) {
    /* 找下一个 ',' 或 ')' */
    const char *comma = strchr(p, ',');
    const char *end = comma ? comma : rp;
    /* 跳过前导空白 */
    while (p < end && (*p == ' ' || *p == '\t')) p++;
    /* 跳过 "..." 变参 */
    if (end - p == 3 && strncmp(p, "...", 3) == 0) break;
    /* 取最后的标识符部分作为参数名 */
    const char *pname_end = end;
    while (pname_end > p && (pname_end[-1] == ' ' || pname_end[-1] == '\t')) pname_end--;
    const char *pname = pname_end;
    while (pname > p && (isalnum((unsigned char)pname[-1]) || pname[-1] == '_')) pname--;
    if (pname == pname_end || pname == p) {
      /* 无参数名，用占位符 */
      snprintf(out + pos, cap - pos, "%s${%d:arg%d}", idx > 1 ? ", " : "", idx, idx);
    } else {
      char pname_buf[128];
      size_t pnl = (size_t)(pname_end - pname);
      if (pnl >= sizeof pname_buf) pnl = sizeof pname_buf - 1;
      memcpy(pname_buf, pname, pnl);
      pname_buf[pnl] = '\0';
      snprintf(out + pos, cap - pos, "%s${%d:%s}", idx > 1 ? ", " : "", idx, pname_buf);
    }
    pos = strlen(out);
    idx++;
    p = end + 1; /* 跳过 ',' */
  }
  snprintf(out + pos, cap - pos, ")$0");
  return 1;
}

/* 关键字 snippet 模板。返回非 NULL 时该关键字有 snippet。 */
static const char *keyword_snippet(const char *kw) {
  if (strcmp(kw, "class") == 0)
    return "class ${1:Name} {\n  $0\n}";
  if (strcmp(kw, "declare") == 0)
    return "declare from \"${1:module}\" {\n  $0\n}";
  if (strcmp(kw, "function") == 0)
    return "function ${1:name}(${2:params}) {\n  $0\n}";
  if (strcmp(kw, "if") == 0)
    return "if (${1:condition}) {\n  $0\n}";
  if (strcmp(kw, "while") == 0)
    return "while (${1:condition}) {\n  $0\n}";
  if (strcmp(kw, "for") == 0)
    return "for ${1:i} in ${2:iterable} {\n  $0\n}";
  if (strcmp(kw, "import") == 0)
    return "import { ${1:name} } from \"${2:module}\";$0";
  if (strcmp(kw, "return") == 0)
    return "return $0;";
  if (strcmp(kw, "defer") == 0)
    return "defer {\n  $0\n}";
  return NULL;
}

typedef struct { cJSON *arr; } CompCtx;
static void comp_cb(void *ctx, const char *name, int kind, const char *detail) {
  if (!name) return;
  cJSON *it = cJSON_CreateObject();
  cJSON_AddStringToObject(it, "label", name);
  int cik = sym_to_cik(kind);
  cJSON_AddNumberToObject(it, "kind", cik);
  if (detail && detail[0]) cJSON_AddStringToObject(it, "detail", detail);

  /* Phase 4: 函数/方法生成调用 snippet。 */
  if (cik == LSP_CIK_FUNCTION || cik == LSP_CIK_METHOD) {
    char snip[512];
    if (func_call_snippet(name, detail, snip, sizeof snip)) {
      cJSON_AddStringToObject(it, "insertText", snip);
      cJSON_AddNumberToObject(it, "insertTextFormat", 2); /* Snippet */
    }
  }
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
    /* Phase 2: 接收者类型推断 → 只列该类成员。 */
    if (!handled && u) {
      char rn[256];
      if (recv_name(d->text, dot_pos, rn, sizeof rn))
        handled = sem_members_of_receiver(u, d, rn, dot_pos, comp_cb, &c);
    }
    /* 兜底：全文件类成员 + declare 模块成员。 */
    if (!handled) sem_all_members(u, comp_cb, &c);
  } else {
    sem_visible_symbols(u, d, off, comp_cb, &c);
    for (int i = 0; i < NKW; i++) {
      cJSON *it = cJSON_CreateObject();
      cJSON_AddStringToObject(it, "label", KEYWORDS[i]);
      cJSON_AddNumberToObject(it, "kind", LSP_CIK_KEYWORD);
      /* Phase 4: 关键字 snippet 模板。 */
      const char *snip = keyword_snippet(KEYWORDS[i]);
      if (snip) {
        cJSON_AddStringToObject(it, "insertText", snip);
        cJSON_AddNumberToObject(it, "insertTextFormat", 2); /* Snippet */
      }
      cJSON_AddItemToArray(arr, it);
    }
  }
  spt_lsp_unit_free(u);
  return arr;
}
