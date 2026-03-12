#pragma once

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

extern "C" {
#include "Vm/lauxlib.h"
#include "Vm/lua.h"
#include "Vm/lualib.h"
}

namespace fs = std::filesystem;

static thread_local std::stringstream *g_currentCapture = nullptr;

static int lua_capture_print(lua_State *L) {
  if (!g_currentCapture)
    return 0;

  int n = lua_gettop(L);

  lua_getglobal(L, "tostring"); // index: n+1

  for (int i = 2; i <= n; i++) {
    if (i > 2)
      *g_currentCapture << " ";

    // 单独处理数字类型，解决 5.14 显示为 5.1400000000000006 的问题
    if (lua_type(L, i) == LUA_TNUMBER) {
      if (lua_isinteger(L, i)) {
        *g_currentCapture << lua_tointeger(L, i);
      } else {
        // 是浮点数
        double val = lua_tonumber(L, i);
        // 使用 %.14g 格式化。
        // %.14g 是 Lua 5.1/5.2 的默认行为，能有效隐藏 IEEE 754 的微小误差
        // 同时保留有意义的精度。
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.14g", val);
        *g_currentCapture << buf;
      }
    } else {
      // 其他类型（String, Table, Bool, Nil）走 Lua 原生 tostring
      lua_pushvalue(L, n + 1); /* 将 tostring 函数压栈 */
      lua_pushnil(L);          /* receiver参数 */
      lua_pushvalue(L, i);     /* 将参数压栈 */
      lua_call(L, 2, 1);       /* 调用 tostring，2个参数 */
      const char *s = lua_tostring(L, -1);
      if (s == NULL)
        return luaL_error(L, "'tostring' must return a string to 'print'");
      *g_currentCapture << s;
      lua_pop(L, 1); /* 弹出结果 */
    }
  }
  *g_currentCapture << "\n"; // print 默认换行
  lua_pop(L, 1);             /* 弹出 tostring 函数 */
  return 0;
}

// 统一的测试运行器，兼容新老接口
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
    std::vector<ModuleDef> modules;         // 辅助模块文件
    bool expectRuntimeError;                // 是否预期发生运行时错误
    std::function<void(lua_State *)> nativeRegistrar; // 原生绑定注册函数（可选）
  };

  // --- 新接口（保持兼容性）---
  void addTest(const std::string &name, const std::string &script,
               const std::string &expectedOutput) {
    tests_.push_back({name, script, expectedOutput, {}, false, nullptr});
  }

  void addNativeTest(const std::string &name, const std::string &script,
                     const std::string &expectedOutput,
                     std::function<void(lua_State *)> registrar) {
    tests_.push_back({name, script, expectedOutput, {}, false, registrar});
  }

  void addModuleTest(const std::string &name, const std::vector<ModuleDef> &modules,
                     const std::string &script, const std::string &expectedOutput,
                     bool expectRuntimeError = false) {
    tests_.push_back({name, script, expectedOutput, modules, expectRuntimeError, nullptr});
  }

  void addModuleNativeTest(const std::string &name, const std::vector<ModuleDef> &modules,
                           const std::string &script, const std::string &expectedOutput,
                           std::function<void(lua_State *)> registrar,
                           bool expectRuntimeError = false) {
    tests_.push_back({name, script, expectedOutput, modules, expectRuntimeError, registrar});
  }

  void addFailTest(const std::string &name, const std::string &script) {
    tests_.push_back({name, script, "", {}, true, nullptr});
  }

  void addNativeFailTest(const std::string &name, const std::string &script,
                         std::function<void(lua_State *)> registrar) {
    tests_.push_back({name, script, "", {}, true, registrar});
  }

  // --- 旧接口（兼容性别名）---
  void runTest(const std::string &name, const std::string &script,
               const std::string &expectedOutput) {
    tests_.push_back({name, script, expectedOutput, {}, false, nullptr});
  }

  void runModuleTest(const std::string &name, const std::vector<ModuleDef> &modules,
                     const std::string &script, const std::string &expectedOutput,
                     bool expectRuntimeError = false) {
    tests_.push_back({name, script, expectedOutput, modules, expectRuntimeError, nullptr});
  }

  void runFailTest(const std::string &name, const std::string &script) {
    tests_.push_back({name, script, "", {}, true, nullptr});
  }

  void runBenchmark(const std::string &name, const std::string &script,
                    const std::string &expectedOutput) {
    // 基准测试暂时实现为普通测试
    tests_.push_back({name, script, expectedOutput, {}, false, nullptr});
  }

  // 统一的运行方法
  int runAll() {
    int passed = 0;
    int total = tests_.size();

    // 准备测试环境目录
    testDir_ = "./test_env_tmp";
    if (fs::exists(testDir_))
      fs::remove_all(testDir_);
    fs::create_directories(testDir_);

    std::cout << "Running " << total << " tests ..." << std::endl;

    for (const auto &test : tests_) {
      if (runSingleTest(test)) {
        passed++;
      }
    }

    // 清理环境
    if (fs::exists(testDir_))
      fs::remove_all(testDir_);

    std::cout << "==================================================" << std::endl;
    if (passed == total) {
      std::cout << "[  PASSED  ] All " << total << " tests passed." << std::endl;
    } else {
      std::cout << "🔴 [  FAILED  ] " << (total - passed) << " tests failed." << std::endl;
    }

    return (passed == total) ? 0 : 1;
  }

  // 获取测试用例用于Catch2集成
  const std::vector<TestCase> &getTests() const { return tests_; }

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

  void setupModules(const std::vector<ModuleDef> &modules) {
    for (const auto &mod : modules) {
      // 模块文件创建为 .spt 后缀
      std::string path = testDir_ + "/" + mod.name + ".spt";
      std::ofstream file(path);
      file << mod.content;
      file.close();
    }
  }

  void cleanupModules(const std::vector<ModuleDef> &modules) {
    for (const auto &mod : modules) {
      std::string path = testDir_ + "/" + mod.name + ".spt";
      if (fs::exists(path))
        fs::remove(path);
    }
  }

  // 配置 Lua 的 package.path 以便 require 能找到测试用的 .spt 文件
  void setupLuaPath(lua_State *L) {
    lua_getglobal(L, "package");
    lua_getfield(L, -1, "path");
    std::string curPath = lua_tostring(L, -1);
    lua_pop(L, 1); // pop old path

    // 添加 ./test_env_tmp/?.spt 到搜索路径
    // 注意：Lua 的路径分隔符是 ;
    std::string newPath = curPath + ";" + testDir_ + "/?.spt";

    lua_pushstring(L, newPath.c_str());
    lua_setfield(L, -2, "path");
    lua_pop(L, 1); // pop package
  }

  bool runSingleTest(const TestCase &test) {
    auto start = std::chrono::high_resolution_clock::now();

    // 0. 环境准备
    setupModules(test.modules);

    // 1. 初始化 Lua 虚拟机
    lua_State *L = luaL_newstate();
    if (!L) {
      printFail(test.name, "Failed to create Lua state", "", "");
      cleanupModules(test.modules);
      return false;
    }

    luaL_openlibs(L); // 加载标准库

    // 2. 配置重定向
    std::stringstream capturedOutput;
    g_currentCapture = &capturedOutput;

    // 覆盖全局 print 函数
    lua_pushcfunction(L, lua_capture_print);
    lua_setglobal(L, "print");

    // 配置 require 搜索路径
    setupLuaPath(L);

    // 3. 注册原生绑定（如果有）
    if (test.nativeRegistrar) {
      test.nativeRegistrar(L);
    }

    // 4. 加载并运行脚本
    int status = luaL_loadbuffer(L, test.script.c_str(), test.script.size(), test.name.c_str());

    if (status == LUA_OK) {
      // 编译成功，现在运行
      status = lua_pcall(L, 0, 0, 0);
    }

    // 计时结束
    auto end = std::chrono::high_resolution_clock::now();
    double duration = std::chrono::duration<double, std::milli>(end - start).count();

    // 获取错误信息（如果有）
    std::string errorMsg;
    if (status != LUA_OK) {
      const char *msg = lua_tostring(L, -1);
      errorMsg = msg ? msg : "Unknown Lua Error";
      lua_pop(L, 1);
    }

    // 5. 资源清理
    g_currentCapture = nullptr;
    lua_close(L);
    cleanupModules(test.modules);

    // 6. 结果判定

    // 情况 A: 预期运行时错误 (包含编译错误)
    if (test.expectRuntimeError) {
      if (status != LUA_OK) {
        std::cout << "[       OK ] " << test.name << " (Expected Error Caught)" << " (" << duration
                  << " ms)" << std::endl;
        return true;
      } else {
        printFail(test.name, "Expected Error, but run Successfully", "Runtime/Compile Error", "OK");
        return false;
      }
    }

    // 情况 B: 正常执行
    if (status != LUA_OK) {
      printFail(test.name, "Unexpected Error", test.expectedOutput, errorMsg);
      return false;
    }

    std::string actual = trim(capturedOutput.str());
    std::string expected = trim(test.expectedOutput);

    if (actual == expected) {
      std::cout << "[       OK ] " << test.name << " (" << duration << " ms)" << std::endl;
      return true;
    } else {
      printFail(test.name, "Output Mismatch", expected, actual);
      return false;
    }
  }

  void printFail(const std::string &name, const std::string &reason, const std::string &expected,
                 const std::string &actual) {
    std::cout << "🔴 [  FAILED  ] " << name << std::endl;
    std::cout << "             Reason: " << reason << std::endl;
    if (!expected.empty() || !actual.empty()) {
      std::cout << "             Expected: \"" << escapeNewlines(expected) << "\"" << std::endl;
      std::cout << "             Actual:   \"" << escapeNewlines(actual) << "\"" << std::endl;
    }
  }
};