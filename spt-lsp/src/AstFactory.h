/**
 * @file AstFactory.h
 * @brief Arena Allocator and AST Node Factory for Tolerant AST Construction
 *
 * This file provides:
 * 1. Arena allocator for efficient node allocation
 * 2. AstFactory for creating properly initialized AST nodes
 * 3. String interning table
 *
 * Design Goals:
 * - Zero allocation failures (arena always succeeds or throws)
 * - All nodes created through factory (enforces never-null invariant)
 * - Efficient memory usage with bulk deallocation
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "AstNodes.h"
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <unordered_map>
#include <vector>

namespace lang {
namespace ast {

// ============================================================================
// Arena Allocator
// ============================================================================

/**
 * @brief Fast arena allocator for AST nodes
 *
 * Features:
 * - Block-based allocation (no individual frees)
 * - Alignment-aware
 * - Efficient for many small allocations
 * - Single bulk deallocation
 */
class Arena {
public:
  static constexpr size_t DefaultBlockSize = 64 * 1024; // 64KB blocks
  static constexpr size_t MaxAlignment = alignof(std::max_align_t);

  explicit Arena(size_t blockSize = DefaultBlockSize)
      : blockSize_(blockSize), current_(nullptr), end_(nullptr) {}

  ~Arena() { clear(); }

  // Non-copyable, movable
  Arena(const Arena &) = delete;
  Arena &operator=(const Arena &) = delete;

  Arena(Arena &&other) noexcept
      : blockSize_(other.blockSize_), blocks_(std::move(other.blocks_)), current_(other.current_),
        end_(other.end_), totalAllocated_(other.totalAllocated_) {
    other.current_ = nullptr;
    other.end_ = nullptr;
    other.totalAllocated_ = 0;
  }

  Arena &operator=(Arena &&other) noexcept {
    if (this != &other) {
      clear();
      blockSize_ = other.blockSize_;
      blocks_ = std::move(other.blocks_);
      current_ = other.current_;
      end_ = other.end_;
      totalAllocated_ = other.totalAllocated_;
      other.current_ = nullptr;
      other.end_ = nullptr;
      other.totalAllocated_ = 0;
    }
    return *this;
  }

  /**
   * @brief Allocate raw memory with alignment
   */
  [[nodiscard]] void *allocate(size_t size, size_t alignment = MaxAlignment) {
    // Align current pointer
    size_t space = end_ - current_;
    void *aligned = current_;
    if (!std::align(alignment, size, aligned, space)) {
      // Need new block
      allocateBlock(std::max(size + alignment, blockSize_));
      space = end_ - current_;
      aligned = current_;
      if (!std::align(alignment, size, aligned, space)) {
        throw std::bad_alloc();
      }
    }

    current_ = static_cast<char *>(aligned) + size;
    totalAllocated_ += size;
    return aligned;
  }

  /**
   * @brief Allocate and construct an object
   */
  template <typename T, typename... Args> [[nodiscard]] T *make(Args &&...args) {
    void *mem = allocate(sizeof(T), alignof(T));
    return new (mem) T(std::forward<Args>(args)...);
  }

  /**
   * @brief Allocate an array
   */
  template <typename T> [[nodiscard]] T *allocateArray(size_t count) {
    if (count == 0)
      return nullptr;
    void *mem = allocate(sizeof(T) * count, alignof(T));
    // Default construct
    T *arr = static_cast<T *>(mem);
    for (size_t i = 0; i < count; ++i) {
      new (&arr[i]) T();
    }
    return arr;
  }

  /**
   * @brief Create ArrayView from vector (copies data into arena)
   */
  template <typename T> [[nodiscard]] ArrayView<T> makeArrayView(const std::vector<T> &vec) {
    if (vec.empty()) {
      return ArrayView<T>{nullptr, 0};
    }
    T *data = allocateArray<T>(vec.size());
    for (size_t i = 0; i < vec.size(); ++i) {
      data[i] = vec[i];
    }
    return ArrayView<T>{data, static_cast<uint32_t>(vec.size())};
  }

  /**
   * @brief Create ArrayView from initializer list
   */
  template <typename T> [[nodiscard]] ArrayView<T> makeArrayView(std::initializer_list<T> list) {
    if (list.size() == 0) {
      return ArrayView<T>{nullptr, 0};
    }
    T *data = allocateArray<T>(list.size());
    size_t i = 0;
    for (const auto &item : list) {
      data[i++] = item;
    }
    return ArrayView<T>{data, static_cast<uint32_t>(list.size())};
  }

  /**
   * @brief Duplicate a string into the arena
   */
  [[nodiscard]] char *strdup(const char *str) {
    if (!str)
      return nullptr;
    size_t len = std::strlen(str) + 1;
    char *copy = static_cast<char *>(allocate(len, 1));
    std::memcpy(copy, str, len);
    return copy;
  }

  [[nodiscard]] char *strdup(std::string_view str) {
    char *copy = static_cast<char *>(allocate(str.size() + 1, 1));
    std::memcpy(copy, str.data(), str.size());
    copy[str.size()] = '\0';
    return copy;
  }

  /**
   * @brief Clear all allocations
   */
  void clear() {
    for (auto *block : blocks_) {
      std::free(block);
    }
    blocks_.clear();
    current_ = nullptr;
    end_ = nullptr;
    totalAllocated_ = 0;
  }

  /**
   * @brief Get total bytes allocated
   */
  [[nodiscard]] size_t totalAllocated() const noexcept { return totalAllocated_; }

  /**
   * @brief Get number of blocks allocated
   */
  [[nodiscard]] size_t blockCount() const noexcept { return blocks_.size(); }

private:
  void allocateBlock(size_t minSize) {
    size_t size = std::max(minSize, blockSize_);
    char *block = static_cast<char *>(std::malloc(size));
    if (!block)
      throw std::bad_alloc();
    blocks_.push_back(block);
    current_ = block;
    end_ = block + size;
  }

  size_t blockSize_;
  std::vector<char *> blocks_;
  char *current_;
  char *end_;
  size_t totalAllocated_ = 0;
};

// ============================================================================
// String Intern Table
// ============================================================================

/**
 * @brief String interning table for efficient string storage and comparison
 */
class StringTable {
public:
  StringTable() {
    // Reserve ID 0 for empty/invalid
    strings_.emplace_back("");
  }

  /**
   * @brief Intern a string and get its ID
   */
  [[nodiscard]] InternedString intern(std::string_view str) {
    auto it = index_.find(std::string(str));
    if (it != index_.end()) {
      return InternedString{it->second};
    }

    uint32_t id = static_cast<uint32_t>(strings_.size());
    strings_.emplace_back(str);
    index_[strings_.back()] = id;
    return InternedString{id};
  }

  /**
   * @brief Get string by ID
   */
  [[nodiscard]] std::string_view get(InternedString id) const {
    if (id.id >= strings_.size()) {
      return "";
    }
    return strings_[id.id];
  }

  /**
   * @brief Get string count
   */
  [[nodiscard]] size_t size() const noexcept { return strings_.size(); }

private:
  std::vector<std::string> strings_;
  std::unordered_map<std::string, uint32_t> index_;
};

// ============================================================================
// AST Factory
// ============================================================================

/**
 * @brief Factory for creating AST nodes with proper initialization
 *
 * Key guarantees:
 * - All nodes are properly initialized
 * - Never returns nullptr (uses Error nodes instead)
 * - Manages arena and string table
 */
class AstFactory {
public:
  AstFactory() = default;

  explicit AstFactory(size_t arenaBlockSize) : arena_(arenaBlockSize) {}

  // Access to underlying components
  [[nodiscard]] Arena &arena() noexcept { return arena_; }

  [[nodiscard]] StringTable &strings() noexcept { return strings_; }

  [[nodiscard]] const StringTable &strings() const noexcept { return strings_; }

  // ========================================================================
  // Error/Placeholder Node Creation
  // ========================================================================

  [[nodiscard]] ErrorExprNode *makeErrorExpr(SourceRange range, std::string_view message = "") {
    auto *node = arena_.make<ErrorExprNode>();
    node->kind = AstKind::ErrorExpr;
    node->flags = NodeFlags::HasError;
    node->range = range;
    node->errorMessage = strings_.intern(message);
    return node;
  }

  [[nodiscard]] MissingExprNode *makeMissingExpr(SourceRange range) {
    auto *node = arena_.make<MissingExprNode>();
    node->kind = AstKind::MissingExpr;
    node->flags = NodeFlags::HasError;
    node->range = range;
    return node;
  }

  [[nodiscard]] ErrorStmtNode *makeErrorStmt(SourceRange range, std::string_view message = "") {
    auto *node = arena_.make<ErrorStmtNode>();
    node->kind = AstKind::ErrorStmt;
    node->flags = NodeFlags::HasError;
    node->range = range;
    node->errorMessage = strings_.intern(message);
    return node;
  }

  [[nodiscard]] ErrorDeclNode *makeErrorDecl(SourceRange range, std::string_view message = "") {
    auto *node = arena_.make<ErrorDeclNode>();
    node->kind = AstKind::ErrorDecl;
    node->flags = NodeFlags::HasError;
    node->range = range;
    node->errorMessage = strings_.intern(message);
    return node;
  }

  [[nodiscard]] ErrorTypeNode *makeErrorType(SourceRange range, std::string_view message = "") {
    auto *node = arena_.make<ErrorTypeNode>();
    node->kind = AstKind::ErrorType;
    node->flags = NodeFlags::HasError;
    node->range = range;
    node->errorMessage = strings_.intern(message);
    return node;
  }

  // ========================================================================
  // Literal Expression Creation
  // ========================================================================

  [[nodiscard]] NullLiteralNode *makeNullLiteral(SourceRange range) {
    auto *node = arena_.make<NullLiteralNode>();
    node->kind = AstKind::NullLiteral;
    node->range = range;
    return node;
  }

  [[nodiscard]] BoolLiteralNode *makeBoolLiteral(SourceRange range, bool value) {
    auto *node = arena_.make<BoolLiteralNode>();
    node->kind = AstKind::BoolLiteral;
    node->range = range;
    node->value = value;
    return node;
  }

  [[nodiscard]] IntLiteralNode *makeIntLiteral(SourceRange range, int64_t value,
                                               bool isHex = false) {
    auto *node = arena_.make<IntLiteralNode>();
    node->kind = AstKind::IntLiteral;
    node->range = range;
    node->value = value;
    node->isHex = isHex;
    return node;
  }

  [[nodiscard]] FloatLiteralNode *makeFloatLiteral(SourceRange range, double value) {
    auto *node = arena_.make<FloatLiteralNode>();
    node->kind = AstKind::FloatLiteral;
    node->range = range;
    node->value = value;
    return node;
  }

  [[nodiscard]] StringLiteralNode *makeStringLiteral(SourceRange range, std::string_view value,
                                                     std::string_view rawValue = "") {
    auto *node = arena_.make<StringLiteralNode>();
    node->kind = AstKind::StringLiteral;
    node->range = range;
    node->value = strings_.intern(value);
    node->rawValue = strings_.intern(rawValue.empty() ? value : rawValue);
    return node;
  }

  // ========================================================================
  // Identifier & Access Expression Creation
  // ========================================================================

  [[nodiscard]] IdentifierNode *makeIdentifier(SourceRange range, std::string_view name) {
    auto *node = arena_.make<IdentifierNode>();
    node->kind = AstKind::Identifier;
    node->range = range;
    node->name = strings_.intern(name);
    return node;
  }

  [[nodiscard]] QualifiedIdentifierNode *
  makeQualifiedIdentifier(SourceRange range, const std::vector<std::string_view> &parts,
                          const std::vector<SourceRange> &partsRange = {}) {
    auto *node = arena_.make<QualifiedIdentifierNode>();
    node->kind = AstKind::QualifiedIdentifier;
    node->range = range;

    std::vector<InternedString> internedParts;
    internedParts.reserve(parts.size());
    for (const auto &part : parts) {
      internedParts.push_back(strings_.intern(part));
    }
    node->parts = arena_.makeArrayView(internedParts);

    // 存储每个部分的位置范围
    LSP_LOG("makeQualifiedIdentifier: partsRange.size()=" << partsRange.size());
    if (!partsRange.empty()) {
      node->partsRange = arena_.makeArrayView(partsRange);
      LSP_LOG("makeQualifiedIdentifier: node->partsRange.size()=" << node->partsRange.size());
      for (size_t i = 0; i < node->partsRange.size(); ++i) {
        LSP_LOG("  partsRange[" << i << "]=[" << node->partsRange[i].begin.offset << "-"
                                << node->partsRange[i].end.offset << "]");
      }
    }

    return node;
  }

  /**
   * @brief Create member access expression
   *
   * @param range Source range
   * @param base Base expression (MUST NOT be null - use ErrorExpr)
   * @param member Member name (empty for incomplete "obj.")
   * @param memberRange Location of the member name (for hover/definition)
   * @param isIncomplete True if cursor is after dot (for completion)
   */
  [[nodiscard]] MemberAccessExprNode *makeMemberAccessExpr(SourceRange range, Expr *base,
                                                           std::string_view member,
                       SourceRange memberRange = SourceRange::invalid(),
                       bool isIncomplete = false) {
    assert(base && "base must not be null - use ErrorExpr");

    auto *node = arena_.make<MemberAccessExprNode>();
    node->kind = AstKind::MemberAccessExpr;
    node->range = range;
    node->base = base;
    node->member = strings_.intern(member);
    node->memberRange = memberRange;
    if (isIncomplete) {
      node->flags = node->flags | NodeFlags::Incomplete;
    }
    if (base->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    return node;
  }

  [[nodiscard]] IndexExprNode *makeIndexExpr(SourceRange range, Expr *base, Expr *index) {
    assert(base && index && "base and index must not be null");

    auto *node = arena_.make<IndexExprNode>();
    node->kind = AstKind::IndexExpr;
    node->range = range;
    node->base = base;
    node->index = index;
    if (base->hasError() || index->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    return node;
  }

  // ========================================================================
  // Operator Expression Creation
  // ========================================================================

  [[nodiscard]] BinaryExprNode *makeBinaryExpr(SourceRange range, BinaryOp op, Expr *left,
                                               Expr *right, SourceLoc opLoc = {}) {
    assert(left && right && "operands must not be null");

    auto *node = arena_.make<BinaryExprNode>();
    node->kind = AstKind::BinaryExpr;
    node->range = range;
    node->op = op;
    node->left = left;
    node->right = right;
    node->opLoc = opLoc;
    if (left->hasError() || right->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    return node;
  }

  [[nodiscard]] UnaryExprNode *makeUnaryExpr(SourceRange range, UnaryOp op, Expr *operand,
                                             bool isPrefix = true) {
    assert(operand && "operand must not be null");

    auto *node = arena_.make<UnaryExprNode>();
    node->kind = AstKind::UnaryExpr;
    node->range = range;
    node->op = op;
    node->operand = operand;
    node->isPrefix = isPrefix;
    if (operand->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    return node;
  }

  // ========================================================================
  // Call & Construction Expression Creation
  // ========================================================================

  [[nodiscard]] CallExprNode *makeCallExpr(SourceRange range, Expr *callee,
                                           const std::vector<Expr *> &args,
                                           SourceRange parenRange = {}) {
    assert(callee && "callee must not be null");

    auto *node = arena_.make<CallExprNode>();
    node->kind = AstKind::CallExpr;
    node->range = range;
    node->callee = callee;
    node->arguments = arena_.makeArrayView(args);
    node->parenRange = parenRange;

    if (callee->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    for (auto *arg : args) {
      assert(arg && "argument must not be null");
      if (arg->hasError()) {
        node->flags = node->flags | NodeFlags::HasError;
      }
    }
    return node;
  }

  // ========================================================================
  // Compound Expression Creation
  // ========================================================================

  [[nodiscard]] ListExprNode *makeListExpr(SourceRange range, const std::vector<Expr *> &elements) {
    auto *node = arena_.make<ListExprNode>();
    node->kind = AstKind::ListExpr;
    node->range = range;
    node->elements = arena_.makeArrayView(elements);
    return node;
  }

  [[nodiscard]] MapEntryNode *makeMapEntry(SourceRange range, Expr *key, Expr *value,
                                           bool isIdentifierKey = false,
                                           bool isBracketedKey = false) {
    assert(key && value && "key and value must not be null");

    auto *node = arena_.make<MapEntryNode>();
    node->kind = AstKind::MapEntryExpr;
    node->range = range;
    node->key = key;
    node->value = value;
    node->isIdentifierKey = isIdentifierKey;
    node->isBracketedKey = isBracketedKey;
    return node;
  }

  [[nodiscard]] MapExprNode *makeMapExpr(SourceRange range,
                                         const std::vector<MapEntryNode *> &entries) {
    auto *node = arena_.make<MapExprNode>();
    node->kind = AstKind::MapExpr;
    node->range = range;
    node->entries = arena_.makeArrayView(entries);
    return node;
  }

  [[nodiscard]] LambdaExprNode *makeLambdaExpr(SourceRange range, TypeNode *returnType,
                                               const std::vector<ParameterDeclNode *> &params,
                                               BlockStmtNode *body, bool isMultiReturn = false) {
    assert(returnType && body && "returnType and body must not be null");

    auto *node = arena_.make<LambdaExprNode>();
    node->kind = AstKind::LambdaExpr;
    node->range = range;
    node->returnType = returnType;
    node->params = arena_.makeArrayView(params);
    node->body = body;
    node->isMultiReturn = isMultiReturn;
    return node;
  }

  [[nodiscard]] ParenExprNode *makeParenExpr(SourceRange range, Expr *inner) {
    assert(inner && "inner must not be null");

    auto *node = arena_.make<ParenExprNode>();
    node->kind = AstKind::ParenExpr;
    node->range = range;
    node->inner = inner;
    if (inner->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    return node;
  }

  [[nodiscard]] VarArgsExprNode *makeVarArgsExpr(SourceRange range) {
    auto *node = arena_.make<VarArgsExprNode>();
    node->kind = AstKind::VarArgsExpr;
    node->range = range;
    return node;
  }

  // ========================================================================
  // Statement Creation
  // ========================================================================

  [[nodiscard]] EmptyStmtNode *makeEmptyStmt(SourceRange range) {
    auto *node = arena_.make<EmptyStmtNode>();
    node->kind = AstKind::EmptyStmt;
    node->range = range;
    return node;
  }

  [[nodiscard]] ExprStmtNode *makeExprStmt(SourceRange range, Expr *expr) {
    assert(expr && "expr must not be null");

    auto *node = arena_.make<ExprStmtNode>();
    node->kind = AstKind::ExprStmt;
    node->range = range;
    node->expr = expr;
    if (expr->hasError()) {
      node->flags = node->flags | NodeFlags::HasError;
    }
    return node;
  }

  [[nodiscard]] BlockStmtNode *makeBlockStmt(SourceRange range,
                                             const std::vector<Stmt *> &statements) {
    auto *node = arena_.make<BlockStmtNode>();
    node->kind = AstKind::BlockStmt;
    node->range = range;
    node->statements = arena_.makeArrayView(statements);
    return node;
  }

  [[nodiscard]] AssignStmtNode *makeAssignStmt(SourceRange range, Expr *target, Expr *value) {
    assert(target && value && "target and value must not be null");

    auto *node = arena_.make<AssignStmtNode>();
    node->kind = AstKind::AssignStmt;
    node->range = range;
    node->target.expr = target;
    node->value = value;
    return node;
  }

  [[nodiscard]] MultiAssignStmtNode *makeMultiAssignStmt(SourceRange range,
                                                         const std::vector<Expr *> &targets,
                                                         const std::vector<Expr *> &values) {
    auto *node = arena_.make<MultiAssignStmtNode>();
    node->kind = AstKind::MultiAssignStmt;
    node->range = range;

    std::vector<LValue> lvalues;
    lvalues.reserve(targets.size());
    for (auto *t : targets) {
      assert(t && "target must not be null");
      lvalues.push_back(LValue{t});
    }
    node->targets = arena_.makeArrayView(lvalues);
    node->values = arena_.makeArrayView(values);
    return node;
  }

  [[nodiscard]] UpdateAssignStmtNode *makeUpdateAssignStmt(SourceRange range, UpdateOp op,
                                                           Expr *target, Expr *value) {
    assert(target && value && "target and value must not be null");

    auto *node = arena_.make<UpdateAssignStmtNode>();
    node->kind = AstKind::UpdateAssignStmt;
    node->range = range;
    node->op = op;
    node->target.expr = target;
    node->value = value;
    return node;
  }

  [[nodiscard]] IfStmtNode *makeIfStmt(SourceRange range,
                                       const std::vector<IfStmtNode::Branch> &branches,
                                       BlockStmtNode *elseBody = nullptr) {
    auto *node = arena_.make<IfStmtNode>();
    node->kind = AstKind::IfStmt;
    node->range = range;
    node->branches = arena_.makeArrayView(branches);
    node->elseBody = elseBody;
    return node;
  }

  [[nodiscard]] WhileStmtNode *makeWhileStmt(SourceRange range, Expr *condition,
                                             BlockStmtNode *body) {
    assert(condition && body && "condition and body must not be null");

    auto *node = arena_.make<WhileStmtNode>();
    node->kind = AstKind::WhileStmt;
    node->range = range;
    node->condition = condition;
    node->body = body;
    return node;
  }

  [[nodiscard]] ForStmtNode *makeForStmtNumeric(SourceRange range, VarDeclNode *numericVar,
                                                Expr *start, Expr *end, Expr *step,
                                                BlockStmtNode *body) {
    assert(numericVar && start && end && body && "numericVar, start, end and body must not be null");

    auto *node = arena_.make<ForStmtNode>();
    node->kind = AstKind::ForStmt;
    node->range = range;
    node->style = ForStmtNode::Style::Numeric;
    node->numericVar = numericVar;
    node->start = start;
    node->end = end;
    node->step = step;
    node->body = body;
    return node;
  }

  [[nodiscard]] ForStmtNode *makeForStmtCStyle(SourceRange range, Stmt *init, Expr *condition,
                                               const std::vector<Stmt *> &updates,
                                               BlockStmtNode *body) {
    assert(body && "body must not be null");

    auto *node = arena_.make<ForStmtNode>();
    node->kind = AstKind::ForStmt;
    node->range = range;
    node->style = ForStmtNode::Style::CStyle;
    node->init = init;
    node->condition = condition;
    node->updates = arena_.makeArrayView(updates);
    node->body = body;
    return node;
  }

  [[nodiscard]] ForStmtNode *makeForStmtForEach(SourceRange range,
                                                const std::vector<VarDeclNode *> &iterVars,
                                                Expr *collection, BlockStmtNode *body) {
    assert(collection && body && "collection and body must not be null");

    auto *node = arena_.make<ForStmtNode>();
    node->kind = AstKind::ForStmt;
    node->range = range;
    node->style = ForStmtNode::Style::ForEach;
    node->iterVars = arena_.makeArrayView(iterVars);
    node->collection = collection;
    node->body = body;
    return node;
  }

  [[nodiscard]] BreakStmtNode *makeBreakStmt(SourceRange range) {
    auto *node = arena_.make<BreakStmtNode>();
    node->kind = AstKind::BreakStmt;
    node->range = range;
    return node;
  }

  [[nodiscard]] ContinueStmtNode *makeContinueStmt(SourceRange range) {
    auto *node = arena_.make<ContinueStmtNode>();
    node->kind = AstKind::ContinueStmt;
    node->range = range;
    return node;
  }

  [[nodiscard]] ReturnStmtNode *makeReturnStmt(SourceRange range,
                                               const std::vector<Expr *> &values = {}) {
    auto *node = arena_.make<ReturnStmtNode>();
    node->kind = AstKind::ReturnStmt;
    node->range = range;
    node->values = arena_.makeArrayView(values);
    return node;
  }

  [[nodiscard]] DeferStmtNode *makeDeferStmt(SourceRange range, BlockStmtNode *body) {
    assert(body && "body must not be null");

    auto *node = arena_.make<DeferStmtNode>();
    node->kind = AstKind::DeferStmt;
    node->range = range;
    node->body = body;
    return node;
  }

  [[nodiscard]] ImportStmtNode *makeImportStmtNamespace(SourceRange range,
                                                        std::string_view modulePath,
                                                        std::string_view namespaceAlias) {
    auto *node = arena_.make<ImportStmtNode>();
    node->kind = AstKind::ImportStmt;
    node->range = range;
    node->style = ImportStmtNode::Style::Namespace;
    node->modulePath = strings_.intern(modulePath);
    node->namespaceAlias = strings_.intern(namespaceAlias);
    return node;
  }

  [[nodiscard]] ImportStmtNode *
  makeImportStmtNamed(SourceRange range, std::string_view modulePath,
                      const std::vector<ImportSpecifier> &specifiers) {
    auto *node = arena_.make<ImportStmtNode>();
    node->kind = AstKind::ImportStmt;
    node->range = range;
    node->style = ImportStmtNode::Style::Named;
    node->modulePath = strings_.intern(modulePath);
    node->specifiers = arena_.makeArrayView(specifiers);
    return node;
  }

  [[nodiscard]] DeclStmtNode *makeDeclStmt(SourceRange range, Decl *decl) {
    assert(decl && "decl must not be null");

    auto *node = arena_.make<DeclStmtNode>();
    node->kind = AstKind::DeclStmt;
    node->range = range;
    node->decl = decl;
    return node;
  }

  // ========================================================================
  // Declaration Creation
  // ========================================================================

  [[nodiscard]] VarDeclNode *makeVarDecl(SourceRange range, std::string_view name, TypeNode *type,
                                         Expr *initializer = nullptr,
                                         NodeFlags modifiers = NodeFlags::None) {
    assert(type && "type must not be null - use ErrorType or InferredType");

    auto *node = arena_.make<VarDeclNode>();
    node->kind = AstKind::VarDecl;
    node->range = range;
    node->name = strings_.intern(name);
    node->type = type;
    node->initializer = initializer;
    node->flags = modifiers;
    return node;
  }

  [[nodiscard]] MultiVarDeclNode *makeMultiVarDecl(SourceRange range,
                                                   const std::vector<std::string_view> &names,
                                                   Expr *initializer,
                                                   NodeFlags modifiers = NodeFlags::None) {
    auto *node = arena_.make<MultiVarDeclNode>();
    node->kind = AstKind::MultiVarDecl;
    node->range = range;

    std::vector<InternedString> internedNames;
    internedNames.reserve(names.size());
    for (const auto &n : names) {
      internedNames.push_back(strings_.intern(n));
    }
    node->names = arena_.makeArrayView(internedNames);
    node->initializer = initializer;
    node->flags = modifiers;
    return node;
  }

  [[nodiscard]] ParameterDeclNode *makeParameterDecl(SourceRange range, std::string_view name,
                                                     TypeNode *type, bool isVariadic = false) {
    assert(type && "type must not be null");

    auto *node = arena_.make<ParameterDeclNode>();
    node->kind = AstKind::ParameterDecl;
    node->range = range;
    node->name = strings_.intern(name);
    node->type = type;
    node->isVariadic = isVariadic;
    return node;
  }

  [[nodiscard]] FunctionDeclNode *
  makeFunctionDecl(SourceRange range, std::string_view name, TypeNode *returnType,
                   const std::vector<ParameterDeclNode *> &parameters, BlockStmtNode *body,
                   QualifiedIdentifierNode *qualifiedName = nullptr,
                   NodeFlags modifiers = NodeFlags::None, bool isMultiReturn = false,
                   bool hasVarArgs = false) {
    assert(returnType && body && "returnType and body must not be null");

    auto *node = arena_.make<FunctionDeclNode>();
    node->kind = AstKind::FunctionDecl;
    node->range = range;
    node->name = strings_.intern(name);
    node->returnType = returnType;
    node->parameters = arena_.makeArrayView(parameters);
    node->body = body;
    node->qualifiedName = qualifiedName;
    node->flags = modifiers;
    node->isMultiReturn = isMultiReturn;
    node->hasVarArgs = hasVarArgs;
    return node;
  }

  [[nodiscard]] FieldDeclNode *makeFieldDecl(SourceRange range, std::string_view name,
                                             TypeNode *type, Expr *initializer = nullptr,
                                             NodeFlags modifiers = NodeFlags::None,
                                             SourceRange nameRange = SourceRange::invalid()) {
    assert(type && "type must not be null");

    auto *node = arena_.make<FieldDeclNode>();
    node->kind = AstKind::FieldDecl;
    node->range = range;
    node->name = strings_.intern(name);
    node->type = type;
    node->initializer = initializer;
    node->flags = modifiers;
    node->nameRange = nameRange;
    return node;
  }

  [[nodiscard]] MethodDeclNode *
  makeMethodDecl(SourceRange range, std::string_view name, TypeNode *returnType,
                 const std::vector<ParameterDeclNode *> &parameters, BlockStmtNode *body,
                 NodeFlags modifiers = NodeFlags::None, bool isMultiReturn = false) {
    assert(returnType && body && "returnType and body must not be null");

    auto *node = arena_.make<MethodDeclNode>();
    node->kind = AstKind::MethodDecl;
    node->range = range;
    node->name = strings_.intern(name);
    node->returnType = returnType;
    node->parameters = arena_.makeArrayView(parameters);
    node->body = body;
    node->flags = modifiers;
    node->isMultiReturn = isMultiReturn;
    return node;
  }

  [[nodiscard]] ClassDeclNode *makeClassDecl(SourceRange range, std::string_view name,
                                             const std::vector<FieldDeclNode *> &fields,
                                             const std::vector<MethodDeclNode *> &methods,
                                             NodeFlags modifiers = NodeFlags::None) {
    auto *node = arena_.make<ClassDeclNode>();
    node->kind = AstKind::ClassDecl;
    node->range = range;
    node->name = strings_.intern(name);
    node->fields = arena_.makeArrayView(fields);
    node->methods = arena_.makeArrayView(methods);
    node->flags = modifiers;
    return node;
  }

  [[nodiscard]] CompilationUnitNode *
  makeCompilationUnit(SourceRange range, std::string_view filename,
                      const std::vector<Stmt *> &statements,
                      const std::vector<ImportStmtNode *> &imports = {}) {
    auto *node = arena_.make<CompilationUnitNode>();
    node->kind = AstKind::CompilationUnit;
    node->range = range;
    node->filename = strings_.intern(filename);
    node->statements = arena_.makeArrayView(statements);
    node->imports = arena_.makeArrayView(imports);
    return node;
  }

  // ========================================================================
  // Type Node Creation
  // ========================================================================

  [[nodiscard]] InferredTypeNode *makeInferredType(SourceRange range) {
    auto *node = arena_.make<InferredTypeNode>();
    node->kind = AstKind::InferredType;
    node->range = range;
    return node;
  }

  [[nodiscard]] PrimitiveTypeNode *makePrimitiveType(SourceRange range,
                                                     PrimitiveKind primitiveKind) {
    auto *node = arena_.make<PrimitiveTypeNode>();
    node->kind = AstKind::PrimitiveType;
    node->range = range;
    node->primitiveKind = primitiveKind;
    return node;
  }

  [[nodiscard]] AnyTypeNode *makeAnyType(SourceRange range) {
    auto *node = arena_.make<AnyTypeNode>();
    node->kind = AstKind::AnyType;
    node->range = range;
    return node;
  }

  [[nodiscard]] ListTypeNode *makeListType(SourceRange range, TypeNode *elementType = nullptr) {
    auto *node = arena_.make<ListTypeNode>();
    node->kind = AstKind::ListType;
    node->range = range;
    node->elementType = elementType;
    return node;
  }

  [[nodiscard]] MapTypeNode *makeMapType(SourceRange range, TypeNode *keyType = nullptr,
                                         TypeNode *valueType = nullptr) {
    auto *node = arena_.make<MapTypeNode>();
    node->kind = AstKind::MapType;
    node->range = range;
    node->keyType = keyType;
    node->valueType = valueType;
    return node;
  }

  [[nodiscard]] QualifiedTypeNode *makeQualifiedType(SourceRange range,
                                                     QualifiedIdentifierNode *name) {
    auto *node = arena_.make<QualifiedTypeNode>();
    node->kind = AstKind::QualifiedType;
    node->range = range;
    node->name = name;
    return node;
  }

  [[nodiscard]] MultiReturnTypeNode *makeMultiReturnType(SourceRange range) {
    auto *node = arena_.make<MultiReturnTypeNode>();
    node->kind = AstKind::MultiReturnType;
    node->range = range;
    return node;
  }

  // ========================================================================
  // Utility: Create Import Specifier
  // ========================================================================

  [[nodiscard]] ImportSpecifier makeImportSpecifier(std::string_view name, std::string_view alias,
                                                    bool isType, SourceRange range) {
    ImportSpecifier spec;
    spec.name = strings_.intern(name);
    spec.alias = strings_.intern(alias.empty() ? name : alias);
    spec.isType = isType;
    spec.range = range;
    return spec;
  }

private:
  Arena arena_;
  StringTable strings_;

public:
  // ========================================================================
  // StringTable Access (for external string lookups)
  // ========================================================================

  /**
   * @brief Get the string table for looking up interned strings
   */
  [[nodiscard]] StringTable &stringTable() noexcept { return strings_; }

  [[nodiscard]] const StringTable &stringTable() const noexcept { return strings_; }
};

} // namespace ast
} // namespace lang
