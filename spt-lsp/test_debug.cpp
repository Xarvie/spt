#include "LspService.h"
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
a.value = 10;

auto obj = NS.Inner;
    )",
                  1);

  // Check hover on 'auto'
  Position pos{11, 2}; // 'auto' is line 11 (0-based)
  auto result = service.hover("file:///test.spt", pos);
  std::cout << "Hover on auto: " << result.contents << std::endl;

  // Check hover on 'obj'
  pos = {11, 6}; // 'obj'
  result = service.hover("file:///test.spt", pos);
  std::cout << "Hover on obj: " << result.contents << std::endl;

  // Check completion on 'a.'
  pos = {9, 2}; // 'a.' is line 9, column 2
  auto comp = service.completion("file:///test.spt", pos);
  std::cout << "Completions for a.:" << std::endl;
  for (const auto &item : comp.items) {
    std::cout << "  " << item.label << " (" << (int)item.kind << ")" << std::endl;
  }

  return 0;
}
