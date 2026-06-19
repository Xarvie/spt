/* rename.c — textDocument/rename -> WorkspaceEdit
**
** Phase 3: 工作区重命名。支持两种跨文件场景：
**   1. 重命名本地定义的导出符号：扫描工作区中导入该符号的文件。
**   2. 重命名导入的符号：解析目标模块，在定义文件中改名，并扫描其他导入者。
** 局部变量仅限当前文件（不会被 import）。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"
#include "workspace.h"

#include <string.h>

typedef struct { cJSON *edits; const Document *d; const char *new_name; } RenCtx;
static void ren_cb(void *ctx, size_t s, size_t e) {
  RenCtx *c = (RenCtx *)ctx;
  cJSON *ed = cJSON_CreateObject();
  cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(c->d, s, e)));
  cJSON_AddStringToObject(ed, "newText", c->new_name);
  cJSON_AddItemToArray(c->edits, ed);
}

/* 在目标文件中查找所有匹配 name 的标识符 token，产出 TextEdit。 */
typedef struct { cJSON *edits; const Document *d; const char *new_name; const char *name; } CrossCtx;
static void cross_ref_cb(void *ctx, size_t s, size_t e) {
  CrossCtx *c = (CrossCtx *)ctx;
  cJSON *ed = cJSON_CreateObject();
  cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(c->d, s, e)));
  cJSON_AddStringToObject(ed, "newText", c->new_name);
  cJSON_AddItemToArray(c->edits, ed);
}

/* 在 wu 文件中扫描所有匹配 name 的标识符 token，写入 cross_edits。
   返回写入的编辑数。 */
static int collect_token_edits(const WsUnit *wu, const char *name, const char *new_name,
                               cJSON *cross_edits) {
  if (!wu->unit || !wu->doc) return 0;
  CrossCtx cc = {cross_edits, wu->doc, new_name, name};
  size_t nl = strlen(name);
  int n = 0;
  for (int ti = 0; ti < wu->unit->token_count; ti++) {
    const SptToken *t = &wu->unit->tokens[ti];
    if (t->kind != TOK_IDENTIFIER) continue;
    if ((size_t)t->length != nl || memcmp(t->lexeme, name, nl) != 0) continue;
    size_t s = 0, e = 0;
    int li = t->line - 1;
    if (li < 0) li = 0;
    if (li >= wu->doc->line_count) continue;
    s = wu->doc->line_starts[li] + (size_t)(t->column > 0 ? t->column - 1 : 0);
    e = s + (size_t)t->length;
    cross_ref_cb(&cc, s, e);
    n++;
  }
  return n;
}

cJSON *feature_rename(const Document *d, LspPos pos, const char *uri, const char *new_name,
                      Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
  cJSON *res = NULL;
  if (r.found) {
    cJSON *changes = cJSON_CreateObject();

    /* 当前文件的编辑。 */
    cJSON *edits = cJSON_CreateArray();
    RenCtx c = {edits, d, new_name};
    sem_references(u, d, off, 1, ren_cb, &c);
    cJSON_AddItemToObject(changes, uri, edits);

    /* Phase 3: 跨文件重命名。
       情况 A: 本地定义的导出符号（r.has_def=1 且非导入）→ 扫描导入者。
       情况 B: 导入的符号（sem_import_binding_path 命中）→
               在定义文件中改名 + 扫描其他导入者。
       注意：sem_resolve 会把 import 绑定也当作 has_def=1 的定义，
       所以必须先检查 sem_import_binding_path 来区分两种情况。 */
    int do_cross = 0;
    char def_mod_path[256] = {0};  /* 定义所在模块路径（情况 B 用） */
    char def_uri[4096] = {0};      /* 定义文件 URI（情况 B 用） */

    if (ws && !r.is_member && !r.is_ambient) {
      /* 先检查是否为导入符号（情况 B）。 */
      if (sem_import_binding_path(u, r.name, def_mod_path, sizeof def_mod_path)) {
        /* 导入符号：解析目标模块 URI。 */
        if (workspace_resolve_module(ws, uri, def_mod_path, def_uri, sizeof def_uri)) {
          do_cross = 1;
        }
      } else if (r.has_def) {
        /* 情况 A: 本地定义（非导入）。 */
        do_cross = 1;
      }
    }

    if (do_cross && ws) {
      workspace_symbols(ws, ""); /* 触发懒构建 */

      /* 情况 B: 先在定义文件中改名（定义 + 所有引用）。 */
      if (def_uri[0] != '\0' && strcmp(def_uri, uri) != 0) {
        char def_path[4096];
        spt_uri_to_path(def_uri, def_path, sizeof def_path);
        WsUnit def_wu = workspace_get_unit(ws, def_path);
        if (def_wu.unit && def_wu.doc) {
          cJSON *cross_edits = cJSON_CreateArray();
          collect_token_edits(&def_wu, r.name, new_name, cross_edits);
          if (cJSON_GetArraySize(cross_edits) > 0)
            cJSON_AddItemToObject(changes, def_uri, cross_edits);
          else
            cJSON_Delete(cross_edits);
        }
      }

      /* 扫描工作区中所有文件，找出导入该符号的文件并改名。
         - 情况 A: 导入目标模块是当前文件（uri）。
         - 情况 B: 导入目标模块是定义文件（def_uri）。 */
      const char *target_uri = def_uri[0] ? def_uri : uri;
      const char *target_mod = def_uri[0] ? def_mod_path : "";
      (void)target_mod;

      for (int i = 0; i < ws->sym_count; i++) {
        const char *other_uri = ws->syms[i].uri;
        if (!other_uri) continue;
        /* 跳过当前文件（已处理）和定义文件（情况 B 已处理）。 */
        if (strcmp(other_uri, uri) == 0) continue;
        if (def_uri[0] && strcmp(other_uri, def_uri) == 0) continue;
        /* 去重。 */
        int dup = 0;
        for (int j = 0; j < i; j++) {
          if (ws->syms[j].uri && strcmp(ws->syms[j].uri, other_uri) == 0) { dup = 1; break; }
        }
        if (dup) continue;

        char other_path[4096];
        spt_uri_to_path(other_uri, other_path, sizeof other_path);
        WsUnit wu = workspace_get_unit(ws, other_path);
        if (!wu.unit || !wu.doc) continue;

        /* 检查该文件是否导入了此符号，且导入目标匹配。 */
        char mod_path[256];
        int imported = sem_import_binding_path(wu.unit, r.name, mod_path, sizeof mod_path);
        if (!imported)
          imported = sem_namespace_import_path(wu.unit, r.name, mod_path, sizeof mod_path);
        if (!imported) continue;

        /* 验证导入目标模块解析后等于 target_uri。 */
        char tgt_uri[4096];
        if (!workspace_resolve_module(ws, other_uri, mod_path, tgt_uri, sizeof tgt_uri)) continue;
        if (strcmp(tgt_uri, target_uri) != 0) continue;

        cJSON *cross_edits = cJSON_CreateArray();
        collect_token_edits(&wu, r.name, new_name, cross_edits);
        if (cJSON_GetArraySize(cross_edits) > 0)
          cJSON_AddItemToObject(changes, other_uri, cross_edits);
        else
          cJSON_Delete(cross_edits);
      }
    }

    res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "changes", changes);
  }
  spt_lsp_unit_free(u);
  return res;
}
