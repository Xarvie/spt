#define LSP_DEBUG_ENABLED
#include "LspLogger.h"
#include "LspService.h"
#include "SemanticAnalyzer.h"
#include <iostream>

using namespace lang::lsp;

int main() {
  LspService service;
  service.initialize("/workspace");
  service.didOpen("file:///test.spt", R"(
class Inner {
    int value = 100;
}

map<any, any> NS = {};
NS.Inner = Inner;

Inner a = Inner();
a.

auto obj = NS.Inner;
    )",
                  1);

  Position pos{12, 2}; // 'auto'
  std::cout << "--- HOVER AUTO ---" << std::endl;
  auto result = service.hover("file:///test.spt", pos);
  std::cout << "Hover on auto: " << result.contents << std::endl;

  pos = {12, 6}; // 'obj'
  std::cout << "--- HOVER OBJ ---" << std::endl;
  result = service.hover("file:///test.spt", pos);
  std::cout << "Hover on obj: " << result.contents << std::endl;

  pos = {10, 3}; // 'a.'
  std::cout << "--- COMPLETION a. ---" << std::endl;
  auto comp = service.completion("file:///test.spt", pos);
  std::cout << "Completions for a.:" << std::endl;
  for (const auto &item : comp.items) {
    std::cout << "  " << item.label << std::endl;
  }

  return 0;
}
