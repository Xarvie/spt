#include "SemanticAnalysis.h"

namespace spt {

bool SemanticAnalyzer::analyze(AstNode *ast) {
  hasError_ = false;
  errors_.clear();

  if (auto *block = dynamic_cast<BlockNode *>(ast)) {
    analyzeBlock(block);
  }

  return !hasError_;
}

void SemanticAnalyzer::analyzeStatement(Statement *stmt) {
  if (!stmt)
    return;

  switch (stmt->nodeType) {
  case NodeType::BLOCK:
    analyzeBlock(static_cast<BlockNode *>(stmt));
    break;

  case NodeType::VARIABLE_DECL: {
    auto *decl = static_cast<VariableDeclNode *>(stmt);
    if (decl->initializer) {
      analyzeExpression(decl->initializer);
    }

    break;
  }

  case NodeType::FUNCTION_DECL: {
    auto *func = static_cast<FunctionDeclNode *>(stmt);
    scopeDepth_++;
    if (func->body) {
      analyzeBlock(func->body);
    }
    scopeDepth_--;
    break;
  }

  case NodeType::CLASS_DECL: {
    auto *cls = static_cast<ClassDeclNode *>(stmt);
    for (auto *member : cls->members) {
      if (auto *decl = member->memberDeclaration) {
        analyzeStatement(decl);
      }
    }
    break;
  }

  case NodeType::IF_STATEMENT: {
    auto *ifStmt = static_cast<IfStatementNode *>(stmt);
    analyzeExpression(ifStmt->condition);
    if (ifStmt->thenBlock)
      analyzeBlock(ifStmt->thenBlock);
    for (auto *elseIf : ifStmt->elseIfClauses) {
      analyzeExpression(elseIf->condition);
      if (elseIf->body)
        analyzeBlock(elseIf->body);
    }
    if (ifStmt->elseBlock)
      analyzeBlock(ifStmt->elseBlock);
    break;
  }

  case NodeType::WHILE_STATEMENT: {
    auto *whileStmt = static_cast<WhileStatementNode *>(stmt);
    analyzeExpression(whileStmt->condition);
    if (whileStmt->body)
      analyzeBlock(whileStmt->body);
    break;
  }

  case NodeType::FOR_CSTYLE_STATEMENT: {
    auto *forStmt = static_cast<ForCStyleStatementNode *>(stmt);
    if (forStmt->condition)
      analyzeExpression(forStmt->condition);
    if (forStmt->body)
      analyzeBlock(forStmt->body);
    break;
  }

  case NodeType::FOR_EACH_STATEMENT: {
    auto *forEach = static_cast<ForEachStatementNode *>(stmt);
    for (auto *expr : forEach->iterableExprs) {
      analyzeExpression(expr);
    }
    if (forEach->body)
      analyzeBlock(forEach->body);
    break;
  }

  case NodeType::RETURN_STATEMENT: {
    auto *ret = static_cast<ReturnStatementNode *>(stmt);
    for (auto *expr : ret->returnValue) {
      analyzeExpression(expr);
    }
    break;
  }

  case NodeType::ASSIGNMENT: {
    auto *assign = static_cast<AssignmentNode *>(stmt);
    for (auto *lv : assign->lvalues)
      analyzeExpression(lv);
    for (auto *rv : assign->rvalues)
      analyzeExpression(rv);
    break;
  }

  case NodeType::EXPRESSION_STATEMENT: {
    auto *exprStmt = static_cast<ExpressionStatementNode *>(stmt);
    analyzeExpression(exprStmt->expression);
    break;
  }

  default:
    break;
  }
}

void SemanticAnalyzer::analyzeExpression(Expression *expr) {
  if (!expr)
    return;

  switch (expr->nodeType) {
  case NodeType::BINARY_OP: {
    auto *binOp = static_cast<BinaryOpNode *>(expr);
    analyzeExpression(binOp->left);
    analyzeExpression(binOp->right);
    break;
  }

  case NodeType::UNARY_OP: {
    auto *unOp = static_cast<UnaryOpNode *>(expr);
    analyzeExpression(unOp->operand);
    break;
  }

  case NodeType::FUNCTION_CALL: {
    auto *call = static_cast<FunctionCallNode *>(expr);
    analyzeExpression(call->functionExpr);
    for (auto *arg : call->arguments) {
      analyzeExpression(arg);
    }
    break;
  }

  case NodeType::MEMBER_ACCESS: {
    auto *member = static_cast<MemberAccessNode *>(expr);
    analyzeExpression(member->objectExpr);
    break;
  }

  case NodeType::INDEX_ACCESS: {
    auto *index = static_cast<IndexAccessNode *>(expr);
    analyzeExpression(index->arrayExpr);
    analyzeExpression(index->indexExpr);
    break;
  }

  case NodeType::LAMBDA: {
    auto *lambda = static_cast<LambdaNode *>(expr);
    scopeDepth_++;
    if (lambda->body)
      analyzeBlock(lambda->body);
    scopeDepth_--;
    break;
  }

  case NodeType::LITERAL_LIST: {
    auto *list = static_cast<LiteralListNode *>(expr);
    for (auto *elem : list->elements) {
      analyzeExpression(elem);
    }
    break;
  }

  case NodeType::LITERAL_MAP: {
    auto *map = static_cast<LiteralMapNode *>(expr);
    for (auto *entry : map->entries) {
      analyzeExpression(entry->key);
      analyzeExpression(entry->value);
    }
    break;
  }

  default:
    break;
  }
}

void SemanticAnalyzer::analyzeBlock(BlockNode *block) {
  scopeDepth_++;
  for (auto *stmt : block->statements) {
    analyzeStatement(stmt);
  }
  scopeDepth_--;
}

void SemanticAnalyzer::error(const std::string &msg, const SourceLocation &loc) {
  hasError_ = true;
  SemanticError err{msg, loc.filename, loc.line, loc.column};
  errors_.push_back(err);

  if (errorHandler_) {
    errorHandler_(err);
  }
}

} // namespace spt
