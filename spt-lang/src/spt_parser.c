/*
** spt_parser.c — 手写递归下降 + 优先级爬升解析器实现。
**
** 与原 ANTLR/Visitor 的等价性要点：
**   - 一元 '-' 映射为 OPK_NEGATE（非 SUB）；复合赋值映射见 compound_op()。
**   - '>>' 由两个相邻 TOK_GT 在表达式层合成为 OPK_BW_RSHIFT（shift 优先级）。
**   - map 字面量键：IDENT/STRING/INTEGER/FLOAT 键一律转为字符串字面量，
**     其中 INTEGER/FLOAT 取**原始词素文本**（复刻 visitor 行为）。
**   - 字符串反转义仅处理 \n \t \\ \' \"，其余原样透传（process_string）。
**   - 顶层（scope_depth==0）的声明置 is_module_root=true。
**   - 语句歧义按 ANTLR 的备选顺序（expression 先于 declaration）处理。
*/
#include "spt_parser.h"

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * 解析器状态
 * ========================================================================= */
typedef struct {
  const SptToken *toks;
  int count;
  int pos;
  SptArena *arena;
  SptDiag *diag;
  int scope_depth;
  int panic; /* 处于错误恢复模式 */
} Parser;

/* ---- token 游标 ---- */
static const SptToken *tok_at(Parser *P, int i) {
  if (i >= P->count)
    i = P->count - 1; /* 末尾 EOF */
  if (i < 0)
    i = 0;
  return &P->toks[i];
}
static const SptToken *cur(Parser *P) { return tok_at(P, P->pos); }
static const SptToken *peek(Parser *P, int k) { return tok_at(P, P->pos + k); }
static SptTokenKind cur_kind(Parser *P) { return cur(P)->kind; }
static SptTokenKind peek_kind(Parser *P, int k) { return peek(P, k)->kind; }
static int at(Parser *P, SptTokenKind k) { return cur_kind(P) == k; }
static int at_end(Parser *P) { return cur_kind(P) == TOK_EOF; }

static SourceLocation loc_of(const SptToken *t) {
  SourceLocation l;
  l.line = t->line;
  l.column = t->column;
  return l;
}
static SourceLocation cur_loc(Parser *P) { return loc_of(cur(P)); }

static const SptToken *advance(Parser *P) {
  const SptToken *t = cur(P);
  if (P->pos < P->count - 1)
    P->pos++;
  return t;
}
static int accept(Parser *P, SptTokenKind k) {
  if (at(P, k)) {
    advance(P);
    return 1;
  }
  return 0;
}

static void error_at(Parser *P, const SptToken *t, const char *fmt, const char *arg) {
  if (P->panic)
    return; /* 抑制级联错误 */
  P->panic = 1;
  if (arg)
    spt_diag_error(P->diag, t->line, t->column, fmt, arg);
  else
    spt_diag_error(P->diag, t->line, t->column, "%s", fmt);
}

/* expect（带"期望/实际"双参数信息的诊断）。 */
static const SptToken *expect2(Parser *P, SptTokenKind k) {
  if (at(P, k))
    return advance(P);
  if (!P->panic) {
    P->panic = 1;
    spt_diag_error(P->diag, cur(P)->line, cur(P)->column, "语法错误：期望 %s，却遇到 %s",
                   spt_token_name(k), spt_token_name(cur_kind(P)));
  }
  return cur(P);
}

/* 错误恢复：跳过 token 直到语句边界。 */
static void synchronize(Parser *P) {
  P->panic = 0;
  while (!at_end(P)) {
    if (peek_kind(P, -1) == TOK_SEMICOLON)
      return;
    switch (cur_kind(P)) {
    case TOK_IF:
    case TOK_WHILE:
    case TOK_FOR:
    case TOK_RETURN:
    case TOK_BREAK:
    case TOK_CONTINUE:
    case TOK_CLASS:
    case TOK_IMPORT:
    case TOK_DEFER:
    case TOK_RBRACE:
    case TOK_LBRACE:
      return;
    default:
      advance(P);
    }
  }
}

/* ===========================================================================
 * 临时指针向量（解析期收集，定稿后拷入 arena）
 * ========================================================================= */
typedef struct {
  AstNode **data;
  int count, cap;
} NodeVec;

static void nv_init(NodeVec *v) {
  v->data = NULL;
  v->count = 0;
  v->cap = 0;
}
static void nv_push(NodeVec *v, AstNode *n) {
  if (v->count >= v->cap) {
    int nc = v->cap ? v->cap * 2 : 8;
    AstNode **nd = (AstNode **)realloc(v->data, sizeof(AstNode *) * (size_t)nc);
    if (!nd)
      return;
    v->data = nd;
    v->cap = nc;
  }
  v->data[v->count++] = n;
}
static AstList nv_finish(NodeVec *v, SptArena *a) {
  AstList l = spt_ast_list_from(a, v->data, v->count);
  free(v->data);
  v->data = NULL;
  return l;
}

/* ===========================================================================
 * 前置声明
 * ========================================================================= */
static AstNode *parse_expression(Parser *P);
static AstNode *parse_unary(Parser *P);
static AstNode *parse_postfix(Parser *P);
static AstNode *parse_primary(Parser *P);
static AstList parse_expression_list(Parser *P);
static AstNode *parse_type(Parser *P);
static AstNode *parse_statement(Parser *P);
static AstNode *parse_block(Parser *P);
static AstNode *parse_body(Parser *P);
static AstNode *parse_lambda(Parser *P);

/* ===========================================================================
 * 字面量辅助
 * ========================================================================= */

/* 取 token 词素的以 NUL 结尾副本（栈缓冲优先，超长用 arena）。 */
static const char *lexeme_cstr(Parser *P, const SptToken *t, char *buf, size_t bufsz) {
  if ((size_t)t->length < bufsz) {
    memcpy(buf, t->lexeme, (size_t)t->length);
    buf[t->length] = '\0';
    return buf;
  }
  return spt_arena_strndup(P->arena, t->lexeme, (size_t)t->length);
}

/* 复刻 visitor::processStringLiteral 并补全 g4 EscapeSequence 全部 4 类：
**   1) '\\' [btnfr"']  -> \b \t \n \f \r \" \'
**   2) '\\' '\\'       -> \\
**   3) UnicodeEscape   -> \u{HEX+}  解析码点并编码为 UTF-8
**   4) HexEscape       -> \xHH      解析为单字节
** 其余未知转义原样透传（保留反斜杠 + 字符），与原 visitor 行为一致。
** 写入 arena，返回指针并通过 *out_len 给出字节长度。 */
static int hex_digit_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* 将码点 cp 编码为 UTF-8 写入 out，返回字节数（1-4）。非法码点返回 0。 */
static int utf8_encode(uint32_t cp, char *out) {
  if (cp <= 0x7F) {
    out[0] = (char)cp;
    return 1;
  } else if (cp <= 0x7FF) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  } else if (cp <= 0xFFFF) {
    if (cp >= 0xD800 && cp <= 0xDFFF) return 0; /* 代理对区间非法 */
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  } else if (cp <= 0x10FFFF) {
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
  }
  return 0;
}

static const char *process_string(Parser *P, const SptToken *t, int *out_len) {
  const char *content = t->lexeme + 1; /* 跳过开引号 */
  int clen = t->length - 2;            /* 去掉两端引号 */
  if (clen < 0)
    clen = 0;
  /* 最坏情况：每个字符都转义成多字节；按 4 倍预估上限。 */
  size_t cap = (size_t)clen * 4 + 1;
  char *out = (char *)spt_arena_alloc(P->arena, cap);
  int oi = 0;
  for (int i = 0; i < clen; i++) {
    char c = content[i];
    if (c == '\\' && i + 1 < clen) {
      i++;
      char e = content[i];
      switch (e) {
      case 'n': out[oi++] = '\n'; break;
      case 't': out[oi++] = '\t'; break;
      case 'b': out[oi++] = '\b'; break;
      case 'f': out[oi++] = '\f'; break;
      case 'r': out[oi++] = '\r'; break;
      case '\\': out[oi++] = '\\'; break;
      case '\'': out[oi++] = '\''; break;
      case '"': out[oi++] = '"'; break;
      case 'u': {
        /* \u{HEX+} —— 至少需要 "{...}" 两个字符 */
        if (i + 1 < clen && content[i + 1] == '{') {
          int j = i + 2;
          uint32_t cp = 0;
          int digits = 0;
          while (j < clen && content[j] != '}') {
            int v = hex_digit_value(content[j]);
            if (v < 0) break;
            cp = (cp << 4) | (uint32_t)v;
            digits++;
            j++;
            if (digits > 8) break; /* 防溢出 */
          }
          if (j < clen && content[j] == '}' && digits > 0) {
            char ub[4];
            int un = utf8_encode(cp, ub);
            if (un > 0) {
              memcpy(out + oi, ub, (size_t)un);
              oi += un;
              i = j; /* 消费到 '}' */
              break;
            }
          }
          /* 非法 \u{...}：原样透传 */
          out[oi++] = '\\';
          out[oi++] = 'u';
          break;
        }
        out[oi++] = '\\';
        out[oi++] = e;
        break;
      }
      case 'x': {
        /* \xHH —— 恰好两个十六进制数字 */
        if (i + 2 < clen) {
          int hi = hex_digit_value(content[i + 1]);
          int lo = hex_digit_value(content[i + 2]);
          if (hi >= 0 && lo >= 0) {
            out[oi++] = (char)((hi << 4) | lo);
            i += 2;
            break;
          }
        }
        /* 非法 \x：原样透传 */
        out[oi++] = '\\';
        out[oi++] = 'x';
        break;
      }
      default:
        out[oi++] = '\\';
        out[oi++] = e;
        break;
      }
    } else {
      out[oi++] = c;
    }
  }
  out[oi] = '\0';
  if (out_len)
    *out_len = oi;
  return out;
}

/* 构造字符串字面量节点（data 直接取 raw，不反转义；用于 ident/int/float 键）。 */
static AstNode *make_raw_string_node(Parser *P, const char *data, int len, SourceLocation loc) {
  AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_STRING, loc);
  char *copy = spt_arena_strndup(P->arena, data, (size_t)len);
  n->u.lit_str.data = copy;
  n->u.lit_str.len = len;
  return n;
}

/* ===========================================================================
 * 类型解析
 * ========================================================================= */
static int is_type_start(SptTokenKind k) {
  switch (k) {
  case TOK_INT:
  case TOK_FLOAT:
  case TOK_NUMBER:
  case TOK_STR:
  case TOK_BOOL:
  case TOK_VOID:
  case TOK_NULL:
  case TOK_COROUTINE:
  case TOK_FUNCTION:
  case TOK_ANY:
  case TOK_LIST:
  case TOK_MAP:
  case TOK_IDENTIFIER:
    return 1;
  default:
    return 0;
  }
}

static AstNode *make_any_type(Parser *P, SourceLocation loc) {
  return spt_ast_new(P->arena, NODE_TYPE_ANY, loc);
}

static AstNode *parse_type(Parser *P) {
  SourceLocation loc = cur_loc(P);
  SptTokenKind k = cur_kind(P);
  switch (k) {
  case TOK_INT:
  case TOK_FLOAT:
  case TOK_NUMBER:
  case TOK_STR:
  case TOK_BOOL:
  case TOK_VOID:
  case TOK_NULL: {
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_TYPE_PRIMITIVE, loc);
    PrimitiveTypeKind pk = PTK_INT;
    switch (k) {
    case TOK_INT: pk = PTK_INT; break;
    case TOK_FLOAT: pk = PTK_FLOAT; break;
    case TOK_NUMBER: pk = PTK_NUMBER; break;
    case TOK_STR: pk = PTK_STRING; break;
    case TOK_BOOL: pk = PTK_BOOL; break;
    case TOK_VOID: pk = PTK_VOID; break;
    case TOK_NULL: pk = PTK_NULL; break;
    default: break;
    }
    n->u.type_prim.kind = pk;
    return n;
  }
  case TOK_COROUTINE:
    advance(P);
    return spt_ast_new(P->arena, NODE_TYPE_COROUTINE_KW, loc);
  case TOK_FUNCTION:
    advance(P);
    return spt_ast_new(P->arena, NODE_TYPE_FUNCTION_KW, loc);
  case TOK_ANY:
    advance(P);
    return spt_ast_new(P->arena, NODE_TYPE_ANY, loc);
  case TOK_LIST: {
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_TYPE_LIST, loc);
    if (accept(P, TOK_LT)) {
      n->u.type_list.element = parse_type(P);
      expect2(P, TOK_GT);
    } else {
      n->u.type_list.element = make_any_type(P, loc); /* 裸 list -> 元素 any（复刻 visitor） */
    }
    return n;
  }
  case TOK_MAP: {
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_TYPE_MAP, loc);
    if (accept(P, TOK_LT)) {
      n->u.type_map.key = parse_type(P);
      expect2(P, TOK_COMMA);
      n->u.type_map.value = parse_type(P);
      expect2(P, TOK_GT);
    } else {
      n->u.type_map.key = make_any_type(P, loc);
      n->u.type_map.value = make_any_type(P, loc);
    }
    return n;
  }
  case TOK_IDENTIFIER: {
    /* 用户类型：限定名 IDENT (. IDENT)* */
    int cap = 4, cnt = 0;
    const char **parts = (const char **)malloc(sizeof(char *) * (size_t)cap);
    char buf[128];
    parts[cnt++] = spt_arena_strdup(P->arena, lexeme_cstr(P, cur(P), buf, sizeof(buf)));
    advance(P);
    while (at(P, TOK_DOT) && peek_kind(P, 1) == TOK_IDENTIFIER) {
      advance(P); /* . */
      if (cnt >= cap) {
        cap *= 2;
        parts = (const char **)realloc(parts, sizeof(char *) * (size_t)cap);
      }
      parts[cnt++] = spt_arena_strdup(P->arena, lexeme_cstr(P, cur(P), buf, sizeof(buf)));
      advance(P);
    }
    AstNode *n = spt_ast_new(P->arena, NODE_TYPE_USER, loc);
    const char **arr = (const char **)spt_arena_alloc(P->arena, sizeof(char *) * (size_t)cnt);
    memcpy(arr, parts, sizeof(char *) * (size_t)cnt);
    free(parts);
    n->u.type_user.parts = arr;
    n->u.type_user.count = cnt;
    return n;
  }
  default:
    error_at(P, cur(P), "%s", "语法错误：期望类型");
    return spt_ast_new(P->arena, NODE_TYPE_ANY, loc);
  }
}

/* ===========================================================================
 * 表达式解析（优先级爬升）
 * ========================================================================= */
typedef struct {
  OperatorKind op;
  int prec; /* -1 表示非二元运算符 */
  int ntok; /* 消耗的 token 数（'>>' 为 2） */
} BinOp;

static BinOp peek_binary_op(Parser *P) {
  BinOp r;
  r.op = OPK_ADD;
  r.prec = -1;
  r.ntok = 1;
  switch (cur_kind(P)) {
  case TOK_OR:      r.op = OPK_OR;       r.prec = 1;  break;
  case TOK_AND:     r.op = OPK_AND;      r.prec = 2;  break;
  case TOK_BIT_OR:  r.op = OPK_BW_OR;    r.prec = 3;  break;
  case TOK_BIT_XOR: r.op = OPK_BW_XOR;   r.prec = 4;  break;
  case TOK_BIT_AND: r.op = OPK_BW_AND;   r.prec = 5;  break;
  case TOK_EQ:      r.op = OPK_EQ;       r.prec = 6;  break;
  case TOK_NEQ:     r.op = OPK_NE;       r.prec = 6;  break;
  case TOK_LT:      r.op = OPK_LT;       r.prec = 7;  break;
  case TOK_LTE:     r.op = OPK_LE;       r.prec = 7;  break;
  case TOK_GTE:     r.op = OPK_GE;       r.prec = 7;  break;
  case TOK_GT:
    if (peek_kind(P, 1) == TOK_GT) { /* '>>' 右移 */
      r.op = OPK_BW_RSHIFT;
      r.prec = 8;
      r.ntok = 2;
    } else {
      r.op = OPK_GT;
      r.prec = 7;
    }
    break;
  case TOK_LSHIFT:  r.op = OPK_BW_LSHIFT; r.prec = 8;  break;
  case TOK_CONCAT:  r.op = OPK_CONCAT;    r.prec = 9;  break;
  case TOK_ADD:     r.op = OPK_ADD;       r.prec = 10; break;
  case TOK_SUB:     r.op = OPK_SUB;       r.prec = 10; break;
  case TOK_MUL:     r.op = OPK_MUL;       r.prec = 11; break;
  case TOK_DIV:     r.op = OPK_DIV;       r.prec = 11; break;
  case TOK_IDIV:    r.op = OPK_IDIV;      r.prec = 11; break;
  case TOK_MOD:     r.op = OPK_MOD;       r.prec = 11; break;
  default: break;
  }
  return r;
}

static AstNode *parse_binary(Parser *P, int min_prec) {
  AstNode *left = parse_unary(P);
  for (;;) {
    BinOp b = peek_binary_op(P);
    if (b.prec < min_prec)
      break;
    SourceLocation loc = cur_loc(P);
    for (int i = 0; i < b.ntok; i++)
      advance(P);
    /* CONCAT (..) 为右结合：同级运算符继续向右递归（min = prec）；
     * 其它运算符为左结合：右侧最小优先级 = prec + 1。 */
    int next_min = (b.op == OPK_CONCAT) ? b.prec : b.prec + 1;
    AstNode *right = parse_binary(P, next_min);
    AstNode *n = spt_ast_new(P->arena, NODE_BINARY_OP, loc);
    n->u.binary.op = b.op;
    n->u.binary.left = left;
    n->u.binary.right = right;
    left = n;
  }
  return left;
}

static AstNode *parse_expression(Parser *P) { return parse_binary(P, 1); }

static AstNode *parse_unary(Parser *P) {
  SptTokenKind k = cur_kind(P);
  OperatorKind op;
  int is_un = 1;
  switch (k) {
  case TOK_NOT: op = OPK_NOT; break;
  case TOK_SUB: op = OPK_NEGATE; break; /* 一元负号 -> NEGATE */
  case TOK_LEN: op = OPK_LENGTH; break;
  case TOK_BIT_NOT: op = OPK_BW_NOT; break;
  default: is_un = 0; op = OPK_NOT; break;
  }
  if (is_un) {
    SourceLocation loc = cur_loc(P);
    /* 特殊处理: 一元负号 + 整数字面量 -> 合并为负整数字面量。
     * 解决 -9223372036854775808 (INT64_MIN) 无法直接表示的问题:
     * 9223372036854775808 超过 LLONG_MAX, strtoll 会截断;
     * 用 strtoull 解析后 0u - value 得到正确的负值（与标准 Lua l_str2int 一致）。 */
    if (k == TOK_SUB && peek_kind(P, 1) == TOK_INTEGER) {
      const SptToken *minus_t = cur(P);
      advance(P); /* 消费 '-' */
      const SptToken *t = cur(P);
      char buf[64];
      const char *s = lexeme_cstr(P, t, buf, sizeof(buf));
      advance(P); /* 消费整数 */
      errno = 0;
      unsigned long long u;
      if (t->length > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        u = strtoull(s, NULL, 16);
      else
        u = strtoull(s, NULL, 10);
      if (errno == ERANGE)
        error_at(P, minus_t, "无效或越界的整数常量 '-%s'", s);
      AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_INT, loc);
      n->u.lit_int.value = (int64_t)(0u - u);
      return n;
    }
    advance(P);
    AstNode *operand = parse_unary(P); /* 右结合 */
    AstNode *n = spt_ast_new(P->arena, NODE_UNARY_OP, loc);
    n->u.unary.op = op;
    n->u.unary.operand = operand;
    return n;
  }
  return parse_postfix(P);
}

static AstNode *parse_postfix(Parser *P) {
  AstNode *e = parse_primary(P);
  for (;;) {
    SptTokenKind k = cur_kind(P);
    if (k == TOK_LBRACKET) {
      SourceLocation loc = cur_loc(P);
      advance(P);
      AstNode *idx = parse_expression(P);
      expect2(P, TOK_RBRACKET);
      AstNode *n = spt_ast_new(P->arena, NODE_INDEX_ACCESS, loc);
      n->u.index.array = e;
      n->u.index.index = idx;
      e = n;
    } else if (k == TOK_DOT) {
      SourceLocation loc = cur_loc(P);
      advance(P);
      const SptToken *id = expect2(P, TOK_IDENTIFIER);
      AstNode *n = spt_ast_new(P->arena, NODE_MEMBER_ACCESS, loc);
      n->u.member.object = e;
      n->u.member.member = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
      e = n;
    } else if (k == TOK_LPAREN) {
      SourceLocation loc = cur_loc(P);
      advance(P);
      AstList args = spt_ast_list_empty();
      if (!at(P, TOK_RPAREN))
        args = parse_expression_list(P);
      expect2(P, TOK_RPAREN);
      AstNode *n = spt_ast_new(P->arena, NODE_FUNCTION_CALL, loc);
      n->u.call.func = e;
      n->u.call.args = args;
      e = n;
    } else {
      break;
    }
  }
  return e;
}

static AstList parse_expression_list(Parser *P) {
  NodeVec v;
  nv_init(&v);
  nv_push(&v, parse_expression(P));
  while (accept(P, TOK_COMMA))
    nv_push(&v, parse_expression(P));
  return nv_finish(&v, P->arena);
}

/* 列表字面量 [ exprList? ] */
static AstNode *parse_list_literal(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_LBRACKET);
  AstList elems = spt_ast_list_empty();
  if (!at(P, TOK_RBRACKET))
    elems = parse_expression_list(P);
  expect2(P, TOK_RBRACKET);
  AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_LIST, loc);
  n->u.lit_list.elements = elems;
  return n;
}

/* 单个 map 条目（5 种键形式） */
static AstNode *parse_map_entry(Parser *P) {
  SourceLocation loc = cur_loc(P);
  AstNode *key = NULL;
  SptTokenKind k = cur_kind(P);
  char buf[256];
  if (k == TOK_IDENTIFIER) {
    const SptToken *t = cur(P);
    key = make_raw_string_node(P, t->lexeme, t->length, loc_of(t));
    advance(P);
    expect2(P, TOK_COLON);
  } else if (k == TOK_LBRACKET) {
    advance(P);
    key = parse_expression(P);
    expect2(P, TOK_RBRACKET);
    expect2(P, TOK_COLON);
  } else if (k == TOK_STRING_LITERAL) {
    const SptToken *t = cur(P);
    int len = 0;
    const char *s = process_string(P, t, &len);
    key = make_raw_string_node(P, s, len, loc_of(t));
    advance(P);
    expect2(P, TOK_COLON);
  } else if (k == TOK_INTEGER || k == TOK_FLOAT_LITERAL) {
    /* 复刻 visitor：整数/浮点键转为字符串字面量，取原始词素 */
    const SptToken *t = cur(P);
    const char *raw = lexeme_cstr(P, t, buf, sizeof(buf));
    key = make_raw_string_node(P, raw, (int)strlen(raw), loc_of(t));
    advance(P);
    expect2(P, TOK_COLON);
  } else {
    error_at(P, cur(P), "%s", "语法错误：期望 map 键");
    key = make_raw_string_node(P, "", 0, loc);
  }
  AstNode *val = parse_expression(P);
  AstNode *n = spt_ast_new(P->arena, NODE_MAP_ENTRY, loc);
  n->u.map_entry.key = key;
  n->u.map_entry.value = val;
  return n;
}

/* map 字面量 { entryList? } */
static AstNode *parse_map_literal(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_LBRACE);
  NodeVec v;
  nv_init(&v);
  if (!at(P, TOK_RBRACE)) {
    nv_push(&v, parse_map_entry(P));
    while (accept(P, TOK_COMMA))
      nv_push(&v, parse_map_entry(P));
  }
  expect2(P, TOK_RBRACE);
  AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_MAP, loc);
  n->u.lit_map.entries = nv_finish(&v, P->arena);
  return n;
}

static AstNode *parse_primary(Parser *P) {
  const SptToken *t = cur(P);
  SourceLocation loc = loc_of(t);
  char buf[256];
  switch (t->kind) {
  case TOK_NULL:
    advance(P);
    return spt_ast_new(P->arena, NODE_LITERAL_NULL, loc);
  case TOK_TRUE: {
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_BOOL, loc);
    n->u.lit_bool.value = true;
    return n;
  }
  case TOK_FALSE: {
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_BOOL, loc);
    n->u.lit_bool.value = false;
    return n;
  }
  case TOK_INTEGER: {
    const char *s = lexeme_cstr(P, t, buf, sizeof(buf));
    advance(P);
    errno = 0;
    long long v;
    /* 0x/0X 前缀（长度>2）按 16 进制；否则 10 进制（复刻 visitor） */
    if (t->length > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
      v = strtoll(s, NULL, 16);
    else
      v = strtoll(s, NULL, 10);
    if (errno == ERANGE)
      error_at(P, t, "无效或越界的整数常量 '%s'", s);
    AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_INT, loc);
    n->u.lit_int.value = (int64_t)v;
    return n;
  }
  case TOK_FLOAT_LITERAL: {
    const char *s = lexeme_cstr(P, t, buf, sizeof(buf));
    advance(P);
    errno = 0;
    double v = strtod(s, NULL); /* 含十六进制浮点 */
    if (errno == ERANGE)
      error_at(P, t, "无效或越界的浮点常量 '%s'", s);
    AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_FLOAT, loc);
    n->u.lit_float.value = v;
    return n;
  }
  case TOK_STRING_LITERAL: {
    int len = 0;
    const char *s = process_string(P, t, &len);
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_LITERAL_STRING, loc);
    n->u.lit_str.data = s;
    n->u.lit_str.len = len;
    return n;
  }
  case TOK_IDENTIFIER: {
    advance(P);
    AstNode *n = spt_ast_new(P->arena, NODE_IDENTIFIER, loc);
    n->u.ident.name = spt_arena_strndup(P->arena, t->lexeme, (size_t)t->length);
    return n;
  }
  case TOK_ELLIPSIS:
    advance(P);
    return spt_ast_new(P->arena, NODE_VAR_ARGS, loc);
  case TOK_LPAREN: {
    advance(P);
    AstNode *e = parse_expression(P);
    expect2(P, TOK_RPAREN);
    return e; /* 圆括号仅分组，无独立节点 */
  }
  case TOK_LBRACKET:
    return parse_list_literal(P);
  case TOK_LBRACE:
    return parse_map_literal(P);
  case TOK_FUNCTION:
    return parse_lambda(P);
  default:
    error_at(P, t, "语法错误：期望表达式，却遇到 %s", spt_token_name(t->kind));
    advance(P); /* 跳过以推进 */
    return spt_ast_new(P->arena, NODE_LITERAL_NULL, loc);
  }
}

/* ===========================================================================
 * 参数列表
 * ========================================================================= */
/* parameter: type IDENTIFIER */
static AstNode *parse_parameter(Parser *P) {
  SourceLocation loc = cur_loc(P);
  AstNode *type = parse_type(P);
  const SptToken *id = expect2(P, TOK_IDENTIFIER);
  AstNode *n = spt_ast_new(P->arena, NODE_PARAMETER_DECL, loc);
  n->u.param.name = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
  n->u.param.type_annotation = type;
  return n;
}

/* parameterList: parameter (COMMA parameter)* (COMMA DDD)? | DDD
** 已定位在 '(' 之后；解析到 ')' 之前。*out_variadic 置变参标志。 */
static AstList parse_parameter_list(Parser *P, int *out_variadic) {
  NodeVec v;
  nv_init(&v);
  *out_variadic = 0;
  if (at(P, TOK_RPAREN))
    return nv_finish(&v, P->arena);
  if (at(P, TOK_ELLIPSIS)) {
    advance(P);
    *out_variadic = 1;
    return nv_finish(&v, P->arena);
  }
  nv_push(&v, parse_parameter(P));
  while (accept(P, TOK_COMMA)) {
    if (at(P, TOK_ELLIPSIS)) {
      advance(P);
      *out_variadic = 1;
      break;
    }
    nv_push(&v, parse_parameter(P));
  }
  return nv_finish(&v, P->arena);
}

/* lambda: FUNCTION OP parameterList? CP (ARROW (type | VARS))? blockStatement
 * 返回类型注解可选：省略时 return_type 为 NULL（codegen 不依赖该字段，
 * 仅用于 LSP/类型检查显示）。降低单表达式匿名函数的书写摩擦。 */
static AstNode *parse_lambda(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_FUNCTION);
  expect2(P, TOK_LPAREN);
  int variadic = 0;
  AstList params = parse_parameter_list(P, &variadic);
  expect2(P, TOK_RPAREN);
  AstNode *ret = NULL;
  if (at(P, TOK_ARROW)) {
    advance(P);
    if (at(P, TOK_VARS)) {
      SourceLocation vloc = cur_loc(P);
      advance(P);
      ret = spt_ast_new(P->arena, NODE_TYPE_MULTIRETURN, vloc);
    } else {
      ret = parse_type(P);
    }
  }
  AstNode *body = parse_block(P);
  AstNode *n = spt_ast_new(P->arena, NODE_LAMBDA, loc);
  n->u.lambda.params = params;
  n->u.lambda.return_type = ret;
  n->u.lambda.body = body;
  n->u.lambda.is_variadic = variadic != 0;
  return n;
}

/* ===========================================================================
 * 声明判定（有界前瞻，无副作用）
 * ========================================================================= */
static int looks_like_declaration(Parser *P) {
  int i = P->pos;
  SptTokenKind k0 = tok_at(P, i)->kind;
  if (k0 == TOK_EXPORT)
    return 1; /* export 只前缀声明 */
  switch (k0) {
  case TOK_GLOBAL:
  case TOK_CONST:
  case TOK_VARS:
  case TOK_CLASS:
  case TOK_STATIC:
  case TOK_INT:
  case TOK_FLOAT:
  case TOK_NUMBER:
  case TOK_STR:
  case TOK_BOOL:
  case TOK_VOID:
  case TOK_COROUTINE:
  case TOK_LIST:
  case TOK_MAP:
  case TOK_ANY:
    return 1;
  case TOK_AUTO:
    return tok_at(P, i + 1)->kind == TOK_IDENTIFIER;
  case TOK_FUNCTION:
    return tok_at(P, i + 1)->kind == TOK_IDENTIFIER; /* 否则是 lambda */
  case TOK_NULL:
    return tok_at(P, i + 1)->kind == TOK_IDENTIFIER; /* 否则是 null 字面量 */
  case TOK_IDENTIFIER: {
    int j = i + 1;
    while (tok_at(P, j)->kind == TOK_DOT && tok_at(P, j + 1)->kind == TOK_IDENTIFIER)
      j += 2;
    return tok_at(P, j)->kind == TOK_IDENTIFIER; /* 类型后跟变量名 -> 声明 */
  }
  default:
    return 0;
  }
}

/* ===========================================================================
 * 赋值 / 更新 / 表达式语句（Approach A：先解析表达式再分类）
 * ========================================================================= */
static int is_lvalue_node(const AstNode *n) {
  return n->type == NODE_IDENTIFIER || n->type == NODE_MEMBER_ACCESS ||
         n->type == NODE_INDEX_ACCESS;
}

static int compound_op(SptTokenKind k, OperatorKind *op) {
  switch (k) {
  case TOK_ADD_ASSIGN: *op = OPK_ASSIGN_ADD; return 1;
  case TOK_SUB_ASSIGN: *op = OPK_ASSIGN_SUB; return 1;
  case TOK_MUL_ASSIGN: *op = OPK_ASSIGN_MUL; return 1;
  case TOK_DIV_ASSIGN: *op = OPK_ASSIGN_DIV; return 1;
  case TOK_IDIV_ASSIGN: *op = OPK_ASSIGN_IDIV; return 1;
  case TOK_MOD_ASSIGN: *op = OPK_ASSIGN_MOD; return 1;
  case TOK_CONCAT_ASSIGN: *op = OPK_ASSIGN_CONCAT; return 1;
  default: return 0;
  }
}

static AstNode *parse_simple_statement(Parser *P) {
  SourceLocation loc = cur_loc(P);
  AstNode *e = parse_expression(P);
  SptTokenKind k = cur_kind(P);
  OperatorKind cop;

  if (k == TOK_ASSIGN || k == TOK_COMMA) {
    /* 普通赋值 */
    NodeVec lhs, rhs;
    nv_init(&lhs);
    nv_init(&rhs);
    nv_push(&lhs, e);
    while (accept(P, TOK_COMMA))
      nv_push(&lhs, parse_expression(P));
    expect2(P, TOK_ASSIGN);
    nv_push(&rhs, parse_expression(P));
    while (accept(P, TOK_COMMA))
      nv_push(&rhs, parse_expression(P));
    expect2(P, TOK_SEMICOLON);
    for (int i = 0; i < lhs.count; i++) {
      if (!is_lvalue_node(lhs.data[i])) {
        error_at(P, cur(P), "%s", "无效的赋值目标");
        break;
      }
    }
    AstNode *n = spt_ast_new(P->arena, NODE_ASSIGNMENT, loc);
    n->u.assign.lvalues = nv_finish(&lhs, P->arena);
    n->u.assign.rvalues = nv_finish(&rhs, P->arena);
    return n;
  } else if (compound_op(k, &cop)) {
    advance(P);
    AstNode *rv = parse_expression(P);
    expect2(P, TOK_SEMICOLON);
    if (!is_lvalue_node(e))
      error_at(P, cur(P), "%s", "无效的更新赋值目标");
    AstNode *n = spt_ast_new(P->arena, NODE_UPDATE_ASSIGNMENT, loc);
    n->u.update.op = cop;
    n->u.update.lvalue = e;
    n->u.update.rvalue = rv;
    return n;
  } else {
    expect2(P, TOK_SEMICOLON);
    AstNode *n = spt_ast_new(P->arena, NODE_EXPRESSION_STATEMENT, loc);
    n->u.expr_stmt.expr = e;
    return n;
  }
}

/* ===========================================================================
 * 声明解析
 * ========================================================================= */
/* 解析限定标识符为点号连接的名字字符串（如 "Foo.bar"）。 */
static const char *parse_qualified_name(Parser *P) {
  const SptToken *first = expect2(P, TOK_IDENTIFIER);
  /* 先估算总长 */
  int total = first->length;
  int save = P->pos;
  while (tok_at(P, P->pos)->kind == TOK_DOT && tok_at(P, P->pos + 1)->kind == TOK_IDENTIFIER) {
    total += 1 + tok_at(P, P->pos + 1)->length;
    P->pos += 2;
  }
  P->pos = save; /* 回退，正式拼接 */
  char *buf = (char *)spt_arena_alloc(P->arena, (size_t)total + 1);
  int oi = 0;
  memcpy(buf + oi, first->lexeme, (size_t)first->length);
  oi += first->length;
  while (tok_at(P, P->pos)->kind == TOK_DOT && tok_at(P, P->pos + 1)->kind == TOK_IDENTIFIER) {
    advance(P); /* . */
    buf[oi++] = '.';
    const SptToken *seg = cur(P);
    memcpy(buf + oi, seg->lexeme, (size_t)seg->length);
    oi += seg->length;
    advance(P);
  }
  buf[oi] = '\0';
  return buf;
}

static AstNode *parse_block(Parser *P);

/* 类声明：CLASS IDENTIFIER { classMember* } */
static AstNode *parse_class_decl(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_CLASS);
  const SptToken *name = expect2(P, TOK_IDENTIFIER);
  expect2(P, TOK_LBRACE);
  NodeVec members;
  nv_init(&members);
  while (!at(P, TOK_RBRACE) && !at_end(P)) {
    if (accept(P, TOK_SEMICOLON))
      continue; /* 空成员 */
    SourceLocation mloc = cur_loc(P);
    int is_static = accept(P, TOK_STATIC) ? 1 : 0;
    int is_const = accept(P, TOK_CONST) ? 1 : 0;
    AstNode *inner = NULL;
    if (at(P, TOK_VARS)) {
      /* 多返回方法：VARS IDENT ( params ) block */
      advance(P);
      const SptToken *mn = expect2(P, TOK_IDENTIFIER);
      expect2(P, TOK_LPAREN);
      int variadic = 0;
      AstList params = parse_parameter_list(P, &variadic);
      expect2(P, TOK_RPAREN);
      AstNode *body = parse_block(P);
      inner = spt_ast_new(P->arena, NODE_FUNCTION_DECL, mloc);
      inner->u.func_decl.name = spt_arena_strndup(P->arena, mn->lexeme, (size_t)mn->length);
      inner->u.func_decl.params = params;
      inner->u.func_decl.return_type = spt_ast_new(P->arena, NODE_TYPE_MULTIRETURN, mloc);
      inner->u.func_decl.body = body;
      inner->u.func_decl.is_static = is_static != 0;
      inner->u.func_decl.is_const = is_const != 0;
      inner->u.func_decl.is_variadic = variadic != 0;
    } else if (at(P, TOK_AUTO)) {
      /* 字段：auto IDENT (= expr)? */
      SourceLocation tl = cur_loc(P);
      advance(P);
      AstNode *type = spt_ast_new(P->arena, NODE_TYPE_AUTO, tl);
      const SptToken *fn = expect2(P, TOK_IDENTIFIER);
      AstNode *init = NULL;
      if (accept(P, TOK_ASSIGN))
        init = parse_expression(P);
      expect2(P, TOK_SEMICOLON);
      inner = spt_ast_new(P->arena, NODE_VARIABLE_DECL, mloc);
      inner->u.var_decl.name = spt_arena_strndup(P->arena, fn->lexeme, (size_t)fn->length);
      inner->u.var_decl.type_annotation = type;
      inner->u.var_decl.initializer = init;
      inner->u.var_decl.is_static = is_static != 0;
      inner->u.var_decl.is_const = is_const != 0;
    } else {
      /* type IDENT ... -> 方法或字段 */
      AstNode *type = parse_type(P);
      const SptToken *mn = expect2(P, TOK_IDENTIFIER);
      if (at(P, TOK_LPAREN)) {
        /* 方法 */
        advance(P);
        int variadic = 0;
        AstList params = parse_parameter_list(P, &variadic);
        expect2(P, TOK_RPAREN);
        AstNode *body = parse_block(P);
        inner = spt_ast_new(P->arena, NODE_FUNCTION_DECL, mloc);
        inner->u.func_decl.name = spt_arena_strndup(P->arena, mn->lexeme, (size_t)mn->length);
        inner->u.func_decl.params = params;
        inner->u.func_decl.return_type = type;
        inner->u.func_decl.body = body;
        inner->u.func_decl.is_static = is_static != 0;
        inner->u.func_decl.is_const = is_const != 0;
        inner->u.func_decl.is_variadic = variadic != 0;
      } else {
        /* 字段：type IDENT (= expr)? ; */
        AstNode *init = NULL;
        if (accept(P, TOK_ASSIGN))
          init = parse_expression(P);
        expect2(P, TOK_SEMICOLON);
        inner = spt_ast_new(P->arena, NODE_VARIABLE_DECL, mloc);
        inner->u.var_decl.name = spt_arena_strndup(P->arena, mn->lexeme, (size_t)mn->length);
        inner->u.var_decl.type_annotation = type;
        inner->u.var_decl.initializer = init;
        inner->u.var_decl.is_static = is_static != 0;
        inner->u.var_decl.is_const = is_const != 0;
      }
    }
    AstNode *cm = spt_ast_new(P->arena, NODE_CLASS_MEMBER, mloc);
    cm->u.class_member.member_declaration = inner;
    cm->u.class_member.is_static = is_static != 0;
    nv_push(&members, cm);
    if (P->panic)
      synchronize(P);
  }
  expect2(P, TOK_RBRACE);
  AstNode *n = spt_ast_new(P->arena, NODE_CLASS_DECL, loc);
  n->u.class_decl.name = spt_arena_strndup(P->arena, name->lexeme, (size_t)name->length);
  n->u.class_decl.members = nv_finish(&members, P->arena);
  return n;
}

/* 多变量声明：VARS GLOBAL? CONST? IDENT (COMMA GLOBAL? CONST? IDENT)* (= expr)? ; */
static AstNode *parse_multi_var_decl(Parser *P, SourceLocation loc) {
  expect2(P, TOK_VARS);
  int cap = 4, cnt = 0;
  MultiDeclVar *vars = (MultiDeclVar *)malloc(sizeof(MultiDeclVar) * (size_t)cap);
  for (;;) {
    int g = accept(P, TOK_GLOBAL) ? 1 : 0;
    int c = accept(P, TOK_CONST) ? 1 : 0;
    const SptToken *id = expect2(P, TOK_IDENTIFIER);
    if (cnt >= cap) {
      cap *= 2;
      vars = (MultiDeclVar *)realloc(vars, sizeof(MultiDeclVar) * (size_t)cap);
    }
    vars[cnt].name = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
    vars[cnt].is_global = g != 0;
    vars[cnt].is_const = c != 0;
    cnt++;
    if (!accept(P, TOK_COMMA))
      break;
  }
  AstNode *init = NULL;
  if (accept(P, TOK_ASSIGN))
    init = parse_expression(P);
  expect2(P, TOK_SEMICOLON);
  MultiDeclVar *arr = (MultiDeclVar *)spt_arena_alloc(P->arena, sizeof(MultiDeclVar) * (size_t)cnt);
  memcpy(arr, vars, sizeof(MultiDeclVar) * (size_t)cnt);
  free(vars);
  AstNode *n = spt_ast_new(P->arena, NODE_MUTI_VARIABLE_DECL, loc);
  n->u.muti_var.vars = arr;
  n->u.muti_var.count = cnt;
  n->u.muti_var.initializer = init;
  return n;
}

/* VARS 之后是否为函数声明（VARS qualifiedIdent '('）。 */
static int vars_is_function(Parser *P) {
  int j = P->pos + 1; /* 跳过 VARS */
  if (tok_at(P, j)->kind == TOK_GLOBAL || tok_at(P, j)->kind == TOK_CONST)
    return 0; /* vars global/const ... -> 多变量 */
  if (tok_at(P, j)->kind != TOK_IDENTIFIER)
    return 0;
  j++;
  while (tok_at(P, j)->kind == TOK_DOT && tok_at(P, j + 1)->kind == TOK_IDENTIFIER)
    j += 2;
  return tok_at(P, j)->kind == TOK_LPAREN;
}

/* 函数声明（VARS 多返回形式）：VARS qualifiedIdent ( params ) block */
static AstNode *parse_func_decl_vars(Parser *P, SourceLocation loc, int is_global, int is_const) {
  expect2(P, TOK_VARS);
  const char *name = parse_qualified_name(P);
  expect2(P, TOK_LPAREN);
  int variadic = 0;
  AstList params = parse_parameter_list(P, &variadic);
  expect2(P, TOK_RPAREN);
  AstNode *body = parse_block(P);
  AstNode *n = spt_ast_new(P->arena, NODE_FUNCTION_DECL, loc);
  n->u.func_decl.name = name;
  n->u.func_decl.params = params;
  n->u.func_decl.return_type = spt_ast_new(P->arena, NODE_TYPE_MULTIRETURN, loc);
  n->u.func_decl.body = body;
  n->u.func_decl.is_global = is_global != 0;
  n->u.func_decl.is_const = is_const != 0;
  n->u.func_decl.is_variadic = variadic != 0;
  return n;
}

/* 变量或函数声明（已消费 export，处理 global/const + 类型 + 名字）。 */
static AstNode *parse_var_or_func(Parser *P, SourceLocation loc) {
  int is_global = accept(P, TOK_GLOBAL) ? 1 : 0;
  int is_const = accept(P, TOK_CONST) ? 1 : 0;

  if (at(P, TOK_VARS)) {
    if (vars_is_function(P))
      return parse_func_decl_vars(P, loc, is_global, is_const);
    /* 多变量声明（注意：grammar 中 vars 多变量无外层 global/const） */
    return parse_multi_var_decl(P, loc);
  }

  if (at(P, TOK_AUTO)) {
    /* 变量：auto IDENT (= expr)? ; */
    SourceLocation tl = cur_loc(P);
    advance(P);
    AstNode *type = spt_ast_new(P->arena, NODE_TYPE_AUTO, tl);
    const SptToken *id = expect2(P, TOK_IDENTIFIER);
    AstNode *init = NULL;
    if (accept(P, TOK_ASSIGN))
      init = parse_expression(P);
    expect2(P, TOK_SEMICOLON);
    AstNode *n = spt_ast_new(P->arena, NODE_VARIABLE_DECL, loc);
    n->u.var_decl.name = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
    n->u.var_decl.type_annotation = type;
    n->u.var_decl.initializer = init;
    n->u.var_decl.is_global = is_global != 0;
    n->u.var_decl.is_const = is_const != 0;
    return n;
  }

  /* 有显式类型：可能是变量或函数 */
  AstNode *type = parse_type(P);
  const SptToken *id = expect2(P, TOK_IDENTIFIER);

  if (at(P, TOK_DOT) || at(P, TOK_LPAREN)) {
    /* 函数声明：type qualifiedIdent ( params ) block。
    ** 名字可能是点号限定：从已读的 id 起继续拼接。 */
    int total = id->length;
    int save = P->pos;
    while (tok_at(P, P->pos)->kind == TOK_DOT && tok_at(P, P->pos + 1)->kind == TOK_IDENTIFIER) {
      total += 1 + tok_at(P, P->pos + 1)->length;
      P->pos += 2;
    }
    P->pos = save;
    char *nbuf = (char *)spt_arena_alloc(P->arena, (size_t)total + 1);
    int oi = 0;
    memcpy(nbuf, id->lexeme, (size_t)id->length);
    oi += id->length;
    while (tok_at(P, P->pos)->kind == TOK_DOT && tok_at(P, P->pos + 1)->kind == TOK_IDENTIFIER) {
      advance(P);
      nbuf[oi++] = '.';
      const SptToken *seg = cur(P);
      memcpy(nbuf + oi, seg->lexeme, (size_t)seg->length);
      oi += seg->length;
      advance(P);
    }
    nbuf[oi] = '\0';
    expect2(P, TOK_LPAREN);
    int variadic = 0;
    AstList params = parse_parameter_list(P, &variadic);
    expect2(P, TOK_RPAREN);
    AstNode *body = parse_block(P);
    AstNode *n = spt_ast_new(P->arena, NODE_FUNCTION_DECL, loc);
    n->u.func_decl.name = nbuf;
    n->u.func_decl.params = params;
    n->u.func_decl.return_type = type;
    n->u.func_decl.body = body;
    n->u.func_decl.is_global = is_global != 0;
    n->u.func_decl.is_const = is_const != 0;
    n->u.func_decl.is_variadic = variadic != 0;
    return n;
  }

  /* 变量声明：type IDENT (= expr)? ; */
  AstNode *init = NULL;
  if (accept(P, TOK_ASSIGN))
    init = parse_expression(P);
  expect2(P, TOK_SEMICOLON);
  AstNode *n = spt_ast_new(P->arena, NODE_VARIABLE_DECL, loc);
  n->u.var_decl.name = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
  n->u.var_decl.type_annotation = type;
  n->u.var_decl.initializer = init;
  n->u.var_decl.is_global = is_global != 0;
  n->u.var_decl.is_const = is_const != 0;
  return n;
}

/* declaration: EXPORT? (variableDeclaration ; | functionDeclaration | classDeclaration) */
static AstNode *parse_declaration(Parser *P) {
  SourceLocation loc = cur_loc(P);
  int is_exported = accept(P, TOK_EXPORT) ? 1 : 0;
  AstNode *d;
  if (at(P, TOK_CLASS)) {
    d = parse_class_decl(P);
    d->u.class_decl.is_exported = is_exported != 0;
    d->u.class_decl.is_module_root = (P->scope_depth == 0);
    return d;
  }
  d = parse_var_or_func(P, loc);
  /* 设置 export / is_module_root（按节点类型分派） */
  int mod_root = (P->scope_depth == 0);
  if (d->type == NODE_VARIABLE_DECL) {
    d->u.var_decl.is_exported = is_exported != 0;
    d->u.var_decl.is_module_root = mod_root;
  } else if (d->type == NODE_MUTI_VARIABLE_DECL) {
    d->u.muti_var.is_exported = is_exported != 0;
    d->u.muti_var.is_module_root = mod_root;
  } else if (d->type == NODE_FUNCTION_DECL) {
    d->u.func_decl.is_exported = is_exported != 0;
    d->u.func_decl.is_module_root = mod_root;
  }
  return d;
}

/* ===========================================================================
 * 控制流 / 其它语句
 * ========================================================================= */
static AstNode *parse_if(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_IF);
  expect2(P, TOK_LPAREN);
  AstNode *cond = parse_expression(P);
  expect2(P, TOK_RPAREN);
  AstNode *then_block = parse_body(P);
  NodeVec elifs;
  nv_init(&elifs);
  AstNode *else_block = NULL;
  while (at(P, TOK_ELSE)) {
    if (peek_kind(P, 1) == TOK_IF) {
      SourceLocation cloc = cur_loc(P);
      advance(P); /* else */
      advance(P); /* if */
      expect2(P, TOK_LPAREN);
      AstNode *c = parse_expression(P);
      expect2(P, TOK_RPAREN);
      AstNode *b = parse_body(P);
      AstNode *clause = spt_ast_new(P->arena, NODE_IF_CLAUSE, cloc);
      clause->u.if_clause.condition = c;
      clause->u.if_clause.body = b;
      nv_push(&elifs, clause);
    } else {
      advance(P); /* else */
      else_block = parse_body(P);
      break;
    }
  }
  AstNode *n = spt_ast_new(P->arena, NODE_IF_STATEMENT, loc);
  n->u.if_stmt.condition = cond;
  n->u.if_stmt.then_block = then_block;
  n->u.if_stmt.else_if_clauses = nv_finish(&elifs, P->arena);
  n->u.if_stmt.else_block = else_block;
  return n;
}

static AstNode *parse_while(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_WHILE);
  expect2(P, TOK_LPAREN);
  AstNode *cond = parse_expression(P);
  expect2(P, TOK_RPAREN);
  AstNode *body = parse_body(P);
  AstNode *n = spt_ast_new(P->arena, NODE_WHILE_STATEMENT, loc);
  n->u.while_stmt.condition = cond;
  n->u.while_stmt.body = body;
  return n;
}

/* for 循环变量：(type|auto) IDENT | IDENT，返回 ParameterDecl（type 可空）。 */
static AstNode *parse_for_var(Parser *P) {
  SourceLocation loc = cur_loc(P);
  AstNode *type = NULL;
  if (at(P, TOK_AUTO)) {
    advance(P);
    type = spt_ast_new(P->arena, NODE_TYPE_AUTO, loc);
  } else if (at(P, TOK_IDENTIFIER)) {
    /* 用户类型 then 名字，还是裸名字？前瞻 IDENT(.IDENT)* 后是否再有 IDENT */
    int j = P->pos + 1;
    while (tok_at(P, j)->kind == TOK_DOT && tok_at(P, j + 1)->kind == TOK_IDENTIFIER)
      j += 2;
    if (tok_at(P, j)->kind == TOK_IDENTIFIER)
      type = parse_type(P); /* 有类型 */
    /* 否则裸名字：type 保持 NULL */
  } else if (is_type_start(cur_kind(P))) {
    type = parse_type(P);
  }
  const SptToken *id = expect2(P, TOK_IDENTIFIER);
  AstNode *n = spt_ast_new(P->arena, NODE_PARAMETER_DECL, loc);
  n->u.param.name = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
  n->u.param.type_annotation = type;
  return n;
}

/* 构造 iter(nvars, expr) 的 NODE_FUNCTION_CALL AST 节点。
   用于 for-each 语法糖：右侧单个非 call 表达式自动包成 iter(2, expr)。 */
static AstNode *build_iter_call(Parser *P, SourceLocation loc,
                                int nvars, AstNode *expr) {
  /* func = identifier "iter" */
  AstNode *func = spt_ast_new(P->arena, NODE_IDENTIFIER, loc);
  func->u.ident.name = spt_arena_strndup(P->arena, "iter", 4);
  /* arg1 = literal int nvars */
  AstNode *nvars_lit = spt_ast_new(P->arena, NODE_LITERAL_INT, loc);
  nvars_lit->u.lit_int.value = nvars;
  /* arg2 = expr */
  AstNode **args = (AstNode **)spt_arena_alloc(P->arena, 2 * sizeof(AstNode *));
  args[0] = nvars_lit;
  args[1] = expr;
  AstList arg_list = spt_ast_list_from(P->arena, args, 2);
  /* call node */
  AstNode *call = spt_ast_new(P->arena, NODE_FUNCTION_CALL, loc);
  call->u.call.func = func;
  call->u.call.args = arg_list;
  return call;
}

static AstNode *parse_for(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_FOR);
  expect2(P, TOK_LPAREN);
  AstNode *var0 = parse_for_var(P);
  if (at(P, TOK_ASSIGN)) {
    /* 数值 for */
    advance(P);
    AstNode *start = parse_expression(P);
    expect2(P, TOK_COMMA);
    AstNode *end = parse_expression(P);
    AstNode *step = NULL;
    if (accept(P, TOK_COMMA))
      step = parse_expression(P);
    expect2(P, TOK_RPAREN);
    AstNode *body = parse_body(P);
    AstNode *n = spt_ast_new(P->arena, NODE_FOR_NUMERIC_STATEMENT, loc);
    n->u.for_num.var_name = var0->u.param.name;
    n->u.for_num.type_annotation = var0->u.param.type_annotation;
    n->u.for_num.start = start;
    n->u.for_num.end = end;
    n->u.for_num.step = step;
    n->u.for_num.body = body;
    return n;
  }
  /* for-each */
  NodeVec vars;
  nv_init(&vars);
  nv_push(&vars, var0);
  while (accept(P, TOK_COMMA))
    nv_push(&vars, parse_for_var(P));
  int nvars = vars.count; /* 用户声明的循环变量数（1 或 2） */
  expect2(P, TOK_COLON);
  AstList iters = parse_expression_list(P);
  expect2(P, TOK_RPAREN);
  AstNode *body = parse_body(P);

  /* 语法糖：右侧仅 1 个非 call 表达式 → 包成 iter(2, expr)
     - call 表达式（如 pairs(l), io.lines(f)）透传，不包
     - 多表达式（如 iter_fn, null, 0）透传，兼容旧式
     - 1 变量时添加隐藏控制变量 __it_k，使 C=2：
       SPT for-each 中第一个 loop var 绑到 R[A+3]=control=key，
       若不加隐藏变量，用户的 v 会绑到 key（索引）而非 value。
       加隐藏变量后 __it_k 绑 key，用户的 v 绑 R[A+4]=value。 */
  if (iters.count == 1 && iters.items[0]->type != NODE_FUNCTION_CALL) {
    if (nvars == 1) {
      /* 在 vars 头部插入隐藏控制变量 __it_k */
      AstNode *hidden = spt_ast_new(P->arena, NODE_PARAMETER_DECL, loc);
      hidden->u.param.name = spt_arena_strndup(P->arena, "__it_k", 6);
      hidden->u.param.type_annotation = NULL;
      nv_push(&vars, NULL); /* 扩容（可能 realloc） */
      for (int i = vars.count - 1; i > 0; i--)
        vars.data[i] = vars.data[i - 1]; /* NodeVec 用 data 字段 */
      vars.data[0] = hidden;
    }
    /* 构造 iter(2, expr) 替换原始表达式 */
    AstNode *iter_call = build_iter_call(P, loc, 2, iters.items[0]);
    AstNode **exprs = (AstNode **)spt_arena_alloc(P->arena, sizeof(AstNode *));
    exprs[0] = iter_call;
    iters.items = exprs;
    iters.count = 1;
  }

  AstNode *n = spt_ast_new(P->arena, NODE_FOR_EACH_STATEMENT, loc);
  n->u.for_each.loop_variables = nv_finish(&vars, P->arena);
  n->u.for_each.iterable_exprs = iters;
  n->u.for_each.body = body;
  return n;
}

static AstNode *parse_return(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_RETURN);
  AstList vals = spt_ast_list_empty();
  if (!at(P, TOK_SEMICOLON))
    vals = parse_expression_list(P);
  expect2(P, TOK_SEMICOLON);
  AstNode *n = spt_ast_new(P->arena, NODE_RETURN_STATEMENT, loc);
  n->u.return_stmt.values = vals;
  return n;
}

static AstNode *parse_import(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_IMPORT);
  if (at(P, TOK_MUL)) {
    /* import * as IDENT from "path" */
    advance(P);
    expect2(P, TOK_AS);
    const SptToken *alias = expect2(P, TOK_IDENTIFIER);
    expect2(P, TOK_FROM);
    const SptToken *path = expect2(P, TOK_STRING_LITERAL);
    int len = 0;
    const char *p = process_string(P, path, &len);
    AstNode *n = spt_ast_new(P->arena, NODE_IMPORT_NAMESPACE, loc);
    n->u.import_ns.alias = spt_arena_strndup(P->arena, alias->lexeme, (size_t)alias->length);
    n->u.import_ns.module_path = p;
    return n;
  }
  if (at(P, TOK_LBRACE)) {
    /* import { spec (, spec)* } from "path" */
    advance(P);
    NodeVec specs;
    nv_init(&specs);
    for (;;) {
      SourceLocation sloc = cur_loc(P);
      const SptToken *name = expect2(P, TOK_IDENTIFIER);
      const char *alias = NULL;
      if (accept(P, TOK_AS)) {
        const SptToken *a = expect2(P, TOK_IDENTIFIER);
        alias = spt_arena_strndup(P->arena, a->lexeme, (size_t)a->length);
      }
      AstNode *spec = spt_ast_new(P->arena, NODE_IMPORT_SPECIFIER, sloc);
      spec->u.import_spec.imported_name =
          spt_arena_strndup(P->arena, name->lexeme, (size_t)name->length);
      spec->u.import_spec.alias = alias;
      nv_push(&specs, spec);
      if (!accept(P, TOK_COMMA))
        break;
    }
    expect2(P, TOK_RBRACE);
    expect2(P, TOK_FROM);
    const SptToken *path = expect2(P, TOK_STRING_LITERAL);
    int len = 0;
    const char *p = process_string(P, path, &len);
    AstNode *n = spt_ast_new(P->arena, NODE_IMPORT_NAMED, loc);
    n->u.import_named.specifiers = nv_finish(&specs, P->arena);
    n->u.import_named.module_path = p;
    return n;
  }
  error_at(P, cur(P), "%s", "语法错误：无效的 import 语句");
  return spt_ast_new(P->arena, NODE_IMPORT_NAMESPACE, loc);
}

static AstNode *parse_defer(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_DEFER);
  AstNode *body = parse_block(P);
  AstNode *n = spt_ast_new(P->arena, NODE_DEFER_STATEMENT, loc);
  n->u.defer_stmt.body = body;
  return n;
}

static AstNode *parse_block(Parser *P) {
  SourceLocation loc = cur_loc(P);
  expect2(P, TOK_LBRACE);
  P->scope_depth++;
  NodeVec stmts;
  nv_init(&stmts);
  while (!at(P, TOK_RBRACE) && !at_end(P)) {
    int before = P->pos;
    AstNode *s = parse_statement(P);
    if (s)
      nv_push(&stmts, s);
    if (P->panic)
      synchronize(P);
    if (P->pos == before)
      advance(P); /* 防止无进展死循环 */
  }
  SourceLocation endloc = cur_loc(P);
  expect2(P, TOK_RBRACE);
  P->scope_depth--;
  AstNode *n = spt_ast_new(P->arena, NODE_BLOCK, loc);
  n->u.block.statements = nv_finish(&stmts, P->arena);
  n->u.block.end_loc = endloc;
  n->u.block.use_end = true;
  return n;
}

/* 控制流体：花括号代码块或单条语句（包成单语句 BLOCK，使 codegen 的 body 永远是 BLOCK）。 */
static AstNode *parse_body(Parser *P) {
  if (at(P, TOK_LBRACE))
    return parse_block(P);
  SourceLocation loc = cur_loc(P);
  AstNode *s = parse_statement(P);
  AstNode *n = spt_ast_new(P->arena, NODE_BLOCK, loc);
  n->u.block.statements = spt_ast_list_from(P->arena, &s, 1);
  n->u.block.end_loc = loc;
  n->u.block.use_end = false;
  return n;
}

/* ===========================================================================
 * 外部符号声明 (declare) —— 编译期擦除，仅供工具/类型检查消费
 * ========================================================================= */

/* 声明里的类：CLASS IDENT { declClassMember* }。成员为签名（无体、无初始化器、无 auto）。 */
static AstNode *parse_decl_class(Parser *P, SourceLocation loc, const char *doc) {
  expect2(P, TOK_CLASS);
  const SptToken *name = expect2(P, TOK_IDENTIFIER);
  expect2(P, TOK_LBRACE);
  NodeVec members;
  nv_init(&members);
  while (!at(P, TOK_RBRACE) && !at_end(P)) {
    if (accept(P, TOK_SEMICOLON))
      continue; /* 空成员 */
    SourceLocation mloc = cur_loc(P);
    const char *mdoc = cur(P)->doc;
    int is_static = accept(P, TOK_STATIC) ? 1 : 0;
    int is_const = accept(P, TOK_CONST) ? 1 : 0;
    AstNode *inner = NULL;
    if (at(P, TOK_VARS)) {
      /* 多返回方法签名：vars IDENT ( params ) ; */
      advance(P);
      const SptToken *mn = expect2(P, TOK_IDENTIFIER);
      expect2(P, TOK_LPAREN);
      int variadic = 0;
      AstList params = parse_parameter_list(P, &variadic);
      expect2(P, TOK_RPAREN);
      expect2(P, TOK_SEMICOLON);
      inner = spt_ast_new(P->arena, NODE_FUNCTION_DECL, mloc);
      inner->u.func_decl.name = spt_arena_strndup(P->arena, mn->lexeme, (size_t)mn->length);
      inner->u.func_decl.params = params;
      inner->u.func_decl.return_type = spt_ast_new(P->arena, NODE_TYPE_MULTIRETURN, mloc);
      inner->u.func_decl.body = NULL;
      inner->u.func_decl.is_static = is_static != 0;
      inner->u.func_decl.is_const = is_const != 0;
      inner->u.func_decl.is_variadic = variadic != 0;
      inner->u.func_decl.is_ambient = true;
      inner->u.func_decl.doc = mdoc;
    } else {
      /* type IDENT ... -> 方法签名或字段签名（无 auto） */
      AstNode *type = parse_type(P);
      const SptToken *mn = expect2(P, TOK_IDENTIFIER);
      if (at(P, TOK_LPAREN)) {
        advance(P);
        int variadic = 0;
        AstList params = parse_parameter_list(P, &variadic);
        expect2(P, TOK_RPAREN);
        expect2(P, TOK_SEMICOLON);
        inner = spt_ast_new(P->arena, NODE_FUNCTION_DECL, mloc);
        inner->u.func_decl.name = spt_arena_strndup(P->arena, mn->lexeme, (size_t)mn->length);
        inner->u.func_decl.params = params;
        inner->u.func_decl.return_type = type;
        inner->u.func_decl.body = NULL;
        inner->u.func_decl.is_static = is_static != 0;
        inner->u.func_decl.is_const = is_const != 0;
        inner->u.func_decl.is_variadic = variadic != 0;
        inner->u.func_decl.is_ambient = true;
        inner->u.func_decl.doc = mdoc;
      } else {
        /* 字段签名：type IDENT ;（无初始化器） */
        expect2(P, TOK_SEMICOLON);
        inner = spt_ast_new(P->arena, NODE_VARIABLE_DECL, mloc);
        inner->u.var_decl.name = spt_arena_strndup(P->arena, mn->lexeme, (size_t)mn->length);
        inner->u.var_decl.type_annotation = type;
        inner->u.var_decl.initializer = NULL;
        inner->u.var_decl.is_static = is_static != 0;
        inner->u.var_decl.is_const = is_const != 0;
        inner->u.var_decl.is_ambient = true;
        inner->u.var_decl.doc = mdoc;
      }
    }
    AstNode *cm = spt_ast_new(P->arena, NODE_CLASS_MEMBER, mloc);
    cm->u.class_member.member_declaration = inner;
    cm->u.class_member.is_static = is_static != 0;
    nv_push(&members, cm);
    if (P->panic)
      synchronize(P);
  }
  expect2(P, TOK_RBRACE);
  AstNode *n = spt_ast_new(P->arena, NODE_CLASS_DECL, loc);
  n->u.class_decl.name = spt_arena_strndup(P->arena, name->lexeme, (size_t)name->length);
  n->u.class_decl.members = nv_finish(&members, P->arena);
  n->u.class_decl.is_ambient = true;
  n->u.class_decl.doc = doc;
  return n;
}

/* 单个声明成员：返回 var_decl/func_decl/class_decl（均 is_ambient），或 NULL（空成员 ';'）。
   doc 在成员首 token 上读取（模块块内逐成员；环境声明的单成员由 parse_declare 补挂）。 */
static AstNode *parse_decl_member(Parser *P) {
  SourceLocation loc = cur_loc(P);
  const char *doc = cur(P)->doc;

  if (accept(P, TOK_SEMICOLON))
    return NULL; /* 空成员 */

  if (at(P, TOK_CLASS))
    return parse_decl_class(P, loc, doc);

  int is_global = accept(P, TOK_GLOBAL) ? 1 : 0;
  int is_const = accept(P, TOK_CONST) ? 1 : 0;

  if (at(P, TOK_VARS)) {
    /* 多返回函数签名：vars qualifiedIdent ( params ) ; */
    advance(P);
    const char *name = parse_qualified_name(P);
    expect2(P, TOK_LPAREN);
    int variadic = 0;
    AstList params = parse_parameter_list(P, &variadic);
    expect2(P, TOK_RPAREN);
    expect2(P, TOK_SEMICOLON);
    AstNode *n = spt_ast_new(P->arena, NODE_FUNCTION_DECL, loc);
    n->u.func_decl.name = name;
    n->u.func_decl.params = params;
    n->u.func_decl.return_type = spt_ast_new(P->arena, NODE_TYPE_MULTIRETURN, loc);
    n->u.func_decl.body = NULL;
    n->u.func_decl.is_global = is_global != 0;
    n->u.func_decl.is_const = is_const != 0;
    n->u.func_decl.is_variadic = variadic != 0;
    n->u.func_decl.is_ambient = true;
    n->u.func_decl.doc = doc;
    return n;
  }

  /* type 后跟名字：函数签名或变量签名（不允许 auto / 初始化器 / 函数体）。
     注意：parse_type 不接受 'auto'，故 `declare auto x;` 会在此报错并使解析失败。 */
  AstNode *type = parse_type(P);
  const SptToken *id = expect2(P, TOK_IDENTIFIER);

  if (at(P, TOK_DOT) || at(P, TOK_LPAREN)) {
    /* 函数签名：type qualifiedIdent ( params ) ; —— 名字可能点号限定 */
    int total = id->length;
    int save = P->pos;
    while (tok_at(P, P->pos)->kind == TOK_DOT && tok_at(P, P->pos + 1)->kind == TOK_IDENTIFIER) {
      total += 1 + tok_at(P, P->pos + 1)->length;
      P->pos += 2;
    }
    P->pos = save;
    char *nbuf = (char *)spt_arena_alloc(P->arena, (size_t)total + 1);
    int oi = 0;
    memcpy(nbuf, id->lexeme, (size_t)id->length);
    oi += id->length;
    while (tok_at(P, P->pos)->kind == TOK_DOT && tok_at(P, P->pos + 1)->kind == TOK_IDENTIFIER) {
      advance(P);
      nbuf[oi++] = '.';
      const SptToken *seg = cur(P);
      memcpy(nbuf + oi, seg->lexeme, (size_t)seg->length);
      oi += seg->length;
      advance(P);
    }
    nbuf[oi] = '\0';
    expect2(P, TOK_LPAREN);
    int variadic = 0;
    AstList params = parse_parameter_list(P, &variadic);
    expect2(P, TOK_RPAREN);
    expect2(P, TOK_SEMICOLON);
    AstNode *n = spt_ast_new(P->arena, NODE_FUNCTION_DECL, loc);
    n->u.func_decl.name = nbuf;
    n->u.func_decl.params = params;
    n->u.func_decl.return_type = type;
    n->u.func_decl.body = NULL;
    n->u.func_decl.is_global = is_global != 0;
    n->u.func_decl.is_const = is_const != 0;
    n->u.func_decl.is_variadic = variadic != 0;
    n->u.func_decl.is_ambient = true;
    n->u.func_decl.doc = doc;
    return n;
  }

  /* 变量签名：type IDENT ;（无初始化器；写了 '=' 会在此因期望 ';' 而报错） */
  expect2(P, TOK_SEMICOLON);
  AstNode *n = spt_ast_new(P->arena, NODE_VARIABLE_DECL, loc);
  n->u.var_decl.name = spt_arena_strndup(P->arena, id->lexeme, (size_t)id->length);
  n->u.var_decl.type_annotation = type;
  n->u.var_decl.initializer = NULL;
  n->u.var_decl.is_global = is_global != 0;
  n->u.var_decl.is_const = is_const != 0;
  n->u.var_decl.is_ambient = true;
  n->u.var_decl.doc = doc;
  return n;
}

/* declare 语句：DECLARE FROM "..." { members }  |  DECLARE <member> */
static AstNode *parse_declare(Parser *P) {
  SourceLocation loc = cur_loc(P);
  const char *doc = cur(P)->doc; /* declare 前的文档（模块级或单符号级） */
  expect2(P, TOK_DECLARE);

  if (at(P, TOK_FROM)) {
    /* 模块声明块：declare from "path" { members } */
    advance(P); /* from */
    const SptToken *path = expect2(P, TOK_STRING_LITERAL);
    int len = 0;
    const char *mp = process_string(P, path, &len);
    expect2(P, TOK_LBRACE);
    NodeVec members;
    nv_init(&members);
    while (!at(P, TOK_RBRACE) && !at_end(P)) {
      int before = P->pos;
      AstNode *m = parse_decl_member(P);
      if (m)
        nv_push(&members, m);
      if (P->panic)
        synchronize(P);
      if (P->pos == before)
        advance(P); /* 防止无进展死循环 */
    }
    expect2(P, TOK_RBRACE);
    AstNode *n = spt_ast_new(P->arena, NODE_DECLARE_MODULE, loc);
    n->u.declare_module.module_path = mp;
    n->u.declare_module.members = nv_finish(&members, P->arena);
    n->u.declare_module.doc = doc;
    return n;
  }

  /* 环境声明：单个外部符号。doc 挂在 declare token 上，补挂到成员节点。 */
  AstNode *m = parse_decl_member(P);
  if (m) {
    if (m->type == NODE_FUNCTION_DECL && !m->u.func_decl.doc)
      m->u.func_decl.doc = doc;
    else if (m->type == NODE_VARIABLE_DECL && !m->u.var_decl.doc)
      m->u.var_decl.doc = doc;
    else if (m->type == NODE_CLASS_DECL && !m->u.class_decl.doc)
      m->u.class_decl.doc = doc;
  }
  return m;
}

static AstNode *parse_statement(Parser *P) {
  switch (cur_kind(P)) {
  case TOK_SEMICOLON:
    advance(P);
    return NULL; /* 空语句 */
  case TOK_IF:
    return parse_if(P);
  case TOK_WHILE:
    return parse_while(P);
  case TOK_FOR:
    return parse_for(P);
  case TOK_BREAK: {
    SourceLocation loc = cur_loc(P);
    advance(P);
    expect2(P, TOK_SEMICOLON);
    return spt_ast_new(P->arena, NODE_BREAK_STATEMENT, loc);
  }
  case TOK_CONTINUE: {
    SourceLocation loc = cur_loc(P);
    advance(P);
    expect2(P, TOK_SEMICOLON);
    return spt_ast_new(P->arena, NODE_CONTINUE_STATEMENT, loc);
  }
  case TOK_RETURN:
    return parse_return(P);
  case TOK_LBRACE:
    return parse_block(P);
  case TOK_IMPORT: {
    AstNode *s = parse_import(P);
    expect2(P, TOK_SEMICOLON);
    return s;
  }
  case TOK_DEFER:
    return parse_defer(P);
  case TOK_DECLARE:
    return parse_declare(P);
  default:
    break;
  }
  if (looks_like_declaration(P))
    return parse_declaration(P);
  return parse_simple_statement(P);
}

/* ===========================================================================
 * 入口
 * ========================================================================= */
/* 解析核心：始终构建并返回尽力而为的根节点（带 panic-mode 恢复）。
** 错误写入 diag；是否因错误而对外返回 NULL 由各入口决定。 */
static AstNode *run_parser(const SptTokenArray *toks, SptArena *arena, SptDiag *diag) {
  Parser P;
  P.toks = toks->tokens;
  P.count = toks->count;
  P.pos = 0;
  P.arena = arena;
  P.diag = diag;
  P.scope_depth = 0;
  P.panic = 0;

  SourceLocation loc = cur_loc(&P);
  NodeVec stmts;
  nv_init(&stmts);
  while (!at_end(&P)) {
    int before = P.pos;
    AstNode *s = parse_statement(&P);
    if (s)
      nv_push(&stmts, s);
    if (P.panic)
      synchronize(&P);
    if (P.pos == before)
      advance(&P);
  }
  SourceLocation endloc = cur_loc(&P);

  AstNode *root = spt_ast_new(arena, NODE_BLOCK, loc);
  root->u.block.statements = nv_finish(&stmts, arena);
  root->u.block.end_loc = endloc;
  root->u.block.use_end = true;
  return root;
}

AstNode *spt_parse(const SptTokenArray *toks, SptArena *arena, SptDiag *diag) {
  AstNode *root = run_parser(toks, arena, diag);
  if (spt_diag_has_error(diag))
    return NULL; /* 编译路径语义：有错即失败 */
  return root;
}

/* 容错解析（供 LSP 使用）：无论有无错误，都返回尽力而为的根节点。
** 诊断仍写入 diag，调用方据此报告问题。 */
AstNode *spt_parse_tolerant(const SptTokenArray *toks, SptArena *arena, SptDiag *diag) {
  return run_parser(toks, arena, diag);
}
