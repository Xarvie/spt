/* signature.c — textDocument/signatureHelp
**
** Phase 3: 跨文件签名帮助。若本地未找到函数，检查是否为具名导入，
** 解析目标模块并在其导出中查找函数签名。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"
#include "spt_token.h"
#include "workspace.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static size_t off_of_tok(const Document *d, const SptToken *t) {
  int l = t->line - 1;
  if (l < 0) l = 0;
  if (l >= d->line_count) return d->text_len;
  size_t o = d->line_starts[l] + (size_t)(t->column > 0 ? t->column - 1 : 0);
  return o > d->text_len ? d->text_len : o;
}

static void sappend(char *b, size_t cap, size_t *p, const char *fmt, ...) {
  if (cap == 0 || *p >= cap) return;
  va_list ap; va_start(ap, fmt);
  int n = vsnprintf(b + *p, cap - *p, fmt, ap);
  va_end(ap);
  if (n > 0) *p += (size_t)n;
  if (*p >= cap) *p = cap - 1;
}

cJSON *feature_signature_help(const Document *d, LspPos pos, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  const char *txt = d->text;

  /* 向左找深度为 0 的未匹配 '('，记录其位置，并统计深度 0 的逗号数。 */
  int depth = 0, commas = 0;
  long open = -1;
  for (long i = (long)off - 1; i >= 0; i--) {
    char c = txt[i];
    if (c == ')' || c == ']' || c == '}') depth++;
    else if (c == '[' || c == '{') { if (depth > 0) depth--; }
    else if (c == '(') {
      if (depth == 0) { open = i; break; }
      depth--;
    } else if (c == ',' && depth == 0) {
      commas++;
    }
  }
  cJSON *res = NULL;
  if (open >= 0) {
    /* '(' 左侧最近的 IDENTIFIER token 即被调用名 */
    const SptToken *name_tok = NULL;
    for (int i = 0; i < u->token_count; i++) {
      const SptToken *t = &u->tokens[i];
      if (t->kind != TOK_IDENTIFIER) continue;
      size_t e = off_of_tok(d, t) + (size_t)t->length;
      if (e <= (size_t)open) {
        if (!name_tok || e > off_of_tok(d, name_tok) + (size_t)name_tok->length)
          name_tok = t;
      }
    }
    if (name_tok) {
      char name[256];
      size_t nl = (size_t)name_tok->length;
      if (nl >= sizeof name) nl = sizeof name - 1;
      memcpy(name, name_tok->lexeme, nl); name[nl] = '\0';
      const AstNode *fn = sem_find_function(u, name);

      /* Phase 3: 本地未找到 → 尝试跨文件具名导入。 */
      if (!fn && ws) {
        char mod_path[256];
        if (sem_import_binding_path(u, name, mod_path, sizeof mod_path)) {
          char tgt_uri[4096];
          if (workspace_resolve_module(ws, d->uri ? d->uri : "", mod_path,
                                       tgt_uri, sizeof tgt_uri)) {
            char tgt_path[4096];
            spt_uri_to_path(tgt_uri, tgt_path, sizeof tgt_path);
            WsUnit wu = workspace_get_unit(ws, tgt_path);
            if (wu.unit) fn = sem_find_function(wu.unit, name);
          }
        }
      }

      if (fn) {
        char label[1024]; size_t lp = 0;
        char ret[256]; sem_type_string(fn->u.func_decl.return_type, ret, sizeof ret);
        sappend(label, sizeof label, &lp, "%s %s(", ret, name);
        cJSON *params = cJSON_CreateArray();
        const AstList *ps = &fn->u.func_decl.params;
        for (int i = 0; i < ps->count; i++) {
          AstNode *p = ps->items[i];
          char pt[256]; sem_type_string(p->u.param.type_annotation, pt, sizeof pt);
          char pl[320];
          snprintf(pl, sizeof pl, "%s %s", pt, p->u.param.name ? p->u.param.name : "_");
          sappend(label, sizeof label, &lp, "%s%s", i ? ", " : "", pl);
          cJSON *po = cJSON_CreateObject();
          cJSON_AddStringToObject(po, "label", pl);
          cJSON_AddItemToArray(params, po);
        }
        if (fn->u.func_decl.is_variadic) sappend(label, sizeof label, &lp, "%s...", ps->count ? ", " : "");
        sappend(label, sizeof label, &lp, ")");

        cJSON *sig = cJSON_CreateObject();
        cJSON_AddStringToObject(sig, "label", label);
        cJSON_AddItemToObject(sig, "parameters", params);
        if (fn->u.func_decl.doc) {
          cJSON *doc = cJSON_CreateObject();
          cJSON_AddStringToObject(doc, "kind", "markdown");
          cJSON_AddStringToObject(doc, "value", fn->u.func_decl.doc);
          cJSON_AddItemToObject(sig, "documentation", doc);
        }
        cJSON *sigs = cJSON_CreateArray();
        cJSON_AddItemToArray(sigs, sig);
        res = cJSON_CreateObject();
        cJSON_AddItemToObject(res, "signatures", sigs);
        cJSON_AddNumberToObject(res, "activeSignature", 0);
        int active = commas;
        if (ps->count > 0 && active >= ps->count) active = ps->count - 1;
        cJSON_AddNumberToObject(res, "activeParameter", active < 0 ? 0 : active);
      }
    }
  }
  spt_lsp_unit_free(u);
  return res;
}
