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

#include <stdlib.h>
#include <string.h>

typedef struct { cJSON *edits; const Document *d; const char *new_name; } RenCtx;
static void ren_cb(void *ctx, size_t s, size_t e) {
  RenCtx *c = (RenCtx *)ctx;
  cJSON *ed = cJSON_CreateObject();
  cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(c->d, s, e)));
  cJSON_AddStringToObject(ed, "newText", c->new_name);
  cJSON_AddItemToArray(c->edits, ed);
}

/* 在目标文件中查找所有匹配 name 的标识符 token，产出 TextEdit。
   Phase 5b: 若 ws 的引用倒排索引可用，直接查表（O(1)），否则回退到线性扫描。 */
typedef struct { cJSON *edits; const Document *d; const char *new_name; const char *name; } CrossCtx;
static void cross_ref_cb(void *ctx, size_t s, size_t e) {
  CrossCtx *c = (CrossCtx *)ctx;
  cJSON *ed = cJSON_CreateObject();
  cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(c->d, s, e)));
  cJSON_AddStringToObject(ed, "newText", c->new_name);
  cJSON_AddItemToArray(c->edits, ed);
}

/* Phase 5b: 由倒排索引直接产出 TextEdit（offset/length 已知，无需再扫 token）。 */
typedef struct { cJSON *edits; const Document *d; const char *new_name; } IdxCtx;
static void idx_occ_cb(void *ctx, const char *uri, size_t offset, int length) {
  (void)uri;
  IdxCtx *c = (IdxCtx *)ctx;
  if (!c->d) return;
  cJSON *ed = cJSON_CreateObject();
  cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(c->d, offset, offset + (size_t)length)));
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

/* Phase 5b/5c: 候选 URI 集合（用于收集导入者 / 包含名字的文件）。 */
typedef struct { char (*uris)[4096]; int count, cap; } UriSet;
static void uriset_init(UriSet *s, int cap) { s->uris = (char(*)[4096])calloc(cap, 4096); s->count = 0; s->cap = cap; }
static void uriset_free(UriSet *s) { free(s->uris); s->uris = NULL; s->count = s->cap = 0; }
static void uriset_add(UriSet *s, const char *uri) {
  for (int i = 0; i < s->count; i++) if (strcmp(s->uris[i], uri) == 0) return;
  if (s->count < s->cap) { strncpy(s->uris[s->count], uri, 4095); s->uris[s->count][4095] = '\0'; s->count++; }
}
static void importer_cb(void *ctx, const char *importer_uri) { uriset_add((UriSet *)ctx, importer_uri); }
static void occ_uri_cb(void *ctx, const char *uri, size_t off, int len) { (void)off; (void)len; uriset_add((UriSet *)ctx, uri); }

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
         - 情况 B: 导入目标模块是定义文件（def_uri）。
         Phase 5b/5c: 优先用倒排索引/依赖图缩小候选集，回退到全量扫描。 */
      const char *target_uri = def_uri[0] ? def_uri : uri;

      /* 收集候选 URI。
         - 情况 B: 用依赖图查 def_mod_path 的导入者（5c）。
         - 情况 A: 用引用倒排索引查包含 r.name 的文件（5b）。 */
      UriSet cand;
      uriset_init(&cand, 256);
      int got = 0;
      if (def_uri[0]) {
        got = workspace_find_importers(ws, def_mod_path, importer_cb, &cand);
      } else {
        got = workspace_find_occurrences(ws, r.name, occ_uri_cb, &cand);
      }

      /* 索引未命中（脏或空）→ 回退到全量 sym 扫描。 */
      if (got == 0) {
        for (int i = 0; i < ws->sym_count; i++) {
          const char *other_uri = ws->syms[i].uri;
          if (!other_uri) continue;
          uriset_add(&cand, other_uri);
        }
      }

      for (int ci = 0; ci < cand.count; ci++) {
        const char *other_uri = cand.uris[ci];
        /* 跳过当前文件（已处理）和定义文件（情况 B 已处理）。 */
        if (strcmp(other_uri, uri) == 0) continue;
        if (def_uri[0] && strcmp(other_uri, def_uri) == 0) continue;

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
      uriset_free(&cand);
    }

    res = cJSON_CreateObject();
    cJSON_AddItemToObject(res, "changes", changes);
  }
  spt_lsp_unit_free(u);
  return res;
}
