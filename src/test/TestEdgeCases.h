#pragma once
#include "TestRunner.h"
#include <string>

// =========================================================
// 10. 边界情况与回归测试 (Edge Cases & Regressions)
// =========================================================

inline void registerEdgeCases(TestRunner &runner) {
  runner.addTest("Edge - Empty Structures",
                 R"(
            list<any> emptyList = [];
            map<string, any> emptyMap = {};
            print(emptyList.length);
            print(emptyMap.size);
       )",
                 "0\n0");

  runner.addTest("Edge - Single Element",
                 R"(
            list<int> l = [42];
            print(l[0]);
            print(l.length);
            print(l.pop());
            print(l.length);
       )",
                 "42\n1\n42\n0");

  runner.addTest("Edge - Deep Nesting",
                 R"(
            map<string, any> m = {};
            m["a"] = {};
            m["a"]["b"] = {};
            m["a"]["b"]["c"] = 42;
            print(m["a"]["b"]["c"]);
       )",
                 "42");

  runner.addTest("Edge - Large Loop",
                 R"(
            int sum = 0;
            for (int i = 0; i < 1000; i = i + 1) {
                sum = sum + 1;
            }
            print(sum);
       )",
                 "1000");

  runner.addTest("Edge - Many Function Calls",
                 R"(
            int identity(int x) { return x; }
            int result = identity(identity(identity(identity(identity(42)))));
            print(result);
       )",
                 "42");

  runner.addTest("Edge - String Edge Cases",
                 R"(
            string empty = "";
            print(empty.length);
            print(empty.toUpper());
            string single = "x";
            print(single.length);
            print(single.toUpper());
       )",
                 "0\n\n1\nX");

  runner.addTest("Edge - Boolean as Condition",
                 R"(
            bool flag = true;
            if (flag) { print("yes"); }
            flag = false;
            if (flag) { print("no"); } else { print("else"); }
       )",
                 "yes\nelse");

  runner.addTest("Edge - Null Handling",
                 R"(
            var x = null;
            if (x) { print("truthy"); } else { print("falsy"); }
            int y = 1;
            if (y) { print("truthy"); } else { print("falsy"); }
            string z = "a";
            if (z) { print("truthy"); } else { print("falsy"); }
       )",
                 "falsy\ntruthy\ntruthy");

  runner.addTest("Edge - Numeric Limits",
                 R"(
            int big = 1000000000;
            print(big * 2);
            int neg = -1000000000;
            print(neg * 2);
       )",
                 "2000000000\n-2000000000");

  runner.addTest("Edge - Mixed Expressions",
                 R"(
            int a = 5;
            int b = 3;
            print((a + b) * (a - b));
            print(a * b + a / b);
            print((a > b) && (b > 0));
            print(10 / 4);
            print(10.0 / 4);
       )",
                 "16\n16\ntrue\n2\n2.5");

  // 回归测试
  std::string multiVarScript = "vars ";
  for (int i = 0; i < 200; ++i) {
    if (i > 0)
      multiVarScript += ", ";
    multiVarScript += "v" + std::to_string(i);
  }
  multiVarScript += " = 0;\nprint(v0);";
  runner.addTest("Regression - Multi-Var Declaration (Bug #9)", multiVarScript, "0");

  std::string hugeModuleBody = R"(
      export var data = {};
      for (int i = 0; i < 2000; i = i + 1) {
          data["key_" .. i] = "value_" .. i;
      }
  )";
  runner.addModuleTest("Regression - Module GC Safety", {{"stress_module", hugeModuleBody}},
                       R"(
          import * as s from "stress_module";
          print("OK");
      )",
                       "OK");
}
