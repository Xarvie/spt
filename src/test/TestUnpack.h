#pragma once
#include "NativeBindings.h"
#include "TestRunner.h"

// =========================================================
// unpack 函数测试 (Lua-style table.unpack)
// 支持：多变量赋值、函数参数展开、return 展开
// =========================================================

inline void registerUnpackTests(TestRunner &runner) {
  // =========================================================
  // 基本功能测试
  // =========================================================

  runner.addTest("unpack: Basic - Unpack All Elements",
                 R"(
        var arr = [1, 2, 3];
        vars a, b, c = unpack(arr);
        print(a, b, c);
      )",
                 "1 2 3");

  runner.addTest("unpack: Basic - Single Element List",
                 R"(
        var arr = [42];
        vars x = unpack(arr);
        print(x);
      )",
                 "42");

  runner.addTest("unpack: Basic - Two Elements",
                 R"(
        var arr = [100, 200];
        vars a, b = unpack(arr);
        print(a, b);
      )",
                 "100 200");

  // =========================================================
  // 空列表和边界情况
  // =========================================================

  runner.addTest("unpack: Empty List Returns Nothing",
                 R"(
        var arr = [];
        vars a = unpack(arr);
        print(a);
      )",
                 "nil");

  runner.addTest("unpack: More Variables Than Elements",
                 R"(
        var arr = [1, 2];
        vars a, b, c, d = unpack(arr);
        print(a, b, c, d);
      )",
                 "1 2 nil nil");

  runner.addTest("unpack: Fewer Variables Than Elements",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        vars a, b = unpack(arr);
        print(a, b);
      )",
                 "1 2");

  // =========================================================
  // 带起始索引的调用
  // =========================================================

  runner.addTest("unpack: With Start Index",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars a, b, c = unpack(arr, 2);
        print(a, b, c);
      )",
                 "30 40 50");

  runner.addTest("unpack: Start From Index 0",
                 R"(
        var arr = [1, 2, 3];
        vars a, b, c = unpack(arr, 0);
        print(a, b, c);
      )",
                 "1 2 3");

  runner.addTest("unpack: Start From Last Element",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        vars x = unpack(arr, 4);
        print(x);
      )",
                 "5");

  // =========================================================
  // 带起始和结束索引的调用
  // =========================================================

  runner.addTest("unpack: With Start and End Index",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars a, b, c = unpack(arr, 1, 3);
        print(a, b, c);
      )",
                 "20 30 40");

  runner.addTest("unpack: Single Element Range",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars x = unpack(arr, 2, 2);
        print(x);
      )",
                 "30");

  runner.addTest("unpack: Full Range Explicit",
                 R"(
        var arr = [1, 2, 3];
        vars a, b, c = unpack(arr, 0, 2);
        print(a, b, c);
      )",
                 "1 2 3");

  // =========================================================
  // 负索引支持
  // =========================================================

  runner.addTest("unpack: Negative Start Index",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars a, b = unpack(arr, -2);
        print(a, b);
      )",
                 "40 50");

  runner.addTest("unpack: Negative End Index",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars a, b, c, d = unpack(arr, 0, -2);
        print(a, b, c, d);
      )",
                 "10 20 30 40");

  runner.addTest("unpack: Both Negative Indices",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars a, b = unpack(arr, -3, -2);
        print(a, b);
      )",
                 "30 40");

  runner.addTest("unpack: Negative Index -1 Means Last",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        vars x = unpack(arr, -1);
        print(x);
      )",
                 "5");

  // =========================================================
  // 无效范围处理
  // =========================================================

  runner.addTest("unpack: Start Greater Than End",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        vars a = unpack(arr, 3, 1);
        print(a);
      )",
                 "nil");

  runner.addTest("unpack: Out of Bounds Start (Clamped)",
                 R"(
        var arr = [1, 2, 3];
        vars a, b, c = unpack(arr, 10);
        print(a, b, c);
      )",
                 "3 nil nil");

  runner.addTest("unpack: Negative Out of Bounds (Clamped to 0)",
                 R"(
        var arr = [1, 2, 3];
        vars a, b, c = unpack(arr, -100);
        print(a, b, c);
      )",
                 "1 2 3");

  // =========================================================
  // 不同类型元素
  // =========================================================

  runner.addTest("unpack: Mixed Types",
                 R"(
        var arr = [42, "hello", true, 3.14];
        vars a, b, c, d = unpack(arr);
        print(a, b, c, d);
      )",
                 "42 hello true 3.14");

  runner.addTest("unpack: Nested Lists",
                 R"(
        var arr = [[1, 2], [3, 4], [5, 6]];
        vars a, b, c = unpack(arr);
        print(len(a), len(b), len(c));
      )",
                 "2 2 2");

  runner.addTest("unpack: With Nil Elements",
                 R"(
        var arr = [1, nil, 3, nil, 5];
        vars a, b, c, d, e = unpack(arr);
        print(a, b, c, d, e);
      )",
                 "1 nil 3 nil 5");

  // =========================================================
  // 实际使用场景
  // =========================================================

  runner.addTest("unpack: Swap Values",
                 R"(
        var pair = [100, 200];
        vars b, a = unpack(pair);
        print(a, b);
      )",
                 "200 100");

  runner.addTest("unpack: Partial Unpack for Head/Tail",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        vars head = unpack(arr, 0, 0);
        print("head:", head);
        vars t1, t2, t3, t4 = unpack(arr, 1);
        print("tail:", t1, t2, t3, t4);
      )",
                 "head: 1\ntail: 2 3 4 5");

  runner.addTest("unpack: With Closure",
                 R"(
        auto makeMultiplier = function(int factor) -> function {
            return function(int x) -> int {
                return x * factor;
            };
        };
        var funcs = [makeMultiplier(2), makeMultiplier(3)];
        vars f1, f2 = unpack(funcs);
        print(f1(10), f2(10));
      )",
                 "20 30");

  runner.addTest("unpack: Chain Multiple Unpacks",
                 R"(
        var first = [1, 2];
        var second = [3, 4];
        vars a, b = unpack(first);
        vars c, d = unpack(second);
        print(a, b, c, d);
      )",
                 "1 2 3 4");

  // =========================================================
  // 边界值测试
  // =========================================================

  runner.addTest("unpack: Large List Partial",
                 R"(
        var arr = [];
        for (int i = 0; i < 100; i = i + 1) {
            arr.push(i);
        }
        vars a, b, c = unpack(arr, 97);
        print(a, b, c);
      )",
                 "97 98 99");

  runner.addTest("unpack: Index At Boundary",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        vars x = unpack(arr, 4, 4);
        print(x);
        vars y = unpack(arr, 0, 0);
        print(y);
      )",
                 "5\n1");

  runner.addTest("unpack: First And Last Only",
                 R"(
        var arr = [10, 20, 30, 40, 50];
        vars first = unpack(arr, 0, 0);
        vars last = unpack(arr, -1, -1);
        print(first, last);
      )",
                 "10 50");

  runner.addTest("unpack: Middle Elements",
                 R"(
        var arr = [1, 2, 3, 4, 5, 6, 7];
        vars a, b, c = unpack(arr, 2, 4);
        print(a, b, c);
      )",
                 "3 4 5");

  // =========================================================
  // 复合场景
  // =========================================================

  runner.addTest("unpack: Nested Unpack Calls",
                 R"(
        int sum2(int a, int b) {
            return a + b;
        }
        var outer = [[1, 2]];
        vars inner = unpack(outer);
        print(sum2(unpack(inner)));
      )",
                 "3");

  runner.addTest("unpack: Return with Spread",
                 R"(
        vars getNumbers() {
            var nums = [10, 20, 30];
            return unpack(nums);
        }
        vars a, b, c = getNumbers();
        print(a, b, c);
      )",
                 "10 20 30");

  // =========================================================
  // 普通函数参数展开 (Script Function Spread)
  // =========================================================

  runner.addTest("unpack: Script Function - Only Spread",
                 R"(
        int sum3(int a, int b, int c) {
            return a + b + c;
        }
        var args = [1, 2, 3];
        print(sum3(unpack(args)));
      )",
                 "6");

  runner.addTest("unpack: Script Function - Fixed Then Spread",
                 R"(
        int sum4(int a, int b, int c, int d) {
            return a + b + c + d;
        }
        var rest = [3, 4];
        print(sum4(1, 2, unpack(rest)));
      )",
                 "10");

  runner.addTest("unpack: Script Function - Empty Spread",
                 R"(
        void noArgs() {
            print("ok");
        }
        var empty = [];
        noArgs(unpack(empty));
      )",
                 "ok");

  // =========================================================
  // 普通方法参数展开 (Script Method Spread)
  // =========================================================

  runner.addTest("unpack: Script Method - Only Spread",
                 R"(
        class Calc {
          int d;
          void init(Calc this, int a) { this.d = a;}
            int add3(Calc this, int a, int b, int c) {
                return a + b + c + this.d;
            }
        }
        var c = new Calc(1);
        var args = [10, 20, 30];
        print(c.add3(unpack(args)));
      )",
                 "61");

  runner.addTest("unpack: Script Method - Fixed Then Spread",
                 R"(
        vars returnTwo() { return 10, 20; }
        class Calc {
            int add4(Calc this, int a, int b, int c, int d) {
                return a + b + c + d;
            }
        }
        var c = new Calc();
        var rest = [30, 40];
        print(c.add4(10, 20, unpack(rest)));
      )",
                 "100");

  runner.addTest("unpack: Script Method - Empty Spread",
                 R"(
        class Obj {
            void run(Obj this) {
                print("run");
            }
        }
        var o = new Obj();
        var empty = [];
        o.run(unpack(empty));
      )",
                 "run");

  // =========================================================
  // 原生方法参数展开 (Native Method Spread)
  // 如 string.slice, list.slice 等内置方法
  // =========================================================

  runner.addTest("unpack: Native Method - Only Spread",
                 R"(
        var s = "Hello World";
        var args = [0, 5];
        print(s.slice(unpack(args)));
      )",
                 "Hello");

  runner.addTest("unpack: Native Method - Fixed Then Spread",
                 R"(
        var s = "Hello World";
        var rest = [5];
        print(s.slice(0, unpack(rest)));
      )",
                 "Hello");

  runner.addTest("unpack: Native Method - List Method",
                 R"(
        var arr = [1, 2, 3, 4, 5];
        var args = [1, 3];
        var sub = arr.slice(unpack(args));
        print(sub[0], sub[1]);
      )",
                 "2 3");

  // =========================================================
  // 原生函数参数展开 (Native Function Spread)
  // 如 print, len, toString 等内置函数
  // =========================================================

  runner.addTest("unpack: Native Function - print",
                 R"(
        var args = ["a", "b", "c"];
        print(unpack(args));
      )",
                 "a b c");

  runner.addTest("unpack: Native Function - Fixed Then Spread",
                 R"(
        var rest = [2, 3];
        print(1, unpack(rest));
      )",
                 "1 2 3");

  runner.addTest("unpack: Native Function - Empty Spread",
                 R"(
        var empty = [];
        print("test", unpack(empty));
      )",
                 "test");
}