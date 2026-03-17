/**
 * @file Scope.h
 * @brief Scope Chain System for Semantic Analysis
 *
 * Provides the scope hierarchy for name resolution:
 * - Nested scope support (parent chain)
 * - Symbol definition and resolution
 * - Scope snapshots for LSP incremental updates
 *
 * Key Design Principles:
 * - Scopes are owned by SymbolTable
 * - Support efficient lookup with parent chain traversal
 * - Support "snapshot" mode for LSP queries
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "AstNodes.h"
#include "Symbol.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lang {
namespace semantic {

// Forward declarations
class SymbolTable;

// ============================================================================
// Scope Kind Enumeration
// ============================================================================

/**
 * @brief Enumeration of scope kinds
 */
enum class ScopeKind : uint8_t {
  Global,    ///< Global/module scope
  Function,  ///< Function body scope
  Block,     ///< Block statement scope (if, while, etc.)
  Class,     ///< Class member scope
  Loop,      ///< Loop scope (for break/continue)
  Namespace, ///< Namespace scope
};

/**
 * @brief Get scope kind name for debugging
 */
[[nodiscard]] constexpr const char *scopeKindToString(ScopeKind kind) noexcept {
  switch (kind) {
  case ScopeKind::Global:
    return "global";
  case ScopeKind::Function:
    return "function";
  case ScopeKind::Block:
    return "block";
  case ScopeKind::Class:
    return "class";
  case ScopeKind::Loop:
    return "loop";
  case ScopeKind::Namespace:
    return "namespace";
  default:
    return "unknown";
  }
}

// ============================================================================
// Scope Class
// ============================================================================

/**
 * @brief Represents a lexical scope for name resolution
 *
 * Scopes form a tree structure with parent pointers.
 * Each scope contains symbols defined in that scope.
 *
 * Usage:
 *   Scope* global = table.createGlobalScope();
 *   Scope* func = table.createScope(ScopeKind::Function, global);
 *
 *   func->define(varSymbol);
 *   Symbol* found = func->resolve("varName");
 */
class Scope {
public:
  Scope(ScopeKind kind, Scope *parent = nullptr, ast::AstNode *astNode = nullptr)
      : kind_(kind), parent_(parent), astNode_(astNode), depth_(parent ? parent->depth_ + 1 : 0) {
    if (parent) {
      parent->children_.push_back(this);
    }
  }

  // Non-copyable
  Scope(const Scope &) = delete;
  Scope &operator=(const Scope &) = delete;

  // ========================================================================
  // Accessors
  // ========================================================================

  [[nodiscard]] ScopeKind kind() const noexcept { return kind_; }

  [[nodiscard]] Scope *parent() const noexcept { return parent_; }

  [[nodiscard]] ast::AstNode *astNode() const noexcept { return astNode_; }

  [[nodiscard]] uint32_t depth() const noexcept { return depth_; }

  [[nodiscard]] const std::vector<Scope *> &children() const noexcept { return children_; }

  // ========================================================================
  // Symbol Definition
  // ========================================================================

  /**
   * @brief Define a symbol in this scope
   * @param symbol The symbol to define
   * @return true if successful, false if name already exists in this scope
   */
  bool define(Symbol *symbol) {
    if (!symbol)
      return false;

    const std::string &name = symbol->name();
    if (symbols_.find(name) != symbols_.end()) {
      return false; // Already defined
    }

    symbols_[name] = symbol;
    orderedSymbols_.push_back(symbol);
    symbol->setScope(this);
    return true;
  }

  /**
   * @brief Define or replace a symbol in this scope
   * @param symbol The symbol to define
   */
  void defineOrReplace(Symbol *symbol) {
    if (!symbol)
      return;

    const std::string &name = symbol->name();
    auto it = symbols_.find(name);
    if (it != symbols_.end()) {
      // Remove from ordered list
      auto ordIt = std::find(orderedSymbols_.begin(), orderedSymbols_.end(), it->second);
      if (ordIt != orderedSymbols_.end()) {
        orderedSymbols_.erase(ordIt);
      }
    }

    symbols_[name] = symbol;
    orderedSymbols_.push_back(symbol);
    symbol->setScope(this);
  }

  // ========================================================================
  // Symbol Resolution
  // ========================================================================

  /**
   * @brief Resolve a symbol by name in this scope only
   * @param name The name to look up
   * @return The symbol, or nullptr if not found
   */
  [[nodiscard]] Symbol *resolveLocal(std::string_view name) const {
    auto it = symbols_.find(std::string(name));
    return it != symbols_.end() ? it->second : nullptr;
  }

  /**
   * @brief Resolve a symbol by name, searching parent scopes
   * @param name The name to look up
   * @return The symbol, or nullptr if not found
   */
  [[nodiscard]] Symbol *resolve(std::string_view name) const {
    // Search this scope
    Symbol *symbol = resolveLocal(name);
    if (symbol)
      return symbol;

    // Search parent scopes
    if (parent_) {
      return parent_->resolve(name);
    }

    return nullptr;
  }

  /**
   * @brief Resolve a symbol and track the scope it was found in
   * @param name The name to look up
   * @param foundInScope Output parameter for the scope where symbol was found
   * @return The symbol, or nullptr if not found
   */
  [[nodiscard]] Symbol *resolveWithScope(std::string_view name, Scope *&foundInScope) const {
    Symbol *symbol = resolveLocal(name);
    if (symbol) {
      foundInScope = const_cast<Scope *>(this);
      return symbol;
    }

    if (parent_) {
      return parent_->resolveWithScope(name, foundInScope);
    }

    foundInScope = nullptr;
    return nullptr;
  }

  /**
   * @brief Check if a name is defined in this scope (not parents)
   */
  [[nodiscard]] bool isDefinedLocally(std::string_view name) const {
    return symbols_.find(std::string(name)) != symbols_.end();
  }

  /**
   * @brief Check if a name is defined in this scope or any parent
   */
  [[nodiscard]] bool isDefined(std::string_view name) const { return resolve(name) != nullptr; }

  // ========================================================================
  // Symbol Iteration
  // ========================================================================

  /**
   * @brief Get all symbols defined in this scope
   */
  [[nodiscard]] const std::vector<Symbol *> &symbols() const noexcept { return orderedSymbols_; }

  /**
   * @brief Get symbol count in this scope
   */
  [[nodiscard]] size_t symbolCount() const noexcept { return orderedSymbols_.size(); }

  /**
   * @brief Iterate over all symbols (including parent scopes)
   */
  void forEachSymbol(const std::function<void(Symbol *)> &callback) const {
    for (Symbol *sym : orderedSymbols_) {
      callback(sym);
    }
    if (parent_) {
      parent_->forEachSymbol(callback);
    }
  }

  /**
   * @brief Get all visible symbols (this scope and parents)
   */
  [[nodiscard]] std::vector<Symbol *> allVisibleSymbols() const {
    std::vector<Symbol *> result;
    std::unordered_map<std::string, bool> seen;

    const Scope *current = this;
    while (current) {
      for (Symbol *sym : current->orderedSymbols_) {
        if (seen.find(sym->name()) == seen.end()) {
          seen[sym->name()] = true;
          result.push_back(sym);
        }
      }
      current = current->parent_;
    }

    return result;
  }

  // ========================================================================
  // Scope Navigation
  // ========================================================================

  /**
   * @brief Find the nearest enclosing scope of a given kind
   */
  [[nodiscard]] Scope *findEnclosing(ScopeKind targetKind) const {
    Scope *current = parent_;
    while (current) {
      if (current->kind_ == targetKind) {
        return current;
      }
      current = current->parent_;
    }
    return nullptr;
  }

  /**
   * @brief Find the nearest enclosing function scope
   */
  [[nodiscard]] Scope *findEnclosingFunction() const {
    return const_cast<Scope *>(this)->findEnclosing(ScopeKind::Function);
  }

  /**
   * @brief Find the nearest enclosing loop scope
   */
  [[nodiscard]] Scope *findEnclosingLoop() const {
    Scope *current = const_cast<Scope *>(this);
    while (current) {
      if (current->kind_ == ScopeKind::Loop) {
        return current;
      }
      current = current->parent_;
    }
    return nullptr;
  }

  /**
   * @brief Find the nearest enclosing class scope
   */
  [[nodiscard]] Scope *findEnclosingClass() const {
    return const_cast<Scope *>(this)->findEnclosing(ScopeKind::Class);
  }

  /**
   * @brief Get the global (root) scope
   */
  [[nodiscard]] Scope *globalScope() const {
    const Scope *current = this;
    while (current->parent_) {
      current = current->parent_;
    }
    return const_cast<Scope *>(current);
  }

  /**
   * @brief Check if this scope is inside a loop
   */
  [[nodiscard]] bool isInsideLoop() const { return findEnclosingLoop() != nullptr; }

  /**
   * @brief Check if this scope is inside a function
   */
  [[nodiscard]] bool isInsideFunction() const {
    return findEnclosingFunction() != nullptr || kind_ == ScopeKind::Function;
  }

  // ========================================================================
  // Debug
  // ========================================================================

  /**
   * @brief Get string representation for debugging
   */
  [[nodiscard]] std::string toString() const {
    std::string result = scopeKindToString(kind_);
    result += " scope (depth=";
    result += std::to_string(depth_);
    result += ", symbols=";
    result += std::to_string(orderedSymbols_.size());
    result += ")";
    return result;
  }

  /**
   * @brief Dump scope hierarchy for debugging
   */
  void dump(int indent = 0) const {
    std::string prefix(indent * 2, ' ');
    printf("%s%s\n", prefix.c_str(), toString().c_str());
    for (Symbol *sym : orderedSymbols_) {
      printf("%s  - %s\n", prefix.c_str(), sym->toString().c_str());
    }
    for (Scope *child : children_) {
      child->dump(indent + 1);
    }
  }

private:
  ScopeKind kind_;
  Scope *parent_;
  ast::AstNode *astNode_;
  uint32_t depth_;

  std::unordered_map<std::string, Symbol *> symbols_; ///< Fast lookup by name
  std::vector<Symbol *> orderedSymbols_;              ///< Insertion order
  std::vector<Scope *> children_;                     ///< Child scopes
};

// ============================================================================
// Symbol Table
// ============================================================================

/**
 * @brief Manages all symbols and scopes in a compilation unit
 *
 * Owns all Symbol and Scope objects. Provides factory methods
 * for creating symbols and scopes.
 *
 * Usage:
 *   SymbolTable table;
 *
 *   Scope* global = table.globalScope();
 *   auto* func = table.createFunction("main", type, loc);
 *   global->define(func);
 *
 *   // Create function body scope
 *   Scope* body = table.createScope(ScopeKind::Function, global);
 */
class SymbolTable {
public:
  SymbolTable() {
    // Create global scope
    globalScope_ = createScope(ScopeKind::Global, nullptr);
  }

  // Non-copyable, movable
  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;
  SymbolTable(SymbolTable &&) = default;
  SymbolTable &operator=(SymbolTable &&) = default;

  // ========================================================================
  // Scope Management
  // ========================================================================

  /**
   * @brief Get the global scope
   */
  [[nodiscard]] Scope *globalScope() const noexcept { return globalScope_; }

  /**
   * @brief Create a new scope
   */
  [[nodiscard]] Scope *createScope(ScopeKind kind, Scope *parent, ast::AstNode *astNode = nullptr) {
    auto scope = std::make_unique<Scope>(kind, parent, astNode);
    Scope *ptr = scope.get();
    scopes_.push_back(std::move(scope));
    return ptr;
  }

  // ========================================================================
  // Symbol Creation
  // ========================================================================

  /**
   * @brief Create a variable symbol
   */
  [[nodiscard]] VariableSymbol *createVariable(std::string_view name, types::TypeRef type,
                                               ast::SourceLoc loc) {
    auto symbol = std::make_unique<VariableSymbol>(name, type, loc);
    VariableSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  /**
   * @brief Create a parameter symbol
   */
  [[nodiscard]] ParameterSymbol *createParameter(std::string_view name, types::TypeRef type,
                                                 ast::SourceLoc loc, uint32_t index) {
    auto symbol = std::make_unique<ParameterSymbol>(name, type, loc, index);
    ParameterSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  /**
   * @brief Create a function symbol
   */
  [[nodiscard]] FunctionSymbol *createFunction(std::string_view name, types::TypeRef type,
                                               ast::SourceLoc loc) {
    auto symbol = std::make_unique<FunctionSymbol>(name, type, loc);
    FunctionSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  /**
   * @brief Create a class symbol
   */
  [[nodiscard]] ClassSymbol *createClass(std::string_view name, ast::SourceLoc loc) {
    auto symbol = std::make_unique<ClassSymbol>(name, loc);
    ClassSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  /**
   * @brief Create a field symbol
   */
  [[nodiscard]] FieldSymbol *createField(std::string_view name, types::TypeRef type,
                                         ast::SourceLoc loc, ClassSymbol *owningClass) {
    auto symbol = std::make_unique<FieldSymbol>(name, type, loc, owningClass);
    FieldSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  /**
   * @brief Create a method symbol
   */
  [[nodiscard]] MethodSymbol *createMethod(std::string_view name, types::TypeRef type,
                                           ast::SourceLoc loc, ClassSymbol *owningClass) {
    auto symbol = std::make_unique<MethodSymbol>(name, type, loc, owningClass);
    MethodSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  /**
   * @brief Create an import symbol
   */
  [[nodiscard]] ImportSymbol *createImport(std::string_view name, std::string_view modulePath,
                                           ast::SourceLoc loc) {
    auto symbol = std::make_unique<ImportSymbol>(name, modulePath, loc);
    ImportSymbol *ptr = symbol.get();
    symbols_.push_back(std::move(symbol));
    return ptr;
  }

  // ========================================================================
  // Statistics
  // ========================================================================

  /**
   * @brief Get total number of symbols
   */
  [[nodiscard]] size_t symbolCount() const noexcept { return symbols_.size(); }

  /**
   * @brief Get total number of scopes
   */
  [[nodiscard]] size_t scopeCount() const noexcept { return scopes_.size(); }

  // ========================================================================
  // Iteration
  // ========================================================================

  /**
   * @brief Get all symbols
   */
  [[nodiscard]] const std::vector<std::unique_ptr<Symbol>> &allSymbols() const noexcept {
    return symbols_;
  }

  /**
   * @brief Get all scopes
   */
  [[nodiscard]] const std::vector<std::unique_ptr<Scope>> &allScopes() const noexcept {
    return scopes_;
  }

  // ========================================================================
  // Snapshot Support (for LSP)
  // ========================================================================

  /**
   * @brief Scope snapshot for LSP queries
   *
   * Captures the visible symbols at a given scope at a point in time.
   * This is a lightweight copy that can be used for queries after
   * the original scopes may have been modified.
   */
  struct ScopeSnapshot {
    ScopeKind kind;
    uint32_t depth;
    std::vector<Symbol *> symbols; ///< Visible symbols (references, not owned)

    [[nodiscard]] Symbol *resolve(std::string_view name) const {
      for (Symbol *sym : symbols) {
        if (sym->name() == name)
          return sym;
      }
      return nullptr;
    }
  };

  /**
   * @brief Create a snapshot of a scope (including inherited symbols)
   */
  [[nodiscard]] ScopeSnapshot createSnapshot(const Scope *scope) const {
    ScopeSnapshot snapshot;
    if (!scope)
      return snapshot;

    snapshot.kind = scope->kind();
    snapshot.depth = scope->depth();
    snapshot.symbols = scope->allVisibleSymbols();

    return snapshot;
  }

  /**
   * @brief Dump symbol table for debugging
   */
  void dump() const {
    printf("SymbolTable: %zu symbols, %zu scopes\n", symbols_.size(), scopes_.size());
    if (globalScope_) {
      globalScope_->dump();
    }
  }

private:
  std::vector<std::unique_ptr<Symbol>> symbols_;
  std::vector<std::unique_ptr<Scope>> scopes_;
  Scope *globalScope_ = nullptr;
};

} // namespace semantic
} // namespace lang
