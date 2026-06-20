/* references.c — textDocument/references
**
** 单文件：sem_references 给出当前文件内的引用。
** 跨文件（Phase 3e）：若符号是导出定义或导入绑定，
** 扫描工作区中导入该符号的文件，收集所有匹配 token 的引用。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"
#include "workspace.h"

#include <string.h>

typedef struct { cJSON *arr; const Document *d; const char *uri; } RefCtx;
static void ref_cb(void *ctx, size_t start, size_t end) {
  RefCtx *c = (RefCtx *)ctx;
  cJSON *loc = cJSON_CreateObject();
  cJSON_AddStringToObject(loc, "uri", c->uri);
  cJSON_AddItemToObject(loc, "range", lsp_range_to_json(doc_range(c->d, start, end)));
  cJSON_AddItemToArray(c->arr, loc);
}

/* 在 wu 文件中扫描所有匹配 name 的标识符 token，写入 arr。 */
static void collect_token_refs(const WsUnit *wu, const char *name, const char *uri,
                               cJSON *arr) {
  if (!wu->unit || !wu->doc) return;
  size_t nl = strlen(name);
  for (int ti = 0; ti < wu->unit->token_count; ti++) {
    const SptToken *t = &wu->unit->tokens[ti];
    if (t->kind != TOK_IDENTIFIER) continue;
    if ((size_t)t->length != nl || memcmp(t->lexeme, name, nl) != 0) continue;
    int li = t->line - 1;
    if (li < 0) li = 0;
    if (li >= wu->doc->line_count) continue;
    size_t s = wu->doc->line_starts[li] + (size_t)(t->column > 0 ? t->column - 1 : 0);
    size_t e = s + (size_t)t->length;
    cJSON *loc = cJSON_CreateObject();
    cJSON_AddStringToObject(loc, "uri", uri);
    cJSON_AddItemToObject(loc, "range", lsp_range_to_json(doc_range(wu->doc, s, e)));
    cJSON_AddItemToArray(arr, loc);
  }
}

cJSON *feature_references(const Document *d, LspPos pos, const char *uri, int include_decl,
                          Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  cJSON *arr = cJSON_CreateArray();
  RefCtx c = { arr, d, uri };
  sem_references(u, d, off, include_decl, ref_cb, &c);

  /* 跨文件引用：扫描工作区中导入该符号的文件。 */
  SemRef r = sem_resolve(u, d, off);
  if (r.found && !r.is_member && !r.is_ambient && ws) {
    char def_mod_path[256] = {0};
    char def_uri[4096] = {0};
    int is_imported = sem_import_binding_path(u, r.name, def_mod_path, sizeof def_mod_path);

    /* 确定定义文件 URI 和 target_uri。 */
    if (is_imported) {
      /* 情况 B: 导入符号 — 定义在目标模块。 */
      workspace_resolve_module(ws, uri, def_mod_path, def_uri, sizeof def_uri);
    } else if (r.has_def) {
      /* 情况 A: 本地定义 — 定义在当前文件。 */
      strncpy(def_uri, uri, sizeof def_uri - 1);
    }

    if (def_uri[0]) {
      const char *target_uri = def_uri;

      /* 在定义文件中收集引用（情况 B：当前文件不是定义文件）。 */
      if (is_imported && strcmp(def_uri, uri) != 0) {
        char def_path[4096];
        spt_uri_to_path(def_uri, def_path, sizeof def_path);
        WsUnit def_wu = workspace_get_unit(ws, def_path);
        if (def_wu.unit && def_wu.doc)
          collect_token_refs(&def_wu, r.name, def_uri, arr);
      }

      /* 扫描工作区中所有文件，找出导入此符号的文件。 */
      workspace_symbols(ws, ""); /* 触发懒构建 */
      for (int i = 0; i < ws->sym_count; i++) {
        const char *other_uri = ws->syms[i].uri;
        if (!other_uri) continue;
        /* 跳过当前文件（已处理）和定义文件（已处理）。 */
        if (strcmp(other_uri, uri) == 0) continue;
        if (def_uri[0] && strcmp(other_uri, def_uri) == 0) continue;

        char other_path[4096];
        spt_uri_to_path(other_uri, other_path, sizeof other_path);
        WsUnit wu = workspace_get_unit(ws, other_path);
        if (!wu.unit || !wu.doc) continue;

        /* 检查该文件是否导入了此符号。 */
        char mod_path[256];
        int imported = sem_import_binding_path(wu.unit, r.name, mod_path, sizeof mod_path);
        if (!imported)
          imported = sem_namespace_import_path(wu.unit, r.name, mod_path, sizeof mod_path);
        if (!imported) continue;

        /* 验证导入目标模块解析后等于 target_uri。 */
        char tgt_uri[4096];
        if (!workspace_resolve_module(ws, other_uri, mod_path, tgt_uri, sizeof tgt_uri)) continue;
        if (strcmp(tgt_uri, target_uri) != 0) continue;

        collect_token_refs(&wu, r.name, other_uri, arr);
      }
    }
  }

  spt_lsp_unit_free(u);
  return arr;
}
