/*
** spt_frontend.h — 前端公共入口（源码 -> AST）。
**
**   - spt_frontend_parse(source, filename)：source 非空则解析 source，否则读取 filename。
**     成功返回模块根 AstNode*（NODE_BLOCK）；语法错误打印到 stderr 并返回 NULL。
**   - spt_frontend_destroy(node)：释放整棵 AST（内部为一次 arena 销毁，O(1)）。
**
** 注：纯 C 接口，无 extern "C"。C++ 用户请用 extern "C" { } 包裹本头（与 lua.h 一致）。
** LUALIB_API 用于 SHARED 库构建时导出符号（与 lua.h / spt_module.h 一致）。
*/
#ifndef SPT_FRONTEND_H
#define SPT_FRONTEND_H

#include "luaconf.h" /* for LUALIB_API */

struct AstNode;

LUALIB_API struct AstNode *spt_frontend_parse(const char *sourceCode_, const char *filename_);
LUALIB_API void spt_frontend_destroy(struct AstNode *node);

#endif /* SPT_FRONTEND_H */
