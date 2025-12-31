#pragma once

#include "../Ast/ast.h"
#include "CodeGen.h"
#include <functional>
#include <memory>
#include <vector>

namespace spt {

class Compiler {
public:
  using ErrorHandler = std::function<void(const CompileError &)>;

  explicit Compiler(const std::string &moduleName = "main", const std::string &source = "<none>");
  ~Compiler();

  // 编译入口
  CompiledChunk compile(AstNode *ast);
  CompiledChunk compileModule(BlockNode *block);

  // 错误处理配置
  void setErrorHandler(ErrorHandler handler) { errorHandler_ = std::move(handler); }

  bool hasError() const { return hasError_; }

  const std::vector<CompileError> &errors() const { return errors_; }

private:
  // === 语句编译 ===
  void compileStatement(Statement *stmt);
  void compileBlock(BlockNode *block);
  void compileVariableDecl(VariableDeclNode *decl);
  void compileMutiVariableDecl(MutiVariableDeclarationNode *decl);

  void compileClassDecl(ClassDeclNode *decl);
  int compileCondition(Expression *expr);
  void compileIfStatement(IfStatementNode *stmt);
  void compileWhileStatement(WhileStatementNode *stmt);
  void compileForCStyle(ForCStyleStatementNode *stmt);
  void compileForEach(ForEachStatementNode *stmt);
  void compileReturn(ReturnStatementNode *stmt);
  void compileBreak(BreakStatementNode *stmt);
  void compileContinue(ContinueStatementNode *stmt);
  void compileAssignment(AssignmentNode *stmt);
  void compileUpdateAssignment(UpdateAssignmentNode *stmt);
  void compileExpressionStatement(ExpressionStatementNode *stmt);

  // === 表达式编译 ===
  void compileExpression(Expression *expr, int dest);
  void compileExpressionForValue(Expression *expr); // 结果存入分配的新栈顶

  void compileLiteral(Expression *expr, int dest);
  void compileIdentifier(IdentifierNode *node, int dest);
  void compileBinaryOp(BinaryOpNode *node, int dest);
  void compileUnaryOp(UnaryOpNode *node, int dest);
  void compileFunctionCall(FunctionCallNode *node, int dest, int nResults = 1);
  void compileMemberAccess(MemberAccessNode *node, int dest);
  void compileIndexAccess(IndexAccessNode *node, int dest);
  void compileLambda(LambdaNode *node, int dest);
  void compileNewExpression(NewExpressionNode *node, int dest);
  void compileThis(ThisExpressionNode *node, int dest);
  void compileListLiteral(LiteralListNode *node, int dest);
  void compileMapLiteral(LiteralMapNode *node, int dest);
  void compileMemberLookup(MemberLookupNode *node, int dest);

  // === 函数/闭包编译辅助 ===
  void compileLambdaBody(LambdaNode *lambda, int dest);
  void compileFunctionDecl(FunctionDeclNode *decl);

  // === 方法调用编译 (OP_INVOKE 支持) ===
  void compileMethodInvoke(Expression *receiverExpr, const std::string &methodName,
                           const std::vector<Expression *> &arguments, int dest, int nResults = 1);
  void compileMethodInvokeFallback(Expression *receiverExpr, const std::string &methodName,
                                   int methodIdx, const std::vector<Expression *> &arguments,
                                   int dest, int nResults = 1);

  int emitLoadEnvironment();

  void emitStoreToEnv(const std::string &name, int srcSlot);
  // === 赋值处理 ===
  LValue compileLValue(Expression *expr);
  void emitStore(const LValue &lv, int srcReg);

  // === 错误与工具 ===
  void error(const std::string &msg, const SourceLocation &loc);
  void error(const std::string &msg);

  OpCode binaryOpToOpcode(OperatorKind op);
  bool isComparisonOp(OperatorKind op);

  // === 模块 ===
  void compileImportNamespace(ImportNamespaceNode *node);
  void compileImportNamed(ImportNamedNode *node);
  void compileExport(VariableDeclNode *decl);

private:
  std::string moduleName_;
  std::string source_;
  std::unique_ptr<CodeGen> cg_; // 核心委托对象

  std::vector<std::string> exports_; // 模块导出符号

  ErrorHandler errorHandler_;
  std::vector<CompileError> errors_;
  bool hasError_ = false;
};

} // namespace spt