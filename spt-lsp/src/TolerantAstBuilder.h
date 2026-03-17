/**
 * @file TolerantAstBuilder.h
 * @brief Tolerant AST Builder - Converts ANTLR CST to Tolerant AST
 *
 * Design Principles:
 * 1. NEVER returns nullptr - always produces Error nodes on failure
 * 2. Gracefully handles incomplete/malformed input for LSP support
 * 3. Propagates error flags through the tree
 * 4. Supports incomplete states for code completion
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

// 启用调试日志 - 调试完成后注释掉这行
#define LSP_DEBUG_ENABLED
#include "LspLogger.h"

#include "AstFactory.h"
#include "LangParserBaseVisitor.h"

#include <any>
#include <charconv>
#include <string>

namespace lang {
namespace ast {

/**
 * @brief Tolerant AST Builder
 *
 * Transforms ANTLR's ParseTree (CST) into a Tolerant AST.
 * All visit methods return AST nodes; nullptr is never returned.
 */
class TolerantAstBuilder : public LangParserBaseVisitor {
public:
  explicit TolerantAstBuilder(AstFactory &factory, std::string_view filename = "<unknown>")
      : factory_(factory), filename_(filename) {}

  /**
   * @brief Build AST from compilation unit
   */
  CompilationUnitNode *build(antlr4::tree::ParseTree *tree) {
    auto result = visit(tree);

    // 首先尝试直接转换为 CompilationUnitNode*
    if (auto *cu = tryCast<CompilationUnitNode>(result)) {
      LSP_LOG("build(): tryCast<CompilationUnitNode> succeeded, cu=" << (void *)cu);
      return cu;
    }

    // visitCompilationUnit 返回的是 Decl*，需要先提取 Decl* 再转换
    if (auto *decl = tryCastDecl(result)) {
      LSP_LOG("build(): tryCastDecl succeeded, decl=" << (void *)decl
                                                      << ", kind=" << astKindToString(decl->kind));
      if (decl->kind == AstKind::CompilationUnit) {
        auto *cu = static_cast<CompilationUnitNode *>(decl);
        LSP_LOG("build(): cast to CompilationUnitNode succeeded, cu="
                << (void *)cu << ", statements.size()=" << cu->statements.size());
        return cu;
      }
    }

    LSP_LOG("build(): all casts failed, returning empty CompilationUnit");
    return factory_.makeCompilationUnit(SourceRange::invalid(), filename_, {});
  }

protected:
  AstFactory &factory_;
  std::string_view filename_;

  // ========================================================================
  // LAYER 1: INFRASTRUCTURE - Source Location Helpers
  // ========================================================================

  [[nodiscard]] SourceLoc getLoc(antlr4::Token *token) const {
    if (!token)
      return SourceLoc::invalid();
    return SourceLoc{static_cast<uint32_t>(token->getLine()),
                     static_cast<uint32_t>(token->getCharPositionInLine() + 1),
                     static_cast<uint32_t>(token->getStartIndex())};
  }

  [[nodiscard]] SourceRange getRange(antlr4::tree::TerminalNode *node) const {
    if (!node)
      return SourceRange::invalid();
    auto *token = node->getSymbol();
    if (!token)
      return SourceRange::invalid();

    SourceLoc begin{static_cast<uint32_t>(token->getLine()),
                    static_cast<uint32_t>(token->getCharPositionInLine() + 1),
                    static_cast<uint32_t>(token->getStartIndex())};
    SourceLoc end{
        static_cast<uint32_t>(token->getLine()),
        static_cast<uint32_t>(token->getCharPositionInLine() + 1 + token->getText().length()),
        static_cast<uint32_t>(token->getStopIndex() + 1)};
    return SourceRange{begin, end};
  }

  [[nodiscard]] SourceRange getRange(antlr4::ParserRuleContext *ctx) const {
    if (!ctx)
      return SourceRange::invalid();

    auto *start = ctx->getStart();
    auto *stop = ctx->getStop();
    if (!start)
      return SourceRange::invalid();

    SourceLoc begin{static_cast<uint32_t>(start->getLine()),
                    static_cast<uint32_t>(start->getCharPositionInLine() + 1),
                    static_cast<uint32_t>(start->getStartIndex())};

    SourceLoc end;
    if (stop) {
      end = SourceLoc{
          static_cast<uint32_t>(stop->getLine()),
          static_cast<uint32_t>(stop->getCharPositionInLine() + 1 + stop->getText().length()),
          static_cast<uint32_t>(stop->getStopIndex() + 1)};
    } else {
      end = SourceLoc{
          static_cast<uint32_t>(start->getLine()),
          static_cast<uint32_t>(start->getCharPositionInLine() + 1 + start->getText().length()),
          static_cast<uint32_t>(start->getStopIndex() + 1)};
    }

    return SourceRange{begin, end};
  }

  // ========================================================================
  // LAYER 1: Safe Type Casting Helpers
  // ========================================================================

  template <typename T> [[nodiscard]] T *tryCast(const std::any &value) const {
    if (!value.has_value())
      return nullptr;
    if (auto *ptr = std::any_cast<T *>(&value))
      return *ptr;
    return nullptr;
  }

  [[nodiscard]] Expr *tryCastExpr(const std::any &value) const {
    if (!value.has_value())
      return nullptr;
    if (auto *p = std::any_cast<Expr *>(&value))
      return *p;

// Try all concrete expression types
#define TRY_EXPR(T)                                                                                \
  if (auto *p = std::any_cast<T *>(&value))                                                        \
    return static_cast<Expr *>(*p);
    TRY_EXPR(ErrorExprNode)
    TRY_EXPR(MissingExprNode) TRY_EXPR(NullLiteralNode) TRY_EXPR(BoolLiteralNode)
        TRY_EXPR(IntLiteralNode) TRY_EXPR(FloatLiteralNode) TRY_EXPR(StringLiteralNode)
            TRY_EXPR(IdentifierNode) TRY_EXPR(QualifiedIdentifierNode)
                TRY_EXPR(MemberAccessExprNode) TRY_EXPR(IndexExprNode) TRY_EXPR(ColonLookupExprNode)
                    TRY_EXPR(BinaryExprNode) TRY_EXPR(UnaryExprNode) TRY_EXPR(CallExprNode)
                        TRY_EXPR(NewExprNode) TRY_EXPR(ListExprNode) TRY_EXPR(MapExprNode)
                            TRY_EXPR(MapEntryNode) TRY_EXPR(LambdaExprNode) TRY_EXPR(ParenExprNode)
                                TRY_EXPR(VarArgsExprNode)
#undef TRY_EXPR
                                    return nullptr;
  }

  [[nodiscard]] Stmt *tryCastStmt(const std::any &value) const {
    if (!value.has_value())
      return nullptr;
    if (auto *p = std::any_cast<Stmt *>(&value))
      return *p;

#define TRY_STMT(T)                                                                                \
  if (auto *p = std::any_cast<T *>(&value))                                                        \
    return static_cast<Stmt *>(*p);
    TRY_STMT(ErrorStmtNode)
    TRY_STMT(EmptyStmtNode) TRY_STMT(ExprStmtNode) TRY_STMT(BlockStmtNode) TRY_STMT(AssignStmtNode)
        TRY_STMT(MultiAssignStmtNode) TRY_STMT(UpdateAssignStmtNode) TRY_STMT(IfStmtNode)
            TRY_STMT(WhileStmtNode) TRY_STMT(ForStmtNode) TRY_STMT(BreakStmtNode)
                TRY_STMT(ContinueStmtNode) TRY_STMT(ReturnStmtNode) TRY_STMT(DeferStmtNode)
                    TRY_STMT(ImportStmtNode) TRY_STMT(DeclStmtNode)
#undef TRY_STMT
                        return nullptr;
  }

  [[nodiscard]] Decl *tryCastDecl(const std::any &value) const {
    if (!value.has_value())
      return nullptr;
    if (auto *p = std::any_cast<Decl *>(&value))
      return *p;

#define TRY_DECL(T)                                                                                \
  if (auto *p = std::any_cast<T *>(&value))                                                        \
    return static_cast<Decl *>(*p);
    TRY_DECL(ErrorDeclNode)
    TRY_DECL(VarDeclNode) TRY_DECL(MultiVarDeclNode) TRY_DECL(ParameterDeclNode)
        TRY_DECL(FunctionDeclNode) TRY_DECL(FieldDeclNode) TRY_DECL(MethodDeclNode)
            TRY_DECL(ClassDeclNode) TRY_DECL(CompilationUnitNode)
#undef TRY_DECL
                return nullptr;
  }

  [[nodiscard]] TypeNode *tryCastType(const std::any &value) const {
    if (!value.has_value())
      return nullptr;
    if (auto *p = std::any_cast<TypeNode *>(&value))
      return *p;

#define TRY_TYPE(T)                                                                                \
  if (auto *p = std::any_cast<T *>(&value))                                                        \
    return static_cast<TypeNode *>(*p);
    TRY_TYPE(ErrorTypeNode)
    TRY_TYPE(InferredTypeNode) TRY_TYPE(PrimitiveTypeNode) TRY_TYPE(AnyTypeNode)
        TRY_TYPE(ListTypeNode) TRY_TYPE(MapTypeNode) TRY_TYPE(QualifiedTypeNode)
            TRY_TYPE(MultiReturnTypeNode)
#undef TRY_TYPE
                return nullptr;
  }

  // ========================================================================
  // LAYER 1: Tolerant Expect Helpers (Core Error Recovery)
  // ========================================================================

  /// Expect expression - NEVER returns nullptr
  [[nodiscard]] Expr *expectExpr(const std::any &value, antlr4::ParserRuleContext *ctx,
                                 std::string_view msg = "expected expression") {
    if (auto *e = tryCastExpr(value))
      return e;
    return factory_.makeErrorExpr(getRange(ctx), msg);
  }

  [[nodiscard]] Expr *expectExpr(antlr4::ParserRuleContext *ctx,
                                 std::string_view msg = "expected expression") {
    if (!ctx)
      return factory_.makeErrorExpr(SourceRange::invalid(), msg);
    return expectExpr(visit(ctx), ctx, msg);
  }

  /// Expect statement - NEVER returns nullptr
  [[nodiscard]] Stmt *expectStmt(const std::any &value, antlr4::ParserRuleContext *ctx,
                                 std::string_view msg = "expected statement") {
    if (auto *s = tryCastStmt(value))
      return s;
    return factory_.makeErrorStmt(getRange(ctx), msg);
  }

  [[nodiscard]] Stmt *expectStmt(antlr4::ParserRuleContext *ctx,
                                 std::string_view msg = "expected statement") {
    if (!ctx)
      return factory_.makeErrorStmt(SourceRange::invalid(), msg);
    return expectStmt(visit(ctx), ctx, msg);
  }

  /// Expect type - NEVER returns nullptr
  [[nodiscard]] TypeNode *expectType(const std::any &value, antlr4::ParserRuleContext *ctx,
                                     std::string_view msg = "expected type") {
    if (auto *t = tryCastType(value))
      return t;
    return factory_.makeErrorType(getRange(ctx), msg);
  }

  [[nodiscard]] TypeNode *expectType(antlr4::ParserRuleContext *ctx,
                                     std::string_view msg = "expected type") {
    if (!ctx)
      return factory_.makeErrorType(SourceRange::invalid(), msg);
    return expectType(visit(ctx), ctx, msg);
  }

  /// Expect var decl - NEVER returns nullptr
  [[nodiscard]] VarDeclNode *expectVarDecl(antlr4::ParserRuleContext *ctx,
                                           std::string_view msg = "expected variable declaration") {
    if (!ctx)
      return factory_.makeVarDecl(SourceRange::invalid(), "",
                                  factory_.makeErrorType(SourceRange::invalid(), msg), nullptr);
    auto value = visit(ctx);
    if (auto *vd = tryCast<VarDeclNode>(value))
      return vd;
    return factory_.makeVarDecl(getRange(ctx), "",
                                factory_.makeErrorType(getRange(ctx), msg), nullptr);
  }

  /// Expect block - NEVER returns nullptr
  [[nodiscard]] BlockStmtNode *expectBlock(antlr4::ParserRuleContext *ctx) {
    if (!ctx) {
      auto *b = factory_.makeBlockStmt(SourceRange::invalid(), {});
      b->flags = b->flags | NodeFlags::HasError;
      return b;
    }
    auto visited = visit(ctx);
    if (auto *b = tryCast<BlockStmtNode>(visited))
      return b;
    auto *b = factory_.makeBlockStmt(getRange(ctx), {});
    b->flags = b->flags | NodeFlags::HasError;
    return b;
  }

  // ========================================================================
  // LAYER 1: List Conversion Helpers
  // ========================================================================

  template <typename CtxType>
  [[nodiscard]] std::vector<Expr *> visitExprList(const std::vector<CtxType *> &ctxs) {
    std::vector<Expr *> result;
    result.reserve(ctxs.size());
    for (auto *ctx : ctxs)
      result.push_back(expectExpr(ctx));
    return result;
  }

  template <typename CtxType>
  [[nodiscard]] std::vector<Stmt *> visitStmtList(const std::vector<CtxType *> &ctxs) {
    std::vector<Stmt *> result;
    result.reserve(ctxs.size());
    for (auto *ctx : ctxs)
      result.push_back(expectStmt(ctx));
    return result;
  }

  [[nodiscard]] std::vector<ParameterDeclNode *>
  visitParamList(const std::vector<LangParser::ParameterContext *> &ctxs) {
    std::vector<ParameterDeclNode *> result;
    result.reserve(ctxs.size());
    for (auto *ctx : ctxs) {
      auto v = visit(ctx);
      if (auto *p = tryCast<ParameterDeclNode>(v)) {
        result.push_back(p);
      } else {
        result.push_back(factory_.makeParameterDecl(
            getRange(ctx), "", factory_.makeErrorType(getRange(ctx), "invalid parameter")));
      }
    }
    return result;
  }

  // ========================================================================
  // LAYER 1: Operator Mapping
  // ========================================================================

  [[nodiscard]] BinaryOp mapBinaryOp(size_t tokenType) const {
    switch (tokenType) {
    case LangLexer::ADD:
      return BinaryOp::Add;
    case LangLexer::SUB:
      return BinaryOp::Sub;
    case LangLexer::MUL:
      return BinaryOp::Mul;
    case LangLexer::DIV:
      return BinaryOp::Div;
    case LangLexer::IDIV:
      return BinaryOp::IDiv;
    case LangLexer::MOD:
      return BinaryOp::Mod;
    case LangLexer::EQ:
      return BinaryOp::Eq;
    case LangLexer::NEQ:
      return BinaryOp::Neq;
    case LangLexer::LT:
      return BinaryOp::Lt;
    case LangLexer::GT:
      return BinaryOp::Gt;
    case LangLexer::LTE:
      return BinaryOp::Lte;
    case LangLexer::GTE:
      return BinaryOp::Gte;
    case LangLexer::AND:
      return BinaryOp::And;
    case LangLexer::OR:
      return BinaryOp::Or;
    case LangLexer::BIT_AND:
      return BinaryOp::BitAnd;
    case LangLexer::BIT_OR:
      return BinaryOp::BitOr;
    case LangLexer::BIT_XOR:
      return BinaryOp::BitXor;
    case LangLexer::LSHIFT:
      return BinaryOp::LShift;
    case LangLexer::CONCAT:
      return BinaryOp::Concat;
    default:
      return BinaryOp::Invalid;
    }
  }

  [[nodiscard]] UnaryOp mapUnaryOp(size_t tokenType) const {
    switch (tokenType) {
    case LangLexer::SUB:
      return UnaryOp::Neg;
    case LangLexer::NOT:
      return UnaryOp::Not;
    case LangLexer::BIT_NOT:
      return UnaryOp::BitNot;
    case LangLexer::LEN:
      return UnaryOp::Len;
    default:
      return UnaryOp::Invalid;
    }
  }

  [[nodiscard]] UpdateOp mapUpdateOp(size_t tokenType) const {
    switch (tokenType) {
    case LangLexer::ADD_ASSIGN:
      return UpdateOp::AddAssign;
    case LangLexer::SUB_ASSIGN:
      return UpdateOp::SubAssign;
    case LangLexer::MUL_ASSIGN:
      return UpdateOp::MulAssign;
    case LangLexer::DIV_ASSIGN:
      return UpdateOp::DivAssign;
    case LangLexer::IDIV_ASSIGN:
      return UpdateOp::IDivAssign;
    case LangLexer::MOD_ASSIGN:
      return UpdateOp::ModAssign;
    case LangLexer::CONCAT_ASSIGN:
      return UpdateOp::ConcatAssign;
    default:
      return UpdateOp::Invalid;
    }
  }

  [[nodiscard]] PrimitiveKind mapPrimitiveKind(size_t tokenType) const {
    switch (tokenType) {
    case LangLexer::INT:
      return PrimitiveKind::Int;
    case LangLexer::FLOAT:
      return PrimitiveKind::Float;
    case LangLexer::NUMBER:
      return PrimitiveKind::Number;
    case LangLexer::STRING:
      return PrimitiveKind::String;
    case LangLexer::BOOL:
      return PrimitiveKind::Bool;
    case LangLexer::VOID:
      return PrimitiveKind::Void;
    case LangLexer::NULL_:
      return PrimitiveKind::Null;
    case LangLexer::COROUTINE:
      return PrimitiveKind::Coroutine;
    case LangLexer::FUNCTION:
      return PrimitiveKind::Function;
    default:
      return PrimitiveKind::Invalid;
    }
  }

  // ========================================================================
  // LAYER 1: Parsing Helpers
  // ========================================================================

  [[nodiscard]] std::pair<int64_t, bool> parseInteger(std::string_view text) const {
    int64_t value = 0;
    bool isHex = false;

    if (text.size() >= 2 && (text[1] == 'x' || text[1] == 'X')) {
      isHex = true;
      text = text.substr(2);
      std::from_chars(text.data(), text.data() + text.size(), value, 16);
    } else {
      std::from_chars(text.data(), text.data() + text.size(), value, 10);
    }
    return {value, isHex};
  }

  [[nodiscard]] double parseFloat(std::string_view text) const {
    try {
      return std::stod(std::string(text));
    } catch (...) {
      return 0.0;
    }
  }

  [[nodiscard]] std::string parseString(std::string_view text) const {
    if (text.size() >= 2)
      text = text.substr(1, text.size() - 2);

    std::string result;
    result.reserve(text.size());

    for (size_t i = 0; i < text.size(); ++i) {
      if (text[i] == '\\' && i + 1 < text.size()) {
        ++i;
        switch (text[i]) {
        case 'n':
          result += '\n';
          break;
        case 't':
          result += '\t';
          break;
        case 'r':
          result += '\r';
          break;
        case 'b':
          result += '\b';
          break;
        case 'f':
          result += '\f';
          break;
        case '"':
          result += '"';
          break;
        case '\'':
          result += '\'';
          break;
        case '\\':
          result += '\\';
          break;
        case 'x':
          if (i + 2 < text.size()) {
            int val = 0;
            std::from_chars(text.data() + i + 1, text.data() + i + 3, val, 16);
            result += static_cast<char>(val);
            i += 2;
          }
          break;
        default:
          result += text[i];
          break;
        }
      } else {
        result += text[i];
      }
    }
    return result;
  }

  [[nodiscard]] NodeFlags collectModifiers(antlr4::tree::TerminalNode *global,
                                           antlr4::tree::TerminalNode *cnst,
                                           antlr4::tree::TerminalNode *statc = nullptr,
                                           antlr4::tree::TerminalNode *exprt = nullptr) const {
    NodeFlags f = NodeFlags::None;
    if (global)
      f = f | NodeFlags::IsGlobal;
    if (cnst)
      f = f | NodeFlags::IsConst;
    if (statc)
      f = f | NodeFlags::IsStatic;
    if (exprt)
      f = f | NodeFlags::IsExport;
    return f;
  }

  // ========================================================================
  // LAYER 2: LEAF NODES - Atoms & Literals
  // ========================================================================

  std::any visitAtomexp(LangParser::AtomexpContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing atom"));
    auto range = getRange(ctx);

    if (ctx->NULL_())
      return static_cast<Expr *>(factory_.makeNullLiteral(range));
    if (ctx->TRUE())
      return static_cast<Expr *>(factory_.makeBoolLiteral(range, true));
    if (ctx->FALSE())
      return static_cast<Expr *>(factory_.makeBoolLiteral(range, false));
    if (ctx->INTEGER()) {
      auto [v, h] = parseInteger(ctx->INTEGER()->getText());
      return static_cast<Expr *>(factory_.makeIntLiteral(range, v, h));
    }
    if (ctx->FLOAT_LITERAL()) {
      return static_cast<Expr *>(
          factory_.makeFloatLiteral(range, parseFloat(ctx->FLOAT_LITERAL()->getText())));
    }
    if (ctx->STRING_LITERAL()) {
      std::string raw = ctx->STRING_LITERAL()->getText();
      return static_cast<Expr *>(factory_.makeStringLiteral(range, parseString(raw), raw));
    }
    return static_cast<Expr *>(factory_.makeErrorExpr(range, "unknown atom"));
  }

  // ========================================================================
  // LAYER 2: Primary Expressions
  // ========================================================================

  std::any visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *ctx) override {
    if (!ctx || !ctx->IDENTIFIER())
      return static_cast<Expr *>(factory_.makeErrorExpr(getRange(ctx), "missing identifier"));
    return static_cast<Expr *>(
        factory_.makeIdentifier(getRange(ctx), ctx->IDENTIFIER()->getText()));
  }

  std::any visitPrimaryAtom(LangParser::PrimaryAtomContext *ctx) override {
    if (!ctx || !ctx->atomexp())
      return static_cast<Expr *>(factory_.makeErrorExpr(getRange(ctx), "missing atom"));
    return visit(ctx->atomexp());
  }

  std::any visitPrimaryParenExp(LangParser::PrimaryParenExpContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing paren"));
    Expr *inner = expectExpr(ctx->expression());
    return static_cast<Expr *>(factory_.makeParenExpr(getRange(ctx), inner));
  }

  std::any visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *ctx) override {
    return static_cast<Expr *>(factory_.makeVarArgsExpr(getRange(ctx)));
  }

  std::any visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *ctx) override {
    if (!ctx || !ctx->listExpression())
      return static_cast<Expr *>(factory_.makeListExpr(getRange(ctx), {}));
    return visit(ctx->listExpression());
  }

  std::any visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *ctx) override {
    if (!ctx || !ctx->mapExpression())
      return static_cast<Expr *>(factory_.makeMapExpr(getRange(ctx), {}));
    return visit(ctx->mapExpression());
  }

  std::any visitListLiteralDef(LangParser::ListLiteralDefContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeListExpr(SourceRange::invalid(), {}));
    std::vector<Expr *> elems;
    if (ctx->expressionList()) {
      for (auto *e : ctx->expressionList()->expression())
        elems.push_back(expectExpr(e));
    }
    return static_cast<Expr *>(factory_.makeListExpr(getRange(ctx), elems));
  }

  std::any visitMapLiteralDef(LangParser::MapLiteralDefContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeMapExpr(SourceRange::invalid(), {}));
    std::vector<MapEntryNode *> entries;
    if (ctx->mapEntryList()) {
      for (auto *e : ctx->mapEntryList()->mapEntry()) {
        auto v = visit(e);
        if (auto *m = tryCast<MapEntryNode>(v))
          entries.push_back(m);
      }
    }
    return static_cast<Expr *>(factory_.makeMapExpr(getRange(ctx), entries));
  }

  std::any visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *ctx) override {
    if (!ctx)
      return static_cast<MapEntryNode *>(factory_.makeMapEntry(
          SourceRange::invalid(), factory_.makeErrorExpr(SourceRange::invalid(), "invalid key"),
          factory_.makeErrorExpr(SourceRange::invalid(), "invalid value")));
    auto range = getRange(ctx);
    Expr *key = ctx->IDENTIFIER()
                    ? static_cast<Expr *>(factory_.makeStringLiteral(getRange(ctx->IDENTIFIER()),
                                                                     ctx->IDENTIFIER()->getText()))
                    : static_cast<Expr *>(factory_.makeErrorExpr(range, "missing key"));
    Expr *val = expectExpr(ctx->expression());
    return static_cast<MapEntryNode *>(factory_.makeMapEntry(range, key, val, true));
  }

  std::any visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *ctx) override {
    if (!ctx)
      return static_cast<MapEntryNode *>(factory_.makeMapEntry(
          SourceRange::invalid(), factory_.makeErrorExpr(SourceRange::invalid(), "invalid key"),
          factory_.makeErrorExpr(SourceRange::invalid(), "invalid value")));
    auto range = getRange(ctx);
    auto exprs = ctx->expression();
    Expr *key =
        exprs.size() > 0 ? expectExpr(exprs[0]) : factory_.makeErrorExpr(range, "missing key");
    Expr *val =
        exprs.size() > 1 ? expectExpr(exprs[1]) : factory_.makeErrorExpr(range, "missing value");
    return static_cast<MapEntryNode *>(factory_.makeMapEntry(range, key, val, false, true));
  }

  std::any visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *ctx) override {
    if (!ctx)
      return static_cast<MapEntryNode *>(factory_.makeMapEntry(
          SourceRange::invalid(), factory_.makeErrorExpr(SourceRange::invalid(), "invalid key"),
          factory_.makeErrorExpr(SourceRange::invalid(), "invalid value")));
    auto range = getRange(ctx);
    Expr *key =
        ctx->STRING_LITERAL()
            ? static_cast<Expr *>(factory_.makeStringLiteral(
                  getRange(ctx->STRING_LITERAL()), parseString(ctx->STRING_LITERAL()->getText()),
                  ctx->STRING_LITERAL()->getText()))
            : static_cast<Expr *>(factory_.makeErrorExpr(range, "missing key"));
    Expr *val = expectExpr(ctx->expression());
    return static_cast<MapEntryNode *>(factory_.makeMapEntry(range, key, val));
  }

  std::any visitMapEntryIntKey(LangParser::MapEntryIntKeyContext *ctx) override {
    if (!ctx)
      return static_cast<MapEntryNode *>(factory_.makeMapEntry(
          SourceRange::invalid(), factory_.makeErrorExpr(SourceRange::invalid(), "invalid key"),
          factory_.makeErrorExpr(SourceRange::invalid(), "invalid value")));
    auto range = getRange(ctx);
    Expr *key = ctx->INTEGER()
                    ? static_cast<Expr *>(factory_.makeIntLiteral(getRange(ctx->INTEGER()),
                                                                   std::stoll(ctx->INTEGER()->getText())))
                    : static_cast<Expr *>(factory_.makeErrorExpr(range, "missing key"));
    Expr *val = expectExpr(ctx->expression());
    return static_cast<MapEntryNode *>(factory_.makeMapEntry(range, key, val));
  }

  std::any visitMapEntryFloatKey(LangParser::MapEntryFloatKeyContext *ctx) override {
    if (!ctx)
      return static_cast<MapEntryNode *>(factory_.makeMapEntry(
          SourceRange::invalid(), factory_.makeErrorExpr(SourceRange::invalid(), "invalid key"),
          factory_.makeErrorExpr(SourceRange::invalid(), "invalid value")));
    auto range = getRange(ctx);
    Expr *key = ctx->FLOAT_LITERAL()
                    ? static_cast<Expr *>(factory_.makeFloatLiteral(getRange(ctx->FLOAT_LITERAL()),
                                                                     std::stod(ctx->FLOAT_LITERAL()->getText())))
                    : static_cast<Expr *>(factory_.makeErrorExpr(range, "missing key"));
    Expr *val = expectExpr(ctx->expression());
    return static_cast<MapEntryNode *>(factory_.makeMapEntry(range, key, val));
  }

  std::any visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *ctx) override {
    if (!ctx)
      return static_cast<QualifiedIdentifierNode *>(
          factory_.makeQualifiedIdentifier(SourceRange::invalid(), {""}));

    // 使用 std::string 避免悬空引用（getText() 返回临时 string）
    std::vector<std::string> partsStorage;
    for (auto *id : ctx->IDENTIFIER()) {
      std::string text = id->getText();
      LSP_LOG("visitQualifiedIdentifier: adding part '" << text << "'");
      partsStorage.push_back(text);
    }

    // 转换为 string_view（现在安全了，因为 partsStorage 在作用域内）
    std::vector<std::string_view> parts;
    for (const auto &s : partsStorage)
      parts.push_back(s);

    // Check for incomplete state: "MyClass." where trailing identifier is missing
    // This happens when DOT tokens exist but fewer IDENTIFIERs than expected
    bool isIncomplete = false;
    auto dots = ctx->DOT();
    if (!dots.empty() && parts.size() <= dots.size()) {
      // More dots than identifiers after first = incomplete (e.g., "a.b." has 2 dots, should have 3
      // ids)
      isIncomplete = true;
      // Add empty placeholder for completion context
      parts.push_back("");
    }

    if (parts.empty()) {
      parts.push_back("");
      isIncomplete = true;
    }

    auto *node = factory_.makeQualifiedIdentifier(getRange(ctx), parts);
    LSP_LOG("visitQualifiedIdentifier: created node with " << node->parts.size() << " parts");
    for (size_t i = 0; i < node->parts.size(); ++i) {
      LSP_LOG("  part[" << i << "].id=" << node->parts[i].id << ", value='"
                        << factory_.strings().get(node->parts[i]) << "'");
    }
    if (isIncomplete) {
      node->flags = node->flags | NodeFlags::Incomplete;
    }
    // 返回 QualifiedIdentifierNode* 以便 tryCast<QualifiedIdentifierNode> 能正确工作
    return static_cast<QualifiedIdentifierNode *>(node);
  }

  // ========================================================================
  // LAYER 3: EXPRESSIONS - Binary Operations (Generic Pattern)
  // ========================================================================

  /// Generic left-associative binary expression builder
  template <typename ChildCtx>
  Expr *buildLeftAssocBinaryExpr(antlr4::ParserRuleContext *ctx,
                                 const std::vector<ChildCtx *> &children,
                                 const std::vector<BinaryOp> &ops) {
    if (children.empty())
      return factory_.makeErrorExpr(getRange(ctx), "missing operands");

    Expr *result = expectExpr(children[0]);
    for (size_t i = 1; i < children.size(); ++i) {
      BinaryOp op = (i - 1 < ops.size()) ? ops[i - 1] : BinaryOp::Invalid;
      Expr *right = expectExpr(children[i]);
      auto range = SourceRange{result->range.begin, right->range.end};
      result = factory_.makeBinaryExpr(range, op, result, right);
    }
    return result;
  }

  // logicalOrExpression
  std::any visitLogicalOrExpression(LangParser::LogicalOrExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops(ctx->OR().size(), BinaryOp::Or);
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->logicalAndExp(), ops));
  }

  // logicalAndExpression
  std::any visitLogicalAndExpression(LangParser::LogicalAndExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops(ctx->AND().size(), BinaryOp::And);
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->bitwiseOrExp(), ops));
  }

  // bitwiseOrExpression
  std::any visitBitwiseOrExpression(LangParser::BitwiseOrExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops(ctx->BIT_OR().size(), BinaryOp::BitOr);
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->bitwiseXorExp(), ops));
  }

  // bitwiseXorExpression
  std::any visitBitwiseXorExpression(LangParser::BitwiseXorExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops(ctx->BIT_XOR().size(), BinaryOp::BitXor);
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->bitwiseAndExp(), ops));
  }

  // bitwiseAndExpression
  std::any visitBitwiseAndExpression(LangParser::BitwiseAndExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops(ctx->BIT_AND().size(), BinaryOp::BitAnd);
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->equalityExp(), ops));
  }

  // equalityExpression
  std::any visitEqualityExpression(LangParser::EqualityExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops;
    for (auto *opCtx : ctx->equalityExpOp()) {
      if (opCtx->EQ())
        ops.push_back(BinaryOp::Eq);
      else if (opCtx->NEQ())
        ops.push_back(BinaryOp::Neq);
      else
        ops.push_back(BinaryOp::Invalid);
    }
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->comparisonExp(), ops));
  }

  // comparisonExpression
  std::any visitComparisonExpression(LangParser::ComparisonExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops;
    for (auto *opCtx : ctx->comparisonExpOp()) {
      if (opCtx->LT())
        ops.push_back(BinaryOp::Lt);
      else if (opCtx->GT())
        ops.push_back(BinaryOp::Gt);
      else if (opCtx->LTE())
        ops.push_back(BinaryOp::Lte);
      else if (opCtx->GTE())
        ops.push_back(BinaryOp::Gte);
      else
        ops.push_back(BinaryOp::Invalid);
    }
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->shiftExp(), ops));
  }

  // shiftExpression (handles >> as GT GT)
  std::any visitShiftExpression(LangParser::ShiftExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops;
    for (auto *opCtx : ctx->shiftExpOp()) {
      if (opCtx->LSHIFT())
        ops.push_back(BinaryOp::LShift);
      else if (opCtx->GT().size() >= 2)
        ops.push_back(BinaryOp::RShift);
      else
        ops.push_back(BinaryOp::Invalid);
    }
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->concatExp(), ops));
  }

  // concatExpression
  std::any visitConcatExpression(LangParser::ConcatExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops(ctx->CONCAT().size(), BinaryOp::Concat);
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->addSubExp(), ops));
  }

  // addSubExpression
  std::any visitAddSubExpression(LangParser::AddSubExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops;
    for (auto *opCtx : ctx->addSubExpOp()) {
      if (opCtx->ADD())
        ops.push_back(BinaryOp::Add);
      else if (opCtx->SUB())
        ops.push_back(BinaryOp::Sub);
      else
        ops.push_back(BinaryOp::Invalid);
    }
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->mulDivModExp(), ops));
  }

  // mulDivModExpression
  std::any visitMulDivModExpression(LangParser::MulDivModExpressionContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));
    std::vector<BinaryOp> ops;
    for (auto *opCtx : ctx->mulDivModExpOp()) {
      if (opCtx->MUL())
        ops.push_back(BinaryOp::Mul);
      else if (opCtx->DIV())
        ops.push_back(BinaryOp::Div);
      else if (opCtx->IDIV())
        ops.push_back(BinaryOp::IDiv);
      else if (opCtx->MOD())
        ops.push_back(BinaryOp::Mod);
      else
        ops.push_back(BinaryOp::Invalid);
    }
    return static_cast<Expr *>(buildLeftAssocBinaryExpr(ctx, ctx->unaryExp(), ops));
  }

  // ========================================================================
  // LAYER 3: Unary Expressions
  // ========================================================================

  std::any visitUnaryPrefix(LangParser::UnaryPrefixContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "missing expr"));

    UnaryOp op = UnaryOp::Invalid;
    if (ctx->NOT())
      op = UnaryOp::Not;
    else if (ctx->SUB())
      op = UnaryOp::Neg;
    else if (ctx->LEN())
      op = UnaryOp::Len;
    else if (ctx->BIT_NOT())
      op = UnaryOp::BitNot;

    Expr *operand = expectExpr(ctx->unaryExp());
    return static_cast<Expr *>(factory_.makeUnaryExpr(getRange(ctx), op, operand));
  }

  std::any visitUnaryToPostfix(LangParser::UnaryToPostfixContext *ctx) override {
    if (!ctx || !ctx->postfixExp())
      return static_cast<Expr *>(factory_.makeErrorExpr(getRange(ctx), "missing expr"));
    return visit(ctx->postfixExp());
  }

  // ========================================================================
  // LAYER 3: Postfix Expressions (CRITICAL FOR LSP COMPLETION)
  // ========================================================================

  std::any visitPostfixExpression(LangParser::PostfixExpressionContext *ctx) override {
    if (!ctx || !ctx->primaryExp())
      return static_cast<Expr *>(factory_.makeErrorExpr(getRange(ctx), "missing expr"));

    Expr *result = expectExpr(ctx->primaryExp());

    for (auto *suffix : ctx->postfixSuffix()) {
      result = applyPostfixSuffix(result, suffix);
    }

    return static_cast<Expr *>(result);
  }

  Expr *applyPostfixSuffix(Expr *base, LangParser::PostfixSuffixContext *suffix) {
    if (!suffix)
      return base;

    // Member access: expr.member (CRITICAL FOR COMPLETION)
    if (auto *m = dynamic_cast<LangParser::PostfixMemberSuffixContext *>(suffix)) {
      auto range = SourceRange{base->range.begin, getRange(m).end};
      // 使用 std::string 避免悬空引用
      std::string member = "";
      bool incomplete = false;

      if (m->IDENTIFIER()) {
        member = m->IDENTIFIER()->getText();
      } else {
        // No identifier after dot -> incomplete for completion!
        incomplete = true;
      }
      return factory_.makeMemberAccessExpr(range, base, member, incomplete);
    }

    // Index: expr[index]
    if (auto *i = dynamic_cast<LangParser::PostfixIndexSuffixContext *>(suffix)) {
      auto range = SourceRange{base->range.begin, getRange(i).end};
      Expr *index = expectExpr(i->expression());
      return factory_.makeIndexExpr(range, base, index);
    }

    // Call: expr(args)
    if (auto *c = dynamic_cast<LangParser::PostfixCallSuffixContext *>(suffix)) {
      auto range = SourceRange{base->range.begin, getRange(c).end};
      std::vector<Expr *> args;
      if (c->arguments() && c->arguments()->expressionList()) {
        for (auto *a : c->arguments()->expressionList()->expression())
          args.push_back(expectExpr(a));
      }
      return factory_.makeCallExpr(range, base, args, getRange(c));
    }

    return base;
  }

  // New expression
  std::any visitNewExpressionDef(LangParser::NewExpressionDefContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "invalid new"));

    auto range = getRange(ctx);
    QualifiedIdentifierNode *typeName = nullptr;
    if (ctx->qualifiedIdentifier()) {
      auto v = visit(ctx->qualifiedIdentifier());
      typeName = tryCast<QualifiedIdentifierNode>(v);
    }
    if (!typeName)
      typeName = factory_.makeQualifiedIdentifier(range, {""});

    std::vector<Expr *> args;
    if (ctx->arguments() && ctx->arguments()->expressionList()) {
      for (auto *a : ctx->arguments()->expressionList()->expression())
        args.push_back(expectExpr(a));
    }
    return static_cast<Expr *>(factory_.makeNewExpr(range, typeName, args));
  }

  std::any visitPrimaryNew(LangParser::PrimaryNewContext *ctx) override {
    if (!ctx || !ctx->newExp())
      return static_cast<Expr *>(factory_.makeErrorExpr(getRange(ctx), "invalid new"));
    return visit(ctx->newExp());
  }

  // Lambda expression
  std::any visitLambdaExprDef(LangParser::LambdaExprDefContext *ctx) override {
    if (!ctx)
      return static_cast<Expr *>(factory_.makeErrorExpr(SourceRange::invalid(), "invalid lambda"));

    auto range = getRange(ctx);
    std::vector<ParameterDeclNode *> params;
    if (ctx->parameterList())
      params = visitParamList(ctx->parameterList()->parameter());

    TypeNode *retType = nullptr;
    bool isMulti = false;
    if (ctx->VARS()) {
      isMulti = true;
      retType = factory_.makeMultiReturnType(getRange(ctx->VARS()));
    } else if (ctx->type()) {
      retType = expectType(ctx->type());
    } else {
      retType = factory_.makeErrorType(range, "missing return type");
    }

    BlockStmtNode *body = expectBlock(ctx->blockStatement());
    return static_cast<Expr *>(factory_.makeLambdaExpr(range, retType, params, body, isMulti));
  }

  std::any visitPrimaryLambda(LangParser::PrimaryLambdaContext *ctx) override {
    if (!ctx || !ctx->lambdaExpression())
      return static_cast<Expr *>(factory_.makeErrorExpr(getRange(ctx), "invalid lambda"));
    return visit(ctx->lambdaExpression());
  }

// Include the implementation file for Layer 4 & 5
#include "TolerantAstBuilderImpl.h"
};

} // namespace ast
} // namespace lang
