/* call_hierarchy.c — textDocument/prepareCallHierarchy + incomingCalls + outgoingCalls
**
** Phase 6d: 调用层级导航。
** - prepare：光标处函数 → CallHierarchyItem
** - outgoing：遍历函数体找所有调用 → CallHierarchyOutgoingCall[]
** - incoming：用倒排索引查"谁调用了此函数" → CallHierarchyIncomingCall[]
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"
#include "spt_token.h"
#include "workspace.h"

#include <string.h>

/* 从函数声明节点构建 CallHierarchyItem（附加到 arr 或作为单个返回）。 */
static cJSON *make_call_item(const SptLspUnit *u, const Document *d, const char *uri,
                             const AstNode *fn) {
  if (!fn || fn->type != NODE_FUNCTION_DECL || !fn->u.func_decl.name) return NULL;
  const char *nm = fn->u.func_decl.name;

  /* selectionRange：函数名范围。 */
  size_t fn_start = 0, fn_end = 0;
  size_t decl_off = 0;
  /* 用 token 精确定位函数名。 */
  for (int i = 0; i < u->token_count; i++) {
    if (u->tokens[i].kind != TOK_IDENTIFIER) continue;
    if ((size_t)u->tokens[i].length != strlen(nm)) continue;
    if (memcmp(u->tokens[i].lexeme, nm, strlen(nm)) != 0) continue;
    int li = u->tokens[i].line - 1;
    if (li < 0) li = 0;
    if (li >= d->line_count) continue;
    size_t s = d->line_starts[li] + (size_t)(u->tokens[i].column > 0 ? u->tokens[i].column - 1 : 0);
    /* 确认是函数声明处的名字（在 fn->loc 附近）。 */
    size_t decl_s = 0;
    if (fn->u.func_decl.body && fn->u.func_decl.body->type == NODE_BLOCK) {
      decl_s = d->line_starts[fn->loc.line - 1 < d->line_count ? fn->loc.line - 1 : 0] +
               (size_t)(fn->loc.column > 0 ? fn->loc.column - 1 : 0);
    }
    /* 名字 token 应在声明起始之后、body 之前。 */
    if (s >= decl_s) {
      fn_start = s;
      fn_end = s + (size_t)u->tokens[i].length;
      decl_off = decl_s;
      break;
    }
  }

  /* range：函数整体范围（声明起始 → body 结束）。 */
  size_t range_end = fn_end;
  if (fn->u.func_decl.body && fn->u.func_decl.body->type == NODE_BLOCK &&
      fn->u.func_decl.body->u.block.use_end) {
    int eli = fn->u.func_decl.body->u.block.end_loc.line - 1;
    if (eli >= 0 && eli < d->line_count) {
      range_end = d->line_starts[eli] +
                  (size_t)(fn->u.func_decl.body->u.block.end_loc.column > 0
                               ? fn->u.func_decl.body->u.block.end_loc.column
                               : 0);
    }
  }

  cJSON *item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "name", nm);
  cJSON_AddNumberToObject(item, "kind", LSP_SK_FUNCTION);
  cJSON_AddStringToObject(item, "uri", uri);
  cJSON_AddItemToObject(item, "range",
                        lsp_range_to_json(doc_range(d, decl_off, range_end)));
  cJSON_AddItemToObject(item, "selectionRange",
                        lsp_range_to_json(doc_range(d, fn_start, fn_end)));
  return item;
}

/* ---- prepareCallHierarchy ---- */
cJSON *feature_prepare_call_hierarchy(const Document *d, LspPos pos, const char *uri) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *arr = cJSON_CreateArray();
  if (!u) return arr;

  size_t off = doc_offset_at(d, pos);
  SemRef r = sem_resolve(u, d, off);
  if (r.found && r.has_def && r.kind == LSP_SK_FUNCTION) {
    /* 找到函数定义节点。 */
    const AstNode *fn = sem_find_function(u, r.name);
    if (fn) {
      cJSON *item = make_call_item(u, d, uri, fn);
      if (item) cJSON_AddItemToArray(arr, item);
    }
  }

  spt_lsp_unit_free(u);
  return arr;
}

/* ---- outgoingCalls ---- */
typedef struct {
  cJSON *arr;
  const SptLspUnit *u;
  const Document *d;
  const char *uri;
  Workspace *ws;
} OutCtx;

/* 从 WsSymbol 构建 CallHierarchyItem（跨文件路径，无 AstNode）。 */
static cJSON *make_call_item_from_symbol(const WsSymbol *sym) {
  cJSON *item = cJSON_CreateObject();
  cJSON_AddStringToObject(item, "name", sym->name);
  cJSON_AddNumberToObject(item, "kind", sym->kind);
  cJSON_AddStringToObject(item, "uri", sym->uri);
  cJSON *rng = cJSON_CreateObject();
  cJSON *start = cJSON_CreateObject();
  cJSON_AddNumberToObject(start, "line", sym->range.start.line);
  cJSON_AddNumberToObject(start, "character", sym->range.start.character);
  cJSON *end = cJSON_CreateObject();
  cJSON_AddNumberToObject(end, "line", sym->range.end.line);
  cJSON_AddNumberToObject(end, "character", sym->range.end.character);
  cJSON_AddItemToObject(rng, "start", start);
  cJSON_AddItemToObject(rng, "end", end);
  cJSON_AddItemToObject(item, "range", rng);
  /* selectionRange 与 range 相同（符号位置）。 */
  cJSON *sr = cJSON_CreateObject();
  cJSON *s2 = cJSON_CreateObject();
  cJSON_AddNumberToObject(s2, "line", sym->range.start.line);
  cJSON_AddNumberToObject(s2, "character", sym->range.start.character);
  cJSON *e2 = cJSON_CreateObject();
  cJSON_AddNumberToObject(e2, "line", sym->range.end.line);
  cJSON_AddNumberToObject(e2, "character", sym->range.end.character);
  cJSON_AddItemToObject(sr, "start", s2);
  cJSON_AddItemToObject(sr, "end", e2);
  cJSON_AddItemToObject(item, "selectionRange", sr);
  return item;
}

static void outgoing_cb(void *ctx, const char *callee, size_t offset, int length) {
  OutCtx *c = (OutCtx *)ctx;
  (void)offset;
  (void)length;
  cJSON *to_item = NULL;

  /* 优先在当前文档查找被调用函数定义。 */
  const AstNode *target_fn = sem_find_function(c->u, callee);
  if (target_fn) {
    to_item = make_call_item(c->u, c->d, c->uri, target_fn);
  } else if (c->ws) {
    /* 跨文件查找：用 workspace_symbols 索引。 */
    cJSON *syms = workspace_symbols(c->ws, callee);
    if (syms && cJSON_IsArray(syms)) {
      int n = cJSON_GetArraySize(syms);
      for (int i = 0; i < n && !to_item; i++) {
        cJSON *s = cJSON_GetArrayItem(syms, i);
        cJSON *name = cJSON_GetObjectItemCaseSensitive(s, "name");
        cJSON *kind = cJSON_GetObjectItemCaseSensitive(s, "kind");
        if (!name || !cJSON_IsString(name)) continue;
        if (strcmp(name->valuestring, callee) != 0) continue;
        /* 只接受函数类型（5=Function, 12=Function）。 */
        int k = (kind && cJSON_IsNumber(kind)) ? kind->valueint : 0;
        if (k != LSP_SK_FUNCTION) continue;
        cJSON *loc = cJSON_GetObjectItemCaseSensitive(s, "location");
        if (!loc) continue;
        cJSON *uri = cJSON_GetObjectItemCaseSensitive(loc, "uri");
        cJSON *rng = cJSON_GetObjectItemCaseSensitive(loc, "range");
        if (!uri || !cJSON_IsString(uri) || !rng) continue;
        WsSymbol wsym;
        wsym.name = (char *)callee;
        wsym.kind = k;
        wsym.uri = uri->valuestring;
        cJSON *st = cJSON_GetObjectItemCaseSensitive(rng, "start");
        cJSON *en = cJSON_GetObjectItemCaseSensitive(rng, "end");
        wsym.range.start.line = (st && cJSON_GetObjectItemCaseSensitive(st, "line")) ? cJSON_GetObjectItemCaseSensitive(st, "line")->valueint : 0;
        wsym.range.start.character = (st && cJSON_GetObjectItemCaseSensitive(st, "character")) ? cJSON_GetObjectItemCaseSensitive(st, "character")->valueint : 0;
        wsym.range.end.line = (en && cJSON_GetObjectItemCaseSensitive(en, "line")) ? cJSON_GetObjectItemCaseSensitive(en, "line")->valueint : 0;
        wsym.range.end.character = (en && cJSON_GetObjectItemCaseSensitive(en, "character")) ? cJSON_GetObjectItemCaseSensitive(en, "character")->valueint : 0;
        wsym.container = NULL;
        to_item = make_call_item_from_symbol(&wsym);
      }
    }
    if (syms) cJSON_Delete(syms);
  }
  if (!to_item) return;

  cJSON *call = cJSON_CreateObject();
  cJSON_AddItemToObject(call, "to", to_item);
  /* fromRanges：调用位置（简化为空数组，因为 sem_outgoing_calls 未传位置）。 */
  cJSON_AddItemToObject(call, "fromRanges", cJSON_CreateArray());
  cJSON_AddItemToArray(c->arr, call);
}

cJSON *feature_call_hierarchy_outgoing(const Document *d, const char *fn_name, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *arr = cJSON_CreateArray();
  if (!u) return arr;

  const AstNode *fn = sem_find_function(u, fn_name);
  if (fn) {
    OutCtx c = { arr, u, d, "", ws };
    sem_outgoing_calls(u, fn, outgoing_cb, &c);
  }

  spt_lsp_unit_free(u);
  return arr;
}

/* ---- incomingCalls ---- */
typedef struct {
  Workspace *ws;
  const char *target_name;  /* 被调用的函数名 */
  cJSON *arr;
} InCtx;

/* 对每个出现，解析所在文件，找 enclosing function。 */
static void incoming_occ_cb(void *ctx, const char *uri, size_t offset, int length) {
  InCtx *c = (InCtx *)ctx;
  (void)length;

  char path[4096];
  spt_uri_to_path(uri, path, sizeof path);
  WsUnit wu = workspace_get_unit(c->ws, path);
  if (!wu.unit || !wu.doc) return;

  /* 检查是否是函数调用（后跟 `(`）。 */
  /* 简化：检查 offset 处 token 后面是否是 `(`。 */
  int is_call = 0;
  for (int ti = 0; ti < wu.unit->token_count; ti++) {
    if (wu.unit->tokens[ti].kind != TOK_IDENTIFIER) continue;
    int li = wu.unit->tokens[ti].line - 1;
    if (li < 0 || li >= wu.doc->line_count) continue;
    size_t s = wu.doc->line_starts[li] +
               (size_t)(wu.unit->tokens[ti].column > 0 ? wu.unit->tokens[ti].column - 1 : 0);
    if (s != offset) continue;
    /* 检查下一个 token 是否是 `(`。 */
    if (ti + 1 < wu.unit->token_count && wu.unit->tokens[ti + 1].kind == TOK_LPAREN)
      is_call = 1;
    break;
  }
  if (!is_call) return;

  /* 找包含此调用的函数。 */
  const AstNode *caller_fn = sem_enclosing_function(wu.unit, wu.doc, offset);
  if (!caller_fn || caller_fn->type != NODE_FUNCTION_DECL || !caller_fn->u.func_decl.name)
    return;

  /* 构建 CallHierarchyItem（调用者）。 */
  cJSON *from_item = make_call_item(wu.unit, wu.doc, uri, caller_fn);
  if (!from_item) return;

  cJSON *call = cJSON_CreateObject();
  cJSON_AddItemToObject(call, "from", from_item);
  /* fromRanges：调用位置。 */
  cJSON *ranges = cJSON_CreateArray();
  cJSON_AddItemToArray(ranges, lsp_range_to_json(doc_range(wu.doc, offset, offset + (size_t)length)));
  cJSON_AddItemToObject(call, "fromRanges", ranges);
  cJSON_AddItemToArray(c->arr, call);
}

cJSON *feature_call_hierarchy_incoming(Workspace *ws, const char *fn_name) {
  cJSON *arr = cJSON_CreateArray();
  InCtx c = { ws, fn_name, arr };
  workspace_find_occurrences(ws, fn_name, incoming_occ_cb, &c);
  return arr;
}
