#include "test_framework.h"
#include "action_chain.h"

using namespace lsp_test;

TEST(chain_basic_completion) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int myVariable = 10;
string myString = "hello";

int main() {
    my
}
)")
        .testCompletion(5, 6, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
            ASSERT(hasCompletionItem(result, "myVariable"));
            ASSERT(hasCompletionItem(result, "myString"));
        })
        .runAndPrint(ctx);
}

TEST(chain_incomplete_code_recovery) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int myVar = 10;
int other = 20;

int main() {
    int x = my
}
)")
        .testCompletion(5, 14, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
            ASSERT(hasCompletionItem(result, "myVar"));
        })
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_delete_and_complete) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int myVariable = 10;
int myValue = 20;

int main() {
    int x = myVariable;
}
)")
        .testCompletion(5, 14, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(hasCompletionItem(result, "myVariable"));
            ASSERT(hasCompletionItem(result, "myValue"));
        })
        .deleteText(5, 12, 20)
        .testCompletion(5, 12, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_insert_and_complete) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int counter = 0;

int main() {
}
)")
        .insertText(4, 1, "    count")
        .testCompletion(4, 9, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(hasCompletionItem(result, "counter"));
        })
        .runAndPrint(ctx);
}

TEST(chain_member_access_incomplete) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
class MyClass {
    int value;
    string name;
    
    int getValue() {
        return this.value;
    }
}

int main() {
    MyClass obj = new MyClass();
    obj.
}
)")
        .testCompletion(11, 8, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_function_call_signature) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int add(int a, int b) {
    return a + b;
}

int main() {
    add(
}
)")
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_error_recovery_after_delete) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int myFunc() {
    return 42;
}

int main() {
    int x = myFunc();
}
)")
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() == 0);
        })
        .deleteLine(1)
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() > 0);
        })
        .changeFile(R"(
int myFunc() {
    return 42;
}

int main() {
    int x = myFunc();
}
)")
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() == 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_multiline_edit) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int main() {
    int a = 1;
}
)")
        .insertText(2, 0, "    int b = 2;\n")
        .insertText(3, 0, "    int c = 3;\n")
        .testDocumentSymbols([](TestContext& ctx, const std::vector<lang::lsp::DocumentSymbol>& symbols) {
            ASSERT(symbols.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_definition_after_change) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int myGlobalVar = 100;

int main() {
    int x = myGlobalVar;
}
)")
        .testDefinition(4, 14, [](TestContext& ctx, const std::vector<lang::lsp::LocationLink>& locs) {
            ASSERT(locs.size() > 0);
        })
        .changeFile(R"(
int renamedVar = 100;

int main() {
    int x = renamedVar;
}
)")
        .testDefinition(4, 14, [](TestContext& ctx, const std::vector<lang::lsp::LocationLink>& locs) {
            ASSERT(locs.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_partial_identifier_completion) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int calculateTotal = 100;
int calculateSum = 200;
int calculator = 300;

int main() {
    calc
}
)")
        .testCompletion(6, 9, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() >= 3);
            bool hasTotal = false, hasSum = false, hasOr = false;
            for (const auto& item : result.items) {
                if (item.label == "calculateTotal") hasTotal = true;
                if (item.label == "calculateSum") hasSum = true;
                if (item.label == "calculator") hasOr = true;
            }
            ASSERT(hasTotal && hasSum && hasOr);
        })
        .runAndPrint(ctx);
}

TEST(chain_import_completion) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
import { myFunction } from "utils.spt";

int main() {
    my
}
)")
        .testCompletion(4, 7, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_class_method_completion) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
class Calculator {
    int value;
    
    void setValue(int v) {
        this.value = v;
    }
    
    int getValue() {
        return this.value;
    }
}

int main() {
    Calculator calc = new Calculator();
    calc.
}
)")
        .testCompletion(14, 10, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_incomplete_for_loop) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int main() {
    for (
}
)")
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() > 0);
        })
        .changeFile(R"(
int main() {
    for (i = 1, 10) {
        print(i);
    }
}
)")
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() == 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_incomplete_class_definition) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
class MyClass {
    int x
}
)")
        .testCompletion(2, 10, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .insertText(2, 9, ";")
        .testDocumentSymbols([](TestContext& ctx, const std::vector<lang::lsp::DocumentSymbol>& symbols) {
            ASSERT(symbols.size() > 0);
        })
        .runAndPrint(ctx);
}

TEST(chain_complex_edit_scenario) {
    TestContext ctx;
    
    ActionChain()
        .initialize("/workspace")
        .openFile("test.spt", R"(
int add(int a, int b) {
    return a + b;
}

int main() {
    int result = add(1, 2);
    return result;
}
)")
        .testDiagnostics([](TestContext& ctx, const std::vector<lang::lsp::Diagnostic>& diags) {
            ASSERT(diags.size() == 0);
        })
        .testCompletion(7, 20, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .changeFile(R"(
int multiply(int a, int b) {
    return a * b;
}

int main() {
    int result = multiply(1, 2);
    return result;
}
)")
        .testCompletion(7, 20, [](TestContext& ctx, const lang::lsp::CompletionResult& result) {
            ASSERT(result.items.size() > 0);
        })
        .runAndPrint(ctx);
}
