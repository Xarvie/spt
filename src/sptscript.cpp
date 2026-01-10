#include "Ast/ast.h"
#include "BytecodeSerializer.h"
#include "Compiler/Compiler.h"
#include "Vm/VM.h"

#include <filesystem>
#include <fstream>
#include <iostream>

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
  AstNode *ast = loadAst(source, filename);
  if (!ast) {
    return -1;
  }

  // 3. 编译
  spt::Compiler compiler("main");
  compiler.setErrorHandler([](const spt::CompileError &err) {
    std::cerr << "[Compile Error] " << err.filename << ":" << err.line << " " << err.message
              << std::endl;
  });

  spt::CompiledChunk chunk = compiler.compile(ast);

  if (compiler.hasError()) {
    return -1;
  }

  spt::VMConfig config;
  config.enableGC = true;

  config.modulePaths.push_back(std::filesystem::path(path).parent_path().string());

  spt::VM vm(config);

  vm.setPrintHandler([](const std::string &msg) { std::cout << msg; });

  vm.setErrorHandler([](const std::string &msg, int line) {
    std::cerr << "[Runtime Error] " << msg << std::endl;
  });

  spt::InterpretResult result = vm.interpret(chunk);

  return (result == spt::InterpretResult::OK) ? 0 : -1;
}

int main(int argc, char *argv[]) {
  if (argc <= 1)
    return -1;
  return runScript(argv[1]);
}