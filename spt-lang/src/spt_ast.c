/*
** spt_ast.c — AST 构造辅助与名称表。
**
** 注意：本文件**没有** spt_frontend_destroy 的递归析构逻辑——AST 生命周期完全由
** arena 管理（见 spt_frontend.c 中的 spt_frontend_destroy = spt_arena_destroy）。
** 这正是「标签联合 + Arena」相对原 C++ 析构链的核心简化。
*/
#include "spt_ast.h"
#include "spt_token.h"

#include <string.h>

AstNode *spt_ast_new(SptArena *a, NodeType type, SourceLocation loc) {
  AstNode *n = (AstNode *)spt_arena_alloc(a, sizeof(AstNode));
  if (!n)
    return NULL;
  n->type = type;
  n->loc = loc;
  /* 联合体已被 arena 清零 */
  return n;
}

AstList spt_ast_list_from(SptArena *a, AstNode **items, int count) {
  AstList l;
  l.count = count;
  if (count <= 0) {
    l.items = NULL;
    return l;
  }
  l.items = (AstNode **)spt_arena_alloc(a, sizeof(AstNode *) * (size_t)count);
  if (l.items)
    memcpy(l.items, items, sizeof(AstNode *) * (size_t)count);
  else
    l.count = 0;
  return l;
}

const char *spt_op_name(OperatorKind op) {
  switch (op) {
  case OPK_NEGATE:           return "-(neg)";
  case OPK_NOT:              return "!";
  case OPK_LENGTH:           return "#";
  case OPK_BW_NOT:           return "~";
  case OPK_ADD:              return "+";
  case OPK_SUB:              return "-";
  case OPK_MUL:              return "*";
  case OPK_DIV:              return "/";
  case OPK_IDIV:             return "~/";
  case OPK_MOD:              return "%";
  case OPK_CONCAT:           return "..";
  case OPK_LT:               return "<";
  case OPK_LE:               return "<=";
  case OPK_GT:               return ">";
  case OPK_GE:               return ">=";
  case OPK_EQ:               return "==";
  case OPK_NE:               return "!=";
  case OPK_AND:              return "&&";
  case OPK_OR:               return "||";
  case OPK_BW_AND:           return "&";
  case OPK_BW_OR:            return "|";
  case OPK_BW_XOR:           return "^";
  case OPK_BW_LSHIFT:        return "<<";
  case OPK_BW_RSHIFT:        return ">>";
  case OPK_ASSIGN_ADD:       return "+=";
  case OPK_ASSIGN_SUB:       return "-=";
  case OPK_ASSIGN_MUL:       return "*=";
  case OPK_ASSIGN_DIV:       return "/=";
  case OPK_ASSIGN_IDIV:      return "~/=";
  case OPK_ASSIGN_MOD:       return "%=";
  case OPK_ASSIGN_CONCAT:    return "..=";
  case OPK_ASSIGN_BW_AND:    return "&=";
  case OPK_ASSIGN_BW_OR:     return "|=";
  case OPK_ASSIGN_BW_XOR:    return "^=";
  case OPK_ASSIGN_BW_LSHIFT: return "<<=";
  case OPK_ASSIGN_BW_RSHIFT: return ">>=";
  }
  return "?";
}

const char *spt_node_type_name(NodeType t) {
  switch (t) {
  case NODE_LITERAL_INT:           return "LiteralInt";
  case NODE_LITERAL_FLOAT:         return "LiteralFloat";
  case NODE_LITERAL_STRING:        return "LiteralString";
  case NODE_LITERAL_BOOL:          return "LiteralBool";
  case NODE_LITERAL_NULL:          return "LiteralNull";
  case NODE_LITERAL_LIST:          return "LiteralList";
  case NODE_LITERAL_MAP:           return "LiteralMap";
  case NODE_MAP_ENTRY:             return "MapEntry";
  case NODE_IDENTIFIER:            return "Identifier";
  case NODE_UNARY_OP:              return "UnaryOp";
  case NODE_BINARY_OP:             return "BinaryOp";
  case NODE_FUNCTION_CALL:         return "FunctionCall";
  case NODE_MEMBER_ACCESS:         return "MemberAccess";
  case NODE_MEMBER_LOOKUP:         return "MemberLookup";
  case NODE_INDEX_ACCESS:          return "IndexAccess";
  case NODE_LAMBDA:                return "Lambda";
  case NODE_THIS_EXPRESSION:       return "This";
  case NODE_VAR_ARGS:              return "VarArgs";
  case NODE_BLOCK:                 return "Block";
  case NODE_EXPRESSION_STATEMENT:  return "ExpressionStatement";
  case NODE_ASSIGNMENT:            return "Assignment";
  case NODE_UPDATE_ASSIGNMENT:     return "UpdateAssignment";
  case NODE_IF_STATEMENT:          return "IfStatement";
  case NODE_IF_CLAUSE:             return "IfClause";
  case NODE_WHILE_STATEMENT:       return "WhileStatement";
  case NODE_FOR_NUMERIC_STATEMENT: return "ForNumericStatement";
  case NODE_FOR_EACH_STATEMENT:    return "ForEachStatement";
  case NODE_BREAK_STATEMENT:       return "BreakStatement";
  case NODE_CONTINUE_STATEMENT:    return "ContinueStatement";
  case NODE_RETURN_STATEMENT:      return "ReturnStatement";
  case NODE_IMPORT_NAMESPACE:      return "ImportNamespace";
  case NODE_IMPORT_NAMED:          return "ImportNamed";
  case NODE_IMPORT_SPECIFIER:      return "ImportSpecifier";
  case NODE_DEFER_STATEMENT:       return "DeferStatement";
  case NODE_VARIABLE_DECL:         return "VariableDecl";
  case NODE_MUTI_VARIABLE_DECL:    return "MutiVariableDecl";
  case NODE_PARAMETER_DECL:        return "ParameterDecl";
  case NODE_FUNCTION_DECL:         return "FunctionDecl";
  case NODE_CLASS_DECL:            return "ClassDecl";
  case NODE_CLASS_MEMBER:          return "ClassMember";
  case NODE_DECLARE_MODULE:        return "DeclareModule";
  case NODE_TYPE_PRIMITIVE:        return "PrimitiveType";
  case NODE_TYPE_ANY:              return "AnyType";
  case NODE_TYPE_AUTO:             return "AutoType";
  case NODE_TYPE_LIST:             return "ListType";
  case NODE_TYPE_MAP:              return "MapType";
  case NODE_TYPE_USER:             return "UserType";
  case NODE_TYPE_FUNCTION_KW:      return "FunctionType";
  case NODE_TYPE_COROUTINE_KW:     return "CoroutineType";
  case NODE_TYPE_MULTIRETURN:      return "MultiReturnType";
  }
  return "?";
}

const char *spt_token_name(SptTokenKind kind) {
  switch (kind) {
  case TOK_EOF:            return "<EOF>";
  case TOK_INT:            return "'int'";
  case TOK_FLOAT:          return "'float'";
  case TOK_NUMBER:         return "'number'";
  case TOK_STR:            return "'str'";
  case TOK_BOOL:           return "'bool'";
  case TOK_ANY:            return "'any'";
  case TOK_VOID:           return "'void'";
  case TOK_NULL:           return "'null'";
  case TOK_LIST:           return "'list'";
  case TOK_MAP:            return "'map'";
  case TOK_FUNCTION:       return "'function'";
  case TOK_COROUTINE:      return "'coro'";
  case TOK_VARS:           return "'vars'";
  case TOK_IF:             return "'if'";
  case TOK_ELSE:           return "'else'";
  case TOK_WHILE:          return "'while'";
  case TOK_FOR:            return "'for'";
  case TOK_BREAK:          return "'break'";
  case TOK_CONTINUE:       return "'continue'";
  case TOK_RETURN:         return "'return'";
  case TOK_DEFER:          return "'defer'";
  case TOK_TRUE:           return "'true'";
  case TOK_FALSE:          return "'false'";
  case TOK_CONST:          return "'const'";
  case TOK_AUTO:           return "'auto'";
  case TOK_GLOBAL:         return "'global'";
  case TOK_STATIC:         return "'static'";
  case TOK_IMPORT:         return "'import'";
  case TOK_AS:             return "'as'";
  case TOK_FROM:           return "'from'";
  case TOK_EXPORT:         return "'export'";
  case TOK_DECLARE:        return "'declare'";
  case TOK_CLASS:          return "'class'";
  case TOK_ADD:            return "'+'";
  case TOK_SUB:            return "'-'";
  case TOK_MUL:            return "'*'";
  case TOK_DIV:            return "'/'";
  case TOK_IDIV:           return "'~/'";
  case TOK_MOD:            return "'%'";
  case TOK_ASSIGN:         return "'='";
  case TOK_ADD_ASSIGN:     return "'+='";
  case TOK_SUB_ASSIGN:     return "'-='";
  case TOK_MUL_ASSIGN:     return "'*='";
  case TOK_DIV_ASSIGN:     return "'/='";
  case TOK_IDIV_ASSIGN:    return "'~/='";
  case TOK_MOD_ASSIGN:     return "'%='";
  case TOK_CONCAT_ASSIGN:  return "'..='";
  case TOK_EQ:             return "'=='";
  case TOK_NEQ:            return "'!='";
  case TOK_LT:             return "'<'";
  case TOK_GT:             return "'>'";
  case TOK_LTE:            return "'<='";
  case TOK_GTE:            return "'>='";
  case TOK_AND:            return "'&&'";
  case TOK_OR:             return "'||'";
  case TOK_NOT:            return "'!'";
  case TOK_CONCAT:         return "'..'";
  case TOK_LEN:            return "'#'";
  case TOK_BIT_AND:        return "'&'";
  case TOK_BIT_OR:         return "'|'";
  case TOK_BIT_XOR:        return "'^'";
  case TOK_BIT_NOT:        return "'~'";
  case TOK_LSHIFT:         return "'<<'";
  case TOK_ARROW:          return "'->'";
  case TOK_LPAREN:         return "'('";
  case TOK_RPAREN:         return "')'";
  case TOK_LBRACKET:       return "'['";
  case TOK_RBRACKET:       return "']'";
  case TOK_LBRACE:         return "'{'";
  case TOK_RBRACE:         return "'}'";
  case TOK_COMMA:          return "','";
  case TOK_DOT:            return "'.'";
  case TOK_COLON:          return "':'";
  case TOK_SEMICOLON:      return "';'";
  case TOK_ELLIPSIS:       return "'...'";
  case TOK_INTEGER:        return "integer";
  case TOK_FLOAT_LITERAL:  return "float";
  case TOK_STRING_LITERAL: return "string";
  case TOK_IDENTIFIER:     return "identifier";
  case TOK_KIND_COUNT:     break;
  }
  return "<token>";
}
