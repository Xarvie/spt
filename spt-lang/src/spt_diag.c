/*
** spt_diag.c — 诊断收集与格式化实现。
*/
#include "spt_diag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

void spt_diag_init(SptDiag *d, const char *filename, const char *source, size_t source_len) {
  d->filename = filename ? filename : "?";
  d->source = source;
  d->source_len = source_len;
  d->count = 0;
  d->overflow = 0;
}

void spt_diag_error(SptDiag *d, int line, int column, const char *fmt, ...) {
  if (d->count >= SPT_DIAG_MAX) {
    d->overflow = 1;
    return;
  }
  SptDiagEntry *e = &d->entries[d->count++];
  e->line = line;
  e->column = column;
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e->message, sizeof(e->message), fmt, ap);
  va_end(ap);
}

int spt_diag_has_error(const SptDiag *d) { return d->count > 0; }

/* 取第 line 行（1 起）在 source 中的 [begin,end) 字节范围。失败返回 0。 */
static int find_line(const SptDiag *d, int line, size_t *begin, size_t *end) {
  if (!d->source || line < 1)
    return 0;
  int cur = 1;
  size_t i = 0;
  size_t start = 0;
  while (i < d->source_len && cur < line) {
    if (d->source[i] == '\n') {
      cur++;
      start = i + 1;
    }
    i++;
  }
  if (cur != line)
    return 0;
  size_t j = start;
  while (j < d->source_len && d->source[j] != '\n')
    j++;
  /* 去掉行尾 \r */
  if (j > start && d->source[j - 1] == '\r')
    j--;
  *begin = start;
  *end = j;
  return 1;
}

void spt_diag_print(const SptDiag *d) {
  for (int k = 0; k < d->count; k++) {
    const SptDiagEntry *e = &d->entries[k];
    fprintf(stderr, "error: %s\n", e->message);
    fprintf(stderr, "  --> %s:%d:%d\n", d->filename, e->line, e->column);

    size_t b = 0, en = 0;
    if (find_line(d, e->line, &b, &en)) {
      int width = fprintf(stderr, "%d", e->line) - 0; /* 行号宽度（用于对齐竖线） */
      /* 上方空行 */
      fprintf(stderr, "%*s |\n", width, "");
      /* 源码行 */
      fprintf(stderr, "%d | %.*s\n", e->line, (int)(en - b), d->source + b);
      /* caret 行：列对齐（按字节，制表符按 1 处理） */
      fprintf(stderr, "%*s | ", width, "");
      int col = e->column < 1 ? 1 : e->column;
      for (int c = 1; c < col; c++) {
        char ch = (b + (size_t)(c - 1) < en) ? d->source[b + (size_t)(c - 1)] : ' ';
        fputc(ch == '\t' ? '\t' : ' ', stderr);
      }
      fputc('^', stderr);
      fputc('\n', stderr);
    }
  }
  if (d->overflow)
    fprintf(stderr, "error: 诊断信息过多，已截断（仅显示前 %d 条）\n", SPT_DIAG_MAX);
}
