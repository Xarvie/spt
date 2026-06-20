/* semantic_tokens.c — textDocument/semanticTokens/full（标识符分类，补充 TextMate） */
#include "lsp_features.h"
#include "semantic.h"
#include "spt_ast.h"
#include "spt_lsp_bridge.h"
#include "spt_token.h"
#include "trace.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>   /* realloc / free —— 缺此头时 MSVC 按 int 返回截断 64 位指针，导致崩溃 */

/* 图例（顺序即编码用的 tokenType 索引） */
const char *const SPT_TOKEN_TYPES[] = {
    "keyword", "type", "class", "function", "variable",
    "parameter", "property", "string", "number", "operator", "namespace", "comment"};
const int SPT_TOKEN_TYPES_COUNT = 12;
enum { TT_CLASS = 2, TT_FUNCTION = 3, TT_VARIABLE = 4, TT_PROPERTY = 6 };

/* 名字集合（顶层函数名/类名/方法名） */
typedef struct { const char **a; int n, cap; } NameSet;
static void ns_add(NameSet *s, const char *nm) {
  if (!nm) return;

  FILE *dbg = spt_open_log();
  if (dbg) { fprintf(dbg, "ns_add: n=%d cap=%d nm=%s\n", s->n, s->cap, nm); fflush(dbg); }

  if (s->n >= s->cap) {
    int newcap = s->cap ? s->cap * 2 : 32;
    if (dbg) { fprintf(dbg, "ns_add: realloc cap %d->%d\n", s->cap, newcap); fflush(dbg); }
    s->a = realloc(s->a, sizeof(char *) * (size_t)newcap);
    if (dbg) { fprintf(dbg, "ns_add: realloc returned %p\n", (void*)s->a); fflush(dbg); }
    s->cap = newcap;
  }
  s->a[s->n] = nm;
  if (dbg) { fprintf(dbg, "ns_add: set a[%d]=%s\n", s->n, nm); fflush(dbg); }
  s->n++;
  if (dbg) { fprintf(dbg, "ns_add: done n=%d\n", s->n); fflush(dbg); fclose(dbg); }
}
static int ns_has(const NameSet *s, const char *nm, size_t len) {
  for (int i = 0; i < s->n; i++)
    if (strlen(s->a[i]) == len && memcmp(s->a[i], nm, len) == 0) return 1;
  return 0;
}

static void gather(const AstNode *root, NameSet *funcs, NameSet *classes) {
  if (!root || root->type != NODE_BLOCK) return;
  const AstList *st = &root->u.block.statements;

  FILE *dbg = spt_open_log();
  if (dbg) { fprintf(dbg, "gather: st->count=%d\n", st->count); fflush(dbg); }

  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    const char *tn = spt_node_type_name(s->type);
    if (dbg) { fprintf(dbg, "gather: [%d] type=%d (%s)\n", i, s->type, tn ? tn : "?"); fflush(dbg); }

    if (s->type == NODE_FUNCTION_DECL) {
      const char *nm = s->u.func_decl.name;
      if (dbg) { fprintf(dbg, "gather: func name=%p (%s)\n", (void*)nm, nm ? nm : "(null)"); fflush(dbg); }
      ns_add(funcs, nm);
    } else if (s->type == NODE_CLASS_DECL) {
      ns_add(classes, s->u.class_decl.name);
      const AstList *m = &s->u.class_decl.members;
      if (dbg) { fprintf(dbg, "gather: class members=%d\n", m->count); fflush(dbg); }
      for (int k = 0; k < m->count; k++) {
        AstNode *cm = m->items[k];
        if (dbg) { fprintf(dbg, "gather: member[%d] type=%d\n", k, cm->type); fflush(dbg); }
        AstNode *decl = cm->u.class_member.member_declaration;
        if (decl && decl->type == NODE_FUNCTION_DECL) ns_add(funcs, decl->u.func_decl.name);
      }
    } else if (s->type == NODE_DECLARE_MODULE) {
      const AstList *mm = &s->u.declare_module.members;
      if (dbg) { fprintf(dbg, "gather: declare members=%d\n", mm->count); fflush(dbg); }
      for (int k = 0; k < mm->count; k++) {
        AstNode *decl = mm->items[k];
        if (dbg) { fprintf(dbg, "gather: decl[%d] type=%d\n", k, decl->type); fflush(dbg); }
        if (decl->type == NODE_FUNCTION_DECL) ns_add(funcs, decl->u.func_decl.name);
        else if (decl->type == NODE_CLASS_DECL) ns_add(classes, decl->u.class_decl.name);
      }
    }
  }
  if (dbg) fclose(dbg);
}

cJSON *feature_semantic_tokens_full(const Document *d) {
  FILE *dbg = spt_open_log();
  if (dbg) { fprintf(dbg, "semtok: start, text_len=%zu line_count=%d\n", d->text_len, d->line_count); fflush(dbg); }

  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  if (dbg) { fprintf(dbg, "semtok: parsed, u=%p root=%p tokens=%d\n", (void*)u, u ? (void*)u->root : NULL, u ? u->token_count : 0); fflush(dbg); }

  cJSON *data = cJSON_CreateArray();
  if (u && u->root) {
    NameSet funcs = {0}, classes = {0};
    gather(u->root, &funcs, &classes);
    if (dbg) { fprintf(dbg, "semtok: gathered, funcs=%d classes=%d\n", funcs.n, classes.n); fflush(dbg); }

    int prev_line = 0, prev_char = 0;
    for (int i = 0; i < u->token_count; i++) {
      const SptToken *t = &u->tokens[i];
      if (t->kind != TOK_IDENTIFIER) continue;
      int member = (i > 0 && (u->tokens[i - 1].kind == TOK_DOT || u->tokens[i - 1].kind == TOK_COLON));
      int type;
      if (member) type = TT_PROPERTY;
      else if (ns_has(&funcs, t->lexeme, (size_t)t->length)) type = TT_FUNCTION;
      else if (ns_has(&classes, t->lexeme, (size_t)t->length)) type = TT_CLASS;
      else type = TT_VARIABLE;

      if (dbg && i < 20) { fprintf(dbg, "semtok: tok[%d] lexeme=%s line=%d col=%d len=%d\n", i, t->lexeme ? t->lexeme : "(null)", t->line, t->column, t->length); fflush(dbg); }

      /* LSP 坐标 + UTF-16 长度 */
      int l = t->line - 1;
      if (l < 0) l = 0;
      size_t base = (l < d->line_count) ? d->line_starts[l] : d->text_len;
      size_t s = base + (size_t)(t->column > 0 ? t->column - 1 : 0);
      if (s > d->text_len) s = d->text_len;
      size_t e = s + (size_t)t->length;
      if (e > d->text_len) e = d->text_len;
      LspPos ps = doc_pos_at(d, s), pe = doc_pos_at(d, e);
      int line = ps.line, ch = ps.character, len = pe.character - ps.character;
      if (len <= 0) len = t->length;

      int dl = line - prev_line;
      int dc = (dl == 0) ? ch - prev_char : ch;
      prev_line = line; prev_char = ch;

      cJSON_AddItemToArray(data, cJSON_CreateNumber(dl));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(dc));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(len));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(type));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(0));
    }
    free(funcs.a); free(classes.a);
    if (dbg) { fprintf(dbg, "semtok: loop done\n"); fflush(dbg); }
  }
  spt_lsp_unit_free(u);
  if (dbg) { fprintf(dbg, "semtok: unit freed\n"); fflush(dbg); }
  cJSON *res = cJSON_CreateObject();
  cJSON_AddItemToObject(res, "data", data);
  if (dbg) { fprintf(dbg, "semtok: done\n"); fflush(dbg); fclose(dbg); }
  return res;
}

/* Phase 6e: semanticTokens/range — 只返回 range 内的 token。 */
cJSON *feature_semantic_tokens_range(const Document *d, LspRange range) {
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *data = cJSON_CreateArray();
  if (u && u->root) {
    NameSet funcs = {0}, classes = {0};
    gather(u->root, &funcs, &classes);

    int prev_line = 0, prev_char = 0;
    for (int i = 0; i < u->token_count; i++) {
      const SptToken *t = &u->tokens[i];
      if (t->kind != TOK_IDENTIFIER) continue;
      int member = (i > 0 && (u->tokens[i - 1].kind == TOK_DOT || u->tokens[i - 1].kind == TOK_COLON));
      int type;
      if (member) type = TT_PROPERTY;
      else if (ns_has(&funcs, t->lexeme, (size_t)t->length)) type = TT_FUNCTION;
      else if (ns_has(&classes, t->lexeme, (size_t)t->length)) type = TT_CLASS;
      else type = TT_VARIABLE;

      int l = t->line - 1;
      if (l < 0) l = 0;
      size_t base = (l < d->line_count) ? d->line_starts[l] : d->text_len;
      size_t s = base + (size_t)(t->column > 0 ? t->column - 1 : 0);
      if (s > d->text_len) s = d->text_len;
      size_t e = s + (size_t)t->length;
      if (e > d->text_len) e = d->text_len;
      LspPos ps = doc_pos_at(d, s), pe = doc_pos_at(d, e);
      int line = ps.line, ch = ps.character, len = pe.character - ps.character;
      if (len <= 0) len = t->length;

      /* range 过滤：跳过 range 外的 token。 */
      if (line < range.start.line || line > range.end.line) continue;
      if (line == range.start.line && ch < range.start.character) continue;
      if (line == range.end.line && ch >= range.end.character) continue;

      int dl = line - prev_line;
      int dc = (dl == 0) ? ch - prev_char : ch;
      prev_line = line; prev_char = ch;

      cJSON_AddItemToArray(data, cJSON_CreateNumber(dl));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(dc));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(len));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(type));
      cJSON_AddItemToArray(data, cJSON_CreateNumber(0));
    }
    free(funcs.a); free(classes.a);
  }
  spt_lsp_unit_free(u);
  cJSON *res = cJSON_CreateObject();
  cJSON_AddItemToObject(res, "data", data);
  return res;
}
