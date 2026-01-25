#pragma once
#include "NativeBindings.h"
#include "TestRunner.h"

// =========================================================
// 多返回值边界测试 (Multi-Return Value Edge Cases)
// =========================================================

inline void registerMultiReturnTests(TestRunner &runner) {

  // ---------------------------------------------------------
  // 基础多返回值测试
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Basic vars declaration",
                 R"(
            vars returnTwo() {
                return 1, 2;
            }
            vars a, b = returnTwo();
            print(a);
            print(b);
       )",
                 "1\n2");

  runner.addTest("MultiReturn - Three values",
                 R"(
            vars returnThree() {
                return 10, 20, 30;
            }
            vars x, y, z = returnThree();
            print(x);
            print(y);
            print(z);
       )",
                 "10\n20\n30");

  runner.addTest("MultiReturn - Mixed types",
                 R"(
            vars returnMixed() {
                return 42, "hello", true;
            }
            vars a, b, c = returnMixed();
            print(a);
            print(b);
            print(c);
       )",
                 "42\nhello\ntrue");

  // ---------------------------------------------------------
  // print 直接使用多返回值
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Print direct call",
                 R"(
            vars getTwoValues() {
                return 100, 200;
            }
            print(getTwoValues());
       )",
                 "100 200");

  runner.addTest("MultiReturn - Print three values",
                 R"(
            vars getThree() {
                return "a", "b", "c";
            }
            print(getThree());
       )",
                 "a b c");

  // ---------------------------------------------------------
  // 方法调用返回多值
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Class method returns two",
                 R"(
            class Pair {
                int x;
                int y;
                void init(Pair this, int a, int b) {
                    this.x = a;
                    this.y = b;
                }
                vars getBoth(Pair this) {
                    return this.x, this.y;
                }
            }
            Pair p = new Pair(5, 10);
            print(p.getBoth());
       )",
                 "5 10");

  runner.addTest("MultiReturn - Class method to vars",
                 R"(
            class Point {
                int x;
                int y;
                void init(Point this, int a, int b) {
                    this.x = a;
                    this.y = b;
                }
                vars coords(Point this) {
                    return this.x, this.y;
                }
            }
            Point p = new Point(3, 7);
            vars a, b = p.coords();
            print(a + b);
       )",
                 "10");

  // ---------------------------------------------------------
  // 变量数量不匹配的边界情况
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - More vars than values",
                 R"(
            vars returnOne() {
                return 42;
            }
            vars a, b, c = returnOne();
            print(a);
            print(b == null);
            print(c == null);
       )",
                 "42\ntrue\ntrue");

  runner.addTest("MultiReturn - Less vars than values",
                 R"(
            vars returnThree() {
                return 1, 2, 3;
            }
            vars a = returnThree();
            print(a);
       )",
                 "1");

  runner.addTest("MultiReturn - Two vars three values",
                 R"(
            vars returnThree() {
                return 10, 20, 30;
            }
            vars x, y = returnThree();
            print(x);
            print(y);
       )",
                 "10\n20");

  // ---------------------------------------------------------
  // 嵌套调用 - 多返回值作为函数参数
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Nested function call",
                 R"(
            vars inner() {
                return 5, 10;
            }
            int outer(int a, int b) {
                return a + b;
            }
            print(outer(inner()));
       )",
                 "15");

  runner.addTest("MultiReturn - Triple nested",
                 R"(
            vars getTwo() {
                return 2, 3;
            }
            int add(int a, int b) {
                return a + b;
            }
            int mul(int x) {
                return x * 10;
            }
            print(mul(add(getTwo())));
       )",
                 "50");

  runner.addTest("MultiReturn - As middle arguments",
                 R"(
            vars getArgs() {
                return 2, 4;
            }
            int compute(int prefix, int a, int suffix) {
                return prefix + a + suffix;
            }
            print(compute(1, getArgs(), 3));
       )",
                 "6");

  runner.addTest("MultiReturn - Only last arg expands",
                 R"(
            vars getTwo() {
                return 100, 200;
            }
            void show(int a, int b, int c) {
                print(a .. " " .. b .. " " .. c);
            }
            show(1, getTwo());
       )",
                 "1 100 200");

  // ---------------------------------------------------------
  // 与其他特性交互
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - In conditional",
                 R"(
            vars getValues(bool flag) {
                if (flag) {
                    return 1, 2;
                } else {
                    return 3, 4;
                }
            }
            vars a, b = getValues(true);
            vars c, d = getValues(false);
            print(a .. "," .. b);
            print(c .. "," .. d);
       )",
                 "1,2\n3,4");

  runner.addTest("MultiReturn - From closure",
                 R"(
            auto makeGetter = function(int x, int y) -> function {
                return function() -> vars {
                    return x, y;
                };
            };
            auto getter = makeGetter(100, 200);
            print(getter());
       )",
                 "100 200");

  runner.addTest("MultiReturn - In loop",
                 R"(
            vars nextPair(int n) {
                return n, n + 1;
            }
            int sum = 0;
            for (int i = 0; i < 3; i = i + 1) {
                vars a, b = nextPair(i);
                sum = sum + a + b;
            }
            print(sum);
       )",
                 "9");

  // ---------------------------------------------------------
  // 单值函数与多变量接收
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Single value function to multiple vars",
                 R"(
            int single() {
                return 42;
            }
            vars a, b = single();
            print(a);
            print(b == null);
       )",
                 "42\ntrue");

  runner.addTest("MultiReturn - Void function to var",
                 R"(
            void nothing() {
                return;
            }
            vars a = nothing();
            print(a == null);
       )",
                 "true");

  // ---------------------------------------------------------
  // 特殊值测试
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - With nil values",
                 R"(
            vars withNil() {
                return 1, null, 3;
            }
            vars a, b, c = withNil();
            print(a);
            print(b == null);
            print(c);
       )",
                 "1\ntrue\n3");

  runner.addTest("MultiReturn - All nil",
                 R"(
            vars allNil() {
                return null, null;
            }
            vars a, b = allNil();
            print(a == null);
            print(b == null);
       )",
                 "true\ntrue");

  runner.addTest("MultiReturn - Boolean values",
                 R"(
            vars getBools() {
                return true, false, true;
            }
            vars a, b, c = getBools();
            print(a);
            print(b);
            print(c);
       )",
                 "true\nfalse\ntrue");

  runner.addTest("MultiReturn - Float values",
                 R"(
            vars getFloats() {
                return 1.5, 2.5, 3.5;
            }
            vars a, b, c = getFloats();
            print(a + b + c);
       )",
                 "7.5");

  // ---------------------------------------------------------
  // 链式与递归
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Chained calls",
                 R"(
            vars first() {
                return 1, 2;
            }
            vars second(int a, int b) {
                return a * 10, b * 10;
            }
            print(second(first()));
       )",
                 "10 20");

  runner.addTest("MultiReturn - Recursive with multi values",
                 R"(
            vars fib(int n) {
                if (n <= 1) {
                    return 0, 1;
                }
                vars a, b = fib(n - 1);
                return b, a + b;
            }
            vars x, y = fib(10);
            print(x);
            print(y);
       )",
                 "34\n55");

  // ---------------------------------------------------------
  // 容器交互
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - To list push",
                 R"(
            vars getTwoStrings() {
                return "hello", "world";
            }
            list<string> items = [];
            vars a, b = getTwoStrings();
            items.push(a);
            items.push(b);
            print(items.join(" "));
       )",
                 "hello world");

  runner.addTest("MultiReturn - With map",
                 R"(
            vars getKeyValue() {
                return "name", "Alice";
            }
            map<string, string> m = {};
            vars k, v = getKeyValue();
            m[k] = v;
            print(m["name"]);
       )",
                 "Alice");

  // ---------------------------------------------------------
  // 边界：大量返回值
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - Five values",
                 R"(
            vars getFive() {
                return 1, 2, 3, 4, 5;
            }
            vars a, b, c, d, e = getFive();
            print(a + b + c + d + e);
       )",
                 "15");

  runner.addTest("MultiReturn - Partial capture of five",
                 R"(
            vars getFive() {
                return 10, 20, 30, 40, 50;
            }
            vars x, y = getFive();
            print(x);
            print(y);
       )",
                 "10\n20");

  // ---------------------------------------------------------
  // 与 pcall 交互
  // ---------------------------------------------------------

  runner.addTest("MultiReturn - pcall success",
                 R"(
            vars mayFail(bool fail) {
                if (fail) {
                    error("oops");
                }
                return 1, 2, 3;
            }
            vars ok, a, b, c = pcall(mayFail, false);
            print(ok);
            print(a);
            print(b);
            print(c);
       )",
                 "true\n1\n2\n3");

  runner.addTest("MultiReturn - pcall failure",
                 R"(
            vars mayFail(bool fail) {
                if (fail) {
                    error("oops");
                }
                return 1, 2;
            }
            vars ok, err = pcall(mayFail, true);
            print(ok);
            print(err);
       )",
                 "false\noops");
}

// ---------------------------------------------------------
// 原生类多返回值测试
// ---------------------------------------------------------

inline void registerNativeMultiReturnTests(TestRunner &runner) {
  //
  //  // Vector3.xyz() 返回三个值
  //  runner.addNativeTest("Native MultiReturn - Vector3 xyz to vars",
  //                       R"(
  //        auto v = Vector3(1.0, 2.0, 3.0);
  //        vars x, y, z = v.xyz();
  //        print(x);
  //        print(y);
  //        print(z);
  //      )",
  //                       "1\n2\n3", registerVector3);
  //
  //  runner.addNativeTest("Native MultiReturn - Vector3 xyz direct print",
  //                       R"(
  //        auto v = Vector3(10.0, 20.0, 30.0);
  //        print(v.xyz());
  //      )",
  //                       "10 20 30", registerVector3);
  //
  //  // Vector3.xy() 返回两个值
  //  runner.addNativeTest("Native MultiReturn - Vector3 xy",
  //                       R"(
  //        auto v = Vector3(5.0, 10.0, 15.0);
  //        vars a, b = v.xy();
  //        print(a);
  //        print(b);
  //      )",
  //                       "5\n10", registerVector3);
  //
  //  runner.addNativeTest("Native MultiReturn - Vector3 xy to function",
  //                       R"(
  //        auto v = Vector3(3.0, 4.0, 0.0);
  //        float sum(float a, float b) {
  //            return a + b;
  //        }
  //        print(sum(v.xy()));
  //      )",
  //                       "7", registerVector3);
  //
  //  // 部分捕获
  //  runner.addNativeTest("Native MultiReturn - Vector3 xyz partial capture",
  //                       R"(
  //        auto v = Vector3(1.0, 2.0, 3.0);
  //        vars x, y = v.xyz();
  //        print(x);
  //        print(y);
  //      )",
  //                       "1\n2", registerVector3);
  //
  //  // 多变量超过返回值数量
  //  runner.addNativeTest("Native MultiReturn - Vector3 xyz more vars",
  //                       R"(
  //        auto v = Vector3(1.0, 2.0, 3.0);
  //        vars a, b, c, d = v.xyz();
  //        print(a);
  //        print(b);
  //        print(c);
  //        print(d == null);
  //      )",
  //                       "1\n2\n3\ntrue", registerVector3);
  //
  //  // 多个 Vector3 的 xyz
  //  runner.addNativeTest("Native MultiReturn - Multiple Vector3 xyz",
  //                       R"(
  //        auto v1 = Vector3(1.0, 2.0, 3.0);
  //        auto v2 = Vector3(4.0, 5.0, 6.0);
  //        vars x1, y1, z1 = v1.xyz();
  //        vars x2, y2, z2 = v2.xyz();
  //        print(x1 + x2);
  //        print(y1 + y2);
  //        print(z1 + z2);
  //      )",
  //                       "5\n7\n9", registerVector3);
  //
  //  // xyz 作为函数参数
  //  runner.addNativeTest("Native MultiReturn - Vector3 xyz as function args",
  //                       R"(
  //        auto v = Vector3(10.0, 20.0, 30.0);
  //        int compute(float a, float b, float c) {
  //            return toInt(a + b + c);
  //        }
  //        print(compute(v.xyz()));
  //      )",
  //                       "60", registerVector3);
  //
  //  // Counter.state() 返回两个值
  //  runner.addNativeTest("Native MultiReturn - Counter state",
  //                       R"(
  //        auto c = Counter(100, 5);
  //        vars val, step = c.state();
  //        print(val);
  //        print(step);
  //      )",
  //                       "100\n5", registerCounter);
  //
  //  runner.addNativeTest("Native MultiReturn - Counter state after ops",
  //                       R"(
  //        auto c = Counter(0, 10);
  //        c.increment();
  //        c.increment();
  //        vars val, step = c.state();
  //        print(val);
  //        print(step);
  //      )",
  //                       "20\n10", registerCounter);
  //
  //  runner.addNativeTest("Native MultiReturn - Counter state direct print",
  //                       R"(
  //        auto c = Counter(42, 7);
  //        print(c.state());
  //      )",
  //                       "42 7", registerCounter);
  //
  //  // 混合原生类多返回值
  //  runner.addNativeTest("Native MultiReturn - Mixed native classes",
  //                       R"(
  //        auto v = Vector3(1.0, 2.0, 3.0);
  //        auto c = Counter(100, 1);
  //
  //        vars x, y, z = v.xyz();
  //        vars val, step = c.state();
  //
  //        print(toInt(x) + toInt(val));
  //        print(toInt(y) + step);
  //      )",
  //                       "101\n3", registerAllNativeBindings);
  //
  //  // 循环中使用
  //  runner.addNativeTest("Native MultiReturn - In loop",
  //                       R"(
  //        float sumX = 0.0;
  //        float sumY = 0.0;
  //        for (int i = 1; i <= 3; i = i + 1) {
  //            auto v = Vector3(toFloat(i), toFloat(i * 2), 0.0);
  //            vars x, y = v.xy();
  //            sumX = sumX + x;
  //            sumY = sumY + y;
  //        }
  //        print(toInt(sumX));
  //        print(toInt(sumY));
  //      )",
  //                       "6\n12", registerVector3);
  //
  //  // 嵌套调用
  //  runner.addNativeTest("Native MultiReturn - Nested with script function",
  //                       R"(
  //        auto v = Vector3(2.0, 3.0, 4.0);
  //
  //        int multiply(float a, float b, float c) {
  //            return toInt(a * b * c);
  //        }
  //
  //        print(multiply(v.xyz()));
  //      )",
  //                       "24", registerVector3);
}
