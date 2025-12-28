#ifndef SPT_AST_CPP_RAWPTR_H
#define SPT_AST_CPP_RAWPTR_H

#include <cstdint>    // 用于 int64_t
#include <functional> // 用于 std::function
#include <optional>   // 用于 std::optional
#include <stdexcept>  // 用于 std::runtime_error 等
#include <string>
#include <type_traits> // 用于 std::decay_t
#include <utility>
#include <utility> // for std::move
#include <variant> // 用于 ForInitializerVariant
#include <vector>

// --- 源码位置信息 ---
struct SourceLocation {
  std::string filename; ///< 文件名
  int line = 0;         ///< 行号 (从 1 开始)
  int column = 0;       ///< 列号 (从 1 开始)
  SourceLocation() = default;

  SourceLocation(std::string file, int ln, int col)
      : filename(std::move(file)), line(ln), column(col) {}
};

// --- 前置声明 ---
class AstVisitor;
class AstNode; // Forward declare for NodeType enum's dependency
enum class NodeType {
  // 字面量节点
  LITERAL_INT,
  LITERAL_FLOAT,
  LITERAL_STRING,
  LITERAL_BOOL,
  LITERAL_NULL,
  LITERAL_LIST,
  LITERAL_MAP,
  MAP_ENTRY, // MapEntryNode 也是一个节点

  // 表达式节点
  IDENTIFIER,
  UNARY_OP,
  BINARY_OP,
  FUNCTION_CALL,
  MEMBER_ACCESS,
  MEMBER_LOOKUP,
  INDEX_ACCESS,
  LAMBDA,
  NEW_EXPRESSION,
  THIS_EXPRESSION,
  VAR_ARGS,

  // 语句节点
  BLOCK,
  EXPRESSION_STATEMENT,
  ASSIGNMENT,
  UPDATE_ASSIGNMENT,
  IF_STATEMENT,
  IF_CLAUSE, // IfClauseNode 也是一个节点
  WHILE_STATEMENT,
  FOR_CSTYLE_STATEMENT,
  FOR_EACH_STATEMENT,
  BREAK_STATEMENT,
  CONTINUE_STATEMENT,
  RETURN_STATEMENT,
  IMPORT_STATEMENT, // Import 语句类型
  IMPORT_NAMESPACE, // Namespace Import 子类型
  IMPORT_NAMED,     // Named Import 子类型
  IMPORT_SPECIFIER, // Import Specifier 节点类型
  // 声明节点 (同时也是语句)
  VARIABLE_DECL,
  MUTI_VARIABLE_DECL,
  PARAMETER_DECL,
  FUNCTION_DECL,
  CLASS_DECL,
  CLASS_MEMBER, // ClassMemberNode 也是一个节点

  // 可以考虑添加一个未知或默认类型
  // NODE_UNKNOWN
};
class AstType;
class Expression;
class Statement;
class Declaration;
class ParameterDeclNode;
class MapEntryNode;
class IfClauseNode;
class ClassMemberNode;
class BlockNode;
class PrimitiveType;
class AnyType;
class AutoType;
class ListType;
class MapType;
class UnionType;
class TupleType;
class UserType;
class FunctionKeywordType;
class CoroutineKeywordType;
class ImportStatementNode;
class ImportNamespaceNode;
class ImportNamedNode;
class ImportSpecifierNode;

// --- AST 工具函数 ---
template <typename T> inline void deleteVectorItems(std::vector<T *> &vec) {
  for (T *item : vec) {
    delete item;
  }
  vec.clear();
}

// --- AST 类型节点基类 ---
class AstType {
public:
  SourceLocation location;
  virtual ~AstType();
  virtual AstType *clone() const = 0;

protected:
  AstType(SourceLocation loc);
};

// --- AST 节点基类 ---
class AstNode {
public:
  SourceLocation location;
  NodeType nodeType;

  // 构造函数接受 NodeType
  AstNode(SourceLocation loc, NodeType type);
  virtual ~AstNode();

protected:
  // 保护默认构造函数（如果需要）
  // AstNode() = default;
};

// --- 表达式、语句、声明 基类 ---
class Expression : public AstNode {
protected:
  // 构造函数传递 NodeType 给 AstNode
  Expression(SourceLocation loc, NodeType type);
};

class Statement : public AstNode {
protected:
  // 构造函数传递 NodeType 给 AstNode
  Statement(SourceLocation loc, NodeType type);
};

class Declaration : public Statement {
public:
  bool isModuleRoot = false; // 标记该声明是否定义在模块最外层
protected:
  // 构造函数传递 NodeType 给 Statement -> AstNode
  Declaration(SourceLocation loc, NodeType type);
};

// --- 具体的类型节点 ---
enum class PrimitiveTypeKind { INT, FLOAT, NUMBER, STRING, BOOL, VOID, NULL_TYPE };

class PrimitiveType : public AstType {
public:
  PrimitiveTypeKind primitiveKind;
  PrimitiveType(PrimitiveTypeKind kind, SourceLocation loc);
  virtual ~PrimitiveType() override;
  virtual AstType *clone() const override;
};

class AnyType : public AstType {
public:
  AnyType(SourceLocation loc);
  virtual ~AnyType() override;
  virtual AstType *clone() const override;
};

class AutoType : public AstType {
public:
  AutoType(SourceLocation loc);
  virtual ~AutoType() override;
  virtual AstType *clone() const override;
};

class ListType : public AstType {
public:
  AstType *elementType = nullptr;
  ListType(AstType *elemType, SourceLocation loc);
  virtual ~ListType() override;
  virtual AstType *clone() const override;
};

class MapType : public AstType {
public:
  AstType *keyType = nullptr;
  AstType *valueType = nullptr;
  MapType(AstType *kType, AstType *vType, SourceLocation loc);
  virtual ~MapType() override;
  virtual AstType *clone() const override;
};

class UnionType : public AstType {
public:
  std::vector<AstType *> memberTypes;
  UnionType(std::vector<AstType *> members, SourceLocation loc);
  virtual ~UnionType() override;
  virtual AstType *clone() const override;
};

class TupleType : public AstType {
public:
  std::vector<AstType *> elementTypes;
  TupleType(std::vector<AstType *> elements, SourceLocation loc);
  virtual ~TupleType() override;
  virtual AstType *clone() const override;
};

class UserType : public AstType {
public:
  // --- 修改：将 std::string name 替换为 vector ---
  // std::string name; // 旧的成员
  std::vector<std::string> qualifiedNameParts; ///< 存储限定名的各个部分，例如 {"Module", "Type"}

  // --- 结束修改 ---

  // --- 修改：更新构造函数以接受 vector ---

  UserType(std::vector<std::string> nameParts, SourceLocation loc)
      : AstType(std::move(loc)), qualifiedNameParts(std::move(nameParts)) {}

  virtual ~UserType() override;

  virtual AstType *clone() const override;

  // --- 辅助方法来获取完整的名称字符串 ---
  std::string getFullName() const {
    if (qualifiedNameParts.empty()) {
      return "";
    }
    // 使用 "." 连接 vector 中的字符串
    std::string fullName = qualifiedNameParts[0];
    for (size_t i = 1; i < qualifiedNameParts.size(); ++i) {
      fullName += "." + qualifiedNameParts[i];
    }
    return fullName;
  }
};

class FunctionKeywordType : public AstType {
public:
  FunctionKeywordType(SourceLocation loc);
  virtual ~FunctionKeywordType() override;
  virtual AstType *clone() const override;
};

class CoroutineKeywordType : public AstType {
public:
  CoroutineKeywordType(SourceLocation loc);
  virtual ~CoroutineKeywordType() override;
  virtual AstType *clone() const override;
};

class MultiReturnType : public AstType {
public:
  MultiReturnType(SourceLocation loc) : AstType(std::move(loc)) {}

  virtual ~MultiReturnType() override = default; // 默认析构即可

  virtual AstType *clone() const override { // 实现克隆
    return new MultiReturnType(*this);
  }

  // 可以根据需要添加其他成员或方法，但目前可能不需要
};

// --- 具体的操作符种类枚举 ---
enum class OperatorKind {
  NEGATE,
  NOT,
  LENGTH,
  BW_NOT,
  ADD,
  SUB,
  MUL,
  DIV,
  MOD,
  CONCAT,
  LT,
  LE,
  GT,
  GE,
  EQ,
  NE,
  AND,
  OR,
  BW_AND,
  BW_OR,
  BW_XOR,
  BW_LSHIFT,
  BW_RSHIFT,
  ASSIGN_ADD,
  ASSIGN_SUB,
  ASSIGN_MUL,
  ASSIGN_DIV,
  ASSIGN_MOD,
  ASSIGN_CONCAT,
  ASSIGN_BW_AND,
  ASSIGN_BW_OR,
  ASSIGN_BW_XOR,
  ASSIGN_BW_LSHIFT,
  ASSIGN_BW_RSHIFT
};

// --- 具体的 AST 节点 ---

// --- 字面量 ---
class LiteralIntNode : public Expression {
public:
  int64_t value;

  LiteralIntNode(int64_t val, SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_INT), value(val) {} // 传递类型

  virtual ~LiteralIntNode() override;
};

class LiteralFloatNode : public Expression {
public:
  double value;

  LiteralFloatNode(double val, SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_FLOAT), value(val) {} // 传递类型

  virtual ~LiteralFloatNode() override;
};

class LiteralStringNode : public Expression {
public:
  std::string value;

  LiteralStringNode(std::string val, SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_STRING), value(std::move(val)) {} // 传递类型

  virtual ~LiteralStringNode() override;
};

class LiteralBoolNode : public Expression {
public:
  bool value;

  LiteralBoolNode(bool val, SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_BOOL), value(val) {} // 传递类型

  virtual ~LiteralBoolNode() override;
};

class LiteralNullNode : public Expression {
public:
  LiteralNullNode(SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_NULL) {} // 传递类型

  virtual ~LiteralNullNode() override;
};

class LiteralListNode : public Expression {
public:
  std::vector<Expression *> elements;

  LiteralListNode(std::vector<Expression *> elems, SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_LIST), elements(std::move(elems)) {
  } // 传递类型

  virtual ~LiteralListNode() override;
};

class MapEntryNode : public AstNode { // 直接继承自 AstNode
public:
  Expression *key = nullptr;
  Expression *value = nullptr;

  MapEntryNode(Expression *k, Expression *v, SourceLocation loc)
      : AstNode(std::move(loc), NodeType::MAP_ENTRY), key(k), value(v) {} // 传递类型

  virtual ~MapEntryNode() override;
};

class LiteralMapNode : public Expression {
public:
  std::vector<MapEntryNode *> entries;

  LiteralMapNode(std::vector<MapEntryNode *> ents, SourceLocation loc)
      : Expression(std::move(loc), NodeType::LITERAL_MAP), entries(std::move(ents)) {} // 传递类型

  virtual ~LiteralMapNode() override;
};

// --- 表达式 ---
class IdentifierNode : public Expression {
public:
  std::string name;

  IdentifierNode(std::string n, SourceLocation loc)
      : Expression(std::move(loc), NodeType::IDENTIFIER), name(std::move(n)) {} // 传递类型

  virtual ~IdentifierNode() override;
};

class ImportStatementNode : public Statement {
protected:
  ImportStatementNode(SourceLocation loc, NodeType type) : Statement(std::move(loc), type) {}
};

class ImportSpecifierNode : public AstNode {
public:
  std::string importedName;         // 在模块中导出的原始名称
  std::optional<std::string> alias; // 本地别名 (如果使用了 'as')
  bool isTypeOnly;                  // 标记是否为 type import

  ImportSpecifierNode(std::string name, std::optional<std::string> aliasOpt, bool typeOnly,
                      SourceLocation loc)
      : AstNode(std::move(loc), NodeType::IMPORT_SPECIFIER), importedName(std::move(name)),
        alias(std::move(aliasOpt)), // 移动 optional
        isTypeOnly(typeOnly) {}

  // 获取在本地作用域中使用的名称（别名优先）
  std::string getLocalName() const { return alias.value_or(importedName); }

  virtual ~ImportSpecifierNode() override;
};

// ImportNamespaceNode 保持不变，它已经有 alias 字段
class ImportNamespaceNode : public ImportStatementNode {
public:
  std::string alias;
  std::string modulePath;

  ImportNamespaceNode(std::string aliasName, std::string path, SourceLocation loc)
      : ImportStatementNode(std::move(loc), NodeType::IMPORT_NAMESPACE),
        alias(std::move(aliasName)), modulePath(std::move(path)) {}

  virtual ~ImportNamespaceNode() override;
};

// ImportNamedNode 包含 ImportSpecifierNode
class ImportNamedNode : public ImportStatementNode {
public:
  std::vector<ImportSpecifierNode *> specifiers;
  std::string modulePath;

  ImportNamedNode(std::vector<ImportSpecifierNode *> specs, std::string path, SourceLocation loc)
      : ImportStatementNode(std::move(loc), NodeType::IMPORT_NAMED), specifiers(std::move(specs)),
        modulePath(std::move(path)) {}

  virtual ~ImportNamedNode() override;
};

class UnaryOpNode : public Expression {
public:
  OperatorKind op;
  Expression *operand = nullptr;

  UnaryOpNode(OperatorKind o, Expression *expr, SourceLocation loc)
      : Expression(std::move(loc), NodeType::UNARY_OP), op(o), operand(expr) {} // 传递类型

  virtual ~UnaryOpNode() override;
};

class BinaryOpNode : public Expression {
public:
  OperatorKind op;
  Expression *left = nullptr;
  Expression *right = nullptr;

  BinaryOpNode(OperatorKind o, Expression *l, Expression *r, SourceLocation loc)
      : Expression(std::move(loc), NodeType::BINARY_OP), op(o), left(l), right(r) {} // 传递类型

  virtual ~BinaryOpNode() override;
};

class FunctionCallNode : public Expression {
public:
  Expression *functionExpr = nullptr;
  std::vector<Expression *> arguments;

  FunctionCallNode(Expression *func, std::vector<Expression *> args, SourceLocation loc)
      : Expression(std::move(loc), NodeType::FUNCTION_CALL), functionExpr(func),
        arguments(std::move(args)) {} // 传递类型

  virtual ~FunctionCallNode() override;
};

class MemberAccessNode : public Expression {
public:
  Expression *objectExpr = nullptr;
  std::string memberName;

  MemberAccessNode(Expression *obj, std::string member, SourceLocation loc)
      : Expression(std::move(loc), NodeType::MEMBER_ACCESS), objectExpr(obj),
        memberName(std::move(member)) {} // 传递类型

  virtual ~MemberAccessNode() override;
};

class MemberLookupNode : public Expression {
public:
  Expression *objectExpr = nullptr;
  std::string memberName;

  MemberLookupNode(Expression *obj, std::string member, SourceLocation loc)
      : Expression(std::move(loc), NodeType::MEMBER_LOOKUP), objectExpr(obj),
        memberName(std::move(member)) {} // 传递类型

  virtual ~MemberLookupNode() override;
};

class IndexAccessNode : public Expression {
public:
  Expression *arrayExpr = nullptr;
  Expression *indexExpr = nullptr;

  IndexAccessNode(Expression *array, Expression *index, SourceLocation loc)
      : Expression(std::move(loc), NodeType::INDEX_ACCESS), arrayExpr(array), indexExpr(index) {
  } // 传递类型

  virtual ~IndexAccessNode() override;
};

class ParameterDeclNode : public Declaration {
public:
  std::string name;
  AstType *typeAnnotation = nullptr;

  ParameterDeclNode(std::string n, AstType *type, SourceLocation loc)
      : Declaration(std::move(loc), NodeType::PARAMETER_DECL), name(std::move(n)),
        typeAnnotation(type) {} // 传递类型

  virtual ~ParameterDeclNode() override;
};

class LambdaNode : public Expression {
public:
  std::vector<ParameterDeclNode *> params;
  AstType *returnType = nullptr; // 现在指向 PrimitiveType, ListType, MultiReturnType 等
  BlockNode *body = nullptr;
  bool isVariadic;

  LambdaNode(std::vector<ParameterDeclNode *> p, AstType *retType, BlockNode *b, bool isVar,
             SourceLocation loc)
      : Expression(std::move(loc), NodeType::LAMBDA), params(std::move(p)), returnType(retType),
        body(b), isVariadic(isVar) {}

  virtual ~LambdaNode() override;
};

class NewExpressionNode : public Expression {
public:
  UserType *classType = nullptr;
  std::vector<Expression *> arguments;

  NewExpressionNode(UserType *cType, std::vector<Expression *> args, SourceLocation loc)
      : Expression(std::move(loc), NodeType::NEW_EXPRESSION), classType(cType),
        arguments(std::move(args)) {} // 传递类型

  virtual ~NewExpressionNode() override;
};

class ThisExpressionNode : public Expression {
public:
  ThisExpressionNode(SourceLocation loc)
      : Expression(std::move(loc), NodeType::THIS_EXPRESSION) {} // 传递类型

  virtual ~ThisExpressionNode() override;
};

class VarArgsNode : public Expression {
public:
  VarArgsNode(SourceLocation loc) : Expression(std::move(loc), NodeType::VAR_ARGS) {} // 传递类型

  virtual ~VarArgsNode() override;
};

// --- 语句 ---
class BlockNode : public Statement {
public:
  std::vector<Statement *> statements;

  BlockNode(std::vector<Statement *> stmts, SourceLocation loc)
      : Statement(std::move(loc), NodeType::BLOCK), statements(std::move(stmts)) {} // 传递类型

  virtual ~BlockNode() override;
};

class ExpressionStatementNode : public Statement {
public:
  Expression *expression = nullptr;

  ExpressionStatementNode(Expression *expr, SourceLocation loc)
      : Statement(std::move(loc), NodeType::EXPRESSION_STATEMENT), expression(expr) {} // 传递类型

  virtual ~ExpressionStatementNode() override;
};

class AssignmentNode : public Statement {
public:
  std::vector<Expression *> lvalues;
  std::vector<Expression *> rvalues;

  AssignmentNode(std::vector<Expression *> lhs, std::vector<Expression *> rhs, SourceLocation loc)
      : Statement(std::move(loc), NodeType::ASSIGNMENT), lvalues(std::move(lhs)),
        rvalues(std::move(rhs)) {}

  virtual ~AssignmentNode() override;
};

class UpdateAssignmentNode : public Statement {
public:
  OperatorKind op;
  Expression *lvalue = nullptr;
  Expression *rvalue = nullptr;

  UpdateAssignmentNode(OperatorKind o, Expression *lval, Expression *rval, SourceLocation loc)
      : Statement(std::move(loc), NodeType::UPDATE_ASSIGNMENT), op(o), lvalue(lval), rvalue(rval) {
  } // 传递类型

  virtual ~UpdateAssignmentNode() override;
};

class IfClauseNode : public AstNode { // 直接继承 AstNode
public:
  Expression *condition = nullptr;
  BlockNode *body = nullptr;

  IfClauseNode(Expression *cond, BlockNode *b, SourceLocation loc)
      : AstNode(std::move(loc), NodeType::IF_CLAUSE), condition(cond), body(b) {} // 传递类型

  virtual ~IfClauseNode() override;
};

class IfStatementNode : public Statement {
public:
  Expression *condition = nullptr;
  BlockNode *thenBlock = nullptr;
  std::vector<IfClauseNode *> elseIfClauses;
  BlockNode *elseBlock = nullptr;

  IfStatementNode(Expression *cond, BlockNode *thenB, std::vector<IfClauseNode *> elseIfs,
                  BlockNode *elseB, SourceLocation loc)
      : Statement(std::move(loc), NodeType::IF_STATEMENT), condition(cond), thenBlock(thenB),
        elseIfClauses(std::move(elseIfs)), elseBlock(elseB) {} // 传递类型

  virtual ~IfStatementNode() override;
};

class WhileStatementNode : public Statement {
public:
  Expression *condition = nullptr;
  BlockNode *body = nullptr;

  WhileStatementNode(Expression *cond, BlockNode *b, SourceLocation loc)
      : Statement(std::move(loc), NodeType::WHILE_STATEMENT), condition(cond), body(b) {
  } // 传递类型

  virtual ~WhileStatementNode() override;
};

using ForInitializerVariant = std::variant<std::vector<Declaration *>, // 来自  variableDeclaration
                                           AssignmentNode *,
                                           std::vector<Expression *> // 来自 expressionList
                                           >;

class ForCStyleStatementNode : public Statement {
public:
  std::optional<ForInitializerVariant> initializer;
  Expression *condition = nullptr;
  std::vector<Statement *> updateActions;
  BlockNode *body = nullptr;

  ForCStyleStatementNode(std::optional<ForInitializerVariant> init, Expression *cond,
                         std::vector<Statement *> updateActs, BlockNode *b, SourceLocation loc)
      : Statement(std::move(loc), NodeType::FOR_CSTYLE_STATEMENT), initializer(std::move(init)),
        condition(cond), updateActions(std::move(updateActs)), body(b) {} // 传递类型

  virtual ~ForCStyleStatementNode() override;
};

class ForEachStatementNode : public Statement {
public:
  std::vector<ParameterDeclNode *> loopVariables;
  Expression *iterableExpr = nullptr;
  BlockNode *body = nullptr;

  ForEachStatementNode(std::vector<ParameterDeclNode *> vars, Expression *iter, BlockNode *b,
                       SourceLocation loc)
      : Statement(std::move(loc), NodeType::FOR_EACH_STATEMENT), loopVariables(std::move(vars)),
        iterableExpr(iter), body(b) {} // 传递类型

  virtual ~ForEachStatementNode() override;
};

class BreakStatementNode : public Statement {
public:
  BreakStatementNode(SourceLocation loc)
      : Statement(std::move(loc), NodeType::BREAK_STATEMENT) {} // 传递类型

  virtual ~BreakStatementNode() override;
};

class ContinueStatementNode : public Statement {
public:
  ContinueStatementNode(SourceLocation loc)
      : Statement(std::move(loc), NodeType::CONTINUE_STATEMENT) {} // 传递类型

  virtual ~ContinueStatementNode() override;
};

class ReturnStatementNode : public Statement {
public:
  std::vector<Expression *> returnValue;

  ReturnStatementNode(std::vector<Expression *> retVals, SourceLocation loc)
      : Statement(std::move(loc), NodeType::RETURN_STATEMENT), returnValue(std::move(retVals)) {
  } // 传递类型

  virtual ~ReturnStatementNode() override;
};

class VariableDeclNode : public Declaration {
public:
  std::string name;
  AstType *typeAnnotation = nullptr;
  Expression *initializer = nullptr;
  bool isConst;
  bool isGlobal;
  bool isStatic;
  bool isExported;

  VariableDeclNode(std::string n, AstType *type, Expression *init, bool isC, bool isG, bool isS,
                   bool isExp, SourceLocation loc)
      : Declaration(std::move(loc),
                    NodeType::VARIABLE_DECL), // 调用基类构造函数
        name(std::move(n)),                   // 初始化 name
        typeAnnotation(type),                 // 初始化 typeAnnotation
        initializer(init),                    // 初始化 initializer
        isConst(isC),                         // 初始化 isConst
        isGlobal(isG),                        // 初始化 isGlobal
        isStatic(isS), isExported(isExp) {}   // 初始化 isStatic

  // <<< 这是析构函数的声明 (实现在 ast.cpp) >>>
  virtual ~VariableDeclNode() override;
};

// 用于存储 mutivar 声明中每个变量的信息
struct MultiDeclVariableInfo {
  std::string name;
  bool isGlobal = false; // 从解析器获取
  bool isConst = false;  // 从解析器获取

  MultiDeclVariableInfo(std::string n, bool isG, bool isC)
      : name(std::move(n)), isGlobal(isG), isConst(isC) {}
};

// 代表 "mutivar a, b = expr;" 语句的 AST 节点
class MutiVariableDeclarationNode : public Declaration {
public:
  std::vector<MultiDeclVariableInfo> variables; // 存储所有声明的变量 (现在不含 location)
  Expression *initializer = nullptr;            // 存储 *单个* 初始化表达式的指针
  bool isExported;

  // 构造函数 - 注意基类构造函数会处理 location
  MutiVariableDeclarationNode(std::vector<MultiDeclVariableInfo> vars, Expression *init,
                              bool exported, SourceLocation loc)
      : Declaration(std::move(loc),
                    NodeType::MUTI_VARIABLE_DECL), // 将 loc 传递给基类
        variables(std::move(vars)), initializer(init), isExported(exported) {}

  // 虚析构函数 (声明) - 保持不变
  virtual ~MutiVariableDeclarationNode() override;
};

class FunctionDeclNode : public Declaration {
public:
  std::string name;
  std::vector<ParameterDeclNode *> params;
  AstType *returnType = nullptr; // 现在指向 PrimitiveType, ListType, MultiReturnType 等
  BlockNode *body = nullptr;
  bool isGlobalDecl;
  bool isStatic;
  bool isVariadic;
  bool isExported;

  FunctionDeclNode(std::string n, std::vector<ParameterDeclNode *> p, AstType *retType,
                   BlockNode *b, bool isG, bool isS, bool isVar, bool isExp,
                   SourceLocation loc) // <<< 添加 isExp 参数
      : Declaration(std::move(loc), NodeType::FUNCTION_DECL), name(std::move(n)),
        params(std::move(p)), returnType(retType), body(b), isGlobalDecl(isG), isStatic(isS),
        isVariadic(isVar), isExported(isExp) {}

  virtual ~FunctionDeclNode() override;
};

class ClassMemberNode : public AstNode { // 直接继承 AstNode
public:
  Declaration *memberDeclaration = nullptr;
  bool isStatic;

  ClassMemberNode(Declaration *decl, SourceLocation loc)
      : AstNode(std::move(loc), NodeType::CLASS_MEMBER), memberDeclaration(decl),
        isStatic(false) { // 传递类型
    if (!decl)
      return; // 安全检查
    if (auto *varDecl = dynamic_cast<VariableDeclNode *>(memberDeclaration)) {
      isStatic = varDecl->isStatic;
    } else if (auto *funcDecl = dynamic_cast<FunctionDeclNode *>(memberDeclaration)) {
      isStatic = funcDecl->isStatic;
    }
  }

  virtual ~ClassMemberNode() override;
};

class ClassDeclNode : public Declaration {
public:
  std::string name;
  std::vector<ClassMemberNode *> members;
  bool isExported;

  ClassDeclNode(std::string n, std::vector<ClassMemberNode *> mems, bool isExp,
                SourceLocation loc) // <<< 添加 isExp 参数
      : Declaration(std::move(loc), NodeType::CLASS_DECL), name(std::move(n)),
        members(std::move(mems)), isExported(isExp) {} // 传递类型

  virtual ~ClassDeclNode() override;
};

// --- 顶级删除函数 ---
void destroyAst(AstNode *node);

// 用于加载源码并生成 AST 的工具函数
// 如果 sourceCode 为空，则读取 filename 文件
// 如果 sourceCode 不为空，filename 仅作报错显示用
AstNode *loadAst(const std::string &sourceCode, const std::string &filename);

#endif // SPT_AST_CPP_RAWPTR_H