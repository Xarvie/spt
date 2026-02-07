/*
** ast_codegen.h
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

#ifndef ast_codegen_h
#define ast_codegen_h

#include "ast.h"

#ifdef __cplusplus
extern "C" {
#endif

#include "lua.h"
#include "lobject.h"
#include "lparser.h"      /* FuncState, expdesc, Dyndata, BlockCnt, etc. */
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
LUAI_FUNC LClosure *astY_compile(lua_State *L, AstNode *root,
                                  Dyndata *dyd, const char *name);

/*
** Compile a single function body from a LambdaNode or FunctionDeclNode.
** Mostly internal, but exposed for testing / REPL use.
*/
LUAI_FUNC Proto *astY_compileFunction(lua_State *L, FuncState *parent_fs,
                                       Dyndata *dyd, AstNode *funcNode,
                                       const char *name);

#ifdef __cplusplus
}
#endif

#endif /* ast_codegen_h */
