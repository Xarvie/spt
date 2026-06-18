/*
** spt_ast_dump.h — AST 转储工具（独立工具，不污染语言核心）
**
** 用法：
**   spt_ast_dump <file.spt>
**
** 输出树形结构到 stdout，用于调试和 LSP 开发。
*/
#ifndef SPT_AST_DUMP_H
#define SPT_AST_DUMP_H

#include "spt_ast.h"

/* 将 AST 以树形结构打印到 stdout。root 为模块根 Block 节点。 */
void spt_ast_dump(const AstNode *root);

#endif /* SPT_AST_DUMP_H */
