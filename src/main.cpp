#include "TestRunner.h"

// =========================================================
// 1. 基础语法与运算 (Basics)
// =========================================================
void registerBasics(TestRunner &runner) {
  runner.addTest("Stack & Arithmetic",
                 R"(
            int a = 10;
            int b = 20;
            print(a + b * 2);
            print((a + b) * 2);
       )",
                 "50\n60");

  runner.addTest("Logic Short-Circuit",
                 R"(
            bool t = true;
            bool f = false;

            // 验证短路: 如果没有短路，1/0 会触发除零错误
            if (t || (1/0 == 0)) { print("OR Short-circuit OK"); }
            if (f && (1/0 == 0)) { print("Fail"); } else { print("AND Short-circuit OK"); }
       )",
                 "OR Short-circuit OK\nAND Short-circuit OK");

  runner.addTest("Variable Shadowing",
                 R"(
            int a = 100;
            {
                int a = 200;
                print(a);
                {
                    int a = 300;
                    print(a);
                }
                print(a);
            }
            print(a);
       )",
                 "200\n300\n200\n100");
}

// =========================================================
// 2. 控制流 (Control Flow)
// =========================================================
void registerControlFlow(TestRunner &runner) {
  runner.addTest("Loops & Recursion",
                 R"(
            int sum = 0;
            for (int i = 0; i < 5; i = i + 1) {
                // [修正] 必须加 {}
                if (i == 2) { continue; }
                sum = sum + i;
            }
            print(sum);

            int fib(int n) {
                // [修正] 必须加 {}
                if (n < 2) { return n; }
                return fib(n-1) + fib(n-2);
            }
            print(fib(10));
       )",
                 "8\n55");

  runner.addTest("Nested Break/Continue",
                 R"(
            int i = 0;
            while (i < 3) {
                int j = 0;
                while (j < 3) {
                    if (j == 1) {
                        j = j + 1;
                        continue;
                    }
                    if (i == 1) {
                        break;
                    }
                    print(i .. "-" .. j);
                    j = j + 1;
                }
                i = i + 1;
            }
       )",
                 "0-0\n0-2\n2-0\n2-2");
}

// =========================================================
// 3. 函数与闭包 (Functions)
// =========================================================
void registerFunctions(TestRunner &runner) {
  runner.addTest("Closure & Upvalues",
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
       )",
                 "1\n2");

  runner.addTest("Deep Closure Shared State",
                 R"(
            var setter;
            var getter;

            {
                int x = 10;
                // [修正] 必须显式写出 -> void
                setter = function(int v) -> void { x = v; };
                getter = function() -> int { return x; };
            }

            print(getter());
            setter(42);
            print(getter());
       )",
                 "10\n42");
}

// =========================================================
// 4. 类与对象 (Classes)
// =========================================================
void registerClasses(TestRunner &runner) {
  runner.addTest("Class Methods & Fields",
                 R"spt(
            class Point {
                int x;
                int y;
                void init(Point this, int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                void move(Point this, int dx, int dy) {
                    this.x = this.x + dx;
                    this.y = this.y + dy;
                }
            }
            Point p = new Point(10, 20);
            p.move(p, 5, 5);
            print(p.x .. ", " .. p.y);
       )spt",
                 "15, 25");

  runner.addTest("Circular Reference Safety",
                 R"(
            class Node {
                any next;
            }
            Node a = new Node();
            Node b = new Node();

            a.next = b;
            b.next = a;

            print("Cycle created");
       )",
                 "Cycle created");
}

// =========================================================
// 5. 数据结构 (List/Map)
// =========================================================
void registerDataStructs(TestRunner &runner) {
  runner.addTest("List & Map Basic",
                 R"(
            list<int> l = [1, 2];
            l[0] = 100;
            print(l[0]);

            map<string, int> m = {"a": 1};
            m["b"] = 2;
            print(m["b"]);
       )",
                 "100\n2");

  runner.addTest("Empty Structs & Mixed Types",
                 R"(
            list<any> emptyList = [];
            print("List size: " .. 0);

            map<string, any> complex = {};
            complex["int"] = 1;
            complex["str"] = "hello";
            complex["list"] = [1, 2];

            print(complex["str"]);
       )",
                 "List size: 0\nhello");
}

// =========================================================
// 6. 模块系统 (Modules)
// =========================================================
void registerModules(TestRunner &runner) {
  runner.addModuleTest("Simple Import", {{"math", "export int sq(int x) { return x*x; }"}},
                       R"(
            import { sq } from "math";
            print(sq(5));
        )",
                       "25");

  std::string hugeModuleBody = R"(
        export var data = {};
        for (int i = 0; i < 2000; i = i + 1) {
            data["key_" .. i] = "value_" .. i;
        }
    )";

  runner.addModuleTest("Regressions: Module GC Safety", {{"stress_module", hugeModuleBody}},
                       R"(
            import * as s from "stress_module";
            print("Module loaded safely");
        )",
                       "Module loaded safely");
}

// =========================================================
// 主函数
// =========================================================
int main() {
  TestRunner runner;

  registerBasics(runner);
  registerControlFlow(runner);
  registerFunctions(runner);
  registerClasses(runner);
  registerDataStructs(runner);
  registerModules(runner);

  return runner.runAll();
}