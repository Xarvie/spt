/**
 * @file AstNodes.h
 * @brief Tolerant AST Node Definitions for Lang Language
 *
 * Design Principles:
 * 1. Tolerant AST: All nodes are always valid, never nullptr
 * 2. Error nodes (ErrorExpr, ErrorStmt, ErrorDecl) for graceful degradation
 * 3. Arena allocation friendly (POD-like structures)
 * 4. No virtual functions, no RTTI - use enum + switch pattern
 * 5. CST is source of truth, AST is semantic view
 * 6. Support incomplete states for LSP completion
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace lang {
namespace ast {

// ============================================================================
// Forward Declarations
// ============================================================================

struct AstNode;
struct Expr;
struct Stmt;
struct Decl;
struct TypeNode;

// ============================================================================
// Source Location & Range
// ============================================================================

/**
 * @brief Represents a single position in source code
 */
struct SourceLoc {
  uint32_t line = 0;   ///< 1-based line number
  uint32_t column = 0; ///< 1-based column number (UTF-8 bytes)
  uint32_t offset = 0; ///< 0-based byte offset from file start

  [[nodiscard]] bool isValid() const noexcept { return line > 0; }

  [[nodiscard]] static SourceLoc invalid() noexcept { return {}; }

  bool operator==(const SourceLoc &other) const noexcept {
    return line == other.line && column == other.column && offset == other.offset;
  }

  bool operator!=(const SourceLoc &other) const noexcept { return !(*this == other); }

  bool operator<(const SourceLoc &other) const noexcept { return offset < other.offset; }
};

/**
 * @brief Represents a range in source code [begin, end)
 */
struct SourceRange {
  SourceLoc begin;
  SourceLoc end;

  [[nodiscard]] bool isValid() const noexcept { return begin.isValid(); }

  [[nodiscard]] static SourceRange invalid() noexcept { return {}; }

  [[nodiscard]] bool contains(SourceLoc loc) const noexcept {
    return loc.offset >= begin.offset && loc.offset < end.offset;
  }

  [[nodiscard]] bool overlaps(const SourceRange &other) const noexcept {
    return begin.offset < other.end.offset && other.begin.offset < end.offset;
  }

  [[nodiscard]] uint32_t length() const noexcept {
    return end.offset > begin.offset ? end.offset - begin.offset : 0;
  }
};

// ============================================================================
// AST Node Kind Enumeration
// ============================================================================

/**
 * @brief Enumeration of all AST node kinds
 *
 * Organized by category for efficient range checks:
 * - Expressions: ExprBegin to ExprEnd
 * - Statements: StmtBegin to StmtEnd
 * - Declarations: DeclBegin to DeclEnd
 * - Types: TypeBegin to TypeEnd
 */
enum class AstKind : uint16_t {
  // ========== Expressions ==========
  ExprBegin = 0,

  // Error & Placeholder
  ErrorExpr = ExprBegin, ///< Invalid/unparseable expression
  MissingExpr,           ///< Expected but missing expression

  // Literals
  NullLiteral,
  BoolLiteral,
  IntLiteral,
  FloatLiteral,
  StringLiteral,

  // Identifiers & Access
  Identifier,          ///< Simple identifier
  QualifiedIdentifier, ///< a.b.c style qualified name
  MemberAccessExpr,    ///< obj.member (may be incomplete: obj.)
  IndexExpr,           ///< expr[index]
  ColonLookupExpr,     ///< expr:member

  // Operators
  BinaryExpr, ///< Binary operations (+, -, *, etc.)
  UnaryExpr,  ///< Unary operations (!, -, #, ~)

  // Calls & Construction
  CallExpr, ///< function(args)
  NewExpr,  ///< new ClassName(args)

  // Compound Expressions
  ListExpr,     ///< [elem1, elem2, ...]
  MapExpr,      ///< {key: value, ...}
  MapEntryExpr, ///< Single map entry
  LambdaExpr,   ///< function(params) -> Type { body }

  // Special
  ParenExpr,   ///< (expr)
  VarArgsExpr, ///< ... (variadic args reference)

  ExprEnd,

  // ========== Statements ==========
  StmtBegin = 100,

  // Error & Placeholder
  ErrorStmt = StmtBegin, ///< Invalid/unparseable statement

  // Basic Statements
  EmptyStmt, ///< ; (semicolon only)
  ExprStmt,  ///< expression;
  BlockStmt, ///< { statements }

  // Assignment
  AssignStmt,       ///< lvalue = expr
  MultiAssignStmt,  ///< a, b = expr1, expr2
  UpdateAssignStmt, ///< lvalue op= expr

  // Control Flow
  IfStmt,    ///< if-else chain
  WhileStmt, ///< while loop
  ForStmt,   ///< for loop (C-style or foreach)
  BreakStmt,
  ContinueStmt,
  ReturnStmt,
  DeferStmt, ///< defer { block }

  // Module
  ImportStmt, ///< import statement

  // Declaration as Statement
  DeclStmt, ///< Wraps a declaration

  StmtEnd,

  // ========== Declarations ==========
  DeclBegin = 200,

  // Error & Placeholder
  ErrorDecl = DeclBegin, ///< Invalid/unparseable declaration

  // Variables
  VarDecl,      ///< Single variable declaration
  MultiVarDecl, ///< mutivar a, b, c = expr

  // Functions
  FunctionDecl,  ///< Function declaration/definition
  ParameterDecl, ///< Function parameter

  // Classes
  ClassDecl,  ///< Class declaration
  FieldDecl,  ///< Class field
  MethodDecl, ///< Class method

  // Module-level
  CompilationUnit, ///< Top-level file node

  DeclEnd,

  // ========== Types ==========
  TypeBegin = 300,

  // Error & Placeholder
  ErrorType = TypeBegin, ///< Invalid/unparseable type
  InferredType,          ///< auto - type to be inferred

  // Primitive Types
  PrimitiveType, ///< int, float, string, bool, void, null, fiber, function
  AnyType,       ///< any

  // Composite Types
  ListType,        ///< list<T>
  MapType,         ///< map<K, V>
  QualifiedType,   ///< User-defined type (a.b.c)
  MultiReturnType, ///< mutivar return type marker

  TypeEnd,
};

// ============================================================================
// Kind Category Helpers
// ============================================================================

[[nodiscard]] inline constexpr bool isExpr(AstKind kind) noexcept {
  return kind >= AstKind::ExprBegin && kind < AstKind::ExprEnd;
}

[[nodiscard]] inline constexpr bool isStmt(AstKind kind) noexcept {
  return kind >= AstKind::StmtBegin && kind < AstKind::StmtEnd;
}

[[nodiscard]] inline constexpr bool isDecl(AstKind kind) noexcept {
  return kind >= AstKind::DeclBegin && kind < AstKind::DeclEnd;
}

[[nodiscard]] inline constexpr bool isType(AstKind kind) noexcept {
  return kind >= AstKind::TypeBegin && kind < AstKind::TypeEnd;
}

[[nodiscard]] inline constexpr bool isError(AstKind kind) noexcept {
  return kind == AstKind::ErrorExpr || kind == AstKind::ErrorStmt || kind == AstKind::ErrorDecl ||
         kind == AstKind::ErrorType;
}

// ============================================================================
// Operator Enumerations
// ============================================================================

/**
 * @brief Binary operator kinds
 */
enum class BinaryOp : uint8_t {
  // Arithmetic
  Add, ///< +
  Sub, ///< -
  Mul, ///< *
  Div, ///< /
  IDiv, ///< ~/
  Mod, ///< %

  // Comparison
  Eq,  ///< ==
  Neq, ///< !=
  Lt,  ///< <
  Gt,  ///< >
  Lte, ///< <=
  Gte, ///< >=

  // Logical
  And, ///< &&
  Or,  ///< ||

  // Bitwise
  BitAnd, ///< &
  BitOr,  ///< |
  BitXor, ///< ^
  LShift, ///< <<
  RShift, ///< >>

  // String
  Concat, ///< ..

  Invalid,
};

/**
 * @brief Unary operator kinds
 */
enum class UnaryOp : uint8_t {
  Neg,    ///< - (negate)
  Not,    ///< ! (logical not)
  BitNot, ///< ~ (bitwise not)
  Len,    ///< # (length)

  Invalid,
};

/**
 * @brief Compound assignment operator kinds
 */
enum class UpdateOp : uint8_t {
  AddAssign,    ///< +=
  SubAssign,    ///< -=
  MulAssign,    ///< *=
  DivAssign,    ///< /=
  IDivAssign,   ///< ~/=
  ModAssign,    ///< %=
  ConcatAssign, ///< ..=

  Invalid,
};

/**
 * @brief Primitive type kinds
 */
enum class PrimitiveKind : uint8_t {
  Int,
  Float,
  Number,
  String,
  Bool,
  Void,
  Null,
  Coroutine,
  Function,

  Invalid,
};

// ============================================================================
// Node State Flags
// ============================================================================

/**
 * @brief Flags indicating node state for LSP/completion support
 */
enum class NodeFlags : uint16_t {
  None = 0,
  Incomplete = 1 << 0,   ///< Syntactically valid but unfinished (e.g., obj.)
  HasError = 1 << 1,     ///< Contains error child nodes
  FromRecovery = 1 << 2, ///< Created during error recovery
  Synthetic = 1 << 3,    ///< Synthesized node (not from source)

  // Declaration modifiers
  IsGlobal = 1 << 4,
  IsConst = 1 << 5,
  IsStatic = 1 << 6,
  IsExport = 1 << 7,
  IsPrivate = 1 << 8,
};

[[nodiscard]] inline constexpr NodeFlags operator|(NodeFlags a, NodeFlags b) noexcept {
  return static_cast<NodeFlags>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

[[nodiscard]] inline constexpr NodeFlags operator&(NodeFlags a, NodeFlags b) noexcept {
  return static_cast<NodeFlags>(static_cast<uint16_t>(a) & static_cast<uint16_t>(b));
}

inline constexpr NodeFlags &operator|=(NodeFlags &a, NodeFlags b) noexcept {
  a = a | b;
  return a;
}

[[nodiscard]] inline constexpr bool hasFlag(NodeFlags flags, NodeFlags flag) noexcept {
  return (static_cast<uint16_t>(flags) & static_cast<uint16_t>(flag)) != 0;
}

// ============================================================================
// Arena-Friendly Array View
// ============================================================================

/**
 * @brief Non-owning view of contiguous elements (for arena-allocated arrays)
 */
template <typename T> struct ArrayView {
  const T *data_ = nullptr;
  uint32_t size_ = 0;

  [[nodiscard]] const T *data() const noexcept { return data_; }

  [[nodiscard]] uint32_t size() const noexcept { return size_; }

  [[nodiscard]] bool empty() const noexcept { return size_ == 0; }

  [[nodiscard]] const T &operator[](uint32_t i) const noexcept {
    assert(i < size_);
    return data_[i];
  }

  [[nodiscard]] const T *begin() const noexcept { return data_; }

  [[nodiscard]] const T *end() const noexcept { return data_ + size_; }

  [[nodiscard]] const T &front() const noexcept {
    assert(!empty());
    return data_[0];
  }

  [[nodiscard]] const T &back() const noexcept {
    assert(!empty());
    return data_[size_ - 1];
  }
};

// ============================================================================
// String Interning Reference
// ============================================================================

/**
 * @brief Reference to an interned string in the string table
 */
struct InternedString {
  uint32_t id = 0; ///< ID in string table (0 = invalid/empty)

  [[nodiscard]] bool isValid() const noexcept { return id != 0; }

  [[nodiscard]] bool isEmpty() const noexcept { return id == 0; }

  bool operator==(const InternedString &other) const noexcept { return id == other.id; }

  bool operator!=(const InternedString &other) const noexcept { return id != other.id; }
};

// ============================================================================
// Base AST Node
// ============================================================================

/**
 * @brief Base structure for all AST nodes
 *
 * All AST nodes inherit from this base which provides:
 * - Kind identification (no RTTI needed)
 * - Source range tracking
 * - State flags
 */
struct AstNode {
  AstKind kind;
  NodeFlags flags = NodeFlags::None;
  SourceRange range;

  [[nodiscard]] bool isIncomplete() const noexcept { return hasFlag(flags, NodeFlags::Incomplete); }

  [[nodiscard]] bool hasError() const noexcept { return hasFlag(flags, NodeFlags::HasError); }

  [[nodiscard]] bool isFromRecovery() const noexcept {
    return hasFlag(flags, NodeFlags::FromRecovery);
  }

  [[nodiscard]] bool isSynthetic() const noexcept { return hasFlag(flags, NodeFlags::Synthetic); }
};

// ============================================================================
// EXPRESSION NODES
// ============================================================================

/**
 * @brief Base for all expression nodes
 */
struct Expr : AstNode {
  // Type information filled during semantic analysis
  // TypeRef resolvedType;  // Uncomment when type system is ready
};

// --------------------------------------------------------------------------
// Error & Placeholder Expressions
// --------------------------------------------------------------------------

/**
 * @brief Represents an invalid/unparseable expression
 *
 * Used when expression parsing fails. Semantic analysis treats this as UnknownType.
 * NEVER returns nullptr - always use ErrorExpr.
 */
struct ErrorExprNode : Expr {
  InternedString errorMessage; ///< Optional diagnostic message

  static constexpr AstKind Kind = AstKind::ErrorExpr;
};

/**
 * @brief Represents a missing expected expression
 *
 * Different from ErrorExpr: this is for cases where an expression was expected
 * but not provided (e.g., empty function argument).
 */
struct MissingExprNode : Expr {
  static constexpr AstKind Kind = AstKind::MissingExpr;
};

// --------------------------------------------------------------------------
// Literal Expressions
// --------------------------------------------------------------------------

struct NullLiteralNode : Expr {
  static constexpr AstKind Kind = AstKind::NullLiteral;
};

struct BoolLiteralNode : Expr {
  bool value = false;

  static constexpr AstKind Kind = AstKind::BoolLiteral;
};

struct IntLiteralNode : Expr {
  int64_t value = 0;
  bool isHex = false; ///< Was written in hexadecimal

  static constexpr AstKind Kind = AstKind::IntLiteral;
};

struct FloatLiteralNode : Expr {
  double value = 0.0;

  static constexpr AstKind Kind = AstKind::FloatLiteral;
};

struct StringLiteralNode : Expr {
  InternedString value;    ///< Processed string content (escapes resolved)
  InternedString rawValue; ///< Original source text (for hover display)

  static constexpr AstKind Kind = AstKind::StringLiteral;
};

// --------------------------------------------------------------------------
// Identifier & Access Expressions
// --------------------------------------------------------------------------

/**
 * @brief Simple identifier reference
 */
struct IdentifierNode : Expr {
  InternedString name;

  // Semantic analysis fills these:
  // SymbolRef resolvedSymbol;

  static constexpr AstKind Kind = AstKind::Identifier;
};

/**
 * @brief Qualified identifier (a.b.c)
 *
 * Used for type names and namespace-qualified references.
 * Different from MemberAccessExpr which is for runtime member access.
 */
struct QualifiedIdentifierNode : Expr {
  ArrayView<InternedString> parts; ///< ["a", "b", "c"] for a.b.c

  static constexpr AstKind Kind = AstKind::QualifiedIdentifier;
};

/**
 * @brief Member access expression (obj.member)
 *
 * CRITICAL FOR COMPLETION:
 * - When flags has Incomplete: represents "obj." (cursor after dot)
 * - member may be empty/error when incomplete
 * - LSP uses this to trigger member completion
 */
struct MemberAccessExprNode : Expr {
  Expr *base = nullptr;  ///< The object being accessed (NEVER null - use ErrorExpr)
  InternedString member; ///< Member name (may be empty if incomplete)

  // When incomplete (obj.), member is empty and flags has Incomplete
  // This is the key trigger for member completion

  static constexpr AstKind Kind = AstKind::MemberAccessExpr;
};

/**
 * @brief Index expression (expr[index])
 */
struct IndexExprNode : Expr {
  Expr *base = nullptr;  ///< Object being indexed (NEVER null)
  Expr *index = nullptr; ///< Index expression (NEVER null - use ErrorExpr)

  static constexpr AstKind Kind = AstKind::IndexExpr;
};

/**
 * @brief Colon lookup expression (expr:member)
 *
 * Used for method lookup (different from dot access in some semantics)
 */
struct ColonLookupExprNode : Expr {
  Expr *base = nullptr;
  InternedString member;

  static constexpr AstKind Kind = AstKind::ColonLookupExpr;
};

// --------------------------------------------------------------------------
// Operator Expressions
// --------------------------------------------------------------------------

/**
 * @brief Binary operation expression
 */
struct BinaryExprNode : Expr {
  BinaryOp op = BinaryOp::Invalid;
  Expr *left = nullptr;  ///< NEVER null
  Expr *right = nullptr; ///< NEVER null

  SourceLoc opLoc; ///< Location of the operator (for diagnostics)

  static constexpr AstKind Kind = AstKind::BinaryExpr;
};

/**
 * @brief Unary operation expression
 */
struct UnaryExprNode : Expr {
  UnaryOp op = UnaryOp::Invalid;
  Expr *operand = nullptr; ///< NEVER null
  bool isPrefix = true;    ///< True for prefix operators

  static constexpr AstKind Kind = AstKind::UnaryExpr;
};

// --------------------------------------------------------------------------
// Call & Construction Expressions
// --------------------------------------------------------------------------

/**
 * @brief Function call expression
 *
 * Handles: function(args), obj.method(args), obj:method(args)
 */
struct CallExprNode : Expr {
  Expr *callee = nullptr;      ///< The callable (NEVER null)
  ArrayView<Expr *> arguments; ///< Arguments (may be empty, entries NEVER null)

  SourceRange parenRange; ///< Range of parentheses (for completion)

  static constexpr AstKind Kind = AstKind::CallExpr;
};

/**
 * @brief New expression (object construction)
 *
 * new ClassName(args)
 */
struct NewExprNode : Expr {
  QualifiedIdentifierNode *typeName = nullptr; ///< Class to instantiate
  ArrayView<Expr *> arguments;                 ///< Constructor arguments

  static constexpr AstKind Kind = AstKind::NewExpr;
};

// --------------------------------------------------------------------------
// Compound Expressions
// --------------------------------------------------------------------------

/**
 * @brief List literal expression [elem1, elem2, ...]
 */
struct ListExprNode : Expr {
  ArrayView<Expr *> elements; ///< Elements (entries NEVER null)

  static constexpr AstKind Kind = AstKind::ListExpr;
};

/**
 * @brief Single map entry (key: value)
 */
struct MapEntryNode : Expr {
  Expr *key = nullptr;   ///< Key expression (NEVER null)
  Expr *value = nullptr; ///< Value expression (NEVER null)

  // For identifier keys (foo: bar), key is IdentifierNode converted to StringLiteral semantically
  bool isIdentifierKey = false;
  bool isBracketedKey = false; ///< [expr]: value syntax

  static constexpr AstKind Kind = AstKind::MapEntryExpr;
};

/**
 * @brief Map literal expression {key: value, ...}
 */
struct MapExprNode : Expr {
  ArrayView<MapEntryNode *> entries; ///< Entries (entries NEVER null)

  static constexpr AstKind Kind = AstKind::MapExpr;
};

/**
 * @brief Lambda/anonymous function expression
 *
 * function(params) -> RetType { body }
 */
struct LambdaExprNode : Expr {
  struct TypeNode *returnType = nullptr; ///< Return type (NEVER null - use ErrorType/InferredType)
  ArrayView<struct ParameterDeclNode *> params; ///< Parameters
  struct BlockStmtNode *body = nullptr;         ///< Function body (NEVER null)

  bool isMultiReturn = false; ///< Uses mutivar return

  static constexpr AstKind Kind = AstKind::LambdaExpr;
};

// --------------------------------------------------------------------------
// Special Expressions
// --------------------------------------------------------------------------

/**
 * @brief Parenthesized expression
 */
struct ParenExprNode : Expr {
  Expr *inner = nullptr; ///< Inner expression (NEVER null)

  static constexpr AstKind Kind = AstKind::ParenExpr;
};

/**
 * @brief Variadic arguments reference (...)
 */
struct VarArgsExprNode : Expr {
  static constexpr AstKind Kind = AstKind::VarArgsExpr;
};

// ============================================================================
// STATEMENT NODES
// ============================================================================

/**
 * @brief Base for all statement nodes
 */
struct Stmt : AstNode {};

// --------------------------------------------------------------------------
// Error & Basic Statements
// --------------------------------------------------------------------------

/**
 * @brief Invalid/unparseable statement
 */
struct ErrorStmtNode : Stmt {
  InternedString errorMessage;

  static constexpr AstKind Kind = AstKind::ErrorStmt;
};

/**
 * @brief Empty statement (;)
 */
struct EmptyStmtNode : Stmt {
  static constexpr AstKind Kind = AstKind::EmptyStmt;
};

/**
 * @brief Expression statement (expr;)
 */
struct ExprStmtNode : Stmt {
  Expr *expr = nullptr; ///< Expression (NEVER null - use ErrorExpr)

  static constexpr AstKind Kind = AstKind::ExprStmt;
};

/**
 * @brief Block statement { stmts }
 */
struct BlockStmtNode : Stmt {
  ArrayView<Stmt *> statements; ///< Contained statements (entries may include ErrorStmt)

  static constexpr AstKind Kind = AstKind::BlockStmt;
};

// --------------------------------------------------------------------------
// Assignment Statements
// --------------------------------------------------------------------------

/**
 * @brief L-value representation for assignments
 */
struct LValue {
  Expr *expr = nullptr; ///< The l-value expression (NEVER null)
                        // Resolved to: IdentifierNode, MemberAccessExprNode, or IndexExprNode
};

/**
 * @brief Simple assignment statement (lvalue = expr)
 */
struct AssignStmtNode : Stmt {
  LValue target;
  Expr *value = nullptr; ///< Value being assigned (NEVER null)

  static constexpr AstKind Kind = AstKind::AssignStmt;
};

/**
 * @brief Multiple assignment (a, b = expr1, expr2)
 */
struct MultiAssignStmtNode : Stmt {
  ArrayView<LValue> targets; ///< L-values on left side
  ArrayView<Expr *> values;  ///< Expressions on right side

  static constexpr AstKind Kind = AstKind::MultiAssignStmt;
};

/**
 * @brief Update assignment statement (lvalue op= expr)
 */
struct UpdateAssignStmtNode : Stmt {
  UpdateOp op = UpdateOp::Invalid;
  LValue target;
  Expr *value = nullptr; ///< Value (NEVER null)

  static constexpr AstKind Kind = AstKind::UpdateAssignStmt;
};

// --------------------------------------------------------------------------
// Control Flow Statements
// --------------------------------------------------------------------------

/**
 * @brief If-else statement
 *
 * Supports: if, if-else, if-elseif-else chains
 */
struct IfStmtNode : Stmt {
  /**
   * @brief Single condition-body pair (for if and else-if)
   */
  struct Branch {
    Expr *condition = nullptr;     ///< Condition (NEVER null)
    BlockStmtNode *body = nullptr; ///< Body block (NEVER null)
    SourceRange conditionRange;    ///< Range of condition (including parens)
  };

  ArrayView<Branch> branches;        ///< If and else-if branches
  BlockStmtNode *elseBody = nullptr; ///< Final else block (may be null = no else)

  static constexpr AstKind Kind = AstKind::IfStmt;
};

/**
 * @brief While loop statement
 */
struct WhileStmtNode : Stmt {
  Expr *condition = nullptr;     ///< Loop condition (NEVER null)
  BlockStmtNode *body = nullptr; ///< Loop body (NEVER null)

  static constexpr AstKind Kind = AstKind::WhileStmt;
};

/**
 * @brief For loop statement (Numeric, C-style, and foreach)
 */
struct ForStmtNode : Stmt {
  /**
   * @brief Determines for loop style
   */
  enum class Style : uint8_t {
    Numeric,  ///< for (i = start, end, step) { body } - Lua style numeric for
    CStyle,   ///< for (init; cond; update) { body } - DEPRECATED
    ForEach,  ///< for (Type v : collection) { body }
  };

  Style style = Style::Numeric;

  // Numeric for: for (var = start, end, step) body
  struct VarDeclNode *numericVar = nullptr; ///< Loop variable (may be typed or untyped)
  Expr *start = nullptr;                    ///< Start value
  Expr *end = nullptr;                      ///< End value
  Expr *step = nullptr;                     ///< Step value (optional, defaults to 1)

  // C-style: for (init; condition; update) body - DEPRECATED
  Stmt *init = nullptr;      ///< Initialization (may be null or ErrorStmt)
  Expr *condition = nullptr; ///< Condition (may be null for infinite loop)
  ArrayView<Stmt *> updates; ///< Update expressions/statements

  // ForEach: for (Type v1, Type v2 : collection) body
  ArrayView<struct VarDeclNode *> iterVars; ///< Iterator variables
  Expr *collection = nullptr;               ///< Collection expression (for foreach)

  BlockStmtNode *body = nullptr; ///< Loop body (NEVER null)

  static constexpr AstKind Kind = AstKind::ForStmt;
};

/**
 * @brief Break statement
 */
struct BreakStmtNode : Stmt {
  static constexpr AstKind Kind = AstKind::BreakStmt;
};

/**
 * @brief Continue statement
 */
struct ContinueStmtNode : Stmt {
  static constexpr AstKind Kind = AstKind::ContinueStmt;
};

/**
 * @brief Return statement
 *
 * Supports single and multiple return values
 */
struct ReturnStmtNode : Stmt {
  ArrayView<Expr *> values; ///< Return values (may be empty for void return)

  static constexpr AstKind Kind = AstKind::ReturnStmt;
};

/**
 * @brief Defer statement
 *
 * defer { block }
 */
struct DeferStmtNode : Stmt {
  BlockStmtNode *body = nullptr; ///< Deferred block (NEVER null)

  static constexpr AstKind Kind = AstKind::DeferStmt;
};

// --------------------------------------------------------------------------
// Import Statement
// --------------------------------------------------------------------------

/**
 * @brief Import specifier (single imported item)
 */
struct ImportSpecifier {
  InternedString name;  ///< Original name
  InternedString alias; ///< Alias (same as name if not aliased)
  bool isType = false;  ///< Has 'type' modifier
  SourceRange range;
};

/**
 * @brief Import statement
 *
 * Supports:
 * - import * as ns from "path"
 * - import { a, b as c } from "path"
 */
struct ImportStmtNode : Stmt {
  enum class Style : uint8_t {
    Namespace, ///< import * as ns from "..."
    Named,     ///< import { a, b } from "..."
  };

  Style style = Style::Named;
  InternedString modulePath;             ///< Module path string
  InternedString namespaceAlias;         ///< For namespace import
  ArrayView<ImportSpecifier> specifiers; ///< For named imports

  static constexpr AstKind Kind = AstKind::ImportStmt;
};

/**
 * @brief Declaration as statement wrapper
 */
struct DeclStmtNode : Stmt {
  struct Decl *decl = nullptr; ///< The declaration (NEVER null)

  static constexpr AstKind Kind = AstKind::DeclStmt;
};

// ============================================================================
// DECLARATION NODES
// ============================================================================

/**
 * @brief Base for all declaration nodes
 */
struct Decl : AstNode {
  InternedString name; ///< Declared name (may be empty for error)

  [[nodiscard]] bool isGlobal() const noexcept { return hasFlag(flags, NodeFlags::IsGlobal); }

  [[nodiscard]] bool isConst() const noexcept { return hasFlag(flags, NodeFlags::IsConst); }

  [[nodiscard]] bool isStatic() const noexcept { return hasFlag(flags, NodeFlags::IsStatic); }

  [[nodiscard]] bool isExport() const noexcept { return hasFlag(flags, NodeFlags::IsExport); }

  [[nodiscard]] bool isPrivate() const noexcept { return hasFlag(flags, NodeFlags::IsPrivate); }
};

// --------------------------------------------------------------------------
// Error Declaration
// --------------------------------------------------------------------------

/**
 * @brief Invalid/unparseable declaration
 */
struct ErrorDeclNode : Decl {
  InternedString errorMessage;

  static constexpr AstKind Kind = AstKind::ErrorDecl;
};

// --------------------------------------------------------------------------
// Variable Declarations
// --------------------------------------------------------------------------

/**
 * @brief Single variable declaration
 *
 * type name = initializer;
 * auto name = initializer;
 * const type name = initializer;
 */
struct VarDeclNode : Decl {
  struct TypeNode *type =
      nullptr;                 ///< Declared type (NEVER null - use ErrorType/InferredType for auto)
  Expr *initializer = nullptr; ///< Initial value (null if no initializer)

  // Note: isGlobal, isConst accessed via base class flags

  static constexpr AstKind Kind = AstKind::VarDecl;
};

/**
 * @brief Multi-variable declaration (mutivar)
 *
 * mutivar a, b, c = expr;
 */
struct MultiVarDeclNode : Decl {
  ArrayView<InternedString> names; ///< Variable names
  Expr *initializer = nullptr;     ///< Shared initializer

  // Type is always inferred for mutivar

  static constexpr AstKind Kind = AstKind::MultiVarDecl;
};

// --------------------------------------------------------------------------
// Function Declarations
// --------------------------------------------------------------------------

/**
 * @brief Function parameter declaration
 */
struct ParameterDeclNode : Decl {
  struct TypeNode *type = nullptr; ///< Parameter type (NEVER null)
  bool isVariadic = false;         ///< Is this the ... parameter

  static constexpr AstKind Kind = AstKind::ParameterDecl;
};

/**
 * @brief Function declaration/definition
 */
struct FunctionDeclNode : Decl {
  struct TypeNode *returnType = nullptr;     ///< Return type (NEVER null)
  ArrayView<ParameterDeclNode *> parameters; ///< Parameters
  BlockStmtNode *body = nullptr;             ///< Function body (NEVER null)

  QualifiedIdentifierNode *qualifiedName = nullptr; ///< For qualified names like Class.method

  bool isMultiReturn = false; ///< Returns multiple values (mutivar)
  bool hasVarArgs = false;    ///< Has ... parameter

  static constexpr AstKind Kind = AstKind::FunctionDecl;
};

// --------------------------------------------------------------------------
// Class Declarations
// --------------------------------------------------------------------------

/**
 * @brief Class field declaration
 */
struct FieldDeclNode : Decl {
  struct TypeNode *type = nullptr; ///< Field type (NEVER null)
  Expr *initializer = nullptr;     ///< Default value (may be null)

  // isStatic, isConst via base class flags

  static constexpr AstKind Kind = AstKind::FieldDecl;
};

/**
 * @brief Class method declaration
 */
struct MethodDeclNode : Decl {
  struct TypeNode *returnType = nullptr;     ///< Return type (NEVER null)
  ArrayView<ParameterDeclNode *> parameters; ///< Parameters
  BlockStmtNode *body = nullptr;             ///< Method body (NEVER null)

  bool isMultiReturn = false;

  // isStatic via base class flags

  static constexpr AstKind Kind = AstKind::MethodDecl;
};

/**
 * @brief Class declaration
 */
struct ClassDeclNode : Decl {
  ArrayView<FieldDeclNode *> fields;   ///< Class fields
  ArrayView<MethodDeclNode *> methods; ///< Class methods

  static constexpr AstKind Kind = AstKind::ClassDecl;
};

// --------------------------------------------------------------------------
// Compilation Unit
// --------------------------------------------------------------------------

/**
 * @brief Top-level compilation unit (file)
 */
struct CompilationUnitNode : Decl {
  ArrayView<Stmt *> statements;        ///< Top-level statements
  ArrayView<ImportStmtNode *> imports; ///< Import statements (for quick access)

  // Metadata
  InternedString filename;

  static constexpr AstKind Kind = AstKind::CompilationUnit;
};

// ============================================================================
// TYPE NODES
// ============================================================================

/**
 * @brief Base for all type annotation nodes
 */
struct TypeNode : AstNode {};

// --------------------------------------------------------------------------
// Error & Special Types
// --------------------------------------------------------------------------

/**
 * @brief Invalid/unparseable type
 */
struct ErrorTypeNode : TypeNode {
  InternedString errorMessage;

  static constexpr AstKind Kind = AstKind::ErrorType;
};

/**
 * @brief Inferred type (auto)
 */
struct InferredTypeNode : TypeNode {
  static constexpr AstKind Kind = AstKind::InferredType;
};

// --------------------------------------------------------------------------
// Concrete Types
// --------------------------------------------------------------------------

/**
 * @brief Primitive type (int, float, string, bool, void, null, fiber, function)
 */
struct PrimitiveTypeNode : TypeNode {
  PrimitiveKind primitiveKind = PrimitiveKind::Invalid;

  static constexpr AstKind Kind = AstKind::PrimitiveType;
};

/**
 * @brief Any type
 */
struct AnyTypeNode : TypeNode {
  static constexpr AstKind Kind = AstKind::AnyType;
};

/**
 * @brief List type (list or list<T>)
 */
struct ListTypeNode : TypeNode {
  TypeNode *elementType = nullptr; ///< Element type (null if unparameterized)

  static constexpr AstKind Kind = AstKind::ListType;
};

/**
 * @brief Map type (map or map<K, V>)
 */
struct MapTypeNode : TypeNode {
  TypeNode *keyType = nullptr;   ///< Key type (null if unparameterized)
  TypeNode *valueType = nullptr; ///< Value type (null if unparameterized)

  static constexpr AstKind Kind = AstKind::MapType;
};

/**
 * @brief Qualified type reference (user-defined type)
 */
struct QualifiedTypeNode : TypeNode {
  QualifiedIdentifierNode *name = nullptr; ///< Type name

  static constexpr AstKind Kind = AstKind::QualifiedType;
};

/**
 * @brief Multi-return type marker
 */
struct MultiReturnTypeNode : TypeNode {
  // mutivar functions have this as return type
  // Actual types determined from return statements during semantic analysis

  static constexpr AstKind Kind = AstKind::MultiReturnType;
};

// ============================================================================
// AST VISITOR PATTERN (Type-Safe, No RTTI)
// ============================================================================

/**
 * @brief Visitor interface using static dispatch
 *
 * Usage:
 *   class MyVisitor : public AstVisitor<MyVisitor, ReturnType> {
 *   public:
 *       ReturnType visitBinaryExpr(BinaryExprNode* node) { ... }
 *       // ... other visit methods
 *   };
 */
template <typename Derived, typename ReturnType = void> class AstVisitor {
public:
  ReturnType visit(AstNode *node) {
    if (!node) {
      if constexpr (std::is_void_v<ReturnType>) {
        return;
      } else {
        return ReturnType{};
      }
    }

    switch (node->kind) {
    // Expressions
    case AstKind::ErrorExpr:
      return derived().visitErrorExpr(static_cast<ErrorExprNode *>(node));
    case AstKind::MissingExpr:
      return derived().visitMissingExpr(static_cast<MissingExprNode *>(node));
    case AstKind::NullLiteral:
      return derived().visitNullLiteral(static_cast<NullLiteralNode *>(node));
    case AstKind::BoolLiteral:
      return derived().visitBoolLiteral(static_cast<BoolLiteralNode *>(node));
    case AstKind::IntLiteral:
      return derived().visitIntLiteral(static_cast<IntLiteralNode *>(node));
    case AstKind::FloatLiteral:
      return derived().visitFloatLiteral(static_cast<FloatLiteralNode *>(node));
    case AstKind::StringLiteral:
      return derived().visitStringLiteral(static_cast<StringLiteralNode *>(node));
    case AstKind::Identifier:
      return derived().visitIdentifier(static_cast<IdentifierNode *>(node));
    case AstKind::QualifiedIdentifier:
      return derived().visitQualifiedIdentifier(static_cast<QualifiedIdentifierNode *>(node));
    case AstKind::MemberAccessExpr:
      return derived().visitMemberAccessExpr(static_cast<MemberAccessExprNode *>(node));
    case AstKind::IndexExpr:
      return derived().visitIndexExpr(static_cast<IndexExprNode *>(node));
    case AstKind::ColonLookupExpr:
      return derived().visitColonLookupExpr(static_cast<ColonLookupExprNode *>(node));
    case AstKind::BinaryExpr:
      return derived().visitBinaryExpr(static_cast<BinaryExprNode *>(node));
    case AstKind::UnaryExpr:
      return derived().visitUnaryExpr(static_cast<UnaryExprNode *>(node));
    case AstKind::CallExpr:
      return derived().visitCallExpr(static_cast<CallExprNode *>(node));
    case AstKind::NewExpr:
      return derived().visitNewExpr(static_cast<NewExprNode *>(node));
    case AstKind::ListExpr:
      return derived().visitListExpr(static_cast<ListExprNode *>(node));
    case AstKind::MapExpr:
      return derived().visitMapExpr(static_cast<MapExprNode *>(node));
    case AstKind::MapEntryExpr:
      return derived().visitMapEntry(static_cast<MapEntryNode *>(node));
    case AstKind::LambdaExpr:
      return derived().visitLambdaExpr(static_cast<LambdaExprNode *>(node));
    case AstKind::ParenExpr:
      return derived().visitParenExpr(static_cast<ParenExprNode *>(node));
    case AstKind::VarArgsExpr:
      return derived().visitVarArgsExpr(static_cast<VarArgsExprNode *>(node));

    // Statements
    case AstKind::ErrorStmt:
      return derived().visitErrorStmt(static_cast<ErrorStmtNode *>(node));
    case AstKind::EmptyStmt:
      return derived().visitEmptyStmt(static_cast<EmptyStmtNode *>(node));
    case AstKind::ExprStmt:
      return derived().visitExprStmt(static_cast<ExprStmtNode *>(node));
    case AstKind::BlockStmt:
      return derived().visitBlockStmt(static_cast<BlockStmtNode *>(node));
    case AstKind::AssignStmt:
      return derived().visitAssignStmt(static_cast<AssignStmtNode *>(node));
    case AstKind::MultiAssignStmt:
      return derived().visitMultiAssignStmt(static_cast<MultiAssignStmtNode *>(node));
    case AstKind::UpdateAssignStmt:
      return derived().visitUpdateAssignStmt(static_cast<UpdateAssignStmtNode *>(node));
    case AstKind::IfStmt:
      return derived().visitIfStmt(static_cast<IfStmtNode *>(node));
    case AstKind::WhileStmt:
      return derived().visitWhileStmt(static_cast<WhileStmtNode *>(node));
    case AstKind::ForStmt:
      return derived().visitForStmt(static_cast<ForStmtNode *>(node));
    case AstKind::BreakStmt:
      return derived().visitBreakStmt(static_cast<BreakStmtNode *>(node));
    case AstKind::ContinueStmt:
      return derived().visitContinueStmt(static_cast<ContinueStmtNode *>(node));
    case AstKind::ReturnStmt:
      return derived().visitReturnStmt(static_cast<ReturnStmtNode *>(node));
    case AstKind::DeferStmt:
      return derived().visitDeferStmt(static_cast<DeferStmtNode *>(node));
    case AstKind::ImportStmt:
      return derived().visitImportStmt(static_cast<ImportStmtNode *>(node));
    case AstKind::DeclStmt:
      return derived().visitDeclStmt(static_cast<DeclStmtNode *>(node));

    // Declarations
    case AstKind::ErrorDecl:
      return derived().visitErrorDecl(static_cast<ErrorDeclNode *>(node));
    case AstKind::VarDecl:
      return derived().visitVarDecl(static_cast<VarDeclNode *>(node));
    case AstKind::MultiVarDecl:
      return derived().visitMultiVarDecl(static_cast<MultiVarDeclNode *>(node));
    case AstKind::ParameterDecl:
      return derived().visitParameterDecl(static_cast<ParameterDeclNode *>(node));
    case AstKind::FunctionDecl:
      return derived().visitFunctionDecl(static_cast<FunctionDeclNode *>(node));
    case AstKind::FieldDecl:
      return derived().visitFieldDecl(static_cast<FieldDeclNode *>(node));
    case AstKind::MethodDecl:
      return derived().visitMethodDecl(static_cast<MethodDeclNode *>(node));
    case AstKind::ClassDecl:
      return derived().visitClassDecl(static_cast<ClassDeclNode *>(node));
    case AstKind::CompilationUnit:
      return derived().visitCompilationUnit(static_cast<CompilationUnitNode *>(node));

    // Types
    case AstKind::ErrorType:
      return derived().visitErrorType(static_cast<ErrorTypeNode *>(node));
    case AstKind::InferredType:
      return derived().visitInferredType(static_cast<InferredTypeNode *>(node));
    case AstKind::PrimitiveType:
      return derived().visitPrimitiveType(static_cast<PrimitiveTypeNode *>(node));
    case AstKind::AnyType:
      return derived().visitAnyType(static_cast<AnyTypeNode *>(node));
    case AstKind::ListType:
      return derived().visitListType(static_cast<ListTypeNode *>(node));
    case AstKind::MapType:
      return derived().visitMapType(static_cast<MapTypeNode *>(node));
    case AstKind::QualifiedType:
      return derived().visitQualifiedType(static_cast<QualifiedTypeNode *>(node));
    case AstKind::MultiReturnType:
      return derived().visitMultiReturnType(static_cast<MultiReturnTypeNode *>(node));

    default:
      return derived().visitUnknown(node);
    }
  }

protected:
// Default implementations - derived class can override
// Uses if constexpr to properly handle void return type
#define DEFAULT_VISIT(NodeType)                                                                    \
  ReturnType visit##NodeType(NodeType##Node *) {                                                   \
    if constexpr (std::is_void_v<ReturnType>) {                                                    \
      return;                                                                                      \
    } else {                                                                                       \
      return ReturnType{};                                                                         \
    }                                                                                              \
  }

  // Expressions
  DEFAULT_VISIT(ErrorExpr)
  DEFAULT_VISIT(MissingExpr)
  DEFAULT_VISIT(NullLiteral)
  DEFAULT_VISIT(BoolLiteral)
  DEFAULT_VISIT(IntLiteral)
  DEFAULT_VISIT(FloatLiteral)
  DEFAULT_VISIT(StringLiteral)
  DEFAULT_VISIT(Identifier)
  DEFAULT_VISIT(QualifiedIdentifier)
  DEFAULT_VISIT(MemberAccessExpr)
  DEFAULT_VISIT(IndexExpr)
  DEFAULT_VISIT(ColonLookupExpr)
  DEFAULT_VISIT(BinaryExpr)
  DEFAULT_VISIT(UnaryExpr)
  DEFAULT_VISIT(CallExpr)
  DEFAULT_VISIT(NewExpr)
  DEFAULT_VISIT(ListExpr)
  DEFAULT_VISIT(MapEntry)
  DEFAULT_VISIT(MapExpr)
  DEFAULT_VISIT(LambdaExpr)
  DEFAULT_VISIT(ParenExpr)
  DEFAULT_VISIT(VarArgsExpr)

  // Statements
  DEFAULT_VISIT(ErrorStmt)
  DEFAULT_VISIT(EmptyStmt)
  DEFAULT_VISIT(ExprStmt)
  DEFAULT_VISIT(BlockStmt)
  DEFAULT_VISIT(AssignStmt)
  DEFAULT_VISIT(MultiAssignStmt)
  DEFAULT_VISIT(UpdateAssignStmt)
  DEFAULT_VISIT(IfStmt)
  DEFAULT_VISIT(WhileStmt)
  DEFAULT_VISIT(ForStmt)
  DEFAULT_VISIT(BreakStmt)
  DEFAULT_VISIT(ContinueStmt)
  DEFAULT_VISIT(ReturnStmt)
  DEFAULT_VISIT(DeferStmt)
  DEFAULT_VISIT(ImportStmt)
  DEFAULT_VISIT(DeclStmt)

  // Declarations
  DEFAULT_VISIT(ErrorDecl)
  DEFAULT_VISIT(VarDecl)
  DEFAULT_VISIT(MultiVarDecl)
  DEFAULT_VISIT(ParameterDecl)
  DEFAULT_VISIT(FunctionDecl)
  DEFAULT_VISIT(FieldDecl)
  DEFAULT_VISIT(MethodDecl)
  DEFAULT_VISIT(ClassDecl)
  DEFAULT_VISIT(CompilationUnit)

  // Types
  DEFAULT_VISIT(ErrorType)
  DEFAULT_VISIT(InferredType)
  DEFAULT_VISIT(PrimitiveType)
  DEFAULT_VISIT(AnyType)
  DEFAULT_VISIT(ListType)
  DEFAULT_VISIT(MapType)
  DEFAULT_VISIT(QualifiedType)
  DEFAULT_VISIT(MultiReturnType)

#undef DEFAULT_VISIT

  ReturnType visitUnknown(AstNode *) {
    if constexpr (std::is_void_v<ReturnType>) {
      return;
    } else {
      return ReturnType{};
    }
  }

private:
  Derived &derived() { return static_cast<Derived &>(*this); }
};

// ============================================================================
// UTILITY: Node Kind to String
// ============================================================================

/**
 * @brief Get string representation of AstKind
 */
[[nodiscard]] inline constexpr const char *astKindToString(AstKind kind) noexcept {
  switch (kind) {
  // Expressions
  case AstKind::ErrorExpr:
    return "ErrorExpr";
  case AstKind::MissingExpr:
    return "MissingExpr";
  case AstKind::NullLiteral:
    return "NullLiteral";
  case AstKind::BoolLiteral:
    return "BoolLiteral";
  case AstKind::IntLiteral:
    return "IntLiteral";
  case AstKind::FloatLiteral:
    return "FloatLiteral";
  case AstKind::StringLiteral:
    return "StringLiteral";
  case AstKind::Identifier:
    return "Identifier";
  case AstKind::QualifiedIdentifier:
    return "QualifiedIdentifier";
  case AstKind::MemberAccessExpr:
    return "MemberAccessExpr";
  case AstKind::IndexExpr:
    return "IndexExpr";
  case AstKind::ColonLookupExpr:
    return "ColonLookupExpr";
  case AstKind::BinaryExpr:
    return "BinaryExpr";
  case AstKind::UnaryExpr:
    return "UnaryExpr";
  case AstKind::CallExpr:
    return "CallExpr";
  case AstKind::NewExpr:
    return "NewExpr";
  case AstKind::ListExpr:
    return "ListExpr";
  case AstKind::MapExpr:
    return "MapExpr";
  case AstKind::MapEntryExpr:
    return "MapEntry";
  case AstKind::LambdaExpr:
    return "LambdaExpr";
  case AstKind::ParenExpr:
    return "ParenExpr";
  case AstKind::VarArgsExpr:
    return "VarArgsExpr";

  // Statements
  case AstKind::ErrorStmt:
    return "ErrorStmt";
  case AstKind::EmptyStmt:
    return "EmptyStmt";
  case AstKind::ExprStmt:
    return "ExprStmt";
  case AstKind::BlockStmt:
    return "BlockStmt";
  case AstKind::AssignStmt:
    return "AssignStmt";
  case AstKind::MultiAssignStmt:
    return "MultiAssignStmt";
  case AstKind::UpdateAssignStmt:
    return "UpdateAssignStmt";
  case AstKind::IfStmt:
    return "IfStmt";
  case AstKind::WhileStmt:
    return "WhileStmt";
  case AstKind::ForStmt:
    return "ForStmt";
  case AstKind::BreakStmt:
    return "BreakStmt";
  case AstKind::ContinueStmt:
    return "ContinueStmt";
  case AstKind::ReturnStmt:
    return "ReturnStmt";
  case AstKind::DeferStmt:
    return "DeferStmt";
  case AstKind::ImportStmt:
    return "ImportStmt";
  case AstKind::DeclStmt:
    return "DeclStmt";

  // Declarations
  case AstKind::ErrorDecl:
    return "ErrorDecl";
  case AstKind::VarDecl:
    return "VarDecl";
  case AstKind::MultiVarDecl:
    return "MultiVarDecl";
  case AstKind::ParameterDecl:
    return "ParameterDecl";
  case AstKind::FunctionDecl:
    return "FunctionDecl";
  case AstKind::FieldDecl:
    return "FieldDecl";
  case AstKind::MethodDecl:
    return "MethodDecl";
  case AstKind::ClassDecl:
    return "ClassDecl";
  case AstKind::CompilationUnit:
    return "CompilationUnit";

  // Types
  case AstKind::ErrorType:
    return "ErrorType";
  case AstKind::InferredType:
    return "InferredType";
  case AstKind::PrimitiveType:
    return "PrimitiveType";
  case AstKind::AnyType:
    return "AnyType";
  case AstKind::ListType:
    return "ListType";
  case AstKind::MapType:
    return "MapType";
  case AstKind::QualifiedType:
    return "QualifiedType";
  case AstKind::MultiReturnType:
    return "MultiReturnType";

  default:
    return "Unknown";
  }
}

/**
 * @brief Get string representation of BinaryOp
 */
[[nodiscard]] inline constexpr const char *binaryOpToString(BinaryOp op) noexcept {
  switch (op) {
  case BinaryOp::Add:
    return "+";
  case BinaryOp::Sub:
    return "-";
  case BinaryOp::Mul:
    return "*";
  case BinaryOp::Div:
    return "/";
  case BinaryOp::IDiv:
    return "~/";
  case BinaryOp::Mod:
    return "%";
  case BinaryOp::Eq:
    return "==";
  case BinaryOp::Neq:
    return "!=";
  case BinaryOp::Lt:
    return "<";
  case BinaryOp::Gt:
    return ">";
  case BinaryOp::Lte:
    return "<=";
  case BinaryOp::Gte:
    return ">=";
  case BinaryOp::And:
    return "&&";
  case BinaryOp::Or:
    return "||";
  case BinaryOp::BitAnd:
    return "&";
  case BinaryOp::BitOr:
    return "|";
  case BinaryOp::BitXor:
    return "^";
  case BinaryOp::LShift:
    return "<<";
  case BinaryOp::RShift:
    return ">>";
  case BinaryOp::Concat:
    return "..";
  default:
    return "?";
  }
}

/**
 * @brief Get string representation of UnaryOp
 */
[[nodiscard]] inline constexpr const char *unaryOpToString(UnaryOp op) noexcept {
  switch (op) {
  case UnaryOp::Neg:
    return "-";
  case UnaryOp::Not:
    return "!";
  case UnaryOp::BitNot:
    return "~";
  case UnaryOp::Len:
    return "#";
  default:
    return "?";
  }
}

/**
 * @brief Get string representation of PrimitiveKind
 */
[[nodiscard]] inline constexpr const char *primitiveKindToString(PrimitiveKind kind) noexcept {
  switch (kind) {
  case PrimitiveKind::Int:
    return "int";
  case PrimitiveKind::Float:
    return "float";
  case PrimitiveKind::Number:
    return "number";
  case PrimitiveKind::String:
    return "string";
  case PrimitiveKind::Bool:
    return "bool";
  case PrimitiveKind::Void:
    return "void";
  case PrimitiveKind::Null:
    return "null";
  case PrimitiveKind::Coroutine:
    return "coro";
  case PrimitiveKind::Function:
    return "function";
  default:
    return "?";
  }
}

// ============================================================================
// TYPE-SAFE CAST HELPERS
// ============================================================================

/**
 * @brief Safe cast with kind check (returns nullptr if wrong kind)
 */
template <typename T> [[nodiscard]] inline T *ast_cast(AstNode *node) noexcept {
  if (node && node->kind == T::Kind) {
    return static_cast<T *>(node);
  }
  return nullptr;
}

template <typename T> [[nodiscard]] inline const T *ast_cast(const AstNode *node) noexcept {
  if (node && node->kind == T::Kind) {
    return static_cast<const T *>(node);
  }
  return nullptr;
}

/**
 * @brief Checked cast (asserts in debug, no check in release)
 */
template <typename T> [[nodiscard]] inline T *ast_cast_checked(AstNode *node) noexcept {
  assert(node && node->kind == T::Kind);
  return static_cast<T *>(node);
}

template <typename T> [[nodiscard]] inline const T *ast_cast_checked(const AstNode *node) noexcept {
  assert(node && node->kind == T::Kind);
  return static_cast<const T *>(node);
}

/**
 * @brief Check if node is of specific type
 */
template <typename T> [[nodiscard]] inline bool ast_isa(const AstNode *node) noexcept {
  return node && node->kind == T::Kind;
}

} // namespace ast
} // namespace lang
