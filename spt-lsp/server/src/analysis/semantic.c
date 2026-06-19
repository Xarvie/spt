/*
** semantic.c — 基于 AST 的符号收集与名字解析。
*/
#include "semantic.h"

#include "protocol.h"
#include "spt_ast.h"
#include "spt_token.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
** 基础：坐标换算、字符串拼接
** ========================================================================= */
static size_t off_of(const Document *d, int line1, int col1) {
  int l = line1 - 1;
  if (l < 0) l = 0;
  if (l >= d->line_count) return d->text_len;
  size_t off = d->line_starts[l] + (size_t)(col1 > 0 ? col1 - 1 : 0);
  if (off > d->text_len) off = d->text_len;
  return off;
}

static size_t line_end_off(const Document *d, int line1) {
  if (line1 >= 1 && line1 < d->line_count)
    return d->line_starts[line1] > 0 ? d->line_starts[line1] - 1 : 0;
  return d->text_len;
}

static void sappend(char *buf, size_t cap, size_t *pos, const char *fmt, ...) {
  if (cap == 0 || *pos >= cap) return;
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf + *pos, cap - *pos, fmt, ap);
  va_end(ap);
  if (n > 0) *pos += (size_t)n;
  if (*pos >= cap) *pos = cap - 1;
}

/* ===========================================================================
** 类型 / 签名渲染
** ========================================================================= */
void sem_type_string(const AstNode *t, char *out, size_t cap) {
  if (cap == 0) return;
  out[0] = '\0';
  if (!t) { snprintf(out, cap, "auto"); return; }
  switch (t->type) {
  case NODE_TYPE_PRIMITIVE:
    switch (t->u.type_prim.kind) {
    case PTK_INT: snprintf(out, cap, "int"); break;
    case PTK_FLOAT: snprintf(out, cap, "float"); break;
    case PTK_NUMBER: snprintf(out, cap, "number"); break;
    case PTK_STRING: snprintf(out, cap, "str"); break;
    case PTK_BOOL: snprintf(out, cap, "bool"); break;
    case PTK_VOID: snprintf(out, cap, "void"); break;
    case PTK_NULL: snprintf(out, cap, "null"); break;
    default: snprintf(out, cap, "?"); break;
    }
    break;
  case NODE_TYPE_ANY: snprintf(out, cap, "any"); break;
  case NODE_TYPE_AUTO: snprintf(out, cap, "auto"); break;
  case NODE_TYPE_FUNCTION_KW: snprintf(out, cap, "function"); break;
  case NODE_TYPE_COROUTINE_KW: snprintf(out, cap, "coro"); break;
  case NODE_TYPE_MULTIRETURN: snprintf(out, cap, "vars"); break;
  case NODE_TYPE_LIST: {
    if (t->u.type_list.element) {
      char el[256];
      sem_type_string(t->u.type_list.element, el, sizeof el);
      snprintf(out, cap, "list<%s>", el);
    } else snprintf(out, cap, "list");
    break;
  }
  case NODE_TYPE_MAP: {
    if (t->u.type_map.key) {
      char k[256], v[256];
      sem_type_string(t->u.type_map.key, k, sizeof k);
      sem_type_string(t->u.type_map.value, v, sizeof v);
      snprintf(out, cap, "map<%s,%s>", k, v);
    } else snprintf(out, cap, "map");
    break;
  }
  case NODE_TYPE_USER: {
    size_t pos = 0;
    for (int i = 0; i < t->u.type_user.count; i++)
      sappend(out, cap, &pos, "%s%s", i ? "." : "", t->u.type_user.parts[i]);
    if (pos == 0) snprintf(out, cap, "?");
    break;
  }
  default: snprintf(out, cap, "?"); break;
  }
}

static void build_func_detail(const AstNode *fn, char *out, size_t cap) {
  size_t pos = 0;
  char ret[256];
  sem_type_string(fn->u.func_decl.return_type, ret, sizeof ret);
  sappend(out, cap, &pos, "%s %s(", ret, fn->u.func_decl.name ? fn->u.func_decl.name : "?");
  const AstList *ps = &fn->u.func_decl.params;
  for (int i = 0; i < ps->count; i++) {
    AstNode *p = ps->items[i];
    char pt[256];
    sem_type_string(p->u.param.type_annotation, pt, sizeof pt);
    sappend(out, cap, &pos, "%s%s %s", i ? ", " : "", pt, p->u.param.name ? p->u.param.name : "_");
  }
  if (fn->u.func_decl.is_variadic)
    sappend(out, cap, &pos, "%s...", ps->count ? ", " : "");
  sappend(out, cap, &pos, ")");
}

/* ===========================================================================
** token 工具
** ========================================================================= */
static int ident_token_at(const SptLspUnit *u, const Document *d, size_t off) {
  for (int i = 0; i < u->token_count; i++) {
    if (u->tokens[i].kind != TOK_IDENTIFIER) continue;
    size_t t = off_of(d, u->tokens[i].line, u->tokens[i].column);
    size_t e = t + (size_t)u->tokens[i].length;
    if (off >= t && off <= e) return i;
  }
  return -1;
}

/* 找 start_off 处或之后第一个 lexeme==name 的 IDENTIFIER，写其字节区间。 */
static int name_token_span(const SptLspUnit *u, const Document *d, size_t start_off,
                           const char *name, size_t *s0, size_t *s1) {
  size_t nl = strlen(name);
  for (int i = 0; i < u->token_count; i++) {
    const SptToken *t = &u->tokens[i];
    if (t->kind != TOK_IDENTIFIER) continue;
    size_t toff = off_of(d, t->line, t->column);
    if (toff < start_off) continue;
    if ((size_t)t->length == nl && memcmp(t->lexeme, name, nl) == 0) {
      *s0 = toff;
      *s1 = toff + (size_t)t->length;
      return 1;
    }
  }
  return 0;
}

/* ===========================================================================
** 定义集合
** ========================================================================= */
typedef struct {
  const char *name;
  int kind;            /* LSP SymbolKind */
  const AstNode *node; /* 定义节点（用于取名字位置/detail/doc） */
} Def;
typedef struct { Def *a; int n, cap; } Defs;

static void defs_push(Defs *v, const char *name, int kind, const AstNode *node) {
  if (!name) return;
  if (v->n >= v->cap) {
    v->cap = v->cap ? v->cap * 2 : 16;
    v->a = (Def *)realloc(v->a, sizeof(Def) * (size_t)v->cap);
  }
  v->a[v->n].name = name;
  v->a[v->n].kind = kind;
  v->a[v->n].node = node;
  v->n++;
}

/* 顶层定义（文件作用域名字）。 */
static void collect_file_defs(const AstNode *root, Defs *out) {
  if (!root || root->type != NODE_BLOCK) return;
  const AstList *st = &root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    switch (s->type) {
    case NODE_FUNCTION_DECL:
      defs_push(out, s->u.func_decl.name, LSP_SK_FUNCTION, s);
      break;
    case NODE_CLASS_DECL:
      defs_push(out, s->u.class_decl.name, LSP_SK_CLASS, s);
      break;
    case NODE_VARIABLE_DECL:
      defs_push(out, s->u.var_decl.name, s->u.var_decl.is_const ? LSP_SK_CONSTANT : LSP_SK_VARIABLE, s);
      break;
    case NODE_MUTI_VARIABLE_DECL:
      for (int k = 0; k < s->u.muti_var.count; k++)
        defs_push(out, s->u.muti_var.vars[k].name, LSP_SK_VARIABLE, s);
      break;
    case NODE_IMPORT_NAMESPACE:
      defs_push(out, s->u.import_ns.alias, LSP_SK_MODULE, s);
      break;
    case NODE_IMPORT_NAMED: {
      const AstList *sp = &s->u.import_named.specifiers;
      for (int k = 0; k < sp->count; k++) {
        AstNode *spec = sp->items[k];
        const char *nm = spec->u.import_spec.alias ? spec->u.import_spec.alias
                                                   : spec->u.import_spec.imported_name;
        defs_push(out, nm, LSP_SK_VARIABLE, spec);
      }
      break;
    }
    default: break;
    }
  }
}

/* 在作用域块内收集局部定义（不进入嵌套函数/lambda 体）。 */
static void collect_locals(const AstNode *n, Defs *out) {
  if (!n) return;
  switch (n->type) {
  case NODE_BLOCK: {
    const AstList *st = &n->u.block.statements;
    for (int i = 0; i < st->count; i++) collect_locals(st->items[i], out);
    break;
  }
  case NODE_VARIABLE_DECL:
    defs_push(out, n->u.var_decl.name, n->u.var_decl.is_const ? LSP_SK_CONSTANT : LSP_SK_VARIABLE, n);
    break;
  case NODE_MUTI_VARIABLE_DECL:
    for (int k = 0; k < n->u.muti_var.count; k++)
      defs_push(out, n->u.muti_var.vars[k].name, LSP_SK_VARIABLE, n);
    break;
  case NODE_FUNCTION_DECL:
    /* 嵌套具名函数：作为局部绑定记录，但不进入其体。 */
    defs_push(out, n->u.func_decl.name, LSP_SK_FUNCTION, n);
    break;
  case NODE_IF_STATEMENT: {
    collect_locals(n->u.if_stmt.then_block, out);
    const AstList *ei = &n->u.if_stmt.else_if_clauses;
    for (int i = 0; i < ei->count; i++) collect_locals(ei->items[i]->u.if_clause.body, out);
    collect_locals(n->u.if_stmt.else_block, out);
    break;
  }
  case NODE_WHILE_STATEMENT:
    collect_locals(n->u.while_stmt.body, out);
    break;
  case NODE_FOR_NUMERIC_STATEMENT:
    /* 循环变量也是局部 */
    /* var_name 节点不存在；以 for 节点承载 */
    collect_locals(n->u.for_num.body, out);
    break;
  case NODE_FOR_EACH_STATEMENT: {
    const AstList *lv = &n->u.for_each.loop_variables;
    for (int i = 0; i < lv->count; i++)
      defs_push(out, lv->items[i]->u.param.name, LSP_SK_VARIABLE, lv->items[i]);
    collect_locals(n->u.for_each.body, out);
    break;
  }
  case NODE_DEFER_STATEMENT:
    collect_locals(n->u.defer_stmt.body, out);
    break;
  default: break;
  }
}

/* 找包含 off 的最内层函数（含方法）。 */
static void find_enclosing_fn(const AstNode *n, const Document *d, size_t off,
                              const AstNode **best, size_t *best_span) {
  if (!n) return;
  if (n->type == NODE_FUNCTION_DECL && n->u.func_decl.body &&
      n->u.func_decl.body->type == NODE_BLOCK && n->u.func_decl.body->u.block.use_end) {
    const AstNode *b = n->u.func_decl.body;
    size_t s = off_of(d, b->loc.line, b->loc.column);
    size_t e = off_of(d, b->u.block.end_loc.line, b->u.block.end_loc.column);
    if (off >= s && off <= e) {
      size_t span = e - s;
      if (span < *best_span) { *best = n; *best_span = span; }
    }
  }
  /* 递归子结构（覆盖能含函数的节点） */
  switch (n->type) {
  case NODE_BLOCK: {
    const AstList *st = &n->u.block.statements;
    for (int i = 0; i < st->count; i++) find_enclosing_fn(st->items[i], d, off, best, best_span);
    break;
  }
  case NODE_FUNCTION_DECL: find_enclosing_fn(n->u.func_decl.body, d, off, best, best_span); break;
  case NODE_CLASS_DECL: {
    const AstList *m = &n->u.class_decl.members;
    for (int i = 0; i < m->count; i++) find_enclosing_fn(m->items[i], d, off, best, best_span);
    break;
  }
  case NODE_CLASS_MEMBER: find_enclosing_fn(n->u.class_member.member_declaration, d, off, best, best_span); break;
  case NODE_IF_STATEMENT: {
    find_enclosing_fn(n->u.if_stmt.then_block, d, off, best, best_span);
    const AstList *ei = &n->u.if_stmt.else_if_clauses;
    for (int i = 0; i < ei->count; i++) find_enclosing_fn(ei->items[i]->u.if_clause.body, d, off, best, best_span);
    find_enclosing_fn(n->u.if_stmt.else_block, d, off, best, best_span);
    break;
  }
  case NODE_WHILE_STATEMENT: find_enclosing_fn(n->u.while_stmt.body, d, off, best, best_span); break;
  case NODE_FOR_NUMERIC_STATEMENT: find_enclosing_fn(n->u.for_num.body, d, off, best, best_span); break;
  case NODE_FOR_EACH_STATEMENT: find_enclosing_fn(n->u.for_each.body, d, off, best, best_span); break;
  case NODE_DEFER_STATEMENT: find_enclosing_fn(n->u.defer_stmt.body, d, off, best, best_span); break;
  default: break;
  }
}

/* 遍历某类的成员，按名查找 func/var 成员。 */
static const AstNode *find_member_in_class(const AstNode *cls, const char *name, int *kind) {
  const AstList *m = &cls->u.class_decl.members;
  for (int i = 0; i < m->count; i++) {
    AstNode *mem = m->items[i];
    AstNode *decl = mem->u.class_member.member_declaration;
    if (!decl) continue;
    if (decl->type == NODE_FUNCTION_DECL && decl->u.func_decl.name &&
        strcmp(decl->u.func_decl.name, name) == 0) {
      *kind = LSP_SK_METHOD;
      return decl;
    }
    if (decl->type == NODE_VARIABLE_DECL && decl->u.var_decl.name &&
        strcmp(decl->u.var_decl.name, name) == 0) {
      *kind = LSP_SK_FIELD;
      return decl;
    }
  }
  return NULL;
}

/* 在全文件所有类中按名查找成员。 */
static const AstNode *find_member_anywhere(const AstNode *root, const char *name, int *kind) {
  if (!root || root->type != NODE_BLOCK) return NULL;
  const AstList *st = &root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    if (s->type == NODE_CLASS_DECL) {
      const AstNode *r = find_member_in_class(s, name, kind);
      if (r) return r;
    }
  }
  return NULL;
}

/* 取定义节点的名字、detail、doc。 */
static const char *def_node_name(const AstNode *n) {
  switch (n->type) {
  case NODE_FUNCTION_DECL: return n->u.func_decl.name;
  case NODE_CLASS_DECL: return n->u.class_decl.name;
  case NODE_VARIABLE_DECL: return n->u.var_decl.name;
  case NODE_PARAMETER_DECL: return n->u.param.name;
  case NODE_IMPORT_SPECIFIER: return n->u.import_spec.alias ? n->u.import_spec.alias : n->u.import_spec.imported_name;
  case NODE_IMPORT_NAMESPACE: return n->u.import_ns.alias;
  default: return NULL;
  }
}

/* 定义节点是否为 declare 外部符号（is_ambient）。 */
static int node_is_ambient(const AstNode *n) {
  if (!n) return 0;
  switch (n->type) {
  case NODE_VARIABLE_DECL: return n->u.var_decl.is_ambient;
  case NODE_FUNCTION_DECL: return n->u.func_decl.is_ambient;
  case NODE_CLASS_DECL:    return n->u.class_decl.is_ambient;
  default: return 0;
  }
}

static const char *def_node_doc(const AstNode *n) {
  switch (n->type) {
  case NODE_FUNCTION_DECL: return n->u.func_decl.doc;
  case NODE_CLASS_DECL: return n->u.class_decl.doc;
  case NODE_VARIABLE_DECL: return n->u.var_decl.doc;
  default: return NULL;
  }
}

static void def_node_detail(const AstNode *n, int kind, char *out, size_t cap) {
  out[0] = '\0';
  size_t pos = 0;
  switch (n->type) {
  case NODE_FUNCTION_DECL: build_func_detail(n, out, cap); break;
  case NODE_CLASS_DECL: sappend(out, cap, &pos, "class %s", n->u.class_decl.name ? n->u.class_decl.name : "?"); break;
  case NODE_VARIABLE_DECL: {
    char ty[256];
    sem_type_string(n->u.var_decl.type_annotation, ty, sizeof ty);
    sappend(out, cap, &pos, "%s%s %s", n->u.var_decl.is_const ? "const " : "", ty,
            n->u.var_decl.name ? n->u.var_decl.name : "?");
    break;
  }
  case NODE_PARAMETER_DECL: {
    char ty[256];
    sem_type_string(n->u.param.type_annotation, ty, sizeof ty);
    sappend(out, cap, &pos, "%s %s", ty, n->u.param.name ? n->u.param.name : "?");
    break;
  }
  case NODE_IMPORT_SPECIFIER:
  case NODE_IMPORT_NAMESPACE: sappend(out, cap, &pos, "import"); break;
  default: (void)kind; break;
  }
}

/* 根据定义节点+名字填充 SemRef 的定义部分。 */
static void fill_def(SemRef *r, const SptLspUnit *u, const Document *d, const AstNode *defn,
                     int kind) {
  const char *nm = def_node_name(defn);
  if (!nm) nm = r->name;
  size_t s0, s1;
  size_t start = off_of(d, defn->loc.line, defn->loc.column);
  if (name_token_span(u, d, start, nm, &s0, &s1)) {
    r->def_start = s0;
    r->def_end = s1;
    r->has_def = 1;
  } else {
    r->has_def = 0;
  }
  r->kind = kind;
  r->is_ambient = node_is_ambient(defn);
  def_node_detail(defn, kind, r->detail, sizeof r->detail);
  const char *doc = def_node_doc(defn);
  if (doc) snprintf(r->doc, sizeof r->doc, "%s", doc);
}

SemRef sem_resolve(const SptLspUnit *u, const Document *d, size_t byte_off) {
  SemRef r;
  memset(&r, 0, sizeof r);
  if (!u || !u->root) return r;

  int ti = ident_token_at(u, d, byte_off);
  if (ti < 0) return r;
  const SptToken *tok = &u->tokens[ti];
  size_t nl = (size_t)tok->length;
  if (nl >= sizeof r.name) nl = sizeof r.name - 1;
  memcpy(r.name, tok->lexeme, nl);
  r.name[nl] = '\0';
  r.found = 1;
  size_t uoff = off_of(d, tok->line, tok->column);
  r.use_start = uoff;
  r.use_end = uoff + (size_t)tok->length;

  /* 成员访问？看前一个 token */
  int member = 0;
  if (ti > 0) {
    SptTokenKind pk = u->tokens[ti - 1].kind;
    if (pk == TOK_DOT || pk == TOK_COLON) member = 1;
  }
  r.is_member = member;

  if (member) {
    int kind = LSP_SK_FIELD;
    const AstNode *defn = find_member_anywhere(u->root, r.name, &kind);
    if (defn) fill_def(&r, u, d, defn, kind);
    return r;
  }

  /* 普通名字：先所在函数局部/参数，再文件级 */
  const AstNode *fn = NULL;
  size_t best = (size_t)-1;
  find_enclosing_fn(u->root, d, byte_off, &fn, &best);

  if (fn) {
    Defs locs = {0};
    const AstList *ps = &fn->u.func_decl.params;
    for (int i = 0; i < ps->count; i++)
      defs_push(&locs, ps->items[i]->u.param.name, LSP_SK_VARIABLE, ps->items[i]);
    collect_locals(fn->u.func_decl.body, &locs);
    for (int i = 0; i < locs.n; i++) {
      if (locs.a[i].name && strcmp(locs.a[i].name, r.name) == 0) {
        fill_def(&r, u, d, locs.a[i].node, locs.a[i].kind);
        free(locs.a);
        return r;
      }
    }
    free(locs.a);
  }

  Defs files = {0};
  collect_file_defs(u->root, &files);
  for (int i = 0; i < files.n; i++) {
    if (files.a[i].name && strcmp(files.a[i].name, r.name) == 0) {
      fill_def(&r, u, d, files.a[i].node, files.a[i].kind);
      free(files.a);
      return r;
    }
  }
  free(files.a);
  return r;
}

/* ===========================================================================
** 引用
** ========================================================================= */
int sem_references(const SptLspUnit *u, const Document *d, size_t byte_off, int include_decl,
                   void (*cb)(void *ctx, size_t start, size_t end), void *ctx) {
  SemRef r = sem_resolve(u, d, byte_off);
  if (!r.found) return 0;

  /* 局部/参数：限制在所在函数体内；否则全文件。 */
  size_t scope_s = 0, scope_e = d->text_len;
  int local = 0;
  if (!r.is_member && r.has_def) {
    /* 若定义是参数或局部（在某函数体内）则限定范围 */
    const AstNode *fn = NULL;
    size_t best = (size_t)-1;
    find_enclosing_fn(u->root, d, byte_off, &fn, &best);
    if (fn) {
      const AstNode *b = fn->u.func_decl.body;
      size_t e = off_of(d, b->u.block.end_loc.line, b->u.block.end_loc.column);
      size_t fnstart = off_of(d, fn->loc.line, fn->loc.column);
      if (r.def_start >= fnstart && r.def_end <= e) {
        local = 1;
        scope_s = fnstart;
        scope_e = e;
      }
    }
  }
  (void)local;

  size_t nl = strlen(r.name);
  int count = 0;
  for (int i = 0; i < u->token_count; i++) {
    const SptToken *t = &u->tokens[i];
    if (t->kind != TOK_IDENTIFIER) continue;
    if ((size_t)t->length != nl || memcmp(t->lexeme, r.name, nl) != 0) continue;
    size_t s = off_of(d, t->line, t->column);
    size_t e = s + (size_t)t->length;
    if (s < scope_s || e > scope_e) continue;
    if (!include_decl && r.has_def && s == r.def_start) continue; /* 跳过定义本身 */
    cb(ctx, s, e);
    count++;
  }
  return count;
}

/* ===========================================================================
** 文档符号
** ========================================================================= */
static cJSON *mk_docsym(const Document *d, const char *name, const char *detail, int kind,
                        size_t r0, size_t r1, size_t s0, size_t s1) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "name", name ? name : "?");
  if (detail && detail[0]) cJSON_AddStringToObject(o, "detail", detail);
  cJSON_AddNumberToObject(o, "kind", kind);
  if (r1 < r0) r1 = r0;
  if (s0 < r0) s0 = r0;
  if (s1 > r1) r1 = s1;
  cJSON_AddItemToObject(o, "range", lsp_range_to_json(doc_range(d, r0, r1)));
  cJSON_AddItemToObject(o, "selectionRange", lsp_range_to_json(doc_range(d, s0, s1)));
  return o;
}

/* 计算定义节点的 选择区间(名字) 与 全区间。返回 1 表示成功取得名字区间。 */
static int decl_ranges(const SptLspUnit *u, const Document *d, const AstNode *n, const char *name,
                       size_t *r0, size_t *r1, size_t *s0, size_t *s1) {
  *r0 = off_of(d, n->loc.line, n->loc.column);
  if (!name_token_span(u, d, *r0, name, s0, s1)) {
    *s0 = *r0;
    *s1 = *r0;
  }
  /* 全区间 end */
  size_t end = line_end_off(d, n->loc.line);
  if (n->type == NODE_FUNCTION_DECL && n->u.func_decl.body &&
      n->u.func_decl.body->type == NODE_BLOCK && n->u.func_decl.body->u.block.use_end) {
    const AstNode *b = n->u.func_decl.body;
    end = off_of(d, b->u.block.end_loc.line, b->u.block.end_loc.column);
  }
  if (end < *s1) end = *s1;
  *r1 = end;
  return 1;
}

static cJSON *docsym_for_class(const SptLspUnit *u, const Document *d, const AstNode *cls) {
  size_t r0, r1, s0, s1;
  decl_ranges(u, d, cls, cls->u.class_decl.name, &r0, &r1, &s0, &s1);
  /* 全区间扩到最后一个成员 */
  const AstList *m = &cls->u.class_decl.members;
  cJSON *node = mk_docsym(d, cls->u.class_decl.name, "class", LSP_SK_CLASS, r0, r1, s0, s1);
  cJSON *children = cJSON_CreateArray();
  size_t maxend = r1;
  for (int i = 0; i < m->count; i++) {
    AstNode *mem = m->items[i];
    AstNode *decl = mem->u.class_member.member_declaration;
    if (!decl) continue;
    char detail[512];
    size_t cr0, cr1, cs0, cs1;
    if (decl->type == NODE_FUNCTION_DECL) {
      decl_ranges(u, d, decl, decl->u.func_decl.name, &cr0, &cr1, &cs0, &cs1);
      build_func_detail(decl, detail, sizeof detail);
      cJSON_AddItemToArray(children, mk_docsym(d, decl->u.func_decl.name, detail,
                                               mem->u.class_member.is_static ? LSP_SK_FUNCTION : LSP_SK_METHOD,
                                               cr0, cr1, cs0, cs1));
      if (cr1 > maxend) maxend = cr1;
    } else if (decl->type == NODE_VARIABLE_DECL) {
      decl_ranges(u, d, decl, decl->u.var_decl.name, &cr0, &cr1, &cs0, &cs1);
      def_node_detail(decl, LSP_SK_FIELD, detail, sizeof detail);
      cJSON_AddItemToArray(children, mk_docsym(d, decl->u.var_decl.name, detail, LSP_SK_FIELD,
                                               cr0, cr1, cs0, cs1));
      if (cr1 > maxend) maxend = cr1;
    }
  }
  /* 用扩展后的 end 重写 range（保持 selectionRange 不变） */
  cJSON_DeleteItemFromObject(node, "range");
  cJSON_AddItemToObject(node, "range", lsp_range_to_json(doc_range(d, r0, maxend)));
  cJSON_AddItemToObject(node, "children", children);
  return node;
}

cJSON *sem_document_symbols(const SptLspUnit *u, const Document *d) {
  cJSON *arr = cJSON_CreateArray();
  if (!u || !u->root || u->root->type != NODE_BLOCK) return arr;
  const AstList *st = &u->root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    size_t r0, r1, s0, s1;
    char detail[512];
    switch (s->type) {
    case NODE_FUNCTION_DECL:
      decl_ranges(u, d, s, s->u.func_decl.name, &r0, &r1, &s0, &s1);
      build_func_detail(s, detail, sizeof detail);
      cJSON_AddItemToArray(arr, mk_docsym(d, s->u.func_decl.name, detail, LSP_SK_FUNCTION, r0, r1, s0, s1));
      break;
    case NODE_CLASS_DECL:
      cJSON_AddItemToArray(arr, docsym_for_class(u, d, s));
      break;
    case NODE_VARIABLE_DECL:
      decl_ranges(u, d, s, s->u.var_decl.name, &r0, &r1, &s0, &s1);
      def_node_detail(s, LSP_SK_VARIABLE, detail, sizeof detail);
      cJSON_AddItemToArray(arr, mk_docsym(d, s->u.var_decl.name, detail,
                                          s->u.var_decl.is_const ? LSP_SK_CONSTANT : LSP_SK_VARIABLE,
                                          r0, r1, s0, s1));
      break;
    case NODE_MUTI_VARIABLE_DECL:
      for (int k = 0; k < s->u.muti_var.count; k++) {
        const char *nm = s->u.muti_var.vars[k].name;
        r0 = off_of(d, s->loc.line, s->loc.column);
        if (!name_token_span(u, d, r0, nm, &s0, &s1)) { s0 = r0; s1 = r0; }
        r1 = line_end_off(d, s->loc.line);
        cJSON_AddItemToArray(arr, mk_docsym(d, nm, "", LSP_SK_VARIABLE, r0, r1, s0, s1));
      }
      break;
    case NODE_DECLARE_MODULE: {
      r0 = off_of(d, s->loc.line, s->loc.column);
      s0 = r0; s1 = r0;
      r1 = line_end_off(d, s->loc.line);
      const char *mp = s->u.declare_module.module_path;
      char nm[300];
      snprintf(nm, sizeof nm, "declare \"%s\"", mp ? mp : "");
      cJSON *node = mk_docsym(d, nm, "module", LSP_SK_MODULE, r0, r1, s0, s1);
      cJSON *children = cJSON_CreateArray();
      const AstList *mm = &s->u.declare_module.members;
      size_t maxend = r1;
      for (int k = 0; k < mm->count; k++) {
        AstNode *decl = mm->items[k];
        size_t cr0, cr1, cs0, cs1;
        char cdetail[512];
        if (decl->type == NODE_FUNCTION_DECL) {
          decl_ranges(u, d, decl, decl->u.func_decl.name, &cr0, &cr1, &cs0, &cs1);
          build_func_detail(decl, cdetail, sizeof cdetail);
          cJSON_AddItemToArray(children, mk_docsym(d, decl->u.func_decl.name, cdetail, LSP_SK_FUNCTION, cr0, cr1, cs0, cs1));
          if (cr1 > maxend) maxend = cr1;
        } else if (decl->type == NODE_VARIABLE_DECL) {
          decl_ranges(u, d, decl, decl->u.var_decl.name, &cr0, &cr1, &cs0, &cs1);
          def_node_detail(decl, LSP_SK_VARIABLE, cdetail, sizeof cdetail);
          cJSON_AddItemToArray(children, mk_docsym(d, decl->u.var_decl.name, cdetail, LSP_SK_VARIABLE, cr0, cr1, cs0, cs1));
          if (cr1 > maxend) maxend = cr1;
        } else if (decl->type == NODE_CLASS_DECL) {
          cJSON_AddItemToArray(children, docsym_for_class(u, d, decl));
        }
      }
      cJSON_DeleteItemFromObject(node, "range");
      cJSON_AddItemToObject(node, "range", lsp_range_to_json(doc_range(d, r0, maxend)));
      cJSON_AddItemToObject(node, "children", children);
      cJSON_AddItemToArray(arr, node);
      break;
    }
    default: break;
    }
  }
  return arr;
}

/* ===========================================================================
** 补全用：可见符号 / 成员枚举（复用上面的收集器）
** ========================================================================= */
void sem_visible_symbols(const SptLspUnit *u, const Document *d, size_t off, SemSymCb cb,
                         void *ctx) {
  if (!u || !u->root) return;
  const AstNode *fn = NULL;
  size_t best = (size_t)-1;
  find_enclosing_fn(u->root, d, off, &fn, &best);
  if (fn) {
    const AstList *ps = &fn->u.func_decl.params;
    for (int i = 0; i < ps->count; i++) {
      char det[512];
      def_node_detail(ps->items[i], LSP_SK_VARIABLE, det, sizeof det);
      cb(ctx, ps->items[i]->u.param.name, LSP_SK_VARIABLE, det);
    }
    Defs locs = {0};
    collect_locals(fn->u.func_decl.body, &locs);
    for (int i = 0; i < locs.n; i++) {
      char det[512];
      def_node_detail(locs.a[i].node, locs.a[i].kind, det, sizeof det);
      cb(ctx, locs.a[i].name, locs.a[i].kind, det);
    }
    free(locs.a);
  }
  Defs files = {0};
  collect_file_defs(u->root, &files);
  for (int i = 0; i < files.n; i++) {
    char det[512];
    def_node_detail(files.a[i].node, files.a[i].kind, det, sizeof det);
    cb(ctx, files.a[i].name, files.a[i].kind, det);
  }
  free(files.a);
}

static void emit_class_members(const AstNode *cls, SemSymCb cb, void *ctx) {
  const AstList *m = &cls->u.class_decl.members;
  for (int i = 0; i < m->count; i++) {
    AstNode *decl = m->items[i]->u.class_member.member_declaration;
    if (!decl) continue;
    char det[512];
    if (decl->type == NODE_FUNCTION_DECL) {
      build_func_detail(decl, det, sizeof det);
      cb(ctx, decl->u.func_decl.name, LSP_SK_METHOD, det);
    } else if (decl->type == NODE_VARIABLE_DECL) {
      def_node_detail(decl, LSP_SK_FIELD, det, sizeof det);
      cb(ctx, decl->u.var_decl.name, LSP_SK_FIELD, det);
    }
  }
}

void sem_all_members(const SptLspUnit *u, SemSymCb cb, void *ctx) {
  if (!u || !u->root || u->root->type != NODE_BLOCK) return;
  const AstList *st = &u->root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    if (s->type == NODE_CLASS_DECL) {
      emit_class_members(s, cb, ctx);
    } else if (s->type == NODE_DECLARE_MODULE) {
      const AstList *mm = &s->u.declare_module.members;
      for (int k = 0; k < mm->count; k++) {
        AstNode *decl = mm->items[k];
        char det[512];
        if (decl->type == NODE_FUNCTION_DECL) {
          build_func_detail(decl, det, sizeof det);
          cb(ctx, decl->u.func_decl.name, LSP_SK_METHOD, det);
        } else if (decl->type == NODE_VARIABLE_DECL) {
          def_node_detail(decl, LSP_SK_FIELD, det, sizeof det);
          cb(ctx, decl->u.var_decl.name, LSP_SK_FIELD, det);
        } else if (decl->type == NODE_CLASS_DECL) {
          emit_class_members(decl, cb, ctx);
        }
      }
    }
  }
}

/* 找包含 off 的最内层函数声明（供 signature/补全外部用）。返回节点或 NULL。 */
const AstNode *sem_enclosing_function(const SptLspUnit *u, const Document *d, size_t off) {
  if (!u || !u->root) return NULL;
  const AstNode *fn = NULL;
  size_t best = (size_t)-1;
  find_enclosing_fn(u->root, d, off, &fn, &best);
  return fn;
}

/* 在全文件查找名为 name 的函数（顶层或类方法），用于 signature help。 */
const AstNode *sem_find_function(const SptLspUnit *u, const char *name) {
  if (!u || !u->root || u->root->type != NODE_BLOCK) return NULL;
  const AstList *st = &u->root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    if (s->type == NODE_FUNCTION_DECL && s->u.func_decl.name &&
        strcmp(s->u.func_decl.name, name) == 0)
      return s;
    if (s->type == NODE_CLASS_DECL) {
      const AstList *m = &s->u.class_decl.members;
      for (int k = 0; k < m->count; k++) {
        AstNode *decl = m->items[k]->u.class_member.member_declaration;
        if (decl && decl->type == NODE_FUNCTION_DECL && decl->u.func_decl.name &&
            strcmp(decl->u.func_decl.name, name) == 0)
          return decl;
      }
    }
    if (s->type == NODE_DECLARE_MODULE) {
      const AstList *mm = &s->u.declare_module.members;
      for (int k = 0; k < mm->count; k++) {
        AstNode *decl = mm->items[k];
        if (decl->type == NODE_FUNCTION_DECL && decl->u.func_decl.name &&
            strcmp(decl->u.func_decl.name, name) == 0)
          return decl;
      }
    }
  }
  return NULL;
}

/* ===========================================================================
** 跨文件 import 解析（Phase 1）
** ========================================================================= */

/* 收集顶层导出符号（is_exported && is_module_root）。与 collect_file_defs 类似，
   但筛选导出且不收集 import 节点（import 不是导出）。 */
static void collect_exports(const AstNode *root, Defs *out) {
  if (!root || root->type != NODE_BLOCK) return;
  const AstList *st = &root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    switch (s->type) {
    case NODE_FUNCTION_DECL:
      if (s->u.func_decl.is_exported && s->u.func_decl.is_module_root)
        defs_push(out, s->u.func_decl.name, LSP_SK_FUNCTION, s);
      break;
    case NODE_CLASS_DECL:
      if (s->u.class_decl.is_exported && s->u.class_decl.is_module_root)
        defs_push(out, s->u.class_decl.name, LSP_SK_CLASS, s);
      break;
    case NODE_VARIABLE_DECL:
      if (s->u.var_decl.is_exported && s->u.var_decl.is_module_root)
        defs_push(out, s->u.var_decl.name, s->u.var_decl.is_const ? LSP_SK_CONSTANT : LSP_SK_VARIABLE, s);
      break;
    case NODE_MUTI_VARIABLE_DECL:
      /* README §14.2：多变量 export 语法合法但不生效——不收集。 */
      break;
    default: break;
    }
  }
}

int sem_resolve_export(const SptLspUnit *u, const Document *d, const char *name, SemRef *out) {
  if (!out) return 0;
  if (!u || !u->root || !name) return 0;
  Defs exps = {0};
  collect_exports(u->root, &exps);
  int found = 0;
  for (int i = 0; i < exps.n; i++) {
    if (exps.a[i].name && strcmp(exps.a[i].name, name) == 0) {
      memset(out, 0, sizeof *out);
      out->found = 1;
      snprintf(out->name, sizeof out->name, "%s", name);
      fill_def(out, u, d, exps.a[i].node, exps.a[i].kind);
      found = 1;
      break;
    }
  }
  free(exps.a);
  return found;
}

int sem_resolve_declare_member(const SptLspUnit *u, const Document *d,
                               const char *module_path, const char *symbol_name, SemRef *out) {
  if (!out || !u || !u->root || !module_path || !symbol_name) return 0;
  if (!symbol_name[0]) return 0; /* 命名空间别名本身无对应 declare 成员 */
  if (u->root->type != NODE_BLOCK) return 0;
  const AstList *st = &u->root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    if (s->type != NODE_DECLARE_MODULE) continue;
    const char *mp = s->u.declare_module.module_path;
    if (!mp || strcmp(mp, module_path) != 0) continue;
    const AstList *mm = &s->u.declare_module.members;
    for (int k = 0; k < mm->count; k++) {
      AstNode *decl = mm->items[k];
      const char *nm = def_node_name(decl);
      if (!nm || strcmp(nm, symbol_name) != 0) continue;
      int kind = LSP_SK_FUNCTION;
      if (decl->type == NODE_VARIABLE_DECL)
        kind = decl->u.var_decl.is_const ? LSP_SK_CONSTANT : LSP_SK_VARIABLE;
      else if (decl->type == NODE_CLASS_DECL)
        kind = LSP_SK_CLASS;
      memset(out, 0, sizeof *out);
      out->found = 1;
      snprintf(out->name, sizeof out->name, "%s", symbol_name);
      fill_def(out, u, d, decl, kind);
      return 1;
    }
  }
  return 0;
}

void sem_all_exports(const SptLspUnit *u, SemExportCb cb, void *ctx) {
  if (!u || !u->root || !cb) return;
  Defs exps = {0};
  collect_exports(u->root, &exps);
  for (int i = 0; i < exps.n; i++) {
    if (!exps.a[i].name) continue;
    char detail[512];
    def_node_detail(exps.a[i].node, exps.a[i].kind, detail, sizeof detail);
    cb(ctx, exps.a[i].name, exps.a[i].kind, detail);
  }
  free(exps.a);
}

static int find_import_binding(const AstNode *root, const char *name, int is_recv,
                               SemImportTarget *out);

int sem_namespace_import_path(const SptLspUnit *u, const char *name, char *module_path, size_t cap) {
  if (!u || !name || !module_path || cap == 0) return 0;
  SemImportTarget t;
  memset(&t, 0, sizeof t);
  if (find_import_binding(u->root, name, 1, &t)) {
    snprintf(module_path, cap, "%s", t.module_path);
    return 1;
  }
  return 0;
}

/* 在文件级 import 语句中查找与给定名字匹配的导入绑定。
   - is_recv=1：name 是成员访问的接收者（m.X 的 m），只匹配 NODE_IMPORT_NAMESPACE.alias。
   - is_recv=0：name 是普通标识符，匹配具名导入的绑定名或命名空间别名。
   找到则填 module_path/symbol_name/is_namespace_self。 */
static int find_import_binding(const AstNode *root, const char *name, int is_recv,
                               SemImportTarget *out) {
  if (!root || root->type != NODE_BLOCK) return 0;
  const AstList *st = &root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    if (s->type == NODE_IMPORT_NAMESPACE && s->u.import_ns.alias &&
        strcmp(s->u.import_ns.alias, name) == 0) {
      snprintf(out->module_path, sizeof out->module_path, "%s", s->u.import_ns.module_path);
      out->symbol_name[0] = '\0';
      out->is_namespace_self = is_recv ? 0 : 1;
      return 1;
    }
    if (!is_recv && s->type == NODE_IMPORT_NAMED) {
      const AstList *sp = &s->u.import_named.specifiers;
      for (int k = 0; k < sp->count; k++) {
        AstNode *spec = sp->items[k];
        const char *bound = spec->u.import_spec.alias ? spec->u.import_spec.alias
                                                      : spec->u.import_spec.imported_name;
        if (bound && strcmp(bound, name) == 0) {
          snprintf(out->module_path, sizeof out->module_path, "%s", s->u.import_named.module_path);
          /* 目标符号名取 imported_name（原始名），而非别名。 */
          snprintf(out->symbol_name, sizeof out->symbol_name, "%s",
                   spec->u.import_spec.imported_name ? spec->u.import_spec.imported_name : "");
          out->is_namespace_self = 0;
          return 1;
        }
      }
    }
  }
  return 0;
}

int sem_resolve_import_target(const SptLspUnit *u, const Document *d, size_t byte_off,
                              SemImportTarget *out) {
  if (!out) return 0;
  memset(out, 0, sizeof *out);
  if (!u || !u->root) return 0;

  int ti = ident_token_at(u, d, byte_off);
  if (ti < 0) return 0;
  const SptToken *tok = &u->tokens[ti];
  char name[256];
  size_t nl = (size_t)tok->length;
  if (nl >= sizeof name) nl = sizeof name - 1;
  memcpy(name, tok->lexeme, nl);
  name[nl] = '\0';

  /* 成员访问？前一个 token 是 '.' / ':'。 */
  int member = 0;
  if (ti > 0) {
    SptTokenKind pk = u->tokens[ti - 1].kind;
    if (pk == TOK_DOT || pk == TOK_COLON) member = 1;
  }

  if (member) {
    /* 接收者 = ti-2（点号前的标识符）。仅处理简单 m.X 形式。 */
    if (ti >= 2 && u->tokens[ti - 2].kind == TOK_IDENTIFIER) {
      const SptToken *rt = &u->tokens[ti - 2];
      char recv[256];
      size_t rl = (size_t)rt->length;
      if (rl >= sizeof recv) rl = sizeof recv - 1;
      memcpy(recv, rt->lexeme, rl);
      recv[rl] = '\0';
      if (find_import_binding(u->root, recv, 1, out)) {
        /* symbol_name = 点击的成员名 X */
        snprintf(out->symbol_name, sizeof out->symbol_name, "%s", name);
        out->is_namespace_self = 0;
        out->found = 1;
        return 1;
      }
    }
    return 0;
  }

  /* 普通标识符：在 import 绑定中查找。 */
  if (find_import_binding(u->root, name, 0, out)) {
    out->found = 1;
    return 1;
  }
  return 0;
}
