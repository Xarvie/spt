/*
** spt_diag.h
** ---------------------------------------------------------------------------
** 统一诊断模型（见迁移规划书 §5.5）。前端（词法/语法）不依赖 Lua state，
** 错误写入诊断缓冲，格式为：
**
**   error: <message>
**     --> <file>:<line>:<col>
**      |
**   12 |   print(add(1, 2 ;
**      |                  ^
**
** spt_frontend_parse 失败时返回 NULL，调用方可取出已格式化的诊断文本打印。
** ---------------------------------------------------------------------------
*/
#ifndef SPT_DIAG_H
#define SPT_DIAG_H

#include <stddef.h>

#define SPT_DIAG_MAX 32 /* 单次编译最多收集的诊断条数 */

typedef struct {
  int line;
  int column;
  char message[256];
} SptDiagEntry;

typedef struct {
  const char *filename;   /* 源文件名（用于 --> 行），不取所有权 */
  const char *source;     /* 源码全文（用于打印出错行 + caret），不取所有权 */
  size_t source_len;
  SptDiagEntry entries[SPT_DIAG_MAX];
  int count;              /* 已记录条数（可能因溢出而被截断到 SPT_DIAG_MAX） */
  int overflow;           /* 是否发生过溢出 */
} SptDiag;

/* 初始化诊断上下文。 */
void spt_diag_init(SptDiag *d, const char *filename, const char *source, size_t source_len);

/* 记录一条错误（printf 风格）。超过容量则丢弃并置 overflow。 */
void spt_diag_error(SptDiag *d, int line, int column, const char *fmt, ...);

/* 是否已有错误。 */
int spt_diag_has_error(const SptDiag *d);

/* 将全部诊断格式化（含源码行与 caret）写入 stderr。 */
void spt_diag_print(const SptDiag *d);

#endif /* SPT_DIAG_H */
