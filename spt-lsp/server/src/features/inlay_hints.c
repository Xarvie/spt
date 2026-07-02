/* inlay_hints.c — textDocument/inlayHints
**
** Phase 3: 参数名提示。在函数调用处，为每个参数显示对应的形参名。
** 例如 f(1, 2) → f(a= 1, b= 2)。
**
** 实现：递归遍历 AST，找到所有 NODE_FUNCTION_CALL 节点，
** 解析被调用函数的形参列表，为每个实参生成 InlayHint。
*/
#include "lsp_features.h"
#include "semantic.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"
#include "workspace.h"

#include <stdio.h>
#include <string.h>

/* ---- 位置辅助 ---- */
static LspPos pos_from_loc(const Document *d, int line1, int col1) {
  return doc_pos_from_frontend(d, line1, col1);
}

/* ---- 递归遍历 AST，收集函数调用的参数名提示 ---- */
typedef struct {
  const SptLspUnit *u;
  const Document *d;
  Workspace *ws;
  cJSON *hints;   /* InlayHint[] */
  LspRange range; /* 请求范围过滤 */
} HintCtx;

static void walk_expr(HintCtx *c, const AstNode *expr);
static void walk_stmt(HintCtx *c, const AstNode *stmt);
static void walk_block(HintCtx *c, const AstNode *block);

/* 在范围内？ */
static int in_range(HintCtx *c, int line1, int col1) {
  LspPos p = pos_from_loc(c->d, line1, col1);
  if (p.line < c->range.start.line)
    return 0;
  if (p.line == c->range.start.line && p.character < c->range.start.character)
    return 0;
  if (p.line > c->range.end.line)
    return 0;
  if (p.line == c->range.end.line && p.character > c->range.end.character)
    return 0;
  return 1;
}

/* 处理函数调用：为每个实参生成参数名提示。 */
static void handle_call(HintCtx *c, const AstNode *call) {
  const AstNode *callee = call->u.call.func;
  if (!callee || callee->type != NODE_IDENTIFIER || !callee->u.ident.name)
    return;

  char name[256];
  snprintf(name, sizeof name, "%s", callee->u.ident.name);

  /* 查找函数定义。 */
  const AstNode *fn = sem_find_function(c->u, name);

  /* Phase 3: 跨文件查找。 */
  if (!fn && c->ws) {
    char mod_path[256];
    if (sem_import_binding_path(c->u, name, mod_path, sizeof mod_path)) {
      char tgt_uri[4096];
      if (workspace_resolve_module(c->ws, c->d->uri ? c->d->uri : "", mod_path, tgt_uri,
                                   sizeof tgt_uri)) {
        char tgt_path[4096];
        spt_uri_to_path(tgt_uri, tgt_path, sizeof tgt_path);
        WsUnit wu = workspace_get_unit(c->ws, tgt_path);
        if (wu.unit)
          fn = sem_find_function(wu.unit, name);
      }
    }
  }

  if (!fn)
    return;

  const AstList *args = &call->u.call.args;
  const AstList *params = &fn->u.func_decl.params;

  /* 为每个实参生成提示（仅当有对应形参名时）。 */
  for (int i = 0; i < args->count && i < params->count; i++) {
    AstNode *arg = args->items[i];
    AstNode *param = params->items[i];
    if (!param->u.param.name || !param->u.param.name[0])
      continue;

    /* 实参位置。 */
    if (!in_range(c, arg->loc.line, arg->loc.column))
      continue;

    LspPos pos = pos_from_loc(c->d, arg->loc.line, arg->loc.column);

    /* 跳过命名参数（如果实参本身就是 name=value 形式，不重复提示）。 */
    if (arg->type == NODE_IDENTIFIER && arg->u.ident.name &&
        strcmp(arg->u.ident.name, param->u.param.name) == 0)
      continue;

    cJSON *hint = cJSON_CreateObject();
    cJSON_AddItemToObject(hint, "position", lsp_pos_to_json(pos));
    char label[300];
    snprintf(label, sizeof label, "%s=", param->u.param.name);
    cJSON_AddStringToObject(hint, "label", label);
    cJSON_AddNumberToObject(hint, "kind", 2); /* Parameter */
    cJSON_AddBoolToObject(hint, "paddingRight", 1);
    cJSON_AddItemToArray(c->hints, hint);
  }
}

/* 递归遍历表达式，找函数调用。 */
static void walk_expr(HintCtx *c, const AstNode *expr) {
  if (!expr)
    return;
  switch (expr->type) {
  case NODE_FUNCTION_CALL:
    handle_call(c, expr);
    /* 继续遍历参数中的嵌套调用。 */
    for (int i = 0; i < expr->u.call.args.count; i++)
      walk_expr(c, expr->u.call.args.items[i]);
    walk_expr(c, expr->u.call.func);
    break;
  case NODE_BINARY_OP:
    walk_expr(c, expr->u.binary.left);
    walk_expr(c, expr->u.binary.right);
    break;
  case NODE_UNARY_OP:
    walk_expr(c, expr->u.unary.operand);
    break;
  case NODE_MEMBER_ACCESS:
    walk_expr(c, expr->u.member.object);
    break;
  case NODE_INDEX_ACCESS:
    walk_expr(c, expr->u.index.array);
    walk_expr(c, expr->u.index.index);
    break;
  case NODE_IDENTIFIER:
  case NODE_LITERAL_INT:
  case NODE_LITERAL_FLOAT:
  case NODE_LITERAL_STRING:
  case NODE_LITERAL_BOOL:
  case NODE_LITERAL_NULL:
  case NODE_LITERAL_LIST:
  case NODE_LITERAL_MAP:
    break;
  default:
    break;
  }
}

/* 递归遍历语句。 */
static void walk_stmt(HintCtx *c, const AstNode *stmt) {
  if (!stmt)
    return;
  switch (stmt->type) {
  case NODE_BLOCK:
    walk_block(c, stmt);
    break;
  case NODE_EXPRESSION_STATEMENT:
    walk_expr(c, stmt->u.expr_stmt.expr);
    break;
  case NODE_ASSIGNMENT:
    for (int i = 0; i < stmt->u.assign.lvalues.count; i++)
      walk_expr(c, stmt->u.assign.lvalues.items[i]);
    for (int i = 0; i < stmt->u.assign.rvalues.count; i++)
      walk_expr(c, stmt->u.assign.rvalues.items[i]);
    break;
  case NODE_IF_STATEMENT:
    walk_expr(c, stmt->u.if_stmt.condition);
    walk_block(c, stmt->u.if_stmt.then_block);
    for (int i = 0; i < stmt->u.if_stmt.else_if_clauses.count; i++) {
      AstNode *cl = stmt->u.if_stmt.else_if_clauses.items[i];
      walk_expr(c, cl->u.if_clause.condition);
      walk_block(c, cl->u.if_clause.body);
    }
    walk_block(c, stmt->u.if_stmt.else_block);
    break;
  case NODE_WHILE_STATEMENT:
    walk_expr(c, stmt->u.while_stmt.condition);
    walk_block(c, stmt->u.while_stmt.body);
    break;
  case NODE_FOR_NUMERIC_STATEMENT:
    walk_expr(c, stmt->u.for_num.start);
    walk_expr(c, stmt->u.for_num.end);
    walk_expr(c, stmt->u.for_num.step);
    walk_block(c, stmt->u.for_num.body);
    break;
  case NODE_FOR_EACH_STATEMENT:
    for (int i = 0; i < stmt->u.for_each.iterable_exprs.count; i++)
      walk_expr(c, stmt->u.for_each.iterable_exprs.items[i]);
    walk_block(c, stmt->u.for_each.body);
    break;
  case NODE_RETURN_STATEMENT:
    for (int i = 0; i < stmt->u.return_stmt.values.count; i++)
      walk_expr(c, stmt->u.return_stmt.values.items[i]);
    break;
  case NODE_VARIABLE_DECL:
    walk_expr(c, stmt->u.var_decl.initializer);
    break;
  case NODE_FUNCTION_DECL:
    walk_block(c, stmt->u.func_decl.body);
    break;
  case NODE_CLASS_DECL:
    for (int i = 0; i < stmt->u.class_decl.members.count; i++) {
      AstNode *m = stmt->u.class_decl.members.items[i];
      if (m->type == NODE_CLASS_MEMBER) {
        AstNode *decl = m->u.class_member.member_declaration;
        if (decl && decl->type == NODE_FUNCTION_DECL)
          walk_block(c, decl->u.func_decl.body);
      }
    }
    break;
  default:
    break;
  }
}

static void walk_block(HintCtx *c, const AstNode *block) {
  if (!block || block->type != NODE_BLOCK)
    return;
  const AstList *st = &block->u.block.statements;
  for (int i = 0; i < st->count; i++)
    walk_stmt(c, st->items[i]);
}

cJSON *feature_inlay_hints(const Document *d, LspRange range, Workspace *ws) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *hints = cJSON_CreateArray();
  if (u && u->root) {
    HintCtx c = {u, d, ws, hints, range};
    walk_block(&c, u->root);
  }
  spt_lsp_unit_free(u);
  return hints;
}
