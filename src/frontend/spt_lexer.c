/*
** spt_lexer.c — 手写词法分析器实现。
**
** 关键等价性保证（须与原 ANTLR/Visitor 一致）：
**   - `>>` 不产生独立 token：扫到两个 '>' 各产生一个 TOK_GT，右移在解析器合成。
**   - 字符串 token 的词素是含引号的原始文本；反转义在解析器按 5 种规则处理。
**   - Unicode 标识符区间直接抄录自 LangLexer.g4。
**   - 数值仅区分 INTEGER / FLOAT_LITERAL 两类；数值转换在解析器用 strtoll/strtod。
*/
#include "spt_lexer.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ===========================================================================
 * 词法器状态
 * ========================================================================= */
typedef struct {
  const char *src;
  size_t len;
  size_t pos;        /* 当前字节偏移 */
  int line;          /* 当前行（1 起） */
  size_t line_start; /* 当前行起始字节偏移（用于算列） */
  SptArena *arena;
  SptDiag *diag;
  int had_error;

  /* token 动态数组（独立 malloc，定稿后拷入 arena） */
  SptToken *toks;
  int count;
  int cap;
} Lexer;

static int lex_col(const Lexer *L, size_t at) {
  return (int)(at - L->line_start) + 1;
}

static void lex_push(Lexer *L, SptTokenKind kind, size_t start, size_t end, int line, int col) {
  if (L->count >= L->cap) {
    int ncap = L->cap ? L->cap * 2 : 256;
    SptToken *nt = (SptToken *)realloc(L->toks, sizeof(SptToken) * (size_t)ncap);
    if (!nt) {
      L->had_error = 1;
      return;
    }
    L->toks = nt;
    L->cap = ncap;
  }
  SptToken *t = &L->toks[L->count++];
  t->kind = kind;
  t->lexeme = L->src + start;
  t->length = (int)(end - start);
  t->line = line;
  t->column = col;
}

static void lex_error(Lexer *L, int line, int col, const char *fmt, const char *arg) {
  L->had_error = 1;
  if (arg)
    spt_diag_error(L->diag, line, col, fmt, arg);
  else
    spt_diag_error(L->diag, line, col, "%s", fmt);
}

/* ===========================================================================
 * UTF-8 解码
 * ========================================================================= */
/* 解码 p 处一个码点；avail 为可用字节数。*adv 置消耗字节数（≥1）。
** 非法序列返回 0xFFFFFFFF 且 *adv=1。 */
static uint32_t utf8_decode(const char *p, size_t avail, int *adv) {
  unsigned char c = (unsigned char)p[0];
  if (c < 0x80) {
    *adv = 1;
    return c;
  }
  int n;
  uint32_t cp;
  if ((c & 0xE0) == 0xC0) {
    n = 2;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    n = 3;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    n = 4;
    cp = c & 0x07;
  } else {
    *adv = 1;
    return 0xFFFFFFFFu;
  }
  if ((size_t)n > avail) {
    *adv = 1;
    return 0xFFFFFFFFu;
  }
  for (int i = 1; i < n; i++) {
    unsigned char cc = (unsigned char)p[i];
    if ((cc & 0xC0) != 0x80) {
      *adv = 1;
      return 0xFFFFFFFFu;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  *adv = n;
  return cp;
}

/* ===========================================================================
 * Unicode 标识符区间（抄录自 LangLexer.g4）
 * ========================================================================= */
typedef struct {
  uint32_t lo, hi;
} Range;

/* IDENT_START 的非 ASCII 区间（ASCII a-zA-Z_ 走快速路径） */
static const Range IDENT_START_RANGES[] = {
    {0x00C0, 0x00D6}, {0x00D8, 0x00F6},   {0x00F8, 0x02FF},   {0x0370, 0x037D},
    {0x037F, 0x1FFF}, {0x200C, 0x200D},   {0x2070, 0x218F},   {0x2C00, 0x2FEF},
    {0x3001, 0xD7FF}, {0xF900, 0xFDCF},   {0xFDF0, 0xFFFD},   {0x10000, 0x1FAFF},
    {0x1FC00, 0xEFFFF},
};

/* IDENT_PART 在 START 之外额外允许的区间（0-9 走快速路径） */
static const Range IDENT_PART_EXTRA[] = {
    {0x0300, 0x036F},   {0x203F, 0x2040},   {0xFE00, 0xFE0F},   {0xE0100, 0xE01EF},
    {0x1F3FB, 0x1F3FF}, {0x20E3, 0x20E3},
};

static int in_ranges(uint32_t cp, const Range *r, size_t n) {
  for (size_t i = 0; i < n; i++)
    if (cp >= r[i].lo && cp <= r[i].hi)
      return 1;
  return 0;
}

static int is_ident_start_cp(uint32_t cp) {
  if (cp < 0x80)
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_';
  return in_ranges(cp, IDENT_START_RANGES, sizeof(IDENT_START_RANGES) / sizeof(Range));
}

static int is_ident_part_cp(uint32_t cp) {
  if (cp < 0x80)
    return (cp >= 'a' && cp <= 'z') || (cp >= 'A' && cp <= 'Z') || cp == '_' ||
           (cp >= '0' && cp <= '9');
  if (is_ident_start_cp(cp))
    return 1;
  return in_ranges(cp, IDENT_PART_EXTRA, sizeof(IDENT_PART_EXTRA) / sizeof(Range));
}

/* ===========================================================================
 * 关键字表
 * ========================================================================= */
typedef struct {
  const char *kw;
  SptTokenKind kind;
} Keyword;

static const Keyword KEYWORDS[] = {
    {"int", TOK_INT},         {"float", TOK_FLOAT},
    {"number", TOK_NUMBER},   {"str", TOK_STR},
    {"bool", TOK_BOOL},       {"any", TOK_ANY},
    {"void", TOK_VOID},       {"null", TOK_NULL},
    {"list", TOK_LIST},       {"map", TOK_MAP},
    {"function", TOK_FUNCTION}, {"coro", TOK_COROUTINE},
    {"vars", TOK_VARS},       {"if", TOK_IF},
    {"else", TOK_ELSE},       {"while", TOK_WHILE},
    {"for", TOK_FOR},         {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"return", TOK_RETURN},
    {"defer", TOK_DEFER},     {"true", TOK_TRUE},
    {"false", TOK_FALSE},     {"const", TOK_CONST},
    {"auto", TOK_AUTO},       {"global", TOK_GLOBAL},
    {"static", TOK_STATIC},   {"import", TOK_IMPORT},
    {"as", TOK_AS},           {"from", TOK_FROM},
    {"export", TOK_EXPORT},
    {"class", TOK_CLASS},
};

static SptTokenKind keyword_lookup(const char *s, int len) {
  for (size_t i = 0; i < sizeof(KEYWORDS) / sizeof(Keyword); i++) {
    if ((int)strlen(KEYWORDS[i].kw) == len && memcmp(KEYWORDS[i].kw, s, (size_t)len) == 0)
      return KEYWORDS[i].kind;
  }
  return TOK_IDENTIFIER;
}

/* ===========================================================================
 * 字符判定辅助
 * ========================================================================= */
static int is_digit(int c) { return c >= '0' && c <= '9'; }
static int is_hex(int c) {
  return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static char peek_at(Lexer *L, size_t off) {
  size_t p = L->pos + off;
  return p < L->len ? L->src[p] : '\0';
}

/* ===========================================================================
 * 各类词素扫描
 * ========================================================================= */

/* 跳过空白与注释，更新行号。 */
static void skip_trivia(Lexer *L) {
  for (;;) {
    if (L->pos >= L->len)
      return;
    char c = L->src[L->pos];
    if (c == ' ' || c == '\t' || c == '\r') {
      L->pos++;
    } else if (c == '\n') {
      L->pos++;
      L->line++;
      L->line_start = L->pos;
    } else if (c == '/' && peek_at(L, 1) == '/') {
      /* 行注释 */
      L->pos += 2;
      while (L->pos < L->len && L->src[L->pos] != '\n')
        L->pos++;
    } else if (c == '/' && peek_at(L, 1) == '*') {
      /* 块注释（可跨行） */
      int sline = L->line, scol = lex_col(L, L->pos);
      L->pos += 2;
      int closed = 0;
      while (L->pos < L->len) {
        if (L->src[L->pos] == '*' && peek_at(L, 1) == '/') {
          L->pos += 2;
          closed = 1;
          break;
        }
        if (L->src[L->pos] == '\n') {
          L->line++;
          L->line_start = L->pos + 1;
        }
        L->pos++;
      }
      if (!closed)
        lex_error(L, sline, scol, "%s", "块注释未闭合（缺少 '*/'）");
    } else {
      return;
    }
  }
}

/* 扫描数值：返回 token 种类（INTEGER 或 FLOAT_LITERAL）。pos 推进到末尾。 */
static SptTokenKind scan_number(Lexer *L) {
  int is_float = 0;
  if (L->src[L->pos] == '0' && (peek_at(L, 1) == 'x' || peek_at(L, 1) == 'X')) {
    /* 十六进制 */
    L->pos += 2;
    while (L->pos < L->len && is_hex(L->src[L->pos]))
      L->pos++;
    if (L->pos < L->len && L->src[L->pos] == '.') {
      is_float = 1;
      L->pos++;
      while (L->pos < L->len && is_hex(L->src[L->pos]))
        L->pos++;
    }
    if (L->pos < L->len && (L->src[L->pos] == 'p' || L->src[L->pos] == 'P')) {
      /* 十六进制指数：[pP][+-]?DIGIT+ */
      size_t save = L->pos;
      size_t q = L->pos + 1;
      if (q < L->len && (L->src[q] == '+' || L->src[q] == '-'))
        q++;
      if (q < L->len && is_digit(L->src[q])) {
        is_float = 1;
        L->pos = q;
        while (L->pos < L->len && is_digit(L->src[L->pos]))
          L->pos++;
      } else {
        L->pos = save; /* 非法指数：不并入数值 */
      }
    }
    return is_float ? TOK_FLOAT_LITERAL : TOK_INTEGER;
  }

  /* 十进制 */
  if (L->src[L->pos] == '.') {
    /* 入口形如 .DIGIT+ */
    is_float = 1;
    L->pos++;
    while (L->pos < L->len && is_digit(L->src[L->pos]))
      L->pos++;
  } else {
    while (L->pos < L->len && is_digit(L->src[L->pos]))
      L->pos++;
    /* 小数点：仅当其后不是另一个 '.'（避免吞掉 '..' / '...'）才视为浮点点 */
    if (L->pos < L->len && L->src[L->pos] == '.' && peek_at(L, 1) != '.') {
      is_float = 1;
      L->pos++;
      while (L->pos < L->len && is_digit(L->src[L->pos]))
        L->pos++;
    }
  }
  /* 十进制指数：[eE][+-]?DIGIT+ */
  if (L->pos < L->len && (L->src[L->pos] == 'e' || L->src[L->pos] == 'E')) {
    size_t save = L->pos;
    size_t q = L->pos + 1;
    if (q < L->len && (L->src[q] == '+' || L->src[q] == '-'))
      q++;
    if (q < L->len && is_digit(L->src[q])) {
      is_float = 1;
      L->pos = q;
      while (L->pos < L->len && is_digit(L->src[L->pos]))
        L->pos++;
    } else {
      L->pos = save;
    }
  }
  return is_float ? TOK_FLOAT_LITERAL : TOK_INTEGER;
}

/* 扫描字符串字面量。返回 1 成功（pos 推进到闭引号之后），0 失败。 */
static int scan_string(Lexer *L) {
  int sline = L->line, scol = lex_col(L, L->pos);
  char quote = L->src[L->pos];
  L->pos++; /* 跳过开引号 */
  while (L->pos < L->len) {
    char c = L->src[L->pos];
    if (c == '\\') {
      /* 转义：消耗反斜杠与其后一个字符（保证 \" 不闭合字符串） */
      L->pos++;
      if (L->pos < L->len) {
        if (L->src[L->pos] == '\n') {
          L->line++;
          L->line_start = L->pos + 1;
        }
        L->pos++;
      }
      continue;
    }
    if (c == quote) {
      L->pos++; /* 跳过闭引号 */
      return 1;
    }
    if (c == '\n') {
      L->line++;
      L->line_start = L->pos + 1;
    }
    L->pos++;
  }
  lex_error(L, sline, scol, "%s", "字符串字面量未闭合");
  return 0;
}

/* 扫描标识符/关键字。pos 推进到末尾。返回 token 种类。 */
static SptTokenKind scan_identifier(Lexer *L, size_t start) {
  /* 首字符已确认是 ident-start，逐码点推进 */
  while (L->pos < L->len) {
    unsigned char c = (unsigned char)L->src[L->pos];
    if (c < 0x80) {
      if (is_ident_part_cp(c))
        L->pos++;
      else
        break;
    } else {
      int adv;
      uint32_t cp = utf8_decode(L->src + L->pos, L->len - L->pos, &adv);
      if (cp != 0xFFFFFFFFu && is_ident_part_cp(cp))
        L->pos += (size_t)adv;
      else
        break;
    }
  }
  return keyword_lookup(L->src + start, (int)(L->pos - start));
}

/* ===========================================================================
 * 主循环
 * ========================================================================= */
int spt_lex(const char *source, size_t len, SptArena *arena, SptDiag *diag, SptTokenArray *out) {
  Lexer L;
  L.src = source;
  L.len = len;
  L.pos = 0;
  L.line = 1;
  L.line_start = 0;
  L.arena = arena;
  L.diag = diag;
  L.had_error = 0;
  L.toks = NULL;
  L.count = 0;
  L.cap = 0;

  while (1) {
    skip_trivia(&L);
    if (L.pos >= L.len)
      break;

    size_t start = L.pos;
    int line = L.line;
    int col = lex_col(&L, start);
    unsigned char c = (unsigned char)L.src[L.pos];

    /* 标识符 / 关键字（含 Unicode 起始） */
    if (c == '_' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
      L.pos++;
      SptTokenKind k = scan_identifier(&L, start);
      lex_push(&L, k, start, L.pos, line, col);
      continue;
    }
    if (c >= 0x80) {
      int adv;
      uint32_t cp = utf8_decode(L.src + L.pos, L.len - L.pos, &adv);
      if (cp != 0xFFFFFFFFu && is_ident_start_cp(cp)) {
        L.pos += (size_t)adv;
        SptTokenKind k = scan_identifier(&L, start);
        lex_push(&L, k, start, L.pos, line, col);
        continue;
      }
      /* 非法的非 ASCII 字符 */
      lex_error(&L, line, col, "%s", "非法字符");
      L.pos += (size_t)(adv > 0 ? adv : 1);
      continue;
    }

    /* 数值（数字开头，或 '.' 紧跟数字） */
    if (is_digit(c) || (c == '.' && is_digit(peek_at(&L, 1)))) {
      SptTokenKind k = scan_number(&L);
      lex_push(&L, k, start, L.pos, line, col);
      continue;
    }

    /* 字符串 */
    if (c == '"' || c == '\'') {
      if (!scan_string(&L))
        continue; /* 错误已记录，跳过本 token 继续 */
      lex_push(&L, TOK_STRING_LITERAL, start, L.pos, line, col);
      continue;
    }

    /* 标点 / 运算符（最大匹配） */
    {
      char c1 = peek_at(&L, 1);
      char c2 = peek_at(&L, 2);
      SptTokenKind k;
      size_t adv = 1;
      switch (c) {
      case '+':
        if (c1 == '=') { k = TOK_ADD_ASSIGN; adv = 2; } else { k = TOK_ADD; }
        break;
      case '-':
        if (c1 == '=') { k = TOK_SUB_ASSIGN; adv = 2; }
        else if (c1 == '>') { k = TOK_ARROW; adv = 2; }
        else { k = TOK_SUB; }
        break;
      case '*':
        if (c1 == '=') { k = TOK_MUL_ASSIGN; adv = 2; } else { k = TOK_MUL; }
        break;
      case '/':
        if (c1 == '=') { k = TOK_DIV_ASSIGN; adv = 2; } else { k = TOK_DIV; }
        break;
      case '%':
        if (c1 == '=') { k = TOK_MOD_ASSIGN; adv = 2; } else { k = TOK_MOD; }
        break;
      case '~':
        if (c1 == '/' && c2 == '=') { k = TOK_IDIV_ASSIGN; adv = 3; }
        else if (c1 == '/') { k = TOK_IDIV; adv = 2; }
        else { k = TOK_BIT_NOT; }
        break;
      case '=':
        if (c1 == '=') { k = TOK_EQ; adv = 2; } else { k = TOK_ASSIGN; }
        break;
      case '!':
        if (c1 == '=') { k = TOK_NEQ; adv = 2; } else { k = TOK_NOT; }
        break;
      case '<':
        if (c1 == '=') { k = TOK_LTE; adv = 2; }
        else if (c1 == '<') { k = TOK_LSHIFT; adv = 2; }
        else { k = TOK_LT; }
        break;
      case '>':
        /* 注意：不产生 '>>'，右移由解析器用两个 '>' 合成 */
        if (c1 == '=') { k = TOK_GTE; adv = 2; } else { k = TOK_GT; }
        break;
      case '&':
        if (c1 == '&') { k = TOK_AND; adv = 2; } else { k = TOK_BIT_AND; }
        break;
      case '|':
        if (c1 == '|') { k = TOK_OR; adv = 2; } else { k = TOK_BIT_OR; }
        break;
      case '^': k = TOK_BIT_XOR; break;
      case '#': k = TOK_LEN; break;
      case '.':
        if (c1 == '.' && c2 == '.') { k = TOK_ELLIPSIS; adv = 3; }
        else if (c1 == '.' && c2 == '=') { k = TOK_CONCAT_ASSIGN; adv = 3; }
        else if (c1 == '.') { k = TOK_CONCAT; adv = 2; }
        else { k = TOK_DOT; }
        break;
      case '(': k = TOK_LPAREN; break;
      case ')': k = TOK_RPAREN; break;
      case '[': k = TOK_LBRACKET; break;
      case ']': k = TOK_RBRACKET; break;
      case '{': k = TOK_LBRACE; break;
      case '}': k = TOK_RBRACE; break;
      case ',': k = TOK_COMMA; break;
      case ':': k = TOK_COLON; break;
      case ';': k = TOK_SEMICOLON; break;
      default: {
        char buf[2] = {(char)c, 0};
        lex_error(&L, line, col, "非法字符 '%s'", buf);
        L.pos++;
        goto next_iter;
      }
      }
      L.pos += adv;
      lex_push(&L, k, start, L.pos, line, col);
    }
  next_iter:;
  }

  /* 末尾 EOF */
  lex_push(&L, TOK_EOF, L.pos, L.pos, L.line, lex_col(&L, L.pos));

  if (L.had_error) {
    free(L.toks);
    return 0;
  }

  /* 拷入 arena */
  SptToken *arr = (SptToken *)spt_arena_alloc(arena, sizeof(SptToken) * (size_t)L.count);
  if (!arr) {
    free(L.toks);
    return 0;
  }
  memcpy(arr, L.toks, sizeof(SptToken) * (size_t)L.count);
  free(L.toks);

  out->tokens = arr;
  out->count = L.count;
  return 1;
}
