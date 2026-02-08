#ifndef SPT_AST_BUILDER_VISITOR_CPP_RAWPTR_H
#define SPT_AST_BUILDER_VISITOR_CPP_RAWPTR_H

#include "LangParserBaseVisitor.h"
#include "antlr4-runtime.h"
#include "ast.h"
#include <any>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

class AstBuilderVisitor : public LangParserBaseVisitor {
private:
  std::string current_filename;
  int scope_depth = 0;

  SourceLocation getSourceLocation(antlr4::ParserRuleContext *ctx);
  SourceLocation getSourceLocation(antlr4::tree::TerminalNode *tn);
  std::string processStringLiteral(const std::string &text);

  template <typename T> T *safeAnyCastRawPtr(const std::any &anyValue, const char *ruleName) {
    if (!anyValue.has_value()) {
      return nullptr;
    }
    try {
      return std::any_cast<T *>(anyValue);
    } catch (const std::bad_any_cast &e) {
      std::string errorMsg = "类型转换错误: 在处理 '";
      errorMsg += ruleName;
      errorMsg += "' 时无法将 std::any 转换为 ";
      errorMsg += typeid(T *).name();
      throw std::runtime_error(errorMsg);
    }
  }

  std::vector<Declaration *> safeAnyCastDeclVector(std::any &anyValue, const char *ruleName);

  std::vector<Expression *> safeAnyCastExprVector(std::any &anyValue, const char *ruleName);

  std::any visitBinaryExpressionRawPtr(
      antlr4::ParserRuleContext *ctx, std::function<size_t()> getChildCount,
      std::function<antlr4::ParserRuleContext *(size_t)> getChild,
      std::function<antlr4::tree::TerminalNode *(size_t, bool &isRShift)> getOperator);

public:
  AstBuilderVisitor(const std::string &filename);

  std::any visitCompilationUnit(LangParser::CompilationUnitContext *ctx) override;
  std::any visitBlockStatement(LangParser::BlockStatementContext *ctx) override;

  std::any visitSemicolonStmt(LangParser::SemicolonStmtContext *ctx) override;
  std::any visitAssignStmt(LangParser::AssignStmtContext *ctx) override;
  std::any visitUpdateStmt(LangParser::UpdateStmtContext *ctx) override;
  std::any visitExpressionStmt(LangParser::ExpressionStmtContext *ctx) override;
  std::any visitDeclarationStmt(LangParser::DeclarationStmtContext *ctx) override;
  std::any visitIfStmt(LangParser::IfStmtContext *ctx) override;
  std::any visitWhileStmt(LangParser::WhileStmtContext *ctx) override;
  std::any visitForStmt(LangParser::ForStmtContext *ctx) override;
  std::any visitBreakStmt(LangParser::BreakStmtContext *ctx) override;
  std::any visitContinueStmt(LangParser::ContinueStmtContext *ctx) override;
  std::any visitReturnStmt(LangParser::ReturnStmtContext *ctx) override;
  std::any visitBlockStmt(LangParser::BlockStmtContext *ctx) override;
  // --- 新增 Defer 支持 ---
  std::any visitDeferStmt(LangParser::DeferStmtContext *ctx) override;
  std::any visitDeferBlockStmt(LangParser::DeferBlockStmtContext *ctx) override;

  std::any visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *ctx) override;
  std::any visitNormalAssignStmt(LangParser::NormalAssignStmtContext *ctx) override;
  std::any visitLvalueBase(LangParser::LvalueBaseContext *ctx) override;

  std::any visitDeclaration(LangParser::DeclarationContext *ctx) override;
  std::any visitVariableDeclarationDef(LangParser::VariableDeclarationDefContext *ctx) override;
  std::any visitFunctionDeclarationDef(LangParser::FunctionDeclarationDefContext *ctx) override;
  std::any visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *ctx) override;
  std::any visitClassFieldMember(LangParser::ClassFieldMemberContext *ctx) override;
  std::any visitClassMethodMember(LangParser::ClassMethodMemberContext *ctx) override;
  std::any visitClassEmptyMember(LangParser::ClassEmptyMemberContext *ctx) override;
  std::any visitParameter(LangParser::ParameterContext *ctx) override;
  std::any visitParameterList(LangParser::ParameterListContext *ctx) override;

  std::any visitTypePrimitive(LangParser::TypePrimitiveContext *ctx) override;
  std::any visitTypeListType(LangParser::TypeListTypeContext *ctx) override;
  std::any visitTypeMap(LangParser::TypeMapContext *ctx) override;

  std::any visitTypeAny(LangParser::TypeAnyContext *ctx) override;
  std::any visitPrimitiveType(LangParser::PrimitiveTypeContext *ctx) override;
  std::any visitListType(LangParser::ListTypeContext *ctx) override;
  std::any visitMapType(LangParser::MapTypeContext *ctx) override;

  std::any visitExpression(LangParser::ExpressionContext *ctx) override;
  std::any visitLogicalOrExpression(LangParser::LogicalOrExpressionContext *ctx) override;
  std::any visitLogicalAndExpression(LangParser::LogicalAndExpressionContext *ctx) override;
  std::any visitBitwiseOrExpression(LangParser::BitwiseOrExpressionContext *ctx) override;
  std::any visitBitwiseXorExpression(LangParser::BitwiseXorExpressionContext *ctx) override;
  std::any visitBitwiseAndExpression(LangParser::BitwiseAndExpressionContext *ctx) override;
  std::any visitEqualityExpression(LangParser::EqualityExpressionContext *ctx) override;
  std::any visitComparisonExpression(LangParser::ComparisonExpressionContext *ctx) override;
  std::any visitShiftExpression(LangParser::ShiftExpressionContext *ctx) override;
  std::any visitConcatExpression(LangParser::ConcatExpressionContext *ctx) override;
  std::any visitAddSubExpression(LangParser::AddSubExpressionContext *ctx) override;
  std::any visitMulDivModExpression(LangParser::MulDivModExpressionContext *ctx) override;
  std::any visitUnaryPrefix(LangParser::UnaryPrefixContext *ctx) override;
  std::any visitUnaryToPostfix(LangParser::UnaryToPostfixContext *ctx) override;
  std::any visitPostfixExpression(LangParser::PostfixExpressionContext *ctx) override;

  std::any visitPrimaryAtom(LangParser::PrimaryAtomContext *ctx) override;
  std::any visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *ctx) override;
  std::any visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *ctx) override;
  std::any visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *ctx) override;
  std::any visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *ctx) override;
  std::any visitPrimaryParenExp(LangParser::PrimaryParenExpContext *ctx) override;
  std::any visitPrimaryNew(LangParser::PrimaryNewContext *ctx) override;
  std::any visitPrimaryLambda(LangParser::PrimaryLambdaContext *ctx) override;

  std::any visitAtomexp(LangParser::AtomexpContext *ctx) override;
  std::any visitLambdaExprDef(LangParser::LambdaExprDefContext *ctx) override;
  std::any visitListLiteralDef(LangParser::ListLiteralDefContext *ctx) override;
  std::any visitMapLiteralDef(LangParser::MapLiteralDefContext *ctx) override;
  std::any visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *ctx) override;
  std::any visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *ctx) override;
  std::any visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *ctx) override;
  std::any visitMapEntryIntKey(LangParser::MapEntryIntKeyContext *ctx) override;
  std::any visitMapEntryFloatKey(LangParser::MapEntryFloatKeyContext *ctx) override;

  std::any visitNewExpressionDef(LangParser::NewExpressionDefContext *ctx) override;
  std::any visitIfStatement(LangParser::IfStatementContext *ctx) override;
  std::any visitWhileStatement(LangParser::WhileStatementContext *ctx) override;
  std::any visitForStatement(LangParser::ForStatementContext *ctx) override;
  std::any visitForNumericControl(LangParser::ForNumericControlContext *ctx) override;
  std::any visitForEachControl(LangParser::ForEachControlContext *ctx) override;
  std::any visitForNumericVarTyped(LangParser::ForNumericVarTypedContext *ctx) override;
  std::any visitForNumericVarUntyped(LangParser::ForNumericVarUntypedContext *ctx) override;
  std::any visitForEachVarTyped(LangParser::ForEachVarTypedContext *ctx) override;
  std::any visitForEachVarUntyped(LangParser::ForEachVarUntypedContext *ctx) override;
  std::any visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *ctx) override;
  std::any visitTypeQualifiedIdentifier(LangParser::TypeQualifiedIdentifierContext *ctx) override;

  std::any
  visitMutiVariableDeclarationDef(LangParser::MutiVariableDeclarationDefContext *ctx) override;

  std::any visitMultiReturnFunctionDeclarationDef(
      LangParser::MultiReturnFunctionDeclarationDefContext *ctx) override;
  std::any visitMultiReturnClassMethodMember(
      LangParser::MultiReturnClassMethodMemberContext *context) override;

  std::any visitImportStmt(LangParser::ImportStmtContext *ctx) override;
  std::any visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *ctx) override;
  std::any visitImportNamedStmt(LangParser::ImportNamedStmtContext *ctx) override;
  std::any visitImportSpecifier(LangParser::ImportSpecifierContext *ctx) override;
};

#endif