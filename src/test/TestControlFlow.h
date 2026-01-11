#pragma once
#include "TestRunner.h"

// =========================================================
// 控制流边界情况测试 (Control Flow)
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
            for (int i = 10; i < 5; i = i + 1) {
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
            for (int i = 0; i < 1; i = i + 1) {
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
            for (int i = 0; i < 100; i = i + 1) {
                break;
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Break - Last Iteration",
                 R"(
            for (int i = 0; i < 5; i = i + 1) {
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
            for (int i = 0; i < 3; i = i + 1) {
                for (int j = 0; j < 3; j = j + 1) {
                    for (int k = 0; k < 3; k = k + 1) {
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
            for (int i = 0; i < 3; i = i + 1) {
                if (i == 0) {
                    continue;
                }
                print(i);
            }
       )",
                 "1\n2");

  runner.addTest("Continue - Last Iteration",
                 R"(
            for (int i = 0; i < 3; i = i + 1) {
                if (i == 2) {
                    continue;
                }
                print(i);
            }
       )",
                 "0\n1");

  runner.addTest("Continue - All Iterations",
                 R"(
            for (int i = 0; i < 3; i = i + 1) {
                continue;
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Continue - Multiple Per Loop",
                 R"(
            for (int i = 0; i < 6; i = i + 1) {
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
            for (int i = 0; i < 10; i = i + 1) {
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
            for (int i = 0; i < 10; i = i + 1) {
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
            int classify(int x) {
                if (x < 0) {
                    return -1;
                }
                if (x == 0) {
                    return 0;
                }
                if (x < 10) {
                    return 1;
                }
                return 2;
            }
            print(classify(-5));
            print(classify(0));
            print(classify(5));
            print(classify(100));
       )",
                 "-1\n0\n1\n2");

  runner.addTest("Return - In Loop",
                 R"(
            int findIndex(int target) {
                int i = 0;
                while (i < 100) {
                    if (i == target) {
                        return i;
                    }
                    i = i + 1;
                }
                return -1;
            }
            print(findIndex(0));
            print(findIndex(50));
            print(findIndex(99));
            print(findIndex(200));
       )",
                 "0\n50\n99\n-1");

  runner.addTest("Return - Nested Loop",
                 R"(
            int findSum(int target) {
                for (int i = 0; i < 10; i = i + 1) {
                    for (int j = 0; j < 10; j = j + 1) {
                        if (i + j == target) {
                            return i * 10 + j;
                        }
                    }
                }
                return -1;
            }
            print(findSum(0));
            print(findSum(5));
            print(findSum(18));
            print(findSum(20));
       )",
                 "0\n5\n99\n-1");

  runner.addTest("Return - Immediate",
                 R"(
            int immediate() {
                return 42;
                print("never");
            }
            print(immediate());
       )",
                 "42");

  runner.addTest("Return - Void Function Early Exit",
                 R"(
            void process(int x) {
                if (x < 0) {
                    print("negative");
                    return;
                }
                if (x == 0) {
                    print("zero");
                    return;
                }
                print("positive");
            }
            process(-1);
            process(0);
            process(1);
       )",
                 "negative\nzero\npositive");

  // =========================================================
  // 7. 递归边界情况
  // =========================================================

  runner.addTest("Recursion - Base Case Zero",
                 R"(
            int countdown(int n) {
                if (n <= 0) {
                    return 0;
                }
                print(n);
                return countdown(n - 1);
            }
            countdown(3);
       )",
                 "3\n2\n1");

  runner.addTest("Recursion - Single Call",
                 R"(
            int single(int n) {
                if (n <= 0) {
                    return 0;
                }
                return n;
            }
            print(single(1));
            print(single(0));
       )",
                 "1\n0");

  runner.addTest("Recursion - Tail Recursion",
                 R"(
            int sumTail(int n, int acc) {
                if (n <= 0) {
                    return acc;
                }
                return sumTail(n - 1, acc + n);
            }
            print(sumTail(5, 0));
            print(sumTail(10, 0));
       )",
                 "15\n55");

  runner.addTest("Recursion - Deep Recursion",
                 R"(
            int deep(int n) {
                if (n <= 0) {
                    return 0;
                }
                return 1 + deep(n - 1);
            }
            print(deep(50));
       )",
                 "50");

  // =========================================================
  // 8. 循环变量作用域
  // =========================================================

  runner.addTest("For - Variable Scope After Loop",
                 R"(
            int i = 999;
            for (int i = 0; i < 3; i = i + 1) {
                print(i);
            }
            print("outer: " .. i);
       )",
                 "0\n1\n2\nouter: 999");

  runner.addTest("While - Variable Modified In Loop",
                 R"(
            int i = 0;
            while (i < 3) {
                i = i + 1;
            }
            print(i);
       )",
                 "3");

  runner.addTest("Nested Loops - Same Variable Name",
                 R"(
            for (int i = 0; i < 2; i = i + 1) {
                for (int i = 10; i < 12; i = i + 1) {
                    print(i);
                }
            }
       )",
                 "10\n11\n10\n11");

  // =========================================================
  // 9. 复杂条件表达式
  // =========================================================

  runner.addTest("If - Complex Condition AND",
                 R"(
            int a = 5;
            int b = 10;
            if (a > 0 && b > 0 && a < b) {
                print("all true");
            }
       )",
                 "all true");

  runner.addTest("If - Complex Condition OR",
                 R"(
            int x = 5;
            if (x < 0 || x > 100 || x == 5) {
                print("one true");
            }
       )",
                 "one true");

  runner.addTest("If - Short Circuit AND",
                 R"(
            int check(int x) {
                print("check " .. x);
                return x;
            }
            if (check(0) && check(1)) {
                print("both");
            }
            print("done");
       )",
                 "check 0\ndone");

  runner.addTest("If - Short Circuit OR",
                 R"(
            int check(int x) {
                print("check " .. x);
                return x;
            }
            if (check(1) || check(2)) {
                print("one");
            }
            print("done");
       )",
                 "check 1\none\ndone");

  runner.addTest("If - Nested OR Simulation",
                 R"(
            int x = 5;
            int found = 0;
            if (x < 0) {
                found = 1;
            } else if (x > 100) {
                found = 1;
            } else if (x == 5) {
                found = 1;
            }
            print(found);
       )",
                 "1");

  runner.addTest("While - Complex Condition",
                 R"(
            int i = 0;
            int j = 10;
            while (i < 5 && j > 5) {
                print(i .. "-" .. j);
                i = i + 1;
                j = j - 1;
            }
       )",
                 "0-10\n1-9\n2-8\n3-7\n4-6");

  // =========================================================
  // 10. 边界值测试
  // =========================================================

  runner.addTest("Loop - Negative Range",
                 R"(
            for (int i = -3; i <= 0; i = i + 1) {
                print(i);
            }
       )",
                 "-3\n-2\n-1\n0");

  runner.addTest("Loop - Counting Down",
                 R"(
            for (int i = 5; i > 0; i = i - 1) {
                print(i);
            }
       )",
                 "5\n4\n3\n2\n1");

  runner.addTest("Loop - Step Greater Than One",
                 R"(
            for (int i = 0; i < 10; i = i + 3) {
                print(i);
            }
       )",
                 "0\n3\n6\n9");

  runner.addTest("If - Boundary Values",
                 R"(
            int test(int x) {
                if (x <= 0) {
                    return -1;
                } else if (x >= 100) {
                    return 1;
                }
                return 0;
            }
            print(test(-1));
            print(test(0));
            print(test(1));
            print(test(99));
            print(test(100));
            print(test(101));
       )",
                 "-1\n-1\n0\n0\n1\n1");

  // =========================================================
  // 11. 嵌套控制结构组合
  // =========================================================

  runner.addTest("If Inside While",
                 R"(
            int i = 0;
            while (i < 5) {
                if (i % 2 == 0) {
                    print("even: " .. i);
                } else {
                    print("odd: " .. i);
                }
                i = i + 1;
            }
       )",
                 "even: 0\nodd: 1\neven: 2\nodd: 3\neven: 4");

  runner.addTest("While Inside If",
                 R"(
            int flag = 1;
            if (flag) {
                int i = 0;
                while (i < 3) {
                    print(i);
                    i = i + 1;
                }
            }
       )",
                 "0\n1\n2");

  runner.addTest("Complex Nesting",
                 R"(
            for (int i = 0; i < 3; i = i + 1) {
                if (i == 1) {
                    int j = 0;
                    while (j < 2) {
                        for (int k = 0; k < 2; k = k + 1) {
                            print(i .. "-" .. j .. "-" .. k);
                        }
                        j = j + 1;
                    }
                }
            }
       )",
                 "1-0-0\n1-0-1\n1-1-0\n1-1-1");

  // =========================================================
  // 12. 特殊情况
  // =========================================================

  runner.addTest("Empty Body Loops",
                 R"(
            int i = 0;
            while (i < 3) {
                i = i + 1;
            }
            print(i);

            int sum = 0;
            for (int j = 0; j < 5; j = j + 1) {
                sum = sum + j;
            }
            print(sum);
       )",
                 "3\n10");

  runner.addTest("Infinite Loop Prevention",
                 R"(
            int count = 0;
            while (true) {
                count = count + 1;
                if (count >= 5) {
                    break;
                }
            }
            print(count);
       )",
                 "5");

  runner.addTest("Nested Return From If",
                 R"(
            int nested(int x) {
                if (x > 0) {
                    if (x > 10) {
                        return 2;
                    } else {
                        return 1;
                    }
                } else {
                    if (x < -10) {
                        return -2;
                    } else {
                        return -1;
                    }
                }
            }
            print(nested(20));
            print(nested(5));
            print(nested(-5));
            print(nested(-20));
       )",
                 "2\n1\n-1\n-2");

  runner.addTest("Multiple Loops Sequential",
                 R"(
            for (int i = 0; i < 2; i = i + 1) {
                print("A" .. i);
            }
            for (int j = 0; j < 2; j = j + 1) {
                print("B" .. j);
            }
            int k = 0;
            while (k < 2) {
                print("C" .. k);
                k = k + 1;
            }
       )",
                 "A0\nA1\nB0\nB1\nC0\nC1");
}

// =========================================================
// 泛型 For 循环边界情况
// =========================================================

inline void registerGenericLoop(TestRunner &runner) {

  // =========================================================
  // 1. 空集合与零次迭代
  // =========================================================

  runner.addTest("Pairs - Empty List",
                 R"(
            list data = [];
            for (auto i, auto v : pairs(data)) {
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Pairs - Empty Map",
                 R"(
            map data = {};
            for (auto k, auto v : pairs(data)) {
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Generic For - Immediate Null",
                 R"(
            int iter(any s, any c) {
                return null;
            }
            for (auto i : iter, null, null) {
                print("never");
            }
            print("done");
       )",
                 "done");

  // =========================================================
  // 2. 单元素集合
  // =========================================================

  runner.addTest("Pairs - Single Element List",
                 R"(
            list data = ["only"];
            for (auto i, auto v : pairs(data)) {
                print(i .. ":" .. v);
            }
       )",
                 "0:only");

  runner.addTest("Pairs - Single Element Map",
                 R"(
            map data = {"key": "value"};
            for (auto k, auto v : pairs(data)) {
                print(k .. ":" .. v);
            }
       )",
                 "key:value");

  runner.addTest("Generic For - Single Iteration",
                 R"(
            int iter(any s, int c) {
                if (c < 1) {
                    return c + 1;
                }
                return null;
            }
            for (auto i : iter, null, 0) {
                print("single: " .. i);
            }
       )",
                 "single: 1");

  // =========================================================
  // 3. Break 边界情况
  // =========================================================

  runner.addTest("Generic For - Break On First",
                 R"(
            int iter(any s, int c) {
                if (c < 10) { return c + 1; }
                return null;
            }
            int count = 0;
            for (auto i : iter, null, 0) {
                count = count + 1;
                break;
            }
            print(count);
       )",
                 "1");

  runner.addTest("Generic For - Break On Last",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                print(i);
                if (i == 3) { break; }
            }
       )",
                 "1\n2\n3");

  runner.addTest("Pairs List - Break On First",
                 R"(
            list data = ["a", "b", "c"];
            int count = 0;
            for (auto i, auto v : pairs(data)) {
                count = count + 1;
                break;
            }
            print(count);
       )",
                 "1");

  runner.addTest("Pairs List - Break On Last",
                 R"(
            list data = ["a", "b", "c"];
            for (auto i, auto v : pairs(data)) {
                print(v);
                if (i == 2) { break; }
            }
       )",
                 "a\nb\nc");

  // =========================================================
  // 4. Continue 边界情况
  // =========================================================

  runner.addTest("Generic For - Continue On First",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                if (i == 1) { continue; }
                print(i);
            }
       )",
                 "2\n3");

  runner.addTest("Generic For - Continue On Last",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                if (i == 3) { continue; }
                print(i);
            }
       )",
                 "1\n2");

  runner.addTest("Generic For - Continue All",
                 R"(
            int iter(any s, int c) {
                if (c < 3) { return c + 1; }
                return null;
            }
            for (auto i : iter, null, 0) {
                continue;
                print("never");
            }
            print("done");
       )",
                 "done");

  runner.addTest("Pairs List - Continue On First",
                 R"(
            list data = ["skip", "b", "c"];
            for (auto i, auto v : pairs(data)) {
                if (i == 0) { continue; }
                print(v);
            }
       )",
                 "b\nc");

  // =========================================================
  // 5. 嵌套泛型循环
  // =========================================================

  runner.addTest("Pairs - Nested List Iteration",
                 R"(
            list outer = ["A", "B"];
            list inner = [1, 2];
            for (auto i, auto a : pairs(outer)) {
                for (auto j, auto b : pairs(inner)) {
                    print(a .. b);
                }
            }
       )",
                 "A1\nA2\nB1\nB2");

  runner.addTest("Generic For - Nested Different Iterators",
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
  // 6. 闭包迭代器边界情况
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
  // 7. 多返回值边界情况
  // =========================================================

  runner.addTest("Generic For - Multiple Values First Null",
                 R"(
            mutivar iter(any s, int c) {
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

  runner.addTest("Pairs - Use Only Key",
                 R"(
            list data = ["a", "b", "c"];
            for (auto i, auto v : pairs(data)) {
                print(i);
            }
       )",
                 "0\n1\n2");

  runner.addTest("Pairs - Use Only Value",
                 R"(
            list data = ["a", "b", "c"];
            for (auto i, auto v : pairs(data)) {
                print(v);
            }
       )",
                 "a\nb\nc");

  // =========================================================
  // 8. 作用域边界情况
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

  runner.addTest("Pairs - Variable Shadowing",
                 R"(
            int i = 999;
            string v = "original";

            list data = ["a", "b"];
            for (auto i, auto v : pairs(data)) {
                print(i .. ":" .. v);
            }
            print(i .. ":" .. v);
       )",
                 "0:a\n1:b\n999:original");

  // =========================================================
  // 9. 状态传递边界情况
  // =========================================================

  runner.addTest("Generic For - State Modification",
                 R"(
            int iter(list state, int c) {
                if (c < len(state)) {
                    return c + 1;
                }
                return null;
            }

            list data = [10, 20, 30];
            for (auto i : iter, data, 0) {
                print(data[i - 1]);
            }
       )",
                 "10\n20\n30");

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

  // =========================================================
  // 10. 混合控制流
  // =========================================================

  runner.addTest("Generic For With Return",
                 R"(
            int findInList(list data, string target) {
                for (auto i, auto v : pairs(data)) {
                    if (v == target) {
                        return i;
                    }
                }
                return -1;
            }

            list items = ["a", "b", "c", "d"];
            print(findInList(items, "a"));
            print(findInList(items, "c"));
            print(findInList(items, "x"));
       )",
                 "0\n2\n-1");

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

  runner.addTest("Pairs In Recursive Function",
                 R"(
            int sumList(list data, int idx) {
                if (idx >= len(data)) {
                    return 0;
                }
                return data[idx] + sumList(data, idx + 1);
            }

            list nums = [1, 2, 3, 4, 5];
            print(sumList(nums, 0));
       )",
                 "15");

  runner.addTest("Nested Pairs Different Types",
                 R"(
            map outer = {"a": 1, "b": 2};
            list inner = [10, 20];
            int sum = 0;

            for (auto k, auto v : pairs(outer)) {
                for (auto i, auto n : pairs(inner)) {
                    sum = sum + v * n;
                }
            }
            print(sum);
       )",
                 "90"); // (1*10 + 1*20) + (2*10 + 2*20) = 30 + 60 = 90
}