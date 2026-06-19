/* folding_range.c — textDocument/foldingRange
**
** 纯语法：遍历 AST，对每个带闭合花括号的块（NODE_BLOCK 且 use_end）发射一条
** FoldingRange（起始行..结束行）。覆盖函数体、if/else/while/for/defer 体。
** class_decl 无 BLOCK 包裹，单独按首行..末成员末行折叠。
*/
#include "lsp_features.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"

/* 前端坐标（line1, col1，均 1 起，col 按字节）-> 字节偏移，钳制到合法范围。 */
static size_t off_of_lc(const Document *d, int line1, int col1) {
  int l = line1 - 1;
  if (l < 0) l = 0;
  if (l >= d->line_count) return d->text_len;
  size_t off = d->line_starts[l] + (size_t)(col1 > 0 ? col1 - 1 : 0);
  if (off > d->text_len) off = d->text_len;
  return off;
}

typedef struct { cJSON *arr; const Document *d; } FoldCtx;

static void emit_fold(FoldCtx *c, size_t start_off, size_t end_off) {
  if (end_off <= start_off) return;
  LspPos a = doc_pos_at(c->d, start_off);
  LspPos b = doc_pos_at(c->d, end_off);
  if (b.line <= a.line) return; /* 单行不折 */
  cJSON *it = cJSON_CreateObject();
  cJSON_AddNumberToObject(it, "startLine", a.line);
  cJSON_AddNumberToObject(it, "endLine", b.line);
  cJSON_AddItemToArray(c->arr, it);
}

/* 递归收集可折叠块。 */
static void fold_walk(const AstNode *n, const Document *d, FoldCtx *c) {
  if (!n) return;
  switch (n->type) {
  case NODE_BLOCK: {
    if (n->u.block.use_end) {
      size_t s = off_of_lc(d, n->loc.line, n->loc.column);
      size_t e = off_of_lc(d, n->u.block.end_loc.line, n->u.block.end_loc.column);
      emit_fold(c, s, e);
    }
    const AstList *st = &n->u.block.statements;
    for (int i = 0; i < st->count; i++) fold_walk(st->items[i], d, c);
    break;
  }
  case NODE_FUNCTION_DECL:
    fold_walk(n->u.func_decl.body, d, c);
    break;
  case NODE_CLASS_DECL: {
    const AstList *m = &n->u.class_decl.members;
    if (m->count >= 1) {
      /* 类体：从 class 关键字行 到 末成员末行 */
      size_t s = off_of_lc(d, n->loc.line, n->loc.column);
      const AstNode *last = m->items[m->count - 1];
      size_t e = off_of_lc(d, last->loc.line, last->loc.column);
      /* 末成员若是方法，延伸到其体末行 */
      if (last->type == NODE_CLASS_MEMBER && last->u.class_member.member_declaration &&
          last->u.class_member.member_declaration->type == NODE_FUNCTION_DECL) {
        const AstNode *body = last->u.class_member.member_declaration->u.func_decl.body;
        if (body && body->type == NODE_BLOCK && body->u.block.use_end)
          e = off_of_lc(d, body->u.block.end_loc.line, body->u.block.end_loc.column);
      }
      emit_fold(c, s, e);
    }
    for (int i = 0; i < m->count; i++) fold_walk(m->items[i], d, c);
    break;
  }
  case NODE_CLASS_MEMBER:
    fold_walk(n->u.class_member.member_declaration, d, c);
    break;
  case NODE_IF_STATEMENT:
    fold_walk(n->u.if_stmt.then_block, d, c);
    {
      const AstList *ei = &n->u.if_stmt.else_if_clauses;
      for (int i = 0; i < ei->count; i++) fold_walk(ei->items[i]->u.if_clause.body, d, c);
    }
    fold_walk(n->u.if_stmt.else_block, d, c);
    break;
  case NODE_WHILE_STATEMENT:
    fold_walk(n->u.while_stmt.body, d, c);
    break;
  case NODE_FOR_NUMERIC_STATEMENT:
    fold_walk(n->u.for_num.body, d, c);
    break;
  case NODE_FOR_EACH_STATEMENT:
    fold_walk(n->u.for_each.body, d, c);
    break;
  case NODE_DEFER_STATEMENT:
    fold_walk(n->u.defer_stmt.body, d, c);
    break;
  case NODE_DECLARE_MODULE: {
    const AstList *mm = &n->u.declare_module.members;
    if (mm->count >= 1) {
      size_t s = off_of_lc(d, n->loc.line, n->loc.column);
      size_t e = off_of_lc(d, mm->items[mm->count - 1]->loc.line, mm->items[mm->count - 1]->loc.column);
      emit_fold(c, s, e);
    }
    for (int i = 0; i < mm->count; i++) fold_walk(mm->items[i], d, c);
    break;
  }
  default: break;
  }
}

cJSON *feature_folding_range(const Document *d) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *arr = cJSON_CreateArray();
  if (u && u->root) {
    FoldCtx c = {arr, d};
    fold_walk(u->root, d, &c);
  }
  spt_lsp_unit_free(u);
  return arr;
}
