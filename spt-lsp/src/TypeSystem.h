/**
 * @file TypeSystem.h
 * @brief Type System Definitions for Semantic Analysis
 *
 * Defines the type system for the Lang language including:
 * - Primitive types (int, float, string, bool, etc.)
 * - Composite types (list, map, function)
 * - User-defined types (classes)
 * - Type inference and checking support
 *
 * Key Design Principles:
 * - Types are immutable once created
 * - Types are interned (unique instances)
 * - Supports gradual typing (any type)
 * - Error types for graceful degradation
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "AstNodes.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lang {
namespace types {

// Forward declarations
class Type;
class PrimitiveType;
class ListType;
class MapType;
class FunctionType;
class ClassType;
class TypeContext;

/**
 * @brief Type kind enumeration
 */
enum class TypeKind : uint8_t {
  // Special types
  Error,   ///< Unknown/error type
  Unknown, ///< Type to be inferred
  Void,    ///< No value
  Null,    ///< Null type
  Any,     ///< Dynamic type (gradual typing)

  // Primitive types
  Bool,
  Int,
  Float,
  Number, ///< Union of Int | Float
  String,

  // Compound types
  List,     ///< list<T>
  Map,      ///< map<K, V>
  Function, ///< (params) -> returnType
  Fiber,    ///< Coroutine handle

  // User-defined
  Class, ///< Class instance type

  // Multi-value (for multiple return values)
  Tuple, ///< (T1, T2, ...) - multiple values
};

/**
 * @brief Get type kind name for debugging
 */
[[nodiscard]] constexpr const char *typeKindToString(TypeKind kind) noexcept {
  switch (kind) {
  case TypeKind::Error:
    return "error";
  case TypeKind::Unknown:
    return "unknown";
  case TypeKind::Void:
    return "void";
  case TypeKind::Null:
    return "null";
  case TypeKind::Any:
    return "any";
  case TypeKind::Bool:
    return "bool";
  case TypeKind::Int:
    return "int";
  case TypeKind::Float:
    return "float";
  case TypeKind::Number:
    return "number";
  case TypeKind::String:
    return "string";
  case TypeKind::List:
    return "list";
  case TypeKind::Map:
    return "map";
  case TypeKind::Function:
    return "function";
  case TypeKind::Fiber:
    return "fiber";
  case TypeKind::Class:
    return "class";
  case TypeKind::Tuple:
    return "tuple";
  default:
    return "?";
  }
}

// ============================================================================
// Type Reference (Lightweight handle)
// ============================================================================

/**
 * @brief Reference to a type (non-owning)
 *
 * This is the primary way to pass types around.
 * All types are owned by TypeContext.
 */
class TypeRef {
public:
  TypeRef() noexcept : type_(nullptr) {}

  TypeRef(const Type *type) noexcept : type_(type) {}

  [[nodiscard]] bool isValid() const noexcept { return type_ != nullptr; }

  [[nodiscard]] bool isNull() const noexcept { return type_ == nullptr; }

  [[nodiscard]] const Type *get() const noexcept { return type_; }

  [[nodiscard]] const Type *operator->() const noexcept { return type_; }

  [[nodiscard]] const Type &operator*() const noexcept { return *type_; }

  explicit operator bool() const noexcept { return isValid(); }

  bool operator==(const TypeRef &other) const noexcept { return type_ == other.type_; }

  bool operator!=(const TypeRef &other) const noexcept { return type_ != other.type_; }

private:
  const Type *type_;
};

// ============================================================================
// Base Type Class
// ============================================================================

/**
 * @brief Base class for all types
 *
 * Types are immutable and interned - equal types share the same instance.
 */
class Type {
public:
  explicit Type(TypeKind kind) noexcept : kind_(kind) {}

  virtual ~Type() = default;

  // Non-copyable (managed by TypeContext)
  Type(const Type &) = delete;
  Type &operator=(const Type &) = delete;

  [[nodiscard]] TypeKind kind() const noexcept { return kind_; }

  /**
   * @brief Get human-readable type name
   */
  [[nodiscard]] virtual std::string toString() const = 0;

  /**
   * @brief Check type equality (structural)
   */
  [[nodiscard]] virtual bool equals(const Type &other) const { return kind_ == other.kind_; }

  /**
   * @brief Compute hash for type interning
   */
  [[nodiscard]] virtual size_t hash() const { return std::hash<int>{}(static_cast<int>(kind_)); }

  // Type predicates
  [[nodiscard]] bool isError() const noexcept { return kind_ == TypeKind::Error; }

  [[nodiscard]] bool isUnknown() const noexcept { return kind_ == TypeKind::Unknown; }

  [[nodiscard]] bool isVoid() const noexcept { return kind_ == TypeKind::Void; }

  [[nodiscard]] bool isNullType() const noexcept { return kind_ == TypeKind::Null; }

  [[nodiscard]] bool isAny() const noexcept { return kind_ == TypeKind::Any; }

  [[nodiscard]] bool isBool() const noexcept { return kind_ == TypeKind::Bool; }

  [[nodiscard]] bool isInt() const noexcept { return kind_ == TypeKind::Int; }

  [[nodiscard]] bool isFloat() const noexcept { return kind_ == TypeKind::Float; }

  [[nodiscard]] bool isNumber() const noexcept {
    return kind_ == TypeKind::Int || kind_ == TypeKind::Float || kind_ == TypeKind::Number;
  }

  [[nodiscard]] bool isString() const noexcept { return kind_ == TypeKind::String; }

  [[nodiscard]] bool isList() const noexcept { return kind_ == TypeKind::List; }

  [[nodiscard]] bool isMap() const noexcept { return kind_ == TypeKind::Map; }

  [[nodiscard]] bool isFunction() const noexcept { return kind_ == TypeKind::Function; }

  [[nodiscard]] bool isFiber() const noexcept { return kind_ == TypeKind::Fiber; }

  [[nodiscard]] bool isClass() const noexcept { return kind_ == TypeKind::Class; }

  [[nodiscard]] bool isTuple() const noexcept { return kind_ == TypeKind::Tuple; }

  [[nodiscard]] bool isPrimitive() const noexcept {
    return kind_ == TypeKind::Bool || kind_ == TypeKind::Int || kind_ == TypeKind::Float ||
           kind_ == TypeKind::String || kind_ == TypeKind::Number;
  }

protected:
  TypeKind kind_;
};

// ============================================================================
// Primitive Types
// ============================================================================

/**
 * @brief Primitive type (bool, int, float, string, etc.)
 */
class PrimitiveType : public Type {
public:
  explicit PrimitiveType(TypeKind kind) : Type(kind) {}

  [[nodiscard]] std::string toString() const override {
    return std::string(typeKindToString(kind_));
  }
};

// ============================================================================
// List Type
// ============================================================================

/**
 * @brief List type: list<T>
 */
class ListType : public Type {
public:
  explicit ListType(TypeRef elementType) : Type(TypeKind::List), elementType_(elementType) {}

  [[nodiscard]] TypeRef elementType() const noexcept { return elementType_; }

  [[nodiscard]] std::string toString() const override {
    if (elementType_) {
      return "list<" + elementType_->toString() + ">";
    }
    return "list";
  }

  [[nodiscard]] bool equals(const Type &other) const override {
    if (kind_ != other.kind())
      return false;
    const auto &o = static_cast<const ListType &>(other);
    if (!elementType_ && !o.elementType_)
      return true;
    if (!elementType_ || !o.elementType_)
      return false;
    return elementType_->equals(*o.elementType_);
  }

  [[nodiscard]] size_t hash() const override {
    size_t h = Type::hash();
    if (elementType_) {
      h ^= elementType_->hash() << 1;
    }
    return h;
  }

private:
  TypeRef elementType_;
};

// ============================================================================
// Map Type
// ============================================================================

/**
 * @brief Map type: map<K, V>
 */
class MapType : public Type {
public:
  MapType(TypeRef keyType, TypeRef valueType)
      : Type(TypeKind::Map), keyType_(keyType), valueType_(valueType) {}

  [[nodiscard]] TypeRef keyType() const noexcept { return keyType_; }

  [[nodiscard]] TypeRef valueType() const noexcept { return valueType_; }

  [[nodiscard]] std::string toString() const override {
    if (keyType_ && valueType_) {
      return "map<" + keyType_->toString() + ", " + valueType_->toString() + ">";
    }
    return "map";
  }

  [[nodiscard]] bool equals(const Type &other) const override {
    if (kind_ != other.kind())
      return false;
    const auto &o = static_cast<const MapType &>(other);

    bool keysEqual =
        (!keyType_ && !o.keyType_) || (keyType_ && o.keyType_ && keyType_->equals(*o.keyType_));
    bool valuesEqual = (!valueType_ && !o.valueType_) ||
                       (valueType_ && o.valueType_ && valueType_->equals(*o.valueType_));
    return keysEqual && valuesEqual;
  }

  [[nodiscard]] size_t hash() const override {
    size_t h = Type::hash();
    if (keyType_)
      h ^= keyType_->hash() << 1;
    if (valueType_)
      h ^= valueType_->hash() << 2;
    return h;
  }

private:
  TypeRef keyType_;
  TypeRef valueType_;
};

// ============================================================================
// Function Type
// ============================================================================

/**
 * @brief Function type: (params) -> returnType
 */
class FunctionType : public Type {
public:
  FunctionType(std::vector<TypeRef> paramTypes, TypeRef returnType, bool isVariadic = false)
      : Type(TypeKind::Function), paramTypes_(std::move(paramTypes)), returnType_(returnType),
        isVariadic_(isVariadic) {}

  [[nodiscard]] const std::vector<TypeRef> &paramTypes() const noexcept { return paramTypes_; }

  [[nodiscard]] TypeRef returnType() const noexcept { return returnType_; }

  [[nodiscard]] bool isVariadic() const noexcept { return isVariadic_; }

  [[nodiscard]] std::string toString() const override {
    std::string result = "(";
    for (size_t i = 0; i < paramTypes_.size(); ++i) {
      if (i > 0)
        result += ", ";
      result += paramTypes_[i] ? paramTypes_[i]->toString() : "?";
    }
    if (isVariadic_) {
      if (!paramTypes_.empty())
        result += ", ";
      result += "...";
    }
    result += ") -> ";
    result += returnType_ ? returnType_->toString() : "void";
    return result;
  }

  [[nodiscard]] bool equals(const Type &other) const override {
    if (kind_ != other.kind())
      return false;
    const auto &o = static_cast<const FunctionType &>(other);

    if (paramTypes_.size() != o.paramTypes_.size())
      return false;
    if (isVariadic_ != o.isVariadic_)
      return false;

    for (size_t i = 0; i < paramTypes_.size(); ++i) {
      if (!paramTypes_[i] || !o.paramTypes_[i])
        continue;
      if (!paramTypes_[i]->equals(*o.paramTypes_[i]))
        return false;
    }

    if (!returnType_ && !o.returnType_)
      return true;
    if (!returnType_ || !o.returnType_)
      return false;
    return returnType_->equals(*o.returnType_);
  }

  [[nodiscard]] size_t hash() const override {
    size_t h = Type::hash();
    for (const auto &p : paramTypes_) {
      if (p)
        h ^= p->hash();
    }
    if (returnType_)
      h ^= returnType_->hash() << 3;
    return h;
  }

private:
  std::vector<TypeRef> paramTypes_;
  TypeRef returnType_;
  bool isVariadic_;
};

// ============================================================================
// Tuple Type (for multiple return values)
// ============================================================================

/**
 * @brief Tuple type: (T1, T2, ...) for multiple return values
 */
class TupleType : public Type {
public:
  explicit TupleType(std::vector<TypeRef> elementTypes)
      : Type(TypeKind::Tuple), elementTypes_(std::move(elementTypes)) {}

  [[nodiscard]] const std::vector<TypeRef> &elementTypes() const noexcept { return elementTypes_; }

  [[nodiscard]] size_t size() const noexcept { return elementTypes_.size(); }

  [[nodiscard]] std::string toString() const override {
    std::string result = "(";
    for (size_t i = 0; i < elementTypes_.size(); ++i) {
      if (i > 0)
        result += ", ";
      result += elementTypes_[i] ? elementTypes_[i]->toString() : "?";
    }
    result += ")";
    return result;
  }

  [[nodiscard]] bool equals(const Type &other) const override {
    if (kind_ != other.kind())
      return false;
    const auto &o = static_cast<const TupleType &>(other);

    if (elementTypes_.size() != o.elementTypes_.size())
      return false;

    for (size_t i = 0; i < elementTypes_.size(); ++i) {
      if (!elementTypes_[i] || !o.elementTypes_[i])
        continue;
      if (!elementTypes_[i]->equals(*o.elementTypes_[i]))
        return false;
    }
    return true;
  }

private:
  std::vector<TypeRef> elementTypes_;
};

// ============================================================================
// Class Type
// ============================================================================

/**
 * @brief Field information in a class
 */
struct FieldInfo {
  std::string name;
  TypeRef type;
  bool isStatic = false;
  bool isConst = false;
};

/**
 * @brief Method information in a class
 */
struct MethodInfo {
  std::string name;
  TypeRef type; ///< FunctionType
  bool isStatic = false;
};

/**
 * @brief Class type
 */
class ClassType : public Type {
public:
  explicit ClassType(std::string name) : Type(TypeKind::Class), name_(std::move(name)) {}

  [[nodiscard]] const std::string &name() const noexcept { return name_; }

  [[nodiscard]] const std::vector<FieldInfo> &fields() const noexcept { return fields_; }

  [[nodiscard]] const std::vector<MethodInfo> &methods() const noexcept { return methods_; }

  void addField(FieldInfo field) { fields_.push_back(std::move(field)); }

  void addMethod(MethodInfo method) { methods_.push_back(std::move(method)); }

  /**
   * @brief Find a field by name
   */
  [[nodiscard]] const FieldInfo *findField(std::string_view name) const {
    for (const auto &f : fields_) {
      if (f.name == name)
        return &f;
    }
    return nullptr;
  }

  /**
   * @brief Find a method by name
   */
  [[nodiscard]] const MethodInfo *findMethod(std::string_view name) const {
    for (const auto &m : methods_) {
      if (m.name == name)
        return &m;
    }
    return nullptr;
  }

  [[nodiscard]] std::string toString() const override { return name_; }

  [[nodiscard]] bool equals(const Type &other) const override {
    if (kind_ != other.kind())
      return false;
    // Classes are nominally typed - same name = same type
    return name_ == static_cast<const ClassType &>(other).name_;
  }

  [[nodiscard]] size_t hash() const override { return std::hash<std::string>{}(name_); }

private:
  std::string name_;
  std::vector<FieldInfo> fields_;
  std::vector<MethodInfo> methods_;
};

// ============================================================================
// Type Context (Type Factory & Interning)
// ============================================================================

/**
 * @brief Type context - manages type creation and interning
 *
 * All types should be created through TypeContext to ensure
 * proper interning and lifetime management.
 *
 * Usage:
 *   TypeContext ctx;
 *
 *   // Get primitive types
 *   auto intType = ctx.intType();
 *   auto stringType = ctx.stringType();
 *
 *   // Create composite types
 *   auto listOfInt = ctx.makeListType(intType);
 *   auto funcType = ctx.makeFunctionType({intType}, stringType);
 */
class TypeContext {
public:
  TypeContext() { initPrimitiveTypes(); }

  // Primitive type accessors
  [[nodiscard]] TypeRef errorType() const noexcept { return errorType_; }

  [[nodiscard]] TypeRef unknownType() const noexcept { return unknownType_; }

  [[nodiscard]] TypeRef voidType() const noexcept { return voidType_; }

  [[nodiscard]] TypeRef nullType() const noexcept { return nullType_; }

  [[nodiscard]] TypeRef anyType() const noexcept { return anyType_; }

  [[nodiscard]] TypeRef boolType() const noexcept { return boolType_; }

  [[nodiscard]] TypeRef intType() const noexcept { return intType_; }

  [[nodiscard]] TypeRef floatType() const noexcept { return floatType_; }

  [[nodiscard]] TypeRef numberType() const noexcept { return numberType_; }

  [[nodiscard]] TypeRef stringType() const noexcept { return stringType_; }

  [[nodiscard]] TypeRef fiberType() const noexcept { return fiberType_; }

  // Composite type creation
  [[nodiscard]] TypeRef makeListType(TypeRef elementType) {
    auto type = std::make_unique<ListType>(elementType);
    return internType(std::move(type));
  }

  [[nodiscard]] TypeRef makeMapType(TypeRef keyType, TypeRef valueType) {
    auto type = std::make_unique<MapType>(keyType, valueType);
    return internType(std::move(type));
  }

  [[nodiscard]] TypeRef makeFunctionType(std::vector<TypeRef> paramTypes, TypeRef returnType,
                                         bool isVariadic = false) {
    auto type = std::make_unique<FunctionType>(std::move(paramTypes), returnType, isVariadic);
    return internType(std::move(type));
  }

  [[nodiscard]] TypeRef makeTupleType(std::vector<TypeRef> elementTypes) {
    auto type = std::make_unique<TupleType>(std::move(elementTypes));
    return internType(std::move(type));
  }

  /**
   * @brief Create or get a class type
   */
  [[nodiscard]] ClassType *getOrCreateClassType(std::string_view name) {
    std::string nameStr(name);
    auto it = classTypes_.find(nameStr);
    if (it != classTypes_.end()) {
      return it->second.get();
    }

    auto type = std::make_unique<ClassType>(nameStr);
    auto *ptr = type.get();
    classTypes_[nameStr] = std::move(type);
    return ptr;
  }

  /**
   * @brief Find a class type by name
   */
  [[nodiscard]] TypeRef findClassType(std::string_view name) const {
    auto it = classTypes_.find(std::string(name));
    return it != classTypes_.end() ? TypeRef(it->second.get()) : TypeRef();
  }

  // ========================================================================
  // Type Checking Utilities
  // ========================================================================

  /**
   * @brief Check if source type is assignable to target type
   */
  [[nodiscard]] bool isAssignableTo(TypeRef source, TypeRef target) const {
    if (!source || !target)
      return false;

    // Any type accepts anything
    if (target->isAny())
      return true;

    // Error types are always assignable (for error recovery)
    if (source->isError())
      return true;

    // Same type
    if (source->equals(*target))
      return true;

    // Null is assignable to any reference type
    if (source->isNullType()) {
      return target->isList() || target->isMap() || target->isClass() || target->isFunction();
    }

    // Number conversions
    if (target->kind() == TypeKind::Number) {
      return source->isInt() || source->isFloat();
    }
    if (target->isFloat() && source->isInt()) {
      return true; // int -> float is safe
    }

    return false;
  }

  /**
   * @brief Find the common type of two types
   */
  [[nodiscard]] TypeRef commonType(TypeRef a, TypeRef b) const {
    if (!a)
      return b;
    if (!b)
      return a;

    if (a->equals(*b))
      return a;

    // Error propagates
    if (a->isError())
      return a;
    if (b->isError())
      return b;

    // Any absorbs everything
    if (a->isAny())
      return anyType_;
    if (b->isAny())
      return anyType_;

    // Number promotion
    if ((a->isInt() && b->isFloat()) || (a->isFloat() && b->isInt())) {
      return floatType_;
    }
    if ((a->isInt() || a->isFloat()) && b->kind() == TypeKind::Number) {
      return numberType_;
    }
    if ((b->isInt() || b->isFloat()) && a->kind() == TypeKind::Number) {
      return numberType_;
    }

    // No common type found - return any
    return anyType_;
  }

  /**
   * @brief Get the result type of a binary operation
   */
  [[nodiscard]] TypeRef getBinaryOpResultType(TypeRef left, TypeRef right, ast::BinaryOp op) const {
    // Comparison operators always return bool
    switch (op) {
    case ast::BinaryOp::Eq:
    case ast::BinaryOp::Neq:
    case ast::BinaryOp::Lt:
    case ast::BinaryOp::Gt:
    case ast::BinaryOp::Lte:
    case ast::BinaryOp::Gte:
      return boolType_;

    case ast::BinaryOp::And:
    case ast::BinaryOp::Or:
      return boolType_;

    case ast::BinaryOp::Concat:
      return stringType_;

    case ast::BinaryOp::Add:
    case ast::BinaryOp::Sub:
    case ast::BinaryOp::Mul:
    case ast::BinaryOp::Div:
    case ast::BinaryOp::Mod:
      return commonType(left, right);

    case ast::BinaryOp::BitAnd:
    case ast::BinaryOp::BitOr:
    case ast::BinaryOp::BitXor:
    case ast::BinaryOp::LShift:
    case ast::BinaryOp::RShift:
      return intType_;

    default:
      return errorType_;
    }
  }

private:
  void initPrimitiveTypes() {
    errorType_ = internPrimitive(TypeKind::Error);
    unknownType_ = internPrimitive(TypeKind::Unknown);
    voidType_ = internPrimitive(TypeKind::Void);
    nullType_ = internPrimitive(TypeKind::Null);
    anyType_ = internPrimitive(TypeKind::Any);
    boolType_ = internPrimitive(TypeKind::Bool);
    intType_ = internPrimitive(TypeKind::Int);
    floatType_ = internPrimitive(TypeKind::Float);
    numberType_ = internPrimitive(TypeKind::Number);
    stringType_ = internPrimitive(TypeKind::String);
    fiberType_ = internPrimitive(TypeKind::Fiber);
  }

  TypeRef internPrimitive(TypeKind kind) {
    auto type = std::make_unique<PrimitiveType>(kind);
    auto *ptr = type.get();
    primitiveTypes_.push_back(std::move(type));
    return TypeRef(ptr);
  }

  TypeRef internType(std::unique_ptr<Type> type) {
    // Simple interning - could be optimized with hash lookup
    for (const auto &existing : compositeTypes_) {
      if (existing->equals(*type)) {
        return TypeRef(existing.get());
      }
    }

    auto *ptr = type.get();
    compositeTypes_.push_back(std::move(type));
    return TypeRef(ptr);
  }

  // Primitive types (cached)
  TypeRef errorType_;
  TypeRef unknownType_;
  TypeRef voidType_;
  TypeRef nullType_;
  TypeRef anyType_;
  TypeRef boolType_;
  TypeRef intType_;
  TypeRef floatType_;
  TypeRef numberType_;
  TypeRef stringType_;
  TypeRef fiberType_;

  // Type storage
  std::vector<std::unique_ptr<PrimitiveType>> primitiveTypes_;
  std::vector<std::unique_ptr<Type>> compositeTypes_;
  std::unordered_map<std::string, std::unique_ptr<ClassType>> classTypes_;
};

} // namespace types
} // namespace lang
