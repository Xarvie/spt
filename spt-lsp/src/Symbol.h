/**
 * @file Symbol.h
 * @brief Symbol System for Semantic Analysis
 *
 * Defines the symbol hierarchy for the Lang language including:
 * - Variable symbols
 * - Function symbols
 * - Class symbols
 * - Parameter symbols
 *
 * Key Design Principles:
 * - Symbols are owned by SymbolTable (arena-allocated)
 * - Each symbol has an associated TypeRef and SourceLoc
 * - Supports LSP features (go-to-definition, find-references)
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "AstNodes.h"
#include "TypeSystem.h"

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace lang {
namespace semantic {

// Forward declarations
class Symbol;
class VariableSymbol;
class FunctionSymbol;
class ClassSymbol;
class ParameterSymbol;
class FieldSymbol;
class MethodSymbol;
class Scope;

// ============================================================================
// Symbol Kind Enumeration
// ============================================================================

/**
 * @brief Enumeration of all symbol kinds
 */
enum class SymbolKind : uint8_t {
  Variable,  ///< Local or global variable
  Parameter, ///< Function parameter
  Function,  ///< Function declaration
  Class,     ///< Class declaration
  Field,     ///< Class field
  Method,    ///< Class method
  Import,    ///< Imported symbol
  Namespace, ///< Namespace/module
};

/**
 * @brief Get symbol kind name for debugging
 */
[[nodiscard]] constexpr const char *symbolKindToString(SymbolKind kind) noexcept {
  switch (kind) {
  case SymbolKind::Variable:
    return "variable";
  case SymbolKind::Parameter:
    return "parameter";
  case SymbolKind::Function:
    return "function";
  case SymbolKind::Class:
    return "class";
  case SymbolKind::Field:
    return "field";
  case SymbolKind::Method:
    return "method";
  case SymbolKind::Import:
    return "import";
  case SymbolKind::Namespace:
    return "namespace";
  default:
    return "unknown";
  }
}

// ============================================================================
// Symbol Flags
// ============================================================================

/**
 * @brief Flags indicating symbol attributes
 */
enum class SymbolFlags : uint16_t {
  None = 0,
  Const = 1 << 0,     ///< Constant (immutable)
  Static = 1 << 1,    ///< Static member
  Export = 1 << 2,    ///< Exported from module
  Private = 1 << 3,   ///< Private member
  Global = 1 << 4,    ///< Global scope
  Builtin = 1 << 5,   ///< Built-in symbol
  Variadic = 1 << 6,  ///< Variadic parameter
  Inferred = 1 << 7,  ///< Type was inferred
  Undefined = 1 << 8, ///< Symbol not yet defined (forward reference)
};

[[nodiscard]] inline constexpr SymbolFlags operator|(SymbolFlags a, SymbolFlags b) noexcept {
  return static_cast<SymbolFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

[[nodiscard]] inline constexpr SymbolFlags operator&(SymbolFlags a, SymbolFlags b) noexcept {
  return static_cast<SymbolFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

// 修复: 移除 [[nodiscard]]，因为赋值操作符的返回值通常不需要检查
inline constexpr SymbolFlags &operator|=(SymbolFlags &a, SymbolFlags b) noexcept {
  a = a | b;
  return a;
}

[[nodiscard]] inline constexpr bool hasSymbolFlag(SymbolFlags flags, SymbolFlags flag) noexcept {
  return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

// ============================================================================
// Symbol Reference (Lightweight handle)
// ============================================================================

/**
 * @brief Reference to a symbol (non-owning)
 */
class SymbolRef {
public:
  SymbolRef() noexcept : symbol_(nullptr) {}

  SymbolRef(Symbol *symbol) noexcept : symbol_(symbol) {}

  [[nodiscard]] bool isValid() const noexcept { return symbol_ != nullptr; }

  [[nodiscard]] bool isNull() const noexcept { return symbol_ == nullptr; }

  [[nodiscard]] Symbol *get() const noexcept { return symbol_; }

  [[nodiscard]] Symbol *operator->() const noexcept { return symbol_; }

  [[nodiscard]] Symbol &operator*() const noexcept { return *symbol_; }

  explicit operator bool() const noexcept { return isValid(); }

  bool operator==(const SymbolRef &other) const noexcept { return symbol_ == other.symbol_; }

  bool operator!=(const SymbolRef &other) const noexcept { return symbol_ != other.symbol_; }

private:
  Symbol *symbol_;
};

// ============================================================================
// Base Symbol Class
// ============================================================================

/**
 * @brief Base class for all symbols
 *
 * Symbols represent named entities in the program:
 * - Variables, parameters, functions, classes
 * - Each has a name, type, and definition location
 */
class Symbol {
public:
  Symbol(SymbolKind kind, std::string_view name, types::TypeRef type, ast::SourceLoc definitionLoc)
      : kind_(kind), name_(name), type_(type), definitionLoc_(definitionLoc) {}

  virtual ~Symbol() = default;

  // Non-copyable (managed by SymbolTable)
  Symbol(const Symbol &) = delete;
  Symbol &operator=(const Symbol &) = delete;

  // Accessors
  [[nodiscard]] SymbolKind kind() const noexcept { return kind_; }

  [[nodiscard]] const std::string &name() const noexcept { return name_; }

  [[nodiscard]] types::TypeRef type() const noexcept { return type_; }

  [[nodiscard]] ast::SourceLoc definitionLoc() const noexcept { return definitionLoc_; }

  [[nodiscard]] SymbolFlags flags() const noexcept { return flags_; }

  [[nodiscard]] ast::AstNode *astNode() const noexcept { return astNode_; }

  [[nodiscard]] Scope *scope() const noexcept { return scope_; }

  // Mutators
  void setType(types::TypeRef type) noexcept { type_ = type; }

  void setFlags(SymbolFlags flags) noexcept { flags_ = flags; }

  void addFlag(SymbolFlags flag) noexcept { flags_ |= flag; }

  void setAstNode(ast::AstNode *node) noexcept { astNode_ = node; }

  void setScope(Scope *scope) noexcept { scope_ = scope; }

  // Flag predicates
  [[nodiscard]] bool isConst() const noexcept { return hasSymbolFlag(flags_, SymbolFlags::Const); }

  [[nodiscard]] bool isStatic() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Static);
  }

  [[nodiscard]] bool isExport() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Export);
  }

  [[nodiscard]] bool isPrivate() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Private);
  }

  [[nodiscard]] bool isGlobal() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Global);
  }

  [[nodiscard]] bool isBuiltin() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Builtin);
  }

  [[nodiscard]] bool isVariadic() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Variadic);
  }

  [[nodiscard]] bool isTypeInferred() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Inferred);
  }

  [[nodiscard]] bool isUndefined() const noexcept {
    return hasSymbolFlag(flags_, SymbolFlags::Undefined);
  }

  // Kind predicates
  [[nodiscard]] bool isVariable() const noexcept { return kind_ == SymbolKind::Variable; }

  [[nodiscard]] bool isParameter() const noexcept { return kind_ == SymbolKind::Parameter; }

  [[nodiscard]] bool isFunction() const noexcept { return kind_ == SymbolKind::Function; }

  [[nodiscard]] bool isClass() const noexcept { return kind_ == SymbolKind::Class; }

  [[nodiscard]] bool isField() const noexcept { return kind_ == SymbolKind::Field; }

  [[nodiscard]] bool isMethod() const noexcept { return kind_ == SymbolKind::Method; }

  /**
   * @brief Add a reference location (for find-all-references)
   */
  void addReference(ast::SourceLoc loc) { references_.push_back(loc); }

  /**
   * @brief Get all reference locations
   */
  [[nodiscard]] const std::vector<ast::SourceLoc> &references() const noexcept {
    return references_;
  }

  /**
   * @brief Get string representation for debugging
   */
  [[nodiscard]] virtual std::string toString() const {
    std::string result = symbolKindToString(kind_);
    result += " ";
    result += name_;
    if (type_) {
      result += ": ";
      result += type_->toString();
    }
    return result;
  }

protected:
  SymbolKind kind_;
  std::string name_;
  types::TypeRef type_;
  ast::SourceLoc definitionLoc_;
  SymbolFlags flags_ = SymbolFlags::None;
  ast::AstNode *astNode_ = nullptr;
  Scope *scope_ = nullptr;
  std::vector<ast::SourceLoc> references_;
};

// ============================================================================
// Variable Symbol
// ============================================================================

/**
 * @brief Symbol for local and global variables
 */
class VariableSymbol : public Symbol {
public:
  VariableSymbol(std::string_view name, types::TypeRef type, ast::SourceLoc loc)
      : Symbol(SymbolKind::Variable, name, type, loc) {}

  [[nodiscard]] std::string toString() const override {
    std::string result = isConst() ? "const " : "var ";
    result += name_;
    if (type_) {
      result += ": ";
      result += type_->toString();
    }
    return result;
  }
};

// ============================================================================
// Parameter Symbol
// ============================================================================

/**
 * @brief Symbol for function parameters
 */
class ParameterSymbol : public Symbol {
public:
  ParameterSymbol(std::string_view name, types::TypeRef type, ast::SourceLoc loc, uint32_t index)
      : Symbol(SymbolKind::Parameter, name, type, loc), index_(index) {}

  [[nodiscard]] uint32_t index() const noexcept { return index_; }

  [[nodiscard]] std::string toString() const override {
    std::string result = "param ";
    result += name_;
    if (type_) {
      result += ": ";
      result += type_->toString();
    }
    return result;
  }

private:
  uint32_t index_; ///< Parameter index (0-based)
};

// ============================================================================
// Function Symbol
// ============================================================================

/**
 * @brief Symbol for function declarations
 */
class FunctionSymbol : public Symbol {
public:
  FunctionSymbol(std::string_view name, types::TypeRef type, ast::SourceLoc loc)
      : Symbol(SymbolKind::Function, name, type, loc) {}

  [[nodiscard]] Scope *bodyScope() const noexcept { return bodyScope_; }

  void setBodyScope(Scope *scope) noexcept { bodyScope_ = scope; }

  [[nodiscard]] const std::vector<ParameterSymbol *> &parameters() const noexcept {
    return parameters_;
  }

  void addParameter(ParameterSymbol *param) { parameters_.push_back(param); }

  [[nodiscard]] types::TypeRef returnType() const noexcept { return returnType_; }

  void setReturnType(types::TypeRef type) noexcept { returnType_ = type; }

  [[nodiscard]] std::string toString() const override {
    std::string result = "function ";
    result += name_;
    result += "(";
    for (size_t i = 0; i < parameters_.size(); ++i) {
      if (i > 0)
        result += ", ";
      result += parameters_[i]->name();
      if (parameters_[i]->type()) {
        result += ": ";
        result += parameters_[i]->type()->toString();
      }
    }
    result += ")";
    if (returnType_) {
      result += " -> ";
      result += returnType_->toString();
    }
    return result;
  }

  [[nodiscard]] bool isMultiReturn() const noexcept { return isMultiReturn_; }

  void setMultiReturn(bool value) noexcept { isMultiReturn_ = value; }

private:
  Scope *bodyScope_ = nullptr;
  std::vector<ParameterSymbol *> parameters_;
  types::TypeRef returnType_;
  bool isMultiReturn_ = false;
};

// ============================================================================
// Field Symbol
// ============================================================================

/**
 * @brief Symbol for class fields
 */
class FieldSymbol : public Symbol {
public:
  FieldSymbol(std::string_view name, types::TypeRef type, ast::SourceLoc loc,
              ClassSymbol *owningClass)
      : Symbol(SymbolKind::Field, name, type, loc), owningClass_(owningClass) {}

  [[nodiscard]] ClassSymbol *owningClass() const noexcept { return owningClass_; }

  [[nodiscard]] std::string toString() const override {
    std::string result = isStatic() ? "static " : "";
    result += "field ";
    result += name_;
    if (type_) {
      result += ": ";
      result += type_->toString();
    }
    return result;
  }

private:
  ClassSymbol *owningClass_;
};

// ============================================================================
// Method Symbol
// ============================================================================

/**
 * @brief Symbol for class methods
 */
class MethodSymbol : public Symbol {
public:
  MethodSymbol(std::string_view name, types::TypeRef type, ast::SourceLoc loc,
               ClassSymbol *owningClass)
      : Symbol(SymbolKind::Method, name, type, loc), owningClass_(owningClass) {}

  [[nodiscard]] ClassSymbol *owningClass() const noexcept { return owningClass_; }

  [[nodiscard]] Scope *bodyScope() const noexcept { return bodyScope_; }

  void setBodyScope(Scope *scope) noexcept { bodyScope_ = scope; }

  [[nodiscard]] const std::vector<ParameterSymbol *> &parameters() const noexcept {
    return parameters_;
  }

  void addParameter(ParameterSymbol *param) { parameters_.push_back(param); }

  [[nodiscard]] types::TypeRef returnType() const noexcept { return returnType_; }

  void setReturnType(types::TypeRef type) noexcept { returnType_ = type; }

  [[nodiscard]] std::string toString() const override {
    std::string result = isStatic() ? "static " : "";
    result += "method ";
    result += name_;
    result += "(";
    for (size_t i = 0; i < parameters_.size(); ++i) {
      if (i > 0)
        result += ", ";
      result += parameters_[i]->name();
    }
    result += ")";
    if (returnType_) {
      result += " -> ";
      result += returnType_->toString();
    }
    return result;
  }

private:
  ClassSymbol *owningClass_;
  Scope *bodyScope_ = nullptr;
  std::vector<ParameterSymbol *> parameters_;
  types::TypeRef returnType_;
};

// ============================================================================
// Class Symbol
// ============================================================================

/**
 * @brief Symbol for class declarations
 */
class ClassSymbol : public Symbol {
public:
  ClassSymbol(std::string_view name, ast::SourceLoc loc)
      : Symbol(SymbolKind::Class, name, types::TypeRef(), loc) {}

  /**
   * @brief Get the class's member scope
   */
  [[nodiscard]] Scope *memberScope() const noexcept { return memberScope_; }

  void setMemberScope(Scope *scope) noexcept { memberScope_ = scope; }

  /**
   * @brief Get fields
   */
  [[nodiscard]] const std::vector<FieldSymbol *> &fields() const noexcept { return fields_; }

  void addField(FieldSymbol *field) { fields_.push_back(field); }

  /**
   * @brief Get methods
   */
  [[nodiscard]] const std::vector<MethodSymbol *> &methods() const noexcept { return methods_; }

  void addMethod(MethodSymbol *method) { methods_.push_back(method); }

  /**
   * @brief Find a field by name
   */
  [[nodiscard]] FieldSymbol *findField(std::string_view name) const {
    for (auto *field : fields_) {
      if (field->name() == name)
        return field;
    }
    return nullptr;
  }

  /**
   * @brief Find a method by name
   */
  [[nodiscard]] MethodSymbol *findMethod(std::string_view name) const {
    for (auto *method : methods_) {
      if (method->name() == name)
        return method;
    }
    return nullptr;
  }

  /**
   * @brief Get associated ClassType
   */
  [[nodiscard]] types::ClassType *classType() const noexcept { return classType_; }

  void setClassType(types::ClassType *type) noexcept {
    classType_ = type;
    type_ = types::TypeRef(type);
  }

  [[nodiscard]] std::string toString() const override {
    std::string result = "class ";
    result += name_;
    result += " { ";
    result += std::to_string(fields_.size()) + " fields, ";
    result += std::to_string(methods_.size()) + " methods }";
    return result;
  }

private:
  Scope *memberScope_ = nullptr;
  std::vector<FieldSymbol *> fields_;
  std::vector<MethodSymbol *> methods_;
  types::ClassType *classType_ = nullptr;
};

// ============================================================================
// Import Symbol
// ============================================================================

/**
 * @brief Symbol for imported names
 */
class ImportSymbol : public Symbol {
public:
  ImportSymbol(std::string_view name, std::string_view modulePath, ast::SourceLoc loc,
               Symbol *targetSymbol = nullptr)
      : Symbol(SymbolKind::Import, name, types::TypeRef(), loc), modulePath_(modulePath),
        targetSymbol_(targetSymbol) {}

  [[nodiscard]] const std::string &modulePath() const noexcept { return modulePath_; }

  [[nodiscard]] Symbol *targetSymbol() const noexcept { return targetSymbol_; }

  void setTargetSymbol(Symbol *symbol) noexcept {
    targetSymbol_ = symbol;
    if (symbol) {
      type_ = symbol->type();
    }
  }

  [[nodiscard]] std::string toString() const override {
    std::string result = "import ";
    result += name_;
    result += " from \"";
    result += modulePath_;
    result += "\"";
    return result;
  }

private:
  std::string modulePath_;
  Symbol *targetSymbol_; ///< The actual symbol being imported
};

// ============================================================================
// Type-Safe Cast Helpers
// ============================================================================

/**
 * @brief Safe cast with kind check (returns nullptr if wrong kind)
 */
template <typename T> [[nodiscard]] inline T *symbol_cast(Symbol *symbol) noexcept {
  if (!symbol)
    return nullptr;

  if constexpr (std::is_same_v<T, VariableSymbol>) {
    return symbol->kind() == SymbolKind::Variable ? static_cast<T *>(symbol) : nullptr;
  } else if constexpr (std::is_same_v<T, ParameterSymbol>) {
    return symbol->kind() == SymbolKind::Parameter ? static_cast<T *>(symbol) : nullptr;
  } else if constexpr (std::is_same_v<T, FunctionSymbol>) {
    return symbol->kind() == SymbolKind::Function ? static_cast<T *>(symbol) : nullptr;
  } else if constexpr (std::is_same_v<T, ClassSymbol>) {
    return symbol->kind() == SymbolKind::Class ? static_cast<T *>(symbol) : nullptr;
  } else if constexpr (std::is_same_v<T, FieldSymbol>) {
    return symbol->kind() == SymbolKind::Field ? static_cast<T *>(symbol) : nullptr;
  } else if constexpr (std::is_same_v<T, MethodSymbol>) {
    return symbol->kind() == SymbolKind::Method ? static_cast<T *>(symbol) : nullptr;
  } else if constexpr (std::is_same_v<T, ImportSymbol>) {
    return symbol->kind() == SymbolKind::Import ? static_cast<T *>(symbol) : nullptr;
  }
  return nullptr;
}

template <typename T> [[nodiscard]] inline const T *symbol_cast(const Symbol *symbol) noexcept {
  return symbol_cast<T>(const_cast<Symbol *>(symbol));
}

} // namespace semantic
} // namespace lang
