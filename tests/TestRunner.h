#pragma once

#include "catch_amalgamated.hpp"
#include "util_os.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "Ast/ast.h"
#include "Common/BytecodeSerializer.h"
#include "Compiler/Compiler.h"
#include "Vm/VM.h"

namespace spt::test {
namespace fs = std::filesystem;

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

  TestRunner() : testDir_(getTestDir()) {
    cleanupTestDir();
    setupTestDir();
  }

  ~TestRunner() { cleanupTestDir(); }

  // 添加普通测试
  void runTest(const std::string &name, const std::string &script,
               const std::string &expectedOutput) {
    SECTION(name) { runSingleTest({name, script, expectedOutput, {}, false}); }
  }

  // 添加带模块文件的测试
  void runModuleTest(const std::string &name, const std::vector<ModuleDef> &modules,
                     const std::string &script, const std::string &expectedOutput,
                     bool expectRuntimeError = false) {
    SECTION(name) { runSingleTest({name, script, expectedOutput, modules, expectRuntimeError}); }
  }

  // 添加预期失败的测试 (Negative Test)
  void runFailTest(const std::string &name, const std::string &script) {
    SECTION(name) { runSingleTest({name, script, "", {}, true}); }
  }

  void runBenchmark(const std::string &name, const std::string &script,
                    const std::string &expectedOutput) {
    BENCHMARK(std::string{name}) { runSingleTest({name, script, expectedOutput, {}, false}); };
  }

private:
  std::string testDir_;

  // 获取唯一的测试目录名称
  std::string getTestDir() {
    static std::atomic<uint64_t> counter{0};
    std::ostringstream oss;

    oss << "./test_env_" + std::to_string(get_current_process_id()) + "_" +
               std::to_string(++counter);
    return oss.str();
  }

  void setupTestDir() {
    std::error_code ec;
    fs::create_directories(testDir_, ec);
  }

  // 清理临时目录
  void cleanupTestDir() {
    std::error_code ec;
    fs::remove_all(testDir_, ec);
  }

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
      std::string path = testDir_ + "/" + mod.name + ".spt";
      std::ofstream file(path);
      file << mod.content;
      file.close();
    }
  }

  // 清理辅助文件
  void cleanupModules(const std::vector<ModuleDef> &modules) {
    std::error_code ec;
    for (const auto &mod : modules) {
      std::string path = testDir_ + "/" + mod.name + ".spt";
      fs::remove(path, ec);
    }
  }

  void runSingleTest(const TestCase &test) {
    // 0. 环境准备 - 创建独立的临时目录
    setupModules(test.modules);

    // 1. 解析
    AstNode *ast = loadAst(test.script, "test_script");
    REQUIRE(ast);
    if (!ast) {
      cleanupModules(test.modules);
      return;
    }

    // 2. 编译
    Compiler compiler("main");
    std::string compileErrors;
    compiler.setErrorHandler([&](const CompileError &err) {
      compileErrors += "Line " + std::to_string(err.line) + ": " + err.message + "\n";
    });

    CompiledChunk chunk = compiler.compile(ast);
    destroyAst(ast); // 编译完成后即可销毁 AST

    REQUIRE(!compiler.hasError());
    if (compiler.hasError()) {
      cleanupModules(test.modules);
      return;
    }

    // 3. 运行
    VMConfig config;
    config.debugMode = false;
    config.modulePaths = {testDir_}; // 将临时目录加入模块搜索路径
    VM vm(config);

    std::stringstream capturedOutput;
    std::stringstream capturedErrors;
    vm.setPrintHandler([&](const std::string &msg) { capturedOutput << msg; });
    vm.setErrorHandler([&](const std::string &err, int) { capturedErrors << err; });
    InterpretResult result = vm.interpret(chunk);

    std::string actual = trim(capturedOutput.str());
    std::string errors = trim(capturedErrors.str());
    std::string expected = trim(test.expectedOutput);

    CAPTURE(test.name, actual, expected, errors);

    // 4. 清理环境
    cleanupModules(test.modules);

    // 5. 结果判定

    // 情况 A: 预期运行时错误
    if (test.expectRuntimeError) {
      REQUIRE(result != InterpretResult::OK);
      return;
    }

    // 情况 B: 正常执行
    REQUIRE(result == InterpretResult::OK);
    if (result != InterpretResult::OK) {
      return;
    }

    REQUIRE(actual == expected);
  }
};
} // namespace spt::test