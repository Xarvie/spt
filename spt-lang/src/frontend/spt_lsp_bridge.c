/*
** spt_lsp_bridge.c — 前端 -> 语言服务器分析单元。
*/
#include "spt_lsp_bridge.h"

#include "spt_arena.h"
#include "spt_diag.h"
#include "spt_lexer.h"
#include "spt_parser.h"

#include <stdlib.h>
#include <string.h>

/* CRLF/CR -> LF 就地规范化（仅缩短）。返回新长度。与 loadast.c 一致。 */
static size_t normalize_newlines(char *s, size_t len) {
  size_t w = 0;
  for (size_t r = 0; r < len; r++) {
    char c = s[r];
    if (c == '\r') {
      s[w++] = '\n';
      if (r + 1 < len && s[r + 1] == '\n')
        r++;
    } else {
      s[w++] = c;
    }
  }
  s[w] = '\0';
  return w;
}

SptLspUnit *spt_lsp_parse(const char *source, size_t len) {
  SptArena *arena = spt_arena_create(0);
  if (!arena)
    return NULL;

  SptLspUnit *u = (SptLspUnit *)spt_arena_alloc(arena, sizeof(SptLspUnit));
  if (!u) {
    spt_arena_destroy(arena);
    return NULL;
  }
  u->arena = arena;
  u->root = NULL;
  u->diags = NULL;
  u->diag_count = 0;
  u->tokens = NULL;
  u->token_count = 0;

  /* 源码复制进 arena 并规范化换行（token 词素与 AST 字符串都指向这里）。 */
  char *code = spt_arena_strndup(arena, source ? source : "", source ? len : 0);
  size_t code_len = normalize_newlines(code, source ? len : 0);
  u->source = code;
  u->source_len = code_len;

  SptDiag diag;
  spt_diag_init(&diag, "<lsp>", code, code_len);

  SptTokenArray toks;
  int lex_ok = spt_lex(code, code_len, arena, &diag, &toks);
  if (lex_ok) {
    u->tokens = toks.tokens;
    u->token_count = toks.count;
    u->root = spt_parse_tolerant(&toks, arena, &diag);
  }

  /* 拷贝诊断到 arena（稳定，独立于栈上的 diag）。 */
  if (diag.count > 0) {
    u->diags = (SptLspDiag *)spt_arena_alloc(arena, sizeof(SptLspDiag) * (size_t)diag.count);
    for (int i = 0; i < diag.count; i++) {
      u->diags[i].line = diag.entries[i].line;
      u->diags[i].column = diag.entries[i].column;
      u->diags[i].message = spt_arena_strdup(arena, diag.entries[i].message);
    }
    u->diag_count = diag.count;
  }

  return u;
}

void spt_lsp_unit_free(SptLspUnit *u) {
  if (!u)
    return;
  spt_arena_destroy((SptArena *)u->arena);
}
