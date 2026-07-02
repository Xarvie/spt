/*
** protocol.h — LSP 公共类型与 JSON 互转，以及各类枚举常量。
**
** 位置语义（LSP 规范）：line 0 起；character 为该行内的 UTF-16 码元计数
** （不含行终止符）。文档存储以 LF 规范化后的 UTF-8 持有文本，行/字符语义不变
** （CRLF/CR 的终止符不计入 character）。
*/
#ifndef SPT_LSP_PROTOCOL_H
#define SPT_LSP_PROTOCOL_H

#include "cJSON.h"

typedef struct {
  int line;      /* 0 起 */
  int character; /* 0 起，UTF-16 码元 */
} LspPos;

/* 文本区间 [start, end)，起止均为 LSP 位置。 */
typedef struct {
  LspPos start;
  LspPos end;
} LspRange;

/* JSON 互转 */
cJSON *lsp_pos_to_json(LspPos p);
cJSON *lsp_range_to_json(LspRange r);
LspPos lsp_pos_from_json(const cJSON *o);     /* 读取 {line,character}，缺失为 0 */
LspRange lsp_range_from_json(const cJSON *o); /* 读取 {start,end}，子字段缺失则对应位置为 0。 */

/* DiagnosticSeverity */
enum { LSP_SEV_ERROR = 1, LSP_SEV_WARNING = 2, LSP_SEV_INFO = 3, LSP_SEV_HINT = 4 };

/* SymbolKind（LSP 规范子集；我们用到的） */
enum {
  LSP_SK_FILE = 1,
  LSP_SK_MODULE = 2,
  LSP_SK_NAMESPACE = 3,
  LSP_SK_CLASS = 5,
  LSP_SK_METHOD = 6,
  LSP_SK_PROPERTY = 7,
  LSP_SK_FIELD = 8,
  LSP_SK_CONSTRUCTOR = 9,
  LSP_SK_FUNCTION = 12,
  LSP_SK_VARIABLE = 13,
  LSP_SK_CONSTANT = 14,
  LSP_SK_STRING = 15,
  LSP_SK_NUMBER = 16,
  LSP_SK_BOOLEAN = 17,
  LSP_SK_ARRAY = 18,
  LSP_SK_OBJECT = 19,
  LSP_SK_KEY = 20,
  LSP_SK_ENUMMEMBER = 22,
  LSP_SK_STRUCT = 23,
  LSP_SK_TYPEPARAMETER = 26
};

/* CompletionItemKind（子集） */
enum {
  LSP_CIK_METHOD = 2,
  LSP_CIK_FUNCTION = 3,
  LSP_CIK_CONSTRUCTOR = 4,
  LSP_CIK_FIELD = 5,
  LSP_CIK_VARIABLE = 6,
  LSP_CIK_CLASS = 7,
  LSP_CIK_MODULE = 9,
  LSP_CIK_PROPERTY = 10,
  LSP_CIK_KEYWORD = 14,
  LSP_CIK_SNIPPET = 15,
  LSP_CIK_CONSTANT = 21,
  LSP_CIK_STRUCT = 22,
  LSP_CIK_TYPEPARAMETER = 25
};

#endif /* SPT_LSP_PROTOCOL_H */
