#pragma once
#include "Test.h"

// =========================================================
// 12. 内置函数测试 (Built-in Functions) - 简化版本，仅测试支持的功能
// =========================================================

inline void registerBuiltinFunctions(Test &runner) {
  // tonumber - 数字转换
  runner.addTest("Builtin - tonumber",
                 R"(
            print(math.floor(3.7));
            print(math.floor(3.2));
            print(tonumber("42"));
       )",
                 "3\n3\n42");

  // tostring - 字符串转换
  runner.addTest("Builtin - tostring",
                 R"(
            print(tostring(42));
            print(tostring(true));
            print(tostring(false));
            print(tostring(null));
       )",
                 "42\ntrue\nfalse\nnil");

  // typeof - 类型检查（使用实际的返回类型）
  runner.addTest("Builtin - typeof",
                 R"(
            print(typeof(42));
            print(typeof(3.14));
            print(typeof("hello"));
            print(typeof(true));
            print(typeof(null));
            print(typeof([1,2,3]));
            print(typeof({"a": 1}));
       )",
                 "number\nnumber\nstring\nboolean\nnil\narray\ntable");

  // typeof 类型判断
  runner.addTest("Builtin - typeof checks",
                 R"(
            print(typeof([1, 2, 3]) == "array");
            print(typeof("not a list") == "array");
            print(typeof({"a": 1}) == "table");
            print(typeof([1, 2]) == "table");
            auto f = function() -> void {};
            print(typeof(f) == "function");
            print(typeof(42) == "function");
       )",
                 "true\nfalse\ntrue\nfalse\ntrue\nfalse");

  // 数学函数 - 仅测试支持的函数
  runner.addTest("Builtin - Math Functions - supported only",
                 R"(
            int i = 4;
            i~/=3.1;
            print(i);
            print(4~/3.1);
            print(math.abs(-5));
            print(math.abs(5));
            print(math.abs(-3.14));
            print(math.floor(3.7));
            print(math.floor(3.2));
            print(math.ceil(3.2));
            print(math.ceil(3.7));
       )",
                 "1\n1\n5\n5\n3.14\n3\n3\n4\n4");

  runner.addTest("Builtin - sqrt",
                 R"(
            print(math.floor(math.sqrt(16)));
            print(math.floor(math.sqrt(9)));
       )",
                 "4\n3");

  runner.addTest("Builtin - min max (math.min/math.max)",
                 R"(
            print(math.min(3, 7));
            print(math.min(10, 2));
            print(math.max(3, 7));
            print(math.max(10, 2));
            print(math.min(-5, 5));
            print(math.max(-5, 5));
       )",
                 "3\n2\n7\n10\n-5\n5");

  // 长度运算符
  runner.addTest("Builtin - # length operator",
                 R"(
            print(#"hello");
            print(#[1, 2, 3, 4]);
            print(#"");
            print(#[]);
       )",
                 "5\n4\n0\n0");

  // pcall 错误处理 - 简化测试，只测试pcall的基本功能
  runner.addTest("Builtin - pcall error handling",
                 R"(
            // 一个简单的函数，接受两个参数并返回它们的和
            int add(int a, int b){
              return a + b;
            }
            
            // 一个会抛出错误的函数
            function throwError(){
              error("custom error message", 0);
              return 42;
            }
            
            // 测试1: 使用pcall调用add函数
            vars ok1, result1 = pcall(add, 10, 20);
            if(ok1) {
                print("pcall success - result:", result1);
            } else {
                print("pcall failed - error:", result1);
            }
            
            // 测试2: pcall捕获错误
            vars ok2, result2 = pcall(throwError);
            if(ok2) {
                print("pcall with error success - result:", result2);
            } else {
                print("pcall with error failed - ok:", ok2, "error:", result2);
            }
       )",
                 "pcall success - result: 30\npcall with error failed - ok: false error: custom "
                 "error message");

  // xpcall 错误处理与回调测试
  runner.addTest(
      "Builtin - xpcall error handling",
      R"(
            int add(int a, int b){
              return a + b;
            }

            function throwError(){
              error("xpcall custom error", 0);
              return 42;
            }

            // 错误处理回调 (加上了 string 类型声明)
            function myErrorHandler(string err){
              return "Handled: " .. tostring(err);
            }

            // 测试1: xpcall 成功调用
            vars ok1, result1 = xpcall(add, myErrorHandler, 10, 20);
            if(ok1) {
                print("xpcall success - result:", result1);
            } else {
                print("xpcall failed unexpectedly");
            }

            // 测试2: xpcall 捕获错误，并验证 handler 是否正常执行
            vars ok2, result2 = xpcall(throwError, myErrorHandler);
            if(ok2) {
                print("xpcall with error success - result:", result2);
            } else {
                // 期望输出 result2 为 "Handled: xpcall custom error"
                print("xpcall failed - error:", result2);
            }
       )",
      "xpcall success - result: 30\nxpcall failed - error: Handled: xpcall custom error");
}
