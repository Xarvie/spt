/**
 * @file SemanticAnalyzer.h
 * @brief Semantic Analysis for Lang Language
 *
 * Provides semantic analysis including:
 * - Symbol table construction
 * - Name resolution
 * - Type inference and checking
 * - Error recovery with graceful degradation
 *
 * Key Design Principles:
 * - Side Table pattern: Type information stored separately from AST
 * - Tolerant: Never throws, returns UnknownType on errors
 * - Supports incremental analysis for LSP
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "AstFactory.h"
#include "AstNodes.h"
#include "Scope.h"
#include "Symbol.h"
#include "TypeSystem.h"

#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lang {
namespace semantic {

// ============================================================================
// Diagnostic System
// ============================================================================

enum class DiagnosticSeverity : uint8_t { Error, Warning, Info, Hint };

struct Diagnostic {
  DiagnosticSeverity severity;
  std::string message;
  ast::SourceRange range;
  std::string code;

  [[nodiscard]] bool isError() const noexcept { return severity == DiagnosticSeverity::Error; }
};

// ============================================================================
// Semantic Model
// ============================================================================

/**
 * @brief Contains all semantic analysis results for a compilation unit
 */
class SemanticModel {
public:
  SemanticModel() = default;
  SemanticModel(const SemanticModel &) = delete;
  SemanticModel &operator=(const SemanticModel &) = delete;
  SemanticModel(SemanticModel &&) = default;
  SemanticModel &operator=(SemanticModel &&) = default;

  // Type Mapping
  void setNodeType(const ast::AstNode *node, types::TypeRef type) {
    if (node)
      nodeTypes_[node] = type;
  }

  [[nodiscard]] types::TypeRef getNodeType(const ast::AstNode *node) const {
    if (!node)
      return typeContext_.unknownType();
    auto it = nodeTypes_.find(node);
    return it != nodeTypes_.end() ? it->second : typeContext_.unknownType();
  }

  [[nodiscard]] bool hasType(const ast::AstNode *node) const {
    return node && nodeTypes_.find(node) != nodeTypes_.end();
  }

  // Symbol Mapping
  void setResolvedSymbol(const ast::AstNode *node, Symbol *symbol) {
    if (node && symbol)
      resolvedSymbols_[node] = symbol;
  }

  [[nodiscard]] Symbol *getResolvedSymbol(const ast::AstNode *node) const {
    if (!node)
      return nullptr;
    auto it = resolvedSymbols_.find(node);
    return it != resolvedSymbols_.end() ? it->second : nullptr;
  }

  void setDefiningSymbol(const ast::AstNode *node, Symbol *symbol) {
    if (node && symbol)
      definingSymbols_[node] = symbol;
  }

  [[nodiscard]] Symbol *getDefiningSymbol(const ast::AstNode *node) const {
    if (!node)
      return nullptr;
    auto it = definingSymbols_.find(node);
    return it != definingSymbols_.end() ? it->second : nullptr;
  }

  // Scope Mapping
  void setNodeScope(const ast::AstNode *node, Scope *scope) {
    if (node && scope)
      nodeScopes_[node] = scope;
  }

  [[nodiscard]] Scope *getNodeScope(const ast::AstNode *node) const {
    if (!node)
      return nullptr;
    auto it = nodeScopes_.find(node);
    return it != nodeScopes_.end() ? it->second : nullptr;
  }

  // Diagnostics
  void addDiagnostic(Diagnostic diag) {
    if (diag.severity == DiagnosticSeverity::Error)
      ++errorCount_;
    diagnostics_.push_back(std::move(diag));
  }

  void addError(ast::SourceRange range, std::string msg, std::string code = "") {
    addDiagnostic({DiagnosticSeverity::Error, std::move(msg), range, std::move(code)});
  }

  void addWarning(ast::SourceRange range, std::string msg, std::string code = "") {
    addDiagnostic({DiagnosticSeverity::Warning, std::move(msg), range, std::move(code)});
  }

  [[nodiscard]] const std::vector<Diagnostic> &diagnostics() const noexcept { return diagnostics_; }

  [[nodiscard]] size_t errorCount() const noexcept { return errorCount_; }

  [[nodiscard]] bool hasErrors() const noexcept { return errorCount_ > 0; }

  // Access
  [[nodiscard]] SymbolTable &symbolTable() noexcept { return symbolTable_; }

  [[nodiscard]] const SymbolTable &symbolTable() const noexcept { return symbolTable_; }

  [[nodiscard]] types::TypeContext &typeContext() noexcept { return typeContext_; }

  [[nodiscard]] const types::TypeContext &typeContext() const noexcept { return typeContext_; }

  // LSP Support
  [[nodiscard]] Symbol *findSymbolAt(ast::SourceLoc loc) const {
    for (const auto &[node, sym] : resolvedSymbols_) {
      if (node->range.contains(loc))
        return sym;
    }
    for (const auto &[node, sym] : definingSymbols_) {
      if (node->range.contains(loc))
        return sym;
    }
    return nullptr;
  }

  [[nodiscard]] std::vector<ast::SourceRange> findReferences(Symbol *sym) const {
    std::vector<ast::SourceRange> refs;
    if (!sym)
      return refs;
    refs.push_back({sym->definitionLoc(), sym->definitionLoc()});
    for (const auto &loc : sym->references()) {
      refs.push_back({loc, loc});
    }
    return refs;
  }

  [[nodiscard]] std::vector<Symbol *> getCompletions(Scope *scope) const {
    return scope ? scope->allVisibleSymbols() : std::vector<Symbol *>{};
  }

private:
  std::unordered_map<const ast::AstNode *, types::TypeRef> nodeTypes_;
  std::unordered_map<const ast::AstNode *, Symbol *> resolvedSymbols_;
  std::unordered_map<const ast::AstNode *, Symbol *> definingSymbols_;
  std::unordered_map<const ast::AstNode *, Scope *> nodeScopes_;
  std::vector<Diagnostic> diagnostics_;
  size_t errorCount_ = 0;
  SymbolTable symbolTable_;
  mutable types::TypeContext typeContext_;
};

// ============================================================================
// Semantic Analyzer
// ============================================================================

/**
 * @brief Performs semantic analysis on an AST
 */
class SemanticAnalyzer : public ast::AstVisitor<SemanticAnalyzer, types::TypeRef> {
public:
  explicit SemanticAnalyzer(const ast::StringTable &strings) : strings_(strings) {}

  [[nodiscard]] SemanticModel analyze(ast::CompilationUnitNode *unit) {
    model_ = SemanticModel();
    currentScope_ = model_.symbolTable().globalScope();

    if (unit) {
      collectDeclarations(unit);
      visit(unit);
    }
    return std::move(model_);
  }

  // Expression Visitors
  types::TypeRef visitErrorExpr(ast::ErrorExprNode *node) {
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitMissingExpr(ast::MissingExprNode *node) {
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitNullLiteral(ast::NullLiteralNode *node) {
    return setType(node, model_.typeContext().nullType());
  }

  types::TypeRef visitBoolLiteral(ast::BoolLiteralNode *node) {
    return setType(node, model_.typeContext().boolType());
  }

  types::TypeRef visitIntLiteral(ast::IntLiteralNode *node) {
    return setType(node, model_.typeContext().intType());
  }

  types::TypeRef visitFloatLiteral(ast::FloatLiteralNode *node) {
    return setType(node, model_.typeContext().floatType());
  }

  types::TypeRef visitStringLiteral(ast::StringLiteralNode *node) {
    return setType(node, model_.typeContext().stringType());
  }

  types::TypeRef visitIdentifier(ast::IdentifierNode *node) {
    auto name = getString(node->name);
    if (Symbol *sym = currentScope_->resolve(name)) {
      model_.setResolvedSymbol(node, sym);
      sym->addReference(node->range.begin);
      return setType(node, sym->type());
    }
    model_.addError(node->range, "Undeclared identifier: " + std::string(name), "E001");
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitQualifiedIdentifier(ast::QualifiedIdentifierNode *node) {
    if (node->parts.empty())
      return setType(node, model_.typeContext().unknownType());

    auto firstName = getString(node->parts[0]);
    if (Symbol *sym = currentScope_->resolve(firstName)) {
      model_.setResolvedSymbol(node, sym);
      if (node->parts.size() == 1)
        return setType(node, sym->type());
    } else {
      model_.addError(node->range, "Undeclared identifier: " + std::string(firstName), "E001");
    }
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitMemberAccessExpr(ast::MemberAccessExprNode *node) {
    types::TypeRef baseType = visit(node->base);
    if (!baseType || baseType->isError() || baseType->isUnknown()) {
      return setType(node, model_.typeContext().unknownType());
    }

    auto memberName = getString(node->member);

    // Check if the member corresponds to a known symbol in the current scope
    // For example, if it is a class definition like `NS.Inner`
    if (Symbol *sym = currentScope_->resolve(memberName)) {
      if (sym->isClass()) {
        return setType(node, sym->type());
      }
    }

    if (baseType->isClass()) {
      auto *classType = static_cast<const types::ClassType *>(baseType.get());
      if (auto *field = classType->findField(memberName)) {
        return setType(node, field->type);
      }
      if (auto *method = classType->findMethod(memberName)) {
        return setType(node, method->type);
      }
      if (!node->isIncomplete()) {
        model_.addError(node->range,
                        "No member '" + std::string(memberName) + "' in type '" +
                            classType->name() + "'",
                        "E002");
      }
    } else if (baseType->isAny()) {
      // Fallback for ANY
      return setType(node, model_.typeContext().anyType());
    }
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitIndexExpr(ast::IndexExprNode *node) {
    types::TypeRef baseType = visit(node->base);
    visit(node->index);

    if (baseType->isList()) {
      auto *lt = static_cast<const types::ListType *>(baseType.get());
      return setType(node, lt->elementType() ? lt->elementType() : model_.typeContext().anyType());
    }
    if (baseType->isMap()) {
      auto *mt = static_cast<const types::MapType *>(baseType.get());
      return setType(node, mt->valueType() ? mt->valueType() : model_.typeContext().anyType());
    }
    if (baseType->isString())
      return setType(node, model_.typeContext().stringType());
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitBinaryExpr(ast::BinaryExprNode *node) {
    types::TypeRef left = visit(node->left);
    types::TypeRef right = visit(node->right);
    return setType(node, model_.typeContext().getBinaryOpResultType(left, right, node->op));
  }

  types::TypeRef visitUnaryExpr(ast::UnaryExprNode *node) {
    types::TypeRef opType = visit(node->operand);
    switch (node->op) {
    case ast::UnaryOp::Neg:
      return setType(node, opType->isNumber() ? opType : model_.typeContext().unknownType());
    case ast::UnaryOp::Not:
      return setType(node, model_.typeContext().boolType());
    case ast::UnaryOp::BitNot:
    case ast::UnaryOp::Len:
      return setType(node, model_.typeContext().intType());
    default:
      return setType(node, model_.typeContext().unknownType());
    }
  }

  types::TypeRef visitCallExpr(ast::CallExprNode *node) {
    types::TypeRef calleeType = visit(node->callee);
    for (auto *arg : node->arguments)
      visit(arg);

    if (calleeType->isFunction()) {
      auto *ft = static_cast<const types::FunctionType *>(calleeType.get());
      return setType(node, ft->returnType());
    }
    if (calleeType->isClass())
      return setType(node, calleeType);
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitListExpr(ast::ListExprNode *node) {
    types::TypeRef elemType = model_.typeContext().unknownType();
    for (auto *elem : node->elements) {
      types::TypeRef et = visit(elem);
      if (!elemType || elemType->isUnknown())
        elemType = et;
      else
        elemType = model_.typeContext().commonType(elemType, et);
    }
    return setType(node, model_.typeContext().makeListType(elemType));
  }

  types::TypeRef visitMapExpr(ast::MapExprNode *node) {
    for (auto *entry : node->entries)
      visit(entry);
    return setType(node, model_.typeContext().makeMapType(model_.typeContext().unknownType(),
                                                          model_.typeContext().unknownType()));
  }

  types::TypeRef visitMapEntry(ast::MapEntryNode *node) {
    visit(node->key);
    visit(node->value);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitLambdaExpr(ast::LambdaExprNode *node) {
    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Function, currentScope_, node);

    std::vector<types::TypeRef> paramTypes;
    uint32_t idx = 0;
    for (auto *param : node->params) {
      types::TypeRef pt = resolveTypeNode(param->type);
      paramTypes.push_back(pt);
      auto *ps = model_.symbolTable().createParameter(getString(param->name), pt,
                                                      param->range.begin, idx++);
      currentScope_->define(ps);
      model_.setDefiningSymbol(param, ps);
    }

    types::TypeRef retType = resolveTypeNode(node->returnType);
    if (node->body)
      visit(node->body);

    currentScope_ = prev;
    return setType(node, model_.typeContext().makeFunctionType(std::move(paramTypes), retType));
  }

  types::TypeRef visitParenExpr(ast::ParenExprNode *node) {
    return setType(node, visit(node->inner));
  }

  types::TypeRef visitVarArgsExpr(ast::VarArgsExprNode *node) {
    return setType(node, model_.typeContext().anyType());
  }

  // Statement Visitors
  types::TypeRef visitErrorStmt(ast::ErrorStmtNode *) { return model_.typeContext().voidType(); }

  types::TypeRef visitEmptyStmt(ast::EmptyStmtNode *) { return model_.typeContext().voidType(); }

  types::TypeRef visitExprStmt(ast::ExprStmtNode *node) {
    visit(node->expr);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitBlockStmt(ast::BlockStmtNode *node) {
    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Block, currentScope_, node);
    model_.setNodeScope(node, currentScope_);
    for (auto *stmt : node->statements)
      visit(stmt);
    currentScope_ = prev;
    return model_.typeContext().voidType();
  }

  types::TypeRef visitAssignStmt(ast::AssignStmtNode *node) {
    visit(node->target.expr);
    visit(node->value);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitMultiAssignStmt(ast::MultiAssignStmtNode *node) {
    for (auto &t : node->targets)
      visit(t.expr);
    for (auto *v : node->values)
      visit(v);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitUpdateAssignStmt(ast::UpdateAssignStmtNode *node) {
    visit(node->target.expr);
    visit(node->value);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitIfStmt(ast::IfStmtNode *node) {
    for (auto &b : node->branches) {
      visit(b.condition);
      visit(b.body);
    }
    if (node->elseBody)
      visit(node->elseBody);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitWhileStmt(ast::WhileStmtNode *node) {
    visit(node->condition);
    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Loop, currentScope_, node);
    visit(node->body);
    currentScope_ = prev;
    return model_.typeContext().voidType();
  }

  types::TypeRef visitForStmt(ast::ForStmtNode *node) {
    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Loop, currentScope_, node);

    if (node->style == ast::ForStmtNode::Style::Numeric) {
      // Numeric for: for (i = start, end, step) { body }
      if (node->numericVar) {
        types::TypeRef vt = resolveTypeNode(node->numericVar->type);
        if (vt->isUnknown())
          vt = model_.typeContext().numberType();
        auto *vs = model_.symbolTable().createVariable(getString(node->numericVar->name), vt, node->numericVar->range.begin);
        currentScope_->define(vs);
        model_.setDefiningSymbol(node->numericVar, vs);
        setType(node->numericVar, vt);
      }
      if (node->start)
        visit(node->start);
      if (node->end)
        visit(node->end);
      if (node->step)
        visit(node->step);
    } else if (node->style == ast::ForStmtNode::Style::CStyle) {
      if (node->init)
        visit(node->init);
      if (node->condition)
        visit(node->condition);
      for (auto *u : node->updates)
        visit(u);
    } else {
      // ForEach
      if (node->collection) {
        types::TypeRef ct = visit(node->collection);
        types::TypeRef elemType = model_.typeContext().anyType();
        if (ct->isList()) {
          auto *lt = static_cast<const types::ListType *>(ct.get());
          if (lt->elementType())
            elemType = lt->elementType();
        }
        for (auto *iv : node->iterVars) {
          types::TypeRef vt = resolveTypeNode(iv->type);
          if (vt->isUnknown())
            vt = elemType;
          auto *vs = model_.symbolTable().createVariable(getString(iv->name), vt, iv->range.begin);
          currentScope_->define(vs);
          model_.setDefiningSymbol(iv, vs);
          setType(iv, vt);
        }
      }
    }
    visit(node->body);
    currentScope_ = prev;
    return model_.typeContext().voidType();
  }

  types::TypeRef visitBreakStmt(ast::BreakStmtNode *node) {
    if (!currentScope_->isInsideLoop())
      model_.addError(node->range, "'break' outside of loop", "E003");
    return model_.typeContext().voidType();
  }

  types::TypeRef visitContinueStmt(ast::ContinueStmtNode *node) {
    if (!currentScope_->isInsideLoop())
      model_.addError(node->range, "'continue' outside of loop", "E003");
    return model_.typeContext().voidType();
  }

  types::TypeRef visitReturnStmt(ast::ReturnStmtNode *node) {
    for (auto *v : node->values)
      visit(v);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitDeferStmt(ast::DeferStmtNode *node) {
    visit(node->body);
    return model_.typeContext().voidType();
  }

  types::TypeRef visitImportStmt(ast::ImportStmtNode *node) {
    auto path = getString(node->modulePath);
    if (node->style == ast::ImportStmtNode::Style::Namespace) {
      auto alias = getString(node->namespaceAlias);
      auto *is = model_.symbolTable().createImport(alias, path, node->range.begin);
      currentScope_->define(is);
      model_.setDefiningSymbol(node, is);
    } else {
      for (auto &s : node->specifiers) {
        auto name = getString(s.alias);
        auto *is = model_.symbolTable().createImport(name, path, s.range.begin);
        currentScope_->define(is);
      }
    }
    return model_.typeContext().voidType();
  }

  types::TypeRef visitDeclStmt(ast::DeclStmtNode *node) {
    visit(node->decl);
    return model_.typeContext().voidType();
  }

  // Declaration Visitors
  types::TypeRef visitErrorDecl(ast::ErrorDeclNode *) { return model_.typeContext().voidType(); }

  types::TypeRef visitVarDecl(ast::VarDeclNode *node) {
    auto name = getString(node->name);
    types::TypeRef declared = resolveTypeNode(node->type);
    types::TypeRef inferred = model_.typeContext().unknownType();
    if (node->initializer)
      inferred = visit(node->initializer);

    types::TypeRef final_;
    if (declared && !declared->isUnknown())
      final_ = declared;
    else if (inferred && !inferred->isUnknown())
      final_ = inferred;
    else
      final_ = model_.typeContext().anyType();

    // Ensure the node's type node gets the final type if it's InferredTypeNode
    if (auto *inferredType = ast::ast_cast<ast::InferredTypeNode>(node->type)) {
      setType(inferredType, final_);
    }

    if (Symbol *existing = currentScope_->resolveLocal(name)) {
      if (existing->type()->isUnknown())
        existing->setType(final_);
      if (declared->isUnknown() && !inferred->isUnknown())
        existing->addFlag(SymbolFlags::Inferred);
      return setType(node, final_);
    }

    auto *vs = model_.symbolTable().createVariable(name, final_, node->range.begin);
    if (node->isConst())
      vs->addFlag(SymbolFlags::Const);
    if (node->isGlobal())
      vs->addFlag(SymbolFlags::Global);
    if (declared->isUnknown())
      vs->addFlag(SymbolFlags::Inferred);
    vs->setAstNode(node);
    currentScope_->define(vs);
    model_.setDefiningSymbol(node, vs);
    return setType(node, final_);
  }

  types::TypeRef visitMultiVarDecl(ast::MultiVarDeclNode *node) {
    types::TypeRef initType = model_.typeContext().unknownType();
    if (node->initializer)
      initType = visit(node->initializer);

    for (size_t i = 0; i < node->names.size(); ++i) {
      auto name = getString(node->names[i]);
      types::TypeRef vt = initType;
      if (initType->isTuple()) {
        auto *tt = static_cast<const types::TupleType *>(initType.get());
        if (i < tt->elementTypes().size())
          vt = tt->elementTypes()[i];
      }
      auto *vs = model_.symbolTable().createVariable(name, vt, node->range.begin);
      vs->addFlag(SymbolFlags::Inferred);
      currentScope_->define(vs);
    }
    return model_.typeContext().voidType();
  }

  types::TypeRef visitParameterDecl(ast::ParameterDeclNode *node) {
    return setType(node, resolveTypeNode(node->type));
  }

  types::TypeRef visitFunctionDecl(ast::FunctionDeclNode *node) {
    auto name = getString(node->name);
    types::TypeRef retType = resolveTypeNode(node->returnType);

    std::vector<types::TypeRef> paramTypes;
    for (auto *p : node->parameters)
      paramTypes.push_back(resolveTypeNode(p->type));

    types::TypeRef funcType =
        model_.typeContext().makeFunctionType(paramTypes, retType, node->hasVarArgs);

    FunctionSymbol *fs = nullptr;
    if (Symbol *existing = currentScope_->resolveLocal(name)) {
      if (existing->isFunction()) {
        fs = static_cast<FunctionSymbol *>(existing);
        fs->setType(funcType);
        fs->setReturnType(retType);
        fs->setAstNode(node);
      }
    }
    if (!fs) {
      fs = model_.symbolTable().createFunction(name, funcType, node->range.begin);
      fs->setReturnType(retType);
      fs->setMultiReturn(node->isMultiReturn);
      fs->setAstNode(node);
      if (node->isExport())
        fs->addFlag(SymbolFlags::Export);
      currentScope_->define(fs);
    }
    model_.setDefiningSymbol(node, fs);

    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Function, currentScope_, node);
    fs->setBodyScope(currentScope_);
    model_.setNodeScope(node, currentScope_);

    uint32_t idx = 0;
    for (auto *p : node->parameters) {
      auto pn = getString(p->name);
      types::TypeRef pt = resolveTypeNode(p->type);
      auto *ps = model_.symbolTable().createParameter(pn, pt, p->range.begin, idx++);
      if (p->isVariadic)
        ps->addFlag(SymbolFlags::Variadic);
      currentScope_->define(ps);
      fs->addParameter(ps);
      model_.setDefiningSymbol(p, ps);
      setType(p, pt);
    }

    if (node->body)
      visit(node->body);
    currentScope_ = prev;
    return setType(node, funcType);
  }

  types::TypeRef visitFieldDecl(ast::FieldDeclNode *node) {
    types::TypeRef ft = resolveTypeNode(node->type);
    if (node->initializer)
      visit(node->initializer);
    return setType(node, ft);
  }

  types::TypeRef visitMethodDecl(ast::MethodDeclNode *node) {
    auto name = getString(node->name);
    types::TypeRef retType = resolveTypeNode(node->returnType);

    std::vector<types::TypeRef> paramTypes;
    for (auto *p : node->parameters)
      paramTypes.push_back(resolveTypeNode(p->type));
    types::TypeRef methType = model_.typeContext().makeFunctionType(paramTypes, retType);

    MethodSymbol *ms = symbol_cast<MethodSymbol>(currentScope_->resolveLocal(name));
    if (ms) {
      ms->setType(methType);
      ms->setReturnType(retType);
    }

    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Function, currentScope_, node);
    if (ms) {
      ms->setBodyScope(currentScope_);
      model_.setNodeScope(node, currentScope_);
    }

    uint32_t idx = 0;
    for (auto *p : node->parameters) {
      auto pn = getString(p->name);
      types::TypeRef pt = resolveTypeNode(p->type);
      auto *ps = model_.symbolTable().createParameter(pn, pt, p->range.begin, idx++);
      currentScope_->define(ps);
      if (ms)
        ms->addParameter(ps);
      model_.setDefiningSymbol(p, ps);
      setType(p, pt);
    }

    if (node->body)
      visit(node->body);
    currentScope_ = prev;
    return setType(node, methType);
  }

  types::TypeRef visitClassDecl(ast::ClassDeclNode *node) {
    auto className = getString(node->name);

    ClassSymbol *cs = nullptr;
    if (Symbol *existing = currentScope_->resolveLocal(className)) {
      if (existing->isClass())
        cs = static_cast<ClassSymbol *>(existing);
    }
    if (!cs) {
      cs = model_.symbolTable().createClass(className, node->range.begin);
      cs->setAstNode(node);
      if (node->isExport())
        cs->addFlag(SymbolFlags::Export);
      currentScope_->define(cs);
    }

    auto *classType = model_.typeContext().getOrCreateClassType(className);
    cs->setClassType(classType);
    model_.setDefiningSymbol(node, cs);

    Scope *prev = currentScope_;
    currentScope_ = model_.symbolTable().createScope(ScopeKind::Class, currentScope_, node);
    cs->setMemberScope(currentScope_);
    model_.setNodeScope(node, currentScope_);

    for (auto *f : node->fields) {
      auto fn = getString(f->name);
      types::TypeRef ft = resolveTypeNode(f->type);
      auto *fld = model_.symbolTable().createField(fn, ft, f->range.begin, cs);
      if (f->isStatic())
        fld->addFlag(SymbolFlags::Static);
      if (f->isConst())
        fld->addFlag(SymbolFlags::Const);
      currentScope_->define(fld);
      cs->addField(fld);
      model_.setDefiningSymbol(f, fld);
      classType->addField({std::string(fn), ft, f->isStatic(), f->isConst()});
      visit(f);
    }

    for (auto *m : node->methods) {
      auto mn = getString(m->name);
      types::TypeRef rt = resolveTypeNode(m->returnType);
      std::vector<types::TypeRef> pts;
      for (auto *p : m->parameters)
        pts.push_back(resolveTypeNode(p->type));
      types::TypeRef mt = model_.typeContext().makeFunctionType(pts, rt);
      auto *meth = model_.symbolTable().createMethod(mn, mt, m->range.begin, cs);
      if (m->isStatic())
        meth->addFlag(SymbolFlags::Static);
      currentScope_->define(meth);
      cs->addMethod(meth);
      model_.setDefiningSymbol(m, meth);
      classType->addMethod({std::string(mn), mt, m->isStatic()});
      visit(m);
    }

    currentScope_ = prev;
    return setType(node, types::TypeRef(classType));
  }

  types::TypeRef visitCompilationUnit(ast::CompilationUnitNode *node) {
    model_.setNodeScope(node, currentScope_);
    for (auto *imp : node->imports)
      visit(imp);
    for (auto *stmt : node->statements)
      visit(stmt);
    return model_.typeContext().voidType();
  }

  // Type Node Visitors
  types::TypeRef visitErrorType(ast::ErrorTypeNode *node) {
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitInferredType(ast::InferredTypeNode *node) {
    // If the node type was already set (e.g. by VarDecl), return it
    if (auto t = model_.getNodeType(node)) {
      return t;
    }
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitPrimitiveType(ast::PrimitiveTypeNode *node) {
    types::TypeRef t;
    switch (node->primitiveKind) {
    case ast::PrimitiveKind::Int:
      t = model_.typeContext().intType();
      break;
    case ast::PrimitiveKind::Float:
      t = model_.typeContext().floatType();
      break;
    case ast::PrimitiveKind::Number:
      t = model_.typeContext().numberType();
      break;
    case ast::PrimitiveKind::String:
      t = model_.typeContext().stringType();
      break;
    case ast::PrimitiveKind::Bool:
      t = model_.typeContext().boolType();
      break;
    case ast::PrimitiveKind::Void:
      t = model_.typeContext().voidType();
      break;
    case ast::PrimitiveKind::Null:
      t = model_.typeContext().nullType();
      break;
    case ast::PrimitiveKind::Coroutine:
      t = model_.typeContext().fiberType();
      break;
    case ast::PrimitiveKind::Function:
      t = model_.typeContext().unknownType();
      break;
    default:
      t = model_.typeContext().unknownType();
      break;
    }
    return setType(node, t);
  }

  types::TypeRef visitAnyType(ast::AnyTypeNode *node) {
    return setType(node, model_.typeContext().anyType());
  }

  types::TypeRef visitListType(ast::ListTypeNode *node) {
    types::TypeRef elem =
        node->elementType ? resolveTypeNode(node->elementType) : model_.typeContext().anyType();
    return setType(node, model_.typeContext().makeListType(elem));
  }

  types::TypeRef visitMapType(ast::MapTypeNode *node) {
    types::TypeRef k =
        node->keyType ? resolveTypeNode(node->keyType) : model_.typeContext().anyType();
    types::TypeRef v =
        node->valueType ? resolveTypeNode(node->valueType) : model_.typeContext().anyType();
    return setType(node, model_.typeContext().makeMapType(k, v));
  }

  types::TypeRef visitQualifiedType(ast::QualifiedTypeNode *node) {
    if (node->name && !node->name->parts.empty()) {
      auto typeName = getString(node->name->parts[0]);
      if (auto ct = model_.typeContext().findClassType(typeName)) {
        return setType(node, ct);
      }
    }
    return setType(node, model_.typeContext().unknownType());
  }

  types::TypeRef visitMultiReturnType(ast::MultiReturnTypeNode *node) {
    return setType(node, model_.typeContext().unknownType());
  }

private:
  types::TypeRef setType(ast::AstNode *node, types::TypeRef type) {
    model_.setNodeType(node, type);
    return type;
  }

  std::string_view getString(ast::InternedString str) const { return strings_.get(str); }

  types::TypeRef resolveTypeNode(ast::TypeNode *node) {
    if (!node)
      return model_.typeContext().unknownType();
    return visit(node);
  }

  void collectDeclarations(ast::CompilationUnitNode *unit) {
    for (auto *stmt : unit->statements) {
      if (auto *ds = ast::ast_cast<ast::DeclStmtNode>(stmt)) {
        collectDecl(ds->decl);
      }
    }
  }

  void collectDecl(ast::Decl *decl) {
    if (!decl)
      return;

    switch (decl->kind) {
    case ast::AstKind::FunctionDecl: {
      auto *fd = static_cast<ast::FunctionDeclNode *>(decl);
      auto name = getString(fd->name);
      auto *fs = model_.symbolTable().createFunction(name, model_.typeContext().unknownType(),
                                                     fd->range.begin);
      fs->addFlag(SymbolFlags::Undefined);
      currentScope_->define(fs);
      break;
    }
    case ast::AstKind::ClassDecl: {
      auto *cd = static_cast<ast::ClassDeclNode *>(decl);
      auto name = getString(cd->name);
      auto *cs = model_.symbolTable().createClass(name, cd->range.begin);
      cs->addFlag(SymbolFlags::Undefined);
      (void)model_.typeContext().getOrCreateClassType(name);
      currentScope_->define(cs);
      break;
    }
    default:
      break;
    }
  }

  const ast::StringTable &strings_;
  SemanticModel model_;
  Scope *currentScope_ = nullptr;
};

} // namespace semantic
} // namespace lang
