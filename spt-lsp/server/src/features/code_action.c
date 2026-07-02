/* code_action.c — textDocument/codeAction
**
** Phase 4: 快速修复——为顶层声明补全缺失的 `export` 前缀。
**   - 遍历 AST 顶层声明（函数/类/变量），若未标记 export 且在请求范围内，
**     生成 CodeAction（quickfix）插入 "export " 前缀。
**   - 仅处理文件级声明（is_module_root），不处理函数内局部变量。
*/
#include "lsp_features.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"

#include <string.h>

/* 检查声明节点是否在字节范围内，且缺少 export 前缀。
   返回 1=可修复，0=跳过。kind_out 输出声明类型名（用于标题）。 */
static int check_export_missing(const AstNode *n, size_t off_s, size_t off_e, const Document *d,
                                const char **kind_out, size_t *insert_off_out) {
  if (!n)
    return 0;
  size_t decl_off = 0;
  int has_export = 0;
  int is_root = 0;
  const char *kind = NULL;

  switch (n->type) {
  case NODE_FUNCTION_DECL:
    kind = "function";
    has_export = n->u.func_decl.is_exported;
    is_root = n->u.func_decl.is_module_root;
    decl_off = doc_offset_at(d, doc_pos_from_frontend(d, n->loc.line, n->loc.column));
    break;
  case NODE_CLASS_DECL:
    kind = "class";
    has_export = n->u.class_decl.is_exported;
    is_root = n->u.class_decl.is_module_root;
    decl_off = doc_offset_at(d, doc_pos_from_frontend(d, n->loc.line, n->loc.column));
    break;
  case NODE_VARIABLE_DECL:
    kind = "variable";
    has_export = n->u.var_decl.is_exported;
    is_root = n->u.var_decl.is_module_root;
    decl_off = doc_offset_at(d, doc_pos_from_frontend(d, n->loc.line, n->loc.column));
    break;
  default:
    return 0;
  }

  if (has_export || !is_root)
    return 0;
  /* 检查声明位置是否在请求范围内。 */
  if (decl_off < off_s || decl_off > off_e)
    return 0;

  *kind_out = kind;
  *insert_off_out = decl_off;
  return 1;
}

cJSON *feature_code_action(const Document *d, LspRange range) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *arr = cJSON_CreateArray();
  if (!u || !u->root) {
    spt_lsp_unit_free(u);
    return arr;
  }

  size_t off_s = doc_offset_at(d, range.start);
  size_t off_e = doc_offset_at(d, range.end);

  /* 遍历顶层声明。 */
  const AstList *st = &u->root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *n = st->items[i];
    const char *kind = NULL;
    size_t insert_off = 0;
    if (!check_export_missing(n, off_s, off_e, d, &kind, &insert_off))
      continue;

    /* 构造 CodeAction。 */
    cJSON *action = cJSON_CreateObject();
    char title[128];
    snprintf(title, sizeof title, "Add 'export' to %s", kind);
    cJSON_AddStringToObject(action, "title", title);
    cJSON_AddStringToObject(action, "kind", "quickfix");

    /* WorkspaceEdit.changes */
    cJSON *changes = cJSON_CreateObject();
    cJSON *edits = cJSON_CreateArray();
    cJSON *ed = cJSON_CreateObject();
    /* 插入 "export " 在声明起始处。 */
    LspPos ins_pos = doc_pos_at(d, insert_off);
    LspRange ins_range = {ins_pos, ins_pos};
    cJSON_AddItemToObject(ed, "range", lsp_range_to_json(ins_range));
    cJSON_AddStringToObject(ed, "newText", "export ");
    cJSON_AddItemToArray(edits, ed);
    cJSON_AddItemToObject(changes, d->uri ? d->uri : "", edits);
    cJSON_AddItemToObject(action, "edit", changes);

    cJSON_AddItemToArray(arr, action);
  }

  spt_lsp_unit_free(u);
  return arr;
}
