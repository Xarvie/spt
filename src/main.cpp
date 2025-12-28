#include "TestRunner.h"

int main() {
  TestRunner runner;

  runner.addTest("Stack Restoration",
                 R"(
              void doNothing() { return; }
              int a = 100;
              print("Before: " .. a);
              doNothing();
              print("After: " .. a);
          )",
                 "Before: 100\nAfter: 100");

  runner.addTest("Multi-Argument & Arithmetic",
                 R"(
              int add3(int x, int y, int z) {
                  return x + y + z;
              }
              print(add3(10, 20, 30));
          )",
                 "60");

  runner.addTest("Loops & Conditions",
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
                 "Sum: 12\nw0\nw1\nw2");

  runner.addTest("Recursion (Fibonacci)",
                 R"(
              int fib(int n) {
                  if (n < 2) { return n; }
                  return fib(n - 1) + fib(n - 2);
              }
              print(fib(10));
          )",
                 "55");

  runner.addTest("Closures (Counter)",
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
                 "c1: 1\nc1: 2\nc2: 1");

  runner.addTest("Class & Methods",
                 R"spt(
              class Vector {
                  int x;
                  int y;

                  
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

              
              Vector v3 = v1.add(v1, v2);

              print(v3.str(v3));
          )spt",
                 "(4, 6)");

  runner.addTest("List Operations",
                 R"(
              list<int> myList = [10, 20, 30];
              print(myList[1]);
              myList[1] = 99;
              print(myList[1]);
          )",
                 "20\n99");

  runner.addTest("Map Operations",
                 R"(
              map<string, any> dict = { "name": "Flex", "ver": 1 };
              print(dict["name"]);
              dict["ver"] = 2;
              print(dict["ver"]);
          )",
                 "Flex\n2");

  runner.addTest("Pure Functions (No 'this')",
                 R"(
              class Math {
                  
                  static int square(int x) { return x * x; }
              }

              
              int r1 = Math.square(10);

              
              var f = Math.square;
              int r2 = f(5);

              
              map<string, any> obj = {};
              obj["sq"] = Math.square;
              auto fn = obj["sq"];
              int r3 = fn(2);

              print(r1 .. ", " .. r2 .. ", " .. r3);
          )",
                 "100, 25, 4");

  runner.addTest("Explicit Context (Map Methods)",
                 R"(
              map<string, any> hero = { "hp": 100 };

              
              hero["heal"] = function(any self, int amount) -> int {
                  self["hp"] = self["hp"] + amount;
                  return self["hp"];
              };

              
              print(hero["heal"](hero, 50));
          )",
                 "150");

  runner.addFailTest("Arity Check (Safety)",
                     R"(
              class Counter {
                  int val;
                  void inc(Counter this) { this.val = this.val + 1; }
              }
              Counter c = new Counter();

              var f = c.inc;
              f(1);
          )");

  runner.addTest("Mixed Calls (Pure + Method)",
                 R"(
              class Utils {
                  static int double(int x) { return x * 2; }
              }

              class Player {
                  int score;
                  void addScore(Player this, int s) {
                      this.score = this.score + Utils.double(s);
                  }
              }

              Player p = new Player();
              p.score = 10;

              
              p.addScore(p, 5); 
              print(p.score);
          )",
                 "20");

  runner.addModuleTest("Simple Module Import", {{"math", "export int sq(int x) { return x*x; }"}},
                       R"(
              import { sq } from "math";
              print(sq(5));
          )",
                       "25");

  std::string hugeModuleBody = "";
  for (int i = 0; i < 254; ++i) {
    hugeModuleBody += "export var v" + std::to_string(i) + " = " + std::to_string(i) + ";\n";
  }

  runner.addModuleTest("PROOF OF CRASH: Module Exports GC", {{"stress_module", hugeModuleBody}},
                       R"(
            import * as s from "stress_module";
            print("恭喜！如果你看到了这句话，说明运气好没挂，或者内存由于不够碎片化没触发 GC。");
        )",
                       "恭喜！如果你看到了这句话，说明运气好没挂，或者内存由于"
                       "不够碎片化没触发 GC。");

  return runner.runAll();
}