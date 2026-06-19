/*
** loadast.h — 前端入口（替代原 ANTLR 版 loadAst）。
**
** 契约与原 Ast/ast.h 完全一致，供后端 astY_compile 及 CLI 调用：
**   - loadAst(source, filename)：source 非空则解析 source，否则读取 filename。
**     成功返回模块根 AstNode*（NODE_BLOCK）；语法错误打印到 stderr 并返回 NULL。
**   - destroyAst(node)：释放整棵 AST（内部为一次 arena 销毁，O(1)）。
**
** 注：新实现为纯 C，无 extern "C"。整条工具链（codegen / spt_module / CLI）均为 C。
*/
#ifndef SPT_LOADAST_H
#define SPT_LOADAST_H

struct AstNode;

struct AstNode *loadAst(const char *sourceCode_, const char *filename_);
void destroyAst(struct AstNode *node);

#endif /* SPT_LOADAST_H */
