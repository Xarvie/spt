#ifndef SPT_AST_UTIL_HPP
#define SPT_AST_UTIL_HPP

#include "ast.h"
#include <iostream>
#include <optional>
#include <string>
#include <variant>
#include <vector>

inline void printIndent(std::ostream &out, int indentLevel) {
  for (int i = 0; i < indentLevel; ++i) {
    out << "  ";
  }
}

inline std::string getOperatorName(OperatorKind op) {
  switch (op) {

  case OperatorKind::NEGATE:
    return "- (数值负)";
  case OperatorKind::NOT:
    return "!";
  case OperatorKind::LENGTH:
    return "#";
  case OperatorKind::BW_NOT:
    return "~";

  case OperatorKind::ADD:
    return "+";
  case OperatorKind::SUB:
    return "-";
  case OperatorKind::MUL:
    return "*";
  case OperatorKind::DIV:
    return "/";
  case OperatorKind::MOD:
    return "%";
  case OperatorKind::CONCAT:
    return "..";
  case OperatorKind::LT:
    return "<";
  case OperatorKind::LE:
    return "<=";
  case OperatorKind::GT:
    return ">";
  case OperatorKind::GE:
    return ">=";
  case OperatorKind::EQ:
    return "==";
  case OperatorKind::NE:
    return "!=";
  case OperatorKind::AND:
    return "&&";
  case OperatorKind::OR:
    return "||";
  case OperatorKind::BW_AND:
    return "&";
  case OperatorKind::BW_OR:
    return "|";
  case OperatorKind::BW_XOR:
    return "^";
  case OperatorKind::BW_LSHIFT:
    return "<<";
  case OperatorKind::BW_RSHIFT:
    return ">>";

  case OperatorKind::ASSIGN_ADD:
    return "+=";
  case OperatorKind::ASSIGN_SUB:
    return "-=";
  case OperatorKind::ASSIGN_MUL:
    return "*=";
  case OperatorKind::ASSIGN_DIV:
    return "/=";
  case OperatorKind::ASSIGN_IDIV:
    return "~/=";
  case OperatorKind::ASSIGN_MOD:
    return "%=";
  case OperatorKind::ASSIGN_CONCAT:
    return "..=";
  case OperatorKind::ASSIGN_BW_AND:
    return "&=";
  case OperatorKind::ASSIGN_BW_OR:
    return "|=";
  case OperatorKind::ASSIGN_BW_XOR:
    return "^=";
  case OperatorKind::ASSIGN_BW_LSHIFT:
    return "<<=";
  case OperatorKind::ASSIGN_BW_RSHIFT:
    return ">>=";
  default:
    return "[未知操作符]";
  }
}

void printAst(std::ostream &out, const AstNode *node, int indentLevel = 0);

inline void printAstType(std::ostream &out, const AstType *type) {
  if (!type) {
    out << "[空类型指针]";
    return;
  }

  if (const auto *p = dynamic_cast<const PrimitiveType *>(type)) {
    switch (p->primitiveKind) {
    case PrimitiveTypeKind::INT:
      out << "int";
      break;
    case PrimitiveTypeKind::FLOAT:
      out << "float";
      break;
    case PrimitiveTypeKind::NUMBER:
      out << "number";
      break;
    case PrimitiveTypeKind::STRING:
      out << "string";
      break;
    case PrimitiveTypeKind::BOOL:
      out << "bool";
      break;
    case PrimitiveTypeKind::VOID:
      out << "void";
      break;
    case PrimitiveTypeKind::NULL_TYPE:
      out << "null";
      break;
    default:
      out << "[未知基础类型]";
      break;
    }
  } else if (dynamic_cast<const AnyType *>(type)) {
    out << "any";
  } else if (dynamic_cast<const AutoType *>(type)) {
    out << "auto";
  } else if (const auto *l = dynamic_cast<const ListType *>(type)) {
    out << "list<";
    printAstType(out, l->elementType);
    out << ">";
  } else if (const auto *m = dynamic_cast<const MapType *>(type)) {
    out << "map<";
    printAstType(out, m->keyType);
    out << ", ";
    printAstType(out, m->valueType);
    out << ">";
  } else if (const auto *u = dynamic_cast<const UnionType *>(type)) {
    out << "union<";
    for (size_t i = 0; i < u->memberTypes.size(); ++i) {
      printAstType(out, u->memberTypes[i]);
      if (i < u->memberTypes.size() - 1)
        out << ", ";
    }
    out << ">";
  } else if (const auto *t = dynamic_cast<const TupleType *>(type)) {
    out << "tuple<";
    for (size_t i = 0; i < t->elementTypes.size(); ++i) {
      printAstType(out, t->elementTypes[i]);
      if (i < t->elementTypes.size() - 1)
        out << ", ";
    }
    out << ">";
  } else if (const auto *user = dynamic_cast<const UserType *>(type)) {
    out << user->getFullName();
  } else if (dynamic_cast<const FunctionKeywordType *>(type)) {
    out << "function";
  } else if (dynamic_cast<const CoroutineKeywordType *>(type)) {
    out << "coroutine";
  } else {
    out << "[未知 AstType 子类]";
  }
}

inline void printAst(std::ostream &out, const AstNode *node, int indentLevel) {
  if (!node) {
    printIndent(out, indentLevel);
    out << "[空节点指针]\n";
    return;
  }

  printIndent(out, indentLevel);

  if (const auto *n = dynamic_cast<const LiteralIntNode *>(node)) {
    out << "整数常量: " << n->value << "\n";
  } else if (const auto *n = dynamic_cast<const LiteralFloatNode *>(node)) {
    out << "浮点常量: " << n->value << "\n";
  } else if (const auto *n = dynamic_cast<const LiteralStringNode *>(node)) {

    out << "字符串常量: \"" << n->value << "\"\n";
  } else if (const auto *n = dynamic_cast<const LiteralBoolNode *>(node)) {
    out << "布尔常量: " << (n->value ? "true" : "false") << "\n";
  } else if (dynamic_cast<const LiteralNullNode *>(node)) {
    out << "空常量: null\n";
  } else if (const auto *n = dynamic_cast<const LiteralListNode *>(node)) {
    out << "列表常量 [\n";
    for (const auto *elem : n->elements) {
      printAst(out, elem, indentLevel + 1);
    }
    printIndent(out, indentLevel);
    out << "]\n";
  } else if (const auto *n = dynamic_cast<const LiteralMapNode *>(node)) {
    out << "Map常量 {\n";
    for (const auto *entry : n->entries) {
      printAst(out, entry, indentLevel + 1);
    }
    printIndent(out, indentLevel);
    out << "}\n";
  } else if (const auto *n = dynamic_cast<const MapEntryNode *>(node)) {
    out << "Map条目:\n";
    printIndent(out, indentLevel + 1);
    out << "键:\n";
    printAst(out, n->key, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "值:\n";
    printAst(out, n->value, indentLevel + 2);
  }

  else if (const auto *n = dynamic_cast<const IdentifierNode *>(node)) {
    out << "标识符: " << n->name << "\n";
  } else if (const auto *n = dynamic_cast<const UnaryOpNode *>(node)) {
    out << "一元操作: " << getOperatorName(n->op) << "\n";
    printAst(out, n->operand, indentLevel + 1);
  } else if (const auto *n = dynamic_cast<const BinaryOpNode *>(node)) {
    out << "二元操作: " << getOperatorName(n->op) << "\n";
    printIndent(out, indentLevel + 1);
    out << "左:\n";
    printAst(out, n->left, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "右:\n";
    printAst(out, n->right, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const FunctionCallNode *>(node)) {
    out << "函数调用:\n";
    printIndent(out, indentLevel + 1);
    out << "函数:\n";
    printAst(out, n->functionExpr, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "参数:\n";
    if (n->arguments.empty()) {
      printIndent(out, indentLevel + 2);
      out << "(无)\n";
    } else {
      for (const auto *arg : n->arguments) {
        printAst(out, arg, indentLevel + 2);
      }
    }
  } else if (const auto *n = dynamic_cast<const MemberAccessNode *>(node)) {
    out << "成员访问 (.):\n";
    printIndent(out, indentLevel + 1);
    out << "对象:\n";
    printAst(out, n->objectExpr, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "成员: " << n->memberName << "\n";
  } else if (const auto *n = dynamic_cast<const MemberLookupNode *>(node)) {
    out << "成员查找 (:):\n";
    printIndent(out, indentLevel + 1);
    out << "对象:\n";
    printAst(out, n->objectExpr, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "成员: " << n->memberName << "\n";
  } else if (const auto *n = dynamic_cast<const IndexAccessNode *>(node)) {
    out << "索引访问 []:\n";
    printIndent(out, indentLevel + 1);
    out << "数组/Map:\n";
    printAst(out, n->arrayExpr, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "索引:\n";
    printAst(out, n->indexExpr, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const LambdaNode *>(node)) {
    out << "Lambda 表达式 " << (n->isVariadic ? "(可变参数)" : "") << "-> ";
    printAstType(out, n->returnType);
    out << "\n";
    printIndent(out, indentLevel + 1);
    out << "参数:\n";
    if (n->params.empty()) {
      printIndent(out, indentLevel + 2);
      out << "(无)\n";
    } else {
      for (const auto *param : n->params) {
        printAst(out, param, indentLevel + 2);
      }
    }
    printIndent(out, indentLevel + 1);
    out << "函数体:\n";
    printAst(out, n->body, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const NewExpressionNode *>(node)) {
    out << "New 表达式: ";
    printAstType(out, n->classType);
    out << "\n";
    printIndent(out, indentLevel + 1);
    out << "参数:\n";
    if (n->arguments.empty()) {
      printIndent(out, indentLevel + 2);
      out << "(无)\n";
    } else {
      for (const auto *arg : n->arguments) {
        printAst(out, arg, indentLevel + 2);
      }
    }
  } else if (dynamic_cast<const ThisExpressionNode *>(node)) {
    out << "This 表达式\n";
  } else if (dynamic_cast<const VarArgsNode *>(node)) {
    out << "可变参数 (...) 表达式\n";
  }

  else if (const auto *n = dynamic_cast<const BlockNode *>(node)) {
    out << "代码块 {\n";
    for (const auto *stmt : n->statements) {
      printAst(out, stmt, indentLevel + 1);
    }
    printIndent(out, indentLevel);
    out << "}\n";
  } else if (const auto *n = dynamic_cast<const ExpressionStatementNode *>(node)) {
    out << "表达式语句:\n";
    printAst(out, n->expression, indentLevel + 1);
  } else if (const auto *n = dynamic_cast<const AssignmentNode *>(node)) {

  } else if (const auto *n = dynamic_cast<const UpdateAssignmentNode *>(node)) {
    out << "更新赋值语句 (" << getOperatorName(n->op) << "):\n";
    printIndent(out, indentLevel + 1);
    out << "左值:\n";
    printAst(out, n->lvalue, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "右值:\n";
    printAst(out, n->rvalue, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const IfStatementNode *>(node)) {
    out << "If 语句:\n";
    printIndent(out, indentLevel + 1);
    out << "条件:\n";
    printAst(out, n->condition, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "Then 块:\n";
    printAst(out, n->thenBlock, indentLevel + 2);
    if (!n->elseIfClauses.empty()) {
      printIndent(out, indentLevel + 1);
      out << "Else If 子句:\n";
      for (const auto *clause : n->elseIfClauses) {
        printAst(out, clause, indentLevel + 2);
      }
    }
    if (n->elseBlock) {
      printIndent(out, indentLevel + 1);
      out << "Else 块:\n";
      printAst(out, n->elseBlock, indentLevel + 2);
    }
  } else if (const auto *n = dynamic_cast<const IfClauseNode *>(node)) {
    out << "Else If 子句:\n";
    printIndent(out, indentLevel + 1);
    out << "条件:\n";
    printAst(out, n->condition, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "块:\n";
    printAst(out, n->body, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const WhileStatementNode *>(node)) {
    out << "While 语句:\n";
    printIndent(out, indentLevel + 1);
    out << "条件:\n";
    printAst(out, n->condition, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "循环体:\n";
    printAst(out, n->body, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const ForCStyleStatementNode *>(node)) {
    out << "For (C 风格) 语句:\n";
    printIndent(out, indentLevel + 1);
    out << "初始化:\n";
    if (n->initializer.has_value()) {

      std::visit(
          [&](auto &&arg) {
            using T = std::decay_t<decltype(arg)>;
            if constexpr (std::is_same_v<T, std::vector<Declaration *>>) {
              out << "(声明列表):\n";
              if (arg.empty()) {
                printIndent(out, indentLevel + 2);
                out << "(空)\n";
              } else {
                for (const auto *item : arg) {
                  printAst(out, item, indentLevel + 2);
                }
              }
            } else if constexpr (std::is_same_v<T, std::vector<AssignmentNode *>>) {
              out << "(赋值语句列表):\n";
              if (arg.empty()) {
                printIndent(out, indentLevel + 2);
                out << "(空)\n";
              } else {
                for (const auto *item : arg) {
                  printAst(out, item, indentLevel + 2);
                }
              }
            } else if constexpr (std::is_same_v<T, std::vector<Expression *>>) {
              out << "(表达式列表):\n";
              if (arg.empty()) {
                printIndent(out, indentLevel + 2);
                out << "(空)\n";
              } else {
                for (const auto *item : arg) {
                  printAst(out, item, indentLevel + 2);
                }
              }
            }
          },
          n->initializer.value());
    } else {
      printIndent(out, indentLevel + 2);
      out << "(空)\n";
    }
    printIndent(out, indentLevel + 1);
    out << "条件:\n";
    printAst(out, n->condition, indentLevel + 2);
    printIndent(out, indentLevel + 1);
    out << "更新:\n";
    if (!n->updateActions.empty()) {

      for (const Statement *actionStmt : n->updateActions) {
        printAst(out, actionStmt, indentLevel + 2);
      }
    }
    printIndent(out, indentLevel + 1);
    out << "循环体:\n";
    printAst(out, n->body, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const ForEachStatementNode *>(node)) {

  } else if (dynamic_cast<const BreakStatementNode *>(node)) {
    out << "Break 语句\n";
  } else if (dynamic_cast<const ContinueStatementNode *>(node)) {
    out << "Continue 语句\n";
  } else if (const auto *n = dynamic_cast<const ReturnStatementNode *>(node)) {
    out << "Return 语句";

    if (n->returnValue.empty())
      out << " (无返回值)\n";
    for (Expression *expr : n->returnValue) {
      out << ":\n";
      printAst(out, expr, indentLevel + 1);
    }

  }

  else if (const auto *n = dynamic_cast<const VariableDeclNode *>(node)) {
    out << "变量声明: " << n->name;
    out << " (类型: ";
    printAstType(out, n->typeAnnotation);
    out << ")";
    if (n->isConst)
      out << " [const]";
    if (n->isGlobal)
      out << " [global]";
    if (n->isStatic)
      out << " [static]";
    out << "\n";
    if (n->initializer) {
      printIndent(out, indentLevel + 1);
      out << "初始化:\n";
      printAst(out, n->initializer, indentLevel + 2);
    }
  } else if (const auto *n = dynamic_cast<const ParameterDeclNode *>(node)) {
    out << "参数声明: " << n->name;
    out << " (类型: ";
    printAstType(out, n->typeAnnotation);
    out << ")\n";
  } else if (const auto *n = dynamic_cast<const FunctionDeclNode *>(node)) {
    out << (n->isStatic ? "静态" : "")
        << (dynamic_cast<const ClassMemberNode *>(node->location.filename == "TEMP_MARKER" ? nullptr
                                                                                           : node)
                ? "方法"
                : "函数")
        << "声明: " << n->name;
    out << (n->isVariadic ? " (可变参数)" : "") << " -> ";
    printAstType(out, n->returnType);
    out << "\n";
    printIndent(out, indentLevel + 1);
    out << "参数:\n";
    if (n->params.empty()) {
      printIndent(out, indentLevel + 2);
      out << "(无)\n";
    } else {
      for (const auto *param : n->params) {
        printAst(out, param, indentLevel + 2);
      }
    }
    printIndent(out, indentLevel + 1);
    out << "函数体:\n";
    printAst(out, n->body, indentLevel + 2);
  } else if (const auto *n = dynamic_cast<const ClassDeclNode *>(node)) {
    out << "类声明: " << n->name << " {\n";
    for (const auto *member : n->members) {
      printAst(out, member, indentLevel + 1);
    }
    printIndent(out, indentLevel);
    out << "}\n";
  } else if (const auto *n = dynamic_cast<const ClassMemberNode *>(node)) {
    out << "类成员 " << (n->isStatic ? "[static]" : "") << ":\n";

    printAst(out, n->memberDeclaration, indentLevel + 1);
  }

  else {
    out << "[未知 AST 节点类型]\n";
  }
}

#endif