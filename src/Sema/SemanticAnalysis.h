#pragma once

#include "../Ast/ast.h"
#include <functional>
#include <string>
#include <vector>

namespace spt {

// 语义错误
struct SemanticError {
  std::string message;
  std::string filename;
  int line;
  int column;
};

// 语义分析器 - 轻量级,主要用于 LSP 提示,编译器可直接忽略类型
class SemanticAnalyzer {
public:
  using ErrorHandler = std::function<void(const SemanticError &)>;

  // 分析 AST (类型标注仅供 LSP 使用,编译器抹除)
  bool analyze(AstNode *ast);

  void setErrorHandler(ErrorHandler handler) { errorHandler_ = std::move(handler); }

  bool hasError() const { return hasError_; }

  const std::vector<SemanticError> &errors() const { return errors_; }

private:
  void analyzeStatement(Statement *stmt);
  void analyzeExpression(Expression *expr);
  void analyzeBlock(BlockNode *block);

  void error(const std::string &msg, const SourceLocation &loc);

private:
  ErrorHandler errorHandler_;
  std::vector<SemanticError> errors_;
  bool hasError_ = false;
  int scopeDepth_ = 0;
};

} // namespace spt
