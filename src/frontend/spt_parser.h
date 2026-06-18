/*
** spt_parser.h
** ---------------------------------------------------------------------------
** 手写递归下降 + 优先级爬升解析器（替代 ANTLR 生成的 LangParser）。
** 直接消费词法器产出的 token 数组，构建 spt_ast 的标签联合 AST。
**
** 表达式优先级（数字越大结合越紧，全部左结合）：
**   || < && < | < ^ < & < ==,!= < <,>,<=,>= < <<,>> < .. < +,- < *,/,~/,%
** 一元 (!,-,#,~) 右结合；后缀 (调用/索引/成员) 最高。
**
** 与原 visitor 行为对齐的要点见 spt_parser.c 内注释。
** ---------------------------------------------------------------------------
*/
#ifndef SPT_PARSER_H
#define SPT_PARSER_H

#include "spt_arena.h"
#include "spt_ast.h"
#include "spt_diag.h"
#include "spt_lexer.h"

/* 解析整个编译单元，返回模块根 BlockNode（NODE_BLOCK）。
** 失败返回 NULL 并向 diag 写入诊断。AST 全部从 arena 分配。 */
AstNode *spt_parse(const SptTokenArray *toks, SptArena *arena, SptDiag *diag);

/* 容错解析：无论有无语法错误都返回尽力而为的 AST（带 panic-mode 恢复）；
** 诊断写入 diag。供语言服务器在用户编辑中途的不完整代码上工作。 */
AstNode *spt_parse_tolerant(const SptTokenArray *toks, SptArena *arena, SptDiag *diag);

#endif /* SPT_PARSER_H */
