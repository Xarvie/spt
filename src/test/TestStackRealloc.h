#pragma once
#include "TestRunner.h"

// =========================================================
// Stack Reallocation Safety Tests - 栈扩容安全性回归测试
// =========================================================
// 这些测试专门验证栈扩容后指针修复的正确性
// 注意：MAX_FRAMES = 64，所以递归深度不能超过 64 层
// 栈扩容主要测试的是 stack (Value 数组) 的扩容，而不是 frames 的扩容

inline void registerStackReallocationTests(TestRunner &runner) {

  // ---------------------------------------------------------
  // 1. 触发栈（Value数组）扩容，而非帧数限制
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - Deep Recursion",
                 R"(
            // 使用尾递归风格，递归深度控制在 MAX_FRAMES 以内
            int deepRecursion(int n, int acc) {
                if (n <= 0) { return acc; }
                return deepRecursion(n - 1, acc + n);
            }
            // 递归深度 50，在 MAX_FRAMES=64 限制内
            print(deepRecursion(50, 0));
       )",
                 "1275"); // 1+2+...+50 = 50*51/2 = 1275

  runner.addTest("Stack Realloc - Many Local Variables Per Frame",
                 R"(
            // 每个帧使用大量局部变量，触发栈空间扩容
            int testManyLocals(int depth) {
                // 声明很多局部变量，占用栈空间
                int a0 = depth; int a1 = depth+1; int a2 = depth+2; int a3 = depth+3;
                int a4 = depth+4; int a5 = depth+5; int a6 = depth+6; int a7 = depth+7;
                int b0 = depth*2; int b1 = depth*2+1; int b2 = depth*2+2; int b3 = depth*2+3;
                int b4 = depth*2+4; int b5 = depth*2+5; int b6 = depth*2+6; int b7 = depth*2+7;

                if (depth <= 0) {
                    return a0 + a7 + b0 + b7;
                }
                return testManyLocals(depth - 1) + a0 + b0;
            }
            print(testManyLocals(30));
       )",
                 "1409");

  // ---------------------------------------------------------
  // 2. UpValue 在栈扩容后的正确性
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - UpValue After Growth",
                 R"(
            // 创建捕获局部变量的闭包，然后触发栈扩容
            auto makeCounter = function() -> function {
                int count = 0;
                return function() -> int {
                    count = count + 1;
                    return count;
                };
            };

            auto counter = makeCounter();

            // 调用触发一定深度的递归（在帧限制内）
            int deepCall(int n) {
                if (n <= 0) { return 0; }
                return deepCall(n - 1) + 1;
            }

            print(counter());  // 1
            deepCall(30);      // 触发一些栈使用
            print(counter());  // 2 - UpValue 应该仍然正确
            deepCall(30);
            print(counter());  // 3
       )",
                 "1\n2\n3");

  runner.addTest("Stack Realloc - Multiple UpValues",
                 R"(
            auto makeAdder = function(int base) -> function {
                int a = base;
                int b = base * 2;
                int c = base * 3;
                return function(int x) -> int {
                    return a + b + c + x;  // 捕获多个 upvalue
                };
            };

            auto adder = makeAdder(10);  // a=10, b=20, c=30

            int deepRecurse(int n) {
                if (n <= 0) { return 0; }
                return deepRecurse(n - 1) + 1;
            }

            print(adder(1));   // 10+20+30+1 = 61
            deepRecurse(40);   // 触发栈使用
            print(adder(2));   // 10+20+30+2 = 62
       )",
                 "61\n62");

  runner.addTest("Stack Realloc - Nested Closures with UpValues",
                 R"(
            auto outer = function() -> function {
                int x = 100;
                auto middle = function() -> function {
                    int y = 200;
                    return function() -> int {
                        return x + y;  // 捕获两层外的变量
                    };
                };
                return middle();
            };

            auto fn = outer();

            int recurse(int n) {
                if (n <= 0) { return 0; }
                return recurse(n - 1) + 1;
            }

            print(fn());       // 300
            recurse(35);
            print(fn());       // 300 - 仍然正确
       )",
                 "300\n300");

  // ---------------------------------------------------------
  // 3. Fiber 中的栈使用
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - Fiber Deep Call",
                 R"(
            auto f = Fiber.create(function(int n) -> int {
                int deepSum(int m, int acc) {
                    if (m <= 0) { return acc; }
                    return deepSum(m - 1, acc + m);
                }
                return deepSum(n, 0);
            });

            print(f.call(40));  // 1+2+...+40 = 820
       )",
                 "820");

  runner.addTest("Stack Realloc - Fiber Yield After Stack Use",
                 R"(
            auto f = Fiber.create(function(any _) -> int {
                int x = 10;

                // 递归使用栈
                int deep(int n) {
                    if (n <= 0) { return 0; }
                    return deep(n - 1) + 1;
                }

                Fiber.yield(x);    // yield 前的值
                deep(30);          // 使用栈
                Fiber.yield(x);    // x 应该仍正确
                x = 20;
                Fiber.yield(x);
                return x + 5;
            });

            print(f.call(null));  // 10
            print(f.call(null));  // 10
            print(f.call(null));  // 20
            print(f.call(null));  // 25
       )",
                 "10\n10\n20\n25");

  runner.addTest("Stack Realloc - Fiber with UpValue",
                 R"(
            int shared = 0;

            auto f = Fiber.create(function(any _) -> int {
                auto increment = function() -> int {
                    shared = shared + 1;
                };

                int recurse(int n) {
                    if (n <= 0) { return 0; }
                    return recurse(n - 1) + 1;
                }

                increment();
                Fiber.yield(shared);
                recurse(30);        // 使用栈
                increment();        // 闭包中的 upvalue 应该正确
                Fiber.yield(shared);
                return shared;
            });

            print(f.call(null));  // 1
            print(f.call(null));  // 2
            print(f.call(null));  // 2
       )",
                 "1\n2\n2");

  // ---------------------------------------------------------
  // 4. 大量局部变量场景
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - Many Local Variables",
                 R"(
            int testManyLocals() {
                int a0 = 0; int a1 = 1; int a2 = 2; int a3 = 3; int a4 = 4;
                int a5 = 5; int a6 = 6; int a7 = 7; int a8 = 8; int a9 = 9;
                int b0 = 10; int b1 = 11; int b2 = 12; int b3 = 13; int b4 = 14;
                int b5 = 15; int b6 = 16; int b7 = 17; int b8 = 18; int b9 = 19;

                int recurse(int n) {
                    if (n <= 0) { return 0; }
                    // 每层递归也有局部变量
                    int local = n;
                    return recurse(n - 1) + local;
                }

                int result = recurse(30);
                // 验证局部变量没有被破坏
                return a0 + a9 + b0 + b9 + result;
            }

            print(testManyLocals());
       )",
                 "503"); // 0+9+10+19 + (1+2+...+30) = 38 + 465 = 503

  runner.addTest("Stack Realloc - Nested Function Calls with Locals",
                 R"(
            int level1(int n) {
                int x1 = n;
                int x2 = n * 2;
                if (n > 0) {
                    return level2(n - 1) + x1 + x2;
                }
                return x1 + x2;
            }

            int level2(int n) {
                int y1 = n;
                int y2 = n * 3;
                if (n > 0) {
                    return level3(n - 1) + y1 + y2;
                }
                return y1 + y2;
            }

            int level3(int n) {
                int z1 = n;
                int z2 = n * 4;
                if (n > 0) {
                    return level1(n - 1) + z1 + z2;
                }
                return z1 + z2;
            }

            print(level1(15));
       )",
                 "470");

  // ---------------------------------------------------------
  // 5. 混合场景：闭包 + 递归
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - Closure in Recursion",
                 R"(
            int recursiveWithClosure(int n, int acc) {
                auto add = function(int x) -> int {
                    return acc + x;
                };

                if (n <= 0) {
                    return add(0);
                }
                return recursiveWithClosure(n - 1, add(n));
            }

            print(recursiveWithClosure(30, 0));
       )",
                 "465"); // 1+2+...+30 = 465

  runner.addTest("Stack Realloc - Fiber Creating Closures",
                 R"(
            auto f = Fiber.create(function(int count) -> int {
                list<function> closures = [];

                for (int i = 0; i < count; i = i + 1) {
                    int captured = i;
                    closures.push(function() -> int {
                        return captured * 2;
                    });
                }

                // 递归使用一些栈空间
                int deep(int n) {
                    if (n <= 0) { return 0; }
                    return deep(n - 1) + 1;
                }
                deep(20);

                // 验证所有闭包仍然正确
                int sum = 0;
                for (int j = 0; j < closures.length; j = j + 1) {
                    sum = sum + closures[j]();
                }
                return sum;
            });

            // sum = 0*2 + 1*2 + 2*2 + ... + 9*2 = 2*(0+1+...+9) = 2*45 = 90
            print(f.call(10));
       )",
                 "90");

  runner.addTest("Stack Realloc - Multiple Fibers Interleaved",
                 R"(
            auto makeRecursiveFiber = function(int id) -> any {
                return Fiber.create(function(int depth) -> int {
                    int recurse(int n) {
                        if (n <= 0) { return id; }
                        return recurse(n - 1) + 1;
                    }
                    Fiber.yield(recurse(depth));
                    Fiber.yield(recurse(depth * 2));
                    return id * 100;
                });
            };

            auto f1 = makeRecursiveFiber(1);
            auto f2 = makeRecursiveFiber(2);
            auto f3 = makeRecursiveFiber(3);

            // 交替调用
            print(f1.call(10));
            print(f2.call(10));
            print(f3.call(10));
            print(f1.call(0));
            print(f2.call(0));
            print(f3.call(0));
       )",
                 "11\n12\n13\n21\n22\n23");

  // ---------------------------------------------------------
  // 7. 边界情况
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - Empty Fiber Resume",
                 R"(
            auto f = Fiber.create(function(any _) -> int {
                int deep(int n) {
                    if (n <= 0) { return 0; }
                    return deep(n - 1) + 1;
                }
                deep(30);
                return 42;
            });
            print(f.call(null));
            print(f.isDone);
       )",
                 "42\ntrue");

  runner.addTest("Stack Realloc - Closure Outlives Stack Frame",
                 R"(
            auto createClosures = function() -> list<function> {
                list<function> result = [];
                for (int i = 0; i < 5; i = i + 1) {
                    int val = i * 10;
                    result.push(function() -> int { return val; });
                }
                return result;
            };

            list<function> closures = createClosures();

            // 递归调用，原来的栈帧已经不存在了
            int deep(int n) {
                if (n <= 0) { return 0; }
                return deep(n - 1) + 1;
            }
            deep(40);

            // 闭包仍然应该正确工作（upvalue 已经关闭）
            int sum = 0;
            for (int j = 0; j < closures.length; j = j + 1) {
                sum = sum + closures[j]();
            }
            // sum = 0 + 10 + 20 + 30 + 40 = 100
            print(sum);
       )",
                 "100");

  runner.addTest("Stack Realloc - Stress Test0",
                 R"(
            // 组合多种操作
            int stressTest(int iterations) {
                int total = 0;

                for (int i = 0; i < iterations; i = i + 1) {
                    // 递归
                    int recurse(int n) {
                        if (n <= 0) { return 0; }
                        return recurse(n - 1) + 1;
                    }

                    // 闭包
                    int captured = i;
                    auto fn = function() -> int { return captured; };

                    // 结合
                    total = total + recurse(20) + fn();
                }

                return total;
            }

            print(stressTest(10));
       )",
                 "245"); // 10 * 20 + (0+1+...+9) = 200 + 45 = 245

  runner.addTest("Stack Realloc - return leak",
                 R"(
        class SecurityVault {
            void clearCache(string password) {
            }
        }

        var vault = new SecurityVault();
        string res = vault.clearCache("123456");

        if (res == "123456") {
            print("返回值泄露");
        } else if (res == nil) {
            print("nil");
        } else {
            print("未知结果: " .. res);
        }
       )",
                 "nil");

  runner.addTest("Return Leak Check - Lambda",
                 R"(
    auto leakTest = function(int secret) -> void {
    };
    var res = leakTest(999);

    if (res == 999) {
        print("a");
    } else if (res == nil) {
        print("nil");
    } else {
        print("c");
    }
    )",
                 "nil");

  // ---------------------------------------------------------
  // 8. 缓存指针失效回归测试 (The Killer Test)
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - Cached Pointer Validation (pcall)",
                 R"(
            auto triggerRealloc = function() -> string {
                // 递归函数，用来吃掉栈空间
                int deepRecurse(int n) {
                    int a = n; int b = n; int c = n; int d = n;
                    if (n <= 0) { return 0; }
                    return deepRecurse(n - 1) + 1;
                }

                // 执行递归，迫使 Fiber stack 扩容
                deepRecurse(250);
                return "survived";
            };

            auto result = pcall(triggerRealloc);

            if (result) {
                print("OK");
            } else {
                print("Failed");
            }
       )",
                 "OK");

  runner.addTest("Stack Realloc - Cached Pointer Validation (Native Init)",
                 R"(
            auto makeHugeStack = function() -> string {
                 int deep(int n) {
                    if (n <= 0) { return 0; }
                    return deep(n - 1) + 1;
                }
                deep(200);
                return "value";
            };

            auto innocentFunc = function(any val) -> any {
                return val;
            };

            // 调用 innocentFunc，参数表达式求值时触发扩容
            auto res = innocentFunc(makeHugeStack());

            print(res);
       )",
                 "value");

  // ---------------------------------------------------------
  // 9. OP_TFORCALL 专用测试 (验证 ensureFrames 及指针修复)
  // ---------------------------------------------------------

  runner.addTest("Stack Realloc - TForCall Frame Expansion",
                 R"(
            // 1. 定义一个简单的迭代器函数
            // 每次 TFORCALL 调用它时，都会尝试压入一个新的 CallFrame
            auto iter = function(any s, int i) -> any {
                if (i < 5) { return i + 1; }
                return null;
            };

            // 2. 递归函数：用于吃掉 CallFrames 空间
            int eatFrames(int depth) {
                if (depth > 0) {
                    return eatFrames(depth - 1);
                }

                // 3. 在栈深处执行 for 循环
                // 当我们处于 frames 数组的边界（如 frameCount == 8）时：
                // OP_TFORCALL 执行 -> 需要压入 iter 的帧 -> 必须调用 ensureFrames(1)
                // 如果没调用 -> 越界写入 -> Crash/UB
                // 如果调用了但没刷新缓存 -> 这是一个悬垂指针 -> Crash
                int sum = 0;
                for (auto i : iter, null, 0) {
                    sum = sum + i;
                }
                return sum;
            }

            int total = 0;
            // 4. 扫描深度范围
            // 默认 DEFAULT_FRAMES_SIZE 是 8。
            // 我们测试 4 到 20 层，肯定能覆盖 8 和 16 这两个扩容边界。
            for (int d = 4; d <= 20; d = d + 1) {
                total = total + eatFrames(d);
            }

            // 验证逻辑:
            // 单次循环 sum = 1+2+3+4+5 = 15
            // 深度循环从 4 到 20，共 17 次调用
            // 总和 = 15 * 17 = 255
            print(total);
       )",
                 "255");

  runner.addTest("Stack Realloc - TForCall Base Pointer Validity",
                 R"(
            // 1. 定义一个"胖"迭代器
            // 通过定义大量局部变量，使其 maxStackSize 变得很大 (例如 > 50)
            // 这样 OP_TFORCALL 在检查栈时，会发现空间不足，强制扩容
            auto fatIter = function(any s, int i) -> any {
                // 占位变量，撑大栈帧
                int a0=0; int a1=0; int a2=0; int a3=0; int a4=0;
                int b0=0; int b1=0; int b2=0; int b3=0; int b4=0;
                int c0=0; int c1=0; int c2=0; int c3=0; int c4=0;
                int d0=0; int d1=0; int d2=0; int d3=0; int d4=0;
                int e0=0; int e1=0; int e2=0; int e3=0; int e4=0;
                int f0=0; int f1=0; int f2=0; int f3=0; int f4=0;

                if (i < 1) { return i + 1; }
                return null;
            };

            // 2. 探测函数
            // 它的栈帧很小，容易通过 OP_CALL 的检查而不触发扩容
            int probe(int depth) {
                if (depth > 0) {
                    return probe(depth - 1);
                }

                // 3. 危险区域
                int count = 0;
                for (auto i : fatIter, null, 0) {
                    count = count + 1;
                }
                return count;
            }

            // 4. 扫描触发点
            int total = 0;
            // 扫描范围稍微大一点，确保命中 DEFAULT_STACK_SIZE 的倍数
            for (int d = 0; d < 100; d = d + 1) {
                total = total + probe(d);
            }

            print("Survival: " + (total > 0));
       )",
                 "Survival: true");
}
