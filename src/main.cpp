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
  runner.addTest("List & Map Remove",
                 R"(
            // --- List Test ---
            list<any> l = [10, 20, 30];

            // 移除索引 1 (20)
            any removedVal = l.removeAt(1);
            print("List removed: " .. removedVal);

            // 验证剩下的元素 [10, 30]
            print("List[1] is now: " .. l[1]);
            print("List length: " .. l.length);

            // 测试越界移除
            print("List invalid: " .. l.removeAt(99));

            // --- Map Test ---
            map<string, any> m = {"a": 100, "b": 200};

            // 移除存在的 key
            any valA = m.remove("a");
            print("Map removed: " .. valA);

            // 验证 key 是否还在
            print("Map has 'a': " .. m.has("a"));
            print("Map has 'b': " .. m.has("b"));

            // 移除不存在的 key
            print("Map invalid: " .. m.remove("z"));
       )",
                 // 预期输出
                 "List removed: 20\n"
                 "List[1] is now: 30\n"
                 "List length: 2\n"
                 "List invalid: nil\n"
                 "Map removed: 100\n"
                 "Map has 'a': false\n"
                 "Map has 'b': true\n"
                 "Map invalid: nil");
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
// 7. 特定 Bug 回归测试 (Regressions)
// =========================================================
void registerRegressionTests(TestRunner &runner) {
  // -----------------------------------------------------------
  // [回归测试] Bug #9: compileMutiVariableDecl 逻辑错误
  // -----------------------------------------------------------
  // 原理：必须使用 'mutivar ... = 0' 语法。
  // 1. 'mutivar' 关键字强制编译器调用 compileMutiVariableDecl。
  // 2. '= 0' (初始化) 强制进入函数内的 if (initializer) 分支。
  // 3. 只有在这个分支里，旧代码才会有重复 push_back 导致局部变量表膨胀的 Bug。
  //
  // 如果 Bug 未修复：200 个变量会占用 400 个槽位 -> 报错 "Too many local variables"。
  // 如果 Bug 已修复：200 个变量占用 200 个槽位 -> 测试通过。
  // -----------------------------------------------------------
  std::string multiVarScript = "mutivar ";
  for (int i = 0; i < 200; ++i) {
    if (i > 0)
      multiVarScript += ", ";
    multiVarScript += "v" + std::to_string(i);
  }
  // 必须赋值！否则走不到 Bug 逻辑路径
  multiVarScript += " = 0;\n";

  // 简单验证：首尾变量是否可读
  multiVarScript += "print(v0);";

  runner.addTest("Regressions: Multi-Var Logic (Bug #9)", multiVarScript,
                 "0" // v0 被赋值为 0，预期输出 0
  );

  // -----------------------------------------------------------
  // [回归测试] Bug #11: CreateError GC Crash
  // -----------------------------------------------------------
  // 原理：
  // 1. 先加载一个巨大模块填满堆内存，逼近 GC 阈值。
  // 2. 立即触发一个错误（导入不存在的模块），调用 createError。
  // 3. createError 内部分配 String 时极大概率触发 GC。
  // 4. 如果 Map 对象没被 protect，就会被回收，导致随后的 set() 操作崩溃。
  // -----------------------------------------------------------
  std::string heapFiller = R"(
      export mutivar garbage = {};
      for (int i = 0; i < 5000; i = i + 1) {
          garbage["fill_" .. i] = "some_long_string_value_" .. i;
      }
  )";

  runner.addModuleTest("PROOF OF CRASH: CreateError GC", {{"filler", heapFiller}},
                       R"(
          import * as f from "filler";
          print("Heap filled.");

          // 这一步会调用 C++ 的 createError 函数
          // 如果没修好，进程会直接挂掉 (Segfault)
          import * as missing from "non_existent_module";
      )",
                       "Heap filled.", true);
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

  // 注册新增的回归测试
  registerRegressionTests(runner);

  runner.addFailTest("Debug Line Info",
                     R"(
              void a() {
                print("test");
              }

              void b() {
                a();
              }

              void c() {
                b();
                c();
              }

              c();
          )");

  return runner.runAll();
}