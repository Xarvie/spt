// =========================================================
// 测试框架主入口
// =========================================================
// 本文件整合所有测试模块，通过注释/取消注释来选择运行的测试


#include "TestRunner.h"

// =========================================================
// 测试模块头文件
// =========================================================

// 性能基准测试
#include "bench/TestBenchmarks.h"

// 原生绑定（类定义和测试）
//#include "test/NativeBindings.h"
//#include "test/TestNativeBindings.h"

// 基础语法测试
#include "test/TestBasics.h"

// 控制流测试（包含泛型循环）
#include "test/TestControlFlow.h"

// 函数与闭包测试
#include "test/TestFunctions.h"

// 类与对象测试
#include "test/TestClasses.h"

// 数据结构测试（List, Map, String）
#include "test/TestDataStructures.h"

// 模块系统测试
#include "test/TestModules.h"

// 方法调用测试
#include "test/TestInvoke.h"

// 边界情况与回归测试
#include "test/TestEdgeCases.h"

// 综合测试
#include "test/TestIntegration.h"

// 内置函数测试
#include "test/TestBuiltins.h"

// Defer 语句测试
#include "test/TestDefer.h"

// Fiber 协程测试
#include "test/TestFiber.h"

// 栈重分配安全测试
#include "test/TestStackRealloc.h"

// GC 垃圾回收测试
#include "test/TestGC.h"

#include "test/TestShortCircuitDiagnostics.h"

// 多返回值测试
#include "TestMultiReturn.h"

// 编译测试
#include "TestBytes.h"
#include "TestCompiler.h"
//#include "TestSptAPI.h"
//#include "TestSptAPI2.h"

// =========================================================
// 主函数
// =========================================================
int main(int argc, char *argv[]) {
  TestRunner runner;

#if 0
  // =====================================================
  // 完整测试套件
  // =====================================================

  // 基础语法
  registerBasics(runner);
  registerControlFlow(runner);


  // 函数与闭包
  registerFunctions(runner);

  // 类与对象
  registerClasses(runner);

  // 数据结构
  registerLists(runner);
  registerMaps(runner);
  registerStrings(runner);

  // 模块系统
  registerModules(runner);

  // 方法调用
  registerInvokeTests(runner);

  // 边界情况与回归
  registerEdgeCases(runner);

  // 综合测试
  registerIntegrationTests(runner);

  // 内置函数
  registerBuiltinFunctions(runner);

  // Defer 语句
  registerDeferTests(runner);

//  // Fiber 协程
//  registerFiberTests(runner);

//  // Bytes
//  registerBytesTests(runner);

  // 栈重分配安全
  registerStackReallocationTests(runner);
//
//  // 原生绑定
//  registerNativeBindingTests(runner);

  // GC 测试
  // registerGCTests(runner);
//  registerGCDebugTests(runner); // 需要调试输出时启用

  registerShortCircuitDiagnostics(runner);

  registerMultiReturnTests(runner);
  registerNativeMultiReturnTests(runner);

  registerCompilerTest(runner);

#else
  // =====================================================
  // 快速测试 / 单项测试
  // =====================================================

  //  registerFib40Bench(runner);
  registerBench2(runner);
#endif
  runner.runAll();
  return 0;
}

