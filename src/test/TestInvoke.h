#pragma once
#include "TestRunner.h"

// =========================================================
// 9. OP_INVOKE 特定测试 (Method Invocation)
// =========================================================

inline void registerInvokeTests(TestRunner &runner) {
  runner.addTest("Invoke - List Methods Chain",
                 R"(
            list<int> l = [];
            l.push(1);
            l.push(2);
            l.push(3);
            print(l.length);
            l.pop();
            print(l.length);
            l.clear();
            print(l.length);
       )",
                 "3\n2\n0");

  runner.addTest("Invoke - Map Methods Chain",
                 R"(
            map<string, int> m = {};
            m["a"] = 1;
            m["b"] = 2;
            print(m.size);
            print(m.has("a"));
            m.remove("a");
            print(m.has("a"));
            print(m.size);
       )",
                 "2\ntrue\nfalse\n1");

  runner.addTest("Invoke - String Methods Chain",
                 R"(
            string s = "  HELLO  ";
            print(s.trim().toLower());
       )",
                 "hello");

  runner.addTest("Invoke - Method on Expression Result",
                 R"(
            list<int> getList() {
                return [1, 2, 3, 4, 5];
            }
            print(getList().length);
            print(getList()[2]);
       )",
                 "5\n3");

  runner.addTest("Invoke - Nested Method Calls",
                 R"(
            string s = "hello,world,test";
            list<any> parts = s.split(",");
            print(parts[0].toUpper());
            print(parts[1].length);
       )",
                 "HELLO\n5");

  runner.addTest("Invoke - Class Method Multiple Args",
                 R"(
            class Math {
                int add3(int a, int b, int c) {
                    return a + b + c;
                }
                int mul3(int a, int b, int c) {
                    return a * b * c;
                }
            }
            Math m = new Math();
            print(m.add3(1, 2, 3));
            print(m.mul3(2, 3, 4));
       )",
                 "6\n24");

  runner.addTest("Invoke - Method Returning Object",
                 R"(
            class Builder {
                list<any> items;
                void __init() {
                    this.items = [];
                }
                Builder add(any item) {
                    this.items.push(item);
                    return this;
                }
                list<any> build() {
                    return this.items;
                }
            }
            Builder b = new Builder();
            b.add(1);
            b.add(2);
            b.add(3);
            list<any> result = b.build();
            print(result.length);
            print(result[1]);
       )",
                 "3\n2");

  runner.addTest("Invoke - Method with Closure Argument",
                 R"(
            class Processor {
                int process(function f, int value) {
                    return f(value);
                }
            }
            auto double = function(int x) -> int { return x * 2; };
            Processor p = new Processor();
            print(p.process(double, 10));
            print(p.process(double, 25));
       )",
                 "20\n50");

  runner.addTest("Invoke - Recursive Method",
                 R"(
            class Factorial {
                int calc(int n) {
                    if (n <= 1) { return 1; }
                    return n * this.calc(n - 1);
                }
            }
            Factorial f = new Factorial();
            print(f.calc(5));
            print(f.calc(7));
       )",
                 "120\n5040");

  runner.addTest("Invoke - Method Modifying Fields",
                 R"(
            class Stack {
                list<any> data;
                void __init() {
                    this.data = [];
                }
                void push(any val) {
                    this.data.push(val);
                }
                any pop() {
                    return this.data.pop();
                }
                int size() {
                    return this.data.length;
                }
            }
            Stack s = new Stack();
            s.push(10);
            s.push(20);
            s.push(30);
            print(s.size());
            print(s.pop());
            print(s.pop());
            print(s.size());
       )",
                 "3\n30\n20\n1");
}
