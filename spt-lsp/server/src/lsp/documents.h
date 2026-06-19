/*
** documents.h — 文档存储 + 位置换算（UTF-16 ↔ 字节偏移）。
**
** 文本在存入时做 CRLF/CR -> LF 规范化（与前端解析一致，且不改变 LSP 的
** line/character 语义）。每个文档维护行起始字节偏移索引，用于 O(log n) 定位行，
** 行内再按 UTF-8 解码计 UTF-16 码元做 character 换算。
*/
#ifndef SPT_LSP_DOCUMENTS_H
#define SPT_LSP_DOCUMENTS_H

#include "protocol.h"

#include <stddef.h>

typedef struct {
  char *uri;       /* 拥有，NUL 结尾 */
  char *text;      /* 拥有，LF 规范化后的 UTF-8，NUL 结尾 */
  size_t text_len; /* 不含末尾 NUL */
  int version;
  size_t *line_starts; /* line_starts[i] = 第 i 行起始字节偏移 */
  int line_count;      /* 行数（至少 1） */
} Document;

typedef struct {
  Document **docs;
  int count;
  int cap;
} DocStore;

void doc_store_init(DocStore *s);
void doc_store_free(DocStore *s);

/* 打开/替换/关闭（Full 同步：每次给整篇文本）。text 会被复制并规范化。 */
Document *doc_store_open(DocStore *s, const char *uri, const char *text, size_t text_len,
                         int version);
Document *doc_store_change(DocStore *s, const char *uri, const char *text, size_t text_len,
                           int version);
void doc_store_close(DocStore *s, const char *uri);

/* Phase 4: 增量同步——用 replacement 替换文档中 [start_off, end_off) 字节区间。
   replacement 会被 LF 规范化。返回文档指针。 */
Document *doc_store_change_range(DocStore *s, const char *uri, size_t start_off, size_t end_off,
                                 const char *replacement, size_t repl_len, int version);

Document *doc_store_get(DocStore *s, const char *uri);

/* ---- 位置换算 ---- */
/* LSP 位置 -> 字节偏移（钳制到合法范围）。 */
size_t doc_offset_at(const Document *d, LspPos p);
/* 字节偏移 -> LSP 位置。 */
LspPos doc_pos_at(const Document *d, size_t off);
/* 字节区间 -> LSP 范围。 */
LspRange doc_range(const Document *d, size_t start, size_t end);

/* 前端坐标（line 1 起，column 1 起且按字节计）-> LSP 位置（0 起，UTF-16）。 */
LspPos doc_pos_from_frontend(const Document *d, int line1, int col1_bytes);

#endif /* SPT_LSP_DOCUMENTS_H */
