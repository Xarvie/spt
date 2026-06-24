/*
** spt_frontend.c — 前端编排：源码 -> 词法 -> 语法 -> AST 根节点。
**
** 生命周期策略：
**   - 每次 spt_frontend_parse 创建一个独立 arena，并把源码**复制进 arena**后再做词法/语法，
**     因此 token 词素与 AST 内所有字符串都指向 arena 内存，杜绝悬垂指针。
**   - 返回的根节点登记到全局注册表（根指针 -> arena）；spt_frontend_destroy 据此销毁 arena。
**     模型为单线程（与原 C++ 实现一致：import 期间嵌套 parse/destroy 也按栈式配对）。
*/
#include "spt_frontend.h"

#include "spt_arena.h"
#include "spt_ast.h"
#include "spt_diag.h"
#include "spt_lexer.h"
#include "spt_parser.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- 根节点 -> arena 注册表（单线程） ---- */
typedef struct ArenaReg {
  struct AstNode *root;
  SptArena *arena;
  struct ArenaReg *next;
} ArenaReg;

static ArenaReg *g_reg = NULL;

static void reg_add(struct AstNode *root, SptArena *a) {
  ArenaReg *r = (ArenaReg *)malloc(sizeof(ArenaReg));
  if (!r)
    return;
  r->root = root;
  r->arena = a;
  r->next = g_reg;
  g_reg = r;
}

static SptArena *reg_take(struct AstNode *root) {
  ArenaReg **pp = &g_reg;
  while (*pp) {
    if ((*pp)->root == root) {
      ArenaReg *hit = *pp;
      SptArena *a = hit->arena;
      *pp = hit->next;
      free(hit);
      return a;
    }
    pp = &(*pp)->next;
  }
  return NULL;
}

/* ---- 读取整文件到 malloc 缓冲 ---- */
static char *read_whole_file(const char *filename, size_t *out_len) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return NULL;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return NULL;
  }
  long n = ftell(f);
  if (n < 0) {
    fclose(f);
    return NULL;
  }
  rewind(f);
  char *buf = (char *)malloc((size_t)n + 1);
  if (!buf) {
    fclose(f);
    return NULL;
  }
  size_t rd = fread(buf, 1, (size_t)n, f);
  fclose(f);
  buf[rd] = '\0';
  if (out_len)
    *out_len = rd;
  return buf;
}

/* ---- 行结束符归一化：\r\n -> \n，孤立 \r -> \n（就地压缩，仅缩短）。
   语言将 CRLF/CR 视为与 LF 等价的行终止符，确保字符串字面量长度、
   行号在不同换行风格下行为一致。返回归一化后的长度。 ---- */
static size_t normalize_newlines(char *s, size_t len) {
  size_t w = 0;
  for (size_t r = 0; r < len; r++) {
    char c = s[r];
    if (c == '\r') {
      s[w++] = '\n';
      if (r + 1 < len && s[r + 1] == '\n')
        r++; /* 跳过 \r\n 的 \n */
    } else {
      s[w++] = c;
    }
  }
  s[w] = '\0';
  return w;
}

LUALIB_API struct AstNode *spt_frontend_parse(const char *sourceCode_, const char *filename_) {
  const char *display = (filename_ && filename_[0]) ? filename_ : "<unknown>";

  /* 取原始源码（来自参数或文件），随后复制进 arena。 */
  char *file_buf = NULL;
  const char *raw = NULL;
  size_t raw_len = 0;

  if (sourceCode_ && sourceCode_[0] != '\0') {
    raw = sourceCode_;
    raw_len = strlen(sourceCode_);
  } else {
    if (!filename_ || filename_[0] == '\0') {
      fprintf(stderr, "[Ast Error] Both sourceCode and filename are empty.\n");
      return NULL;
    }
    file_buf = read_whole_file(filename_, &raw_len);
    if (!file_buf) {
      fprintf(stderr, "[Ast Error] Cannot open file: %s\n", filename_);
      return NULL;
    }
    raw = file_buf;
  }

  SptArena *arena = spt_arena_create(0);
  if (!arena) {
    free(file_buf);
    return NULL;
  }

  /* 源码复制进 arena —— token 词素与 AST 字符串将指向此处。 */
  char *code = (char *)spt_arena_strndup(arena, raw, raw_len);
  free(file_buf); /* 原始缓冲不再需要 */
  file_buf = NULL;

  /* 归一化行结束符（CRLF/CR -> LF），就地压缩并更新长度。 */
  raw_len = normalize_newlines(code, raw_len);

  SptDiag diag;
  spt_diag_init(&diag, display, code, raw_len);

  SptTokenArray toks;
  if (!spt_lex(code, raw_len, arena, &diag, &toks)) {
    spt_diag_print(&diag);
    fprintf(stderr, "[Ast Error] Syntax errors in %s\n", display);
    spt_arena_destroy(arena);
    return NULL;
  }

  AstNode *root = spt_parse(&toks, arena, &diag);
  if (!root) {
    spt_diag_print(&diag);
    fprintf(stderr, "[Ast Error] Syntax errors in %s\n", display);
    spt_arena_destroy(arena);
    return NULL;
  }

  reg_add(root, arena);
  return root;
}

LUALIB_API void spt_frontend_destroy(struct AstNode *node) {
  if (!node)
    return;
  SptArena *a = reg_take(node);
  if (a) {
    spt_arena_destroy(a);
  } else {
    /* 未登记：理论上不会发生。可能是重复 destroy 或外部构造的节点。
    ** 静默忽略会掩盖 use-after-free / double-free 类 bug，这里告警。 */
    fprintf(stderr,
            "[Ast Warning] spt_frontend_destroy: node %p not registered (double free?)\n",
            (void *)node);
  }
}
