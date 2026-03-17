/**
 * @file NodeFinder.h
 * @brief AST Node Finding Utilities for LSP Operations
 *
 * Provides efficient node lookup capabilities including:
 * - Find node at position (for hover, completion, go-to-definition)
 * - Find all references to a symbol
 * - Find enclosing scope
 * - Find declaration of identifier
 *
 * Key Features:
 * - Position-based node lookup with O(depth) complexity
 * - Context-aware completion triggers
 * - Scope chain traversal
 * - Support for incomplete/error nodes (critical for LSP)
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

// 启用调试日志 - 调试完成后注释掉这行
#define LSP_DEBUG_ENABLED
#include "LspLogger.h"

#include "AstFactory.h"
#include "AstNodes.h"
#include "LineOffsetTable.h"

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

namespace lang {
namespace lsp {

/**
 * @brief Context information for a found node
 */
struct NodeContext {
  ast::AstNode *node = nullptr;          ///< The found node
  std::vector<ast::AstNode *> ancestors; ///< Path from root to node (root first)

  [[nodiscard]] bool valid() const noexcept { return node != nullptr; }

  /**
   * @brief Get the immediate parent node
   */
  [[nodiscard]] ast::AstNode *parent() const noexcept {
    return ancestors.empty() ? nullptr : ancestors.back();
  }

  /**
   * @brief Find nearest ancestor of specific kind
   */
  template <typename T> [[nodiscard]] T *findAncestor() const {
    for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
      if (auto *result = ast::ast_cast<T>(*it)) {
        return result;
      }
    }
    return nullptr;
  }

  /**
   * @brief Check if node is inside a specific construct
   */
  [[nodiscard]] bool isInsideFunction() const {
    return findAncestor<ast::FunctionDeclNode>() != nullptr ||
           findAncestor<ast::MethodDeclNode>() != nullptr ||
           findAncestor<ast::LambdaExprNode>() != nullptr;
  }

  [[nodiscard]] bool isInsideClass() const { return findAncestor<ast::ClassDeclNode>() != nullptr; }

  [[nodiscard]] bool isInsideLoop() const {
    return findAncestor<ast::WhileStmtNode>() != nullptr ||
           findAncestor<ast::ForStmtNode>() != nullptr;
  }
};

/**
 * @brief Completion context for LSP
 */
enum class CompletionTrigger : uint8_t {
  None,
  DotAccess,      ///< After '.' (member completion)
  ColonAccess,    ///< After ':' (method completion)
  NewExpression,  ///< After 'new' (class completion)
  TypeAnnotation, ///< In type position (type completion)
  Import,         ///< In import statement (module completion)
  Identifier,     ///< Regular identifier (scope completion)
  Argument,       ///< Inside function call (parameter hints)
};

/**
 * @brief Completion context information
 */
struct CompletionContext {
  CompletionTrigger trigger = CompletionTrigger::None;
  ast::Expr *baseExpr = nullptr;         ///< For member completion, the base expression
  ast::AstNode *enclosingNode = nullptr; ///< The enclosing construct
  uint32_t argumentIndex = 0;            ///< For argument completion, which argument
  std::string_view partialText;          ///< Partial identifier being typed
};

/**
 * @brief Result of a node search
 */
struct FindResult {
  NodeContext context;
  CompletionContext completion;

  [[nodiscard]] bool valid() const noexcept { return context.valid(); }

  [[nodiscard]] ast::AstNode *node() const noexcept { return context.node; }
};

/**
 * @brief AST Node Finder - The "eyes" of the LSP
 *
 * Usage:
 *   NodeFinder finder(compilationUnit);
 *
 *   // Find node at cursor position
 *   auto result = finder.findNodeAt(offset);
 *   if (result.valid()) {
 *       // Use result.context.node
 *   }
 *
 *   // Find for completion
 *   auto completionCtx = finder.getCompletionContext(offset);
 *
 *   // Find enclosing function
 *   auto* func = finder.findEnclosingFunction(offset);
 */
class NodeFinder {
public:
  explicit NodeFinder(ast::CompilationUnitNode *root) : root_(root) {}

  // ========================================================================
  // Position-Based Node Finding
  // ========================================================================

  /**
   * @brief Find the most specific node containing the given offset
   * @param offset Byte offset in source
   * @return FindResult with node and context
   */
  [[nodiscard]] FindResult findNodeAt(uint32_t offset) const {
    LSP_LOG_SEP("findNodeAt");
    LSP_LOG("offset=" << offset << ", root_=" << (void *)root_);

    FindResult result;

    if (!root_) {
      LSP_LOG("root_ is nullptr, returning empty result");
      return result;
    }

    LSP_LOG_NODE_EX("root", root_);

    std::vector<ast::AstNode *> path;
    ast::AstNode *best = findDeepestAt(root_, offset, path);

    LSP_LOG("findDeepestAt returned: " << (best ? ast::astKindToString(best->kind) : "nullptr"));
    LSP_LOG("path.size=" << path.size());

    if (best) {
      result.context.node = best;
      // path 现在是从根到叶的正确顺序
      result.context.ancestors = std::move(path);
      result.completion = analyzeCompletionContext(result.context, offset);
      LSP_LOG("result.context.node=" << ast::astKindToString(result.context.node->kind));
    } else {
      LSP_LOG("best is nullptr, result will be invalid");
    }

    return result;
  }

  /**
   * @brief Find node at (line, column) position
   * @param line 1-based line number
   * @param column 1-based column number
   * @param lineTable Line offset table for conversion
   */
  [[nodiscard]] FindResult findNodeAt(uint32_t line, uint32_t column,
                                      const LineOffsetTable &lineTable) const {
    uint32_t offset = lineTable.getOffset(Position{line, column});
    return findNodeAt(offset);
  }

  /**
   * @brief Find all nodes whose range contains the offset
   * @param offset Byte offset
   * @return Vector of nodes from outermost to innermost
   */
  [[nodiscard]] std::vector<ast::AstNode *> findAllContaining(uint32_t offset) const {
    std::vector<ast::AstNode *> result;

    if (!root_) {
      return result;
    }

    collectContaining(root_, offset, result);
    return result;
  }

  // ========================================================================
  // Completion Context
  // ========================================================================

  /**
   * @brief Get completion context at offset
   */
  [[nodiscard]] CompletionContext getCompletionContext(uint32_t offset) const {
    auto result = findNodeAt(offset);
    return result.completion;
  }

  // ========================================================================
  // Scope & Declaration Finding
  // ========================================================================

  /**
   * @brief Find the enclosing function/method/lambda at offset
   */
  [[nodiscard]] ast::AstNode *findEnclosingFunction(uint32_t offset) const {
    auto result = findNodeAt(offset);
    if (!result.valid()) {
      return nullptr;
    }

    // Check ancestors for function-like nodes
    if (auto *func = result.context.findAncestor<ast::FunctionDeclNode>()) {
      return func;
    }
    if (auto *method = result.context.findAncestor<ast::MethodDeclNode>()) {
      return method;
    }
    if (auto *lambda = result.context.findAncestor<ast::LambdaExprNode>()) {
      return lambda;
    }

    return nullptr;
  }

  /**
   * @brief Find the enclosing class at offset
   */
  [[nodiscard]] ast::ClassDeclNode *findEnclosingClass(uint32_t offset) const {
    auto result = findNodeAt(offset);
    if (!result.valid()) {
      return nullptr;
    }
    return result.context.findAncestor<ast::ClassDeclNode>();
  }

  /**
   * @brief Find the enclosing block scope at offset
   */
  [[nodiscard]] ast::BlockStmtNode *findEnclosingBlock(uint32_t offset) const {
    auto result = findNodeAt(offset);
    if (!result.valid()) {
      return nullptr;
    }
    return result.context.findAncestor<ast::BlockStmtNode>();
  }

  /**
   * @brief Find declarations visible at offset
   * @param offset Position to search from
   * @return Vector of visible declarations
   */
  [[nodiscard]] std::vector<ast::Decl *> findVisibleDeclarations(uint32_t offset) const {
    std::vector<ast::Decl *> result;

    auto findResult = findNodeAt(offset);
    if (!findResult.valid()) {
      return result;
    }

    // Walk up the ancestor chain, collecting declarations from each scope
    for (auto it = findResult.context.ancestors.rbegin(); it != findResult.context.ancestors.rend();
         ++it) {
      collectDeclarationsInScope(*it, offset, result);
    }

    // Also collect top-level declarations
    if (root_) {
      collectTopLevelDeclarations(result);
    }

    return result;
  }

  // ========================================================================
  // Reference Finding
  // ========================================================================

  /**
   * @brief Find all references to a name in the AST
   * @param name The identifier name to find
   * @return Vector of identifier nodes matching the name
   */
  [[nodiscard]] std::vector<ast::IdentifierNode *>
  findAllReferences(const ast::StringTable &strings, ast::InternedString name) const {
    std::vector<ast::IdentifierNode *> result;

    if (!root_) {
      return result;
    }

    findReferencesRecursive(root_, strings, name, result);
    return result;
  }

  // ========================================================================
  // Node Type Queries
  // ========================================================================

  /**
   * @brief Find all nodes of a specific type
   */
  template <typename T> [[nodiscard]] std::vector<T *> findAllOfType() const {
    std::vector<T *> result;

    if (!root_) {
      return result;
    }

    findByType<T>(root_, result);
    return result;
  }

  /**
   * @brief Find all function declarations
   */
  [[nodiscard]] std::vector<ast::FunctionDeclNode *> findAllFunctions() const {
    return findAllOfType<ast::FunctionDeclNode>();
  }

  /**
   * @brief Find all class declarations
   */
  [[nodiscard]] std::vector<ast::ClassDeclNode *> findAllClasses() const {
    return findAllOfType<ast::ClassDeclNode>();
  }

  /**
   * @brief Find all variable declarations
   */
  [[nodiscard]] std::vector<ast::VarDeclNode *> findAllVariables() const {
    return findAllOfType<ast::VarDeclNode>();
  }

  // ========================================================================
  // Static Child Iteration Utility
  // ========================================================================

  /**
   * @brief Iterate over all children of a node
   */
  template <typename Visitor> static void forEachChild(ast::AstNode *node, Visitor &&visitor) {
    if (!node)
      return;

    switch (node->kind) {
    // Expressions
    case ast::AstKind::BinaryExpr: {
      auto *n = static_cast<ast::BinaryExprNode *>(node);
      visitor(n->left);
      visitor(n->right);
      break;
    }
    case ast::AstKind::UnaryExpr: {
      auto *n = static_cast<ast::UnaryExprNode *>(node);
      visitor(n->operand);
      break;
    }
    case ast::AstKind::CallExpr: {
      auto *n = static_cast<ast::CallExprNode *>(node);
      visitor(n->callee);
      for (auto *arg : n->arguments)
        visitor(arg);
      break;
    }
    case ast::AstKind::MemberAccessExpr: {
      auto *n = static_cast<ast::MemberAccessExprNode *>(node);
      visitor(n->base);
      break;
    }
    case ast::AstKind::IndexExpr: {
      auto *n = static_cast<ast::IndexExprNode *>(node);
      visitor(n->base);
      visitor(n->index);
      break;
    }
    case ast::AstKind::ColonLookupExpr: {
      auto *n = static_cast<ast::ColonLookupExprNode *>(node);
      visitor(n->base);
      break;
    }
    case ast::AstKind::NewExpr: {
      auto *n = static_cast<ast::NewExprNode *>(node);
      visitor(n->typeName);
      for (auto *arg : n->arguments)
        visitor(arg);
      break;
    }
    case ast::AstKind::ListExpr: {
      auto *n = static_cast<ast::ListExprNode *>(node);
      for (auto *elem : n->elements)
        visitor(elem);
      break;
    }
    case ast::AstKind::MapExpr: {
      auto *n = static_cast<ast::MapExprNode *>(node);
      for (auto *entry : n->entries)
        visitor(entry);
      break;
    }
    case ast::AstKind::MapEntryExpr: {
      auto *n = static_cast<ast::MapEntryNode *>(node);
      visitor(n->key);
      visitor(n->value);
      break;
    }
    case ast::AstKind::LambdaExpr: {
      auto *n = static_cast<ast::LambdaExprNode *>(node);
      visitor(n->returnType);
      for (auto *p : n->params)
        visitor(p);
      visitor(n->body);
      break;
    }
    case ast::AstKind::ParenExpr: {
      auto *n = static_cast<ast::ParenExprNode *>(node);
      visitor(n->inner);
      break;
    }

    // Statements
    case ast::AstKind::ExprStmt: {
      auto *n = static_cast<ast::ExprStmtNode *>(node);
      visitor(n->expr);
      break;
    }
    case ast::AstKind::BlockStmt: {
      auto *n = static_cast<ast::BlockStmtNode *>(node);
      for (auto *stmt : n->statements)
        visitor(stmt);
      break;
    }
    case ast::AstKind::AssignStmt: {
      auto *n = static_cast<ast::AssignStmtNode *>(node);
      visitor(n->target.expr);
      visitor(n->value);
      break;
    }
    case ast::AstKind::MultiAssignStmt: {
      auto *n = static_cast<ast::MultiAssignStmtNode *>(node);
      for (const auto &t : n->targets)
        visitor(t.expr);
      for (auto *v : n->values)
        visitor(v);
      break;
    }
    case ast::AstKind::UpdateAssignStmt: {
      auto *n = static_cast<ast::UpdateAssignStmtNode *>(node);
      visitor(n->target.expr);
      visitor(n->value);
      break;
    }
    case ast::AstKind::IfStmt: {
      auto *n = static_cast<ast::IfStmtNode *>(node);
      for (const auto &branch : n->branches) {
        visitor(branch.condition);
        visitor(branch.body);
      }
      if (n->elseBody)
        visitor(n->elseBody);
      break;
    }
    case ast::AstKind::WhileStmt: {
      auto *n = static_cast<ast::WhileStmtNode *>(node);
      visitor(n->condition);
      visitor(n->body);
      break;
    }
    case ast::AstKind::ForStmt: {
      auto *n = static_cast<ast::ForStmtNode *>(node);
      if (n->init)
        visitor(n->init);
      if (n->condition)
        visitor(n->condition);
      for (auto *u : n->updates)
        visitor(u);
      for (auto *v : n->iterVars)
        visitor(v);
      if (n->collection)
        visitor(n->collection);
      visitor(n->body);
      break;
    }
    case ast::AstKind::ReturnStmt: {
      auto *n = static_cast<ast::ReturnStmtNode *>(node);
      for (auto *v : n->values)
        visitor(v);
      break;
    }
    case ast::AstKind::DeferStmt: {
      auto *n = static_cast<ast::DeferStmtNode *>(node);
      visitor(n->body);
      break;
    }
    case ast::AstKind::DeclStmt: {
      auto *n = static_cast<ast::DeclStmtNode *>(node);
      visitor(n->decl);
      break;
    }

    // Declarations
    case ast::AstKind::VarDecl: {
      auto *n = static_cast<ast::VarDeclNode *>(node);
      visitor(n->type);
      if (n->initializer)
        visitor(n->initializer);
      break;
    }
    case ast::AstKind::MultiVarDecl: {
      auto *n = static_cast<ast::MultiVarDeclNode *>(node);
      if (n->initializer)
        visitor(n->initializer);
      break;
    }
    case ast::AstKind::ParameterDecl: {
      auto *n = static_cast<ast::ParameterDeclNode *>(node);
      visitor(n->type);
      break;
    }
    case ast::AstKind::FunctionDecl: {
      auto *n = static_cast<ast::FunctionDeclNode *>(node);
      visitor(n->returnType);
      for (auto *p : n->parameters)
        visitor(p);
      visitor(n->body);
      break;
    }
    case ast::AstKind::FieldDecl: {
      auto *n = static_cast<ast::FieldDeclNode *>(node);
      visitor(n->type);
      if (n->initializer)
        visitor(n->initializer);
      break;
    }
    case ast::AstKind::MethodDecl: {
      auto *n = static_cast<ast::MethodDeclNode *>(node);
      visitor(n->returnType);
      for (auto *p : n->parameters)
        visitor(p);
      visitor(n->body);
      break;
    }
    case ast::AstKind::ClassDecl: {
      auto *n = static_cast<ast::ClassDeclNode *>(node);
      for (auto *f : n->fields)
        visitor(f);
      for (auto *m : n->methods)
        visitor(m);
      break;
    }
    case ast::AstKind::CompilationUnit: {
      auto *n = static_cast<ast::CompilationUnitNode *>(node);
      LSP_LOG("CompilationUnit has " << n->statements.size() << " statements");
      for (size_t i = 0; i < n->statements.size(); ++i) {
        LSP_LOG("  stmt[" << i << "]: "
                          << (n->statements[i] ? ast::astKindToString(n->statements[i]->kind)
                                               : "nullptr"));
        visitor(n->statements[i]);
      }
      break;
    }

    // Types
    case ast::AstKind::ListType: {
      auto *n = static_cast<ast::ListTypeNode *>(node);
      if (n->elementType)
        visitor(n->elementType);
      break;
    }
    case ast::AstKind::MapType: {
      auto *n = static_cast<ast::MapTypeNode *>(node);
      if (n->keyType)
        visitor(n->keyType);
      if (n->valueType)
        visitor(n->valueType);
      break;
    }
    case ast::AstKind::QualifiedType: {
      auto *n = static_cast<ast::QualifiedTypeNode *>(node);
      visitor(n->name);
      break;
    }

    // Leaf nodes - no children
    default:
      break;
    }
  }

private:
  ast::CompilationUnitNode *root_;

  // ========================================================================
  // Recursive Search Helpers
  // ========================================================================

  /**
   * @brief 递归查找包含指定偏移的最深节点
   *
   * 修复: 现在正确地构建从根到叶的路径
   * 修复: CompilationUnit 即使 range 无效也继续搜索子节点
   */
  [[nodiscard]] ast::AstNode *findDeepestAt(ast::AstNode *node, uint32_t offset,
                                            std::vector<ast::AstNode *> &path) const {
    LSP_LOG_SEARCH(offset, node);

    if (!node) {
      LSP_LOG("  -> node is nullptr, return nullptr");
      return nullptr;
    }

    // CompilationUnit 特殊处理：即使 range 无效也搜索子节点
    bool isCompilationUnit = (node->kind == ast::AstKind::CompilationUnit);
    LSP_LOG("  isCompilationUnit=" << isCompilationUnit
                                   << ", range.isValid()=" << node->range.isValid()
                                   << ", range.begin.line=" << node->range.begin.line
                                   << ", range.begin.offset=" << node->range.begin.offset
                                   << ", range.end.offset=" << node->range.end.offset);

    // 对于非 CompilationUnit 节点，检查 range 有效性
    if (!isCompilationUnit && !node->range.isValid()) {
      LSP_LOG("  -> range invalid (non-CU), return nullptr");
      return nullptr;
    }

    // 检查 offset 是否在节点范围内（CompilationUnit 跳过此检查）
    if (!isCompilationUnit) {
      if (offset < node->range.begin.offset || offset >= node->range.end.offset) {
        LSP_LOG("  -> offset " << offset << " not in range [" << node->range.begin.offset << ", "
                               << node->range.end.offset << "), return nullptr");
        return nullptr;
      }
    }

    // 此节点包含 offset（或是 CompilationUnit），将其添加到路径
    path.push_back(node);
    LSP_LOG("  -> added to path, path.size=" << path.size());

    // 尝试在子节点中找到更深的匹配
    ast::AstNode *deeperResult = nullptr;
    int childIndex = 0;

    forEachChild(node, [&](ast::AstNode *child) {
      LSP_LOG("  checking child[" << childIndex << "]:");
      childIndex++;
      if (!deeperResult) { // 只取第一个匹配的子节点
        if (auto *found = findDeepestAt(child, offset, path)) {
          LSP_LOG("  -> found in child!");
          deeperResult = found;
        }
      }
    });

    // 如果在子节点中找到了，返回那个结果
    // 否则返回当前节点
    if (deeperResult) {
      LSP_LOG("  -> returning deeper result: " << ast::astKindToString(deeperResult->kind));
      return deeperResult;
    }

    // 当前节点是最深的匹配，它已经在路径末尾
    LSP_LOG("  -> returning current node: " << ast::astKindToString(node->kind));
    return node;
  }

  void collectContaining(ast::AstNode *node, uint32_t offset,
                         std::vector<ast::AstNode *> &result) const {
    if (!node) {
      return;
    }

    // CompilationUnit 特殊处理
    bool isCompilationUnit = (node->kind == ast::AstKind::CompilationUnit);

    if (!isCompilationUnit && !node->range.isValid()) {
      return;
    }

    if (!isCompilationUnit) {
      if (offset < node->range.begin.offset || offset >= node->range.end.offset) {
        return;
      }
    }

    result.push_back(node);

    forEachChild(node, [&](ast::AstNode *child) { collectContaining(child, offset, result); });
  }

  // ========================================================================
  // Completion Context Analysis
  // ========================================================================

  [[nodiscard]] CompletionContext analyzeCompletionContext(const NodeContext &ctx,
                                                           uint32_t offset) const {
    CompletionContext result;
    result.enclosingNode = ctx.parent();

    // Check if we're in a member access expression
    if (auto *member = ast::ast_cast<ast::MemberAccessExprNode>(ctx.node)) {
      result.trigger = CompletionTrigger::DotAccess;
      result.baseExpr = member->base;
      return result;
    }

    // Check if parent is incomplete member access (obj.|)
    if (ctx.parent()) {
      if (auto *member = ast::ast_cast<ast::MemberAccessExprNode>(ctx.parent())) {
        if (member->isIncomplete()) {
          result.trigger = CompletionTrigger::DotAccess;
          result.baseExpr = member->base;
          return result;
        }
      }
    }

    // Check for colon lookup
    if (auto *colon = ast::ast_cast<ast::ColonLookupExprNode>(ctx.node)) {
      result.trigger = CompletionTrigger::ColonAccess;
      result.baseExpr = colon->base;
      return result;
    }

    // Check for new expression
    if (ast::ast_cast<ast::NewExprNode>(ctx.node) || ctx.findAncestor<ast::NewExprNode>()) {
      result.trigger = CompletionTrigger::NewExpression;
      return result;
    }

    // Check for function call (argument completion)
    if (auto *call = ctx.findAncestor<ast::CallExprNode>()) {
      result.trigger = CompletionTrigger::Argument;
      result.baseExpr = call->callee;
      // Determine argument index based on offset
      result.argumentIndex = calculateArgumentIndex(call, offset);
      return result;
    }

    // Check for type position
    if (isInTypePosition(ctx)) {
      result.trigger = CompletionTrigger::TypeAnnotation;
      return result;
    }

    // Check for import
    if (ctx.findAncestor<ast::ImportStmtNode>()) {
      result.trigger = CompletionTrigger::Import;
      return result;
    }

    // Default: identifier completion
    if (ast::ast_cast<ast::IdentifierNode>(ctx.node)) {
      result.trigger = CompletionTrigger::Identifier;
    }

    return result;
  }

  [[nodiscard]] bool isInTypePosition(const NodeContext &ctx) const {
    // Check if we're inside a type annotation node
    if (ast::isType(ctx.node->kind)) {
      return true;
    }

    // Check if parent expects a type
    auto *parent = ctx.parent();
    if (!parent)
      return false;

    // Variable declaration type position
    if (auto *var = ast::ast_cast<ast::VarDeclNode>(parent)) {
      return ctx.node == var->type;
    }

    // Parameter type position
    if (auto *param = ast::ast_cast<ast::ParameterDeclNode>(parent)) {
      return ctx.node == param->type;
    }

    // Function return type
    if (auto *func = ast::ast_cast<ast::FunctionDeclNode>(parent)) {
      return ctx.node == func->returnType;
    }

    return false;
  }

  [[nodiscard]] uint32_t calculateArgumentIndex(ast::CallExprNode *call, uint32_t offset) const {
    if (call->arguments.empty()) {
      return 0;
    }

    uint32_t index = 0;
    for (auto *arg : call->arguments) {
      if (arg && arg->range.isValid()) {
        if (offset < arg->range.begin.offset) {
          return index;
        }
        if (offset >= arg->range.begin.offset && offset <= arg->range.end.offset) {
          return index;
        }
      }
      ++index;
    }

    return static_cast<uint32_t>(call->arguments.size());
  }

  // ========================================================================
  // Declaration Collection
  // ========================================================================

  void collectDeclarationsInScope(ast::AstNode *scopeNode, uint32_t offset,
                                  std::vector<ast::Decl *> &result) const {
    if (!scopeNode)
      return;

    // Only collect from block statements
    if (auto *block = ast::ast_cast<ast::BlockStmtNode>(scopeNode)) {
      for (auto *stmt : block->statements) {
        // Only include declarations before the offset
        if (stmt->range.begin.offset > offset) {
          break;
        }

        if (auto *declStmt = ast::ast_cast<ast::DeclStmtNode>(stmt)) {
          if (declStmt->decl) {
            result.push_back(declStmt->decl);
          }
        }
      }
    }

    // Collect parameters from functions
    if (auto *func = ast::ast_cast<ast::FunctionDeclNode>(scopeNode)) {
      for (auto *param : func->parameters) {
        result.push_back(param);
      }
    }

    // Collect members from classes
    if (auto *cls = ast::ast_cast<ast::ClassDeclNode>(scopeNode)) {
      for (auto *field : cls->fields) {
        result.push_back(field);
      }
      for (auto *method : cls->methods) {
        result.push_back(method);
      }
    }
  }

  void collectTopLevelDeclarations(std::vector<ast::Decl *> &result) const {
    if (!root_)
      return;

    for (auto *stmt : root_->statements) {
      if (auto *declStmt = ast::ast_cast<ast::DeclStmtNode>(stmt)) {
        if (declStmt->decl) {
          result.push_back(declStmt->decl);
        }
      }
    }
  }

  // ========================================================================
  // Reference Finding
  // ========================================================================

  void findReferencesRecursive(ast::AstNode *node, const ast::StringTable &strings,
                               ast::InternedString name,
                               std::vector<ast::IdentifierNode *> &result) const {
    if (!node)
      return;

    if (auto *ident = ast::ast_cast<ast::IdentifierNode>(node)) {
      if (ident->name == name) {
        result.push_back(ident);
      }
    }

    forEachChild(
        node, [&](ast::AstNode *child) { findReferencesRecursive(child, strings, name, result); });
  }

  // ========================================================================
  // Type-based Finding
  // ========================================================================

  template <typename T> void findByType(ast::AstNode *node, std::vector<T *> &result) const {
    if (!node)
      return;

    if (auto *typed = ast::ast_cast<T>(node)) {
      result.push_back(typed);
    }

    forEachChild(node, [&](ast::AstNode *child) { findByType<T>(child, result); });
  }

  // ========================================================================
  // Visitor Helper (for visitChildren)
  // ========================================================================

  void visitChildren(ast::AstNode *node, const std::function<void(ast::AstNode *)> &visitor) const {
    forEachChild(node, visitor);
  }
};

// ============================================================================
// Convenience Functions
// ============================================================================

/**
 * @brief Find node at position (convenience function)
 */
[[nodiscard]] inline FindResult findNodeAt(ast::CompilationUnitNode *root, uint32_t offset) {
  NodeFinder finder(root);
  return finder.findNodeAt(offset);
}

/**
 * @brief Find node at (line, column) (convenience function)
 */
[[nodiscard]] inline FindResult findNodeAt(ast::CompilationUnitNode *root, uint32_t line,
                                           uint32_t column, const LineOffsetTable &lineTable) {
  NodeFinder finder(root);
  return finder.findNodeAt(line, column, lineTable);
}

/**
 * @brief Get completion trigger at position (convenience function)
 */
[[nodiscard]] inline CompletionContext getCompletionContext(ast::CompilationUnitNode *root,
                                                            uint32_t offset) {
  NodeFinder finder(root);
  return finder.getCompletionContext(offset);
}

} // namespace lsp
} // namespace lang
