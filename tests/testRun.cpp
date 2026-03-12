/**
 * SPT语言回归测试程序
 *
 * 这个程序使用TestRunner.h来自动验证SPT语言的所有核心功能。
 * 集成所有测试模块进行全面的回归测试。
 */

#include "Test.h"
#include <iostream>

// 导入现有的测试模块
#include "TestBasics.h"
#include "TestBuiltins.h"
#include "TestClasses.h"
#include "TestCompiler.h"
#include "TestControlFlow.h"
#include "TestFunctions.h"
#include "TestListMap.h"
#include "TestModules.h"
#include "TestOperatorOverload.h"

int main() {
  Test runner;

  std::cout << "================================================" << std::endl;
  std::cout << "SPT语言回归测试开始" << std::endl;
  std::cout << "================================================" << std::endl;

  // 注册所有测试模块
  std::cout << "注册基础语法测试..." << std::endl;
  registerBasics(runner);

  std::cout << "注册控制流测试..." << std::endl;
  registerControlFlow(runner);

  std::cout << "注册函数测试..." << std::endl;
  registerFunctions(runner);

  std::cout << "注册List/Map测试..." << std::endl;
  registerListMapTest(runner);
  registerListMapFullTest(runner);

  std::cout << "注册内置函数测试..." << std::endl;
  registerBuiltinFunctions(runner);

  std::cout << "注册类测试..." << std::endl;
  registerClasses(runner);

  std::cout << "注册编译器测试..." << std::endl;
  registerCompilerTest(runner);

  std::cout << "注册模块测试..." << std::endl;
  registerModules(runner);

  std::cout << "注册运算符重载测试..." << std::endl;
  registerOperatorOverloadTest(runner);

  // 统计测试数量
  size_t totalTests = runner.getTests().size();
  std::cout << "================================================" << std::endl;
  std::cout << "总计 " << totalTests << " 个测试用例" << std::endl;
  std::cout << "开始执行测试..." << std::endl;
  std::cout << "================================================" << std::endl;

  // 运行所有测试
  int result = runner.runAll();
  return result;
}