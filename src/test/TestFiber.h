#pragma once
#include "TestRunner.h"

// =========================================================
// 14. Fiber 协程测试 (Fiber / Coroutine)
// =========================================================

inline void registerFiberTests(TestRunner &runner) {

  // ---------------------------------------------------------
  // 基础功能测试
  // ---------------------------------------------------------

  runner.addTest("Fiber - Basic Creation",
                 R"(
            auto f = Fiber.create(function(int x) -> int {
                return x * 2;
            });
            print(typeOf(f));
       )",
                 "fiber");

  runner.addTest("Fiber - Simple Call",
                 R"(
            auto f = Fiber.create(function(int x) -> int {
                return x + 10;
            });
            var result = f.call(5);
            print(result);
       )",
                 "15");

  runner.addTest("Fiber - isDone after completion",
                 R"(
            auto f = Fiber.create(function(int x) -> int {
                return x;
            });
            print(f.isDone);
            f.call(42);
            print(f.isDone);
       )",
                 "false\ntrue");

  // ---------------------------------------------------------
  // Yield 测试
  // ---------------------------------------------------------

  runner.addTest("Fiber - Single Yield",
                 R"(
            auto f = Fiber.create(function(int x) -> int {
                Fiber.yield(x * 2);
                return x * 3;
            });
            print(f.call(10));
            print(f.isDone);
            print(f.call(0));
            print(f.isDone);
       )",
                 "20\nfalse\n30\ntrue");

  runner.addTest("Fiber - Multiple Yields",
                 R"(
            auto f = Fiber.create(function(any _) -> string {
                Fiber.yield("first");
                Fiber.yield("second");
                Fiber.yield("third");
                return "done";
            });
            print(f.call(null));
            print(f.call(null));
            print(f.call(null));
            print(f.call(null));
            print(f.isDone);
       )",
                 "first\nsecond\nthird\ndone\ntrue");

  runner.addTest("Fiber - Yield with Value Passing",
                 R"(
            // 计数器 fiber：接收增量，返回当前值
            auto counter = Fiber.create(function(int start) -> int {
                int current = start;
                while (true) {
                    var delta = Fiber.yield(current);
                    current = current + delta;
                }
            });
            print(counter.call(0));   // 初始值 0
            print(counter.call(1));   // 0 + 1 = 1
            print(counter.call(5));   // 1 + 5 = 6
            print(counter.call(10));  // 6 + 10 = 16
       )",
                 "0\n1\n6\n16");

  // ---------------------------------------------------------
  // 生成器模式
  // ---------------------------------------------------------

  runner.addTest("Fiber - Generator Pattern",
                 R"(
            auto range = function(int start, int end) -> any {
                return Fiber.create(function(any _) -> int {
                    for (int i = start; i < end; i = i + 1) {
                        Fiber.yield(i);
                    }
                    return -1;  // 结束标记
                });
            };

            auto iter = range(0, 5);
            string result = "";
            while (!iter.isDone) {
                var v = iter.call(null);
                if (!iter.isDone) {
                    result = result .. v .. ",";
                }
            }
            print(result);
       )",
                 "0,1,2,3,4,");

  runner.addTest("Fiber - Fibonacci Generator",
                 R"(
            auto fibGen = Fiber.create(function(int n) -> int {
                int a = 0;
                int b = 1;
                for (int i = 0; i < n; i = i + 1) {
                    Fiber.yield(a);
                    int temp = a + b;
                    a = b;
                    b = temp;
                }
                return -1;
            });

            list<int> fibs = [];
            var v = fibGen.call(10);
            while (!fibGen.isDone) {
                fibs.push(v);
                v = fibGen.call(0);
            }
            print(fibs.join(","));
       )",
                 "0,1,1,2,3,5,8,13,21,34");

  // ---------------------------------------------------------
  // 错误处理
  // ---------------------------------------------------------

  runner.addTest("Fiber - Abort",
                 R"(
            auto f = Fiber.create(function(any _) -> string {
                Fiber.yield("before");
                Fiber.abort("something went wrong");
                return "after";  // 不会执行
            });
            print(f.call(null));
            var result = f.try(null);
            print(f.isDone);
            print(f.error);
       )",
                 "before\ntrue\nsomething went wrong");

  runner.addTest("Fiber - Error Property",
                 R"(
            auto f = Fiber.create(function(any _) -> int {
                Fiber.abort("test error");
                return 0;
            });
            print(f.error);
            f.try(null);
            print(f.error);
            print(f.isDone);
       )",
                 "nil\ntest error\ntrue");

  runner.addTest("Fiber - Try Catches Error",
                 R"(
            auto f = Fiber.create(function(any _) -> int {
                Fiber.abort("oops");
                return 42;
            });
            var result = f.try(null);
            print(result);
            print(f.isDone);
       )",
                 "oops\ntrue");

  // ---------------------------------------------------------
  // Fiber.current
  // ---------------------------------------------------------

  runner.addTest("Fiber - Current",
                 R"(
            auto f = Fiber.create(function(any _) -> string {
                var me = Fiber.current;
                return typeOf(me);
            });
            print(f.call(null));
       )",
                 "fiber");

  // ---------------------------------------------------------
  // 协作式多任务
  // ---------------------------------------------------------

  runner.addTest("Fiber - Cooperative Multitasking",
                 R"(
            list<string> log = [];

            auto task1 = Fiber.create(function(any _) -> string {
                log.push("T1-1");
                Fiber.yield(null);
                log.push("T1-2");
                Fiber.yield(null);
                log.push("T1-3");
                return "T1-done";
            });

            auto task2 = Fiber.create(function(any _) -> string {
                log.push("T2-1");
                Fiber.yield(null);
                log.push("T2-2");
                return "T2-done";
            });

            // 交替执行
            while (!task1.isDone || !task2.isDone) {
                if (!task1.isDone) { task1.call(null); }
                if (!task2.isDone) { task2.call(null); }
            }
            print(log.join(","));
       )",
                 "T1-1,T2-1,T1-2,T2-2,T1-3");

  runner.addTest("Fiber - Round Robin Scheduler",
                 R"(
            list<string> output = [];

            auto makeWorker = function(string name, int count) -> any {
                return Fiber.create(function(any _) -> string {
                    for (int i = 0; i < count; i = i + 1) {
                        output.push(name .. i);
                        Fiber.yield(null);
                    }
                    return name .. "-done";
                });
            };

            list<any> workers = [
                makeWorker("A", 3),
                makeWorker("B", 2),
                makeWorker("C", 4)
            ];

            // 简单的轮询调度
            bool hasActive = true;
            while (hasActive) {
                hasActive = false;
                for (int i = 0; i < workers.length; i = i + 1) {
                    if (!workers[i].isDone) {
                        workers[i].call(null);
                        hasActive = true;
                    }
                }
            }
            print(output.join(","));
       )",
                 "A0,B0,C0,A1,B1,C1,A2,C2,C3");

  // ---------------------------------------------------------
  // 状态机
  // ---------------------------------------------------------

  runner.addTest("Fiber - State Machine",
                 R"(
            // 使用 Fiber 实现简单的状态机
            auto trafficLight = Fiber.create(function(any _) -> string {
                while (true) {
                    Fiber.yield("RED");
                    Fiber.yield("GREEN");
                    Fiber.yield("YELLOW");
                }
            });

            list<string> states = [];
            for (int i = 0; i < 6; i = i + 1) {
                states.push(trafficLight.call(null));
            }
            print(states.join(","));
       )",
                 "RED,GREEN,YELLOW,RED,GREEN,YELLOW");

  // ---------------------------------------------------------
  // 嵌套 Fiber
  // ---------------------------------------------------------

  runner.addTest("Fiber - Nested Fiber Calls",
                 R"(
            auto inner = Fiber.create(function(int x) -> int {
                return x * 2;
            });

            auto outer = Fiber.create(function(int x) -> int {
                // 在一个 fiber 中调用另一个 fiber
                int result = inner.call(x);
                return result + 1;
            });

            print(outer.call(5));
       )",
                 "11");

  // ---------------------------------------------------------
  // 闭包与 Fiber
  // ---------------------------------------------------------

  runner.addTest("Fiber - Closure Capture",
                 R"(
            int counter = 0;

            auto f = Fiber.create(function(any _) -> int {
                counter = counter + 1;
                Fiber.yield(counter);
                counter = counter + 1;
                Fiber.yield(counter);
                counter = counter + 1;
                return counter;
            });

            print(f.call(null));
            print(f.call(null));
            print(f.call(null));
            print(counter);
       )",
                 "1\n2\n3\n3");

  runner.addTest("Fiber - Factory with Closure",
                 R"(
            auto makeAccumulator = function(int initial) -> any {
                int sum = initial;
                return Fiber.create(function(int delta) -> int {
                    while (true) {
                        sum = sum + delta;
                        var newDelta = Fiber.yield(sum);
                        delta = newDelta;
                    }
                });
            };

            auto acc = makeAccumulator(100);
            print(acc.call(10));   // 100 + 10 = 110
            print(acc.call(20));   // 110 + 20 = 130
            print(acc.call(5));    // 130 + 5 = 135
       )",
                 "110\n130\n135");

  // ---------------------------------------------------------
  // 边界情况
  // ---------------------------------------------------------

  runner.addTest("Fiber - Call Completed Fiber",
                 R"(
            auto f = Fiber.create(function(any _) -> int {
                return 42;
            });
            print(f.call(null));
            print(f.isDone);
            // 再次调用已完成的 fiber 应该报错，用 try 捕获
            var result = f.try(null);
            print(f.error != null);
       )",
                 "42\ntrue\ntrue");

  runner.addTest("Fiber - Yield Nil",
                 R"(
            auto f = Fiber.create(function(any _) -> any {
                Fiber.yield(null);
                return "done";
            });
            var v = f.call(null);
            print(v == null);
            print(f.call(null));
       )",
                 "true\ndone");

  runner.addTest("Fiber - Empty Function",
                 R"(
            auto f = Fiber.create(function(any _) -> any {
                return null;
            });
            var result = f.call(null);
            print(result == null);
            print(f.isDone);
       )",
                 "true\ntrue");

  // ---------------------------------------------------------
  // 复杂场景
  // ---------------------------------------------------------

  runner.addTest("Fiber - Pipeline Processing",
                 R"(
            // 数据处理管道
            auto producer = Fiber.create(function(any _) -> int {
                for (int i = 1; i <= 5; i = i + 1) {
                    Fiber.yield(i);
                }
                return 0;
            });

            auto doubler = Fiber.create(function(int x) -> int {
                while (true) {
                    var newX = Fiber.yield(x * 2);
                    x = newX;
                }
            });

            list<int> results = [];
            var v = producer.call(null);
            while (!producer.isDone) {
                var doubled = doubler.call(v);
                results.push(doubled);
                v = producer.call(null);
            }
            print(results.join(","));
       )",
                 "2,4,6,8,10");

  runner.addTest("Fiber - Suspend without Value",
                 R"(
            auto f = Fiber.create(function(any _) -> string {
                print("before suspend");
                Fiber.suspend();
                print("after suspend");
                return "done";
            });
            f.call(null);
            print("---");
            f.call(null);
       )",
                 "before suspend\n---\nafter suspend");

  // ---------------------------------------------------------
  // 与其他特性的交互
  // ---------------------------------------------------------

  runner.addTest("Fiber - With List Operations",
                 R"(
            auto listProcessor = Fiber.create(function(list<int> items) -> int {
                for (int i = 0; i < items.length; i = i + 1) {
                    Fiber.yield(items[i] * items[i]);
                }
                return -1;
            });

            list<int> squares = [];
            var v = listProcessor.call([1, 2, 3, 4, 5]);
            while (!listProcessor.isDone) {
                if (v != -1) { squares.push(v); }
                v = listProcessor.call([]);
            }
            print(squares.join(","));
       )",
                 "1,4,9,16,25");

  runner.addTest("Fiber - With Map",
                 R"(
            auto f = Fiber.create(function(map<string, int> data) -> string {
                list<string> keys = data.keys();
                for (int i = 0; i < keys.length; i = i + 1) {
                    Fiber.yield(keys[i] .. "=" .. data[keys[i]]);
                }
                return "end";
            });

            list<string> output = [];
            map<string, int> m = {"a": 1, "b": 2, "c": 3};
            var v = f.call(m);
            while (!f.isDone) {
                if (v != "end") { output.push(v); }
                v = f.call({});
            }
            // Map 迭代顺序可能不固定，只检查长度
            print(output.length);
       )",
                 "3");

  // ---------------------------------------------------------
  // 性能相关（简单压力测试）
  // ---------------------------------------------------------

  runner.addTest("Fiber - Many Yields",
                 R"(
            auto f = Fiber.create(function(int n) -> int {
                int sum = 0;
                for (int i = 0; i < n; i = i + 1) {
                    var delta = Fiber.yield(i);
                    sum = sum + delta;
                }
                return sum;
            });

            int total = 0;
            var v = f.call(100);
            while (!f.isDone) {
                total = total + v;
                v = f.call(1);
            }
            // v 是最终的 sum = 100 (每次传入 1)
            print(v);
            // total = 0+1+2+...+99 = 4950
            print(total);
       )",
                 "100\n4950");

  runner.addTest("Fiber - Multiple Fibers",
                 R"(
            int sum = 0;
            for (int i = 0; i < 10; i = i + 1) {
                auto f = Fiber.create(function(int x) -> int {
                    return x * x;
                });
                sum = sum + f.call(i);
            }
            // 0+1+4+9+16+25+36+49+64+81 = 285
            print(sum);
       )",
                 "285");
}
