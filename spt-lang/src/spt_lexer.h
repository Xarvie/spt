/*
** spt_lexer.h
** ---------------------------------------------------------------------------
** 手写词法分析器（替代 ANTLR 生成的 LangLexer）。
** 一次性将源码切分为 Token 数组，供解析器做任意前瞻（见规划书 §5.1）。
** ---------------------------------------------------------------------------
*/
#ifndef SPT_LEXER_H
#define SPT_LEXER_H

#include "spt_arena.h"
#include "spt_diag.h"
#include "spt_token.h"

#include <stddef.h>

typedef struct {
  SptToken *tokens; /* arena 所有；末尾恒有一个 TOK_EOF */
  int count;        /* 含末尾 EOF */
} SptTokenArray;

/* 将 [source, source+len) 切分为 token。token 数组从 arena 分配。
** 成功返回 1；遇到非法字符等词法错误返回 0 并向 diag 写入诊断。 */
int spt_lex(const char *source, size_t len, SptArena *arena, SptDiag *diag, SptTokenArray *out);

#endif /* SPT_LEXER_H */
