#pragma once
#include "TestRunner.h"

// =========================================================
// 控制流边界情况测试 (Control Flow)
// 完全修正版：适配0-based索引 + Lua风格数值for循环
// =========================================================

inline void registerControlFlow(TestRunner &runner) {

  // =========================================================
  // 1. 空循环与零次迭代
  // =========================================================

  runner.addTest("While - Zero Iterations",
                 R"(
            int i = 10;
            while (i < 5) {
                print("never");
                i = i + 1;
            }
            print("done");
       )",
                 "done");

  runner.addTest("For - Zero Iterations",
                 R"(
            for (int i = 10, 4) {
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("While - Single Iteration",
                 R"(
            int i = 0;
            while (i < 1) {
                print(i);
                i = i + 1;
            }
       )",
                 "0");

  runner.addTest("For - Single Iteration",
                 R"(
            for (int i = 0, 0) {
                print(i);
            }
       )",
                 "0");

  // =========================================================
  // 2. If-Else 边界情况
  // =========================================================

  runner.addTest("If - No Else Branch",
                 R"(
            int x = 10;
            if (x > 5) {
                print("yes");
            }
            print("end");
       )",
                 "yes\nend");

  runner.addTest("If - Condition False No Else",
                 R"(
            int x = 3;
            if (x > 5) {
                print("yes");
            }
            print("end");
       )",
                 "end");

  runner.addTest("If - Deeply Nested",
                 R"(
            int a = 1;
            int b = 2;
            int c = 3;
            if (a == 1) {
                if (b == 2) {
                    if (c == 3) {
                        print("deep");
                    } else {
                        print("c wrong");
                    }
                } else {
                    print("b wrong");
                }
            } else {
                print("a wrong");
            }
       )",
                 "deep");

  runner.addTest("If-Else - Long Chain All False",
                 R"(
            int x = 100;
            if (x == 1) {
                print("one");
            } else if (x == 2) {
                print("two");
            } else if (x == 3) {
                print("three");
            } else if (x == 4) {
                print("four");
            } else {
                print("other");
            }
       )",
                 "other");

  runner.addTest("If-Else - First Match",
                 R"(
            int x = 1;
            if (x == 1) {
                print("first");
            } else if (x == 1) {
                print("second");
            } else {
                print("else");
            }
       )",
                 "first");

  runner.addTest("If - Empty Block",
                 R"(
            int x = 5;
            if (x < 3) {
            }
            print("after");
       )",
                 "after");

  // =========================================================
  // 3. Break 边界情况
  // =========================================================

  runner.addTest("Break - First Iteration",
                 R"(
            for (int i = 0, 99) {
                break;
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Break - Last Iteration",
                 R"(
            for (int i = 0, 4) {
                print(i);
                if (i == 4) {
                    break;
                }
            }
       )",
                 "0\n1\n2\n3\n4");

  runner.addTest("Break - While True Pattern",
                 R"(
            int count = 0;
            while (true) {
                print(count);
                count = count + 1;
                if (count >= 3) {
                    break;
                }
            }
            print("exit");
       )",
                 "0\n1\n2\nexit");

  runner.addTest("Break - Deeply Nested Loops",
                 R"(
            for (int i = 0, 2) {
                for (int j = 0, 2) {
                    for (int k = 0, 2) {
                        if (k == 1) {
                            break;
                        }
                        print(i .. "-" .. j .. "-" .. k);
                    }
                }
            }
       )",
                 "0-0-0\n0-1-0\n0-2-0\n1-0-0\n1-1-0\n1-2-0\n2-0-0\n2-1-0\n2-2-0");

  // =========================================================
  // 4. Continue 边界情况
  // =========================================================

  runner.addTest("Continue - First Iteration",
                 R"(
            for (int i = 0, 2) {
                if (i == 0) {
                    continue;
                }
                print(i);
            }
       )",
                 "1\n2");

  runner.addTest("Continue - Last Iteration",
                 R"(
            for (int i = 0, 2) {
                if (i == 2) {
                    continue;
                }
                print(i);
            }
       )",
                 "0\n1");

  runner.addTest("Continue - All Iterations",
                 R"(
            for (int i = 0, 2) {
                continue;
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Continue - Multiple Per Loop",
                 R"(
            for (int i = 0, 5) {
                if (i == 1) { continue; }
                if (i == 3) { continue; }
                if (i == 5) { continue; }
                print(i);
            }
       )",
                 "0\n2\n4");

  runner.addTest("Continue - While Loop",
                 R"(
            int i = 0;
            while (i < 5) {
                i = i + 1;
                if (i == 3) {
                    continue;
                }
                print(i);
            }
       )",
                 "1\n2\n4\n5");

  // =========================================================
  // 5. Break + Continue 组合
  // =========================================================

  runner.addTest("Break After Continue",
                 R"(
            for (int i = 0, 9) {
                if (i % 2 == 0) {
                    continue;
                }
                if (i >= 5) {
                    break;
                }
                print(i);
            }
       )",
                 "1\n3");

  runner.addTest("Continue After Break Check",
                 R"(
            for (int i = 0, 9) {
                if (i == 7) {
                    break;
                }
                if (i < 3) {
                    continue;
                }
                print(i);
            }
       )",
                 "3\n4\n5\n6");

  // =========================================================
  // 6. Return 边界情况
  // =========================================================

  runner.addTest("Return - Multiple Paths",
                 R"(
            int test(int x) {
                if (x < 0) {
                    return -1;
                }
                if (x == 0) {
                    return 0;
                }
                return 1;
            }
            print(test(-5));
            print(test(0));
            print(test(10));
       )",
                 "-1\n0\n1");

  runner.addTest("Return - Early Exit From Loop",
                 R"(
            int findValue(int target) {
                for (int i = 0, 9) {
                    if (i == target) {
                        return i;
                    }
                }
                return -1;
            }
            print(findValue(5));
            print(findValue(15));
       )",
                 "5\n-1");

  runner.addTest("Return - No Value",
                 R"(
            function noReturn() {
                print("running");
                return;
                print("never");
            }
            noReturn();
            print("done");
       )",
                 "running\ndone");

  runner.addTest("Return - In Nested Blocks",
                 R"(
            int nested(int x) {
                if (x > 0) {
                    for (int i = 0, x - 1) {
                        if (i == 2) {
                            return i;
                        }
                    }
                }
                return -1;
            }
            print(nested(5));
            print(nested(1));
       )",
                 "2\n-1");

  // =========================================================
  // 7. 数值For循环边界情况
  // =========================================================

  runner.addTest("Numeric For - Basic Range",
                 R"(
            for (int i = 0, 4) {
                print(i);
            }
       )",
                 "0\n1\n2\n3\n4");

  runner.addTest("Numeric For - With Step",
                 R"(
            for (int i = 0, 10, 2) {
                print(i);
            }
       )",
                 "0\n2\n4\n6\n8\n10");

  runner.addTest("Numeric For - Negative Step",
                 R"(
            for (int i = 5, 0, -1) {
                print(i);
            }
       )",
                 "5\n4\n3\n2\n1\n0");

  runner.addTest("Numeric For - Large Step",
                 R"(
            for (int i = 0, 10, 5) {
                print(i);
            }
       )",
                 "0\n5\n10");

  runner.addTest("Numeric For - Single Value",
                 R"(
            for (int i = 5, 5) {
                print(i);
            }
       )",
                 "5");

  // =========================================================
  // 8. Generic For 迭代器边界情况
  // =========================================================

  runner.addTest("Generic For - Simple Iterator",
                 R"(
            int iter(any s, int c) {
                if (c < 3) {
                    return c + 1;
                }
                return null;
            }
            for (auto i : iter, null, 0) {
                print(i);
            }
       )",
                 "1\n2\n3");

  runner.addTest("Generic For - No Iterations",
                 R"(
            int iter(any s, int c) {
                return null;
            }
            for (auto i : iter, null, 0) {
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Generic For - Single Iteration",
                 R"(
            int iter(any s, int c) {
                if (c < 1) {
                    return c + 1;
                }
                return null;
            }
            for (auto i : iter, null, 0) {
                print(i);
            }
       )",
                 "1");

  runner.addTest("Generic For - Multiple Values",
                 R"(
            vars iter(any s, int c) {
                if (c < 3) {
                    return c + 1, c * 10;
                }
                return null;
            }
            for (auto i, auto v : iter, null, 0) {
                print(i .. ":" .. v);
            }
       )",
                 "1:0\n2:10\n3:20");

  runner.addTest("Generic For - State Parameter",
                 R"(
            int iter(int state, int c) {
                if (c < state) {
                    return c + 1;
                }
                return null;
            }
            for (auto i : iter, 5, 0) {
                print(i);
            }
       )",
                 "1\n2\n3\n4\n5");

  runner.addTest("Nested Generic For - Basic",
                 R"(
            int iter1(any s, int c) {
                if (c < 2) { return c + 1; }
                return null;
            }
            int iter2(any s, int c) {
                if (c < 20) { return c + 10; }
                return null;
            }
            for (auto i : iter1, null, 0) {
                for (auto j : iter2, null, 0) {
                    print(i .. "-" .. j);
                }
            }
       )",
                 "1-10\n1-20\n2-10\n2-20");

  runner.addTest("Nested Generic For - Break Inner",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                for (auto j : iter, null, 0) {
                    if (j == 2) { break; }
                    print(i .. "-" .. j);
                }
            }
       )",
                 "1-1\n2-1\n3-1");

  runner.addTest("Nested Generic For - Break Outer",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                for (auto j : iter, null, 0) {
                    print(i .. "-" .. j);
                }
                if (i == 2) { break; }
            }
       )",
                 "1-1\n1-2\n1-3\n2-1\n2-2\n2-3");

  runner.addTest("Nested Generic For - Continue Inner",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                for (auto j : iter, null, 0) {
                    if (j == 2) { continue; }
                    print(i .. "-" .. j);
                }
            }
       )",
                 "1-1\n1-3\n2-1\n2-3\n3-1\n3-3");

  runner.addTest("Nested Generic For - Continue Outer",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                if (i == 2) { continue; }
                for (auto j : iter, null, 0) {
                    print(i .. "-" .. j);
                }
            }
       )",
                 "1-1\n1-2\n1-3\n3-1\n3-2\n3-3");

  // =========================================================
  // 9. 闭包迭代器边界情况
  // =========================================================

  runner.addTest("Closure Iterator - Zero Iterations",
                 R"(
            function make_empty() {
                return function(any s, any c) -> int {
                    return null;
                };
            }
            for (auto i : make_empty()) {
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Closure Iterator - Break Preserves Closure",
                 R"(
            function make_counter(int max) {
                int count = 0;
                return function(any s, any c) -> int {
                    count = count + 1;
                    if (count <= max) {
                        return count;
                    }
                    return null;
                };
            }

            auto iter = make_counter(10);
            for (auto i : iter) {
                print(i);
                if (i == 3) { break; }
            }
       )",
                 "1\n2\n3");

  // =========================================================
  // 10. 多返回值边界情况
  // =========================================================

  runner.addTest("Generic For - Multiple Values First Null",
                 R"(
            vars iter(any s, int c) {
                if (c < 2) {
                    return c + 1, "val" .. (c + 1);
                }
                return null;
            }
            for (auto i, auto v : iter, null, 0) {
                print(i .. ":" .. v);
            }
       )",
                 "1:val1\n2:val2");

  //  runner.addTest("Pairs - Use Only Key",
  //                 R"(
  //            list data = ["a", "b", "c"];
  //            for (auto i, auto v : pairs(data)) {
  //                print(i);
  //            }
  //       )",
  //                 "0\n1\n2");

  //  runner.addTest("Pairs - Use Only Value",
  //                 R"(
  //            list data = ["a", "b", "c"];
  //            for (auto i, auto v : pairs(data)) {
  //                print(v);
  //            }
  //       )",
  //                 "a\nb\nc");

  // =========================================================
  // 11. 作用域边界情况
  // =========================================================

  runner.addTest(
      "Generic For - Variable Shadowing Multiple",
      R"(
            int i = 100;
            int j = 200;

            int iter(any s, int c) {
                if (c < 2) { return c + 1; }
                return null;
            }

            for (auto i : iter, null, 0) {
                print("outer i: " .. i);
                for (auto i : iter, null, 0) {
                    print("inner i: " .. i);
                }
            }
            print("final i: " .. i);
       )",
      "outer i: 1\ninner i: 1\ninner i: 2\nouter i: 2\ninner i: 1\ninner i: 2\nfinal i: 100");

  //  runner.addTest("Pairs - Variable Shadowing",
  //                 R"(
  //            int i = 999;
  //            string v = "original";
  //
  //            list data = ["a", "b"];
  //            for (auto i, auto v : pairs(data)) {
  //                print(i .. ":" .. v);
  //            }
  //            print(i .. ":" .. v);
  //       )",
  //                 "0:a\n1:b\n999:original");

  runner.addTest("Numeric For - Variable Shadowing",
                 R"(
            int i = 999;

            for (int i = 0, 2) {
                print("loop i: " .. i);
            }
            print("outer i: " .. i);
       )",
                 "loop i: 0\nloop i: 1\nloop i: 2\nouter i: 999");

  runner.addTest("Generic For - Null State",
                 R"(
            int iter(any state, int c) {
                if (state == null && c < 3) {
                    return c + 1;
                }
                return null;
            }
            for (auto i : iter, null, 0) {
                print(i);
            }
       )",
                 "1\n2\n3");

  runner.addTest("Generic For - Complex Control Mix",
                 R"(
            int iter(any s, int c) {
                if (c < 10) { return c + 1; }
                return null;
            }

            int sum = 0;
            for (auto i : iter, null, 0) {
                if (i == 3) { continue; }
                if (i == 7) { continue; }
                if (i == 9) { break; }
                sum = sum + i;
            }
            print(sum);
       )",
                 "26"); // 1+2+4+5+6+8 = 26

  // =========================================================
  // 14. 数值For和Generic For混合
  // =========================================================

  runner.addTest("Numeric For Nested In Generic For",
                 R"(
            int iter(any s, int c) {
                if (c < 2) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                for (int j = 0, 2) {
                    print(i .. "-" .. j);
                }
            }
       )",
                 "1-0\n1-1\n1-2\n2-0\n2-1\n2-2");

  runner.addTest("Generic For Nested In Numeric For",
                 R"(
            int iter(any s, int c) {
                if (c < 2) { return c + 1; }
                return null;
            }
            for (int i = 0, 1) {
                for (auto j : iter, null, 0) {
                    print(i .. "-" .. j);
                }
            }
       )",
                 "0-1\n0-2\n1-1\n1-2");

  runner.addTest("Numeric For - Break And Continue",
                 R"(
            for (int i = 0, 9) {
                if (i < 3) { continue; }
                if (i > 6) { break; }
                print(i);
            }
       )",
                 "3\n4\n5\n6");
}