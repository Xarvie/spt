#pragma once
#include "TestRunner.h"

// =========================================================
// 13. Defer语句测试 (Defer Statement)
// =========================================================

inline void registerDeferTests(TestRunner &runner) {
  runner.addTest("Defer - Basic Execution Order",
                 R"(
            list<string> logs = [];
            void run() {
                defer { logs.push("first"); }
                defer { logs.push("second"); }
                logs.push("start");
            }
            run();
            print(logs.join(","));
       )",
                 "start,second,first");

  runner.addTest("Defer - With Return Statement",
                 R"(
            int testReturn() {
                defer { print("deferred"); }
                print("returning");
                return 42;
            }
            print(testReturn());
       )",
                 "returning\ndeferred\n42");

  runner.addTest("Defer - Modifying Side Effects",
                 R"(
            int global1 = 0;
            void sideEffect() {
                defer { global1 = 100; }
                global1 = 50;
            }
            print(global1);
            sideEffect();
            print(global1);
       )",
                 "0\n100");

  runner.addTest("Defer - Closure Capture",
                 R"(
            void captureTest() {
                int x = 10;
                defer {
                    // 应该捕获最终的 x 值 (20)
                    print(x);
                }
                x = 20;
            }
            captureTest();
       )",
                 "20");

  runner.addTest("Defer - Inside Control Flow (Function Scope)",
                 R"(
            void scopeTest() {
                if (true) {
                    defer { print("deferred"); }
                    print("inside block");
                }
                print("outside block");
            }
            // defer 通常绑定在函数作用域，所以会在 "outside block" 之后执行
            scopeTest();
       )",
                 "inside block\noutside block\ndeferred");

  runner.addTest("Defer - Argument Evaluation",
                 R"(
             // 测试 defer 内部的代码是在最后执行的，而不是定义时执行
            void evalTime() {
                int a = 1;
                defer {
                    if (a == 2) { print("correct"); } else { print("wrong"); }
                }
                a = 2;
            }
            evalTime();
       )",
                 "correct");

  runner.addTest("Defer - Nested Defers",
                 R"(
            void nested() {
                defer {
                    print("outer");
                }
                print("start");
            }
            nested();
       )",
                 "start\nouter");

  runner.addTest("Defer After Recursion",
                 R"(
            list<string> logs = [];

            void testDefer(int depth) {
                defer { logs.push("defer-" .. depth); }

                if (depth > 0) {
                    testDefer(depth - 1);
                } else {
                    logs.push("bottom");
                }
            }

            testDefer(20);
            print(logs.length);
            print(logs[0]);
            print(logs[logs.length - 1]);
       )",
                 "22\nbottom\ndefer-20");

  runner.addTest("Defer with Closure",
                 R"(
            int result = 0;

            void testDeferClosure(int n) {
                int local = n;
                defer {
                    result = result + local;
                }

                if (n > 0) {
                    testDeferClosure(n - 1);
                }
            }

            testDeferClosure(30);
            // result = 30 + 29 + ... + 0 = 465
            print(result);
       )",
                 "465");

  runner.addTest("Defer - ForLoop rebind",
                 R"(
            void rebind() {
                int i = -1;
                defer { print(i); }
                for(int i = 0; i < 3; i+=1){
                  defer { print(i); }
                  int i = 9;
                  defer { print(i); }
                }
                i = 3;
            }
            rebind();
       )",
                 "9\n2\n9\n1\n9\n0\n3");
}
