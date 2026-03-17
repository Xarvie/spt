#include "test_framework.h"
#include "SourceFile.h"

using namespace lang::lsp;

static void disableDebugLog() {
}

TEST(numeric_for_basic) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
for (i = 1, 10) {
    print(i);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(numeric_for_with_step) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
for (int i = 1, 10, 2) {
    print(i);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(numeric_for_auto_type) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
for (auto i = 1, 10) {
    print(i);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(foreach_basic) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
for (k, v : pairs(t)) {
    print(k, v);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(foreach_typed) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
for (string k, auto v : items) {
    print(k, v);
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(idiv_operator) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
int a = 10 ~/ 3;
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(idiv_assign_operator) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
int a = 10;
a ~/= 3;
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(coroutine_type) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
coro myCoroutine() {
    return;
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(coroutine_variable) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
coro c = myCoroutine();
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(vars_multi_return) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
vars a, b = getValues();
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(vars_multi_return_with_modifiers) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
vars global a, const b = getValues();
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(function_multi_return) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
vars getValues() {
    return 1, 2;
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(map_entry_int_key) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
auto m = {1: "one", 2: "two"};
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(map_entry_float_key) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
auto m = {1.5: "half", 2.5: "two and half"};
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(import_namespace) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
import * as MyModule from "mymodule.spt";
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(import_named) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
import {foo, bar as baz} from "mymodule.spt";
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(defer_statement) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
defer {
    cleanup();
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(lambda_expression) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
auto f = function(int x) -> int { return x * 2; };
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(lambda_multi_return) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
auto f = function(int x) -> vars { return x, x * 2; };
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(class_declaration) {
    disableDebugLog();
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
    ASSERT_NO_ERRORS(file);
}

TEST(class_static_member) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
class MyClass {
    static int count;
    
    static int getCount() {
        return count;
    }
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}

TEST(complete_program) {
    disableDebugLog();
    SourceFile file("test.spt", R"(
import * as Util from "util.spt";

int globalVar = 100;

vars calculate(int a, int b) {
    return a + b, a - b;
}

class Calculator {
    int value;
    
    void setValue(int v) {
        this.value = v;
    }
    
    int add(int x) {
        this.value += x;
        return this.value;
    }
}

int main() {
    vars sum, diff = calculate(10, 5);
    
    for (i = 1, 10) {
        print(i);
    }
    
    for (k, v : pairs({1: "a", 2: "b"})) {
        print(k, v);
    }
    
    int result = 10 ~/ 3;
    result ~/= 2;
    
    Calculator calc = new Calculator();
    calc.setValue(5);
    calc.add(3);
    
    defer {
        print("cleanup");
    }
    
    return 0;
}
)");
    auto* ast = file.getAst();
    ASSERT_NOT_NULL(ast);
    ASSERT_NO_ERRORS(file);
}
