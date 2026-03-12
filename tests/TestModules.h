#pragma once
#include "Test.h"

// =========================================================
// 8. 模块系统 (Modules)
// =========================================================

inline void registerModules(Test &runner) {

  runner.addTest("Import Namespace (Built-in)",
                 R"(
        import * as m from "math";
        print(m.abs(-42));
    )",
                 "42");

  runner.addTest("Import Named (Built-in)",
                 R"(
        import { abs, max } from "math";
        print(abs(-10));
        print(max(15, 25));
    )",
                 "10\n25");
}
