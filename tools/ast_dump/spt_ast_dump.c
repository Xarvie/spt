/*
** spt_ast_dump.c — AST 转储工具实现
**
** 遍历 AST 节点，输出带缩进的树形结构。覆盖所有现有节点类型。
** 新增节点类型时，只需在 dump_node 的 switch 里加一个 case。
**
** 输出格式示例：
**   Block [1:0]
**   ├─ VariableDecl [1:0] name=x is_const=false
**   │  └─ PrimitiveType [1:0] kind=int
**   │  └─ LiteralInt [1:12] value=42
**   └─ FunctionDecl [3:0] name=add is_exported=true
**      ├─ PrimitiveType [3:0] kind=int
**      ├─ ParameterDecl [3:14] name=a
**      │  └─ PrimitiveType [3:11] kind=int
**      └─ Block [3:24]
**         └─ ReturnStatement [4:4]
**            └─ BinaryOp [4:11] op=+
**               ├─ Identifier [4:11] name=a
**               └─ Identifier [4:15] name=b
*/
#include "spt_ast_dump.h"

#include <stdio.h>
#include <string.h>

/* ---- 缩进打印 ---- */
static void print_indent(int depth, bool is_last, const bool *branch) {
  if (depth == 0) return;
  for (int i = 0; i < depth - 1; i++) {
    printf(branch[i] ? "   " : "│  ");
  }
  printf(is_last ? "└─ " : "├─ ");
}

/* ---- 前向声明 ---- */
static void dump_node(const AstNode *n, int depth, bool is_last, const bool *branch);
static void dump_list(AstList list, int depth, const char *label, const bool *branch);

/* ---- 类型名 ---- */
static const char *prim_type_name(PrimitiveTypeKind k) {
  switch (k) {
  case PTK_INT:    return "int";
  case PTK_FLOAT:  return "float";
  case PTK_NUMBER: return "number";
  case PTK_STRING: return "str";
  case PTK_BOOL:   return "bool";
  case PTK_VOID:   return "void";
  case PTK_NULL:   return "null";
  }
  return "?";
}

/* ---- 位置 ---- */
static void print_loc(const AstNode *n) {
  printf(" [%d:%d]", n->loc.line, n->loc.column);
}

/* ---- 核心转储 ---- */
static void dump_node(const AstNode *n, int depth, bool is_last, const bool *branch) {
  if (!n) {
    print_indent(depth, is_last, branch);
    printf("(null)\n");
    return;
  }

  print_indent(depth, is_last, branch);
  printf("%s", spt_node_type_name(n->type));
  print_loc(n);

  /* 更新分支状态 */
  bool new_branch[64];
  if (depth < 64) {
    for (int i = 0; i < depth; i++) new_branch[i] = branch[i];
    new_branch[depth] = is_last;
  }

  switch (n->type) {
  /* ---- 字面量 ---- */
  case NODE_LITERAL_INT:
    printf(" value=%lld", (long long)n->u.lit_int.value);
    break;
  case NODE_LITERAL_FLOAT:
    printf(" value=%g", n->u.lit_float.value);
    break;
  case NODE_LITERAL_STRING:
    printf(" data=\"%.*s\"", n->u.lit_str.len, n->u.lit_str.data);
    break;
  case NODE_LITERAL_BOOL:
    printf(" value=%s", n->u.lit_bool.value ? "true" : "false");
    break;
  case NODE_LITERAL_NULL:
    break;
  case NODE_LITERAL_LIST:
    printf("\n");
    dump_list(n->u.lit_list.elements, depth + 1, NULL, new_branch);
    return;
  case NODE_LITERAL_MAP:
    printf("\n");
    dump_list(n->u.lit_map.entries, depth + 1, NULL, new_branch);
    return;
  case NODE_MAP_ENTRY:
    printf("\n");
    dump_node(n->u.map_entry.key, depth + 1, false, new_branch);
    dump_node(n->u.map_entry.value, depth + 1, true, new_branch);
    return;

  /* ---- 表达式 ---- */
  case NODE_IDENTIFIER:
    printf(" name=%s", n->u.ident.name ? n->u.ident.name : "(null)");
    break;
  case NODE_UNARY_OP:
    printf(" op=%s\n", spt_op_name(n->u.unary.op));
    dump_node(n->u.unary.operand, depth + 1, true, new_branch);
    return;
  case NODE_BINARY_OP:
    printf(" op=%s\n", spt_op_name(n->u.binary.op));
    dump_node(n->u.binary.left, depth + 1, false, new_branch);
    dump_node(n->u.binary.right, depth + 1, true, new_branch);
    return;
  case NODE_FUNCTION_CALL:
    printf("\n");
    dump_node(n->u.call.func, depth + 1, n->u.call.args.count == 0, new_branch);
    dump_list(n->u.call.args, depth + 1, NULL, new_branch);
    return;
  case NODE_MEMBER_ACCESS:
  case NODE_MEMBER_LOOKUP:
    printf(" member=%s\n", n->u.member.member ? n->u.member.member : "(null)");
    dump_node(n->u.member.object, depth + 1, true, new_branch);
    return;
  case NODE_INDEX_ACCESS:
    printf("\n");
    dump_node(n->u.index.array, depth + 1, false, new_branch);
    dump_node(n->u.index.index, depth + 1, true, new_branch);
    return;
  case NODE_LAMBDA:
    printf(" is_variadic=%s\n", n->u.lambda.is_variadic ? "true" : "false");
    dump_list(n->u.lambda.params, depth + 1, "params", new_branch);
    dump_node(n->u.lambda.return_type, depth + 1, false, new_branch);
    dump_node(n->u.lambda.body, depth + 1, true, new_branch);
    return;
  case NODE_THIS_EXPRESSION:
  case NODE_VAR_ARGS:
    break;

  /* ---- 语句 ---- */
  case NODE_BLOCK:
    printf("\n");
    dump_list(n->u.block.statements, depth + 1, NULL, new_branch);
    return;
  case NODE_EXPRESSION_STATEMENT:
    printf("\n");
    dump_node(n->u.expr_stmt.expr, depth + 1, true, new_branch);
    return;
  case NODE_ASSIGNMENT:
    printf("\n");
    dump_list(n->u.assign.lvalues, depth + 1, "lvalues", new_branch);
    dump_list(n->u.assign.rvalues, depth + 1, "rvalues", new_branch);
    return;
  case NODE_UPDATE_ASSIGNMENT:
    printf(" op=%s\n", spt_op_name(n->u.update.op));
    dump_node(n->u.update.lvalue, depth + 1, false, new_branch);
    dump_node(n->u.update.rvalue, depth + 1, true, new_branch);
    return;
  case NODE_IF_CLAUSE:
    printf("\n");
    dump_node(n->u.if_clause.condition, depth + 1, false, new_branch);
    dump_node(n->u.if_clause.body, depth + 1, true, new_branch);
    return;
  case NODE_IF_STATEMENT:
    printf("\n");
    dump_node(n->u.if_stmt.condition, depth + 1, false, new_branch);
    dump_node(n->u.if_stmt.then_block, depth + 1, false, new_branch);
    dump_list(n->u.if_stmt.else_if_clauses, depth + 1, "elif", new_branch);
    dump_node(n->u.if_stmt.else_block, depth + 1, true, new_branch);
    return;
  case NODE_WHILE_STATEMENT:
    printf("\n");
    dump_node(n->u.while_stmt.condition, depth + 1, false, new_branch);
    dump_node(n->u.while_stmt.body, depth + 1, true, new_branch);
    return;
  case NODE_FOR_NUMERIC_STATEMENT:
    printf(" var=%s\n", n->u.for_num.var_name ? n->u.for_num.var_name : "(null)");
    dump_node(n->u.for_num.type_annotation, depth + 1, false, new_branch);
    dump_node(n->u.for_num.start, depth + 1, false, new_branch);
    dump_node(n->u.for_num.end, depth + 1, false, new_branch);
    dump_node(n->u.for_num.step, depth + 1, false, new_branch);
    dump_node(n->u.for_num.body, depth + 1, true, new_branch);
    return;
  case NODE_FOR_EACH_STATEMENT:
    printf("\n");
    dump_list(n->u.for_each.loop_variables, depth + 1, "vars", new_branch);
    dump_list(n->u.for_each.iterable_exprs, depth + 1, "iter", new_branch);
    dump_node(n->u.for_each.body, depth + 1, true, new_branch);
    return;
  case NODE_BREAK_STATEMENT:
  case NODE_CONTINUE_STATEMENT:
    break;
  case NODE_RETURN_STATEMENT:
    printf("\n");
    dump_list(n->u.return_stmt.values, depth + 1, NULL, new_branch);
    return;
  case NODE_IMPORT_NAMESPACE:
    printf(" alias=%s module=%s\n",
           n->u.import_ns.alias ? n->u.import_ns.alias : "(null)",
           n->u.import_ns.module_path ? n->u.import_ns.module_path : "(null)");
    return;
  case NODE_IMPORT_NAMED:
    printf(" module=%s\n",
           n->u.import_named.module_path ? n->u.import_named.module_path : "(null)");
    dump_list(n->u.import_named.specifiers, depth + 1, NULL, new_branch);
    return;
  case NODE_IMPORT_SPECIFIER:
    printf(" imported=%s alias=%s",
           n->u.import_spec.imported_name ? n->u.import_spec.imported_name : "(null)",
           n->u.import_spec.alias ? n->u.import_spec.alias : "(none)");
    break;
  case NODE_DEFER_STATEMENT:
    printf("\n");
    dump_node(n->u.defer_stmt.body, depth + 1, true, new_branch);
    return;

  /* ---- 声明 ---- */
  case NODE_VARIABLE_DECL:
    printf(" name=%s const=%d global=%d static=%d exported=%d\n",
           n->u.var_decl.name ? n->u.var_decl.name : "(null)",
           n->u.var_decl.is_const, n->u.var_decl.is_global,
           n->u.var_decl.is_static, n->u.var_decl.is_exported);
    dump_node(n->u.var_decl.type_annotation, depth + 1, n->u.var_decl.initializer == NULL, new_branch);
    dump_node(n->u.var_decl.initializer, depth + 1, true, new_branch);
    return;
  case NODE_MUTI_VARIABLE_DECL:
    printf(" count=%d exported=%d\n", n->u.muti_var.count, n->u.muti_var.is_exported);
    for (int i = 0; i < n->u.muti_var.count; i++) {
      print_indent(depth + 1, false, new_branch);
      printf("var[%d] name=%s global=%d const=%d\n", i,
             n->u.muti_var.vars[i].name ? n->u.muti_var.vars[i].name : "(null)",
             n->u.muti_var.vars[i].is_global, n->u.muti_var.vars[i].is_const);
    }
    dump_node(n->u.muti_var.initializer, depth + 1, true, new_branch);
    return;
  case NODE_PARAMETER_DECL:
    printf(" name=%s\n", n->u.param.name ? n->u.param.name : "(null)");
    dump_node(n->u.param.type_annotation, depth + 1, true, new_branch);
    return;
  case NODE_FUNCTION_DECL:
    printf(" name=%s global=%d static=%d variadic=%d exported=%d const=%d\n",
           n->u.func_decl.name ? n->u.func_decl.name : "(null)",
           n->u.func_decl.is_global, n->u.func_decl.is_static,
           n->u.func_decl.is_variadic, n->u.func_decl.is_exported,
           n->u.func_decl.is_const);
    dump_list(n->u.func_decl.params, depth + 1, "params", new_branch);
    dump_node(n->u.func_decl.return_type, depth + 1, false, new_branch);
    dump_node(n->u.func_decl.body, depth + 1, true, new_branch);
    return;
  case NODE_CLASS_MEMBER:
    printf(" static=%d\n", n->u.class_member.is_static);
    dump_node(n->u.class_member.member_declaration, depth + 1, true, new_branch);
    return;
  case NODE_CLASS_DECL:
    printf(" name=%s exported=%d\n",
           n->u.class_decl.name ? n->u.class_decl.name : "(null)",
           n->u.class_decl.is_exported);
    dump_list(n->u.class_decl.members, depth + 1, NULL, new_branch);
    return;

  /* ---- 类型节点 ---- */
  case NODE_TYPE_PRIMITIVE:
    printf(" kind=%s", prim_type_name(n->u.type_prim.kind));
    break;
  case NODE_TYPE_ANY:
  case NODE_TYPE_AUTO:
  case NODE_TYPE_FUNCTION_KW:
  case NODE_TYPE_COROUTINE_KW:
  case NODE_TYPE_MULTIRETURN:
    break;
  case NODE_TYPE_LIST:
    printf("\n");
    dump_node(n->u.type_list.element, depth + 1, true, new_branch);
    return;
  case NODE_TYPE_MAP:
    printf("\n");
    dump_node(n->u.type_map.key, depth + 1, false, new_branch);
    dump_node(n->u.type_map.value, depth + 1, true, new_branch);
    return;
  case NODE_TYPE_USER:
    printf(" parts=");
    for (int i = 0; i < n->u.type_user.count; i++) {
      if (i > 0) printf(".");
      printf("%s", n->u.type_user.parts[i] ? n->u.type_user.parts[i] : "(null)");
    }
    break;
  }

  printf("\n");
}

/* ---- 列表转储 ---- */
static void dump_list(AstList list, int depth, const char *label, const bool *branch) {
  if (label) {
    print_indent(depth, list.count == 0, branch);
    printf("%s: %d items\n", label, list.count);
    if (list.count == 0) return;
    depth++;
  }

  for (int i = 0; i < list.count; i++) {
    bool is_last = (i == list.count - 1);
    dump_node(list.items[i], depth, is_last, branch);
  }
}

/* ---- 公开入口 ---- */
void spt_ast_dump(const AstNode *root) {
  if (!root) {
    printf("(null AST)\n");
    return;
  }
  bool branch[64] = {0};
  dump_node(root, 0, true, branch);
}
