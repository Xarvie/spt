/*
** spt_lsp_bridge.h — 前端面向语言服务器的稳定桥。
**
** 给定源码，产出一个「分析单元」：尽力而为的 AST + 诊断 + token 列表 + 规范化源码。
** 单元自带 arena，整体生命周期由 spt_lsp_unit_free 管理（不走 spt_frontend_parse 的全局注册表）。
**
** 与 spt_frontend_parse 的区别：
**   - 容错：即使有语法错误也返回（可能不完整的）AST，便于在编辑中途的代码上提供功能。
**   - 暴露诊断（行/列/消息）与 token（种类/跨度/文档注释），供 diagnostics 与语义高亮。
**   - CRLF/CR 规范化为 LF（与编译路径一致），位置以规范化后的源码为准。
*/
#ifndef SPT_LSP_BRIDGE_H
#define SPT_LSP_BRIDGE_H

#include "spt_ast.h"
#include "spt_token.h"

#include <stddef.h>

typedef struct {
  int line;            /* 1 起 */
  int column;          /* 1 起，按字节计 */
  const char *message; /* arena 所有，NUL 结尾 */
} SptLspDiag;

typedef struct SptLspUnit {
  void *arena; /* SptArena*，拥有以下所有内存 */

  AstNode *root; /* 尽力而为的根（NODE_BLOCK）；词法严重失败时可能为 NULL */

  SptLspDiag *diags;
  int diag_count;

  const SptToken *tokens; /* 末尾含 TOK_EOF；词法失败时 token_count 可能为 0 */
  int token_count;

  const char *source; /* 规范化（LF）后的源码副本，NUL 结尾 */
  size_t source_len;
} SptLspUnit;

/* 解析源码为分析单元。永不返回 NULL（除 OOM）；诊断在 unit->diags。 */
SptLspUnit *spt_lsp_parse(const char *source, size_t len);

/* 释放单元及其全部内存。u 可为 NULL。 */
void spt_lsp_unit_free(SptLspUnit *u);

#endif /* SPT_LSP_BRIDGE_H */
