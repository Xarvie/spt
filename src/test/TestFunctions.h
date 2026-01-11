#pragma once
#include "TestRunner.h"

// =========================================================
// 3. 函数与闭包 (Functions & Closures)
// =========================================================

inline void registerFunctions(TestRunner &runner) {
  runner.addTest("Basic Function",
                 R"(
            int add(int a, int b) {
                return a + b;
            }
            print(add(3, 4));
            print(add(10, 20));
       )",
                 "7\n30");

  runner.addTest("Function with No Return Value",
                 R"(
            void greet(string name) {
                print("Hello, " .. name);
            }
            greet("World");
            greet("Claude");
       )",
                 "Hello, World\nHello, Claude");

  runner.addTest("Nested Functions",
                 R"(
            int outer(int x) {
                int inner(int y) {
                    return y * 2;
                }
                return inner(x) + 1;
            }
            print(outer(5));
            print(outer(10));
       )",
                 "11\n21");

  runner.addTest("Lambda Expression",
                 R"(
            auto add = function(int a, int b) -> int {
                return a + b;
            };
            print(add(3, 4));

            auto mul = function(int x, int y) -> int { return x * y; };
            print(mul(5, 6));
       )",
                 "7\n30");

  runner.addTest("Closure Basic",
                 R"(
            auto makeCounter = function() -> function {
                int count = 0;
                return function() -> int {
                    count = count + 1;
                    return count;
                };
            };
            auto c1 = makeCounter();
            print(c1());
            print(c1());
            print(c1());
       )",
                 "1\n2\n3");

  runner.addTest("Multiple Closures Independent",
                 R"(
            auto makeCounter = function() -> function {
                int count = 0;
                return function() -> int {
                    count = count + 1;
                    return count;
                };
            };
            auto c1 = makeCounter();
            auto c2 = makeCounter();
            print(c1());
            print(c1());
            print(c2());
            print(c1());
            print(c2());
       )",
                 "1\n2\n1\n3\n2");

  runner.addTest("Closure Shared State",
                 R"(
            var setter;
            var getter;
            {
                int x = 10;
                setter = function(int v) -> void { x = v; };
                getter = function() -> int { return x; };
            }
            print(getter());
            setter(42);
            print(getter());
            setter(100);
            print(getter());
       )",
                 "10\n42\n100");

  runner.addTest("Higher-Order Function",
                 R"(
            int apply(function f, int x) {
                return f(x);
            }
            auto double = function(int n) -> int { return n * 2; };
            auto square = function(int n) -> int { return n * n; };
            print(apply(double, 5));
            print(apply(square, 5));
       )",
                 "10\n25");

  runner.addTest("mutivar Function",
                 R"(
            mutivar returnAB(int a, int b) {
                return a, b;
            }
            mutivar a, b = returnAB(1, 2);
            print(a, b);
       )",
                 "1 2");

  runner.addTest("Closure with Multiple Upvalues",
                 R"(
            auto makeAdder = function(int a, int b) -> function {
                return function(int x) -> int {
                    return a + b + x;
                };
            };
            auto add5and3 = makeAdder(5, 3);
            print(add5and3(10));
            print(add5and3(20));
       )",
                 "18\n28");

  runner.addTest("Deeply Nested Closure",
                 R"(
            auto level1 = function(int a) -> function {
                return function(int b) -> function {
                    return function(int c) -> int {
                        return a + b + c;
                    };
                };
            };
            auto l2 = level1(10);
            auto l3 = l2(20);
            print(l3(30));
       )",
                 "60");
}
