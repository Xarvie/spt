/**
 * @file TolerantAstBuilderImpl.h
 * @brief TolerantAstBuilder Implementation - Layer 4 & 5 (Statements & Declarations)
 *
 * This file is included at the end of TolerantAstBuilder class definition.
 * It contains:
 * - Layer 4: Statements (block, control flow, assignment)
 * - Layer 5: Declarations (variable, function, class)
 *
 * @copyright Copyright (c) 2024-2025
 */

// ============================================================================
// LAYER 4: STATEMENTS
// ============================================================================

// ------------------------------------------------------------------------
// Block & Empty Statements
// ------------------------------------------------------------------------

std::any visitBlockStmt(LangParser::BlockStmtContext *ctx) override {
  if (!ctx || !ctx->blockStatement())
    return static_cast<Stmt *>(factory_.makeBlockStmt(getRange(ctx), {}));
  return visit(ctx->blockStatement());
}

std::any visitBlockStatement(LangParser::BlockStatementContext *ctx) override {
  if (!ctx)
    return static_cast<BlockStmtNode *>(factory_.makeBlockStmt(SourceRange::invalid(), {}));
  std::vector<Stmt *> stmts = visitStmtList(ctx->statement());
  return static_cast<BlockStmtNode *>(factory_.makeBlockStmt(getRange(ctx), stmts));
}

std::any visitSemicolonStmt(LangParser::SemicolonStmtContext *ctx) override {
  return static_cast<Stmt *>(factory_.makeEmptyStmt(getRange(ctx)));
}

// ------------------------------------------------------------------------
// Expression Statement
// ------------------------------------------------------------------------

std::any visitExpressionStmt(LangParser::ExpressionStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "missing expr"));
  Expr *expr = expectExpr(ctx->expression());
  return static_cast<Stmt *>(factory_.makeExprStmt(getRange(ctx), expr));
}

// ------------------------------------------------------------------------
// Assignment Statements
// ------------------------------------------------------------------------

/// Visit lvalue and return as expression
Expr *visitLValue(LangParser::LvalueContext *ctx) {
  if (!ctx)
    return factory_.makeErrorExpr(SourceRange::invalid(), "missing lvalue");

  // LvalueContext is base class, cast to LvalueBaseContext to access members
  auto *baseCtx = dynamic_cast<LangParser::LvalueBaseContext *>(ctx);
  if (!baseCtx) {
    return factory_.makeErrorExpr(getRange(ctx), "invalid lvalue");
  }

  Expr *result = nullptr;
  if (baseCtx->IDENTIFIER()) {
    result =
        factory_.makeIdentifier(getRange(baseCtx->IDENTIFIER()), baseCtx->IDENTIFIER()->getText());
  } else {
    result = factory_.makeErrorExpr(getRange(ctx), "missing identifier");
  }

  for (auto *suffix : baseCtx->lvalueSuffix()) {
    if (auto *idx = dynamic_cast<LangParser::LvalueIndexContext *>(suffix)) {
      Expr *index = expectExpr(idx->expression());
      auto range = SourceRange{result->range.begin, getRange(idx).end};
      result = factory_.makeIndexExpr(range, result, index);
    } else if (auto *mem = dynamic_cast<LangParser::LvalueMemberContext *>(suffix)) {
      // 使用 std::string 避免悬空引用（getText() 返回临时 string）
      std::string member = mem->IDENTIFIER() ? mem->IDENTIFIER()->getText() : "";
      auto range = SourceRange{result->range.begin, getRange(mem).end};
      result = factory_.makeMemberAccessExpr(range, result, member);
    }
  }
  return result;
}

std::any visitNormalAssignStmt(LangParser::NormalAssignStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid assign"));

  auto range = getRange(ctx);
  std::vector<Expr *> targets;
  for (auto *lv : ctx->lvalue())
    targets.push_back(visitLValue(lv));

  std::vector<Expr *> values;
  for (auto *e : ctx->expression())
    values.push_back(expectExpr(e));

  if (targets.size() == 1 && values.size() == 1) {
    return static_cast<Stmt *>(factory_.makeAssignStmt(range, targets[0], values[0]));
  }
  return static_cast<Stmt *>(factory_.makeMultiAssignStmt(range, targets, values));
}

std::any visitAssignStmt(LangParser::AssignStmtContext *ctx) override {
  if (!ctx || !ctx->assignStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid assign"));
  return visit(ctx->assignStatement());
}

std::any visitUpdateStmt(LangParser::UpdateStmtContext *ctx) override {
  if (!ctx || !ctx->updateStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid update"));
  return visit(ctx->updateStatement());
}

std::any visitUpdateAssignStmt(LangParser::UpdateAssignStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid update"));

  auto range = getRange(ctx);

  // UpdateAssignStmtContext itself has lvalue, op, expression members
  // Get lvalue
  Expr *target = nullptr;
  if (ctx->lvalue()) {
    target = visitLValue(ctx->lvalue());
  } else {
    target = factory_.makeErrorExpr(range, "missing lvalue");
  }

  // Get operator
  UpdateOp op = UpdateOp::Invalid;
  if (ctx->op) {
    op = mapUpdateOp(ctx->op->getType());
  }

  // Get value
  Expr *value = expectExpr(ctx->expression());

  return static_cast<Stmt *>(factory_.makeUpdateAssignStmt(range, op, target, value));
}

// ------------------------------------------------------------------------
// Control Flow Statements
// ------------------------------------------------------------------------

std::any visitIfStmt(LangParser::IfStmtContext *ctx) override {
  if (!ctx || !ctx->ifStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid if"));
  return visit(ctx->ifStatement());
}

std::any visitIfStatement(LangParser::IfStatementContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid if"));

  auto range = getRange(ctx);
  std::vector<IfStmtNode::Branch> branches;

  auto conditions = ctx->expression();
  auto bodies = ctx->blockStatement();

  for (size_t i = 0; i < conditions.size(); ++i) {
    IfStmtNode::Branch branch;
    branch.condition = expectExpr(conditions[i]);
    branch.conditionRange = getRange(conditions[i]);
    branch.body = (i < bodies.size()) ? expectBlock(bodies[i])
                                      : factory_.makeBlockStmt(SourceRange::invalid(), {});
    branches.push_back(branch);
  }

  BlockStmtNode *elseBody = nullptr;
  if (bodies.size() > conditions.size()) {
    elseBody = expectBlock(bodies.back());
  }

  return static_cast<Stmt *>(factory_.makeIfStmt(range, branches, elseBody));
}

std::any visitWhileStmt(LangParser::WhileStmtContext *ctx) override {
  if (!ctx || !ctx->whileStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid while"));
  return visit(ctx->whileStatement());
}

std::any visitWhileStatement(LangParser::WhileStatementContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid while"));
  Expr *cond = expectExpr(ctx->expression());
  BlockStmtNode *body = expectBlock(ctx->blockStatement());
  return static_cast<Stmt *>(factory_.makeWhileStmt(getRange(ctx), cond, body));
}

std::any visitForStmt(LangParser::ForStmtContext *ctx) override {
  if (!ctx || !ctx->forStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid for"));
  return visit(ctx->forStatement());
}

std::any visitForStatement(LangParser::ForStatementContext *ctx) override {
  if (!ctx || !ctx->forControl())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid for"));

  // Store context for body access
  currentForStmt_ = ctx;
  auto result = visit(ctx->forControl());
  currentForStmt_ = nullptr;
  return result;
}

// Numeric for: for (i = start, end, step) { body }
std::any visitForNumericControl(LangParser::ForNumericControlContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid numeric for"));

  VarDeclNode *var = nullptr;
  if (ctx->forNumericVar()) {
    var = expectVarDecl(ctx->forNumericVar());
  }

  Expr *start = expectExpr(ctx->expression(0));
  Expr *end = expectExpr(ctx->expression(1));
  Expr *step = nullptr;
  if (ctx->expression().size() > 2) {
    step = expectExpr(ctx->expression(2));
  }

  BlockStmtNode *body = nullptr;
  if (currentForStmt_ && currentForStmt_->blockStatement()) {
    body = expectBlock(currentForStmt_->blockStatement());
  } else {
    body = factory_.makeBlockStmt(SourceRange::invalid(), {});
  }

  auto range = currentForStmt_ ? getRange(currentForStmt_) : getRange(ctx);
  return static_cast<Stmt *>(factory_.makeForStmtNumeric(range, var, start, end, step, body));
}

// ForEach: for (k, v : iterator) { body }
std::any visitForEachControl(LangParser::ForEachControlContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid foreach"));

  std::vector<VarDeclNode *> iterVars;
  for (auto *fv : ctx->forEachVar()) {
    auto v = visit(fv);
    if (auto *vd = tryCast<VarDeclNode>(v)) {
      iterVars.push_back(vd);
    }
  }

  std::vector<Expr *> iterators;
  if (ctx->expressionList()) {
    for (auto *e : ctx->expressionList()->expression()) {
      iterators.push_back(expectExpr(e));
    }
  }

  BlockStmtNode *body = nullptr;
  if (currentForStmt_ && currentForStmt_->blockStatement()) {
    body = expectBlock(currentForStmt_->blockStatement());
  } else {
    body = factory_.makeBlockStmt(SourceRange::invalid(), {});
  }

  auto range = currentForStmt_ ? getRange(currentForStmt_) : getRange(ctx);
  // For generic for-each, collection is the first iterator expression
  Expr *collection = iterators.empty() ? nullptr : iterators[0];
  return static_cast<Stmt *>(factory_.makeForStmtForEach(range, iterVars, collection, body));
}

// For numeric variable (typed)
std::any visitForNumericVarTyped(LangParser::ForNumericVarTypedContext *ctx) override {
  if (!ctx)
    return static_cast<VarDeclNode *>(
        factory_.makeVarDecl(SourceRange::invalid(), "", factory_.makeErrorType(SourceRange::invalid(), "invalid type"), nullptr));

  TypeNode *type = nullptr;
  if (ctx->type()) {
    type = expectType(ctx->type());
  } else if (ctx->AUTO()) {
    type = factory_.makeInferredType(getRange(ctx->AUTO()));
  }

  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";
  return factory_.makeVarDecl(getRange(ctx), name, type, nullptr);
}

// For numeric variable (untyped)
std::any visitForNumericVarUntyped(LangParser::ForNumericVarUntypedContext *ctx) override {
  if (!ctx)
    return static_cast<VarDeclNode *>(
        factory_.makeVarDecl(SourceRange::invalid(), "", factory_.makeErrorType(SourceRange::invalid(), "invalid type"), nullptr));

  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";
  // Untyped variable uses auto type
  return factory_.makeVarDecl(getRange(ctx), name, factory_.makeInferredType(getRange(ctx)), nullptr);
}

// For each variable (typed)
std::any visitForEachVarTyped(LangParser::ForEachVarTypedContext *ctx) override {
  if (!ctx)
    return static_cast<VarDeclNode *>(
        factory_.makeVarDecl(SourceRange::invalid(), "", factory_.makeErrorType(SourceRange::invalid(), "invalid type"), nullptr));

  TypeNode *type = nullptr;
  if (ctx->type()) {
    type = expectType(ctx->type());
  } else if (ctx->AUTO()) {
    type = factory_.makeInferredType(getRange(ctx->AUTO()));
  }

  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";
  return factory_.makeVarDecl(getRange(ctx), name, type, nullptr);
}

// For each variable (untyped)
std::any visitForEachVarUntyped(LangParser::ForEachVarUntypedContext *ctx) override {
  if (!ctx)
    return static_cast<VarDeclNode *>(
        factory_.makeVarDecl(SourceRange::invalid(), "", factory_.makeErrorType(SourceRange::invalid(), "invalid type"), nullptr));

  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";
  return factory_.makeVarDecl(getRange(ctx), name, factory_.makeInferredType(getRange(ctx)), nullptr);
}

std::any visitBreakStmt(LangParser::BreakStmtContext *ctx) override {
  return static_cast<Stmt *>(factory_.makeBreakStmt(getRange(ctx)));
}

std::any visitContinueStmt(LangParser::ContinueStmtContext *ctx) override {
  return static_cast<Stmt *>(factory_.makeContinueStmt(getRange(ctx)));
}

std::any visitReturnStmt(LangParser::ReturnStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeReturnStmt(SourceRange::invalid()));

  std::vector<Expr *> values;
  if (ctx->expressionList()) {
    for (auto *e : ctx->expressionList()->expression())
      values.push_back(expectExpr(e));
  }
  return static_cast<Stmt *>(factory_.makeReturnStmt(getRange(ctx), values));
}

std::any visitDeferStmt(LangParser::DeferStmtContext *ctx) override {
  if (!ctx || !ctx->deferStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid defer"));
  return visit(ctx->deferStatement());
}

std::any visitDeferBlockStmt(LangParser::DeferBlockStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid defer"));
  BlockStmtNode *body = expectBlock(ctx->blockStatement());
  return static_cast<Stmt *>(factory_.makeDeferStmt(getRange(ctx), body));
}

// ------------------------------------------------------------------------
// Import Statements
// ------------------------------------------------------------------------

std::any visitImportStmt(LangParser::ImportStmtContext *ctx) override {
  if (!ctx || !ctx->importStatement())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid import"));
  return visit(ctx->importStatement());
}

std::any visitImportNamespaceStmt(LangParser::ImportNamespaceStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid import"));

  // 使用 std::string 避免悬空引用
  std::string alias = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";
  std::string path = ctx->STRING_LITERAL() ? parseString(ctx->STRING_LITERAL()->getText()) : "";

  return static_cast<Stmt *>(factory_.makeImportStmtNamespace(getRange(ctx), path, alias));
}

std::any visitImportNamedStmt(LangParser::ImportNamedStmtContext *ctx) override {
  if (!ctx)
    return static_cast<Stmt *>(factory_.makeErrorStmt(SourceRange::invalid(), "invalid import"));

  std::vector<ImportSpecifier> specs;
  for (auto *spec : ctx->importSpecifier()) {
    auto ids = spec->IDENTIFIER();
    // 使用 std::string 避免悬空引用
    std::string name = ids.size() > 0 ? ids[0]->getText() : "";
    std::string alias = ids.size() > 1 ? ids[1]->getText() : name;
    specs.push_back(factory_.makeImportSpecifier(name, alias, false, getRange(spec)));
  }

  std::string path = ctx->STRING_LITERAL() ? parseString(ctx->STRING_LITERAL()->getText()) : "";
  return static_cast<Stmt *>(factory_.makeImportStmtNamed(getRange(ctx), path, specs));
}

// ============================================================================
// LAYER 5: DECLARATIONS
// ============================================================================

// ------------------------------------------------------------------------
// Types
// ------------------------------------------------------------------------

std::any visitTypePrimitive(LangParser::TypePrimitiveContext *ctx) override {
  if (!ctx || !ctx->primitiveType())
    return static_cast<TypeNode *>(factory_.makeErrorType(getRange(ctx), "missing type"));

  auto *p = ctx->primitiveType();
  PrimitiveKind kind = PrimitiveKind::Invalid;
  if (p->INT())
    kind = PrimitiveKind::Int;
  else if (p->FLOAT())
    kind = PrimitiveKind::Float;
  else if (p->NUMBER())
    kind = PrimitiveKind::Number;
  else if (p->STRING())
    kind = PrimitiveKind::String;
  else if (p->BOOL())
    kind = PrimitiveKind::Bool;
  else if (p->VOID())
    kind = PrimitiveKind::Void;
  else if (p->NULL_())
    kind = PrimitiveKind::Null;
  else if (p->COROUTINE())
    kind = PrimitiveKind::Coroutine;
  else if (p->FUNCTION())
    kind = PrimitiveKind::Function;

  return static_cast<TypeNode *>(factory_.makePrimitiveType(getRange(ctx), kind));
}

std::any visitTypeAny(LangParser::TypeAnyContext *ctx) override {
  return static_cast<TypeNode *>(factory_.makeAnyType(getRange(ctx)));
}

std::any visitTypeListType(LangParser::TypeListTypeContext *ctx) override {
  if (!ctx || !ctx->listType())
    return static_cast<TypeNode *>(factory_.makeListType(getRange(ctx)));
  TypeNode *elem = ctx->listType()->type() ? expectType(ctx->listType()->type()) : nullptr;
  return static_cast<TypeNode *>(factory_.makeListType(getRange(ctx), elem));
}

std::any visitTypeMap(LangParser::TypeMapContext *ctx) override {
  if (!ctx || !ctx->mapType())
    return static_cast<TypeNode *>(factory_.makeMapType(getRange(ctx)));
  auto types = ctx->mapType()->type();
  TypeNode *key = types.size() > 0 ? expectType(types[0]) : nullptr;
  TypeNode *val = types.size() > 1 ? expectType(types[1]) : nullptr;
  return static_cast<TypeNode *>(factory_.makeMapType(getRange(ctx), key, val));
}

std::any visitTypeQualifiedIdentifier(LangParser::TypeQualifiedIdentifierContext *ctx) override {
  if (!ctx || !ctx->qualifiedIdentifier())
    return static_cast<TypeNode *>(factory_.makeErrorType(getRange(ctx), "missing type"));
  auto v = visit(ctx->qualifiedIdentifier());
  auto *qid = tryCast<QualifiedIdentifierNode>(v);
  if (!qid)
    return static_cast<TypeNode *>(factory_.makeErrorType(getRange(ctx), "invalid type"));
  return static_cast<TypeNode *>(factory_.makeQualifiedType(getRange(ctx), qid));
}

// ------------------------------------------------------------------------
// Variable Declarations
// ------------------------------------------------------------------------

std::any visitDeclarationStmt(LangParser::DeclarationStmtContext *ctx) override {
  if (!ctx || !ctx->declaration())
    return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid declaration"));
  auto v = visit(ctx->declaration());
  if (auto *d = tryCastDecl(v))
    return static_cast<Stmt *>(factory_.makeDeclStmt(getRange(ctx), d));
  return static_cast<Stmt *>(factory_.makeErrorStmt(getRange(ctx), "invalid declaration"));
}

std::any visitDeclaration(LangParser::DeclarationContext *ctx) override {
  if (!ctx)
    return static_cast<Decl *>(factory_.makeErrorDecl(SourceRange::invalid(), "missing decl"));

  NodeFlags exportFlag = ctx->EXPORT() ? NodeFlags::IsExport : NodeFlags::None;

  if (ctx->variableDeclaration()) {
    auto v = visit(ctx->variableDeclaration());
    if (auto *d = tryCastDecl(v)) {
      d->flags = d->flags | exportFlag;
      return v;
    }
  }
  if (ctx->functionDeclaration()) {
    auto v = visit(ctx->functionDeclaration());
    if (auto *d = tryCastDecl(v)) {
      d->flags = d->flags | exportFlag;
      return v;
    }
  }
  if (ctx->classDeclaration()) {
    auto v = visit(ctx->classDeclaration());
    if (auto *d = tryCastDecl(v)) {
      d->flags = d->flags | exportFlag;
      return v;
    }
  }
  return static_cast<Decl *>(factory_.makeErrorDecl(getRange(ctx), "unknown declaration"));
}

std::any visitDeclaration_item(LangParser::Declaration_itemContext *ctx) override {
  if (!ctx)
    return static_cast<VarDeclNode *>(
        factory_.makeVarDecl(SourceRange::invalid(), "",
                             factory_.makeErrorType(SourceRange::invalid(), "invalid decl item")));

  TypeNode *type = nullptr;
  if (ctx->AUTO()) {
    type = factory_.makeInferredType(getRange(ctx->AUTO()));
  } else if (ctx->type()) {
    type = expectType(ctx->type());
  } else {
    type = factory_.makeErrorType(getRange(ctx), "missing type");
  }

  // 使用 std::string 避免悬空引用
  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";
  return static_cast<VarDeclNode *>(factory_.makeVarDecl(getRange(ctx), name, type));
}

std::any visitVariableDeclarationDef(LangParser::VariableDeclarationDefContext *ctx) override {
  if (!ctx)
    return static_cast<Decl *>(factory_.makeErrorDecl(SourceRange::invalid(), "invalid var decl"));

  NodeFlags mods = collectModifiers(ctx->GLOBAL(), ctx->CONST());

  TypeNode *type = nullptr;
  std::string name = "";

  if (ctx->declaration_item()) {
    auto v = visit(ctx->declaration_item());
    if (auto *vd = tryCast<VarDeclNode>(v)) {
      type = vd->type;
      name = std::string(factory_.strings().get(vd->name));
    }
  }
  if (!type)
    type = factory_.makeErrorType(getRange(ctx), "missing type");

  Expr *init = ctx->expression() ? expectExpr(ctx->expression()) : nullptr;

  return static_cast<Decl *>(factory_.makeVarDecl(getRange(ctx), name, type, init, mods));
}

std::any
visitMutiVariableDeclarationDef(LangParser::MutiVariableDeclarationDefContext *ctx) override {
  if (!ctx)
    return static_cast<Decl *>(factory_.makeErrorDecl(SourceRange::invalid(), "invalid mutivar"));

  // VARS GLOBAL? CONST? IDENTIFIER (COMMA GLOBAL? CONST? IDENTIFIER)*
  // 每个IDENTIFIER都可以有自己的修饰符
  std::vector<std::string> namesStorage;
  for (auto *id : ctx->IDENTIFIER())
    namesStorage.push_back(id->getText());

  // 转换为 string_view
  std::vector<std::string_view> names;
  for (const auto &s : namesStorage)
    names.push_back(s);

  // 使用第一个IDENTIFIER的修饰符作为整体修饰符
  NodeFlags mods = NodeFlags::None;
  if (ctx->GLOBAL().size() > 0)
    mods |= NodeFlags::IsGlobal;
  if (ctx->CONST().size() > 0)
    mods |= NodeFlags::IsConst;

  Expr *init = ctx->expression() ? expectExpr(ctx->expression()) : nullptr;

  return static_cast<Decl *>(factory_.makeMultiVarDecl(getRange(ctx), names, init, mods));
}

// ------------------------------------------------------------------------
// Function & Method Declarations
// ------------------------------------------------------------------------

std::any visitParameter(LangParser::ParameterContext *ctx) override {
  if (!ctx)
    return static_cast<ParameterDeclNode *>(factory_.makeParameterDecl(
        SourceRange::invalid(), "",
        factory_.makeErrorType(SourceRange::invalid(), "invalid param")));

  TypeNode *type = expectType(ctx->type());
  // 使用 std::string 避免悬空引用
  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";

  return static_cast<ParameterDeclNode *>(factory_.makeParameterDecl(getRange(ctx), name, type));
}

std::any visitFunctionDeclarationDef(LangParser::FunctionDeclarationDefContext *ctx) override {
  if (!ctx)
    return static_cast<Decl *>(factory_.makeErrorDecl(SourceRange::invalid(), "invalid function"));

  auto range = getRange(ctx);
  NodeFlags mods = ctx->GLOBAL() ? NodeFlags::IsGlobal : NodeFlags::None;

  TypeNode *retType = expectType(ctx->type());

  // 使用 std::string 确保安全
  std::string name = "";
  QualifiedIdentifierNode *qualName = nullptr;
  if (ctx->qualifiedIdentifier()) {
    auto v = visit(ctx->qualifiedIdentifier());
    LSP_LOG("visitFunctionDeclarationDef: v.type()=" << v.type().name());
    qualName = tryCast<QualifiedIdentifierNode>(v);
    LSP_LOG("visitFunctionDeclarationDef: qualName=" << (void *)qualName);
    if (qualName && !qualName->parts.empty()) {
      LSP_LOG("visitFunctionDeclarationDef: qualName->parts.size()=" << qualName->parts.size());
      auto lastPart = qualName->parts.back();
      LSP_LOG("visitFunctionDeclarationDef: lastPart.id=" << lastPart.id);
      name = std::string(factory_.strings().get(lastPart));
      LSP_LOG("visitFunctionDeclarationDef: name from qualName=" << name);
    } else {
      LSP_LOG("visitFunctionDeclarationDef: qualName is null or parts empty!");
    }
  }
  LSP_LOG("visitFunctionDeclarationDef: final name=" << name);

  std::vector<ParameterDeclNode *> params;
  bool hasVarArgs = false;
  if (ctx->parameterList()) {
    params = visitParamList(ctx->parameterList()->parameter());
    hasVarArgs = ctx->parameterList()->DDD() != nullptr;
  }

  BlockStmtNode *body = expectBlock(ctx->blockStatement());

  return static_cast<Decl *>(factory_.makeFunctionDecl(range, name, retType, params, body, qualName,
                                                       mods, false, hasVarArgs));
}

std::any visitMultiReturnFunctionDeclarationDef(
    LangParser::MultiReturnFunctionDeclarationDefContext *ctx) override {
  if (!ctx)
    return static_cast<Decl *>(factory_.makeErrorDecl(SourceRange::invalid(), "invalid function"));

  auto range = getRange(ctx);
  NodeFlags mods = ctx->GLOBAL() ? NodeFlags::IsGlobal : NodeFlags::None;

  TypeNode *retType = factory_.makeMultiReturnType(getRange(ctx->VARS()));

  // 使用 std::string 确保安全
  std::string name = "";
  QualifiedIdentifierNode *qualName = nullptr;
  if (ctx->qualifiedIdentifier()) {
    auto v = visit(ctx->qualifiedIdentifier());
    qualName = tryCast<QualifiedIdentifierNode>(v);
    if (qualName && !qualName->parts.empty()) {
      name = std::string(factory_.strings().get(qualName->parts.back()));
    }
  }

  std::vector<ParameterDeclNode *> params;
  bool hasVarArgs = false;
  if (ctx->parameterList()) {
    params = visitParamList(ctx->parameterList()->parameter());
    hasVarArgs = ctx->parameterList()->DDD() != nullptr;
  }

  BlockStmtNode *body = expectBlock(ctx->blockStatement());

  return static_cast<Decl *>(factory_.makeFunctionDecl(range, name, retType, params, body, qualName,
                                                       mods, true, hasVarArgs));
}

// ------------------------------------------------------------------------
// Class Declarations
// ------------------------------------------------------------------------

std::any visitClassDeclarationDef(LangParser::ClassDeclarationDefContext *ctx) override {
  if (!ctx)
    return static_cast<Decl *>(factory_.makeErrorDecl(SourceRange::invalid(), "invalid class"));

  auto range = getRange(ctx);
  // 使用 std::string 避免悬空引用
  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";

  std::vector<FieldDeclNode *> fields;
  std::vector<MethodDeclNode *> methods;

  for (auto *member : ctx->classMember()) {
    auto v = visit(member);
    if (auto *f = tryCast<FieldDeclNode>(v)) {
      fields.push_back(f);
    } else if (auto *m = tryCast<MethodDeclNode>(v)) {
      methods.push_back(m);
    }
  }

  return static_cast<Decl *>(factory_.makeClassDecl(range, name, fields, methods));
}

std::any visitClassFieldMember(LangParser::ClassFieldMemberContext *ctx) override {
  if (!ctx)
    return static_cast<FieldDeclNode *>(
        factory_.makeFieldDecl(SourceRange::invalid(), "",
                               factory_.makeErrorType(SourceRange::invalid(), "invalid field")));

  NodeFlags mods = collectModifiers(nullptr, ctx->CONST(), ctx->STATIC());

  TypeNode *type = nullptr;
  // 使用 std::string 确保安全
  std::string name = "";

  if (ctx->declaration_item()) {
    auto v = visit(ctx->declaration_item());
    if (auto *vd = tryCast<VarDeclNode>(v)) {
      type = vd->type;
      name = std::string(factory_.strings().get(vd->name));
    }
  }
  if (!type)
    type = factory_.makeErrorType(getRange(ctx), "missing type");

  Expr *init = ctx->expression() ? expectExpr(ctx->expression()) : nullptr;

  return static_cast<FieldDeclNode *>(
      factory_.makeFieldDecl(getRange(ctx), name, type, init, mods));
}

std::any visitClassMethodMember(LangParser::ClassMethodMemberContext *ctx) override {
  if (!ctx)
    return static_cast<MethodDeclNode *>(
        factory_.makeMethodDecl(SourceRange::invalid(), "",
                                factory_.makeErrorType(SourceRange::invalid(), "invalid method"),
                                {}, factory_.makeBlockStmt(SourceRange::invalid(), {})));

  NodeFlags mods = ctx->STATIC() ? NodeFlags::IsStatic : NodeFlags::None;
  TypeNode *retType = expectType(ctx->type());
  // 使用 std::string 避免悬空引用
  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";

  std::vector<ParameterDeclNode *> params;
  if (ctx->parameterList()) {
    params = visitParamList(ctx->parameterList()->parameter());
  }

  BlockStmtNode *body = expectBlock(ctx->blockStatement());

  return static_cast<MethodDeclNode *>(
      factory_.makeMethodDecl(getRange(ctx), name, retType, params, body, mods));
}

std::any
visitMultiReturnClassMethodMember(LangParser::MultiReturnClassMethodMemberContext *ctx) override {
  if (!ctx)
    return static_cast<MethodDeclNode *>(
        factory_.makeMethodDecl(SourceRange::invalid(), "",
                                factory_.makeErrorType(SourceRange::invalid(), "invalid method"),
                                {}, factory_.makeBlockStmt(SourceRange::invalid(), {})));

  NodeFlags mods = ctx->STATIC() ? NodeFlags::IsStatic : NodeFlags::None;
  TypeNode *retType = factory_.makeMultiReturnType(getRange(ctx->VARS()));
  // 使用 std::string 避免悬空引用
  std::string name = ctx->IDENTIFIER() ? ctx->IDENTIFIER()->getText() : "";

  std::vector<ParameterDeclNode *> params;
  if (ctx->parameterList()) {
    params = visitParamList(ctx->parameterList()->parameter());
  }

  BlockStmtNode *body = expectBlock(ctx->blockStatement());

  return static_cast<MethodDeclNode *>(
      factory_.makeMethodDecl(getRange(ctx), name, retType, params, body, mods, true));
}

std::any visitClassEmptyMember(LangParser::ClassEmptyMemberContext * /*ctx*/) override {
  // Empty member (just semicolon) - return nothing meaningful
  return std::any{};
}

// ------------------------------------------------------------------------
// Compilation Unit (Root)
// ------------------------------------------------------------------------

std::any visitCompilationUnit(LangParser::CompilationUnitContext *ctx) override {
  LSP_LOG_SEP("visitCompilationUnit");
  LSP_LOG(">>> VERSION 3 - FIXED std::any_cast TYPE MISMATCH <<<");
  LSP_LOG("ctx=" << (void *)ctx);
  LSP_LOG("AstFactory address=" << (void *)&factory_);
  LSP_LOG("StringTable address=" << (void *)&factory_.strings()
                                 << ", size=" << factory_.strings().size());

  if (!ctx) {
    LSP_LOG("ctx is nullptr, returning empty CompilationUnit");
    return static_cast<Decl *>(factory_.makeCompilationUnit(SourceRange::invalid(), filename_, {}));
  }

  auto ctxStatements = ctx->statement();
  LSP_LOG("ctx->statement().size()=" << ctxStatements.size());

  std::vector<Stmt *> stmts = visitStmtList(ctxStatements);
  LSP_LOG("After visitStmtList, stmts.size()=" << stmts.size());

  for (size_t i = 0; i < stmts.size(); ++i) {
    if (stmts[i]) {
      LSP_LOG("  stmts[" << i << "]: " << astKindToString(stmts[i]->kind) << " range=["
                         << stmts[i]->range.begin.offset << "-" << stmts[i]->range.end.offset
                         << "]");
    } else {
      LSP_LOG("  stmts[" << i << "]: nullptr");
    }
  }

  // Extract imports for quick access
  std::vector<ImportStmtNode *> imports;
  for (auto *stmt : stmts) {
    if (stmt->kind == AstKind::ImportStmt) {
      imports.push_back(static_cast<ImportStmtNode *>(stmt));
    }
  }

  auto range = getRange(ctx);
  LSP_LOG("CompilationUnit range: [" << range.begin.offset << "-" << range.end.offset
                                     << "], line=" << range.begin.line);

  auto *result = factory_.makeCompilationUnit(range, filename_, stmts, imports);
  LSP_LOG("Created CompilationUnit at address: " << (void *)result << ", statements.size()="
                                                 << result->statements.size());

  // 打印最终 StringTable 状态
  LSP_LOG("Final StringTable size=" << factory_.strings().size());
  LSP_LOG("Final StringTable contents:");
  for (size_t i = 0; i < factory_.strings().size() && i < 10; ++i) {
    ast::InternedString is{static_cast<uint32_t>(i)};
    LSP_LOG("  [" << i << "] = '" << factory_.strings().get(is) << "'");
  }

  // 必须返回 CompilationUnitNode* 类型，否则 std::any_cast 会失败
  return static_cast<CompilationUnitNode *>(result);
}

private:
// Context storage for nested for-loop processing
LangParser::ForStatementContext *currentForStmt_ = nullptr;
