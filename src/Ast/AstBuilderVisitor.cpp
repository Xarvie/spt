#include "AstBuilderVisitor.h"
#include "LangParser.h"
#include "ast.h"

#include <stdexcept>
#include <vector>

#include <optional>
#include <utility>

AstBuilderVisitor::AstBuilderVisitor(const std::string &filename) : current_filename(filename) {}

SourceLocation AstBuilderVisitor::getSourceLocation(antlr4::ParserRuleContext *ctx) {

  if (!ctx) {
    return {current_filename, 0, 0};
  }
  antlr4::Token *startToken = ctx->getStart();
  if (!startToken)
    throw std::runtime_error("AstBuilderVisitor::getSourceLocation(ctx)nullptr");
  return {current_filename, (int)startToken->getLine(),
          (int)startToken->getCharPositionInLine() + 1};
}

SourceLocation AstBuilderVisitor::getSourceLocation(antlr4::tree::TerminalNode *tn) {

  if (!tn) {
    return {current_filename, 0, 0};
  }
  antlr4::Token *token = tn->getSymbol();
  if (!token)
    throw std::runtime_error("AstBuilderVisitor::getSourceLocation(tn)nullptr");
  return {current_filename, (int)token->getLine(), (int)token->getCharPositionInLine() + 1};
}

std::string AstBuilderVisitor::processStringLiteral(const std::string &text) {

  if (text.length() < 2) {
    return "";
  }
  char quote_char = text[0];
  std::string content = text.substr(1, text.length() - 2);
  std::string result = "";
  result.reserve(content.length());
  for (size_t i = 0; i < content.length(); ++i) {
    if (content[i] == '\\' && i + 1 < content.length()) {
      ++i;
      switch (content[i]) {
      case 'n':
        result += '\n';
        break;
      case 't':
        result += '\t';
        break;
      case '\\':
        result += '\\';
        break;
      case '\'':
        result += '\'';
        break;
      case '"':
        result += '"';
        break;
      default:
        result += '\\';
        result += content[i];
        break;
      }
    } else {
      result += content[i];
    }
  }
  return result;
}

std::vector<Declaration *> AstBuilderVisitor::safeAnyCastDeclVector(std::any &anyValue,
                                                                    const char *ruleName) {

  if (!ruleName)
    throw std::runtime_error("AstBuilderVisitor::safeAnyCastDeclVectornullptr");
  if (!anyValue.has_value()) {
    return {};
  }
  try {

    return std::any_cast<std::vector<Declaration *>>(anyValue);
  } catch (const std::bad_any_cast &e) {
    std::string errorMsg = "类型转换错误: 在处理 '";
    errorMsg += ruleName;
    errorMsg += "' 时无法将 std::any 转换为 std::vector<Declaration*>";
    throw std::runtime_error(errorMsg);
  }
}

std::vector<Expression *> AstBuilderVisitor::safeAnyCastExprVector(std::any &anyValue,
                                                                   const char *ruleName) {

  if (!ruleName)
    throw std::runtime_error("AstBuilderVisitor::safeAnyCastExprVectornullptr");
  if (!anyValue.has_value()) {
    return {};
  }
  try {

    return std::any_cast<std::vector<Expression *>>(anyValue);
  } catch (const std::bad_any_cast &e) {
    std::string errorMsg = "类型转换错误: 在处理 '";
    errorMsg += ruleName;
    errorMsg += "' 时无法将 std::any 转换为 std::vector<Expression*>";
    throw std::runtime_error(errorMsg);
  }
}

std::any AstBuilderVisitor::visitBinaryExpressionRawPtr(
    antlr4::ParserRuleContext *ctx, std::function<size_t()> getChildCount,
    std::function<antlr4::ParserRuleContext *(size_t)> getChild,
    std::function<antlr4::tree::TerminalNode *(size_t, bool &isToRShift)> getOperator) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBinaryExpressionRawPtrnullptr");

  antlr4::ParserRuleContext *child0 = getChild(0);
  if (!child0)
    throw std::runtime_error("AstBuilderVisitor::visitBinaryExpressionRawPtrnullptr");
  std::any leftAny = visit(child0);

  AstNode *leftNodeRaw =
      safeAnyCastRawPtr<AstNode>(leftAny, "visitBinaryExpression > left operand");

  Expression *left = dynamic_cast<Expression *>(leftNodeRaw);
  if (!left && leftAny.has_value()) {
    delete leftNodeRaw;
    throw std::runtime_error("二元操作符左侧必须是表达式");
  }
  if (!left)
    return std::any();

  size_t count = getChildCount();
  for (size_t i = 1; i < count; ++i) {

    bool isRShift = false;
    antlr4::tree::TerminalNode *opNode = getOperator(i - 1, isRShift);
    if (!opNode) {

      continue;
    }
    SourceLocation opLoc = getSourceLocation(opNode);
    OperatorKind opKind;
    antlr4::Token *opToken = opNode->getSymbol();
    if (!opToken)
      throw std::runtime_error("AstBuilderVisitor::visitBinaryExpressionRawPtrnullptr");

    int tokenType = opToken->getType();

    switch (tokenType) {
    case LangParser::OR:
      opKind = OperatorKind::OR;
      break;
    case LangParser::AND:
      opKind = OperatorKind::AND;
      break;
    case LangParser::BIT_OR:
      opKind = OperatorKind::BW_OR;
      break;
    case LangParser::BIT_XOR:
      opKind = OperatorKind::BW_XOR;
      break;
    case LangParser::BIT_AND:
      opKind = OperatorKind::BW_AND;
      break;
    case LangParser::EQ:
      opKind = OperatorKind::EQ;
      break;
    case LangParser::NEQ:
      opKind = OperatorKind::NE;
      break;
    case LangParser::LT:
      opKind = OperatorKind::LT;
      break;
    case LangParser::GT: {
      if (isRShift) {
        opKind = OperatorKind::BW_RSHIFT;
        break;
      }
      opKind = OperatorKind::GT;
      break;
    }
    case LangParser::LTE:
      opKind = OperatorKind::LE;
      break;
    case LangParser::GTE:
      opKind = OperatorKind::GE;
      break;
    case LangParser::LSHIFT:
      opKind = OperatorKind::BW_LSHIFT;
      break;

    case LangParser::CONCAT:
      opKind = OperatorKind::CONCAT;
      break;
    case LangParser::ADD:
      opKind = OperatorKind::ADD;
      break;
    case LangParser::SUB:
      opKind = OperatorKind::SUB;
      break;
    case LangParser::MUL:
      opKind = OperatorKind::MUL;
      break;
    case LangParser::DIV:
      opKind = OperatorKind::DIV;
      break;
    case LangParser::IDIV:
      opKind = OperatorKind::IDIV;
      break;
    case LangParser::MOD:
      opKind = OperatorKind::MOD;
      break;
    default:
      delete left;
      throw std::runtime_error("未处理的二元操作符 Token 类型: " + std::to_string(tokenType) +
                               " 在行 " + std::to_string(opLoc.line));
    }

    antlr4::ParserRuleContext *childI = getChild(i);
    if (!childI) {
      delete left;
      throw std::runtime_error("AstBuilderVisitor::visitBinaryExpressionRawPtrnullptr");
    }
    std::any rightAny = visit(childI);
    AstNode *rightNodeRaw =
        safeAnyCastRawPtr<AstNode>(rightAny, "visitBinaryExpression > right operand");
    Expression *right = dynamic_cast<Expression *>(rightNodeRaw);
    if (!right && rightAny.has_value()) {
      delete left;
      delete rightNodeRaw;
      throw std::runtime_error("二元操作符右侧必须是表达式");
    }
    if (!right) {
      delete left;
      return std::any();
    }

    BinaryOpNode *newNode = new BinaryOpNode(opKind, left, right, opLoc);
    if (!newNode)
      throw std::runtime_error("AstBuilderVisitor::visitBinaryExpressionRawPtrnullptr");

    left = newNode;
  }
  return std::any(static_cast<AstNode *>(left));
}

std::any AstBuilderVisitor::visitCompilationUnit(LangParser::CompilationUnitContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitCompilationUnitnullptr");
  scope_depth = 0;
  SourceLocation loc = getSourceLocation(ctx);
  std::vector<Statement *> statements;

  for (auto stmtCtx : ctx->statement()) {
    if (!stmtCtx)
      throw std::runtime_error("AstBuilderVisitor::visitCompilationUnitnullptr");
    std::any result = visit(stmtCtx);

    if (result.type() == typeid(std::vector<Declaration *>)) {
      try {

        std::vector<Declaration *> declVec = std::any_cast<std::vector<Declaration *>>(result);
        for (Declaration *decl : declVec) {
          if (!decl)
            throw std::runtime_error("AstBuilderVisitor::visitCompilationUnitnullptr");

          statements.push_back(static_cast<Statement *>(decl));
        }

      } catch (const std::bad_any_cast &) {
        throw std::runtime_error("内部错误: visitCompilationUnit 无法转换 Declaration 列表。");
      }
    }

    else if (result.has_value()) {
      AstNode *nodePtr = safeAnyCastRawPtr<AstNode>(result, "visitCompilationUnit > statement");
      if (nodePtr) {
        Statement *stmtPtr = dynamic_cast<Statement *>(nodePtr);
        if (stmtPtr) {
          statements.push_back(stmtPtr);
        } else {

          delete nodePtr;
          throw std::runtime_error("内部错误: visitCompilationUnit 收到非语句节点。");
        }
      }
    }
  }

  SourceLocation endLoc = getSourceLocation(ctx->EOF());
  BlockNode *blockNode = new BlockNode(std::move(statements), loc, endLoc);
  if (!blockNode)
    throw std::runtime_error("AstBuilderVisitor::visitCompilationUnitnullptr");
  return std::any(static_cast<AstNode *>(blockNode));
}

std::any AstBuilderVisitor::visitBlockStatement(LangParser::BlockStatementContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBlockStatementnullptr");
  scope_depth++;
  SourceLocation loc = getSourceLocation(ctx);
  std::vector<Statement *> statements;

  for (auto stmtCtx : ctx->statement()) {
    if (!stmtCtx)
      throw std::runtime_error("AstBuilderVisitor::visitBlockStatementnullptr");
    std::any result = visit(stmtCtx);

    if (result.type() == typeid(std::vector<Declaration *>)) {
      try {

        std::vector<Declaration *> declVec = std::any_cast<std::vector<Declaration *>>(result);
        for (Declaration *decl : declVec) {
          if (!decl)
            throw std::runtime_error("AstBuilderVisitor::visitBlockStatementnullptr");

          statements.push_back(static_cast<Statement *>(decl));
        }

      } catch (const std::bad_any_cast &) {
        throw std::runtime_error("内部错误: visitBlockStatement 无法转换 Declaration 列表。");
      }
    }

    else if (result.has_value()) {
      AstNode *nodePtr = safeAnyCastRawPtr<AstNode>(result, "visitBlockStatement > statement");
      if (nodePtr) {
        Statement *stmtPtr = dynamic_cast<Statement *>(nodePtr);
        if (stmtPtr) {
          statements.push_back(stmtPtr);
        } else {

          delete nodePtr;
          throw std::runtime_error("内部错误: visitBlockStatement 收到非语句节点。");
        }
      }
    }
  }

  SourceLocation endLoc = getSourceLocation(ctx->CCB());
  BlockNode *blockNode = new BlockNode(std::move(statements), loc, endLoc);
  if (!blockNode)
    throw std::runtime_error("AstBuilderVisitor::visitBlockStatementnullptr");
  scope_depth--;
  return std::any(static_cast<AstNode *>(blockNode));
}

std::any AstBuilderVisitor::visitSemicolonStmt(LangParser::SemicolonStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitSemicolonStmtnullptr");
  return std::any();
}

std::any AstBuilderVisitor::visitAssignStmt(LangParser::AssignStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitAssignStmtnullptr");
  if (!ctx->assignStatement())
    throw std::runtime_error("AstBuilderVisitor::visitAssignStmtnullptr");
  return visit(ctx->assignStatement());
}

std::any AstBuilderVisitor::visitMutiVariableDeclarationDef(
    LangParser::MutiVariableDeclarationDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("visitMutiVariableDeclarationDef nullptr context");
  SourceLocation loc = getSourceLocation(ctx);
  std::vector<MultiDeclVariableInfo> varInfos;
  Expression *initializer = nullptr;

  try {
    auto idNodes = ctx->IDENTIFIER();
    auto globalNodes = ctx->GLOBAL();
    auto constNodes = ctx->CONST();
    auto commaNodes = ctx->COMMA();

    if (idNodes.empty()) {
      throw std::runtime_error("vars 声明至少需要一个变量名");
    }

    size_t currentGlobalIdx = 0;
    size_t currentConstIdx = 0;
    size_t currentCommaIdx = 0;

    size_t previousDelimiterIndex = ctx->VARS()->getSymbol()->getTokenIndex();

    for (size_t i = 0; i < idNodes.size(); ++i) {
      antlr4::tree::TerminalNode *idNode = idNodes[i];
      if (!idNode)
        throw std::runtime_error("vars 声明中的 IDENTIFIER 节点为空");
      size_t idTokenIndex = idNode->getSymbol()->getTokenIndex();

      bool is_global = false;
      if (currentGlobalIdx < globalNodes.size()) {
        antlr4::tree::TerminalNode *globalNode = globalNodes[currentGlobalIdx];
        if (globalNode) {
          size_t globalTokenIndex = globalNode->getSymbol()->getTokenIndex();

          if (globalTokenIndex > previousDelimiterIndex && globalTokenIndex < idTokenIndex) {
            is_global = true;
            currentGlobalIdx++;
            previousDelimiterIndex = globalTokenIndex;
          }
        } else {
        }
      }

      bool is_const = false;
      if (currentConstIdx < constNodes.size()) {
        antlr4::tree::TerminalNode *constNode = constNodes[currentConstIdx];
        if (constNode) {
          size_t constTokenIndex = constNode->getSymbol()->getTokenIndex();

          if (constTokenIndex > previousDelimiterIndex && constTokenIndex < idTokenIndex) {
            is_const = true;
            currentConstIdx++;
            previousDelimiterIndex = constTokenIndex;
          }
        } else {
        }
      }

      std::string name = idNode->getText();

      varInfos.emplace_back(std::move(name), is_global, is_const);

      if (i < commaNodes.size()) {
        antlr4::tree::TerminalNode *commaNode = commaNodes[i];
        if (!commaNode) {
          throw std::runtime_error("vars 声明中的 COMMA 节点为空");
        }
        previousDelimiterIndex = commaNode->getSymbol()->getTokenIndex();
      } else {

        previousDelimiterIndex = idTokenIndex;
      }
    }

    if (ctx->ASSIGN()) {
      if (!ctx->expression()) {
        throw std::runtime_error("内部错误: vars 声明中 '=' 后缺少初始化表达式");
      }
      std::any initResult = visit(ctx->expression());
      AstNode *initRaw =
          safeAnyCastRawPtr<AstNode>(initResult, "visitMutiVariableDeclarationDef > initializer");
      initializer = dynamic_cast<Expression *>(initRaw);
      if (!initializer && initResult.has_value()) {
        delete initRaw;
        throw std::runtime_error("vars 初始化器必须是表达式");
      }
      if (!initializer) {

        throw std::runtime_error("访问 vars 初始化表达式失败");
      }
    }

    MutiVariableDeclarationNode *multiDeclNode =
        new MutiVariableDeclarationNode(std::move(varInfos), initializer, false, loc);
    if (!multiDeclNode) {
      delete initializer;
      throw std::runtime_error("创建 MutiVariableDeclarationNode 失败");
    }

    return std::any(static_cast<AstNode *>(multiDeclNode));

  } catch (...) {

    delete initializer;
    throw;
  }
}

std::any AstBuilderVisitor::visitUpdateStmt(LangParser::UpdateStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitUpdateStmtnullptr");
  if (!ctx->updateStatement())
    throw std::runtime_error("AstBuilderVisitor::visitUpdateStmtnullptr");
  return visit(ctx->updateStatement());
}

std::any AstBuilderVisitor::visitExpressionStmt(LangParser::ExpressionStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitExpressionStmtnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  if (!ctx->expression())
    throw std::runtime_error("AstBuilderVisitor::visitExpressionStmtnullptr");
  std::any exprResult = visit(ctx->expression());
  AstNode *exprNodeRaw = safeAnyCastRawPtr<AstNode>(exprResult, "visitExpressionStmt > expression");
  Expression *exprNode = dynamic_cast<Expression *>(exprNodeRaw);
  if (!exprNode && exprResult.has_value()) {
    delete exprNodeRaw;
    throw std::runtime_error("ExpressionStatement 需要一个表达式节点");
  }
  if (!exprNode)
    return std::any();

  ExpressionStatementNode *stmtNode = new ExpressionStatementNode(exprNode, loc);
  if (!stmtNode)
    throw std::runtime_error("AstBuilderVisitor::visitExpressionStmtnullptr");
  return std::any(static_cast<AstNode *>(stmtNode));
}

std::any AstBuilderVisitor::visitDeclarationStmt(LangParser::DeclarationStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitDeclarationStmtnullptr");
  if (!ctx->declaration())
    throw std::runtime_error("AstBuilderVisitor::visitDeclarationStmtnullptr");

  return visit(ctx->declaration());
}

std::any AstBuilderVisitor::visitIfStmt(LangParser::IfStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitIfStmtnullptr");
  if (!ctx->ifStatement())
    throw std::runtime_error("AstBuilderVisitor::visitIfStmtnullptr");
  return visit(ctx->ifStatement());
}

std::any AstBuilderVisitor::visitWhileStmt(LangParser::WhileStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitWhileStmtnullptr");
  if (!ctx->whileStatement())
    throw std::runtime_error("AstBuilderVisitor::visitWhileStmtnullptr");
  return visit(ctx->whileStatement());
}

std::any AstBuilderVisitor::visitForStmt(LangParser::ForStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForStmtnullptr");
  if (!ctx->forStatement())
    throw std::runtime_error("AstBuilderVisitor::visitForStmtnullptr");
  return visit(ctx->forStatement());
}

std::any AstBuilderVisitor::visitBreakStmt(LangParser::BreakStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBreakStmtnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  BreakStatementNode *node = new BreakStatementNode(loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitBreakStmtnullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitContinueStmt(LangParser::ContinueStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitContinueStmtnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  ContinueStatementNode *node = new ContinueStatementNode(loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitContinueStmtnullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitDeferStmt(LangParser::DeferStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitDeferStmt nullptr");

  if (!ctx->deferStatement())
    throw std::runtime_error("AstBuilderVisitor::visitDeferStmt missing child");

  return visit(ctx->deferStatement());
}

std::any AstBuilderVisitor::visitDeferBlockStmt(LangParser::DeferBlockStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitDeferBlockStmt nullptr");

  SourceLocation loc = getSourceLocation(ctx);

  if (!ctx->blockStatement())
    throw std::runtime_error("defer 语句缺少代码块");

  std::any bodyResult = visit(ctx->blockStatement());

  AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(bodyResult, "visitDeferBlockStmt > body");
  BlockNode *body = dynamic_cast<BlockNode *>(bodyRaw);

  if (!body && bodyResult.has_value()) {
    delete bodyRaw;
    throw std::runtime_error("defer 的主体必须是一个代码块");
  }
  if (!body) {
    throw std::runtime_error("defer 主体访问失败");
  }

  DeferStatementNode *node = new DeferStatementNode(body, loc);
  if (!node)
    throw std::runtime_error("创建 DeferStatementNode 失败");

  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitReturnStmt(LangParser::ReturnStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitReturnStmtnullptr");
  SourceLocation loc = getSourceLocation(ctx);

  std::vector<Expression *> returnValueList;
  if (ctx->expressionList()) {
    auto exprListCtx = ctx->expressionList();
    if (!exprListCtx)
      throw std::runtime_error("AstBuilderVisitor::visitReturnStmtnullptr");

    for (auto retExprCtx : exprListCtx->expression()) {
      if (!retExprCtx)
        throw std::runtime_error("AstBuilderVisitor::visitReturnStmtnullptr");

      std::any result = visit(retExprCtx);
      AstNode *retExprNodeRaw =
          safeAnyCastRawPtr<AstNode>(result, "visitReturnStmt > expressionList");
      Expression *retExprNode = dynamic_cast<Expression *>(retExprNodeRaw);
      if (!retExprNode && result.has_value()) {
        delete retExprNodeRaw;
        throw std::runtime_error("返回值必须是表达式");
      }
      if (!retExprNode)
        throw std::runtime_error("返回值访问失败");
      returnValueList.push_back(retExprNode);
    }
  }

  ReturnStatementNode *node = new ReturnStatementNode(returnValueList, loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitReturnStmtnullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitBlockStmt(LangParser::BlockStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBlockStmtnullptr");
  if (!ctx->blockStatement())
    throw std::runtime_error("AstBuilderVisitor::visitBlockStmtnullptr");
  return visit(ctx->blockStatement());
}

std::any
AstBuilderVisitor::visitFunctionDeclarationDef(LangParser::FunctionDeclarationDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitFunctionDeclarationDefnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  bool is_global = (ctx->GLOBAL() != nullptr);
  bool is_const = (ctx->CONST() != nullptr);
  AstType *returnType = nullptr;
  std::vector<ParameterDeclNode *> params;
  bool isVariadic = false;
  BlockNode *body = nullptr;
  IdentifierNode *qualIdentNode = nullptr;

  try {
    if (!ctx->type())
      throw std::runtime_error("AstBuilderVisitor::visitFunctionDeclarationDefnullptr");

    std::any returnTypeResult = visit(ctx->type());
    returnType = safeAnyCastRawPtr<AstType>(returnTypeResult, "visitFunctionDeclarationDef > type");
    if (!returnType)
      throw std::runtime_error("函数必须有返回类型");

    if (!ctx->qualifiedIdentifier())
      throw std::runtime_error("AstBuilderVisitor::visitFunctionDeclarationDefnullptr");

    std::any qualIdentResult = visit(ctx->qualifiedIdentifier());
    AstNode *qualIdentRaw = safeAnyCastRawPtr<AstNode>(
        qualIdentResult, "visitFunctionDeclarationDef > qualifiedIdentifier");
    qualIdentNode = dynamic_cast<IdentifierNode *>(qualIdentRaw);
    if (!qualIdentNode) {
      if (qualIdentRaw)
        delete qualIdentRaw;
      throw std::runtime_error("函数名必须是标识符");
    }
    std::string funcName = qualIdentNode->name;

    if (ctx->parameterList()) {
      std::any paramListResultAny = visit(ctx->parameterList());

      try {
        auto paramListResult =
            std::any_cast<std::pair<std::vector<ParameterDeclNode *>, bool>>(paramListResultAny);

        for (const auto &p : paramListResult.first) {
          if (!p)
            throw std::runtime_error("AstBuilderVisitor::visitFunctionDeclarationDefnullptr");
        }
        params = std::move(paramListResult.first);
        isVariadic = paramListResult.second;
      } catch (const std::bad_any_cast &e) {

        throw std::runtime_error("AstBuilderVisitor::"
                                 "visitFunctionDeclarationDefnullptr");
      }
    }

    if (!ctx->blockStatement())
      throw std::runtime_error("AstBuilderVisitor::visitFunctionDeclarationDefnullptr");

    std::any bodyResult = visit(ctx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(bodyResult, "visitFunctionDeclarationDef > body");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      if (bodyRaw)
        delete bodyRaw;
      throw std::runtime_error("函数体必须是代码块");
    }

    FunctionDeclNode *funcDecl =
        new FunctionDeclNode(std::move(funcName), std::move(params), returnType, body, is_global,
                             false, isVariadic, false, is_const, loc);
    if (!funcDecl)
      throw std::runtime_error("AstBuilderVisitor::visitFunctionDeclarationDefnullptr");

    delete qualIdentNode;
    qualIdentNode = nullptr;

    return std::any(static_cast<AstNode *>(funcDecl));

  } catch (...) {

    delete returnType;
    deleteVectorItems(params);
    delete body;
    delete qualIdentNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitMultiReturnFunctionDeclarationDef(
    LangParser::MultiReturnFunctionDeclarationDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMultiReturnFunctionDeclarationDefnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  bool is_global = (ctx->GLOBAL() != nullptr);
  bool is_const = (ctx->CONST() != nullptr);
  std::vector<ParameterDeclNode *> params;
  bool isVariadic = false;
  BlockNode *body = nullptr;
  IdentifierNode *qualIdentNode = nullptr;
  AstType *returnType = nullptr;

  try {

    if (!ctx->VARS())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnFunctionDeclarationDefnullptr");

    returnType = new MultiReturnType(getSourceLocation(ctx->VARS()));
    if (!returnType)
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnFunctionDeclarationDefnullptr");

    if (!ctx->qualifiedIdentifier())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnFunctionDeclarationDefnullptr");

    std::any qualIdentResult = visit(ctx->qualifiedIdentifier());
    AstNode *qualIdentRaw = safeAnyCastRawPtr<AstNode>(
        qualIdentResult, "visitMultiReturnFunctionDeclarationDef > qualifiedIdentifier");
    qualIdentNode = dynamic_cast<IdentifierNode *>(qualIdentRaw);
    if (!qualIdentNode) {
      if (qualIdentRaw)
        delete qualIdentRaw;
      throw std::runtime_error("函数名必须是标识符");
    }
    std::string funcName = qualIdentNode->name;

    if (ctx->parameterList()) {
      std::any paramListResultAny = visit(ctx->parameterList());
      try {
        auto paramListResult =
            std::any_cast<std::pair<std::vector<ParameterDeclNode *>, bool>>(paramListResultAny);

        for (const auto &p : paramListResult.first) {
          if (!p)
            throw std::runtime_error("AstBuilderVisitor::"
                                     "visitMultiReturnFunctionDeclarationDefnullptr");
        }
        params = std::move(paramListResult.first);
        isVariadic = paramListResult.second;
      } catch (const std::bad_any_cast &e) {
        throw std::runtime_error("AstBuilderVisitor::"
                                 "visitMultiReturnFunctionDeclarationDefnullptr");
      }
    }

    if (!ctx->blockStatement())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnFunctionDeclarationDefnullptr");

    std::any bodyResult = visit(ctx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(
        bodyResult, "visitMultiReturnFunctionDeclarationDef > blockStatement");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      if (bodyRaw)
        delete bodyRaw;
      throw std::runtime_error("函数体必须是代码块");
    }

    FunctionDeclNode *funcDecl =
        new FunctionDeclNode(std::move(funcName), std::move(params), returnType, body, is_global,
                             false, isVariadic, false, is_const, loc);
    if (!funcDecl)
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnFunctionDeclarationDefnullptr");

    delete qualIdentNode;
    qualIdentNode = nullptr;

    return std::any(static_cast<AstNode *>(funcDecl));
  } catch (...) {
    delete returnType;
    deleteVectorItems(params);
    delete body;
    delete qualIdentNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitClassMethodMember(LangParser::ClassMethodMemberContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");
  SourceLocation loc = getSourceLocation(ctx);
  bool is_static = (ctx->STATIC() != nullptr);
  bool is_const = (ctx->CONST() != nullptr);
  AstType *returnType = nullptr;
  std::vector<ParameterDeclNode *> params;
  bool isVariadic = false;
  BlockNode *body = nullptr;
  try {
    if (!ctx->type())
      throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");

    std::any returnTypeResult = visit(ctx->type());
    returnType = safeAnyCastRawPtr<AstType>(returnTypeResult, "visitClassMethodMember > type");
    if (!returnType)
      throw std::runtime_error("类方法必须有返回类型");

    if (!ctx->IDENTIFIER())
      throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");

    std::string funcName = ctx->IDENTIFIER()->getText();

    if (ctx->parameterList()) {
      std::any paramListResultAny = visit(ctx->parameterList());
      try {
        auto paramListResult =
            std::any_cast<std::pair<std::vector<ParameterDeclNode *>, bool>>(paramListResultAny);

        for (const auto &p : paramListResult.first) {
          if (!p)
            throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");
        }
        params = std::move(paramListResult.first);
        isVariadic = paramListResult.second;
      } catch (const std::bad_any_cast &e) {
        throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");
      }
    }

    if (!ctx->blockStatement())
      throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");

    std::any bodyResult = visit(ctx->blockStatement());
    AstNode *bodyRaw =
        safeAnyCastRawPtr<AstNode>(bodyResult, "visitClassMethodMember > blockStatement");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      if (bodyRaw)
        delete bodyRaw;
      throw std::runtime_error("类方法体必须是代码块");
    }

    FunctionDeclNode *funcDecl =
        new FunctionDeclNode(std::move(funcName), std::move(params), returnType, body, false,
                             is_static, isVariadic, false, is_const, loc);
    if (!funcDecl)
      throw std::runtime_error("AstBuilderVisitor::visitClassMethodMembernullptr");

    return std::any(static_cast<AstNode *>(funcDecl));
  } catch (...) {
    delete returnType;
    deleteVectorItems(params);
    delete body;
    throw;
  }
}

std::any AstBuilderVisitor::visitMultiReturnClassMethodMember(
    LangParser::MultiReturnClassMethodMemberContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMultiReturnClassMethodMembernullptr");
  SourceLocation loc = getSourceLocation(ctx);
  bool is_static = (ctx->STATIC() != nullptr);
  bool is_const = (ctx->CONST() != nullptr);
  std::vector<ParameterDeclNode *> params;
  bool isVariadic = false;
  BlockNode *body = nullptr;
  AstType *returnType = nullptr;

  try {

    if (!ctx->VARS())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnClassMethodMembernullptr");

    returnType = new MultiReturnType(getSourceLocation(ctx->VARS()));
    if (!returnType)
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnClassMethodMembernullptr");

    if (!ctx->IDENTIFIER())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnClassMethodMembernullptr");

    std::string funcName = ctx->IDENTIFIER()->getText();

    if (ctx->parameterList()) {
      std::any paramListResultAny = visit(ctx->parameterList());
      try {
        auto paramListResult =
            std::any_cast<std::pair<std::vector<ParameterDeclNode *>, bool>>(paramListResultAny);

        for (const auto &p : paramListResult.first) {
          if (!p)
            throw std::runtime_error("AstBuilderVisitor::visitMultiReturnClassMethodMembernullptr");
        }
        params = std::move(paramListResult.first);
        isVariadic = paramListResult.second;
      } catch (const std::bad_any_cast &e) {
        throw std::runtime_error("AstBuilderVisitor::"
                                 "visitMultiReturnClassMethodMembernullptr");
      }
    }

    if (!ctx->blockStatement())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnClassMethodMembernullptr");

    std::any bodyResult = visit(ctx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(
        bodyResult, "visitMultiReturnClassMethodMember > blockStatement");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      if (bodyRaw)
        delete bodyRaw;
      throw std::runtime_error("类方法体必须是代码块");
    }

    FunctionDeclNode *funcDecl =
        new FunctionDeclNode(std::move(funcName), std::move(params), returnType, body, false,
                             is_static, isVariadic, false, is_const, loc);
    if (!funcDecl)
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitMultiReturnClassMethodMembernullptr");

    return std::any(static_cast<AstNode *>(funcDecl));
  } catch (...) {
    delete returnType;
    deleteVectorItems(params);
    delete body;
    throw;
  }
}

std::any AstBuilderVisitor::visitLambdaExprDef(LangParser::LambdaExprDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");
  if (!ctx->FUNCTION())
    throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");
  SourceLocation loc = getSourceLocation(ctx->FUNCTION());
  std::vector<ParameterDeclNode *> params;
  bool isVariadic = false;
  AstType *returnType = nullptr;
  BlockNode *body = nullptr;
  try {
    if (ctx->parameterList()) {
      std::any paramListResultAny = visit(ctx->parameterList());
      try {
        auto paramListResult =
            std::any_cast<std::pair<std::vector<ParameterDeclNode *>, bool>>(paramListResultAny);

        for (const auto &p : paramListResult.first) {
          if (!p)
            throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");
        }
        params = std::move(paramListResult.first);
        isVariadic = paramListResult.second;
      } catch (const std::bad_any_cast &e) {
        throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");
      }
    }

    if (ctx->type()) {
      std::any returnTypeResult = visit(ctx->type());
      returnType = safeAnyCastRawPtr<AstType>(returnTypeResult, "visitLambdaExprDef > type");
      if (!returnType)
        throw std::runtime_error("Lambda 返回类型访问失败");
    } else if (ctx->VARS()) {

      returnType = new MultiReturnType(getSourceLocation(ctx->VARS()));
      if (!returnType)
        throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");
    } else {
      throw std::runtime_error("Lambda 表达式缺少返回类型或 vars 关键字");
    }

    if (!ctx->blockStatement())
      throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");
    std::any bodyResult = visit(ctx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(bodyResult, "visitLambdaExprDef > body");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      if (bodyRaw)
        delete bodyRaw;
      throw std::runtime_error("Lambda 体必须是代码块");
    }

    LambdaNode *lambdaNode = new LambdaNode(std::move(params), returnType, body, isVariadic, loc);
    if (!lambdaNode)
      throw std::runtime_error("AstBuilderVisitor::visitLambdaExprDefnullptr");

    return std::any(static_cast<AstNode *>(lambdaNode));
  } catch (...) {
    deleteVectorItems(params);
    delete returnType;
    delete body;
    throw;
  }
}

std::any AstBuilderVisitor::visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitUpdateAssignStmtnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  if (!ctx->lvalue())
    throw std::runtime_error("AstBuilderVisitor::visitUpdateAssignStmtnullptr");

  std::any lvalResult = visit(ctx->lvalue());
  AstNode *lvalRaw = safeAnyCastRawPtr<AstNode>(lvalResult, "visitUpdateAssignStmt > lvalue");
  Expression *lval = dynamic_cast<Expression *>(lvalRaw);
  if (!lval && lvalResult.has_value()) {
    delete lvalRaw;
    throw std::runtime_error("更新赋值左侧必须是表达式");
  }
  if (!lval)
    return std::any();

  if (!ctx->expression())
    throw std::runtime_error("AstBuilderVisitor::visitUpdateAssignStmtnullptr");

  std::any rvalResult = visit(ctx->expression());
  AstNode *rvalRaw = safeAnyCastRawPtr<AstNode>(rvalResult, "visitUpdateAssignStmt > expression");
  Expression *rval = dynamic_cast<Expression *>(rvalRaw);
  if (!rval && rvalResult.has_value()) {
    delete lval;
    delete rvalRaw;
    throw std::runtime_error("更新赋值右侧必须是表达式");
  }
  if (!rval) {
    delete lval;
    return std::any();
  }

  if (!ctx->op)
    throw std::runtime_error("AstBuilderVisitor::visitUpdateAssignStmtnullptr");
  OperatorKind op;
  int tokenType = ctx->op->getType();

  switch (tokenType) {
  case LangParser::ADD_ASSIGN:
    op = OperatorKind::ASSIGN_ADD;
    break;
  case LangParser::SUB_ASSIGN:
    op = OperatorKind::ASSIGN_SUB;
    break;
  case LangParser::MUL_ASSIGN:
    op = OperatorKind::ASSIGN_MUL;
    break;
  case LangParser::DIV_ASSIGN:
    op = OperatorKind::ASSIGN_DIV;
    break;
  case LangParser::IDIV_ASSIGN:
    op = OperatorKind::ASSIGN_IDIV;
    break;
  case LangParser::MOD_ASSIGN:
    op = OperatorKind::ASSIGN_MOD;
    break;
  case LangParser::CONCAT_ASSIGN:
    op = OperatorKind::ASSIGN_CONCAT;
    break;
  default:
    delete lval;
    delete rval;
    throw std::runtime_error("未知的更新赋值操作符在行 " + std::to_string(loc.line));
  }

  UpdateAssignmentNode *node = new UpdateAssignmentNode(op, lval, rval, loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitUpdateAssignStmtnullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitNormalAssignStmt(LangParser::NormalAssignStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("visitNormalAssignStmt nullptr context");
  SourceLocation loc = getSourceLocation(ctx);
  std::vector<Expression *> lvalues;
  std::vector<Expression *> rvalues;

  try {

    for (auto lvalCtx : ctx->lvalue()) {
      if (!lvalCtx)
        throw std::runtime_error("赋值语句中 lvalue context 为空");
      std::any lvalResult = visit(lvalCtx);
      AstNode *lvalRaw = safeAnyCastRawPtr<AstNode>(lvalResult, "visitNormalAssignStmt lvalue");
      Expression *lval = dynamic_cast<Expression *>(lvalRaw);
      if (!lval) {
        if (lvalRaw)
          delete lvalRaw;
        deleteVectorItems(lvalues);
        throw std::runtime_error("左值必须是表达式，或者访问左值失败");
      }
      lvalues.push_back(lval);
    }
    if (lvalues.empty()) {
      throw std::runtime_error("赋值语句至少需要一个左值");
    }

    auto rvalCtxList = ctx->expression();
    if (rvalCtxList.empty()) {
      deleteVectorItems(lvalues);
      throw std::runtime_error("赋值语句至少需要一个右侧表达式");
    }

    for (auto rvalCtx : rvalCtxList) {
      if (!rvalCtx) {
        deleteVectorItems(lvalues);
        deleteVectorItems(rvalues);
        throw std::runtime_error("赋值语句右侧表达式 context 为空");
      }
      std::any rvalResult = visit(rvalCtx);
      AstNode *rvalRaw = safeAnyCastRawPtr<AstNode>(rvalResult, "visitNormalAssignStmt rvalue");
      Expression *rval = dynamic_cast<Expression *>(rvalRaw);
      if (!rval) {
        if (rvalRaw)
          delete rvalRaw;
        deleteVectorItems(lvalues);
        deleteVectorItems(rvalues);
        throw std::runtime_error("右值必须是表达式，或者访问右值失败");
      }
      rvalues.push_back(rval);
    }

    AssignmentNode *node = new AssignmentNode(std::move(lvalues), std::move(rvalues), loc);
    if (!node)
      throw std::runtime_error("创建 AssignmentNode 失败");
    return std::any(static_cast<AstNode *>(node));

  } catch (...) {

    deleteVectorItems(lvalues);
    deleteVectorItems(rvalues);
    throw;
  }
}

std::any AstBuilderVisitor::visitLvalueBase(LangParser::LvalueBaseContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *currentLval = nullptr;
  if (ctx->IDENTIFIER()) {
    currentLval =
        new IdentifierNode(ctx->IDENTIFIER()->getText(), getSourceLocation(ctx->IDENTIFIER()));
    if (!currentLval)
      throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");
  } else {
    throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");
  }

  for (auto suffixCtx : ctx->lvalueSuffix()) {
    if (!suffixCtx)
      throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");

    SourceLocation suffixLoc = getSourceLocation(suffixCtx);
    if (auto indexCtx = dynamic_cast<LangParser::LvalueIndexContext *>(suffixCtx)) {
      if (!indexCtx->expression())
        throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");

      std::any indexResult = visit(indexCtx->expression());
      AstNode *indexExprRaw = safeAnyCastRawPtr<AstNode>(indexResult, "visitLvalueBase > index");
      Expression *indexExpr = dynamic_cast<Expression *>(indexExprRaw);
      if (!indexExpr && indexResult.has_value()) {
        delete currentLval;
        delete indexExprRaw;
        throw std::runtime_error("索引必须是表达式");
      }
      if (!indexExpr) {
        delete currentLval;
        return std::any();
      }
      IndexAccessNode *newNode = new IndexAccessNode(currentLval, indexExpr, suffixLoc);
      if (!newNode)
        throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");
      currentLval = newNode;
    } else if (auto memberCtx = dynamic_cast<LangParser::LvalueMemberContext *>(suffixCtx)) {
      if (!memberCtx->IDENTIFIER())
        throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");

      MemberAccessNode *newNode =
          new MemberAccessNode(currentLval, memberCtx->IDENTIFIER()->getText(), suffixLoc);
      if (!newNode)
        throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");
      currentLval = newNode;
    } else {

      delete currentLval;
      throw std::runtime_error("AstBuilderVisitor::visitLvalueBasenullptr");
    }
  }
  return std::any(static_cast<AstNode *>(currentLval));
}

std::any AstBuilderVisitor::visitDeclaration(LangParser::DeclarationContext *ctx) {
  if (!ctx)
    throw std::runtime_error("visitDeclaration nullptr context");
  bool isExported = (ctx->EXPORT() != nullptr);
  std::any resultAny;

  try {

    if (ctx->variableDeclaration()) {
      resultAny = visit(ctx->variableDeclaration());
    } else if (ctx->functionDeclaration()) {
      resultAny = visit(ctx->functionDeclaration());
    } else if (ctx->classDeclaration()) {
      resultAny = visit(ctx->classDeclaration());
    } else {

      throw std::runtime_error("visitDeclaration 未找到有效的子声明节点");
    }

    if (!resultAny.has_value()) {

      throw std::runtime_error("访问子声明返回了空的 std::any");
    }

    AstNode *declNodeRaw =
        safeAnyCastRawPtr<AstNode>(resultAny, "visitDeclaration > child declaration");
    if (!declNodeRaw) {

      throw std::runtime_error("访问子声明返回了空指针的 std::any");
    }

    Declaration *declNode = dynamic_cast<Declaration *>(declNodeRaw);
    if (!declNode) {

      delete declNodeRaw;
      throw std::runtime_error("内部错误: visitDeclaration 收到的子节点不是 Declaration 类型");
    } else {

      declNode->isModuleRoot = (scope_depth == 0);
    }

    if (auto *varDecl = dynamic_cast<VariableDeclNode *>(declNode)) {
      varDecl->isExported = isExported;
    } else if (auto *funcDecl = dynamic_cast<FunctionDeclNode *>(declNode)) {
      funcDecl->isExported = isExported;
    } else if (auto *classDecl = dynamic_cast<ClassDeclNode *>(declNode)) {
      classDecl->isExported = isExported;
    } else if (auto *mutiVarDecl = dynamic_cast<MutiVariableDeclarationNode *>(declNode)) {
      mutiVarDecl->isExported = isExported;
    }

    return resultAny;

  } catch (...) {

    throw;
  }
}

std::any AstBuilderVisitor::visitImportStmt(LangParser::ImportStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitImportStmtnullptr");
  if (!ctx->importStatement())
    throw std::runtime_error("AstBuilderVisitor::visitImportStmtnullptr");
  return visit(ctx->importStatement());
}

std::any
AstBuilderVisitor::visitVariableDeclarationDef(LangParser::VariableDeclarationDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitVariableDeclarationDefnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  bool is_global = (ctx->GLOBAL() != nullptr);
  bool is_const = (ctx->CONST() != nullptr);
  AstType *typeAnn = nullptr;
  Expression *initializer = nullptr;
  std::string name = "";
  if (!ctx->declaration_item())
    throw std::runtime_error("AstBuilderVisitor::visitVariableDeclarationDefnullptr");

  auto itemCtx = ctx->declaration_item();
  SourceLocation itemLoc = getSourceLocation(itemCtx);

  try {

    if (!itemCtx->IDENTIFIER())
      throw std::runtime_error("AstBuilderVisitor::"
                               "visitVariableDeclarationDefnullptr");

    name = itemCtx->IDENTIFIER()->getText();

    if (itemCtx->type()) {
      std::any typeResult = visit(itemCtx->type());
      typeAnn = safeAnyCastRawPtr<AstType>(typeResult, "visitVariableDeclarationDef > type");
    } else if (itemCtx->AUTO()) {
      typeAnn = new AutoType(getSourceLocation(itemCtx->AUTO()));
      if (!typeAnn)
        throw std::runtime_error("AstBuilderVisitor::visitVariableDeclarationDefnullptr");

    } else {
      throw std::runtime_error("内部错误: 变量声明缺少类型或 auto 在行 " +
                               std::to_string(itemLoc.line));
    }
    if (!typeAnn)
      throw std::runtime_error("无法为变量声明获取类型注解");

    if (ctx->expression()) {
      std::any initResult = visit(ctx->expression());
      AstNode *initRaw =
          safeAnyCastRawPtr<AstNode>(initResult, "visitVariableDeclarationDef > initializer");
      initializer = dynamic_cast<Expression *>(initRaw);
      if (!initializer && initResult.has_value()) {
        delete initRaw;
        throw std::runtime_error("变量初始化器必须是表达式");
      }
    }

    VariableDeclNode *declNode = new VariableDeclNode(std::move(name), typeAnn, initializer,
                                                      is_const, is_global, false, false, itemLoc);
    if (!declNode)
      throw std::runtime_error("AstBuilderVisitor::visitVariableDeclarationDefnullptr");

    return std::any(static_cast<AstNode *>(declNode));
  } catch (...) {

    delete typeAnn;
    delete initializer;
    throw;
  }
}

std::any AstBuilderVisitor::visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitClassDeclarationDefnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  if (!ctx->IDENTIFIER())
    throw std::runtime_error("AstBuilderVisitor::visitClassDeclarationDefnullptr");

  std::string className = ctx->IDENTIFIER()->getText();
  std::vector<ClassMemberNode *> members;

  try {
    for (auto memberCtx : ctx->classMember()) {
      if (!memberCtx)
        throw std::runtime_error("AstBuilderVisitor::visitClassDeclarationDefnullptr");

      std::any memberResult = visit(memberCtx);

      if (memberResult.has_value()) {
        AstNode *nodePtr =
            safeAnyCastRawPtr<AstNode>(memberResult, "visitClassDeclarationDef > member");
        if (nodePtr) {

          Declaration *declPtr = dynamic_cast<Declaration *>(nodePtr);
          if (declPtr) {

            ClassMemberNode *memberNode =
                new ClassMemberNode(declPtr, getSourceLocation(memberCtx));
            if (!memberNode)
              throw std::runtime_error("AstBuilderVisitor::visitClassDeclarationDefnullptr");

            members.push_back(memberNode);
          } else {

            delete nodePtr;
            throw std::runtime_error("内部错误: 类成员必须是声明类型节点。");
          }
        }
      }
    }
  } catch (...) {

    deleteVectorItems(members);
    throw;
  }

  ClassDeclNode *classDecl =
      new ClassDeclNode(std::move(className), std::move(members), false, loc);
  if (!classDecl)
    throw std::runtime_error("AstBuilderVisitor::visitClassDeclarationDefnullptr");
  return std::any(static_cast<AstNode *>(classDecl));
}

std::any AstBuilderVisitor::visitClassFieldMember(LangParser::ClassFieldMemberContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitClassFieldMembernullptr");
  SourceLocation loc = getSourceLocation(ctx);
  bool is_static = (ctx->STATIC() != nullptr);
  bool is_const = (ctx->CONST() != nullptr);

  AstType *typeAnn = nullptr;
  Expression *initializer = nullptr;
  std::string name = "";
  if (!ctx->declaration_item())
    throw std::runtime_error("AstBuilderVisitor::visitClassFieldMembernullptr");

  auto itemCtx = ctx->declaration_item();
  SourceLocation itemLoc = getSourceLocation(itemCtx);

  try {

    if (!itemCtx->IDENTIFIER())
      throw std::runtime_error("AstBuilderVisitor::visitClassFieldMembernullptr");
    name = itemCtx->IDENTIFIER()->getText();

    if (itemCtx->type()) {
      std::any typeResult = visit(itemCtx->type());
      typeAnn = safeAnyCastRawPtr<AstType>(typeResult, "visitClassFieldMember > type");
    } else if (itemCtx->AUTO()) {
      typeAnn = new AutoType(getSourceLocation(itemCtx->AUTO()));
      if (!typeAnn)
        throw std::runtime_error("AstBuilderVisitor::visitClassFieldMembernullptr");
    } else {
      throw std::runtime_error("内部错误: 类字段声明缺少类型或 auto 在行 " +
                               std::to_string(itemLoc.line));
    }
    if (!typeAnn)
      throw std::runtime_error("无法为类字段获取类型注解");

    if (ctx->expression()) {
      std::any initResult = visit(ctx->expression());
      AstNode *initRaw =
          safeAnyCastRawPtr<AstNode>(initResult, "visitClassFieldMember > initializer");
      initializer = dynamic_cast<Expression *>(initRaw);
      if (!initializer && initResult.has_value()) {
        delete initRaw;
        throw std::runtime_error("类字段初始化器必须是表达式");
      }
    }

    VariableDeclNode *declNode = new VariableDeclNode(std::move(name), typeAnn, initializer,
                                                      is_const, false, is_static, false, itemLoc);
    if (!declNode)
      throw std::runtime_error("AstBuilderVisitor::visitClassFieldMembernullptr");

    return std::any(static_cast<AstNode *>(declNode));
  } catch (...) {

    delete typeAnn;
    delete initializer;
    throw;
  }
}

std::any AstBuilderVisitor::visitClassEmptyMember(LangParser::ClassEmptyMemberContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitClassEmptyMembernullptr");
  return std::any();
}

std::any AstBuilderVisitor::visitTypeAny(LangParser::TypeAnyContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitTypeAnynullptr");
  if (!ctx->ANY())
    throw std::runtime_error("AstBuilderVisitor::visitTypeAnynullptr");
  AnyType *node = new AnyType(getSourceLocation(ctx->ANY()));
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitTypeAnynullptr");
  return std::any(static_cast<AstType *>(node));
}

std::any AstBuilderVisitor::visitPrimitiveType(LangParser::PrimitiveTypeContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimitiveTypenullptr");
  SourceLocation loc = getSourceLocation(ctx);
  AstType *node = nullptr;
  if (ctx->INT())
    node = new PrimitiveType(PrimitiveTypeKind::INT, loc);
  else if (ctx->FLOAT())
    node = new PrimitiveType(PrimitiveTypeKind::FLOAT, loc);
  else if (ctx->NUMBER())
    node = new PrimitiveType(PrimitiveTypeKind::NUMBER, loc);
  else if (ctx->STRING())
    node = new PrimitiveType(PrimitiveTypeKind::STRING, loc);
  else if (ctx->BOOL())
    node = new PrimitiveType(PrimitiveTypeKind::BOOL, loc);
  else if (ctx->VOID())
    node = new PrimitiveType(PrimitiveTypeKind::VOID, loc);
  else if (ctx->NULL_())
    node = new PrimitiveType(PrimitiveTypeKind::NULL_TYPE, loc);
  else if (ctx->COROUTINE())
    node = new CoroutineKeywordType(loc);
  else if (ctx->FUNCTION())
    node = new FunctionKeywordType(loc);
  else
    throw std::runtime_error("未知的基础类型在行 " + std::to_string(loc.line));

  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitPrimitiveTypenullptr");
  return std::any(static_cast<AstType *>(node));
}

std::any AstBuilderVisitor::visitListType(LangParser::ListTypeContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitListTypenullptr");
  SourceLocation loc = getSourceLocation(ctx);
  AstType *elementType = nullptr;
  if (ctx->type()) {
    std::any elemResult = visit(ctx->type());
    elementType = safeAnyCastRawPtr<AstType>(elemResult, "visitListType > type");
  }
  if (!elementType) {
    elementType = new AnyType(loc);
    if (!elementType)
      throw std::runtime_error("AstBuilderVisitor::visitListTypenullptr");
  }
  ListType *listNode = new ListType(elementType, loc);
  if (!listNode)
    throw std::runtime_error("AstBuilderVisitor::visitListTypenullptr");
  return std::any(static_cast<AstType *>(listNode));
}

std::any AstBuilderVisitor::visitMapType(LangParser::MapTypeContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapTypenullptr");
  SourceLocation loc = getSourceLocation(ctx);
  AstType *keyType = nullptr;
  AstType *valueType = nullptr;
  try {
    if (ctx->type().size() == 2) {
      if (!ctx->type(0) || !ctx->type(1))
        throw std::runtime_error("AstBuilderVisitor::visitMapTypenullptr");
      std::any keyResult = visit(ctx->type(0));
      keyType = safeAnyCastRawPtr<AstType>(keyResult, "visitMapType > key type");
      std::any valResult = visit(ctx->type(1));
      valueType = safeAnyCastRawPtr<AstType>(valResult, "visitMapType > value type");
      if (!keyType || !valueType) {

        throw std::runtime_error("Failed to visit map key or value type");
      }
    } else {
      keyType = new AnyType(loc);
      if (!keyType)
        throw std::runtime_error("AstBuilderVisitor::visitMapTypenullptr");
      valueType = new AnyType(loc);
      if (!valueType) {
        delete keyType;
        throw std::runtime_error("AstBuilderVisitor::visitMapTypenullptr");
      }
    }

    MapType *mapNode = new MapType(keyType, valueType, loc);
    if (!mapNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapTypenullptr");
    return std::any(static_cast<AstType *>(mapNode));
  } catch (...) {
    delete keyType;
    delete valueType;
    throw;
  }
}

std::any AstBuilderVisitor::visitExpression(LangParser::ExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitExpressionnullptr");
  if (!ctx->logicalOrExp())
    throw std::runtime_error("AstBuilderVisitor::visitExpressionnullptr");
  return visit(ctx->logicalOrExp());
}

std::any AstBuilderVisitor::visitLogicalOrExpression(LangParser::LogicalOrExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitLogicalOrExpressionnullptr");

  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->logicalAndExp().size(); },
      [&](size_t i) { return ctx->logicalAndExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->OR().size())
          throw std::runtime_error("AstBuilderVisitor::visitLogicalOrExpressionnullptr");
        return ctx->OR(i);
      });
}

std::any
AstBuilderVisitor::visitLogicalAndExpression(LangParser::LogicalAndExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitLogicalAndExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->bitwiseOrExp().size(); },
      [&](size_t i) { return ctx->bitwiseOrExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->AND().size())
          throw std::runtime_error("AstBuilderVisitor::visitLogicalAndExpressionnullptr");
        return ctx->AND(i);
      });
}

std::any AstBuilderVisitor::visitBitwiseOrExpression(LangParser::BitwiseOrExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBitwiseOrExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->bitwiseXorExp().size(); },
      [&](size_t i) { return ctx->bitwiseXorExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->BIT_OR().size())
          throw std::runtime_error("AstBuilderVisitor::visitBitwiseOrExpressionnullptr");
        return ctx->BIT_OR(i);
      });
}

std::any
AstBuilderVisitor::visitBitwiseXorExpression(LangParser::BitwiseXorExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBitwiseXorExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->bitwiseAndExp().size(); },
      [&](size_t i) { return ctx->bitwiseAndExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->BIT_XOR().size())
          throw std::runtime_error("AstBuilderVisitor::visitBitwiseXorExpressionnullptr");
        return ctx->BIT_XOR(i);
      });
}

std::any
AstBuilderVisitor::visitBitwiseAndExpression(LangParser::BitwiseAndExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitBitwiseAndExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->equalityExp().size(); },
      [&](size_t i) { return ctx->equalityExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->BIT_AND().size())
          throw std::runtime_error("AstBuilderVisitor::visitBitwiseAndExpressionnullptr");
        return ctx->BIT_AND(i);
      });
}

std::any AstBuilderVisitor::visitEqualityExpression(LangParser::EqualityExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitEqualityExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->comparisonExp().size(); },
      [&](size_t i) { return ctx->comparisonExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->equalityExpOp().size())
          throw std::runtime_error("AstBuilderVisitor::visitEqualityExpressionnullptr");
        auto opCtx = ctx->equalityExpOp(i);
        if (!opCtx || opCtx->children.empty() || !opCtx->children[0])
          throw std::runtime_error("AstBuilderVisitor::visitEqualityExpressionnullptr");

        auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(opCtx->children[0]);
        if (!termNode)
          throw std::runtime_error("AstBuilderVisitor::visitEqualityExpressionnullptr");
        return termNode;
      });
}

std::any
AstBuilderVisitor::visitComparisonExpression(LangParser::ComparisonExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitComparisonExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->shiftExp().size(); }, [&](size_t i) { return ctx->shiftExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->comparisonExpOp().size())
          throw std::runtime_error("AstBuilderVisitor::visitComparisonExpressionnullptr");
        auto opCtx = ctx->comparisonExpOp(i);
        if (!opCtx || opCtx->children.empty() || !opCtx->children[0])
          throw std::runtime_error("AstBuilderVisitor::visitComparisonExpressionnullptr");
        auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(opCtx->children[0]);
        if (!termNode)
          throw std::runtime_error("AstBuilderVisitor::visitComparisonExpressionnullptr");
        return termNode;
      });
}

std::any AstBuilderVisitor::visitShiftExpression(LangParser::ShiftExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitShiftExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->concatExp().size(); }, [&](size_t i) { return ctx->concatExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->shiftExpOp().size())
          throw std::runtime_error("AstBuilderVisitor::visitShiftExpressionnullptr");
        auto opCtx = ctx->shiftExpOp(i);
        if (!opCtx || opCtx->children.empty() || !opCtx->children[0])
          throw std::runtime_error("AstBuilderVisitor::visitShiftExpressionnullptr");
        auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(opCtx->children[0]);
        if (!termNode)
          throw std::runtime_error("AstBuilderVisitor::visitShiftExpressionnullptr");
        if (!termNode->getSymbol())
          throw std::runtime_error("AstBuilderVisitor::visitShiftExpressionnullptr");
        if (termNode->getSymbol()->getType() == LangParser::GT)
          isRShift = true;
        return termNode;
      });
}

std::any AstBuilderVisitor::visitConcatExpression(LangParser::ConcatExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitConcatExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->addSubExp().size(); }, [&](size_t i) { return ctx->addSubExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->CONCAT().size())
          throw std::runtime_error("AstBuilderVisitor::visitConcatExpressionnullptr");
        return ctx->CONCAT(i);
      });
}

std::any AstBuilderVisitor::visitAddSubExpression(LangParser::AddSubExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitAddSubExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->mulDivModExp().size(); },
      [&](size_t i) { return ctx->mulDivModExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->addSubExpOp().size())
          throw std::runtime_error("AstBuilderVisitor::visitAddSubExpressionnullptr");
        auto opCtx = ctx->addSubExpOp(i);
        if (!opCtx || opCtx->children.empty() || !opCtx->children[0])
          throw std::runtime_error("AstBuilderVisitor::visitAddSubExpressionnullptr");
        auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(opCtx->children[0]);
        if (!termNode)
          throw std::runtime_error("AstBuilderVisitor::visitAddSubExpressionnullptr");
        return termNode;
      });
}

std::any AstBuilderVisitor::visitMulDivModExpression(LangParser::MulDivModExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMulDivModExpressionnullptr");
  return visitBinaryExpressionRawPtr(
      ctx, [&]() { return ctx->unaryExp().size(); }, [&](size_t i) { return ctx->unaryExp(i); },
      [&](size_t i, bool &isRShift) {
        if (i >= ctx->mulDivModExpOp().size())
          throw std::runtime_error("AstBuilderVisitor::visitMulDivModExpressionnullptr");
        auto opCtx = ctx->mulDivModExpOp(i);
        if (!opCtx || opCtx->children.empty() || !opCtx->children[0])
          throw std::runtime_error("AstBuilderVisitor::visitMulDivModExpressionnullptr");
        auto termNode = dynamic_cast<antlr4::tree::TerminalNode *>(opCtx->children[0]);
        if (!termNode)
          throw std::runtime_error("AstBuilderVisitor::visitMulDivModExpressionnullptr");
        return termNode;
      });
}

std::any AstBuilderVisitor::visitUnaryPrefix(LangParser::UnaryPrefixContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitUnaryPrefixnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  if (!ctx->unaryExp())
    throw std::runtime_error("AstBuilderVisitor::visitUnaryPrefixnullptr");
  std::any operandResult = visit(ctx->unaryExp());
  AstNode *operandRaw = safeAnyCastRawPtr<AstNode>(operandResult, "visitUnaryPrefix > operand");
  Expression *operand = dynamic_cast<Expression *>(operandRaw);
  if (!operand && operandResult.has_value()) {
    delete operandRaw;
    throw std::runtime_error("一元操作符的操作数必须是表达式");
  }
  if (!operand)
    return std::any();

  OperatorKind op;

  if (ctx->NOT())
    op = OperatorKind::NOT;
  else if (ctx->SUB())
    op = OperatorKind::NEGATE;
  else if (ctx->LEN())
    op = OperatorKind::LENGTH;
  else if (ctx->BIT_NOT())
    op = OperatorKind::BW_NOT;
  else {
    delete operand;
    throw std::runtime_error("未知的一元前缀操作符在行 " + std::to_string(loc.line));
  }

  UnaryOpNode *node = new UnaryOpNode(op, operand, loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitUnaryPrefixnullptr");

  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitUnaryToPostfix(LangParser::UnaryToPostfixContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitUnaryToPostfixnullptr");
  if (!ctx->postfixExp())
    throw std::runtime_error("AstBuilderVisitor::visitUnaryToPostfixnullptr");
  return visit(ctx->postfixExp());
}

std::any AstBuilderVisitor::visitPostfixExpression(LangParser::PostfixExpressionContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  if (!ctx->primaryExp())
    throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

  std::any primaryResult = visit(ctx->primaryExp());
  AstNode *currentExprRaw =
      safeAnyCastRawPtr<AstNode>(primaryResult, "visitPostfixExpression > primary");
  Expression *currentExpr = dynamic_cast<Expression *>(currentExprRaw);
  if (!currentExpr && primaryResult.has_value()) {
    delete currentExprRaw;
    throw std::runtime_error("后缀表达式的基础必须是表达式");
  }
  if (!currentExpr)
    return std::any();

  for (auto suffixCtx : ctx->postfixSuffix()) {
    if (!suffixCtx)
      throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

    SourceLocation suffixLoc = getSourceLocation(suffixCtx);

    try {
      if (auto indexCtx = dynamic_cast<LangParser::PostfixIndexSuffixContext *>(suffixCtx)) {
        if (!indexCtx->expression())
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        std::any indexResult = visit(indexCtx->expression());
        AstNode *indexExprRaw =
            safeAnyCastRawPtr<AstNode>(indexResult, "visitPostfixExpression > index");
        Expression *indexExpr = dynamic_cast<Expression *>(indexExprRaw);
        if (!indexExpr && indexResult.has_value()) {
          delete currentExpr;
          delete indexExprRaw;
          throw std::runtime_error("索引必须是表达式");
        }
        if (!indexExpr) {
          delete currentExpr;
          throw std::runtime_error("索引访问失败");
        }
        IndexAccessNode *newNode = new IndexAccessNode(currentExpr, indexExpr, suffixLoc);
        if (!newNode)
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        currentExpr = newNode;
      } else if (auto memberCtx =
                     dynamic_cast<LangParser::PostfixMemberSuffixContext *>(suffixCtx)) {
        if (!memberCtx->IDENTIFIER())
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        MemberAccessNode *newNode =
            new MemberAccessNode(currentExpr, memberCtx->IDENTIFIER()->getText(), suffixLoc);
        if (!newNode)
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        currentExpr = newNode;
      } else if (auto colonCtx =
                     dynamic_cast<LangParser::PostfixColonLookupSuffixContext *>(suffixCtx)) {
        if (!colonCtx->IDENTIFIER())
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        MemberLookupNode *newNode =
            new MemberLookupNode(currentExpr, colonCtx->IDENTIFIER()->getText(), suffixLoc);
        if (!newNode)
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        currentExpr = newNode;
      } else if (auto callCtx = dynamic_cast<LangParser::PostfixCallSuffixContext *>(suffixCtx)) {
        std::vector<Expression *> args;
        if (callCtx->arguments()) {
          auto argsCtx = callCtx->arguments();
          if (!argsCtx)
            throw std::runtime_error("AstBuilderVisitor::"
                                     "visitPostfixExpressionnullptr");
          if (argsCtx->expressionList()) {
            auto exprListCtx = argsCtx->expressionList();
            if (!exprListCtx)
              throw std::runtime_error("AstBuilderVisitor::"
                                       "visitPostfixExpressionnullptr");
            for (auto argExprCtx : exprListCtx->expression()) {
              if (!argExprCtx)
                throw std::runtime_error("AstBuilderVisitor::"
                                         "visitPostfixExpressionnullptr");

              std::any argResult = visit(argExprCtx);
              AstNode *argNodeRaw =
                  safeAnyCastRawPtr<AstNode>(argResult, "visitPostfixExpression > call > arg");
              Expression *argNode = dynamic_cast<Expression *>(argNodeRaw);
              if (!argNode && argResult.has_value()) {
                delete currentExpr;
                deleteVectorItems(args);
                delete argNodeRaw;
                throw std::runtime_error("函数调用的参数必须是表达式");
              }
              if (!argNode) {
                delete currentExpr;
                deleteVectorItems(args);
                throw std::runtime_error("函数参数访问失败");
              }
              args.push_back(argNode);
            }
          }
        }
        FunctionCallNode *newNode = new FunctionCallNode(currentExpr, std::move(args), suffixLoc);
        if (!newNode)
          throw std::runtime_error("AstBuilderVisitor::visitPostfixExpressionnullptr");

        currentExpr = newNode;
      } else {
        delete currentExpr;
        throw std::runtime_error("未知的后缀操作符在行 " + std::to_string(suffixLoc.line));
      }
    } catch (...) {
      delete currentExpr;

      throw;
    }
  }
  return std::any(static_cast<AstNode *>(currentExpr));
}

std::any AstBuilderVisitor::visitPrimaryAtom(LangParser::PrimaryAtomContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryAtomnullptr");
  if (!ctx->atomexp())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryAtomnullptr");
  return visit(ctx->atomexp());
}

std::any AstBuilderVisitor::visitPrimaryListLiteral(LangParser::PrimaryListLiteralContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryListLiteralnullptr");
  if (!ctx->listExpression())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryListLiteralnullptr");
  return visit(ctx->listExpression());
}

std::any AstBuilderVisitor::visitPrimaryMapLiteral(LangParser::PrimaryMapLiteralContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryMapLiteralnullptr");
  if (!ctx->mapExpression())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryMapLiteralnullptr");
  return visit(ctx->mapExpression());
}

std::any AstBuilderVisitor::visitPrimaryIdentifier(LangParser::PrimaryIdentifierContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryIdentifiernullptr");
  if (!ctx->IDENTIFIER())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryIdentifiernullptr");
  SourceLocation loc = getSourceLocation(ctx);
  IdentifierNode *node = new IdentifierNode(ctx->IDENTIFIER()->getText(), loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryIdentifiernullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitPrimaryVarArgs(LangParser::PrimaryVarArgsContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryVarArgsnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  VarArgsNode *node = new VarArgsNode(loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryVarArgsnullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitPrimaryParenExp(LangParser::PrimaryParenExpContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryParenExpnullptr");
  if (!ctx->expression())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryParenExpnullptr");
  return visit(ctx->expression());
}

std::any AstBuilderVisitor::visitPrimaryNew(LangParser::PrimaryNewContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryNewnullptr");
  if (!ctx->newExp())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryNewnullptr");
  return visit(ctx->newExp());
}

std::any AstBuilderVisitor::visitPrimaryLambda(LangParser::PrimaryLambdaContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryLambdanullptr");
  if (!ctx->lambdaExpression())
    throw std::runtime_error("AstBuilderVisitor::visitPrimaryLambdanullptr");
  return visit(ctx->lambdaExpression());
}

std::any AstBuilderVisitor::visitAtomexp(LangParser::AtomexpContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitAtomexpnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *node = nullptr;
  if (ctx->INTEGER()) {
    try {
      long long val = 0;
      std::string text = ctx->INTEGER()->getText();
      if (text.length() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X'))
        val = std::stoll(text, nullptr, 16);
      else
        val = std::stoll(text, nullptr, 10);
      node = new LiteralIntNode(val, loc);
    } catch (const std::exception &e) {
      throw std::runtime_error("无效或越界的整数常量 '" + ctx->INTEGER()->getText() + "' 在行 " +
                               std::to_string(loc.line));
    }
  } else if (ctx->FLOAT_LITERAL()) {
    try {
      double val = std::stod(ctx->FLOAT_LITERAL()->getText());
      node = new LiteralFloatNode(val, loc);
    } catch (const std::exception &e) {
      throw std::runtime_error("无效或越界的浮点常量 '" + ctx->FLOAT_LITERAL()->getText() +
                               "' 在行 " + std::to_string(loc.line));
    }
  } else if (ctx->STRING_LITERAL()) {
    std::string processed_string = processStringLiteral(ctx->STRING_LITERAL()->getText());
    node = new LiteralStringNode(std::move(processed_string), loc);
  } else if (ctx->TRUE()) {
    node = new LiteralBoolNode(true, loc);
  } else if (ctx->FALSE()) {
    node = new LiteralBoolNode(false, loc);
  } else if (ctx->NULL_()) {
    node = new LiteralNullNode(loc);
  } else
    throw std::runtime_error("未知的原子表达式类型在行 " + std::to_string(loc.line));

  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitAtomexpnullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitListLiteralDef(LangParser::ListLiteralDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitListLiteralDefnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  std::vector<Expression *> elements;
  try {
    if (ctx->expressionList()) {
      auto exprListCtx = ctx->expressionList();
      if (!exprListCtx)
        throw std::runtime_error("AstBuilderVisitor::visitListLiteralDefnullptr");

      for (auto exprCtx : exprListCtx->expression()) {
        if (!exprCtx)
          throw std::runtime_error("AstBuilderVisitor::visitListLiteralDefnullptr");

        std::any elemResult = visit(exprCtx);
        AstNode *elemNodeRaw =
            safeAnyCastRawPtr<AstNode>(elemResult, "visitListLiteralDef > element");
        Expression *elemNode = dynamic_cast<Expression *>(elemNodeRaw);
        if (!elemNode && elemResult.has_value()) {
          deleteVectorItems(elements);
          delete elemNodeRaw;
          throw std::runtime_error("列表元素必须是表达式");
        }
        if (!elemNode) {
          deleteVectorItems(elements);
          throw std::runtime_error("列表元素访问失败");
        }
        elements.push_back(elemNode);
      }
    }

    LiteralListNode *listNode = new LiteralListNode(std::move(elements), loc);
    if (!listNode)
      throw std::runtime_error("AstBuilderVisitor::visitListLiteralDefnullptr");
    return std::any(static_cast<AstNode *>(listNode));
  } catch (...) {
    deleteVectorItems(elements);
    throw;
  }
}

std::any AstBuilderVisitor::visitMapLiteralDef(LangParser::MapLiteralDefContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapLiteralDefnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  std::vector<MapEntryNode *> entries;
  try {
    if (ctx->mapEntryList()) {
      auto entryListCtx = ctx->mapEntryList();
      if (!entryListCtx)
        throw std::runtime_error("AstBuilderVisitor::visitMapLiteralDefnullptr");
      for (auto entryCtx : entryListCtx->mapEntry()) {
        if (!entryCtx)
          throw std::runtime_error("AstBuilderVisitor::visitMapLiteralDefnullptr");

        std::any entryResult = visit(entryCtx);
        AstNode *entryNodeRaw =
            safeAnyCastRawPtr<AstNode>(entryResult, "visitMapLiteralDef > entry");
        MapEntryNode *entryNode = dynamic_cast<MapEntryNode *>(entryNodeRaw);
        if (!entryNode && entryResult.has_value()) {
          deleteVectorItems(entries);
          delete entryNodeRaw;
          throw std::runtime_error("Map 元素必须是 Entry 类型");
        }
        if (!entryNode) {
          deleteVectorItems(entries);
          throw std::runtime_error("Map entry 访问失败");
        }
        entries.push_back(entryNode);
      }
    }

    LiteralMapNode *mapNode = new LiteralMapNode(std::move(entries), loc);
    if (!mapNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapLiteralDefnullptr");
    return std::any(static_cast<AstNode *>(mapNode));
  } catch (...) {
    deleteVectorItems(entries);
    throw;
  }
}

std::any AstBuilderVisitor::visitMapEntryIdentKey(LangParser::MapEntryIdentKeyContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapEntryIdentKeynullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *keyNode = nullptr;
  Expression *valueNode = nullptr;
  try {
    if (!ctx->IDENTIFIER())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryIdentKeynullptr");
    keyNode =
        new LiteralStringNode(ctx->IDENTIFIER()->getText(), getSourceLocation(ctx->IDENTIFIER()));
    if (!keyNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryIdentKeynullptr");

    if (!ctx->expression())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryIdentKeynullptr");

    std::any valResult = visit(ctx->expression());
    AstNode *valueNodeRaw = safeAnyCastRawPtr<AstNode>(valResult, "visitMapEntryIdentKey > value");
    valueNode = dynamic_cast<Expression *>(valueNodeRaw);
    if (!valueNode && valResult.has_value()) {
      delete keyNode;
      delete valueNodeRaw;
      throw std::runtime_error("Map 值必须是表达式");
    }
    if (!valueNode) {
      delete keyNode;
      throw std::runtime_error("Map 值访问失败");
    }

    MapEntryNode *entryNode = new MapEntryNode(keyNode, valueNode, loc);
    if (!entryNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryIdentKeynullptr");
    return std::any(static_cast<AstNode *>(entryNode));
  } catch (...) {
    delete keyNode;
    delete valueNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitMapEntryExprKey(LangParser::MapEntryExprKeyContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapEntryExprKeynullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *keyNode = nullptr;
  Expression *valueNode = nullptr;
  try {
    if (!ctx->expression(0))
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryExprKeynullptr");

    std::any keyResult = visit(ctx->expression(0));
    AstNode *keyNodeRaw = safeAnyCastRawPtr<AstNode>(keyResult, "visitMapEntryExprKey > key");
    keyNode = dynamic_cast<Expression *>(keyNodeRaw);
    if (!keyNode && keyResult.has_value()) {
      delete keyNodeRaw;
      throw std::runtime_error("Map 键必须是表达式");
    }
    if (!keyNode)
      throw std::runtime_error("Map 键访问失败");

    if (!ctx->expression(1))
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryExprKeynullptr");

    std::any valResult = visit(ctx->expression(1));
    AstNode *valueNodeRaw = safeAnyCastRawPtr<AstNode>(valResult, "visitMapEntryExprKey > value");
    valueNode = dynamic_cast<Expression *>(valueNodeRaw);
    if (!valueNode && valResult.has_value()) {
      delete keyNode;
      delete valueNodeRaw;
      throw std::runtime_error("Map 值必须是表达式");
    }
    if (!valueNode) {
      delete keyNode;
      throw std::runtime_error("Map 值访问失败");
    }

    MapEntryNode *entryNode = new MapEntryNode(keyNode, valueNode, loc);
    if (!entryNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryExprKeynullptr");
    return std::any(static_cast<AstNode *>(entryNode));
  } catch (...) {
    delete keyNode;
    delete valueNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitMapEntryStringKey(LangParser::MapEntryStringKeyContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapEntryStringKeynullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *keyNode = nullptr;
  Expression *valueNode = nullptr;
  try {
    if (!ctx->STRING_LITERAL())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryStringKeynullptr");

    std::string processed_key = processStringLiteral(ctx->STRING_LITERAL()->getText());
    keyNode =
        new LiteralStringNode(std::move(processed_key), getSourceLocation(ctx->STRING_LITERAL()));
    if (!keyNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryStringKeynullptr");

    if (!ctx->expression())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryStringKeynullptr");

    std::any valResult = visit(ctx->expression());
    AstNode *valueNodeRaw = safeAnyCastRawPtr<AstNode>(valResult, "visitMapEntryStringKey > value");
    valueNode = dynamic_cast<Expression *>(valueNodeRaw);
    if (!valueNode && valResult.has_value()) {
      delete keyNode;
      delete valueNodeRaw;
      throw std::runtime_error("Map 值必须是表达式");
    }
    if (!valueNode) {
      delete keyNode;
      throw std::runtime_error("Map 值访问失败");
    }

    MapEntryNode *entryNode = new MapEntryNode(keyNode, valueNode, loc);
    if (!entryNode)
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryStringKeynullptr");
    return std::any(static_cast<AstNode *>(entryNode));
  } catch (...) {
    delete keyNode;
    delete valueNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitMapEntryIntKey(LangParser::MapEntryIntKeyContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapEntryIntKey nullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *keyNode = nullptr;
  Expression *valueNode = nullptr;
  try {
    if (!ctx->INTEGER())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryIntKey no INTEGER token");

    // Create a string key for shorthand syntax: {1:"one"} becomes {"1":"one"}
    std::string text = ctx->INTEGER()->getText();
    keyNode = new LiteralStringNode(text, getSourceLocation(ctx->INTEGER()));

    if (!ctx->expression())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryIntKey no expression");

    std::any valResult = visit(ctx->expression());
    AstNode *valueNodeRaw = safeAnyCastRawPtr<AstNode>(valResult, "visitMapEntryIntKey > value");
    valueNode = dynamic_cast<Expression *>(valueNodeRaw);
    if (!valueNode && valResult.has_value()) {
      delete keyNode;
      delete valueNodeRaw;
      throw std::runtime_error("Map 值必须是表达式");
    }
    if (!valueNode) {
      delete keyNode;
      throw std::runtime_error("Map 值访问失败");
    }

    MapEntryNode *entryNode = new MapEntryNode(keyNode, valueNode, loc);
    return std::any(static_cast<AstNode *>(entryNode));
  } catch (...) {
    delete keyNode;
    delete valueNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitMapEntryFloatKey(LangParser::MapEntryFloatKeyContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitMapEntryFloatKey nullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *keyNode = nullptr;
  Expression *valueNode = nullptr;
  try {
    if (!ctx->FLOAT_LITERAL())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryFloatKey no FLOAT_LITERAL token");

    // Create a string key for shorthand syntax: {1.5:"one"} becomes {"1.5":"one"}
    std::string text = ctx->FLOAT_LITERAL()->getText();
    keyNode = new LiteralStringNode(text, getSourceLocation(ctx->FLOAT_LITERAL()));

    if (!ctx->expression())
      throw std::runtime_error("AstBuilderVisitor::visitMapEntryFloatKey no expression");

    std::any valResult = visit(ctx->expression());
    AstNode *valueNodeRaw = safeAnyCastRawPtr<AstNode>(valResult, "visitMapEntryFloatKey > value");
    valueNode = dynamic_cast<Expression *>(valueNodeRaw);
    if (!valueNode && valResult.has_value()) {
      delete keyNode;
      delete valueNodeRaw;
      throw std::runtime_error("Map 值必须是表达式");
    }
    if (!valueNode) {
      delete keyNode;
      throw std::runtime_error("Map 值访问失败");
    }

    MapEntryNode *entryNode = new MapEntryNode(keyNode, valueNode, loc);
    return std::any(static_cast<AstNode *>(entryNode));
  } catch (...) {
    delete keyNode;
    delete valueNode;
    throw;
  }
}

std::any AstBuilderVisitor::visitNewExpressionDef(LangParser::NewExpressionDefContext *ctx) {
  if (!ctx) {
    throw std::runtime_error("AstBuilderVisitor::visitNewExpressionDef nullptr context");
  }
  SourceLocation loc = getSourceLocation(ctx);
  UserType *classType = nullptr;
  std::vector<Expression *> args;

  try {

    auto qualIdentCtx = ctx->qualifiedIdentifier();
    if (!qualIdentCtx) {
      throw std::runtime_error("AstBuilderVisitor::visitNewExpressionDef "
                               "nullptr qualifiedIdentifier child");
    }
    SourceLocation typeLoc = getSourceLocation(qualIdentCtx);
    std::vector<std::string> nameParts;
    for (antlr4::tree::TerminalNode *terminalNode : qualIdentCtx->IDENTIFIER()) {
      if (!terminalNode) {
        throw std::runtime_error("New 表达式的类名限定标识符中发现空的 TerminalNode");
      }
      nameParts.push_back(terminalNode->getText());
    }
    if (nameParts.empty()) {
      throw std::runtime_error("无法从 New 表达式的类名限定标识符中提取名称部分");
    }

    classType = new UserType(std::move(nameParts), typeLoc);
    if (!classType) {
      throw std::runtime_error("为 New 表达式创建 UserType 节点失败");
    }

    if (ctx->arguments()) {
      auto argsCtx = ctx->arguments();
      if (!argsCtx)
        throw std::runtime_error("AstBuilderVisitor::visitNewExpressionDef nullptr arguments");
      if (argsCtx->expressionList()) {
        auto exprListCtx = argsCtx->expressionList();
        if (!exprListCtx)
          throw std::runtime_error("AstBuilderVisitor::visitNewExpressionDef "
                                   "nullptr expressionList");
        for (auto argExprCtx : exprListCtx->expression()) {
          if (!argExprCtx) {
            delete classType;
            throw std::runtime_error("New 参数列表中存在空表达式");
          }
          std::any argResult = visit(argExprCtx);
          AstNode *argNodeRaw =
              safeAnyCastRawPtr<AstNode>(argResult, "visitNewExpressionDef > argument");
          Expression *argNode = dynamic_cast<Expression *>(argNodeRaw);
          if (!argNode && argResult.has_value()) {
            delete classType;
            deleteVectorItems(args);
            delete argNodeRaw;
            throw std::runtime_error("New 参数必须是表达式");
          }
          if (!argNode) {
            delete classType;
            deleteVectorItems(args);
            throw std::runtime_error("New 参数访问失败");
          }
          args.push_back(argNode);
        }
      }
    }

    NewExpressionNode *newNode = new NewExpressionNode(classType, std::move(args), loc);
    if (!newNode) {

      delete classType;
      deleteVectorItems(args);
      throw std::runtime_error("创建 NewExpressionNode 失败");
    }
    return std::any(static_cast<AstNode *>(newNode));

  } catch (...) {

    delete classType;
    deleteVectorItems(args);
    throw;
  }
}

std::any AstBuilderVisitor::visitIfStatement(LangParser::IfStatementContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *mainCondition = nullptr;
  BlockNode *mainThenBlock = nullptr;
  std::vector<IfClauseNode *> elseIfClauses;
  BlockNode *elseBlock = nullptr;

  try {
    if (!ctx->expression(0))
      throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");
    std::any mainCondResult = visit(ctx->expression(0));
    AstNode *mainCondRaw =
        safeAnyCastRawPtr<AstNode>(mainCondResult, "visitIfStatement > main condition");
    mainCondition = dynamic_cast<Expression *>(mainCondRaw);
    if (!mainCondition && mainCondResult.has_value()) {
      delete mainCondRaw;
      throw std::runtime_error("If 条件必须是表达式");
    }
    if (!mainCondition)
      throw std::runtime_error("If 条件访问失败");

    if (!ctx->blockStatement(0))
      throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");

    std::any mainThenResult = visit(ctx->blockStatement(0));
    AstNode *mainThenRaw =
        safeAnyCastRawPtr<AstNode>(mainThenResult, "visitIfStatement > main then block");
    mainThenBlock = dynamic_cast<BlockNode *>(mainThenRaw);
    if (!mainThenBlock && mainThenResult.has_value()) {
      delete mainCondition;
      delete mainThenRaw;
      throw std::runtime_error("If 体必须是代码块");
    }
    if (!mainThenBlock) {
      delete mainCondition;
      throw std::runtime_error("If 体访问失败");
    }

    size_t exprIndex = 1;
    size_t blockIndex = 1;
    size_t numElseIfs = ctx->IF().size() - 1;
    size_t numElse = ctx->ELSE().size() - numElseIfs;

    for (size_t i = 0; i < numElseIfs; ++i) {
      if (i >= ctx->ELSE().size() || !ctx->ELSE(i))
        throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");
      SourceLocation elseIfLoc = getSourceLocation(ctx->ELSE(i));

      if (exprIndex >= ctx->expression().size() || !ctx->expression(exprIndex))
        throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");

      std::any elseIfCondResult = visit(ctx->expression(exprIndex));
      AstNode *elseIfCondRaw =
          safeAnyCastRawPtr<AstNode>(elseIfCondResult, "visitIfStatement > elseif condition");
      Expression *elseIfCondition = dynamic_cast<Expression *>(elseIfCondRaw);
      if (!elseIfCondition && elseIfCondResult.has_value()) {
        throw std::runtime_error("Else If 条件必须是表达式");
      }
      if (!elseIfCondition) {
        throw std::runtime_error("Else If 条件访问失败");
      }

      if (blockIndex >= ctx->blockStatement().size() || !ctx->blockStatement(blockIndex))
        throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");

      std::any elseIfBlockResult = visit(ctx->blockStatement(blockIndex));
      AstNode *elseIfBlockRaw =
          safeAnyCastRawPtr<AstNode>(elseIfBlockResult, "visitIfStatement > elseif block");
      BlockNode *elseIfBlock = dynamic_cast<BlockNode *>(elseIfBlockRaw);
      if (!elseIfBlock && elseIfBlockResult.has_value()) {
        throw std::runtime_error("Else If 体必须是代码块");
      }
      if (!elseIfBlock) {
        throw std::runtime_error("Else If 体访问失败");
      }

      IfClauseNode *elseIfNode = new IfClauseNode(elseIfCondition, elseIfBlock, elseIfLoc);
      if (!elseIfNode)
        throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");
      elseIfClauses.push_back(elseIfNode);

      exprIndex++;
      blockIndex++;
    }

    if (numElse > 0) {
      if (blockIndex >= ctx->blockStatement().size() || !ctx->blockStatement(blockIndex))
        throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");

      std::any elseBlockResult = visit(ctx->blockStatement(blockIndex));
      AstNode *elseBlockRaw =
          safeAnyCastRawPtr<AstNode>(elseBlockResult, "visitIfStatement > else block");
      elseBlock = dynamic_cast<BlockNode *>(elseBlockRaw);
      if (!elseBlock && elseBlockResult.has_value()) {
        delete elseBlockRaw;
      }
    }

    IfStatementNode *ifNode =
        new IfStatementNode(mainCondition, mainThenBlock, std::move(elseIfClauses), elseBlock, loc);
    if (!ifNode)
      throw std::runtime_error("AstBuilderVisitor::visitIfStatementnullptr");
    return std::any(static_cast<AstNode *>(ifNode));

  } catch (...) {

    delete mainCondition;
    delete mainThenBlock;
    deleteVectorItems(elseIfClauses);
    delete elseBlock;
    throw;
  }
}

std::any AstBuilderVisitor::visitWhileStatement(LangParser::WhileStatementContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitWhileStatementnullptr");
  SourceLocation loc = getSourceLocation(ctx);
  Expression *condition = nullptr;
  BlockNode *body = nullptr;
  try {
    if (!ctx->expression())
      throw std::runtime_error("AstBuilderVisitor::visitWhileStatementnullptr");

    std::any condResult = visit(ctx->expression());
    AstNode *condRaw = safeAnyCastRawPtr<AstNode>(condResult, "visitWhileStatement > condition");
    condition = dynamic_cast<Expression *>(condRaw);
    if (!condition && condResult.has_value()) {
      delete condRaw;
      throw std::runtime_error("While 条件必须是表达式");
    }
    if (!condition)
      throw std::runtime_error("While 条件访问失败");

    if (!ctx->blockStatement())
      throw std::runtime_error("AstBuilderVisitor::visitWhileStatementnullptr");
    std::any bodyResult = visit(ctx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(bodyResult, "visitWhileStatement > body");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body && bodyResult.has_value()) {
      delete condition;
      delete bodyRaw;
      throw std::runtime_error("While 体必须是代码块");
    }
    if (!body) {
      delete condition;
      throw std::runtime_error("While 体访问失败");
    }

    WhileStatementNode *whileNode = new WhileStatementNode(condition, body, loc);
    if (!whileNode)
      throw std::runtime_error("AstBuilderVisitor::visitWhileStatementnullptr");
    return std::any(static_cast<AstNode *>(whileNode));
  } catch (...) {
    delete condition;
    delete body;
    throw;
  }
}

std::any AstBuilderVisitor::visitForStatement(LangParser::ForStatementContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForStatementnullptr");
  if (ctx->forControl() == nullptr)
    throw std::runtime_error("forControl 访问失败");

  if (!ctx->forControl())
    throw std::runtime_error("AstBuilderVisitor::visitForStatementnullptr");
  return visit(ctx->forControl());
}

// ===== ForNumericControl: for (type i = start, end, step) { ... } =====
std::any AstBuilderVisitor::visitForNumericControl(LangParser::ForNumericControlContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForNumericControl nullptr");

  auto forStmtCtx = dynamic_cast<LangParser::ForStatementContext *>(ctx->parent);
  if (!forStmtCtx)
    throw std::runtime_error("内部错误: ForNumericControl 缺少父节点 ForStatementContext");
  SourceLocation loc = getSourceLocation(forStmtCtx);

  AstType *typeAnn = nullptr;
  std::string varName;
  Expression *startExpr = nullptr;
  Expression *endExpr = nullptr;
  Expression *stepExpr = nullptr;
  BlockNode *body = nullptr;

  try {
    // 1. 处理循环变量 (forNumericVar)
    if (!ctx->forNumericVar())
      throw std::runtime_error("ForNumericControl 缺少循环变量");

    // visit forNumericVar 返回 pair<string, AstType*>
    std::any varResult = visit(ctx->forNumericVar());
    auto varInfo = std::any_cast<std::pair<std::string, AstType *>>(varResult);
    varName = std::move(varInfo.first);
    typeAnn = varInfo.second;

    // 2. 处理表达式列表: start, end[, step]
    auto exprs = ctx->expression();
    if (exprs.size() < 2)
      throw std::runtime_error("数值 for 循环至少需要 start 和 end 两个表达式");

    // start
    {
      std::any r = visit(exprs[0]);
      AstNode *raw = safeAnyCastRawPtr<AstNode>(r, "visitForNumericControl > start");
      startExpr = dynamic_cast<Expression *>(raw);
      if (!startExpr) {
        delete raw;
        throw std::runtime_error("数值 for 循环 start 必须是表达式");
      }
    }

    // end
    {
      std::any r = visit(exprs[1]);
      AstNode *raw = safeAnyCastRawPtr<AstNode>(r, "visitForNumericControl > end");
      endExpr = dynamic_cast<Expression *>(raw);
      if (!endExpr) {
        delete raw;
        throw std::runtime_error("数值 for 循环 end 必须是表达式");
      }
    }

    // step (可选)
    if (exprs.size() >= 3) {
      std::any r = visit(exprs[2]);
      AstNode *raw = safeAnyCastRawPtr<AstNode>(r, "visitForNumericControl > step");
      stepExpr = dynamic_cast<Expression *>(raw);
      if (!stepExpr) {
        delete raw;
        throw std::runtime_error("数值 for 循环 step 必须是表达式");
      }
    }

    // 3. 处理循环体
    if (!forStmtCtx->blockStatement())
      throw std::runtime_error("For 循环体 blockStatement 为空");
    std::any bodyResult = visit(forStmtCtx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(bodyResult, "visitForNumericControl > body");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      delete bodyRaw;
      throw std::runtime_error("For 体必须是代码块");
    }

    // 4. 创建节点
    ForNumericStatementNode *forNode = new ForNumericStatementNode(
        std::move(varName), typeAnn, startExpr, endExpr, stepExpr, body, loc);
    return std::any(static_cast<AstNode *>(forNode));

  } catch (...) {
    delete typeAnn;
    delete startExpr;
    delete endExpr;
    delete stepExpr;
    delete body;
    throw;
  }
}

// ===== ForNumericVar 分支 =====
std::any AstBuilderVisitor::visitForNumericVarTyped(LangParser::ForNumericVarTypedContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForNumericVarTyped nullptr");
  if (!ctx->IDENTIFIER())
    throw std::runtime_error("ForNumericVarTyped 缺少 IDENTIFIER");

  std::string name = ctx->IDENTIFIER()->getText();
  AstType *typeAnn = nullptr;

  if (ctx->type()) {
    std::any typeResult = visit(ctx->type());
    typeAnn = safeAnyCastRawPtr<AstType>(typeResult, "visitForNumericVarTyped > type");
  } else if (ctx->AUTO()) {
    typeAnn = new AutoType(getSourceLocation(ctx->AUTO()));
  }

  if (!typeAnn)
    throw std::runtime_error("ForNumericVarTyped 无法获取类型注解");

  return std::any(std::make_pair(std::move(name), typeAnn));
}

std::any
AstBuilderVisitor::visitForNumericVarUntyped(LangParser::ForNumericVarUntypedContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForNumericVarUntyped nullptr");
  if (!ctx->IDENTIFIER())
    throw std::runtime_error("ForNumericVarUntyped 缺少 IDENTIFIER");

  std::string name = ctx->IDENTIFIER()->getText();
  AstType *typeAnn = nullptr; // 无类型

  return std::any(std::make_pair(std::move(name), typeAnn));
}

// ===== ForEachControl: for (k, v : exprs) { ... } =====
std::any AstBuilderVisitor::visitForEachControl(LangParser::ForEachControlContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForEachControl nullptr");

  auto forStmtCtx = dynamic_cast<LangParser::ForStatementContext *>(ctx->parent);
  if (!forStmtCtx)
    throw std::runtime_error("内部错误: ForEachControl 缺少父节点 ForStatementContext");
  SourceLocation loc = getSourceLocation(forStmtCtx);

  std::vector<ParameterDeclNode *> loopVars;
  std::vector<Expression *> iterableExprs;
  BlockNode *body = nullptr;

  try {
    // 1. 处理循环变量 (forEachVar)
    auto varCtxs = ctx->forEachVar();
    if (varCtxs.empty())
      throw std::runtime_error("For-each 循环缺少循环变量");

    for (auto varCtx : varCtxs) {
      if (!varCtx)
        throw std::runtime_error("For-each 循环变量 context 为空");

      // visit forEachVar 返回 pair<string, AstType*>
      std::any varResult = visit(varCtx);
      auto varInfo = std::any_cast<std::pair<std::string, AstType *>>(varResult);

      SourceLocation itemLoc = getSourceLocation(varCtx);
      ParameterDeclNode *varDecl =
          new ParameterDeclNode(std::move(varInfo.first), varInfo.second, itemLoc);
      loopVars.push_back(varDecl);
    }

    // 2. 处理表达式列表
    if (!ctx->expressionList())
      throw std::runtime_error("For-each 缺少表达式列表");

    auto exprListCtx = ctx->expressionList();
    for (auto exprCtx : exprListCtx->expression()) {
      std::any iterResult = visit(exprCtx);
      AstNode *iterRaw = safeAnyCastRawPtr<AstNode>(iterResult, "visitForEachControl > expression");
      Expression *expr = dynamic_cast<Expression *>(iterRaw);
      if (!expr) {
        delete iterRaw;
        throw std::runtime_error("For-each 迭代源必须是表达式");
      }
      iterableExprs.push_back(expr);
    }

    if (iterableExprs.empty())
      throw std::runtime_error("For-each 至少需要一个迭代表达式");

    // 3. 处理循环体
    if (!forStmtCtx->blockStatement())
      throw std::runtime_error("For 循环体 blockStatement 为空");
    std::any bodyResult = visit(forStmtCtx->blockStatement());
    AstNode *bodyRaw = safeAnyCastRawPtr<AstNode>(bodyResult, "visitForEachControl > body");
    body = dynamic_cast<BlockNode *>(bodyRaw);
    if (!body) {
      delete bodyRaw;
      throw std::runtime_error("For 体必须是代码块");
    }

    // 4. 创建节点
    ForEachStatementNode *forNode =
        new ForEachStatementNode(std::move(loopVars), std::move(iterableExprs), body, loc);
    return std::any(static_cast<AstNode *>(forNode));

  } catch (...) {
    deleteVectorItems(loopVars);
    deleteVectorItems(iterableExprs);
    delete body;
    throw;
  }
}

// ===== ForEachVar 分支 =====
std::any AstBuilderVisitor::visitForEachVarTyped(LangParser::ForEachVarTypedContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForEachVarTyped nullptr");
  if (!ctx->IDENTIFIER())
    throw std::runtime_error("ForEachVarTyped 缺少 IDENTIFIER");

  std::string name = ctx->IDENTIFIER()->getText();
  AstType *typeAnn = nullptr;

  if (ctx->type()) {
    std::any typeResult = visit(ctx->type());
    typeAnn = safeAnyCastRawPtr<AstType>(typeResult, "visitForEachVarTyped > type");
  } else if (ctx->AUTO()) {
    typeAnn = new AutoType(getSourceLocation(ctx->AUTO()));
  }

  if (!typeAnn)
    throw std::runtime_error("ForEachVarTyped 无法获取类型注解");

  return std::any(std::make_pair(std::move(name), typeAnn));
}

std::any AstBuilderVisitor::visitForEachVarUntyped(LangParser::ForEachVarUntypedContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitForEachVarUntyped nullptr");
  if (!ctx->IDENTIFIER())
    throw std::runtime_error("ForEachVarUntyped 缺少 IDENTIFIER");

  std::string name = ctx->IDENTIFIER()->getText();
  AstType *typeAnn = nullptr; // 无类型

  return std::any(std::make_pair(std::move(name), typeAnn));
}

std::any AstBuilderVisitor::visitQualifiedIdentifier(LangParser::QualifiedIdentifierContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitQualifiedIdentifiernullptr");
  SourceLocation loc = getSourceLocation(ctx);

  IdentifierNode *node = new IdentifierNode(ctx->getText(), loc);
  if (!node)
    throw std::runtime_error("AstBuilderVisitor::visitQualifiedIdentifiernullptr");
  return std::any(static_cast<AstNode *>(node));
}

std::any
AstBuilderVisitor::visitTypeQualifiedIdentifier(LangParser::TypeQualifiedIdentifierContext *ctx) {
  if (!ctx) {
    throw std::runtime_error("AstBuilderVisitor::visitTypeQualifiedIdentifier nullptr context");
  }
  SourceLocation loc = getSourceLocation(ctx);

  auto qualIdentCtx = ctx->qualifiedIdentifier();
  if (!qualIdentCtx) {
    throw std::runtime_error("AstBuilderVisitor::visitTypeQualifiedIdentifier "
                             "nullptr qualifiedIdentifier child");
  }

  std::vector<std::string> nameParts;

  for (antlr4::tree::TerminalNode *terminalNode : qualIdentCtx->IDENTIFIER()) {
    if (!terminalNode) {

      throw std::runtime_error("限定标识符中发现空的 TerminalNode");
    }
    nameParts.push_back(terminalNode->getText());
  }

  if (nameParts.empty()) {
    throw std::runtime_error("无法从限定标识符中提取名称部分");
  }

  UserType *userTypeNode = new UserType(std::move(nameParts), loc);
  if (!userTypeNode) {

    throw std::runtime_error("创建 UserType 节点失败");
  }

  return std::any(static_cast<AstType *>(userTypeNode));
}

std::any AstBuilderVisitor::visitTypePrimitive(LangParser::TypePrimitiveContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitTypePrimitivenullptr");
  if (!ctx->primitiveType())
    throw std::runtime_error("AstBuilderVisitor::visitTypePrimitivenullptr");

  return visit(ctx->primitiveType());
}

std::any AstBuilderVisitor::visitTypeListType(LangParser::TypeListTypeContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitTypeListTypenullptr");
  if (!ctx->listType())
    throw std::runtime_error("AstBuilderVisitor::visitTypeListTypenullptr");

  return visit(ctx->listType());
}

std::any AstBuilderVisitor::visitTypeMap(LangParser::TypeMapContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitTypeMapnullptr");
  if (!ctx->mapType())
    throw std::runtime_error("AstBuilderVisitor::visitTypeMapnullptr");

  return visit(ctx->mapType());
}

std::any AstBuilderVisitor::visitParameter(LangParser::ParameterContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitParameternullptr");
  SourceLocation loc = getSourceLocation(ctx);
  AstType *typeAnn = nullptr;
  try {
    if (!ctx->type())
      throw std::runtime_error("AstBuilderVisitor::visitParameternullptr");
    std::any typeResult = visit(ctx->type());
    typeAnn = safeAnyCastRawPtr<AstType>(typeResult, "visitParameter > type");

    if (!typeAnn) {
      throw std::runtime_error("参数缺少类型注解或类型访问失败，在行 " + std::to_string(loc.line));
    }

    if (!ctx->IDENTIFIER())
      throw std::runtime_error("AstBuilderVisitor::visitParameternullptr");
    std::string name = ctx->IDENTIFIER()->getText();

    ParameterDeclNode *node = new ParameterDeclNode(std::move(name), typeAnn, loc);
    if (!node)
      throw std::runtime_error("AstBuilderVisitor::visitParameternullptr");

    return std::any(static_cast<AstNode *>(node));
  } catch (...) {

    delete typeAnn;
    throw;
  }
}

std::any AstBuilderVisitor::visitParameterList(LangParser::ParameterListContext *ctx) {
  if (!ctx)
    throw std::runtime_error("AstBuilderVisitor::visitParameterListnullptr");
  std::vector<ParameterDeclNode *> params;
  bool isVariadic = false;

  try {

    if (ctx->DDD() && ctx->parameter().empty()) {
      isVariadic = true;
    }

    else {

      for (auto paramCtx : ctx->parameter()) {
        if (!paramCtx)
          throw std::runtime_error("AstBuilderVisitor::visitParameterListnullptr");

        std::any paramResult = visit(paramCtx);
        AstNode *paramNodeRaw =
            safeAnyCastRawPtr<AstNode>(paramResult, "visitParameterList > parameter");
        ParameterDeclNode *paramNode = dynamic_cast<ParameterDeclNode *>(paramNodeRaw);
        if (!paramNode && paramResult.has_value()) {
          deleteVectorItems(params);
          delete paramNodeRaw;
          throw std::runtime_error("参数列表访问返回了非 ParameterDeclNode 类型");
        }
        if (paramNode) {
          params.push_back(paramNode);
        } else {
          deleteVectorItems(params);
          throw std::runtime_error("参数访问失败");
        }
      }

      if (ctx->DDD()) {
        isVariadic = true;
      }
    }

    return std::any(std::make_pair(std::move(params), isVariadic));

  } catch (...) {

    deleteVectorItems(params);
    throw;
  }
}

std::any AstBuilderVisitor::visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("visitImportNamespaceStmt nullptr context");
  SourceLocation loc = getSourceLocation(ctx);

  if (!ctx->MUL() || !ctx->AS() || !ctx->IDENTIFIER() || !ctx->FROM() || !ctx->STRING_LITERAL()) {
    throw std::runtime_error("Incomplete namespace import structure at line " +
                             std::to_string(loc.line));
  }

  std::string alias = ctx->IDENTIFIER()->getText();
  std::string path_literal = ctx->STRING_LITERAL()->getText();
  std::string path = processStringLiteral(path_literal);

  ImportNamespaceNode *node = new ImportNamespaceNode(std::move(alias), std::move(path), loc);
  if (!node)
    throw std::runtime_error("Failed to create ImportNamespaceNode");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitImportSpecifier(LangParser::ImportSpecifierContext *ctx) {
  if (!ctx)
    throw std::runtime_error("visitImportSpecifier nullptr context");
  SourceLocation loc = getSourceLocation(ctx);

  bool isTypeOnly = (ctx->TYPE() != nullptr);

  auto idNodes = ctx->IDENTIFIER();
  if (idNodes.empty() || !idNodes[0]) {
    throw std::runtime_error("Import specifier missing identifier at line " +
                             std::to_string(loc.line));
  }
  antlr4::tree::TerminalNode *originalNameNode = idNodes[0];
  std::string importedName = originalNameNode->getText();

  std::optional<std::string> aliasOpt = std::nullopt;
  if (ctx->AS()) {
    if (idNodes.size() > 1 && idNodes[1]) {
      antlr4::tree::TerminalNode *aliasNameNode = idNodes[1];
      aliasOpt = aliasNameNode->getText();
    } else {

      SourceLocation asLoc = getSourceLocation(ctx->AS());
      throw std::runtime_error("Syntax error: 'as' keyword requires an alias identifier at line " +
                               std::to_string(asLoc.line));
    }
  } else if (idNodes.size() > 1) {

    SourceLocation extraIdLoc = getSourceLocation(idNodes[1]);
    throw std::runtime_error("Internal parser error: unexpected second "
                             "identifier without 'as' keyword at line " +
                             std::to_string(extraIdLoc.line));
  }

  SourceLocation nodeLoc = getSourceLocation(originalNameNode);
  ImportSpecifierNode *node =
      new ImportSpecifierNode(std::move(importedName), std::move(aliasOpt), isTypeOnly, nodeLoc);
  if (!node)
    throw std::runtime_error("Failed to create ImportSpecifierNode");
  return std::any(static_cast<AstNode *>(node));
}

std::any AstBuilderVisitor::visitImportNamedStmt(LangParser::ImportNamedStmtContext *ctx) {
  if (!ctx)
    throw std::runtime_error("visitImportNamedStmt nullptr context");
  SourceLocation loc = getSourceLocation(ctx);

  if (!ctx->OCB() || !ctx->CCB() || !ctx->FROM() || !ctx->STRING_LITERAL()) {
    throw std::runtime_error("Incomplete named import structure at line " +
                             std::to_string(loc.line));
  }

  std::string path_literal = ctx->STRING_LITERAL()->getText();
  std::string path = processStringLiteral(path_literal);
  std::vector<ImportSpecifierNode *> specifiers;
  try {
    for (auto specCtx : ctx->importSpecifier()) {
      if (!specCtx)
        throw std::runtime_error("visitImportNamedStmt nullptr specifier context");
      std::any specResult = visit(specCtx);
      AstNode *specNodeRaw =
          safeAnyCastRawPtr<AstNode>(specResult, "visitImportNamedStmt > specifier");
      ImportSpecifierNode *specNode = dynamic_cast<ImportSpecifierNode *>(specNodeRaw);
      if (specNode) {
        specifiers.push_back(specNode);
      } else {
        if (specNodeRaw)
          delete specNodeRaw;
        throw std::runtime_error("Failed to visit import specifier at line " +
                                 std::to_string(getSourceLocation(specCtx).line));
      }
    }
    if (specifiers.empty() && !ctx->importSpecifier().empty()) {

      throw std::runtime_error("Failed to process one or more import specifiers at line " +
                               std::to_string(loc.line));
    }

    ImportNamedNode *node = new ImportNamedNode(std::move(specifiers), std::move(path), loc);
    if (!node)
      throw std::runtime_error("Failed to create ImportNamedNode");
    return std::any(static_cast<AstNode *>(node));
  } catch (...) {
    deleteVectorItems(specifiers);
    throw;
  }
}