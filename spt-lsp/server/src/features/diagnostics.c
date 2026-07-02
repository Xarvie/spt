/*
** diagnostics.c — 容错解析 -> LSP 诊断。
**
** Phase 3: 增加结构性语义警告（克制）：
**   - 未定义名：标识符无法解析到任何定义（跳过成员访问/import/声明名/内建）
**   - arity 不符：函数调用参数数 vs 声明形参数，差值>1 且无 varargs 时警告
** 不报类型不匹配。
*/
#include "diagnostics.h"

#include "protocol.h"
#include "semantic.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"
#include "spt_token.h"

#include <string.h>

/* 找前端坐标 (line1,col1) 处起始的 token，返回其字节长度；无则 0。 */
static int token_byte_len_at(const SptLspUnit *u, int line1, int col1) {
  for (int i = 0; i < u->token_count; i++) {
    if (u->tokens[i].line == line1 && u->tokens[i].column == col1)
      return u->tokens[i].length;
  }
  return 0;
}

/* 添加一条诊断。 */
static void add_diag(cJSON *arr, const Document *d, int line1, int col1, int len, int severity,
                     const char *msg) {
  LspPos start = doc_pos_from_frontend(d, line1, col1);
  size_t sb = doc_offset_at(d, start);
  size_t eb = sb + (len > 0 ? (size_t)len : 1);
  if (eb > d->text_len)
    eb = d->text_len;
  LspPos end = doc_pos_at(d, eb);
  LspRange r = {start, end};

  cJSON *diag = cJSON_CreateObject();
  cJSON_AddItemToObject(diag, "range", lsp_range_to_json(r));
  cJSON_AddNumberToObject(diag, "severity", severity);
  cJSON_AddStringToObject(diag, "source", "spt");
  cJSON_AddStringToObject(diag, "message", msg);
  cJSON_AddItemToArray(arr, diag);
}

/* ---- Phase 3: 未定义名检测 ---- */

/* 已知内建函数名（不报未定义）。 */
static int is_builtin_name(const char *name) {
  static const char *builtins[] = {
      "print", "assert", "type",  "len",      "push",  "pop",     "keys", "values",
      "has",   "delete", "error", "tostring", "toint", "tofloat", "str",  "int",
      "float", "bool",   "sqrt",  "pow",      "abs",   "min",     "max",  "sum",
      "range", "open",   "close", "read",     "write", "clock",   "time", NULL};
  for (int i = 0; builtins[i]; i++)
    if (strcmp(name, builtins[i]) == 0)
      return 1;
  return 0;
}

/* 判断 token 是否在 import 语句中（跳过 import/from/as 后的标识符）。 */
static int is_in_import(const SptLspUnit *u, int ti) {
  /* 向前查找，遇到 import/from 关键字且中间无 ; 则认为在 import 语句中。 */
  for (int j = ti - 1; j >= 0; j--) {
    SptTokenKind k = u->tokens[j].kind;
    if (k == TOK_SEMICOLON)
      return 0;
    if (k == TOK_IMPORT || k == TOK_FROM || k == TOK_AS)
      return 1;
  }
  return 0;
}

static void check_undefined_names(const SptLspUnit *u, const Document *d, cJSON *arr) {
  for (int i = 0; i < u->token_count; i++) {
    const SptToken *t = &u->tokens[i];
    if (t->kind != TOK_IDENTIFIER)
      continue;

    /* 跳过成员访问（前驱为 . 或 :）。 */
    if (i > 0) {
      SptTokenKind pk = u->tokens[i - 1].kind;
      if (pk == TOK_DOT || pk == TOK_COLON)
        continue;
    }

    /* 跳过 import 语句中的标识符。 */
    if (is_in_import(u, i))
      continue;

    /* 提取名字。 */
    char name[256];
    size_t nl = (size_t)t->length;
    if (nl >= sizeof name)
      nl = sizeof name - 1;
    memcpy(name, t->lexeme, nl);
    name[nl] = '\0';

    /* 跳过内建函数。 */
    if (is_builtin_name(name))
      continue;

    /* 用 sem_resolve 检查是否可解析。 */
    size_t byte_off = 0;
    int li = t->line - 1;
    if (li < 0)
      li = 0;
    if (li < d->line_count)
      byte_off = d->line_starts[li] + (size_t)(t->column > 0 ? t->column - 1 : 0);

    SemRef r = sem_resolve(u, d, byte_off);
    if (r.found && !r.has_def && !r.is_member) {
      char msg[300];
      snprintf(msg, sizeof msg, "未定义的名称: '%s'", name);
      add_diag(arr, d, t->line, t->column, t->length, 2 /* Warning */, msg);
    }
  }
}

/* ---- Phase 3: arity 不符检测 ---- */

typedef struct {
  const SptLspUnit *u;
  const Document *d;
  cJSON *arr;
} ArityCtx;

static void arity_walk_expr(ArityCtx *c, const AstNode *expr);
static void arity_walk_stmt(ArityCtx *c, const AstNode *stmt);
static void arity_walk_block(ArityCtx *c, const AstNode *block);

static void check_call_arity(ArityCtx *c, const AstNode *call) {
  const AstNode *callee = call->u.call.func;
  if (!callee || callee->type != NODE_IDENTIFIER || !callee->u.ident.name)
    return;

  const AstNode *fn = sem_find_function(c->u, callee->u.ident.name);
  if (!fn)
    return;

  int nargs = call->u.call.args.count;
  int nparams = fn->u.func_decl.params.count;

  /* 变参函数不检查。 */
  if (fn->u.func_decl.is_variadic)
    return;

  /* 差值 > 1 才报告（克制）。 */
  int diff = nargs - nparams;
  if (diff < 0)
    diff = -diff;
  if (diff <= 1)
    return;

  char msg[300];
  snprintf(msg, sizeof msg, "参数数量不符: 期望 %d 个, 实际 %d 个", nparams, nargs);
  add_diag(c->arr, c->d, call->loc.line, call->loc.column, 0, 2 /* Warning */, msg);
}

static void arity_walk_expr(ArityCtx *c, const AstNode *expr) {
  if (!expr)
    return;
  switch (expr->type) {
  case NODE_FUNCTION_CALL:
    check_call_arity(c, expr);
    for (int i = 0; i < expr->u.call.args.count; i++)
      arity_walk_expr(c, expr->u.call.args.items[i]);
    arity_walk_expr(c, expr->u.call.func);
    break;
  case NODE_BINARY_OP:
    arity_walk_expr(c, expr->u.binary.left);
    arity_walk_expr(c, expr->u.binary.right);
    break;
  case NODE_UNARY_OP:
    arity_walk_expr(c, expr->u.unary.operand);
    break;
  case NODE_MEMBER_ACCESS:
    arity_walk_expr(c, expr->u.member.object);
    break;
  case NODE_INDEX_ACCESS:
    arity_walk_expr(c, expr->u.index.array);
    arity_walk_expr(c, expr->u.index.index);
    break;
  default:
    break;
  }
}

static void arity_walk_stmt(ArityCtx *c, const AstNode *stmt) {
  if (!stmt)
    return;
  switch (stmt->type) {
  case NODE_BLOCK:
    arity_walk_block(c, stmt);
    break;
  case NODE_EXPRESSION_STATEMENT:
    arity_walk_expr(c, stmt->u.expr_stmt.expr);
    break;
  case NODE_ASSIGNMENT:
    for (int i = 0; i < stmt->u.assign.lvalues.count; i++)
      arity_walk_expr(c, stmt->u.assign.lvalues.items[i]);
    for (int i = 0; i < stmt->u.assign.rvalues.count; i++)
      arity_walk_expr(c, stmt->u.assign.rvalues.items[i]);
    break;
  case NODE_IF_STATEMENT:
    arity_walk_expr(c, stmt->u.if_stmt.condition);
    arity_walk_block(c, stmt->u.if_stmt.then_block);
    for (int i = 0; i < stmt->u.if_stmt.else_if_clauses.count; i++) {
      AstNode *cl = stmt->u.if_stmt.else_if_clauses.items[i];
      arity_walk_expr(c, cl->u.if_clause.condition);
      arity_walk_block(c, cl->u.if_clause.body);
    }
    arity_walk_block(c, stmt->u.if_stmt.else_block);
    break;
  case NODE_WHILE_STATEMENT:
    arity_walk_expr(c, stmt->u.while_stmt.condition);
    arity_walk_block(c, stmt->u.while_stmt.body);
    break;
  case NODE_FOR_NUMERIC_STATEMENT:
    arity_walk_expr(c, stmt->u.for_num.start);
    arity_walk_expr(c, stmt->u.for_num.end);
    arity_walk_expr(c, stmt->u.for_num.step);
    arity_walk_block(c, stmt->u.for_num.body);
    break;
  case NODE_FOR_EACH_STATEMENT:
    for (int i = 0; i < stmt->u.for_each.iterable_exprs.count; i++)
      arity_walk_expr(c, stmt->u.for_each.iterable_exprs.items[i]);
    arity_walk_block(c, stmt->u.for_each.body);
    break;
  case NODE_RETURN_STATEMENT:
    for (int i = 0; i < stmt->u.return_stmt.values.count; i++)
      arity_walk_expr(c, stmt->u.return_stmt.values.items[i]);
    break;
  case NODE_VARIABLE_DECL:
    arity_walk_expr(c, stmt->u.var_decl.initializer);
    break;
  case NODE_FUNCTION_DECL:
    arity_walk_block(c, stmt->u.func_decl.body);
    break;
  case NODE_CLASS_DECL:
    for (int i = 0; i < stmt->u.class_decl.members.count; i++) {
      AstNode *m = stmt->u.class_decl.members.items[i];
      if (m->type == NODE_CLASS_MEMBER) {
        AstNode *decl = m->u.class_member.member_declaration;
        if (decl && decl->type == NODE_FUNCTION_DECL)
          arity_walk_block(c, decl->u.func_decl.body);
      }
    }
    break;
  default:
    break;
  }
}

static void arity_walk_block(ArityCtx *c, const AstNode *block) {
  if (!block || block->type != NODE_BLOCK)
    return;
  const AstList *st = &block->u.block.statements;
  for (int i = 0; i < st->count; i++)
    arity_walk_stmt(c, st->items[i]);
}

cJSON *diagnostics_compute(const Document *d) {
  cJSON *arr = cJSON_CreateArray();

  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  if (u) {
    /* 解析错误（来自前端容错解析）。 */
    for (int i = 0; i < u->diag_count; i++) {
      LspPos start = doc_pos_from_frontend(d, u->diags[i].line, u->diags[i].column);
      size_t sb = doc_offset_at(d, start);
      int tl = token_byte_len_at(u, u->diags[i].line, u->diags[i].column);
      size_t eb = sb + (tl > 0 ? (size_t)tl : 1);
      if (eb > d->text_len)
        eb = d->text_len;
      LspPos end = doc_pos_at(d, eb);
      LspRange r = {start, end};

      cJSON *diag = cJSON_CreateObject();
      cJSON_AddItemToObject(diag, "range", lsp_range_to_json(r));
      cJSON_AddNumberToObject(diag, "severity", LSP_SEV_ERROR);
      cJSON_AddStringToObject(diag, "source", "spt");
      cJSON_AddStringToObject(diag, "message", u->diags[i].message ? u->diags[i].message : "");
      cJSON_AddItemToArray(arr, diag);
    }

    /* Phase 3: 结构性语义警告（仅在无解析错误时，避免噪声）。 */
    if (u->diag_count == 0 && u->root) {
      check_undefined_names(u, d, arr);
      ArityCtx ac = {u, d, arr};
      arity_walk_block(&ac, u->root);
    }

    spt_lsp_unit_free(u);
  }

  cJSON *params = cJSON_CreateObject();
  cJSON_AddStringToObject(params, "uri", d->uri);
  cJSON_AddNumberToObject(params, "version", d->version);
  cJSON_AddItemToObject(params, "diagnostics", arr);
  return params;
}
