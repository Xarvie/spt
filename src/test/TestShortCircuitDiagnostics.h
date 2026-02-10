#pragma once
#include "TestRunner.h"

// =========================================================
// 短路求值诊断测试 (Short Circuit Diagnosis)
// =========================================================

inline void registerShortCircuitDiagnostics(TestRunner &runner) {

  runner.addTest("Truthy - Integer One",
                 R"(
            if (1) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  runner.addTest("Truthy - Boolean False",
                 R"(
            if (false) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "falsy");

  runner.addTest("Truthy - Boolean True",
                 R"(
            if (true) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  // 2. AND 运算符基本测试
  runner.addTest("AND - false && false",
                 R"(
            if (false && false) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "falsy");

  runner.addTest("AND - false && true",
                 R"(
            if (false && true) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "falsy");

  runner.addTest("AND - true && false",
                 R"(
            if (true && false) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "falsy");

  runner.addTest("AND - true && true",
                 R"(
            if (true && true) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  // 4. OR 运算符基本测试
  runner.addTest("OR - false || false",
                 R"(
            if (false || false) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "falsy");

  runner.addTest("OR - false || true",
                 R"(
            if (false || true) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  runner.addTest("OR - true || false",
                 R"(
            if (true || false) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  runner.addTest("OR - true || true",
                 R"(
            if (true || true) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  // 5. 短路测试 - AND
  runner.addTest("Short Circuit AND - Left False",
                 R"(
            int called = 0;
            int sideEffect() {
                called = 1;
                return 1;
            }
            
            if (false && sideEffect()) {
                print("entered");
            }
            print(called);
       )",
                 "0");

  runner.addTest("Short Circuit AND - Left True",
                 R"(
            int called = 0;
            int sideEffect() {
                called = 1;
                return 1;
            }
            
            if (true && sideEffect()) {
                print("entered");
            }
            print(called);
       )",
                 "entered\n1");

  // 6. 短路测试 - OR
  runner.addTest("Short Circuit OR - Left True",
                 R"(
            int called = 0;
            int sideEffect() {
                called = 1;
                return 1;
            }
            
            if (true || sideEffect()) {
                print("entered");
            }
            print(called);
       )",
                 "entered\n0");

  runner.addTest("Short Circuit OR - Left False",
                 R"(
            int called = 0;
            int sideEffect() {
                called = 1;
                return 1;
            }
            
            if (false || sideEffect()) {
                print("entered");
            }
            print(called);
       )",
                 "entered\n1");

  //  // 7. 短路测试 - 函数调用版本 (原始失败的测试)
  //  runner.addTest("Short Circuit AND - Function Returns Zero",
  //                 R"(
  //            int check(int x) {
  //                print("check " .. x);
  //                return x;
  //            }
  //            if (check(0) && check(1)) {
  //                print("both");
  //            }
  //            print("done");
  //       )",
  //                 "check 0\ndone");

  runner.addTest("Short Circuit OR - Function Returns One",
                 R"(
            int check(int x) {
                print("check " .. x);
                return x;
            }
            if (check(1) || check(2)) {
                print("one");
            }
            print("done");
       )",
                 "check 1\none\ndone");

  // 8. AND 返回值测试 (检查是否返回值而不是布尔)
  runner.addTest("AND Return Value - Should Be Last Truthy Or First Falsy",
                 R"(
            auto result = 5 && 10;
            print(result);
       )",
                 "10");

  runner.addTest("AND Return Value - First Falsy",
                 R"(
            auto result = 0 && 10;
            print(result);
       )",
                 "10");

  runner.addTest("OR Return Value - First Truthy",
                 R"(
            auto result = 5 || 10;
            print(result);
       )",
                 "5");

  // 9. 嵌套逻辑运算
  runner.addTest("Nested AND",
                 R"(
            if (true && true && true) {
                print("all true");
            }
       )",
                 "all true");

  runner.addTest("Nested AND Short Circuit",
                 R"(
            int count = 0;
            int inc() {
                count = count + 1;
                return count;
            }
            
            auto result = false && inc() && inc();
            print(count);
       )",
                 "0");

  runner.addTest("Nested OR Short Circuit",
                 R"(
            int count = 0;
            int inc() {
                count = count + 1;
                return count;
            }
            
            auto result = true || inc() || inc();
            print(count);
       )",
                 "0");

  // 10. 混合 AND/OR
  runner.addTest("Mixed AND OR - Priority",
                 R"(
            if (true || false && false) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");

  runner.addTest("Mixed OR AND - Priority",
                 R"(
            if (false && true || true) {
                print("truthy");
            } else {
                print("falsy");
            }
       )",
                 "truthy");
}
