#pragma once
#include "TestRunner.h"

// =========================================================
// Apply 函数测试 (Apply Function Tests)
// 签名: apply(fn, [args], [receiver])
// =========================================================

inline void registerApply(TestRunner &runner) {
  runner.addTest("Apply Basic",
                 R"(
            int add(int a, int b) {
                return a + b;
            }
            int result = apply(add, [1, 2]);
            print(result);
       )",
                 "3");

  runner.addTest("Apply No Args",
                 R"(
            void sayHello() {
                print("Hello!");
            }
            apply(sayHello);
       )",
                 "Hello!");

  runner.addTest("Apply Single Arg",
                 R"(
            int double(int x) {
                return x * 2;
            }
            print(apply(double, [5]));
            print(apply(double, [21]));
       )",
                 "10\n42");

  runner.addTest("Apply Multiple Args",
                 R"(
            int sum4(int a, int b, int c, int d) {
                return a + b + c + d;
            }
            list<int> args = [1, 2, 3, 4];
            print(apply(sum4, args));
       )",
                 "10");

  runner.addTest("Apply With Native Function",
                 R"(
            list<int> args = [10, 3];
            print(apply(max, args));
            print(apply(min, args));
            print(apply(pow, [2, 8]));
       )",
                 "10\n3\n256");

  runner.addTest("Apply Dynamic Function Selection",
                 R"(
            int add(int a, int b) { return a + b; }
            int sub(int a, int b) { return a - b; }
            int mul(int a, int b) { return a * b; }

            map<string, any> ops = {
                "add": add,
                "sub": sub,
                "mul": mul
            };

            list<int> args = [10, 3];
            print(apply(ops["add"], args));
            print(apply(ops["sub"], args));
            print(apply(ops["mul"], args));
       )",
                 "13\n7\n30");

  runner.addTest("Apply With Lambda",
                 R"(
            auto square = function(int x) -> int { return x * x; };
            auto cube = function(int x) -> int { return x * x * x; };

            print(apply(square, [5]));
            print(apply(cube, [3]));
       )",
                 "25\n27");

  runner.addTest("Apply With Closure",
                 R"(
            function makeAdder(int n) {
                return function (int x) -> int { return x + n; };
            }

            auto add5 = makeAdder(5);
            auto add10 = makeAdder(10);

            print(apply(add5, [100]));
            print(apply(add10, [100]));
       )",
                 "105\n110");

  runner.addTest("Apply Complex Types",
                 R"(
            string processData(list<int> nums, string prefix) {
                return prefix .. ": " .. nums.join(",");
            }

            list<any> args = [[1, 2, 3], "Result"];
            print(apply(processData, args));
       )",
                 "Result: 1,2,3");

  runner.addTest("Apply With Map Arg",
                 R"(
            int getMapValue(map<string, int> m, string key) {
                return m[key];
            }

            map<string, int> data = {"a": 100, "b": 200};
            print(apply(getMapValue, [data, "a"]));
            print(apply(getMapValue, [data, "b"]));
       )",
                 "100\n200");

  runner.addTest("Apply Return Types",
                 R"(
            int returnInt() { return 42; }
            string returnString() { return "hello"; }
            list<int> returnList() { return [1, 2, 3]; }
            bool returnBool() { return true; }

            print(apply(returnInt));
            print(apply(returnString));
            print(apply(returnList)[1]);
            print(apply(returnBool));
       )",
                 "42\nhello\n2\ntrue");

  runner.addTest("Apply Nested Calls",
                 R"(
            int outer(int x) {
                int inner(int y) {
                    return y * 2;
                }
                return apply(inner, [x + 1]);
            }

            print(apply(outer, [10]));
       )",
                 "22");

  runner.addTest("Apply In Loop",
                 R"(
            int increment(int x) {
                return x + 1;
            }

            int value = 0;
            for (int i = 0; i < 5; i+=1) {
                value = apply(increment, [value]);
            }
            print(value);
       )",
                 "5");

  runner.addTest("Apply With Print",
                 R"(
            apply(print, ["Hello"]);
            apply(print, [42]);
            apply(print, [true]);
       )",
                 "Hello\n42\ntrue");

  runner.addTest("Apply Function From List",
                 R"(
            fn a() { return "A"; }
            fn b() { return "B"; }
            fn c() { return "C"; }

            list<any> funcs = [a, b, c];
            for (int i = 0; i < funcs.length; i = i + 1) {
                print(apply(funcs[i]));
            }
       )",
                 "A\nB\nC");

  runner.addTest("Apply Build Args Dynamically",
                 R"(
            int sum3(int a, int b, int c) {
                return a + b + c;
            }

            list<int> args = [];
            args.push(10);
            args.push(20);
            args.push(30);

            print(apply(sum3, args));
       )",
                 "60");

  runner.addTest("Apply With String Operations",
                 R"(
            string greet(string name, string greeting) {
                return greeting .. ", " .. name .. "!";
            }

            string name = "World";
            list<any> args = [name, "Hello"];
            print(apply(greet, args));
       )",
                 "Hello, World!");

  runner.addTest("Apply Recursive Style",
                 R"(
            int factorial(int n) {
                if (n <= 1) {
                    return 1;
                }
                return n * apply(factorial, [n - 1]);
            }

            print(apply(factorial, [5]));
            print(apply(factorial, [6]));
       )",
                 "120\n720");

  runner.addTest("Apply With Pcall",
                 R"(
            double divide(int a, int b) {
                if (b == 0) {
                    error("Division by zero");
                }
                return a / b;
            }

            // 正常调用
            vars ok, result = pcall(apply, divide, [10, 2]);
            print(ok);
            print(result);

            // 错误调用
            ok, result = pcall(apply, divide, [10, 0]);
            print(ok);
       )",
                 "true\n5\nfalse");

  runner.addTest("Apply TypeOf Check",
                 R"(
            any identity(any x) {
                return x;
            }

            print(typeOf(apply(identity, [42])));
            print(typeOf(apply(identity, ["hello"])));
            print(typeOf(apply(identity, [[1,2,3]])));
       )",
                 "int\nstring\nlist");

  // =====================================================
  // Apply With Receiver 测试
  // 签名: apply(fn, args, receiver)
  // =====================================================

  runner.addTest("Apply With Receiver Basic",
                 R"(
            class Counter {
                int value;
                void init(Counter this, int start) {
                    this.value = start;
                }
            }

            // 定义一个需要 this 的方法
            int getValue(Counter this) {
                return this.value;
            }

            int addValue(Counter this, int n) {
                return this.value + n;
            }

            Counter c = new Counter(42);

            // 使用 apply 传入 receiver (第三个参数)
            print(apply(getValue, [], c));
            print(apply(addValue, [10], c));
       )",
                 "42\n52");

  runner.addTest("Apply With Receiver Method From Map",
                 R"(
            class Box {
                int value;
                void init(Box this, int v) {
                    this.value = v;
                }
            }

            int getValue(Box this) { return this.value; }
            int double(Box this) { return this.value * 2; }
            int addN(Box this, int n) { return this.value + n; }

            map<string, any> methods = {
                "get": getValue,
                "double": double,
                "add": addN
            };

            Box b = new Box(100);

            print(apply(methods["get"], [], b));
            print(apply(methods["double"], [], b));
            print(apply(methods["add"], [50], b));
       )",
                 "100\n200\n150");

  runner.addTest("Apply With Receiver Multiple Instances",
                 R"(
            class Point {
                int x;
                int y;
                void init(Point this, int x, int y) {
                    this.x = x;
                    this.y = y;
                }
            }

            int sum(Point this) {
                return this.x + this.y;
            }

            int distance(Point this, Point other) {
                int dx = this.x - other.x;
                int dy = this.y - other.y;
                return dx * dx + dy * dy;
            }

            Point p1 = new Point(10, 20);
            Point p2 = new Point(13, 24);

            print(apply(sum, [], p1));
            print(apply(sum, [], p2));
            print(apply(distance, [p2], p1));
       )",
                 "30\n37\n25");

  runner.addTest("Apply With Receiver Modify State",
                 R"(
            class Accumulator {
                int total;
                void init(Accumulator this, int start) {
                    this.total = start;
                }
            }

            void add(Accumulator this, int n) {
                this.total = this.total + n;
            }

            int getTotal(Accumulator this) {
                return this.total;
            }

            Accumulator acc = new Accumulator(0);

            apply(add, [10], acc);
            apply(add, [20], acc);
            apply(add, [30], acc);

            print(apply(getTotal, [], acc));
       )",
                 "60");

  runner.addTest("Apply With Receiver In Loop",
                 R"(
            class Counter {
                int count;
                void init(Counter this) {
                    this.count = 0;
                }
            }

            void increment(Counter this) {
                this.count = this.count + 1;
            }

            int get(Counter this) {
                return this.count;
            }

            Counter c = new Counter();

            for (int i = 0; i < 5; i = i + 1) {
                apply(increment, [], c);
            }

            print(apply(get, [], c));
       )",
                 "5");

  runner.addTest("Apply With Receiver Chain",
                 R"(
            class Builder {
                string result;
                void init(Builder this) {
                    this.result = "";
                }
            }

            Builder append(Builder this, string s) {
                this.result = this.result .. s;
                return this;
            }

            string build(Builder this) {
                return this.result;
            }

            Builder b = new Builder();

            apply(append, ["Hello"], b);
            apply(append, [" "], b);
            apply(append, ["World"], b);

            print(apply(build, [], b));
       )",
                 "Hello World");

  runner.addTest("Apply With Receiver Dynamic Dispatch",
                 R"(
            class Shape {
                string t;
                void init(Shape this, string t) {
                    this.t = t;
                }
            }

            string getType(Shape this) {
                return this.t;
            }

            int calculate(Shape this, int value) {
                if (this.t == "square") {
                    return value * value;
                } else if (this.t == "double") {
                    return value * 2;
                }
                return value;
            }

            Shape square = new Shape("square");
            Shape doubler = new Shape("double");

            print(apply(getType, [], square));
            print(apply(calculate, [5], square));
            print(apply(calculate, [5], doubler));
       )",
                 "square\n25\n10");

  runner.addTest("Apply With Receiver Function List",
                 R"(
            class Data {
                int value;
                void init(Data this, int v) {
                    this.value = v;
                }
            }

            int plusOne(Data this) { return this.value + 1; }
            int timesTwo(Data this) { return this.value * 2; }
            int squared(Data this) { return this.value * this.value; }

            list<any> transforms = [plusOne, timesTwo, squared];
            Data d = new Data(5);

            for (int i = 0; i < transforms.length; i = i + 1) {
                print(apply(transforms[i], [], d));
            }
       )",
                 "6\n10\n25");

  runner.addTest("Apply With Receiver Closure",
                 R"(
            class Holder {
                int value;
                void init(Holder this, int v) {
                    this.value = v;
                }
            }

            function makeProcessor(int multiplier) {
                return function(Holder this, int offset) -> int {
                    return this.value * multiplier + offset;
                };
            }

            auto process2 = makeProcessor(2);
            auto process3 = makeProcessor(3);

            Holder h = new Holder(10);

            print(apply(process2, [5], h));
            print(apply(process3, [5], h));
       )",
                 "25\n35");

  runner.addTest("Apply With Nil Receiver",
                 R"(
            // 确保 receiver 为 nil 时正常工作
            int add(int a, int b) {
                return a + b;
            }

            int identity(int x) {
                return x;
            }

            print(apply(add, [10, 20]));
            print(apply(identity, [42]));
       )",
                 "30\n42");

  runner.addTest("Apply With Receiver Nested Object",
                 R"(
            class Inner {
                int value;
                void init(Inner this, int v) {
                    this.value = v;
                }
            }

            class Outer {
                any inner;
                void init(Outer this, int v) {
                    this.inner = new Inner(v);
                }
            }

            int getInnerValue(Outer this) {
                return this.inner.value;
            }

            void setInnerValue(Outer this, int v) {
                this.inner.value = v;
            }

            Outer o = new Outer(100);
            print(apply(getInnerValue, [], o));

            apply(setInnerValue, [200], o);
            print(apply(getInnerValue, [], o));
       )",
                 "100\n200");

  runner.addTest("Apply With Receiver Return Instance",
                 R"(
            class Node {
                int value;
                any next;
                void init(Node this, int v) {
                    this.value = v;
                    this.next = null;
                }
            }

            Node setNext(Node this, Node n) {
                this.next = n;
                return n;
            }

            int getValue(Node this) {
                return this.value;
            }

            Node n1 = new Node(1);
            Node n2 = new Node(2);
            Node n3 = new Node(3);

            apply(setNext, [n2], n1);
            apply(setNext, [n3], n2);

            print(apply(getValue, [], n1));
            print(apply(getValue, [], n1.next));
            print(apply(getValue, [], n1.next.next));
       )",
                 "1\n2\n3");

  runner.addTest("Apply With Receiver Pcall",
                 R"(
            class Safe {
                int value;
                void init(Safe this, int v) {
                    this.value = v;
                }
            }

            int divide(Safe this, int divisor) {
                if (divisor == 0) {
                    error("Division by zero");
                }
                return this.value / divisor;
            }

            Safe s = new Safe(100);

            // 正常调用
            vars ok, result = pcall(apply, divide, [10], s);
            print(ok);
            print(result);

            // 错误调用
            ok, result = pcall(apply, divide, [0], s);
            print(ok);
       )",
                 "true\n10\nfalse");
}