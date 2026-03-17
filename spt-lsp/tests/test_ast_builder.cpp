#include "test_framework.h"
#include "SourceFile.h"
#include "AstNodes.h"

using namespace lang::lsp;
using namespace lang::ast;

template <typename T>
T* castNode(AstNode* node) {
    if (!node) return nullptr;
    if constexpr (std::is_same_v<T, ForStmtNode>) {
        if (node->kind == AstKind::ForStmt) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, VarDeclNode>) {
        if (node->kind == AstKind::VarDecl) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, FunctionDeclNode>) {
        if (node->kind == AstKind::FunctionDecl) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, MultiVarDeclNode>) {
        if (node->kind == AstKind::MultiVarDecl) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, DeferStmtNode>) {
        if (node->kind == AstKind::DeferStmt) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, ImportStmtNode>) {
        if (node->kind == AstKind::ImportStmt) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, ClassDeclNode>) {
        if (node->kind == AstKind::ClassDecl) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, UpdateAssignStmtNode>) {
        if (node->kind == AstKind::UpdateAssignStmt) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, BinaryExprNode>) {
        if (node->kind == AstKind::BinaryExpr) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, MapExprNode>) {
        if (node->kind == AstKind::MapExpr) return static_cast<T*>(node);
    } else if constexpr (std::is_same_v<T, LambdaExprNode>) {
        if (node->kind == AstKind::LambdaExpr) return static_cast<T*>(node);
    }
    return nullptr;
}

template <typename T>
T* findNode(CompilationUnitNode* ast) {
    if (!ast) return nullptr;
    
    for (auto* stmt : ast->statements) {
        if (auto* t = castNode<T>(stmt)) {
            return t;
        }
        
        if (stmt->kind == AstKind::DeclStmt) {
            auto* declStmt = static_cast<DeclStmtNode*>(stmt);
            if (auto* t = castNode<T>(declStmt->decl)) {
                return t;
            }
        }
    }
    
    return nullptr;
}

TEST(ast_numeric_for_style) {
    SourceFile file("test.spt", R"(
for (i = 1, 10) {
    print(i);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* forStmt = findNode<ForStmtNode>(ast);
    ASSERT_NOT_NULL(forStmt);
    ASSERT(forStmt->style == ForStmtNode::Style::Numeric);
}

TEST(ast_numeric_for_with_step) {
    SourceFile file("test.spt", R"(
for (int i = 1, 10, 2) {
    print(i);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* forStmt = findNode<ForStmtNode>(ast);
    ASSERT_NOT_NULL(forStmt);
    ASSERT(forStmt->style == ForStmtNode::Style::Numeric);
    ASSERT_NOT_NULL(forStmt->numericVar);
    ASSERT_NOT_NULL(forStmt->start);
    ASSERT_NOT_NULL(forStmt->end);
    ASSERT_NOT_NULL(forStmt->step);
}

TEST(ast_foreach_style) {
    SourceFile file("test.spt", R"(
for (k, v : pairs(t)) {
    print(k, v);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* forStmt = findNode<ForStmtNode>(ast);
    ASSERT_NOT_NULL(forStmt);
    ASSERT(forStmt->style == ForStmtNode::Style::ForEach);
}

TEST(ast_idiv_operator) {
    SourceFile file("test.spt", R"(
int a = 10 ~/ 3;
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* varDecl = findNode<VarDeclNode>(ast);
    ASSERT_NOT_NULL(varDecl);
    ASSERT_NOT_NULL(varDecl->initializer);
    
    auto* binExpr = castNode<BinaryExprNode>(varDecl->initializer);
    ASSERT_NOT_NULL(binExpr);
    ASSERT(binExpr->op == BinaryOp::IDiv);
}

TEST(ast_coroutine_type) {
    SourceFile file("test.spt", R"(
coro myFunc() {
    return;
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* funcDecl = findNode<FunctionDeclNode>(ast);
    ASSERT_NOT_NULL(funcDecl);
    ASSERT_NOT_NULL(funcDecl->returnType);
    ASSERT(funcDecl->returnType->kind == AstKind::PrimitiveType);
}

TEST(ast_vars_multi_return_func) {
    SourceFile file("test.spt", R"(
vars getValues() {
    return 1, 2;
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* funcDecl = findNode<FunctionDeclNode>(ast);
    ASSERT_NOT_NULL(funcDecl);
    ASSERT(funcDecl->isMultiReturn);
}

TEST(ast_vars_multi_return_var) {
    SourceFile file("test.spt", R"(
vars a, b = getValues();
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* multiVarDecl = findNode<MultiVarDeclNode>(ast);
    ASSERT_NOT_NULL(multiVarDecl);
    ASSERT(multiVarDecl->names.size() == 2);
}

TEST(ast_map_int_key) {
    SourceFile file("test.spt", R"(
auto m = {1: "one"};
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* varDecl = findNode<VarDeclNode>(ast);
    ASSERT_NOT_NULL(varDecl);
    ASSERT_NOT_NULL(varDecl->initializer);
    
    auto* mapExpr = castNode<MapExprNode>(varDecl->initializer);
    ASSERT_NOT_NULL(mapExpr);
    ASSERT(mapExpr->entries.size() == 1);
}

TEST(ast_defer_statement) {
    SourceFile file("test.spt", R"(
defer {
    cleanup();
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* deferStmt = findNode<DeferStmtNode>(ast);
    ASSERT_NOT_NULL(deferStmt);
    ASSERT_NOT_NULL(deferStmt->body);
}

TEST(ast_lambda_expression) {
    SourceFile file("test.spt", R"(
auto f = function(int x) -> int { return x * 2; };
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* varDecl = findNode<VarDeclNode>(ast);
    ASSERT_NOT_NULL(varDecl);
    ASSERT_NOT_NULL(varDecl->initializer);
    
    auto* lambdaExpr = castNode<LambdaExprNode>(varDecl->initializer);
    ASSERT_NOT_NULL(lambdaExpr);
    ASSERT_NOT_NULL(lambdaExpr->returnType);
    ASSERT(lambdaExpr->params.size() == 1);
}

TEST(ast_import_namespace) {
    SourceFile file("test.spt", R"(
import * as MyModule from "module.spt";
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* importStmt = findNode<ImportStmtNode>(ast);
    ASSERT_NOT_NULL(importStmt);
    ASSERT(importStmt->style == ImportStmtNode::Style::Namespace);
}

TEST(ast_import_named) {
    SourceFile file("test.spt", R"(
import {foo, bar as baz} from "module.spt";
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* importStmt = findNode<ImportStmtNode>(ast);
    ASSERT_NOT_NULL(importStmt);
    ASSERT(importStmt->style == ImportStmtNode::Style::Named);
    ASSERT(importStmt->specifiers.size() == 2);
}

TEST(ast_class_declaration) {
    SourceFile file("test.spt", R"(
class MyClass {
    int x;
    
    int getX() {
        return this.x;
    }
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    
    auto* classDecl = findNode<ClassDeclNode>(ast);
    ASSERT_NOT_NULL(classDecl);
}
