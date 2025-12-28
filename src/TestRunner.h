#pragma once

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "Ast/ast.h"
#include "Compiler/Compiler.h"
#include "Vm/VM.h"

using namespace spt;
namespace fs = std::filesystem;

extern AstNode *loadAst(const std::string &sourceCode,
                        const std::string &filename);

class TestRunner {
public:
  struct ModuleDef {
    std::string name;
    std::string content;
  };

  struct TestCase {
    std::string name;
    std::string script;
    std::string expectedOutput;
    std::vector<ModuleDef> modules; // 辅助模块文件
    bool expectRuntimeError;        // 是否预期发生运行时错误
  };

  // 添加普通测试
  void addTest(const std::string &name, const std::string &script,
               const std::string &expectedOutput) {
    tests_.push_back({name, script, expectedOutput, {}, false});
  }

  // 添加带模块文件的测试
  void addModuleTest(const std::string &name,
                     const std::vector<ModuleDef> &modules,
                     const std::string &script,
                     const std::string &expectedOutput) {
    tests_.push_back({name, script, expectedOutput, modules, false});
  }

  // 添加预期失败的测试 (Negative Test)
  void addFailTest(const std::string &name, const std::string &script) {
    tests_.push_back({name, script, "", {}, true});
  }

  int runAll() {
    int passed = 0;
    int total = tests_.size();

    // 准备测试环境目录
    testDir_ = "./test_env_tmp";
    if (fs::exists(testDir_))
      fs::remove_all(testDir_);
    fs::create_directories(testDir_);

    std::cout << "=================================================="
              << std::endl;
    std::cout << "Running " << total << " tests..." << std::endl;
    std::cout << "=================================================="
              << std::endl;

    for (const auto &test : tests_) {
      if (runSingleTest(test)) {
        passed++;
      }
    }

    // 清理环境
    if (fs::exists(testDir_))
      fs::remove_all(testDir_);

    std::cout << "=================================================="
              << std::endl;
    if (passed == total) {
      std::cout << "\033[32m[  PASSED  ] All " << total
                << " tests passed.\033[0m" << std::endl;
    } else {
      std::cout << "\033[31m[  FAILED  ] " << (total - passed)
                << " tests failed.\033[0m" << std::endl;
    }

    return (passed == total) ? 0 : 1;
  }

private:
  std::vector<TestCase> tests_;
  std::string testDir_;

  std::string trim(const std::string &str) {
    size_t first = str.find_first_not_of(" \t\n\r");
    if (std::string::npos == first)
      return "";
    size_t last = str.find_last_not_of(" \t\n\r");
    return str.substr(first, (last - first + 1));
  }

  std::string escapeNewlines(const std::string &str) {
    std::string res = str;
    size_t pos = 0;
    while ((pos = res.find('\n', pos)) != std::string::npos) {
      res.replace(pos, 1, "\\n");
      pos += 2;
    }
    return res;
  }

  // 创建测试所需的辅助文件
  void setupModules(const std::vector<ModuleDef> &modules) {
    for (const auto &mod : modules) {
      std::string path = testDir_ + "/" + mod.name + ".flx";
      std::ofstream file(path);
      file << mod.content;
      file.close();
    }
  }

  // 清理辅助文件
  void cleanupModules(const std::vector<ModuleDef> &modules) {
    for (const auto &mod : modules) {
      std::string path = testDir_ + "/" + mod.name + ".flx";
      if (fs::exists(path))
        fs::remove(path);
    }
  }

  bool runSingleTest(const TestCase &test) {
    auto start = std::chrono::high_resolution_clock::now();

    // 0. 环境准备
    setupModules(test.modules);

    // 1. 解析
    AstNode *ast = loadAst(test.script, "test_script");
    if (!ast) {
      printFail(test.name, "Parse Error", "", "");
      cleanupModules(test.modules);
      return false;
    }

    // 2. 编译
    Compiler compiler("main");
    std::string compileErrors;
    compiler.setErrorHandler([&](const CompileError &err) {
      compileErrors +=
          "Line " + std::to_string(err.line) + ": " + err.message + "\n";
    });

    CompiledChunk chunk = compiler.compile(ast);
    destroyAst(ast); // 编译完成后即可销毁 AST

    if (compiler.hasError()) {
      printFail(test.name, "Compilation Failed", "", compileErrors);
      cleanupModules(test.modules);
      return false;
    }

    // 3. 运行
    VMConfig config;
    config.debugMode = false;
    config.modulePaths = {testDir_}; // 将临时目录加入模块搜索路径
    VM vm(config);

    std::stringstream capturedOutput;
    vm.setPrintHandler([&](const std::string &msg) { capturedOutput << msg; });

    InterpretResult result = vm.interpret(chunk);

    // 计时结束
    auto end = std::chrono::high_resolution_clock::now();
    double duration =
        std::chrono::duration<double, std::milli>(end - start).count();

    // 4. 清理环境
    cleanupModules(test.modules);

    // 5. 结果判定

    // 情况 A: 预期运行时错误
    if (test.expectRuntimeError) {
      if (result != InterpretResult::OK) {
        std::cout << "\033[32m[       OK ]\033[0m " << test.name
                  << " (Expected Error Caught)" << " (" << duration << " ms)"
                  << std::endl;
        return true;
      } else {
        printFail(test.name, "Expected Runtime Error, but got OK",
                  "Runtime Error", "OK");
        return false;
      }
    }

    // 情况 B: 正常执行
    if (result != InterpretResult::OK) {
      printFail(test.name, "Unexpected Runtime Error", test.expectedOutput,
                capturedOutput.str());
      return false;
    }

    std::string actual = trim(capturedOutput.str());
    std::string expected = trim(test.expectedOutput);

    if (actual == expected) {
      std::cout << "\033[32m[       OK ]\033[0m " << test.name << " ("
                << duration << " ms)" << std::endl;
      return true;
    } else {
      printFail(test.name, "Output Mismatch", expected, actual);
      return false;
    }
  }

  void printFail(const std::string &name, const std::string &reason,
                 const std::string &expected, const std::string &actual) {
    std::cout << "\033[31m[  FAILED  ]\033[0m " << name << std::endl;
    std::cout << "             Reason: " << reason << std::endl;
    if (!expected.empty() || !actual.empty()) {
      std::cout << "             Expected: \"" << escapeNewlines(expected)
                << "\"" << std::endl;
      std::cout << "             Actual:   \"" << escapeNewlines(actual) << "\""
                << std::endl;
    }
  }
};