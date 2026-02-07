#include "Ast/ast.h"
#include "BytecodeSerializer.h"


#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

int runScript(const char *path) {
  std::string source;
  try {
    std::ifstream file(path);
    if (!file) {
      std::cerr << "Could not open file: " << path << std::endl;
      return -1;
    }
    std::stringstream buffer;
    buffer << file.rdbuf();
    source = buffer.str();
  } catch (const std::exception &e) {
    std::cerr << "Error reading file: " << e.what() << std::endl;
    return -1;
  }

  std::string filename = std::filesystem::path(path).filename().string();

  // 2. 解析 AST
  AstNode *ast = loadAst(source.c_str(), filename.c_str());
  if (!ast) {
    return -1;
  }

//  return (result == spt::InterpretResult::OK) ? 0 : -1;
  return 0;
}

int main(int argc, char *argv[]) {
  if (argc <= 1)
    return -1;
  return runScript(argv[1]);
}