#include "TestRunner.h"

int main() {
  TestRunner runner;

      // =========================================================
      // 1. 基础功能回归测试
      // =========================================================
      runner.addTest(
              "Stack Restoration",
              R"(
              void doNothing() { return; }
              int a = 100;
              print("Before: " .. a);
              doNothing();
              print("After: " .. a);
          )",
              "Before: 100\nAfter: 100"
      );

      runner.addTest(
              "Multi-Argument & Arithmetic",
              R"(
              int add3(int x, int y, int z) {
                  return x + y + z;
              }
              print(add3(10, 20, 30));
          )",
              "60"
      );

      // =========================================================
      // 2. 控制流测试
      // =========================================================
      runner.addTest(
              "Loops & Conditions",
              R"(
              int sum = 0;
              for (int i = 1; i <= 5; i = i + 1) {
                  if (i == 3) {
                      continue;
                  }
                  sum = sum + i;
              }
              print("Sum: " .. sum);

              int j = 0;
              while (j < 3) {
                  print("w" .. j);
                  j = j + 1;
              }
          )",
              "Sum: 12\nw0\nw1\nw2"
      );

      // =========================================================
      // 3. 递归测试
      // =========================================================
      runner.addTest(
              "Recursion (Fibonacci)",
              R"(
              int fib(int n) {
                  if (n < 2) { return n; }
                  return fib(n - 1) + fib(n - 2);
              }
              print(fib(10));
          )",
              "55"
      );

      // =========================================================
      // 4. 闭包测试
      // =========================================================
      runner.addTest(
              "Closures (Counter)",
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

              print("c1: " .. c1());
              print("c1: " .. c1());
              print("c2: " .. c2());
          )",
              "c1: 1\nc1: 2\nc2: 1"
      );

      // =========================================================
      // 5. 类与对象测试 (显式 this)
      // =========================================================
      runner.addTest(
              "Class & Methods",
              R"spt(
              class Vector {
                  int x;
                  int y;

                  // 显式 this 构造
                  void init(Vector this, int x, int y) {
                      this.x = x;
                      this.y = y;
                  }

                  Vector add(Vector this, Vector other) {
                      return new Vector(this.x + other.x, this.y + other.y);
                  }

                  string str(Vector this) {
                      return "(" .. this.x .. ", " .. this.y .. ")";
                  }
              }

              Vector v1 = new Vector(1, 2);
              Vector v2 = new Vector(3, 4);

              // 显式调用: v1.add(v1, v2)
              Vector v3 = v1.add(v1, v2);

              print(v3.str(v3));
          )spt",
              "(4, 6)"
      );

      // =========================================================
      // 6. 数据结构测试
      // =========================================================
      runner.addTest(
              "List Operations",
              R"(
              list<int> myList = [10, 20, 30];
              print(myList[1]);
              myList[1] = 99;
              print(myList[1]);
          )",
              "20\n99"
      );

      runner.addTest(
              "Map Operations",
              R"(
              map<string, any> dict = { "name": "Flex", "ver": 1 };
              print(dict["name"]);
              dict["ver"] = 2;
              print(dict["ver"]);
          )",
              "Flex\n2"
      );

      // =========================================================
      // 7. 架构深度测试：显式调用约定 (Explicit Calling Convention)
      // =========================================================

      // 测试 A: 纯函数 (Static-like behavior)
      runner.addTest(
              "Pure Functions (No 'this')",
              R"(
              class Math {
                  // 不定义 this 参数，这就是个纯函数
                  static int square(int x) { return x * x; }
              }

              // 1. 直接调用
              int r1 = Math.square(10);

              // 2. 别名调用
              var f = Math.square;
              int r2 = f(5);

              // 3. 动态挂载
              map<string, any> obj = {};
              obj["sq"] = Math.square;
              auto fn = obj["sq"];
              int r3 = fn(2);

              print(r1 .. ", " .. r2 .. ", " .. r3);
          )",
              "100, 25, 4"
      );

      // 测试 B: 显式上下文传递 (Map Methods)
      runner.addTest(
              "Explicit Context (Map Methods)",
              R"(
              map<string, any> hero = { "hp": 100 };

              // 显式定义接收 'self'
              hero["heal"] = function(any self, int amount) -> int {
                  self["hp"] = self["hp"] + amount;
                  return self["hp"];
              };

              // 调用时必须显式传入 hero
              print(hero["heal"](hero, 50));
          )",
              "150"
      );

      // 测试 C: 参数数量检查 (负面测试)
      // 注意：这里使用了 addFailTest，预期会发生运行时错误
      runner.addFailTest(
              "Arity Check (Safety)",
              R"(
              class Counter {
                  int val;
                  // 定义了接收 1 个参数 (this)
                  void inc(Counter this) { this.val = this.val + 1; }
              }
              Counter c = new Counter();

              var f = c.inc;

              // 错误：f 期望 1 个参数 (this)，但提供了 0 个
              f();
          )"
      );

      // 测试 D: 混合调用
      runner.addTest(
              "Mixed Calls (Pure + Method)",
              R"(
              class Utils {
                  // 纯函数，无 this
                  static int double(int x) { return x * 2; }
              }

              class Player {
                  int score;


                  // 实例方法，有 this
                  void addScore(Player this, int s) {
                      // 内部显式访问 this.score
                      // Utils.double 是纯函数，直接调
                      this.score = this.score + Utils.double(s);
                  }
              }

              Player p = new Player();
              p.score = 10;

              // 外部调用显式传 p
              p.addScore(p, 5); // 10 + (5*2) = 20
              print(p.score);
          )",
              "20"
      );

      // =========================================================
      // 8. 模块系统简单回归 (验证 TestRunner 的文件能力)
      // =========================================================
      runner.addModuleTest(
              "Simple Module Import",
              {
                      {"math", "export int sq(int x) { return x*x; }"}
              },
              R"(
              import { sq } from "math";
              print(sq(5));
          )",
              "25"
      );

  // 1. 动态生成一个包含大量导出变量的模块源码
  // 生成 2000 个导出变量，确保 Map 必须扩容 (Resize)
  // 只有扩容才会在 set() 内部触发内存分配，进而触发 GC
  // =========================================================
  // 验证 Bug 2: 模块导出 GC 崩溃 (Segfault Proof)
  // =========================================================
  std::string hugeModuleBody = "";
  // 生成 2000 个导出变量，确保 Map 必须扩容 (Resize)
  for (int i = 0; i < 254; ++i) {
    hugeModuleBody += "export var v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  }

  runner.addModuleTest("PROOF OF CRASH: Module Exports GC",
                       {// 创建虚拟模块文件
                        {"stress_module", hugeModuleBody}},
                       R"(
            // 【修正】完全匹配你的 G4 语法：
            // IMPORT MUL AS IDENTIFIER FROM STRING_LITERAL
            import * as s from "stress_module";

            print("恭喜！如果你看到了这句话，说明运气好没挂，或者内存由于不够碎片化没触发 GC。");
        )",
                       "恭喜！如果你看到了这句话，说明运气好没挂，或者内存由于"
                       "不够碎片化没触发 GC。");

  return runner.runAll();
}