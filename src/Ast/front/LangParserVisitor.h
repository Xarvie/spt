
// Generated from LangParser.g4 by ANTLR 4.13.1

#pragma once


#include "antlr4-runtime.h"
#include "LangParser.h"



/**
 * This class defines an abstract visitor for a parse tree
 * produced by LangParser.
 */
class  LangParserVisitor : public antlr4::tree::AbstractParseTreeVisitor {
public:

  /**
   * Visit parse trees produced by LangParser.
   */
    virtual std::any visitCompilationUnit(LangParser::CompilationUnitContext *context) = 0;

    virtual std::any visitBlockStatement(LangParser::BlockStatementContext *context) = 0;

    virtual std::any visitSemicolonStmt(LangParser::SemicolonStmtContext *context) = 0;

    virtual std::any visitAssignStmt(LangParser::AssignStmtContext *context) = 0;

    virtual std::any visitUpdateStmt(LangParser::UpdateStmtContext *context) = 0;

    virtual std::any visitExpressionStmt(LangParser::ExpressionStmtContext *context) = 0;

    virtual std::any visitDeclarationStmt(LangParser::DeclarationStmtContext *context) = 0;

    virtual std::any visitIfStmt(LangParser::IfStmtContext *context) = 0;

    virtual std::any visitWhileStmt(LangParser::WhileStmtContext *context) = 0;

    virtual std::any visitForStmt(LangParser::ForStmtContext *context) = 0;

    virtual std::any visitBreakStmt(LangParser::BreakStmtContext *context) = 0;

    virtual std::any visitContinueStmt(LangParser::ContinueStmtContext *context) = 0;

    virtual std::any visitReturnStmt(LangParser::ReturnStmtContext *context) = 0;

    virtual std::any visitBlockStmt(LangParser::BlockStmtContext *context) = 0;

    virtual std::any visitImportStmt(LangParser::ImportStmtContext *context) = 0;

    virtual std::any visitDeferStmt(LangParser::DeferStmtContext *context) = 0;

    virtual std::any visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *context) = 0;

    virtual std::any visitImportNamedStmt(LangParser::ImportNamedStmtContext *context) = 0;

    virtual std::any visitImportSpecifier(LangParser::ImportSpecifierContext *context) = 0;

    virtual std::any visitDeferBlockStmt(LangParser::DeferBlockStmtContext *context) = 0;

    virtual std::any visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *context) = 0;

    virtual std::any visitNormalAssignStmt(LangParser::NormalAssignStmtContext *context) = 0;

    virtual std::any visitLvalueBase(LangParser::LvalueBaseContext *context) = 0;

    virtual std::any visitLvalueIndex(LangParser::LvalueIndexContext *context) = 0;

    virtual std::any visitLvalueMember(LangParser::LvalueMemberContext *context) = 0;

    virtual std::any visitDeclaration(LangParser::DeclarationContext *context) = 0;

    virtual std::any visitVariableDeclarationDef(LangParser::VariableDeclarationDefContext *context) = 0;

    virtual std::any visitMutiVariableDeclarationDef(LangParser::MutiVariableDeclarationDefContext *context) = 0;

    virtual std::any visitDeclaration_item(LangParser::Declaration_itemContext *context) = 0;

    virtual std::any visitFunctionDeclarationDef(LangParser::FunctionDeclarationDefContext *context) = 0;

    virtual std::any visitMultiReturnFunctionDeclarationDef(LangParser::MultiReturnFunctionDeclarationDefContext *context) = 0;

    virtual std::any visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *context) = 0;

    virtual std::any visitClassFieldMember(LangParser::ClassFieldMemberContext *context) = 0;

    virtual std::any visitClassMethodMember(LangParser::ClassMethodMemberContext *context) = 0;

    virtual std::any visitMultiReturnClassMethodMember(LangParser::MultiReturnClassMethodMemberContext *context) = 0;

    virtual std::any visitClassEmptyMember(LangParser::ClassEmptyMemberContext *context) = 0;

    virtual std::any visitTypePrimitive(LangParser::TypePrimitiveContext *context) = 0;

    virtual std::any visitTypeListType(LangParser::TypeListTypeContext *context) = 0;

    virtual std::any visitTypeMap(LangParser::TypeMapContext *context) = 0;

    virtual std::any visitTypeAny(LangParser::TypeAnyContext *context) = 0;

    virtual std::any visitTypeQualifiedIdentifier(LangParser::TypeQualifiedIdentifierContext *context) = 0;

    virtual std::any visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *context) = 0;

    virtual std::any visitPrimitiveType(LangParser::PrimitiveTypeContext *context) = 0;

    virtual std::any visitListType(LangParser::ListTypeContext *context) = 0;

    virtual std::any visitMapType(LangParser::MapTypeContext *context) = 0;

    virtual std::any visitExpression(LangParser::ExpressionContext *context) = 0;

    virtual std::any visitExpressionList(LangParser::ExpressionListContext *context) = 0;

    virtual std::any visitLogicalOrExpression(LangParser::LogicalOrExpressionContext *context) = 0;

    virtual std::any visitLogicalAndExpression(LangParser::LogicalAndExpressionContext *context) = 0;

    virtual std::any visitBitwiseOrExpression(LangParser::BitwiseOrExpressionContext *context) = 0;

    virtual std::any visitBitwiseXorExpression(LangParser::BitwiseXorExpressionContext *context) = 0;

    virtual std::any visitBitwiseAndExpression(LangParser::BitwiseAndExpressionContext *context) = 0;

    virtual std::any visitEqualityExpression(LangParser::EqualityExpressionContext *context) = 0;

    virtual std::any visitEqualityExpOp(LangParser::EqualityExpOpContext *context) = 0;

    virtual std::any visitComparisonExpression(LangParser::ComparisonExpressionContext *context) = 0;

    virtual std::any visitComparisonExpOp(LangParser::ComparisonExpOpContext *context) = 0;

    virtual std::any visitShiftExpression(LangParser::ShiftExpressionContext *context) = 0;

    virtual std::any visitShiftExpOp(LangParser::ShiftExpOpContext *context) = 0;

    virtual std::any visitConcatExpression(LangParser::ConcatExpressionContext *context) = 0;

    virtual std::any visitAddSubExpression(LangParser::AddSubExpressionContext *context) = 0;

    virtual std::any visitAddSubExpOp(LangParser::AddSubExpOpContext *context) = 0;

    virtual std::any visitMulDivModExpression(LangParser::MulDivModExpressionContext *context) = 0;

    virtual std::any visitMulDivModExpOp(LangParser::MulDivModExpOpContext *context) = 0;

    virtual std::any visitUnaryPrefix(LangParser::UnaryPrefixContext *context) = 0;

    virtual std::any visitUnaryToPostfix(LangParser::UnaryToPostfixContext *context) = 0;

    virtual std::any visitPostfixExpression(LangParser::PostfixExpressionContext *context) = 0;

    virtual std::any visitPostfixIndexSuffix(LangParser::PostfixIndexSuffixContext *context) = 0;

    virtual std::any visitPostfixMemberSuffix(LangParser::PostfixMemberSuffixContext *context) = 0;

    virtual std::any visitPostfixColonLookupSuffix(LangParser::PostfixColonLookupSuffixContext *context) = 0;

    virtual std::any visitPostfixCallSuffix(LangParser::PostfixCallSuffixContext *context) = 0;

    virtual std::any visitPrimaryAtom(LangParser::PrimaryAtomContext *context) = 0;

    virtual std::any visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *context) = 0;

    virtual std::any visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *context) = 0;

    virtual std::any visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *context) = 0;

    virtual std::any visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *context) = 0;

    virtual std::any visitPrimaryParenExp(LangParser::PrimaryParenExpContext *context) = 0;

    virtual std::any visitPrimaryNew(LangParser::PrimaryNewContext *context) = 0;

    virtual std::any visitPrimaryLambda(LangParser::PrimaryLambdaContext *context) = 0;

    virtual std::any visitAtomexp(LangParser::AtomexpContext *context) = 0;

    virtual std::any visitLambdaExprDef(LangParser::LambdaExprDefContext *context) = 0;

    virtual std::any visitListLiteralDef(LangParser::ListLiteralDefContext *context) = 0;

    virtual std::any visitMapLiteralDef(LangParser::MapLiteralDefContext *context) = 0;

    virtual std::any visitMapEntryList(LangParser::MapEntryListContext *context) = 0;

    virtual std::any visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *context) = 0;

    virtual std::any visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *context) = 0;

    virtual std::any visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *context) = 0;

    virtual std::any visitNewExpressionDef(LangParser::NewExpressionDefContext *context) = 0;

    virtual std::any visitIfStatement(LangParser::IfStatementContext *context) = 0;

    virtual std::any visitWhileStatement(LangParser::WhileStatementContext *context) = 0;

    virtual std::any visitForStatement(LangParser::ForStatementContext *context) = 0;

    virtual std::any visitForCStyleControl(LangParser::ForCStyleControlContext *context) = 0;

    virtual std::any visitForEachExplicitControl(LangParser::ForEachExplicitControlContext *context) = 0;

    virtual std::any visitForUpdate(LangParser::ForUpdateContext *context) = 0;

    virtual std::any visitForUpdateSingle(LangParser::ForUpdateSingleContext *context) = 0;

    virtual std::any visitForInitStatement(LangParser::ForInitStatementContext *context) = 0;

    virtual std::any visitMultiDeclaration(LangParser::MultiDeclarationContext *context) = 0;

    virtual std::any visitParameterList(LangParser::ParameterListContext *context) = 0;

    virtual std::any visitParameter(LangParser::ParameterContext *context) = 0;

    virtual std::any visitArguments(LangParser::ArgumentsContext *context) = 0;


};

