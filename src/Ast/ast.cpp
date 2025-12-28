#include "ast.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits> // for std::decay_t in ForCStyleStatementNode destructor
#include <variant>
#include <vector>

// --- 辅助函数实现 ---
static std::vector<AstType *> cloneAstTypeList(const std::vector<AstType *> &source) {
  std::vector<AstType *> clonedList;
  clonedList.reserve(source.size());
  try {
    for (const AstType *item : source) {
      clonedList.push_back(item ? item->clone() : nullptr);
    }
  } catch (...) {
    deleteVectorItems(clonedList);
    throw;
  }
  return clonedList;
}

// --- 基类构造/析构函数实现 ---
AstType::AstType(SourceLocation loc) : location(std::move(loc)) {}
AstType::~AstType() = default;

// AstNode 构造函数现在初始化 nodeType
AstNode::AstNode(SourceLocation loc, NodeType type)
    : location(std::move(loc)), nodeType(type) {} // <--- 实现修改
AstNode::~AstNode() = default;

// Expression 构造函数传递 type 给 AstNode
Expression::Expression(SourceLocation loc, NodeType type)
    : AstNode(std::move(loc), type) {} // <--- 实现修改

// Statement 构造函数传递 type 给 AstNode
Statement::Statement(SourceLocation loc, NodeType type)
    : AstNode(std::move(loc), type) {} // <--- 实现修改

// Declaration 构造函数传递 type 给 Statement -> AstNode
Declaration::Declaration(SourceLocation loc, NodeType type)
    : Statement(std::move(loc), type) {} // <--- 实现修改

// --- 具体类型节点构造函数和析构函数 ---
PrimitiveType::PrimitiveType(PrimitiveTypeKind kind, SourceLocation loc)
    : AstType(std::move(loc)), primitiveKind(kind) {}
PrimitiveType::~PrimitiveType() = default;
AnyType::AnyType(SourceLocation loc) : AstType(std::move(loc)) {}
AnyType::~AnyType() = default;
AutoType::AutoType(SourceLocation loc) : AstType(std::move(loc)) {}
AutoType::~AutoType() = default;
ListType::ListType(AstType *elemType, SourceLocation loc)
    : AstType(std::move(loc)), elementType(elemType) {}
ListType::~ListType() { delete elementType; }
MapType::MapType(AstType *kType, AstType *vType, SourceLocation loc)
    : AstType(std::move(loc)), keyType(kType), valueType(vType) {}
MapType::~MapType() {
  delete keyType;
  delete valueType;
}
UnionType::UnionType(std::vector<AstType *> members, SourceLocation loc)
    : AstType(std::move(loc)), memberTypes(std::move(members)) {}
UnionType::~UnionType() { deleteVectorItems(memberTypes); }
TupleType::TupleType(std::vector<AstType *> elements, SourceLocation loc)
    : AstType(std::move(loc)), elementTypes(std::move(elements)) {}
TupleType::~TupleType() { deleteVectorItems(elementTypes); }

UserType::~UserType() = default;
FunctionKeywordType::FunctionKeywordType(SourceLocation loc) : AstType(std::move(loc)) {}
FunctionKeywordType::~FunctionKeywordType() = default;
CoroutineKeywordType::CoroutineKeywordType(SourceLocation loc) : AstType(std::move(loc)) {}
CoroutineKeywordType::~CoroutineKeywordType() = default;

// --- 具体类型节点 CLONE 方法实现 ---
AstType *PrimitiveType::clone() const { return new PrimitiveType(*this); }
AstType *AnyType::clone() const { return new AnyType(*this); }
AstType *AutoType::clone() const { return new AutoType(*this); }
AstType *ListType::clone() const {
  AstType *clonedElementType = nullptr;
  try {
    clonedElementType = elementType ? elementType->clone() : nullptr;
    // 注意：这里假设 ListType 的构造函数没有因为移动了 NodeType
    // 初始化而改变其他参数
    return new ListType(clonedElementType, location);
  } catch (...) {
    delete clonedElementType;
    throw;
  }
}
AstType *MapType::clone() const {
  AstType *clonedKeyType = nullptr;
  AstType *clonedValueType = nullptr;
  try {
    clonedKeyType = keyType ? keyType->clone() : nullptr;
    clonedValueType = valueType ? valueType->clone() : nullptr;
    return new MapType(clonedKeyType, clonedValueType, location);
  } catch (...) {
    delete clonedKeyType;
    delete clonedValueType;
    throw;
  }
}
AstType *UnionType::clone() const { return new UnionType(cloneAstTypeList(memberTypes), location); }
AstType *TupleType::clone() const {
  return new TupleType(cloneAstTypeList(elementTypes), location);
}
AstType *UserType::clone() const { return new UserType(this->qualifiedNameParts, this->location); }
AstType *FunctionKeywordType::clone() const { return new FunctionKeywordType(*this); }
AstType *CoroutineKeywordType::clone() const { return new CoroutineKeywordType(*this); }

// --- 具体 AST 节点构造函数和析构函数 ---
// *** 构造函数定义已移至 ast.h (内联) ***
// 只保留析构函数定义

// 字面量
LiteralIntNode::~LiteralIntNode() = default;
LiteralFloatNode::~LiteralFloatNode() = default;
LiteralStringNode::~LiteralStringNode() = default;
LiteralBoolNode::~LiteralBoolNode() = default;
LiteralNullNode::~LiteralNullNode() = default;
LiteralListNode::~LiteralListNode() { deleteVectorItems(elements); }
MapEntryNode::~MapEntryNode() {
  delete key;
  delete value;
}
LiteralMapNode::~LiteralMapNode() { deleteVectorItems(entries); }

// 表达式
IdentifierNode::~IdentifierNode() = default;
UnaryOpNode::~UnaryOpNode() { delete operand; }
BinaryOpNode::~BinaryOpNode() {
  delete left;
  delete right;
}
FunctionCallNode::~FunctionCallNode() {
  delete functionExpr;
  deleteVectorItems(arguments);
}
MemberAccessNode::~MemberAccessNode() { delete objectExpr; }
MemberLookupNode::~MemberLookupNode() { delete objectExpr; }
IndexAccessNode::~IndexAccessNode() {
  delete arrayExpr;
  delete indexExpr;
}
ParameterDeclNode::~ParameterDeclNode() { delete typeAnnotation; }
LambdaNode::~LambdaNode() {
  deleteVectorItems(params);
  delete returnType;
  delete body;
}
NewExpressionNode::~NewExpressionNode() {
  delete classType;
  deleteVectorItems(arguments);
}
ThisExpressionNode::~ThisExpressionNode() = default;
VarArgsNode::~VarArgsNode() = default;

// 语句
BlockNode::~BlockNode() { deleteVectorItems(statements); }
ExpressionStatementNode::~ExpressionStatementNode() { delete expression; }
AssignmentNode::~AssignmentNode() {
  deleteVectorItems(lvalues); // 清理左值 vector
  deleteVectorItems(rvalues); // 清理右值 vector
}
UpdateAssignmentNode::~UpdateAssignmentNode() {
  delete lvalue;
  delete rvalue;
}
IfClauseNode::~IfClauseNode() {
  delete condition;
  delete body;
}
IfStatementNode::~IfStatementNode() {
  delete condition;
  delete thenBlock;
  deleteVectorItems(elseIfClauses);
  delete elseBlock;
}
WhileStatementNode::~WhileStatementNode() {
  delete condition;
  delete body;
}
ForCStyleStatementNode::~ForCStyleStatementNode() {
  if (initializer.has_value()) {
    std::visit(
        [](auto &&arg) {                         // 使用 std::visit 访问 variant
          using T = std::decay_t<decltype(arg)>; // 获取 arg 的实际类型

          if constexpr (std::is_same_v<T, std::vector<Declaration *>> ||
                        std::is_same_v<T, std::vector<Expression *>>) {
            // 如果是 vector<...> 指针类型，调用 deleteVectorItems
            deleteVectorItems(arg);
          } else if constexpr (std::is_same_v<T, AssignmentNode *>) // <<< 修改：检查是否为
                                                                    // AssignmentNode*
          {
            // 如果是 AssignmentNode* 类型，直接 delete 指针
            delete arg;
          }

        },
        initializer.value()); // 对 optional 中的 variant 值进行操作
  }
  // 清理其他成员保持不变
  delete condition;
  deleteVectorItems(updateActions);
  delete body;
}
ForEachStatementNode::~ForEachStatementNode() {
  deleteVectorItems(loopVariables); // 清理 vector 中的所有指针
  delete iterableExpr;
  delete body;
}
BreakStatementNode::~BreakStatementNode() = default;
ContinueStatementNode::~ContinueStatementNode() = default;
ReturnStatementNode::~ReturnStatementNode() { deleteVectorItems(returnValue); }

// 声明
VariableDeclNode::~VariableDeclNode() {
  delete typeAnnotation;
  delete initializer;
}
MutiVariableDeclarationNode::~MutiVariableDeclarationNode() { delete initializer; }
FunctionDeclNode::~FunctionDeclNode() {
  deleteVectorItems(params);
  delete returnType;
  delete body;
}
ClassMemberNode::~ClassMemberNode() { delete memberDeclaration; }
ClassDeclNode::~ClassDeclNode() { deleteVectorItems(members); }

ImportSpecifierNode::~ImportSpecifierNode() = default; // std::optional<std::string> 会自动管理
ImportNamespaceNode::~ImportNamespaceNode() = default;
ImportNamedNode::~ImportNamedNode() { deleteVectorItems(specifiers); }

// --- 顶级删除函数 ---
void destroyAst(AstNode *node) {
  delete node; // 触发根节点的析构函数，从而递归删除整个树
}

#include <any>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

// --- ANTLR4 ---
#include "../Ast/AstBuilderVisitor.h"
#include "../Ast/front/LangLexer.h"
#include "../Ast/front/LangParser.h"
#include "antlr4-runtime.h"

using namespace antlr4;

AstNode *loadAst(const std::string &sourceCode, const std::string &filename) {
  std::string codeToParse;
  std::string displayFileName = filename.empty() ? "<unknown>" : filename;

  // 1. 确定输入源
  if (sourceCode.empty()) {
    if (filename.empty()) {
      std::cerr << "[Ast Error] Both sourceCode and filename are empty." << std::endl;
      return nullptr;
    }
    std::ifstream file(filename);
    if (!file.is_open()) {
      std::cerr << "[Ast Error] Cannot open file: " << filename << std::endl;
      return nullptr;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    codeToParse = buffer.str();
  } else {
    codeToParse = sourceCode;
  }

  // 2. 词法与语法分析
  ANTLRInputStream input(codeToParse);
  LangLexer lexer(&input);
  CommonTokenStream tokens(&lexer);
  LangParser parser(&tokens);

  parser.removeErrorListeners();
  parser.addErrorListener(&ConsoleErrorListener::INSTANCE);

  auto *tree = parser.compilationUnit();
  if (parser.getNumberOfSyntaxErrors() > 0) {
    std::cerr << "[Ast Error] Syntax errors in " << displayFileName << std::endl;
    return nullptr;
  }

  // 3. 构建 AST
  AstBuilderVisitor builder(displayFileName);
  std::any astAny;

  try {
    astAny = builder.visitCompilationUnit(tree);
  } catch (const std::exception &e) {
    std::cerr << "[Ast Error] Builder exception: " << e.what() << std::endl;
    return nullptr;
  }

  if (!astAny.has_value())
    return nullptr;

  try {
    return std::any_cast<AstNode *>(astAny);
  } catch (const std::bad_any_cast &) {
    return nullptr;
  }
}