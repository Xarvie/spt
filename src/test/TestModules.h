#pragma once
#include "TestRunner.h"

// =========================================================
// 8. 模块系统 (Modules)
// =========================================================

inline void registerModules(TestRunner &runner) {
  runner.addModuleTest("Import Named", {{"math", R"(
                export int square(int x) { return x * x; }
                export int cube(int x) { return x * x * x; }
            )"}},
                       R"(
            import { square, cube } from "math";
            print(square(5));
            print(cube(3));
        )",
                       "25\n27");

  runner.addModuleTest("Import Namespace", {{"utils", R"(
                export int add(int a, int b) { return a + b; }
                export int mul(int a, int b) { return a * b; }
            )"}},
                       R"(
            import { add, mul } from "utils";
            print(add(3, 4));
            print(mul(3, 4));
        )",
                       "7\n12");

  runner.addModuleTest("Import Variables", {{"config", R"(
                export int MAX_SIZE = 100;
                export string NAME = "TestApp";
            )"}},
                       R"(
            import { MAX_SIZE, NAME } from "config";
            print(MAX_SIZE);
            print(NAME);
        )",
                       "100\nTestApp");

  runner.addModuleTest("Import Class", {{"shapes", R"(
                export class Rectangle {
                    int width;
                    int height;
                    void init(Rectangle this, int w, int h) {
                        this.width = w;
                        this.height = h;
                    }
                    int area(Rectangle this) {
                        return this.width * this.height;
                    }
                }
            )"}},
                       R"(
            import { Rectangle } from "shapes";
            Rectangle r = new Rectangle(10, 5);
            print(r.area());
        )",
                       "50");

  runner.addModuleTest("Multiple Module Import",
                       {{"mod_a", "export int valA = 10;"},
                        {"mod_b", "export int valB = 20;"},
                        {"mod_c", "export int valC = 30;"}},
                       R"(
            import { valA } from "mod_a";
            import { valB } from "mod_b";
            import { valC } from "mod_c";
            print(valA + valB + valC);
        )",
                       "60");

  runner.addModuleTest("Module with Closure", {{"counter_mod", R"(
                export auto makeCounter = function() -> function {
                    int count = 0;
                    return function() -> int {
                        count = count + 1;
                        return count;
                    };
                };
            )"}},
                       R"(
            import { makeCounter } from "counter_mod";
            auto c = makeCounter();
            print(c());
            print(c());
            print(c());
        )",
                       "1\n2\n3");
}
