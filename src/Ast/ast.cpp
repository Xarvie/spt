#include "ast.h"
#include <algorithm>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <variant>
#include <vector>

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

AstType::AstType(SourceLocation loc) : location(std::move(loc)) {}

AstType::~AstType() = default;

AstNode::AstNode(SourceLocation loc, NodeType type) : location(std::move(loc)), nodeType(type) {}

AstNode::~AstNode() = default;

Expression::Expression(SourceLocation loc, NodeType type) : AstNode(std::move(loc), type) {}

Statement::Statement(SourceLocation loc, NodeType type) : AstNode(std::move(loc), type) {}

Declaration::Declaration(SourceLocation loc, NodeType type) : Statement(std::move(loc), type) {}

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

AstType *PrimitiveType::clone() const { return new PrimitiveType(*this); }

AstType *AnyType::clone() const { return new AnyType(*this); }

AstType *AutoType::clone() const { return new AutoType(*this); }

AstType *ListType::clone() const {
  AstType *clonedElementType = nullptr;
  try {
    clonedElementType = elementType ? elementType->clone() : nullptr;

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

BlockNode::~BlockNode() { deleteVectorItems(statements); }

ExpressionStatementNode::~ExpressionStatementNode() { delete expression; }

AssignmentNode::~AssignmentNode() {
  deleteVectorItems(lvalues);
  deleteVectorItems(rvalues);
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
        [](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;

          if constexpr (std::is_same_v<T, std::vector<Declaration *>> ||
                        std::is_same_v<T, std::vector<Expression *>>) {

            deleteVectorItems(arg);
          } else if constexpr (std::is_same_v<T, AssignmentNode *>)

          {

            delete arg;
          }
        },
        initializer.value());
  }

  delete condition;
  deleteVectorItems(updateActions);
  delete body;
}

ForEachStatementNode::~ForEachStatementNode() {
  deleteVectorItems(loopVariables);
  delete iterableExpr;
  delete body;
}

BreakStatementNode::~BreakStatementNode() = default;
ContinueStatementNode::~ContinueStatementNode() = default;

ReturnStatementNode::~ReturnStatementNode() { deleteVectorItems(returnValue); }

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

ImportSpecifierNode::~ImportSpecifierNode() = default;
ImportNamespaceNode::~ImportNamespaceNode() = default;

ImportNamedNode::~ImportNamedNode() { deleteVectorItems(specifiers); }

void destroyAst(AstNode *node) { delete node; }

#include <any>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#include "../Ast/AstBuilderVisitor.h"
#include "../Ast/front/LangLexer.h"
#include "../Ast/front/LangParser.h"
#include "antlr4-runtime.h"

using namespace antlr4;

AstNode *loadAst(const std::string &sourceCode, const std::string &filename) {
  std::string codeToParse;
  std::string displayFileName = filename.empty() ? "<unknown>" : filename;

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

DeferStatementNode::~DeferStatementNode() {
  if (body)
    delete body;
}