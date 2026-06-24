/*
** spt_codegen.h
** AST-to-Lua 5.5 bytecode compiler
** Replaces lparser.c with a front-end that walks a pre-built AST
** and emits Lua 5.5 bytecode through the lcode.h interface.
**
** Usage:
**   LClosure *cl = astY_compile(L, ast_root, dyd, source_name);
**
** The returned LClosure is pinned on the Lua stack (same contract
** as luaY_parser).
*/

#ifndef spt_codegen_h
#define spt_codegen_h

#include "spt_ast.h"

/*
** 兼容层：原 C++ AST 是类层级，本移植统一为单一 AstNode 标签联合。
** 将所有旧节点类名 typedef 为 AstNode，使签名/前置声明中的类型名仍可编译；
** 节点负载通过 AstNode.u.<variant> 访问（codegen 内已机械改写）。
*/
typedef AstNode BlockNode;
typedef AstNode Statement;
typedef AstNode Expression;
typedef AstNode Declaration;
typedef AstNode AstType;
typedef AstNode MapEntryNode;
typedef AstNode ParameterDeclNode;
typedef AstNode LiteralIntNode;
typedef AstNode LiteralFloatNode;
typedef AstNode LiteralStringNode;
typedef AstNode LiteralBoolNode;
typedef AstNode LiteralNullNode;
typedef AstNode LiteralListNode;
typedef AstNode LiteralMapNode;
typedef AstNode IdentifierNode;
typedef AstNode UnaryOpNode;
typedef AstNode BinaryOpNode;
typedef AstNode FunctionCallNode;
typedef AstNode MemberAccessNode;
typedef AstNode MemberLookupNode;
typedef AstNode IndexAccessNode;
typedef AstNode LambdaNode;
typedef AstNode ThisExpressionNode;
typedef AstNode VarArgsNode;
typedef AstNode ExpressionStatementNode;
typedef AstNode AssignmentNode;
typedef AstNode UpdateAssignmentNode;
typedef AstNode IfClauseNode;
typedef AstNode IfStatementNode;
typedef AstNode WhileStatementNode;
typedef AstNode ForNumericStatementNode;
typedef AstNode ForEachStatementNode;
typedef AstNode BreakStatementNode;
typedef AstNode ContinueStatementNode;
typedef AstNode ReturnStatementNode;
typedef AstNode VariableDeclNode;
typedef AstNode MutiVariableDeclarationNode;
typedef AstNode FunctionDeclNode;
typedef AstNode ClassMemberNode;
typedef AstNode ClassDeclNode;
typedef AstNode ImportNamespaceNode;
typedef AstNode ImportNamedNode;
typedef AstNode ImportSpecifierNode;
typedef AstNode DeferStatementNode;

#ifdef __cplusplus
extern "C" {
#endif

#include "lobject.h"
#include "lparser.h" /* FuncState, expdesc, Dyndata, BlockCnt, etc. */
#include "lua.h"
#include "lzio.h"

/*
** Main entry point – compiles an AST tree into a Lua closure.
**
** Parameters:
**   L          – Lua state (must have enough stack space)
**   root       – root AstNode* (expected to be a BlockNode for a module)
**   dyd        – dynamic data (same struct Lua's parser uses; will be reset)
**   name       – source name shown in debug info (e.g. "@myfile.spt")
**
** Returns:
**   LClosure* pushed on top of the Lua stack (caller pops it).
**   On error, throws a Lua error through luaD_throw.
*/
LUALIB_API LClosure *astY_compile(lua_State *L, AstNode *root, Dyndata *dyd, const char *name);

/*
** Compile a single function body from a LambdaNode or FunctionDeclNode.
** Mostly internal, but exposed for testing / REPL use.
*/
LUALIB_API Proto *astY_compileFunction(lua_State *L, FuncState *parent_fs, Dyndata *dyd,
                                      AstNode *funcNode, const char *name);

#ifdef __cplusplus
}
#endif

#endif /* spt_codegen_h */
