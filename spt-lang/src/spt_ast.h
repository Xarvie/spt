/*
** spt_ast.h
** ---------------------------------------------------------------------------
** SPT 抽象语法树 —— 以「标签联合 + Arena 分配」取代原 C++ 类层级
** （见迁移规划书 §5.2）。
**
**   - 单一 AstNode 结构：公共头(type + loc) + 各节点负载的 union。
**   - 类型注解节点（PrimitiveType/ListType/…）并入同一联合，统一为 AstNode*。
**   - 所有节点、列表、字符串均从 SptArena 分配；spt_frontend_destroy 即销毁整个 arena。
**   - 节点经 spt_arena_alloc 清零分配，故所有指针字段默认 NULL，
**     与原 C++ 默认成员初始化一致。
**
** 与原 ast.h 的 NodeType / OperatorKind / PrimitiveTypeKind 一一对应，
** 仅加 NODE_/OPK_/PTK_ 前缀（C 枚举无作用域），便于 codegen 机械移植。
** ---------------------------------------------------------------------------
*/
#ifndef SPT_AST_H
#define SPT_AST_H

#include "spt_arena.h"

#include <stdbool.h>
#include <stdint.h>

/* --- 源码位置 --- */
typedef struct {
  int line;   /* 1 起 */
  int column; /* 1 起 */
} SourceLocation;

/* --- 节点种类（含表达式/语句/声明/类型） --- */
typedef enum {
  /* 字面量 */
  NODE_LITERAL_INT,
  NODE_LITERAL_FLOAT,
  NODE_LITERAL_STRING,
  NODE_LITERAL_BOOL,
  NODE_LITERAL_NULL,
  NODE_LITERAL_LIST,
  NODE_LITERAL_MAP,
  NODE_MAP_ENTRY,

  /* 表达式 */
  NODE_IDENTIFIER,
  NODE_UNARY_OP,
  NODE_BINARY_OP,
  NODE_FUNCTION_CALL,
  NODE_MEMBER_ACCESS,
  NODE_MEMBER_LOOKUP, /* 语法暂未产生（无 `:` 方法语法），保留以兼容 codegen */
  NODE_INDEX_ACCESS,
  NODE_LAMBDA,
  NODE_THIS_EXPRESSION,
  NODE_VAR_ARGS,

  /* 语句 */
  NODE_BLOCK,
  NODE_EXPRESSION_STATEMENT,
  NODE_ASSIGNMENT,
  NODE_UPDATE_ASSIGNMENT,
  NODE_IF_STATEMENT,
  NODE_IF_CLAUSE,
  NODE_WHILE_STATEMENT,
  NODE_FOR_NUMERIC_STATEMENT,
  NODE_FOR_EACH_STATEMENT,
  NODE_BREAK_STATEMENT,
  NODE_CONTINUE_STATEMENT,
  NODE_RETURN_STATEMENT,
  NODE_IMPORT_NAMESPACE,
  NODE_IMPORT_NAMED,
  NODE_IMPORT_SPECIFIER,
  NODE_DEFER_STATEMENT,

  /* 声明 */
  NODE_VARIABLE_DECL,
  NODE_MUTI_VARIABLE_DECL,
  NODE_PARAMETER_DECL,
  NODE_FUNCTION_DECL,
  NODE_CLASS_DECL,
  NODE_CLASS_MEMBER,
  NODE_DECLARE_MODULE, /* declare from "..." { ... } 外部模块声明块（编译期擦除） */

  /* 类型节点 */
  NODE_TYPE_PRIMITIVE,
  NODE_TYPE_ANY,
  NODE_TYPE_AUTO,
  NODE_TYPE_LIST,
  NODE_TYPE_MAP,
  NODE_TYPE_USER,
  NODE_TYPE_FUNCTION_KW,
  NODE_TYPE_COROUTINE_KW,
  NODE_TYPE_MULTIRETURN
} NodeType;

/* --- 运算符种类（对应原 OperatorKind） --- */
typedef enum {
  OPK_NEGATE,
  OPK_NOT,
  OPK_LENGTH,
  OPK_BW_NOT,
  OPK_ADD,
  OPK_SUB,
  OPK_MUL,
  OPK_DIV,
  OPK_IDIV,
  OPK_MOD,
  OPK_CONCAT,
  OPK_LT,
  OPK_LE,
  OPK_GT,
  OPK_GE,
  OPK_EQ,
  OPK_NE,
  OPK_AND,
  OPK_OR,
  OPK_BW_AND,
  OPK_BW_OR,
  OPK_BW_XOR,
  OPK_BW_LSHIFT,
  OPK_BW_RSHIFT,
  OPK_ASSIGN_ADD,
  OPK_ASSIGN_SUB,
  OPK_ASSIGN_MUL,
  OPK_ASSIGN_DIV,
  OPK_ASSIGN_IDIV,
  OPK_ASSIGN_MOD,
  OPK_ASSIGN_CONCAT,
  OPK_ASSIGN_BW_AND,
  OPK_ASSIGN_BW_OR,
  OPK_ASSIGN_BW_XOR,
  OPK_ASSIGN_BW_LSHIFT,
  OPK_ASSIGN_BW_RSHIFT
} OperatorKind;

/* --- 基本类型种类（对应原 PrimitiveTypeKind） --- */
typedef enum {
  PTK_INT,
  PTK_FLOAT,
  PTK_NUMBER,
  PTK_STRING,
  PTK_BOOL,
  PTK_VOID,
  PTK_NULL
} PrimitiveTypeKind;

typedef struct AstNode AstNode;

/* 节点指针数组（取代 std::vector<T*>）。items/数组本身均 arena 所有。 */
typedef struct {
  AstNode **items;
  int count;
} AstList;

/* `vars a, b = expr;` 中单个变量的信息（取代 MultiDeclVariableInfo）。 */
typedef struct {
  const char *name;
  bool is_global;
  bool is_const;
} MultiDeclVar;

struct AstNode {
  NodeType type;
  SourceLocation loc;

  union {
    /* ---- 字面量 ---- */
    struct {
      int64_t value;
    } lit_int;
    struct {
      double value;
    } lit_float;
    struct {
      const char *data; /* 已反转义；可含嵌入 NUL */
      int len;
    } lit_str;
    struct {
      bool value;
    } lit_bool;
    /* NODE_LITERAL_NULL: 无负载 */
    struct {
      AstList elements;
    } lit_list;
    struct {
      AstList entries; /* 每个元素为 NODE_MAP_ENTRY */
    } lit_map;
    struct {
      AstNode *key;
      AstNode *value;
    } map_entry;

    /* ---- 表达式 ---- */
    struct {
      const char *name;
    } ident;
    struct {
      OperatorKind op;
      AstNode *operand;
    } unary;
    struct {
      OperatorKind op;
      AstNode *left;
      AstNode *right;
    } binary;
    struct {
      AstNode *func;
      AstList args;
    } call;
    struct { /* MEMBER_ACCESS 与 MEMBER_LOOKUP 共用此布局 */
      AstNode *object;
      const char *member;
    } member;
    struct {
      AstNode *array;
      AstNode *index;
    } index;
    struct {
      AstList params; /* NODE_PARAMETER_DECL 列表 */
      AstNode *return_type;
      AstNode *body; /* NODE_BLOCK */
      bool is_variadic;
    } lambda;
    /* NODE_THIS_EXPRESSION / NODE_VAR_ARGS: 无负载 */

    /* ---- 语句 ---- */
    struct {
      AstList statements;
      SourceLocation end_loc;
      bool use_end;
    } block;
    struct {
      AstNode *expr;
    } expr_stmt;
    struct {
      AstList lvalues;
      AstList rvalues;
    } assign;
    struct {
      OperatorKind op;
      AstNode *lvalue;
      AstNode *rvalue;
    } update;
    struct {
      AstNode *condition;
      AstNode *body;
    } if_clause;
    struct {
      AstNode *condition;
      AstNode *then_block;
      AstList else_if_clauses; /* NODE_IF_CLAUSE 列表 */
      AstNode *else_block;     /* 可空 */
    } if_stmt;
    struct {
      AstNode *condition;
      AstNode *body;
    } while_stmt;
    struct {
      const char *var_name;
      AstNode *type_annotation; /* 可空 */
      AstNode *start;
      AstNode *end;
      AstNode *step; /* 可空 */
      AstNode *body;
    } for_num;
    struct {
      AstList loop_variables; /* NODE_PARAMETER_DECL 列表（类型可空） */
      AstList iterable_exprs;
      AstNode *body;
    } for_each;
    /* NODE_BREAK_STATEMENT / NODE_CONTINUE_STATEMENT: 无负载 */
    struct {
      AstList values;
    } return_stmt;
    struct {
      const char *alias;
      const char *module_path;
    } import_ns;
    struct {
      AstList specifiers; /* NODE_IMPORT_SPECIFIER 列表 */
      const char *module_path;
    } import_named;
    struct {
      const char *imported_name;
      const char *alias; /* 可空（无 as 时为 NULL） */
    } import_spec;
    struct {
      AstNode *body; /* NODE_BLOCK */
    } defer_stmt;

    /* ---- 声明 ---- */
    struct {
      const char *name;
      AstNode *type_annotation; /* 可空（auto 时） */
      AstNode *initializer;     /* 可空 */
      bool is_const;
      bool is_global;
      bool is_static;
      bool is_exported;
      bool is_module_root;
      bool is_ambient;  /* declare 声明：编译期擦除，不产生绑定 */
      const char *doc;  /* 前置文档注释（描述），可空 */
    } var_decl;
    struct {
      MultiDeclVar *vars;
      int count;
      AstNode *initializer; /* 单个初始化表达式，可空 */
      bool is_exported;
      bool is_module_root;
    } muti_var;
    struct {
      const char *name;
      AstNode *type_annotation; /* 可空 */
    } param;
    struct {
      const char *name;
      AstList params;
      AstNode *return_type;
      AstNode *body;
      bool is_global;
      bool is_static;
      bool is_variadic;
      bool is_exported;
      bool is_const;
      bool is_module_root;
      bool is_ambient;  /* declare 声明：编译期擦除，body 为空 */
      const char *doc;  /* 前置文档注释（描述），可空 */
    } func_decl;
    struct {
      AstNode *member_declaration;
      bool is_static;
    } class_member;
    struct {
      const char *name;
      AstList members;
      bool is_exported;
      bool is_module_root;
      bool is_ambient;  /* declare 声明：编译期擦除 */
      const char *doc;  /* 前置文档注释（描述），可空 */
    } class_decl;
    struct {
      const char *module_path; /* declare from "<module_path>" */
      AstList members;         /* var_decl/func_decl/class_decl 节点，均 is_ambient=true */
      const char *doc;         /* 模块级文档注释（描述），可空 */
    } declare_module;

    /* ---- 类型节点 ---- */
    struct {
      PrimitiveTypeKind kind;
    } type_prim;
    /* NODE_TYPE_ANY / AUTO / FUNCTION_KW / COROUTINE_KW / MULTIRETURN: 无负载 */
    struct {
      AstNode *element; /* 可空（裸 list） */
    } type_list;
    struct {
      AstNode *key;   /* 可空（裸 map） */
      AstNode *value; /* 可空 */
    } type_map;
    struct {
      const char **parts; /* 限定名各部分，如 {"Module","Type"} */
      int count;
    } type_user;
  } u;
};

/* ---------------------------------------------------------------------------
** 节点构造：统一从 arena 分配并填好 type/loc，返回零初始化的节点。
** 调用方随后填写联合字段。
** --------------------------------------------------------------------------- */
AstNode *spt_ast_new(SptArena *a, NodeType type, SourceLocation loc);

/* 将临时指针数组拷入 arena，形成 AstList（取代 vector→定稿数组）。 */
AstList spt_ast_list_from(SptArena *a, AstNode **items, int count);

/* 空列表常量。 */
static inline AstList spt_ast_list_empty(void) {
  AstList l;
  l.items = NULL;
  l.count = 0;
  return l;
}

/* 是否为类型注解节点。 */
static inline bool spt_ast_is_type(NodeType t) {
  return t >= NODE_TYPE_PRIMITIVE && t <= NODE_TYPE_MULTIRETURN;
}

/* 运算符可读名（调试/打印用）。 */
const char *spt_op_name(OperatorKind op);
const char *spt_node_type_name(NodeType t);

#endif /* SPT_AST_H */
