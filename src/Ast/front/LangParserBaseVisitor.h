
// Generated from LangParser.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "LangParserVisitor.h"


/**
 * This class provides an empty implementation of LangParserVisitor, which can be
 * extended to create a visitor which only needs to handle a subset of the available methods.
 */
class  LangParserBaseVisitor : public LangParserVisitor {
public:

  virtual std::any visitCompilationUnit(LangParser::CompilationUnitContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlockStatement(LangParser::BlockStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitSemicolonStmt(LangParser::SemicolonStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAssignStmt(LangParser::AssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUpdateStmt(LangParser::UpdateStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpressionStmt(LangParser::ExpressionStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeclarationStmt(LangParser::DeclarationStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIfStmt(LangParser::IfStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWhileStmt(LangParser::WhileStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForStmt(LangParser::ForStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBreakStmt(LangParser::BreakStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitContinueStmt(LangParser::ContinueStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitReturnStmt(LangParser::ReturnStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBlockStmt(LangParser::BlockStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImportStmt(LangParser::ImportStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeferStmt(LangParser::DeferStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImportNamedStmt(LangParser::ImportNamedStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitImportSpecifier(LangParser::ImportSpecifierContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeferBlockStmt(LangParser::DeferBlockStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNormalAssignStmt(LangParser::NormalAssignStmtContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLvalueBase(LangParser::LvalueBaseContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLvalueIndex(LangParser::LvalueIndexContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLvalueMember(LangParser::LvalueMemberContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeclaration(LangParser::DeclarationContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitVariableDeclarationDef(LangParser::VariableDeclarationDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMutiVariableDeclarationDef(LangParser::MutiVariableDeclarationDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitDeclaration_item(LangParser::Declaration_itemContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitFunctionDeclarationDef(LangParser::FunctionDeclarationDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultiReturnFunctionDeclarationDef(LangParser::MultiReturnFunctionDeclarationDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitClassFieldMember(LangParser::ClassFieldMemberContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitClassMethodMember(LangParser::ClassMethodMemberContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultiReturnClassMethodMember(LangParser::MultiReturnClassMethodMemberContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitClassEmptyMember(LangParser::ClassEmptyMemberContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypePrimitive(LangParser::TypePrimitiveContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeListType(LangParser::TypeListTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeMap(LangParser::TypeMapContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeAny(LangParser::TypeAnyContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitTypeQualifiedIdentifier(LangParser::TypeQualifiedIdentifierContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimitiveType(LangParser::PrimitiveTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListType(LangParser::ListTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapType(LangParser::MapTypeContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpression(LangParser::ExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitExpressionList(LangParser::ExpressionListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLogicalOrExpression(LangParser::LogicalOrExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLogicalAndExpression(LangParser::LogicalAndExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBitwiseOrExpression(LangParser::BitwiseOrExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBitwiseXorExpression(LangParser::BitwiseXorExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitBitwiseAndExpression(LangParser::BitwiseAndExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEqualityExpression(LangParser::EqualityExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitEqualityExpOp(LangParser::EqualityExpOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitComparisonExpression(LangParser::ComparisonExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitComparisonExpOp(LangParser::ComparisonExpOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitShiftExpression(LangParser::ShiftExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitShiftExpOp(LangParser::ShiftExpOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitConcatExpression(LangParser::ConcatExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddSubExpression(LangParser::AddSubExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAddSubExpOp(LangParser::AddSubExpOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulDivModExpression(LangParser::MulDivModExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMulDivModExpOp(LangParser::MulDivModExpOpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryPrefix(LangParser::UnaryPrefixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitUnaryToPostfix(LangParser::UnaryToPostfixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPostfixExpression(LangParser::PostfixExpressionContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPostfixIndexSuffix(LangParser::PostfixIndexSuffixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPostfixMemberSuffix(LangParser::PostfixMemberSuffixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPostfixColonLookupSuffix(LangParser::PostfixColonLookupSuffixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPostfixCallSuffix(LangParser::PostfixCallSuffixContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryAtom(LangParser::PrimaryAtomContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryParenExp(LangParser::PrimaryParenExpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryNew(LangParser::PrimaryNewContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitPrimaryLambda(LangParser::PrimaryLambdaContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitAtomexp(LangParser::AtomexpContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitLambdaExprDef(LangParser::LambdaExprDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitListLiteralDef(LangParser::ListLiteralDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapLiteralDef(LangParser::MapLiteralDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapEntryList(LangParser::MapEntryListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitNewExpressionDef(LangParser::NewExpressionDefContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitIfStatement(LangParser::IfStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitWhileStatement(LangParser::WhileStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForStatement(LangParser::ForStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForCStyleControl(LangParser::ForCStyleControlContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForEachExplicitControl(LangParser::ForEachExplicitControlContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForUpdate(LangParser::ForUpdateContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForUpdateSingle(LangParser::ForUpdateSingleContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitForInitStatement(LangParser::ForInitStatementContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitMultiDeclaration(LangParser::MultiDeclarationContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParameterList(LangParser::ParameterListContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitParameter(LangParser::ParameterContext *ctx) override {
    return visitChildren(ctx);
  }

  virtual std::any visitArguments(LangParser::ArgumentsContext *ctx) override {
    return visitChildren(ctx);
  }


};

