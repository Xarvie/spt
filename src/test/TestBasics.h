#pragma once
#include "TestRunner.h"

// =========================================================
// 1. 基础语法与运算 (Basics)
// =========================================================

inline void registerBasics(TestRunner &runner) {

  runner.addTest(
      "Comparison NaN",
      R"(
            float nanX = math.sqrt(-1);
            print(nanX == nanX); // false
            print(nanX != nanX); // true
            print(nanX < nanX); // false
            print(nanX > nanX); // false
            print(nanX <= nanX); // false
            print(nanX >= nanX); // false
            print(nanX == 1); // false
            print(nanX != 1); // true
            print(nanX < 1); // false
            print(nanX > 1); // false
            print(nanX <= 1); // false
            print(nanX >= 1); // false
       )",
      "false\ntrue\nfalse\nfalse\nfalse\nfalse\nfalse\ntrue\nfalse\nfalse\nfalse\nfalse");

  runner.addTest("Logic Short-Circuit",
                 R"(
            bool t = true;
            bool f = false;
            if (t || (1/0 == 0)) { print("OR OK"); }
            if (f && (1/0 == 0)) { print("Fail"); } else { print("AND OK"); }
       )",
                 "OR OK\nAND OK");

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

  runner.addTest("Null and Type Checks",
                 R"(
            var x = null;
            print(x);
            if (x == null) { print("is null"); }
            int a = 42;
            string s = "hello";
            bool b = true;
            float f = 3.14;
            print(a);
            print(s);
            print(b);
       )",
                 "nil\nis null\n42\nhello\ntrue");

  runner.addTest("Update Assignment Operators",
                 R"(
            int a = 10;
            a += 5;
            print(a);
            a -= 3;
            print(a);
            a *= 2;
            print(a);
            a /= 4;
            print(a);
            int b = 17;
            b %= 5;
            print(b);
       )",
                 "15\n12\n24\n6\n2");
}
