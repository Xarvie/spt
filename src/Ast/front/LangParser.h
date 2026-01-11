
// Generated from LangParser.g4 by ANTLR 4.13.1

#pragma once

#include "antlr4-runtime.h"

class LangParser : public antlr4::Parser {
public:
  enum {
    INT = 1,
    FLOAT = 2,
    NUMBER = 3,
    STRING = 4,
    BOOL = 5,
    ANY = 6,
    VOID = 7,
    NULL_ = 8,
    LIST = 9,
    MAP = 10,
    FUNCTION = 11,
    COROUTINE = 12,
    MUTIVAR = 13,
    IF = 14,
    ELSE = 15,
    WHILE = 16,
    FOR = 17,
    BREAK = 18,
    CONTINUE = 19,
    RETURN = 20,
    DEFER = 21,
    TRUE = 22,
    FALSE = 23,
    CONST = 24,
    AUTO = 25,
    GLOBAL = 26,
    STATIC = 27,
    IMPORT = 28,
    AS = 29,
    TYPE = 30,
    FROM = 31,
    PRIVATE = 32,
    EXPORT = 33,
    CLASS = 34,
    NEW = 35,
    ADD = 36,
    SUB = 37,
    MUL = 38,
    DIV = 39,
    IDIV = 40,
    MOD = 41,
    ASSIGN = 42,
    ADD_ASSIGN = 43,
    SUB_ASSIGN = 44,
    MUL_ASSIGN = 45,
    DIV_ASSIGN = 46,
    IDIV_ASSIGN = 47,
    MOD_ASSIGN = 48,
    CONCAT_ASSIGN = 49,
    EQ = 50,
    NEQ = 51,
    LT = 52,
    GT = 53,
    LTE = 54,
    GTE = 55,
    AND = 56,
    OR = 57,
    NOT = 58,
    CONCAT = 59,
    LEN = 60,
    BIT_AND = 61,
    BIT_OR = 62,
    BIT_XOR = 63,
    BIT_NOT = 64,
    LSHIFT = 65,
    ARROW = 66,
    OP = 67,
    CP = 68,
    OSB = 69,
    CSB = 70,
    OCB = 71,
    CCB = 72,
    COMMA = 73,
    DOT = 74,
    COL = 75,
    SEMICOLON = 76,
    DDD = 77,
    INTEGER = 78,
    FLOAT_LITERAL = 79,
    STRING_LITERAL = 80,
    IDENTIFIER = 81,
    WS = 82,
    LINE_COMMENT = 83,
    BLOCK_COMMENT = 84
  };

  enum {
    RuleCompilationUnit = 0,
    RuleBlockStatement = 1,
    RuleStatement = 2,
    RuleImportStatement = 3,
    RuleImportSpecifier = 4,
    RuleDeferStatement = 5,
    RuleUpdateStatement = 6,
    RuleAssignStatement = 7,
    RuleLvalue = 8,
    RuleLvalueSuffix = 9,
    RuleDeclaration = 10,
    RuleVariableDeclaration = 11,
    RuleDeclaration_item = 12,
    RuleFunctionDeclaration = 13,
    RuleClassDeclaration = 14,
    RuleClassMember = 15,
    RuleType = 16,
    RuleQualifiedIdentifier = 17,
    RulePrimitiveType = 18,
    RuleListType = 19,
    RuleMapType = 20,
    RuleExpression = 21,
    RuleExpressionList = 22,
    RuleLogicalOrExp = 23,
    RuleLogicalAndExp = 24,
    RuleBitwiseOrExp = 25,
    RuleBitwiseXorExp = 26,
    RuleBitwiseAndExp = 27,
    RuleEqualityExp = 28,
    RuleEqualityExpOp = 29,
    RuleComparisonExp = 30,
    RuleComparisonExpOp = 31,
    RuleShiftExp = 32,
    RuleShiftExpOp = 33,
    RuleConcatExp = 34,
    RuleAddSubExp = 35,
    RuleAddSubExpOp = 36,
    RuleMulDivModExp = 37,
    RuleMulDivModExpOp = 38,
    RuleUnaryExp = 39,
    RulePostfixExp = 40,
    RulePostfixSuffix = 41,
    RulePrimaryExp = 42,
    RuleAtomexp = 43,
    RuleLambdaExpression = 44,
    RuleListExpression = 45,
    RuleMapExpression = 46,
    RuleMapEntryList = 47,
    RuleMapEntry = 48,
    RuleNewExp = 49,
    RuleIfStatement = 50,
    RuleWhileStatement = 51,
    RuleForStatement = 52,
    RuleForControl = 53,
    RuleForUpdate = 54,
    RuleForUpdateSingle = 55,
    RuleForInitStatement = 56,
    RuleMultiDeclaration = 57,
    RuleParameterList = 58,
    RuleParameter = 59,
    RuleArguments = 60
  };

  explicit LangParser(antlr4::TokenStream *input);

  LangParser(antlr4::TokenStream *input, const antlr4::atn::ParserATNSimulatorOptions &options);

  ~LangParser() override;

  std::string getGrammarFileName() const override;

  const antlr4::atn::ATN &getATN() const override;

  const std::vector<std::string> &getRuleNames() const override;

  const antlr4::dfa::Vocabulary &getVocabulary() const override;

  antlr4::atn::SerializedATNView getSerializedATN() const override;

  class CompilationUnitContext;
  class BlockStatementContext;
  class StatementContext;
  class ImportStatementContext;
  class ImportSpecifierContext;
  class DeferStatementContext;
  class UpdateStatementContext;
  class AssignStatementContext;
  class LvalueContext;
  class LvalueSuffixContext;
  class DeclarationContext;
  class VariableDeclarationContext;
  class Declaration_itemContext;
  class FunctionDeclarationContext;
  class ClassDeclarationContext;
  class ClassMemberContext;
  class TypeContext;
  class QualifiedIdentifierContext;
  class PrimitiveTypeContext;
  class ListTypeContext;
  class MapTypeContext;
  class ExpressionContext;
  class ExpressionListContext;
  class LogicalOrExpContext;
  class LogicalAndExpContext;
  class BitwiseOrExpContext;
  class BitwiseXorExpContext;
  class BitwiseAndExpContext;
  class EqualityExpContext;
  class EqualityExpOpContext;
  class ComparisonExpContext;
  class ComparisonExpOpContext;
  class ShiftExpContext;
  class ShiftExpOpContext;
  class ConcatExpContext;
  class AddSubExpContext;
  class AddSubExpOpContext;
  class MulDivModExpContext;
  class MulDivModExpOpContext;
  class UnaryExpContext;
  class PostfixExpContext;
  class PostfixSuffixContext;
  class PrimaryExpContext;
  class AtomexpContext;
  class LambdaExpressionContext;
  class ListExpressionContext;
  class MapExpressionContext;
  class MapEntryListContext;
  class MapEntryContext;
  class NewExpContext;
  class IfStatementContext;
  class WhileStatementContext;
  class ForStatementContext;
  class ForControlContext;
  class ForUpdateContext;
  class ForUpdateSingleContext;
  class ForInitStatementContext;
  class MultiDeclarationContext;
  class ParameterListContext;
  class ParameterContext;
  class ArgumentsContext;

  class CompilationUnitContext : public antlr4::ParserRuleContext {
  public:
    CompilationUnitContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EOF();
    std::vector<StatementContext *> statement();
    StatementContext *statement(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  CompilationUnitContext *compilationUnit();

  class BlockStatementContext : public antlr4::ParserRuleContext {
  public:
    BlockStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *OCB();
    antlr4::tree::TerminalNode *CCB();
    std::vector<StatementContext *> statement();
    StatementContext *statement(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  BlockStatementContext *blockStatement();

  class StatementContext : public antlr4::ParserRuleContext {
  public:
    StatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    StatementContext() = default;
    void copyFrom(StatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ImportStmtContext : public StatementContext {
  public:
    ImportStmtContext(StatementContext *ctx);

    ImportStatementContext *importStatement();
    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ForStmtContext : public StatementContext {
  public:
    ForStmtContext(StatementContext *ctx);

    ForStatementContext *forStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class WhileStmtContext : public StatementContext {
  public:
    WhileStmtContext(StatementContext *ctx);

    WhileStatementContext *whileStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class AssignStmtContext : public StatementContext {
  public:
    AssignStmtContext(StatementContext *ctx);

    AssignStatementContext *assignStatement();
    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ExpressionStmtContext : public StatementContext {
  public:
    ExpressionStmtContext(StatementContext *ctx);

    ExpressionContext *expression();
    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ReturnStmtContext : public StatementContext {
  public:
    ReturnStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *RETURN();
    antlr4::tree::TerminalNode *SEMICOLON();
    ExpressionListContext *expressionList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class UpdateStmtContext : public StatementContext {
  public:
    UpdateStmtContext(StatementContext *ctx);

    UpdateStatementContext *updateStatement();
    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class DeferStmtContext : public StatementContext {
  public:
    DeferStmtContext(StatementContext *ctx);

    DeferStatementContext *deferStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class DeclarationStmtContext : public StatementContext {
  public:
    DeclarationStmtContext(StatementContext *ctx);

    DeclarationContext *declaration();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class SemicolonStmtContext : public StatementContext {
  public:
    SemicolonStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class IfStmtContext : public StatementContext {
  public:
    IfStmtContext(StatementContext *ctx);

    IfStatementContext *ifStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class BlockStmtContext : public StatementContext {
  public:
    BlockStmtContext(StatementContext *ctx);

    BlockStatementContext *blockStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class BreakStmtContext : public StatementContext {
  public:
    BreakStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *BREAK();
    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ContinueStmtContext : public StatementContext {
  public:
    ContinueStmtContext(StatementContext *ctx);

    antlr4::tree::TerminalNode *CONTINUE();
    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  StatementContext *statement();

  class ImportStatementContext : public antlr4::ParserRuleContext {
  public:
    ImportStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ImportStatementContext() = default;
    void copyFrom(ImportStatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ImportNamespaceStmtContext : public ImportStatementContext {
  public:
    ImportNamespaceStmtContext(ImportStatementContext *ctx);

    antlr4::tree::TerminalNode *IMPORT();
    antlr4::tree::TerminalNode *MUL();
    antlr4::tree::TerminalNode *AS();
    antlr4::tree::TerminalNode *IDENTIFIER();
    antlr4::tree::TerminalNode *FROM();
    antlr4::tree::TerminalNode *STRING_LITERAL();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ImportNamedStmtContext : public ImportStatementContext {
  public:
    ImportNamedStmtContext(ImportStatementContext *ctx);

    antlr4::tree::TerminalNode *IMPORT();
    antlr4::tree::TerminalNode *OCB();
    std::vector<ImportSpecifierContext *> importSpecifier();
    ImportSpecifierContext *importSpecifier(size_t i);
    antlr4::tree::TerminalNode *CCB();
    antlr4::tree::TerminalNode *FROM();
    antlr4::tree::TerminalNode *STRING_LITERAL();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ImportStatementContext *importStatement();

  class ImportSpecifierContext : public antlr4::ParserRuleContext {
  public:
    ImportSpecifierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENTIFIER();
    antlr4::tree::TerminalNode *IDENTIFIER(size_t i);
    antlr4::tree::TerminalNode *TYPE();
    antlr4::tree::TerminalNode *AS();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ImportSpecifierContext *importSpecifier();

  class DeferStatementContext : public antlr4::ParserRuleContext {
  public:
    DeferStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    DeferStatementContext() = default;
    void copyFrom(DeferStatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class DeferBlockStmtContext : public DeferStatementContext {
  public:
    DeferBlockStmtContext(DeferStatementContext *ctx);

    antlr4::tree::TerminalNode *DEFER();
    BlockStatementContext *blockStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  DeferStatementContext *deferStatement();

  class UpdateStatementContext : public antlr4::ParserRuleContext {
  public:
    UpdateStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    UpdateStatementContext() = default;
    void copyFrom(UpdateStatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class UpdateAssignStmtContext : public UpdateStatementContext {
  public:
    UpdateAssignStmtContext(UpdateStatementContext *ctx);

    antlr4::Token *op = nullptr;
    LvalueContext *lvalue();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *ADD_ASSIGN();
    antlr4::tree::TerminalNode *SUB_ASSIGN();
    antlr4::tree::TerminalNode *MUL_ASSIGN();
    antlr4::tree::TerminalNode *DIV_ASSIGN();
    antlr4::tree::TerminalNode *IDIV_ASSIGN();
    antlr4::tree::TerminalNode *MOD_ASSIGN();
    antlr4::tree::TerminalNode *CONCAT_ASSIGN();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  UpdateStatementContext *updateStatement();

  class AssignStatementContext : public antlr4::ParserRuleContext {
  public:
    AssignStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    AssignStatementContext() = default;
    void copyFrom(AssignStatementContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class NormalAssignStmtContext : public AssignStatementContext {
  public:
    NormalAssignStmtContext(AssignStatementContext *ctx);

    std::vector<LvalueContext *> lvalue();
    LvalueContext *lvalue(size_t i);
    antlr4::tree::TerminalNode *ASSIGN();
    std::vector<ExpressionContext *> expression();
    ExpressionContext *expression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  AssignStatementContext *assignStatement();

  class LvalueContext : public antlr4::ParserRuleContext {
  public:
    LvalueContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    LvalueContext() = default;
    void copyFrom(LvalueContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class LvalueBaseContext : public LvalueContext {
  public:
    LvalueBaseContext(LvalueContext *ctx);

    antlr4::tree::TerminalNode *IDENTIFIER();
    std::vector<LvalueSuffixContext *> lvalueSuffix();
    LvalueSuffixContext *lvalueSuffix(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  LvalueContext *lvalue();

  class LvalueSuffixContext : public antlr4::ParserRuleContext {
  public:
    LvalueSuffixContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    LvalueSuffixContext() = default;
    void copyFrom(LvalueSuffixContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class LvalueIndexContext : public LvalueSuffixContext {
  public:
    LvalueIndexContext(LvalueSuffixContext *ctx);

    antlr4::tree::TerminalNode *OSB();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *CSB();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class LvalueMemberContext : public LvalueSuffixContext {
  public:
    LvalueMemberContext(LvalueSuffixContext *ctx);

    antlr4::tree::TerminalNode *DOT();
    antlr4::tree::TerminalNode *IDENTIFIER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  LvalueSuffixContext *lvalueSuffix();

  class DeclarationContext : public antlr4::ParserRuleContext {
  public:
    DeclarationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    VariableDeclarationContext *variableDeclaration();
    antlr4::tree::TerminalNode *SEMICOLON();
    FunctionDeclarationContext *functionDeclaration();
    ClassDeclarationContext *classDeclaration();
    antlr4::tree::TerminalNode *EXPORT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  DeclarationContext *declaration();

  class VariableDeclarationContext : public antlr4::ParserRuleContext {
  public:
    VariableDeclarationContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    VariableDeclarationContext() = default;
    void copyFrom(VariableDeclarationContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class MutiVariableDeclarationDefContext : public VariableDeclarationContext {
  public:
    MutiVariableDeclarationDefContext(VariableDeclarationContext *ctx);

    antlr4::tree::TerminalNode *MUTIVAR();
    std::vector<antlr4::tree::TerminalNode *> IDENTIFIER();
    antlr4::tree::TerminalNode *IDENTIFIER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> GLOBAL();
    antlr4::tree::TerminalNode *GLOBAL(size_t i);
    std::vector<antlr4::tree::TerminalNode *> CONST();
    antlr4::tree::TerminalNode *CONST(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);
    antlr4::tree::TerminalNode *ASSIGN();
    ExpressionContext *expression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class VariableDeclarationDefContext : public VariableDeclarationContext {
  public:
    VariableDeclarationDefContext(VariableDeclarationContext *ctx);

    Declaration_itemContext *declaration_item();
    antlr4::tree::TerminalNode *GLOBAL();
    antlr4::tree::TerminalNode *CONST();
    antlr4::tree::TerminalNode *ASSIGN();
    ExpressionContext *expression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  VariableDeclarationContext *variableDeclaration();

  class Declaration_itemContext : public antlr4::ParserRuleContext {
  public:
    Declaration_itemContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *IDENTIFIER();
    TypeContext *type();
    antlr4::tree::TerminalNode *AUTO();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  Declaration_itemContext *declaration_item();

  class FunctionDeclarationContext : public antlr4::ParserRuleContext {
  public:
    FunctionDeclarationContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    FunctionDeclarationContext() = default;
    void copyFrom(FunctionDeclarationContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class FunctionDeclarationDefContext : public FunctionDeclarationContext {
  public:
    FunctionDeclarationDefContext(FunctionDeclarationContext *ctx);

    TypeContext *type();
    QualifiedIdentifierContext *qualifiedIdentifier();
    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    BlockStatementContext *blockStatement();
    antlr4::tree::TerminalNode *GLOBAL();
    ParameterListContext *parameterList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class MultiReturnFunctionDeclarationDefContext : public FunctionDeclarationContext {
  public:
    MultiReturnFunctionDeclarationDefContext(FunctionDeclarationContext *ctx);

    antlr4::tree::TerminalNode *MUTIVAR();
    QualifiedIdentifierContext *qualifiedIdentifier();
    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    BlockStatementContext *blockStatement();
    antlr4::tree::TerminalNode *GLOBAL();
    ParameterListContext *parameterList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  FunctionDeclarationContext *functionDeclaration();

  class ClassDeclarationContext : public antlr4::ParserRuleContext {
  public:
    ClassDeclarationContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ClassDeclarationContext() = default;
    void copyFrom(ClassDeclarationContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ClassDeclarationDefContext : public ClassDeclarationContext {
  public:
    ClassDeclarationDefContext(ClassDeclarationContext *ctx);

    antlr4::tree::TerminalNode *CLASS();
    antlr4::tree::TerminalNode *IDENTIFIER();
    antlr4::tree::TerminalNode *OCB();
    antlr4::tree::TerminalNode *CCB();
    std::vector<ClassMemberContext *> classMember();
    ClassMemberContext *classMember(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ClassDeclarationContext *classDeclaration();

  class ClassMemberContext : public antlr4::ParserRuleContext {
  public:
    ClassMemberContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ClassMemberContext() = default;
    void copyFrom(ClassMemberContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ClassMethodMemberContext : public ClassMemberContext {
  public:
    ClassMethodMemberContext(ClassMemberContext *ctx);

    TypeContext *type();
    antlr4::tree::TerminalNode *IDENTIFIER();
    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    BlockStatementContext *blockStatement();
    antlr4::tree::TerminalNode *STATIC();
    ParameterListContext *parameterList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ClassFieldMemberContext : public ClassMemberContext {
  public:
    ClassFieldMemberContext(ClassMemberContext *ctx);

    Declaration_itemContext *declaration_item();
    antlr4::tree::TerminalNode *STATIC();
    antlr4::tree::TerminalNode *CONST();
    antlr4::tree::TerminalNode *ASSIGN();
    ExpressionContext *expression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ClassEmptyMemberContext : public ClassMemberContext {
  public:
    ClassEmptyMemberContext(ClassMemberContext *ctx);

    antlr4::tree::TerminalNode *SEMICOLON();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class MultiReturnClassMethodMemberContext : public ClassMemberContext {
  public:
    MultiReturnClassMethodMemberContext(ClassMemberContext *ctx);

    antlr4::tree::TerminalNode *MUTIVAR();
    antlr4::tree::TerminalNode *IDENTIFIER();
    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    BlockStatementContext *blockStatement();
    antlr4::tree::TerminalNode *STATIC();
    ParameterListContext *parameterList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ClassMemberContext *classMember();

  class TypeContext : public antlr4::ParserRuleContext {
  public:
    TypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    TypeContext() = default;
    void copyFrom(TypeContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class TypeMapContext : public TypeContext {
  public:
    TypeMapContext(TypeContext *ctx);

    MapTypeContext *mapType();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class TypeAnyContext : public TypeContext {
  public:
    TypeAnyContext(TypeContext *ctx);

    antlr4::tree::TerminalNode *ANY();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class TypeListTypeContext : public TypeContext {
  public:
    TypeListTypeContext(TypeContext *ctx);

    ListTypeContext *listType();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class TypeQualifiedIdentifierContext : public TypeContext {
  public:
    TypeQualifiedIdentifierContext(TypeContext *ctx);

    QualifiedIdentifierContext *qualifiedIdentifier();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class TypePrimitiveContext : public TypeContext {
  public:
    TypePrimitiveContext(TypeContext *ctx);

    PrimitiveTypeContext *primitiveType();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  TypeContext *type();

  class QualifiedIdentifierContext : public antlr4::ParserRuleContext {
  public:
    QualifiedIdentifierContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IDENTIFIER();
    antlr4::tree::TerminalNode *IDENTIFIER(size_t i);
    std::vector<antlr4::tree::TerminalNode *> DOT();
    antlr4::tree::TerminalNode *DOT(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  QualifiedIdentifierContext *qualifiedIdentifier();

  class PrimitiveTypeContext : public antlr4::ParserRuleContext {
  public:
    PrimitiveTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *INT();
    antlr4::tree::TerminalNode *FLOAT();
    antlr4::tree::TerminalNode *NUMBER();
    antlr4::tree::TerminalNode *STRING();
    antlr4::tree::TerminalNode *BOOL();
    antlr4::tree::TerminalNode *VOID();
    antlr4::tree::TerminalNode *NULL_();
    antlr4::tree::TerminalNode *COROUTINE();
    antlr4::tree::TerminalNode *FUNCTION();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  PrimitiveTypeContext *primitiveType();

  class ListTypeContext : public antlr4::ParserRuleContext {
  public:
    ListTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LIST();
    antlr4::tree::TerminalNode *LT();
    TypeContext *type();
    antlr4::tree::TerminalNode *GT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ListTypeContext *listType();

  class MapTypeContext : public antlr4::ParserRuleContext {
  public:
    MapTypeContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MAP();
    antlr4::tree::TerminalNode *LT();
    std::vector<TypeContext *> type();
    TypeContext *type(size_t i);
    antlr4::tree::TerminalNode *COMMA();
    antlr4::tree::TerminalNode *GT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MapTypeContext *mapType();

  class ExpressionContext : public antlr4::ParserRuleContext {
  public:
    ExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    LogicalOrExpContext *logicalOrExp();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ExpressionContext *expression();

  class ExpressionListContext : public antlr4::ParserRuleContext {
  public:
    ExpressionListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ExpressionContext *> expression();
    ExpressionContext *expression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ExpressionListContext *expressionList();

  class LogicalOrExpContext : public antlr4::ParserRuleContext {
  public:
    LogicalOrExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    LogicalOrExpContext() = default;
    void copyFrom(LogicalOrExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class LogicalOrExpressionContext : public LogicalOrExpContext {
  public:
    LogicalOrExpressionContext(LogicalOrExpContext *ctx);

    std::vector<LogicalAndExpContext *> logicalAndExp();
    LogicalAndExpContext *logicalAndExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> OR();
    antlr4::tree::TerminalNode *OR(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  LogicalOrExpContext *logicalOrExp();

  class LogicalAndExpContext : public antlr4::ParserRuleContext {
  public:
    LogicalAndExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    LogicalAndExpContext() = default;
    void copyFrom(LogicalAndExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class LogicalAndExpressionContext : public LogicalAndExpContext {
  public:
    LogicalAndExpressionContext(LogicalAndExpContext *ctx);

    std::vector<BitwiseOrExpContext *> bitwiseOrExp();
    BitwiseOrExpContext *bitwiseOrExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> AND();
    antlr4::tree::TerminalNode *AND(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  LogicalAndExpContext *logicalAndExp();

  class BitwiseOrExpContext : public antlr4::ParserRuleContext {
  public:
    BitwiseOrExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    BitwiseOrExpContext() = default;
    void copyFrom(BitwiseOrExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class BitwiseOrExpressionContext : public BitwiseOrExpContext {
  public:
    BitwiseOrExpressionContext(BitwiseOrExpContext *ctx);

    std::vector<BitwiseXorExpContext *> bitwiseXorExp();
    BitwiseXorExpContext *bitwiseXorExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> BIT_OR();
    antlr4::tree::TerminalNode *BIT_OR(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  BitwiseOrExpContext *bitwiseOrExp();

  class BitwiseXorExpContext : public antlr4::ParserRuleContext {
  public:
    BitwiseXorExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    BitwiseXorExpContext() = default;
    void copyFrom(BitwiseXorExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class BitwiseXorExpressionContext : public BitwiseXorExpContext {
  public:
    BitwiseXorExpressionContext(BitwiseXorExpContext *ctx);

    std::vector<BitwiseAndExpContext *> bitwiseAndExp();
    BitwiseAndExpContext *bitwiseAndExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> BIT_XOR();
    antlr4::tree::TerminalNode *BIT_XOR(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  BitwiseXorExpContext *bitwiseXorExp();

  class BitwiseAndExpContext : public antlr4::ParserRuleContext {
  public:
    BitwiseAndExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    BitwiseAndExpContext() = default;
    void copyFrom(BitwiseAndExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class BitwiseAndExpressionContext : public BitwiseAndExpContext {
  public:
    BitwiseAndExpressionContext(BitwiseAndExpContext *ctx);

    std::vector<EqualityExpContext *> equalityExp();
    EqualityExpContext *equalityExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> BIT_AND();
    antlr4::tree::TerminalNode *BIT_AND(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  BitwiseAndExpContext *bitwiseAndExp();

  class EqualityExpContext : public antlr4::ParserRuleContext {
  public:
    EqualityExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    EqualityExpContext() = default;
    void copyFrom(EqualityExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class EqualityExpressionContext : public EqualityExpContext {
  public:
    EqualityExpressionContext(EqualityExpContext *ctx);

    std::vector<ComparisonExpContext *> comparisonExp();
    ComparisonExpContext *comparisonExp(size_t i);
    std::vector<EqualityExpOpContext *> equalityExpOp();
    EqualityExpOpContext *equalityExpOp(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  EqualityExpContext *equalityExp();

  class EqualityExpOpContext : public antlr4::ParserRuleContext {
  public:
    EqualityExpOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *EQ();
    antlr4::tree::TerminalNode *NEQ();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  EqualityExpOpContext *equalityExpOp();

  class ComparisonExpContext : public antlr4::ParserRuleContext {
  public:
    ComparisonExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ComparisonExpContext() = default;
    void copyFrom(ComparisonExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ComparisonExpressionContext : public ComparisonExpContext {
  public:
    ComparisonExpressionContext(ComparisonExpContext *ctx);

    std::vector<ShiftExpContext *> shiftExp();
    ShiftExpContext *shiftExp(size_t i);
    std::vector<ComparisonExpOpContext *> comparisonExpOp();
    ComparisonExpOpContext *comparisonExpOp(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ComparisonExpContext *comparisonExp();

  class ComparisonExpOpContext : public antlr4::ParserRuleContext {
  public:
    ComparisonExpOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LT();
    antlr4::tree::TerminalNode *GT();
    antlr4::tree::TerminalNode *LTE();
    antlr4::tree::TerminalNode *GTE();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ComparisonExpOpContext *comparisonExpOp();

  class ShiftExpContext : public antlr4::ParserRuleContext {
  public:
    ShiftExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ShiftExpContext() = default;
    void copyFrom(ShiftExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ShiftExpressionContext : public ShiftExpContext {
  public:
    ShiftExpressionContext(ShiftExpContext *ctx);

    std::vector<ConcatExpContext *> concatExp();
    ConcatExpContext *concatExp(size_t i);
    std::vector<ShiftExpOpContext *> shiftExpOp();
    ShiftExpOpContext *shiftExpOp(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ShiftExpContext *shiftExp();

  class ShiftExpOpContext : public antlr4::ParserRuleContext {
  public:
    ShiftExpOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *LSHIFT();
    std::vector<antlr4::tree::TerminalNode *> GT();
    antlr4::tree::TerminalNode *GT(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ShiftExpOpContext *shiftExpOp();

  class ConcatExpContext : public antlr4::ParserRuleContext {
  public:
    ConcatExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ConcatExpContext() = default;
    void copyFrom(ConcatExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ConcatExpressionContext : public ConcatExpContext {
  public:
    ConcatExpressionContext(ConcatExpContext *ctx);

    std::vector<AddSubExpContext *> addSubExp();
    AddSubExpContext *addSubExp(size_t i);
    std::vector<antlr4::tree::TerminalNode *> CONCAT();
    antlr4::tree::TerminalNode *CONCAT(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ConcatExpContext *concatExp();

  class AddSubExpContext : public antlr4::ParserRuleContext {
  public:
    AddSubExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    AddSubExpContext() = default;
    void copyFrom(AddSubExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class AddSubExpressionContext : public AddSubExpContext {
  public:
    AddSubExpressionContext(AddSubExpContext *ctx);

    std::vector<MulDivModExpContext *> mulDivModExp();
    MulDivModExpContext *mulDivModExp(size_t i);
    std::vector<AddSubExpOpContext *> addSubExpOp();
    AddSubExpOpContext *addSubExpOp(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  AddSubExpContext *addSubExp();

  class AddSubExpOpContext : public antlr4::ParserRuleContext {
  public:
    AddSubExpOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *ADD();
    antlr4::tree::TerminalNode *SUB();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  AddSubExpOpContext *addSubExpOp();

  class MulDivModExpContext : public antlr4::ParserRuleContext {
  public:
    MulDivModExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    MulDivModExpContext() = default;
    void copyFrom(MulDivModExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class MulDivModExpressionContext : public MulDivModExpContext {
  public:
    MulDivModExpressionContext(MulDivModExpContext *ctx);

    std::vector<UnaryExpContext *> unaryExp();
    UnaryExpContext *unaryExp(size_t i);
    std::vector<MulDivModExpOpContext *> mulDivModExpOp();
    MulDivModExpOpContext *mulDivModExpOp(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MulDivModExpContext *mulDivModExp();

  class MulDivModExpOpContext : public antlr4::ParserRuleContext {
  public:
    MulDivModExpOpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *MUL();
    antlr4::tree::TerminalNode *DIV();
    antlr4::tree::TerminalNode *IDIV();
    antlr4::tree::TerminalNode *MOD();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MulDivModExpOpContext *mulDivModExpOp();

  class UnaryExpContext : public antlr4::ParserRuleContext {
  public:
    UnaryExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    UnaryExpContext() = default;
    void copyFrom(UnaryExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class UnaryPrefixContext : public UnaryExpContext {
  public:
    UnaryPrefixContext(UnaryExpContext *ctx);

    UnaryExpContext *unaryExp();
    antlr4::tree::TerminalNode *NOT();
    antlr4::tree::TerminalNode *SUB();
    antlr4::tree::TerminalNode *LEN();
    antlr4::tree::TerminalNode *BIT_NOT();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class UnaryToPostfixContext : public UnaryExpContext {
  public:
    UnaryToPostfixContext(UnaryExpContext *ctx);

    PostfixExpContext *postfixExp();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  UnaryExpContext *unaryExp();

  class PostfixExpContext : public antlr4::ParserRuleContext {
  public:
    PostfixExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    PostfixExpContext() = default;
    void copyFrom(PostfixExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class PostfixExpressionContext : public PostfixExpContext {
  public:
    PostfixExpressionContext(PostfixExpContext *ctx);

    PrimaryExpContext *primaryExp();
    std::vector<PostfixSuffixContext *> postfixSuffix();
    PostfixSuffixContext *postfixSuffix(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  PostfixExpContext *postfixExp();

  class PostfixSuffixContext : public antlr4::ParserRuleContext {
  public:
    PostfixSuffixContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    PostfixSuffixContext() = default;
    void copyFrom(PostfixSuffixContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class PostfixMemberSuffixContext : public PostfixSuffixContext {
  public:
    PostfixMemberSuffixContext(PostfixSuffixContext *ctx);

    antlr4::tree::TerminalNode *DOT();
    antlr4::tree::TerminalNode *IDENTIFIER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PostfixColonLookupSuffixContext : public PostfixSuffixContext {
  public:
    PostfixColonLookupSuffixContext(PostfixSuffixContext *ctx);

    antlr4::tree::TerminalNode *COL();
    antlr4::tree::TerminalNode *IDENTIFIER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PostfixIndexSuffixContext : public PostfixSuffixContext {
  public:
    PostfixIndexSuffixContext(PostfixSuffixContext *ctx);

    antlr4::tree::TerminalNode *OSB();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *CSB();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PostfixCallSuffixContext : public PostfixSuffixContext {
  public:
    PostfixCallSuffixContext(PostfixSuffixContext *ctx);

    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    ArgumentsContext *arguments();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  PostfixSuffixContext *postfixSuffix();

  class PrimaryExpContext : public antlr4::ParserRuleContext {
  public:
    PrimaryExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    PrimaryExpContext() = default;
    void copyFrom(PrimaryExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class PrimaryAtomContext : public PrimaryExpContext {
  public:
    PrimaryAtomContext(PrimaryExpContext *ctx);

    AtomexpContext *atomexp();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryParenExpContext : public PrimaryExpContext {
  public:
    PrimaryParenExpContext(PrimaryExpContext *ctx);

    antlr4::tree::TerminalNode *OP();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *CP();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryLambdaContext : public PrimaryExpContext {
  public:
    PrimaryLambdaContext(PrimaryExpContext *ctx);

    LambdaExpressionContext *lambdaExpression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryListLiteralContext : public PrimaryExpContext {
  public:
    PrimaryListLiteralContext(PrimaryExpContext *ctx);

    ListExpressionContext *listExpression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryMapLiteralContext : public PrimaryExpContext {
  public:
    PrimaryMapLiteralContext(PrimaryExpContext *ctx);

    MapExpressionContext *mapExpression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryIdentifierContext : public PrimaryExpContext {
  public:
    PrimaryIdentifierContext(PrimaryExpContext *ctx);

    antlr4::tree::TerminalNode *IDENTIFIER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryNewContext : public PrimaryExpContext {
  public:
    PrimaryNewContext(PrimaryExpContext *ctx);

    NewExpContext *newExp();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class PrimaryVarArgsContext : public PrimaryExpContext {
  public:
    PrimaryVarArgsContext(PrimaryExpContext *ctx);

    antlr4::tree::TerminalNode *DDD();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  PrimaryExpContext *primaryExp();

  class AtomexpContext : public antlr4::ParserRuleContext {
  public:
    AtomexpContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *NULL_();
    antlr4::tree::TerminalNode *TRUE();
    antlr4::tree::TerminalNode *FALSE();
    antlr4::tree::TerminalNode *INTEGER();
    antlr4::tree::TerminalNode *FLOAT_LITERAL();
    antlr4::tree::TerminalNode *STRING_LITERAL();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  AtomexpContext *atomexp();

  class LambdaExpressionContext : public antlr4::ParserRuleContext {
  public:
    LambdaExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    LambdaExpressionContext() = default;
    void copyFrom(LambdaExpressionContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class LambdaExprDefContext : public LambdaExpressionContext {
  public:
    LambdaExprDefContext(LambdaExpressionContext *ctx);

    antlr4::tree::TerminalNode *FUNCTION();
    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    antlr4::tree::TerminalNode *ARROW();
    BlockStatementContext *blockStatement();
    TypeContext *type();
    antlr4::tree::TerminalNode *MUTIVAR();
    ParameterListContext *parameterList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  LambdaExpressionContext *lambdaExpression();

  class ListExpressionContext : public antlr4::ParserRuleContext {
  public:
    ListExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ListExpressionContext() = default;
    void copyFrom(ListExpressionContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ListLiteralDefContext : public ListExpressionContext {
  public:
    ListLiteralDefContext(ListExpressionContext *ctx);

    antlr4::tree::TerminalNode *OSB();
    antlr4::tree::TerminalNode *CSB();
    ExpressionListContext *expressionList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ListExpressionContext *listExpression();

  class MapExpressionContext : public antlr4::ParserRuleContext {
  public:
    MapExpressionContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    MapExpressionContext() = default;
    void copyFrom(MapExpressionContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class MapLiteralDefContext : public MapExpressionContext {
  public:
    MapLiteralDefContext(MapExpressionContext *ctx);

    antlr4::tree::TerminalNode *OCB();
    antlr4::tree::TerminalNode *CCB();
    MapEntryListContext *mapEntryList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MapExpressionContext *mapExpression();

  class MapEntryListContext : public antlr4::ParserRuleContext {
  public:
    MapEntryListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<MapEntryContext *> mapEntry();
    MapEntryContext *mapEntry(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MapEntryListContext *mapEntryList();

  class MapEntryContext : public antlr4::ParserRuleContext {
  public:
    MapEntryContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    MapEntryContext() = default;
    void copyFrom(MapEntryContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class MapEntryExprKeyContext : public MapEntryContext {
  public:
    MapEntryExprKeyContext(MapEntryContext *ctx);

    antlr4::tree::TerminalNode *OSB();
    std::vector<ExpressionContext *> expression();
    ExpressionContext *expression(size_t i);
    antlr4::tree::TerminalNode *CSB();
    antlr4::tree::TerminalNode *COL();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class MapEntryIdentKeyContext : public MapEntryContext {
  public:
    MapEntryIdentKeyContext(MapEntryContext *ctx);

    antlr4::tree::TerminalNode *IDENTIFIER();
    antlr4::tree::TerminalNode *COL();
    ExpressionContext *expression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class MapEntryStringKeyContext : public MapEntryContext {
  public:
    MapEntryStringKeyContext(MapEntryContext *ctx);

    antlr4::tree::TerminalNode *STRING_LITERAL();
    antlr4::tree::TerminalNode *COL();
    ExpressionContext *expression();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MapEntryContext *mapEntry();

  class NewExpContext : public antlr4::ParserRuleContext {
  public:
    NewExpContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    NewExpContext() = default;
    void copyFrom(NewExpContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class NewExpressionDefContext : public NewExpContext {
  public:
    NewExpressionDefContext(NewExpContext *ctx);

    antlr4::tree::TerminalNode *NEW();
    QualifiedIdentifierContext *qualifiedIdentifier();
    antlr4::tree::TerminalNode *OP();
    antlr4::tree::TerminalNode *CP();
    ArgumentsContext *arguments();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  NewExpContext *newExp();

  class IfStatementContext : public antlr4::ParserRuleContext {
  public:
    IfStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<antlr4::tree::TerminalNode *> IF();
    antlr4::tree::TerminalNode *IF(size_t i);
    std::vector<antlr4::tree::TerminalNode *> OP();
    antlr4::tree::TerminalNode *OP(size_t i);
    std::vector<ExpressionContext *> expression();
    ExpressionContext *expression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> CP();
    antlr4::tree::TerminalNode *CP(size_t i);
    std::vector<BlockStatementContext *> blockStatement();
    BlockStatementContext *blockStatement(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ELSE();
    antlr4::tree::TerminalNode *ELSE(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  IfStatementContext *ifStatement();

  class WhileStatementContext : public antlr4::ParserRuleContext {
  public:
    WhileStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *WHILE();
    antlr4::tree::TerminalNode *OP();
    ExpressionContext *expression();
    antlr4::tree::TerminalNode *CP();
    BlockStatementContext *blockStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  WhileStatementContext *whileStatement();

  class ForStatementContext : public antlr4::ParserRuleContext {
  public:
    ForStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    antlr4::tree::TerminalNode *FOR();
    antlr4::tree::TerminalNode *OP();
    ForControlContext *forControl();
    antlr4::tree::TerminalNode *CP();
    BlockStatementContext *blockStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ForStatementContext *forStatement();

  class ForControlContext : public antlr4::ParserRuleContext {
  public:
    ForControlContext(antlr4::ParserRuleContext *parent, size_t invokingState);

    ForControlContext() = default;
    void copyFrom(ForControlContext *context);
    using antlr4::ParserRuleContext::copyFrom;

    virtual size_t getRuleIndex() const override;
  };

  class ForEachExplicitControlContext : public ForControlContext {
  public:
    ForEachExplicitControlContext(ForControlContext *ctx);

    std::vector<Declaration_itemContext *> declaration_item();
    Declaration_itemContext *declaration_item(size_t i);
    antlr4::tree::TerminalNode *COL();
    ExpressionListContext *expressionList();
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  class ForCStyleControlContext : public ForControlContext {
  public:
    ForCStyleControlContext(ForControlContext *ctx);

    ForInitStatementContext *forInitStatement();
    std::vector<antlr4::tree::TerminalNode *> SEMICOLON();
    antlr4::tree::TerminalNode *SEMICOLON(size_t i);
    ExpressionContext *expression();
    ForUpdateContext *forUpdate();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ForControlContext *forControl();

  class ForUpdateContext : public antlr4::ParserRuleContext {
  public:
    ForUpdateContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ForUpdateSingleContext *> forUpdateSingle();
    ForUpdateSingleContext *forUpdateSingle(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ForUpdateContext *forUpdate();

  class ForUpdateSingleContext : public antlr4::ParserRuleContext {
  public:
    ForUpdateSingleContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExpressionContext *expression();
    UpdateStatementContext *updateStatement();
    AssignStatementContext *assignStatement();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ForUpdateSingleContext *forUpdateSingle();

  class ForInitStatementContext : public antlr4::ParserRuleContext {
  public:
    ForInitStatementContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    MultiDeclarationContext *multiDeclaration();
    AssignStatementContext *assignStatement();
    ExpressionListContext *expressionList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ForInitStatementContext *forInitStatement();

  class MultiDeclarationContext : public antlr4::ParserRuleContext {
  public:
    MultiDeclarationContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<Declaration_itemContext *> declaration_item();
    Declaration_itemContext *declaration_item(size_t i);
    std::vector<antlr4::tree::TerminalNode *> ASSIGN();
    antlr4::tree::TerminalNode *ASSIGN(size_t i);
    std::vector<ExpressionContext *> expression();
    ExpressionContext *expression(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  MultiDeclarationContext *multiDeclaration();

  class ParameterListContext : public antlr4::ParserRuleContext {
  public:
    ParameterListContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    std::vector<ParameterContext *> parameter();
    ParameterContext *parameter(size_t i);
    std::vector<antlr4::tree::TerminalNode *> COMMA();
    antlr4::tree::TerminalNode *COMMA(size_t i);
    antlr4::tree::TerminalNode *DDD();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ParameterListContext *parameterList();

  class ParameterContext : public antlr4::ParserRuleContext {
  public:
    ParameterContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    TypeContext *type();
    antlr4::tree::TerminalNode *IDENTIFIER();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ParameterContext *parameter();

  class ArgumentsContext : public antlr4::ParserRuleContext {
  public:
    ArgumentsContext(antlr4::ParserRuleContext *parent, size_t invokingState);
    virtual size_t getRuleIndex() const override;
    ExpressionListContext *expressionList();

    virtual std::any accept(antlr4::tree::ParseTreeVisitor *visitor) override;
  };

  ArgumentsContext *arguments();

  // By default the static state used to implement the parser is lazily initialized during the first
  // call to the constructor. You can call this function if you wish to initialize the static state
  // ahead of time.
  static void initialize();

private:
};
