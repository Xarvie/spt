/*
 * lexer.c — Hand-written tokenizer for SPT source.
 *
 * Tokens point directly into the source buffer (no copies), so the source must
 * outlive lexing. Supports line comments (//), block comments, integer and
 * float literals, double-quoted strings with simple escapes, identifiers, and
 * the operator/keyword set in compiler.h.
 */
#include "compiler.h"
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

void lex_init(Lexer *lx, const char *src) {
  lx->p = src;
  lx->line = 1;
}

static int kw(const char *s, int len, const char *word, TokenType type, Token *t) {
  if ((int)strlen(word) == len && memcmp(s, word, (size_t)len) == 0) { t->type = type; return 1; }
  return 0;
}

static void skip_trivia(Lexer *lx) {
  for (;;) {
    char c = *lx->p;
    if (c == '\n') { lx->line++; lx->p++; }
    else if (c == ' ' || c == '\t' || c == '\r') { lx->p++; }
    else if (c == '/' && lx->p[1] == '/') {            /* line comment */
      lx->p += 2;
      while (*lx->p && *lx->p != '\n') lx->p++;
    }
    else if (c == '/' && lx->p[1] == '*') {            /* block comment */
      lx->p += 2;
      while (*lx->p && !(*lx->p == '*' && lx->p[1] == '/')) { if (*lx->p == '\n') lx->line++; lx->p++; }
      if (*lx->p) lx->p += 2;
    }
    else break;
  }
}

void lex_next(Lexer *lx, Token *t) {
  skip_trivia(lx);
  t->line = lx->line;
  char c = *lx->p;

  if (c == '\0') { t->type = TK_EOF; return; }

  /* number */
  if (isdigit((unsigned char)c)) {
    const char *start = lx->p;
    int is_float = 0;
    while (isdigit((unsigned char)*lx->p)) lx->p++;
    if (*lx->p == '.') { is_float = 1; lx->p++; while (isdigit((unsigned char)*lx->p)) lx->p++; }
    if (*lx->p == 'e' || *lx->p == 'E') {
      is_float = 1; lx->p++;
      if (*lx->p == '+' || *lx->p == '-') lx->p++;
      while (isdigit((unsigned char)*lx->p)) lx->p++;
    }
    if (is_float) { t->type = TK_FLOAT; t->fval = strtod(start, NULL); }
    else          { t->type = TK_INT;   t->ival = (int64_t)strtoll(start, NULL, 10); }
    return;
  }

  /* identifier or keyword */
  if (isalpha((unsigned char)c) || c == '_') {
    const char *start = lx->p;
    while (isalnum((unsigned char)*lx->p) || *lx->p == '_') lx->p++;
    int len = (int)(lx->p - start);
    t->s = start; t->len = len;
    if (kw(start, len, "global",   TK_GLOBAL,   t)) return;
    if (kw(start, len, "function", TK_FUNCTION, t)) return;
    if (kw(start, len, "if",       TK_IF,       t)) return;
    if (kw(start, len, "else",     TK_ELSE,     t)) return;
    if (kw(start, len, "while",    TK_WHILE,    t)) return;
    if (kw(start, len, "return",   TK_RETURN,   t)) return;
    if (kw(start, len, "break",    TK_BREAK,    t)) return;
    if (kw(start, len, "continue", TK_CONTINUE, t)) return;
    if (kw(start, len, "true",     TK_TRUE,     t)) return;
    if (kw(start, len, "false",    TK_FALSE,    t)) return;
    if (kw(start, len, "null",     TK_NULL,     t)) return;
    if (kw(start, len, "int",      TK_KW_INT,    t)) return;
    if (kw(start, len, "float",    TK_KW_FLOAT,  t)) return;
    if (kw(start, len, "string",   TK_KW_STRING, t)) return;
    if (kw(start, len, "bool",     TK_KW_BOOL,   t)) return;
    if (kw(start, len, "list",     TK_KW_LIST,   t)) return;
    if (kw(start, len, "map",      TK_KW_MAP,    t)) return;
    if (kw(start, len, "const",    TK_CONST,     t)) return;
    t->type = TK_NAME;
    return;
  }

  /* string */
  if (c == '"') {
    lx->p++;
    const char *start = lx->p;
    /* SPT strings are interned later; here we just delimit. Escapes are
     * resolved by the parser when it materialises the literal. */
    while (*lx->p && *lx->p != '"') {
      if (*lx->p == '\\' && lx->p[1]) lx->p++;
      if (*lx->p == '\n') lx->line++;
      lx->p++;
    }
    t->type = TK_STR; t->s = start; t->len = (int)(lx->p - start);
    if (*lx->p == '"') lx->p++;
    return;
  }

  /* operators / punctuation */
  lx->p++;
  switch (c) {
    case '+': t->type = TK_PLUS; return;
    case '-': if (*lx->p == '>') { lx->p++; t->type = TK_ARROW; } else t->type = TK_MINUS; return;
    case '*': t->type = TK_STAR; return;
    case '/': t->type = TK_SLASH; return;
    case '%': t->type = TK_PERCENT; return;
    case '(': t->type = TK_LPAREN; return;
    case ')': t->type = TK_RPAREN; return;
    case '{': t->type = TK_LBRACE; return;
    case '}': t->type = TK_RBRACE; return;
    case '[': t->type = TK_LBRACKET; return;
    case ']': t->type = TK_RBRACKET; return;
    case ',': t->type = TK_COMMA; return;
    case ';': t->type = TK_SEMI; return;
    case '#': t->type = TK_HASH; return;
    case '.': if (*lx->p == '.') { lx->p++; t->type = TK_DOTDOT; return; }
              t->type = TK_EOF; return;   /* lone '.' is not a token */
    case '=': if (*lx->p == '=') { lx->p++; t->type = TK_EQ; } else t->type = TK_ASSIGN; return;
    case '!': if (*lx->p == '=') { lx->p++; t->type = TK_NE; } else t->type = TK_NOT; return;
    case '<': if (*lx->p == '=') { lx->p++; t->type = TK_LE; } else t->type = TK_LT; return;
    case '>': if (*lx->p == '=') { lx->p++; t->type = TK_GE; } else t->type = TK_GT; return;
    case ':': t->type = TK_COLON; return;
    case '&': if (*lx->p == '&') { lx->p++; t->type = TK_ANDAND; return; }
              t->type = TK_EOF; return;   /* lone '&' is not a token */
    case '|': if (*lx->p == '|') { lx->p++; t->type = TK_OROR; return; }
              t->type = TK_EOF; return;   /* lone '|' is not a token */
    default:  t->type = TK_EOF; return;   /* unknown char -> treated as EOF */
  }
}
