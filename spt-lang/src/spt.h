/*
** spt.h — SPT 公共 API 门面头文件。
**
** 一行 include 即可获得嵌入 SPT 所需的全部声明：
**
**   C:
**     #include "spt.h"
**
**   C++:
**     extern "C" {
**     #include "spt.h"
**     }
**
** 快速上手（嵌入 SPT 运行一段脚本）：
**
**   lua_State *L = luaL_newstate();
**   luaL_openlibs(L);
**   spt_register_module_loader(L, ".");       // 支持 import "xxx.spt"
**
**   const char *src = "print(\"hello SPT\");";
**   AstNode *ast = spt_frontend_parse(src, "test.spt");
**   if (ast) {
**       Dyndata dyd = {0};
**       astY_compile(L, ast, &dyd, "@test.spt");  // AST -> 字节码
**       spt_frontend_destroy(ast);
**       lua_pcall(L, 0, 0, 0);                    // 执行
**   }
**   lua_close(L);
**
** 各层职责：
**   lua.h / lualib.h / lauxlib.h  — 魔改 Lua 5.5 VM（状态机、GC、标准库）
**   spt_ast.h                      — AST 节点定义（标签联合 + Arena）
**   spt_frontend.h                 — 源码 -> AST（词法 + 语法）
**   spt_codegen.h                  — AST -> 字节码
**   spt_module.h                   — import "xxx.spt" 模块加载器
**
** 与官方 Lua 一样，本头不内嵌 extern "C"；C++ 用户请自行包裹（见上方示例）。
*/
#ifndef SPT_H
#define SPT_H

/* ---- VM 层：Lua 5.5 核心 API ---- */
#include "luaconf.h"
#include "lua.h"
#include "lualib.h"
#include "lauxlib.h"

/* ---- 前端层：AST 数据结构 + 源码解析 ---- */
#include "spt_ast.h"
#include "spt_frontend.h"

/* ---- 代码生成层：AST -> 字节码 ---- */
#include "spt_codegen.h"

/* ---- 模块加载：import "xxx.spt" ---- */
#include "spt_module.h"

#endif /* SPT_H */
