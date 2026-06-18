/*
 * parser.c — Recursive-descent parser: tokens -> AST.
 *
 * Errors are reported by formatting a message and longjmp-ing out, so the
 * grammar functions can assume well-formed input and stay readable. AST nodes
 * and the arrays they reference are allocated from the caller's arena.
 */
#include "compiler.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <setjmp.h>

typedef struct {
  Lexer    lex;
  Token    cur;
  AstArena *arena;
  char    *err;
  size_t   errsz;
  jmp_buf  jb;
} P;

/* growable temp vector of Node* (malloc-backed, copied into the arena) */
typedef struct { Node **items; int n, cap; } NodeVec;
static void vec_push(NodeVec *v, Node *n) {
  if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 8;
    v->items = (Node **)realloc(v->items, (size_t)v->cap * sizeof(Node *)); }
  v->items[v->n++] = n;
}
static Node **vec_to_arena(P *p, NodeVec *v, int *out_n) {
  *out_n = v->n;
  Node **arr = NULL;
  if (v->n) {
    arr = (Node **)arena_alloc(p->arena, (size_t)v->n * sizeof(Node *));
    memcpy(arr, v->items, (size_t)v->n * sizeof(Node *));
  }
  free(v->items);
  return arr;
}

static void perror_at(P *p, const char *fmt, ...) {
  va_list ap; va_start(ap, fmt);
  int k = snprintf(p->err, p->errsz, "line %d: ", p->cur.line);
  if (k < 0) k = 0;
  vsnprintf(p->err + k, p->errsz - (size_t)k, fmt, ap);
  va_end(ap);
  longjmp(p->jb, 1);
}

static void advance(P *p) { lex_next(&p->lex, &p->cur); }
static int  check(P *p, TokenType t) { return p->cur.type == t; }
static int  accept(P *p, TokenType t) { if (check(p, t)) { advance(p); return 1; } return 0; }
static void expect(P *p, TokenType t, const char *what) {
  if (!accept(p, t)) perror_at(p, "expected %s", what);
}

static Node *node(P *p, NodeKind k) {
  Node *n = (Node *)arena_alloc(p->arena, sizeof(Node));
  n->kind = k; n->line = p->cur.line;
  return n;
}

static int is_type_kw(TokenType t) {
  return t == TK_KW_INT || t == TK_KW_FLOAT || t == TK_KW_STRING || t == TK_KW_BOOL;
}
static Type parse_type(P *p) {
  Type ty;
  switch (p->cur.type) {
    case TK_KW_INT:    ty = TY_INT;    break;
    case TK_KW_FLOAT:  ty = TY_FLOAT;  break;
    case TK_KW_STRING: ty = TY_STRING; break;
    case TK_KW_BOOL:   ty = TY_BOOL;   break;
    default: perror_at(p, "expected a type"); return TY_DYN;
  }
  advance(p);
  return ty;
}

/* resolve string escapes into an arena buffer */
static void unescape(P *p, const char *s, int len, const char **out_s, int *out_len) {
  char *buf = (char *)arena_alloc(p->arena, (size_t)len + 1);
  int j = 0;
  for (int i = 0; i < len; i++) {
    if (s[i] == '\\' && i + 1 < len) {
      char e = s[++i];
      switch (e) {
        case 'n': buf[j++] = '\n'; break;
        case 't': buf[j++] = '\t'; break;
        case 'r': buf[j++] = '\r'; break;
        case '\\': buf[j++] = '\\'; break;
        case '"': buf[j++] = '"'; break;
        case '0': buf[j++] = '\0'; break;
        default:  buf[j++] = e; break;
      }
    } else buf[j++] = s[i];
  }
  buf[j] = '\0';
  *out_s = buf; *out_len = j;
}

/* forward decls */
static Node *parse_expr(P *p);
static Node *parse_block(P *p);
static Node *parse_stmt(P *p);

/* primary := literal | NAME | '(' expr ')' | '[' list ']' */
static Node *parse_primary(P *p) {
  Node *n;
  switch (p->cur.type) {
    case TK_INT:   n = node(p, N_INT);   n->u.ival = p->cur.ival; advance(p); return n;
    case TK_FLOAT: n = node(p, N_FLOAT); n->u.fval = p->cur.fval; advance(p); return n;
    case TK_TRUE:  n = node(p, N_BOOL);  n->u.bval = 1; advance(p); return n;
    case TK_FALSE: n = node(p, N_BOOL);  n->u.bval = 0; advance(p); return n;
    case TK_NULL:  n = node(p, N_NULL);  advance(p); return n;
    case TK_STR:
      n = node(p, N_STR);
      unescape(p, p->cur.s, p->cur.len, &n->u.str.s, &n->u.str.len);
      advance(p); return n;
    case TK_NAME:
      n = node(p, N_NAME); n->u.str.s = p->cur.s; n->u.str.len = p->cur.len;
      advance(p); return n;
    case TK_LPAREN: {
      advance(p);
      n = parse_expr(p);
      expect(p, TK_RPAREN, "')'");
      return n;
    }
    case TK_LBRACKET: {                  /* list literal */
      n = node(p, N_LIST);
      advance(p);
      NodeVec v = {0};
      if (!check(p, TK_RBRACKET)) {
        do { vec_push(&v, parse_expr(p)); } while (accept(p, TK_COMMA));
      }
      expect(p, TK_RBRACKET, "']'");
      n->u.list.elems = vec_to_arena(p, &v, &n->u.list.n);
      return n;
    }
    default:
      perror_at(p, "unexpected token in expression");
      return NULL; /* unreachable */
  }
}

/* postfix := primary ( call-args | index )* */
static Node *parse_postfix(P *p) {
  Node *e = parse_primary(p);
  for (;;) {
    if (check(p, TK_LPAREN)) {           /* call */
      Node *c = node(p, N_CALL);
      c->u.call.fn = e;
      advance(p);
      NodeVec v = {0};
      if (!check(p, TK_RPAREN)) {
        do { vec_push(&v, parse_expr(p)); } while (accept(p, TK_COMMA));
      }
      expect(p, TK_RPAREN, "')'");
      c->u.call.args = vec_to_arena(p, &v, &c->u.call.nargs);
      e = c;
    } else if (check(p, TK_LBRACKET)) {  /* index */
      Node *ix = node(p, N_INDEX);
      ix->u.index.obj = e;
      advance(p);
      ix->u.index.idx = parse_expr(p);
      expect(p, TK_RBRACKET, "']'");
      e = ix;
    } else break;
  }
  return e;
}

/* unary := (type) unary | (- | !) unary | postfix
 *
 * The cast `(type)expr` is recognised by a one-token lookahead: when we see
 * `(` followed by a type keyword followed by `)`, it's a cast, not a grouping.
 * The operand is parsed as another unary so `(int)-(float)x` works as written. */
static Node *parse_unary(P *p) {
  if (check(p, TK_LPAREN)) {
    Lexer save_lex = p->lex;
    Token  save_cur = p->cur;
    advance(p);                            /* consume '(' */
    if (is_type_kw(p->cur.type)) {
      Type ty = parse_type(p);
      expect(p, TK_RPAREN, "')'");
      Node *n = node(p, N_CAST);
      n->u.cast.target = ty;
      n->u.cast.e = parse_unary(p);
      return n;
    }
    p->lex = save_lex;                     /* not a cast: restore and fall through */
    p->cur = save_cur;
  }
  if (check(p, TK_MINUS) || check(p, TK_NOT)) {
    Node *n = node(p, N_UNOP);
    n->u.bin.op = p->cur.type;
    advance(p);
    n->u.bin.l = parse_unary(p);
    return n;
  }
  if (check(p, TK_HASH)) {
    advance(p);
    Node *n = node(p, N_LEN);
    n->u.one.e = parse_unary(p);
    return n;
  }
  return parse_postfix(p);
}

static Node *bin(P *p, TokenType op, Node *l, Node *r) {
  Node *n = node(p, N_BINOP);
  n->u.bin.op = op; n->u.bin.l = l; n->u.bin.r = r;
  return n;
}

/* mul := unary ( (* | / | %) unary )* */
static Node *parse_mul(P *p) {
  Node *l = parse_unary(p);
  while (check(p, TK_STAR) || check(p, TK_SLASH) || check(p, TK_PERCENT)) {
    TokenType op = p->cur.type; advance(p);
    l = bin(p, op, l, parse_unary(p));
  }
  return l;
}

/* additive := mul ( (+ | -) mul )* */
static Node *parse_add(P *p) {
  Node *l = parse_mul(p);
  while (check(p, TK_PLUS) || check(p, TK_MINUS)) {
    TokenType op = p->cur.type; advance(p);
    l = bin(p, op, l, parse_mul(p));
  }
  return l;
}

/* concat := additive ( .. additive )*    (left-associative; lower than +/-) */
static Node *parse_concat(P *p) {
  Node *l = parse_add(p);
  while (check(p, TK_DOTDOT)) {
    TokenType op = p->cur.type; advance(p);
    l = bin(p, op, l, parse_add(p));
  }
  return l;
}

/* compare := concat ( (== != < <= > >=) concat )* */
static Node *parse_expr(P *p) {
  Node *l = parse_concat(p);
  while (check(p, TK_EQ) || check(p, TK_NE) || check(p, TK_LT) ||
         check(p, TK_LE) || check(p, TK_GT) || check(p, TK_GE)) {
    TokenType op = p->cur.type; advance(p);
    l = bin(p, op, l, parse_concat(p));
  }
  return l;
}

static Node *parse_block(P *p) {
  expect(p, TK_LBRACE, "'{'");
  Node *b = node(p, N_BLOCK);
  NodeVec v = {0};
  while (!check(p, TK_RBRACE) && !check(p, TK_EOF)) vec_push(&v, parse_stmt(p));
  expect(p, TK_RBRACE, "'}'");
  b->u.list.elems = vec_to_arena(p, &v, &b->u.list.n);
  return b;
}

static Node *parse_stmt(P *p) {
  switch (p->cur.type) {
    case TK_GLOBAL: {
      Node *n = node(p, N_GLOBAL);
      advance(p);
      if (!check(p, TK_NAME)) perror_at(p, "expected name after 'global'");
      n->u.glob.s = p->cur.s; n->u.glob.len = p->cur.len; advance(p);
      expect(p, TK_ASSIGN, "'='");
      n->u.glob.value = parse_expr(p);
      expect(p, TK_SEMI, "';'");
      return n;
    }
    case TK_FUNCTION: {
      Node *n = node(p, N_FUNC);
      advance(p);
      if (!check(p, TK_NAME)) perror_at(p, "expected function name");
      n->u.func.s = p->cur.s; n->u.func.len = p->cur.len; advance(p);
      expect(p, TK_LPAREN, "'('");
      NodeVec v = {0};
      Type ptypes[64]; int npt = 0;
      if (!check(p, TK_RPAREN)) {
        do {
          Type pty = TY_DYN;
          if (is_type_kw(p->cur.type)) pty = parse_type(p);   /* optional type */
          if (!check(p, TK_NAME)) perror_at(p, "expected parameter name");
          Node *pm = node(p, N_NAME); pm->u.str.s = p->cur.s; pm->u.str.len = p->cur.len;
          advance(p);
          vec_push(&v, pm);
          if (npt >= 64) perror_at(p, "too many parameters");
          ptypes[npt++] = pty;
        } while (accept(p, TK_COMMA));
      }
      expect(p, TK_RPAREN, "')'");
      n->u.func.params = vec_to_arena(p, &v, &n->u.func.nparams);
      n->u.func.ptypes = NULL;
      if (npt) {
        n->u.func.ptypes = (Type *)arena_alloc(p->arena, (size_t)npt * sizeof(Type));
        memcpy(n->u.func.ptypes, ptypes, (size_t)npt * sizeof(Type));
      }
      n->u.func.ret = TY_DYN;
      if (accept(p, TK_ARROW)) n->u.func.ret = parse_type(p);   /* optional -> T */
      n->u.func.body = parse_block(p);
      return n;
    }
    case TK_IF: {
      Node *n = node(p, N_IF);
      advance(p);
      expect(p, TK_LPAREN, "'('");
      n->u.ctrl.cond = parse_expr(p);
      expect(p, TK_RPAREN, "')'");
      n->u.ctrl.a = parse_block(p);
      n->u.ctrl.b = NULL;
      if (accept(p, TK_ELSE)) n->u.ctrl.b = check(p, TK_IF) ? parse_stmt(p) : parse_block(p);
      return n;
    }
    case TK_WHILE: {
      Node *n = node(p, N_WHILE);
      advance(p);
      expect(p, TK_LPAREN, "'('");
      n->u.ctrl.cond = parse_expr(p);
      expect(p, TK_RPAREN, "')'");
      n->u.ctrl.a = parse_block(p);
      n->u.ctrl.b = NULL;
      return n;
    }
    case TK_RETURN: {
      Node *n = node(p, N_RETURN);
      advance(p);
      n->u.one.e = check(p, TK_SEMI) ? NULL : parse_expr(p);
      expect(p, TK_SEMI, "';'");
      return n;
    }
    case TK_BREAK: {
      Node *n = node(p, N_BREAK);
      advance(p);
      expect(p, TK_SEMI, "';'");
      return n;
    }
    case TK_CONTINUE: {
      Node *n = node(p, N_CONTINUE);
      advance(p);
      expect(p, TK_SEMI, "';'");
      return n;
    }
    case TK_LBRACE:
      return parse_block(p);
    case TK_KW_INT: case TK_KW_FLOAT: case TK_KW_STRING: case TK_KW_BOOL:
    case TK_CONST: {
      int is_const = 0;
      Type ty = TY_DYN;
      if (p->cur.type == TK_CONST) {
        is_const = 1; advance(p);
        if (is_type_kw(p->cur.type)) ty = parse_type(p);
      } else ty = parse_type(p);
      Node *n = node(p, N_VARDECL);
      if (!check(p, TK_NAME)) perror_at(p, "expected variable name");
      n->u.vardecl.s = p->cur.s; n->u.vardecl.len = p->cur.len; advance(p);
      n->u.vardecl.ty = ty; n->u.vardecl.is_const = is_const;
      expect(p, TK_ASSIGN, "'='");
      n->u.vardecl.value = parse_expr(p);
      expect(p, TK_SEMI, "';'");
      return n;
    }
    default: {
      Node *e = parse_expr(p);
      if (accept(p, TK_ASSIGN)) {
        if (e->kind != N_NAME && e->kind != N_INDEX)
          perror_at(p, "invalid assignment target");
        Node *n = node(p, N_ASSIGN);
        n->u.assign.target = e;
        n->u.assign.value = parse_expr(p);
        expect(p, TK_SEMI, "';'");
        return n;
      }
      expect(p, TK_SEMI, "';'");
      Node *n = node(p, N_EXPRSTMT);
      n->u.one.e = e;
      return n;
    }
  }
}

Node *spt_parse(spt_State *L, AstArena *arena, const char *src, char *err, size_t errsz) {
  (void)L;
  P p;
  p.arena = arena; p.err = err; p.errsz = errsz;
  lex_init(&p.lex, src);
  if (setjmp(p.jb) != 0) return NULL;
  advance(&p);
  Node *b = node(&p, N_BLOCK);
  NodeVec v = {0};
  while (!check(&p, TK_EOF)) vec_push(&v, parse_stmt(&p));
  b->u.list.elems = vec_to_arena(&p, &v, &b->u.list.n);
  return b;
}
