#pragma once
#include "TestRunner.h"

// =========================================================
// 1. 基础语法与运算 (Basics)
// =========================================================

inline void registerBasics(TestRunner &runner) {
  runner.addTest("Arithmetic Operations",
                 R"(
            int a = 10;
            int b = 20;
            print(a + b * 2);
            print((a + b) * 2);
            print(100 / 4);
            print(17 % 5);
            print(-a);
            print(10 / 3);
       )",
                 "50\n60\n25\n2\n-10\n3");

  runner.addTest("Float Arithmetic",
                 R"(
            float x = 3.14;
            float y = 2.0;
            print(x + y);
            print(x * y);
            print(10.0 / 4.0);
       )",
                 "5.14\n6.28\n2.5");

  runner.addTest("String Concatenation",
                 R"(
            string s1 = "Hello";
            string s2 = "World";
            print(s1 .. " " .. s2);
            print("Value: " .. 42);
            print("count: " .. 100);
       )",
                 "Hello World\nValue: 42\ncount: 100");

  runner.addTest("Boolean Operations",
                 R"(
            bool t = true;
            bool f = false;
            print(t && t);
            print(t && f);
            print(f || t);
            print(f || f);
            print(!t);
            print(!f);
       )",
                 "true\nfalse\ntrue\nfalse\nfalse\ntrue");

  runner.addTest("Comparison Operators",
                 R"(
            print(5 == 5);
            print(5 != 3);
            print(3 < 5);
            print(5 > 3);
            print(5 <= 5);
            print(5 >= 5);
            print(3 <= 5);
            print(5 >= 3);
       )",
                 "true\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue\ntrue");

  runner.addTest(
      "Comparison NaN",
      R"(
            float nanX = sqrt(-1);
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
