#include "TestRunner.h"
#include <filesystem>

void registerBench(TestRunner &runner) {
  runner.addTest("Particle Simulation",
                 R"(
            class Vector {
                float x;
                float y;

                void init(Vector this, float x, float y) {
                    this.x = x;
                    this.y = y;
                }

                void add(Vector this, float dx, float dy) {
                    this.x += dx;
                    this.y += dy;
                }
            }

            class Particle {
                Vector pos;
                Vector vel;
                int id;

                void init(Particle this, int id) {
                    this.id = id;
                    // 嵌套对象创建
                    this.pos = new Vector(0.0, 0.0);
                    this.vel = new Vector(1.5, 0.5);
                }

                void update(Particle this) {
                    // 核心热点：方法调用 (OP_INVOKE)
                    this.pos.add(this.vel.x, this.vel.y);

                    // 简单的边界反弹逻辑 (条件跳转)
                    if (this.pos.x > 100.0) {
                        this.pos.x = 0.0;
                    }
                    if (this.pos.y > 100.0) {
                        this.pos.y = 0.0;
                    }
                }

                float checksum(Particle this) {
                    return this.pos.x + this.pos.y;
                }
            }

            // 1. 初始化容器
            list<any> systems = [];
            int count = 2000;

            for (int i = 0; i < count; i += 1) {
                systems.push(new Particle(i));
            }

            // 2. 主模拟循环
            int frames = 1000;
            for (int f = 0; f < frames; f += 1) {
                for (int i = 0; i < systems.length; i += 1) {
                    Particle p = systems[i];
                    p.update();
                }
            }

            // 3. 验证结果
            float total = 0.0;
            for (int i = 0; i < systems.length; i += 1) {
                Particle p = systems[i];
                total += p.checksum();
            }

            print(toInt(total));
       )",
                 "382000");
}

void registerFib38Bench(TestRunner &runner) {
  runner.addTest("Recursion - Fibonacci",
                 R"(
            int fib(int n) {
                if (n < 2) { return n; }
                return fib(n-1) + fib(n-2);
            }
            print(fib(38));
       )",
                 "39088169");
}

// =========================================================
// 1. 基础语法与运算 (Basics)
// =========================================================
void registerBasics(TestRunner &runner) {
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

// =========================================================
// 2. 控制流 (Control Flow)
// =========================================================
void registerControlFlow(TestRunner &runner) {
  runner.addTest("If-Else Chain",
                 R"(
            int x = 15;
            if (x < 10) {
                print("small");
            } else if (x < 20) {
                print("medium");
            } else {
                print("large");
            }

            int y = 5;
            if (y < 10) {
                print("small");
            } else if (y < 20) {
                print("medium");
            }

            int z = 25;
            if (z < 10) {
                print("small");
            } else {
                print("large");
            }
       )",
                 "medium\nsmall\nlarge");

  runner.addTest("While Loop",
                 R"(
            int i = 0;
            int sum = 0;
            while (i < 5) {
                sum = sum + i;
                i = i + 1;
            }
            print(sum);
       )",
                 "10");

  runner.addTest("For Loop",
                 R"(
            int sum = 0;
            for (int i = 1; i <= 5; i = i + 1) {
                sum = sum + i;
            }
            print(sum);
       )",
                 "15");

  runner.addTest("Nested Loops",
                 R"(
            for (int i = 0; i < 3; i = i + 1) {
                for (int j = 0; j < 2; j = j + 1) {
                    print(i .. "-" .. j);
                }
            }
       )",
                 "0-0\n0-1\n1-0\n1-1\n2-0\n2-1");

  runner.addTest("Break Statement",
                 R"(
            for (int i = 0; i < 10; i = i + 1) {
                if (i == 5) { break; }
                print(i);
            }
       )",
                 "0\n1\n2\n3\n4");

  runner.addTest("Continue Statement",
                 R"(
            for (int i = 0; i < 5; i = i + 1) {
                if (i == 2) { continue; }
                print(i);
            }
       )",
                 "0\n1\n3\n4");

  runner.addTest("Nested Break/Continue",
                 R"(
            int i = 0;
            while (i < 3) {
                int j = 0;
                while (j < 3) {
                    if (j == 1) {
                        j = j + 1;
                        continue;
                    }
                    if (i == 1) {
                        break;
                    }
                    print(i .. "-" .. j);
                    j = j + 1;
                }
                i = i + 1;
            }
       )",
                 "0-0\n0-2\n2-0\n2-2");

  runner.addTest("Recursion - Fibonacci",
                 R"(
            int fib(int n) {
                if (n < 2) { return n; }
                return fib(n-1) + fib(n-2);
            }
            print(fib(0));
            print(fib(1));
            print(fib(5));
            print(fib(10));
       )",
                 "0\n1\n5\n55");

  runner.addTest("Recursion - Factorial",
                 R"(
            int factorial(int n) {
                if (n <= 1) { return 1; }
                return n * factorial(n - 1);
            }
            print(factorial(0));
            print(factorial(1));
            print(factorial(5));
            print(factorial(7));
       )",
                 "1\n1\n120\n5040");

  runner.addTest("Early Return",
                 R"(
            int findFirst(int target) {
                for (int i = 0; i < 100; i = i + 1) {
                    if (i == target) {
                        return i;
                    }
                }
                return -1;
            }
            print(findFirst(7));
            print(findFirst(50));
       )",
                 "7\n50");
}

// =========================================================
// 3. 函数与闭包 (Functions & Closures)
// =========================================================
void registerFunctions(TestRunner &runner) {
  runner.addTest("Basic Function",
                 R"(
            int add(int a, int b) {
                return a + b;
            }
            print(add(3, 4));
            print(add(10, 20));
       )",
                 "7\n30");

  runner.addTest("Function with No Return Value",
                 R"(
            void greet(string name) {
                print("Hello, " .. name);
            }
            greet("World");
            greet("Claude");
       )",
                 "Hello, World\nHello, Claude");

  runner.addTest("Nested Functions",
                 R"(
            int outer(int x) {
                int inner(int y) {
                    return y * 2;
                }
                return inner(x) + 1;
            }
            print(outer(5));
            print(outer(10));
       )",
                 "11\n21");

  runner.addTest("Lambda Expression",
                 R"(
            auto add = function(int a, int b) -> int {
                return a + b;
            };
            print(add(3, 4));

            auto mul = function(int x, int y) -> int { return x * y; };
            print(mul(5, 6));
       )",
                 "7\n30");

  runner.addTest("Closure Basic",
                 R"(
            auto makeCounter = function() -> function {
                int count = 0;
                return function() -> int {
                    count = count + 1;
                    return count;
                };
            };
            auto c1 = makeCounter();
            print(c1());
            print(c1());
            print(c1());
       )",
                 "1\n2\n3");

  runner.addTest("Multiple Closures Independent",
                 R"(
            auto makeCounter = function() -> function {
                int count = 0;
                return function() -> int {
                    count = count + 1;
                    return count;
                };
            };
            auto c1 = makeCounter();
            auto c2 = makeCounter();
            print(c1());
            print(c1());
            print(c2());
            print(c1());
            print(c2());
       )",
                 "1\n2\n1\n3\n2");

  runner.addTest("Closure Shared State",
                 R"(
            var setter;
            var getter;
            {
                int x = 10;
                setter = function(int v) -> void { x = v; };
                getter = function() -> int { return x; };
            }
            print(getter());
            setter(42);
            print(getter());
            setter(100);
            print(getter());
       )",
                 "10\n42\n100");

  runner.addTest("Higher-Order Function",
                 R"(
            int apply(function f, int x) {
                return f(x);
            }
            auto double = function(int n) -> int { return n * 2; };
            auto square = function(int n) -> int { return n * n; };
            print(apply(double, 5));
            print(apply(square, 5));
       )",
                 "10\n25");

  runner.addTest("mutivar Function",
                 R"(
            mutivar returnAB(int a, int b) {
                return a, b;
            }
            mutivar a, b = returnAB(1, 2);
            print(a, b);
       )",
                 "1 2");

  runner.addTest("Closure with Multiple Upvalues",
                 R"(
            auto makeAdder = function(int a, int b) -> function {
                return function(int x) -> int {
                    return a + b + x;
                };
            };
            auto add5and3 = makeAdder(5, 3);
            print(add5and3(10));
            print(add5and3(20));
       )",
                 "18\n28");

  runner.addTest("Deeply Nested Closure",
                 R"(
            auto level1 = function(int a) -> function {
                return function(int b) -> function {
                    return function(int c) -> int {
                        return a + b + c;
                    };
                };
            };
            auto l2 = level1(10);
            auto l3 = l2(20);
            print(l3(30));
       )",
                 "60");
}

// =========================================================
// 4. 类与对象 (Classes & Objects)
// =========================================================
void registerClasses(TestRunner &runner) {
  runner.addTest("Class Basic",
                 R"(
            class Point {
                int x;
                int y;
                void init(Point this, int x, int y) {
                    this.x = x;
                    this.y = y;
                }
            }
            Point p = new Point(10, 20);
            print(p.x);
            print(p.y);
       )",
                 "10\n20");

  runner.addTest("Class Methods",
                 R"(
            class Counter {
                int value;
                void init(Counter this, int start) {
                    this.value = start;
                }
                void increment(Counter this) {
                    this.value = this.value + 1;
                }
                void add(Counter this, int n) {
                    this.value = this.value + n;
                }
                int get(Counter this) {
                    return this.value;
                }
            }
            Counter c = new Counter(0);
            c.increment();
            print(c.get());
            c.add(5);
            print(c.get());
            c.increment();
            print(c.get());
       )",
                 "1\n6\n7");

  runner.addTest("Class Method Chaining Style",
                 R"(
            class Point {
                int x;
                int y;
                void init(Point this, int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                void move(Point this, int dx, int dy) {
                    this.x = this.x + dx;
                    this.y = this.y + dy;
                }
                void scale(Point this, int factor) {
                    this.x = this.x * factor;
                    this.y = this.y * factor;
                }
            }
            Point p = new Point(10, 20);
            p.move(5, 5);
            print(p.x .. ", " .. p.y);
            p.scale(2);
            print(p.x .. ", " .. p.y);
       )",
                 "15, 25\n30, 50");

  runner.addTest("Multiple Instances",
                 R"(
            class Box {
                int value;
                void init(Box this, int v) {
                    this.value = v;
                }
            }
            Box a = new Box(10);
            Box b = new Box(20);
            Box c = new Box(30);
            print(a.value);
            print(b.value);
            print(c.value);
            a.value = 100;
            print(a.value);
            print(b.value);
       )",
                 "10\n20\n30\n100\n20");

  runner.addTest("Class with Complex Fields",
                 R"(
            class Container {
                any data;
                void init(Container this, any d) {
                    this.data = d;
                }
            }
            Container c1 = new Container(42);
            Container c2 = new Container("hello");
            Container c3 = new Container([1, 2, 3]);
            print(c1.data);
            print(c2.data);
            print(c3.data[1]);
       )",
                 "42\nhello\n2");

  runner.addTest("Circular Reference Safety",
                 R"(
            class Node {
                any next;
                int value;
                void init(Node this, int v) {
                    this.value = v;
                    this.next = null;
                }
            }
            Node a = new Node(1);
            Node b = new Node(2);
            a.next = b;
            b.next = a;
            print(a.value);
            print(a.next.value);
            print(a.next.next.value);
       )",
                 "1\n2\n1");

  runner.addTest("Class Without Init",
                 R"(
            class Simple {
                int x;
                int y;
            }
            Simple s = new Simple();
            s.x = 10;
            s.y = 20;
            print(s.x + s.y);
       )",
                 "30");

  runner.addTest("Method Returning Value",
                 R"(
            class Calculator {
                int value;
                void init(Calculator this, int v) {
                    this.value = v;
                }
                int double(Calculator this) {
                    return this.value * 2;
                }
                int addTo(Calculator this, int other) {
                    return this.value + other;
                }
            }
            Calculator calc = new Calculator(15);
            print(calc.double());
            print(calc.addTo(10));
       )",
                 "30\n25");

  runner.addTest("Nested Object Access",
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
            Outer o = new Outer(42);
            print(o.inner.value);
       )",
                 "42");

  runner.addTest("Object Receiver",
                 R"(
            class Receiver {
                int value;
                void init(Receiver this, int v) {
                    this.value = v;
                }
            }
            int receiverMethodAdd(any this, int a){
                return this.value + a;
            }
            auto o = new Receiver(42);
            o.add = receiverMethodAdd;
            print(o.add(1));
       )",
                 "43");
}

// =========================================================
// 5. 数据结构 - List (Lists)
// =========================================================
void registerLists(TestRunner &runner) {
  runner.addTest("List Basic Operations",
                 R"(
            list<int> l = [1, 2, 3];
            print(l[0]);
            print(l[1]);
            print(l[2]);
            l[1] = 20;
            print(l[1]);
       )",
                 "1\n2\n3\n20");

  runner.addTest("List Length",
                 R"(
            list<int> l1 = [];
            print(l1.length);
            list<int> l2 = [1];
            print(l2.length);
            list<int> l3 = [1, 2, 3, 4, 5];
            print(l3.length);
       )",
                 "0\n1\n5");

  runner.addTest("List Push and Pop",
                 R"(
            list<int> l = [];
            l.push(10);
            l.push(20);
            l.push(30);
            print(l.length);
            print(l.pop());
            print(l.pop());
            print(l.length);
            print(l[0]);
       )",
                 "3\n30\n20\n1\n10");

  runner.addTest("List Insert",
                 R"(
            list<int> l = [1, 3];
            l.insert(1, 2);
            print(l[0] .. ", " .. l[1] .. ", " .. l[2]);
            l.insert(0, 0);
            print(l[0] .. ", " .. l[1]);
            l.insert(4, 4);
            print(l[4]);
       )",
                 "1, 2, 3\n0, 1\n4");

  runner.addTest("List RemoveAt",
                 R"(
            list<int> l = [10, 20, 30, 40];
            int removed = l.removeAt(1);
            print(removed);
            print(l.length);
            print(l[0] .. ", " .. l[1] .. ", " .. l[2]);
       )",
                 "20\n3\n10, 30, 40");

  runner.addTest("List Clear",
                 R"(
            list<int> l = [1, 2, 3, 4, 5];
            print(l.length);
            l.clear();
            print(l.length);
       )",
                 "5\n0");

  runner.addTest("List IndexOf",
                 R"(
            list<int> l = [10, 20, 30, 20, 40];
            print(l.indexOf(20));
            print(l.indexOf(30));
            print(l.indexOf(99));
       )",
                 "1\n2\n-1");

  runner.addTest("List Contains",
                 R"(
            list<int> l = [1, 2, 3, 4, 5];
            print(l.contains(3));
            print(l.contains(10));
       )",
                 "true\nfalse");

  runner.addTest("List Mixed Types",
                 R"(
            list<any> l = [1, "hello", true, 3.14];
            print(l[0]);
            print(l[1]);
            print(l[2]);
            print(l[3]);
       )",
                 "1\nhello\ntrue\n3.14");

  runner.addTest("List Nested",
                 R"(
            list<any> matrix = [[1, 2], [3, 4], [5, 6]];
            print(matrix[0][0]);
            print(matrix[1][1]);
            print(matrix[2][0]);
       )",
                 "1\n4\n5");

  runner.addTest("List Slice",
                 R"(
            list<int> l = [0, 1, 2, 3, 4, 5];
            list<int> s1 = l.slice(1, 4);
            print(s1.length);
            print(s1[0] .. "," .. s1[1] .. "," .. s1[2]);

            list<int> s2 = l.slice(0, 2);
            print(s2[0] .. "," .. s2[1]);

            list<int> s3 = l.slice(4, 6);
            print(s3[0] .. "," .. s3[1]);

            list<int> s4 = l.slice(-3, -1);
            print(s4.length);
       )",
                 "3\n1,2,3\n0,1\n4,5\n2");

  runner.addTest("List Join",
                 R"(
            list<any> l1 = [1, 2, 3];
            print(l1.join(","));
            print(l1.join(" - "));
            print(l1.join(""));

            list<string> l2 = ["hello", "world"];
            print(l2.join(" "));

            list<any> empty = [];
            print("[" .. empty.join(",") .. "]");
       )",
                 "1,2,3\n1 - 2 - 3\n123\nhello world\n[]");

  runner.addTest("List in Loop",
                 R"(
            list<int> l = [10, 20, 30, 40, 50];
            int sum = 0;
            for (int i = 0; i < l.length; i = i + 1) {
                sum = sum + l[i];
            }
            print(sum);
       )",
                 "150");
}

// =========================================================
// 6. 数据结构 - Map (Maps)
// =========================================================
void registerMaps(TestRunner &runner) {
  runner.addTest("Map Basic Operations",
                 R"(
            map<string, int> m = {"a": 1, "b": 2};
            print(m["a"]);
            print(m["b"]);
            m["c"] = 3;
            print(m["c"]);
            m["a"] = 100;
            print(m["a"]);
       )",
                 "1\n2\n3\n100");

  runner.addTest("Map Size",
                 R"(
            map<string, int> m1 = {};
            print(m1.size);
            map<string, int> m2 = {"x": 1};
            print(m2.size);
            map<string, int> m3 = {"a": 1, "b": 2, "c": 3};
            print(m3.size);
       )",
                 "0\n1\n3");

  runner.addTest("Map Has",
                 R"(
            map<string, int> m = {"a": 1, "b": 2};
            print(m.has("a"));
            print(m.has("b"));
            print(m.has("c"));
       )",
                 "true\ntrue\nfalse");

  runner.addTest("Map Remove",
                 R"(
            map<string, int> m = {"a": 100, "b": 200, "c": 300};
            int val = m.remove("b");
            print(val);
            print(m.has("b"));
            print(m.size);
       )",
                 "200\nfalse\n2");

  runner.addTest("Map Keys",
                 R"(
            map<string, int> m = {"x": 1, "y": 2};
            list<any> keys = m.keys();
            print(keys.length);
       )",
                 "2");

  runner.addTest("Map Values",
                 R"(
            map<string, int> m = {"a": 10, "b": 20};
            list<any> vals = m.values();
            print(vals.length);
       )",
                 "2");

  runner.addTest("Map Clear",
                 R"(
            map<string, int> m = {"a": 1, "b": 2, "c": 3};
            print(m.size);
            m.clear();
            print(m.size);
       )",
                 "3\n0");

  runner.addTest("Map Mixed Value Types",
                 R"(
            map<string, any> m = {};
            m["int"] = 42;
            m["str"] = "hello";
            m["bool"] = true;
            m["list"] = [1, 2, 3];
            print(m["int"]);
            print(m["str"]);
            print(m["bool"]);
            print(m["list"][1]);
       )",
                 "42\nhello\ntrue\n2");

  runner.addTest("Map Nested",
                 R"(
            map<string, any> outer = {};
            map<string, int> inner = {"x": 10, "y": 20};
            outer["point"] = inner;
            print(outer["point"]["x"]);
            print(outer["point"]["y"]);
       )",
                 "10\n20");

  runner.addTest("Map Integer Keys",
                 R"(
            map<int, string> m = {};
            m[1] = "one";
            m[2] = "two";
            m[100] = "hundred";
            print(m[1]);
            print(m[2]);
            print(m[100]);
       )",
                 "one\ntwo\nhundred");
}

// =========================================================
// 7. 字符串方法 (String Methods)
// =========================================================
void registerStrings(TestRunner &runner) {
  runner.addTest("String Length",
                 R"(
            string s1 = "";
            print(s1.length);
            string s2 = "hello";
            print(s2.length);
            string s3 = "hello world";
            print(s3.length);
       )",
                 "0\n5\n11");

  runner.addTest("String Slice",
                 R"(
            string s = "hello world";
            print(s.slice(0, 5));
            print(s.slice(6, 11));
            print(s.slice(0, 1));
       )",
                 "hello\nworld\nh");

  runner.addTest("String Slice Negative Index",
                 R"(
            string s = "hello";
            print(s.slice(-3, 5));
            print(s.slice(0, -1));
       )",
                 "llo\nhell");

  runner.addTest("String IndexOf",
                 R"(
            string s = "hello world";
            print(s.indexOf("world"));
            print(s.indexOf("o"));
            print(s.indexOf("xyz"));
       )",
                 "6\n4\n-1");

  runner.addTest("String Contains",
                 R"(
            string s = "hello world";
            print(s.contains("world"));
            print(s.contains("llo"));
            print(s.contains("xyz"));
       )",
                 "true\ntrue\nfalse");

  runner.addTest("String StartsWith EndsWith",
                 R"(
            string s = "hello world";
            print(s.startsWith("hello"));
            print(s.startsWith("world"));
            print(s.endsWith("world"));
            print(s.endsWith("hello"));
       )",
                 "true\nfalse\ntrue\nfalse");

  runner.addTest("String ToUpper ToLower",
                 R"(
            string s = "Hello World";
            print(s.toUpper());
            print(s.toLower());
       )",
                 "HELLO WORLD\nhello world");

  runner.addTest("String Trim",
                 R"(
            string s1 = "  hello  ";
            print("[" .. s1.trim() .. "]");
            string s2 = "\t\ntest\n\t";
            print("[" .. s2.trim() .. "]");
       )",
                 "[hello]\n[test]");

  runner.addTest("String Split",
                 R"(
            string s = "a,b,c,d";
            list<any> parts = s.split(",");
            print(parts.length);
            print(parts[0]);
            print(parts[2]);
       )",
                 "4\na\nc");

  runner.addTest("String Split Empty Delimiter",
                 R"(
            string s = "abc";
            list<any> chars = s.split("");
            print(chars.length);
            print(chars[0]);
            print(chars[1]);
            print(chars[2]);
       )",
                 "3\na\nb\nc");

  runner.addTest("String Find",
                 R"(
            string s = "hello world";
            print(s.find("world"));
            print(s.find("o"));
            print(s.find("xyz"));
       )",
                 "6\n4\n-1");

  runner.addTest("String Replace",
                 R"(
            string s1 = "hello world";
            print(s1.replace("world", "there"));

            string s2 = "aaa";
            print(s2.replace("a", "bb"));

            string s3 = "no match";
            print(s3.replace("xyz", "abc"));

            string s4 = "a-b-c";
            print(s4.replace("-", ""));
       )",
                 "hello there\nbbbbbb\nno match\nabc");
}

// =========================================================
// 8. 模块系统 (Modules)
// =========================================================
void registerModules(TestRunner &runner) {
  runner.addModuleTest("Import Named", {{"math", R"(
                export int square(int x) { return x * x; }
                export int cube(int x) { return x * x * x; }
            )"}},
                       R"(
            import { square, cube } from "math";
            print(square(5));
            print(cube(3));
        )",
                       "25\n27");

  runner.addModuleTest("Import Namespace", {{"utils", R"(
                export int add(int a, int b) { return a + b; }
                export int mul(int a, int b) { return a * b; }
            )"}},
                       R"(
            import { add, mul } from "utils";
            print(add(3, 4));
            print(mul(3, 4));
        )",
                       "7\n12");

  runner.addModuleTest("Import Variables", {{"config", R"(
                export int MAX_SIZE = 100;
                export string NAME = "TestApp";
            )"}},
                       R"(
            import { MAX_SIZE, NAME } from "config";
            print(MAX_SIZE);
            print(NAME);
        )",
                       "100\nTestApp");

  runner.addModuleTest("Import Class", {{"shapes", R"(
                export class Rectangle {
                    int width;
                    int height;
                    void init(Rectangle this, int w, int h) {
                        this.width = w;
                        this.height = h;
                    }
                    int area(Rectangle this) {
                        return this.width * this.height;
                    }
                }
            )"}},
                       R"(
            import { Rectangle } from "shapes";
            Rectangle r = new Rectangle(10, 5);
            print(r.area());
        )",
                       "50");

  runner.addModuleTest("Multiple Module Import",
                       {{"mod_a", "export int valA = 10;"},
                        {"mod_b", "export int valB = 20;"},
                        {"mod_c", "export int valC = 30;"}},
                       R"(
            import { valA } from "mod_a";
            import { valB } from "mod_b";
            import { valC } from "mod_c";
            print(valA + valB + valC);
        )",
                       "60");

  runner.addModuleTest("Module with Closure", {{"counter_mod", R"(
                export auto makeCounter = function() -> function {
                    int count = 0;
                    return function() -> int {
                        count = count + 1;
                        return count;
                    };
                };
            )"}},
                       R"(
            import { makeCounter } from "counter_mod";
            auto c = makeCounter();
            print(c());
            print(c());
            print(c());
        )",
                       "1\n2\n3");
}

// =========================================================
// 9. OP_INVOKE 特定测试 (Method Invocation)
// =========================================================
void registerInvokeTests(TestRunner &runner) {
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
                int add3(Math this, int a, int b, int c) {
                    return a + b + c;
                }
                int mul3(Math this, int a, int b, int c) {
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
                void init(Builder this) {
                    this.items = [];
                }
                Builder add(Builder this, any item) {
                    this.items.push(item);
                    return this;
                }
                list<any> build(Builder this) {
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
                int process(Processor this, function f, int value) {
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
                int calc(Factorial this, int n) {
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
                void init(Stack this) {
                    this.data = [];
                }
                void push(Stack this, any val) {
                    this.data.push(val);
                }
                any pop(Stack this) {
                    return this.data.pop();
                }
                int size(Stack this) {
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

// =========================================================
// 10. 边界情况与回归测试 (Edge Cases & Regressions)
// =========================================================
void registerEdgeCases(TestRunner &runner) {
  runner.addTest("Edge - Empty Structures",
                 R"(
            list<any> emptyList = [];
            map<string, any> emptyMap = {};
            print(emptyList.length);
            print(emptyMap.size);
       )",
                 "0\n0");

  runner.addTest("Edge - Single Element",
                 R"(
            list<int> l = [42];
            print(l[0]);
            print(l.length);
            print(l.pop());
            print(l.length);
       )",
                 "42\n1\n42\n0");

  runner.addTest("Edge - Deep Nesting",
                 R"(
            map<string, any> m = {};
            m["a"] = {};
            m["a"]["b"] = {};
            m["a"]["b"]["c"] = 42;
            print(m["a"]["b"]["c"]);
       )",
                 "42");

  runner.addTest("Edge - Large Loop",
                 R"(
            int sum = 0;
            for (int i = 0; i < 1000; i = i + 1) {
                sum = sum + 1;
            }
            print(sum);
       )",
                 "1000");

  runner.addTest("Edge - Many Function Calls",
                 R"(
            int identity(int x) { return x; }
            int result = identity(identity(identity(identity(identity(42)))));
            print(result);
       )",
                 "42");

  runner.addTest("Edge - String Edge Cases",
                 R"(
            string empty = "";
            print(empty.length);
            print(empty.toUpper());
            string single = "x";
            print(single.length);
            print(single.toUpper());
       )",
                 "0\n\n1\nX");

  runner.addTest("Edge - Boolean as Condition",
                 R"(
            bool flag = true;
            if (flag) { print("yes"); }
            flag = false;
            if (flag) { print("no"); } else { print("else"); }
       )",
                 "yes\nelse");

  runner.addTest("Edge - Null Handling",
                 R"(
            var x = null;
            if (x) { print("truthy"); } else { print("falsy"); }
            int y = 1;
            if (y) { print("truthy"); } else { print("falsy"); }
            string z = "a";
            if (z) { print("truthy"); } else { print("falsy"); }
       )",
                 "falsy\ntruthy\ntruthy");

  runner.addTest("Edge - Numeric Limits",
                 R"(
            int big = 1000000000;
            print(big * 2);
            int neg = -1000000000;
            print(neg * 2);
       )",
                 "2000000000\n-2000000000");

  runner.addTest("Edge - Mixed Expressions",
                 R"(
            int a = 5;
            int b = 3;
            print((a + b) * (a - b));
            print(a * b + a / b);
            print((a > b) && (b > 0));
            print(10 / 4);
            print(10.0 / 4);
       )",
                 "16\n16\ntrue\n2\n2.5");

  // 回归测试
  std::string multiVarScript = "mutivar ";
  for (int i = 0; i < 200; ++i) {
    if (i > 0)
      multiVarScript += ", ";
    multiVarScript += "v" + std::to_string(i);
  }
  multiVarScript += " = 0;\nprint(v0);";
  runner.addTest("Regression - Multi-Var Declaration (Bug #9)", multiVarScript, "0");

  std::string hugeModuleBody = R"(
      export var data = {};
      for (int i = 0; i < 2000; i = i + 1) {
          data["key_" .. i] = "value_" .. i;
      }
  )";
  runner.addModuleTest("Regression - Module GC Safety", {{"stress_module", hugeModuleBody}},
                       R"(
          import * as s from "stress_module";
          print("OK");
      )",
                       "OK");
}

// =========================================================
// 11. 综合测试 (Integration Tests)
// =========================================================
void registerIntegrationTests(TestRunner &runner) {
  runner.addTest("Integration - Simple Calculator",
                 R"(
            class Calculator {
                int value;
                void init(Calculator this) {
                    this.value = 0;
                }
                void set(Calculator this, int v) {
                    this.value = v;
                }
                void add(Calculator this, int v) {
                    this.value = this.value + v;
                }
                void sub(Calculator this, int v) {
                    this.value = this.value - v;
                }
                void mul(Calculator this, int v) {
                    this.value = this.value * v;
                }
                int result(Calculator this) {
                    return this.value;
                }
            }
            Calculator c = new Calculator();
            c.set(10);
            c.add(5);
            c.mul(2);
            c.sub(10);
            print(c.result());
       )",
                 "20");

  runner.addTest("Integration - Linked List",
                 R"(
            class Node {
                int value;
                any next;
                void init(Node this, int v) {
                    this.value = v;
                    this.next = null;
                }
            }

            Node head = new Node(1);
            head.next = new Node(2);
            head.next.next = new Node(3);

            int sum = 0;
            Node current = head;
            while (current != null) {
                sum = sum + current.value;
                current = current.next;
            }
            print(sum);
       )",
                 "6");

  runner.addTest("Integration - Word Counter",
                 R"(
            string text = "hello world hello";
            list<any> words = text.split(" ");

            map<string, int> counts = {};
            for (int i = 0; i < words.length; i = i + 1) {
                string word = words[i];
                if (counts.has(word)) {
                    counts[word] = counts[word] + 1;
                } else {
                    counts[word] = 1;
                }
            }
            print(counts["hello"]);
            print(counts["world"]);
       )",
                 "2\n1");

  runner.addTest("Integration - Event System",
                 R"(
            class EventEmitter {
                list<any> listeners;
                void init(EventEmitter this) {
                    this.listeners = [];
                }
                void on(EventEmitter this, function callback) {
                    this.listeners.push(callback);
                }
                void emit(EventEmitter this, any data) {
                    for (int i = 0; i < this.listeners.length; i = i + 1) {
                        this.listeners[i](data);
                    }
                }
            }

            EventEmitter emitter = new EventEmitter();
            int total = 0;
            emitter.on(function(any x) -> void { total = total + x; });
            emitter.on(function(any x) -> void { total = total + x * 2; });

            emitter.emit(10);
            print(total);
       )",
                 "30");

  runner.addTest("Integration - Binary Search",
                 R"(
            int search(list<int> arr, int target) {
                int left = 0;
                int right = 7;

                while (left <= right) {
                    int mid = (left + right) / 2;
                    if (arr[mid] == target) {
                        return mid;
                    } else if (arr[mid] < target) {
                        left = mid + 1;
                    } else {
                        right = mid - 1;
                    }
                }
                return -1;
            }

            list<int> arr = [1, 3, 5, 7, 9, 11, 13, 15];
            print(search(arr, 7));
            print(search(arr, 1));
            print(search(arr, 15));
            print(search(arr, 8));
       )",
                 "3\n0\n7\n-1");

  runner.addTest("Integration - Memoized Fibonacci",
                 R"(
            map<int, int> cache = {};

            int fib(int n) {
                if (n < 2) { return n; }
                if (cache.has(n)) { return cache[n]; }
                int result = fib(n-1) + fib(n-2);
                cache[n] = result;
                return result;
            }

            print(fib(10));
            print(fib(20));
            print(fib(30));
       )",
                 "55\n6765\n832040");

  runner.addTest("Integration - State Machine",
                 R"(
            class StateMachine {
                string state;
                void init(StateMachine this) {
                    this.state = "idle";
                }
                void transition(StateMachine this, string event) {
                    if (this.state == "idle" && event == "start") {
                        this.state = "running";
                    } else if (this.state == "running" && event == "pause") {
                        this.state = "paused";
                    } else if (this.state == "paused" && event == "resume") {
                        this.state = "running";
                    } else if (this.state == "running" && event == "stop") {
                        this.state = "stopped";
                    }
                }
                string getState(StateMachine this) {
                    return this.state;
                }
            }

            StateMachine sm = new StateMachine();
            print(sm.getState());
            sm.transition("start");
            print(sm.getState());
            sm.transition("pause");
            print(sm.getState());
            sm.transition("resume");
            print(sm.getState());
            sm.transition("stop");
            print(sm.getState());
       )",
                 "idle\nrunning\npaused\nrunning\nstopped");
}

// =========================================================
// 12. 内置函数测试 (Built-in Functions)
// =========================================================
void registerBuiltinFunctions(TestRunner &runner) {
  // 类型转换
  runner.addTest("Builtin - toInt",
                 R"(
            print(toInt(3.7));
            print(toInt(3.2));
            print(toInt("42"));
            print(toInt("123abc"));
            print(toInt(true));
            print(toInt(false));
       )",
                 "3\n3\n42\n123\n1\n0");

  runner.addTest("Builtin - toFloat",
                 R"(
            print(toFloat(42));
            print(toFloat("3.14"));
            print(toFloat(true));
       )",
                 "42\n3.14\n1");

  runner.addTest("Builtin - toString",
                 R"(
            print(toString(42));
            print(toString(true));
            print(toString(false));
            print(toString(null));
       )",
                 "42\ntrue\nfalse\nnil");

  runner.addTest("Builtin - toBool",
                 R"(
            print(toBool(1));
            print(toBool(0));
            print(toBool("hello"));
            print(toBool(""));
            print(toBool(null));
       )",
                 "true\ntrue\ntrue\ntrue\nfalse");

  // 类型检查
  runner.addTest("Builtin - Type Checks",
                 R"(
            print(isInt(42));
            print(isInt(3.14));
            print(isFloat(3.14));
            print(isFloat(42));
            print(isNumber(42));
            print(isNumber(3.14));
            print(isString("hello"));
            print(isString(42));
            print(isBool(true));
            print(isBool(1));
            print(isNull(null));
            print(isNull(0));
       )",
                 "true\nfalse\ntrue\nfalse\ntrue\ntrue\ntrue\nfalse\ntrue\nfalse\ntrue\nfalse");

  runner.addTest("Builtin - typeOf",
                 R"(
            print(typeOf(42));
            print(typeOf(3.14));
            print(typeOf("hello"));
            print(typeOf(true));
            print(typeOf(null));
            print(typeOf([1,2,3]));
            print(typeOf({"a": 1}));
       )",
                 "int\nfloat\nstring\nbool\nnil\nlist\nmap");

  runner.addTest("Builtin - isList isMap isFunction",
                 R"(
            print(isList([1, 2, 3]));
            print(isList("not a list"));
            print(isMap({"a": 1}));
            print(isMap([1, 2]));
            auto f = function() -> void {};
            print(isFunction(f));
            print(isFunction(42));
       )",
                 "true\nfalse\ntrue\nfalse\ntrue\nfalse");

  // 数学函数
  runner.addTest("Builtin - Math Functions",
                 R"(
            print(abs(-5));
            print(abs(5));
            print(abs(-3.14));
            print(floor(3.7));
            print(floor(3.2));
            print(ceil(3.2));
            print(ceil(3.7));
            print(round(3.4));
            print(round(3.5));
            print(round(3.6));
       )",
                 "5\n5\n3.14\n3\n3\n4\n4\n3\n4\n4");

  runner.addTest("Builtin - sqrt pow",
                 R"(
            print(toInt(sqrt(16)));
            print(toInt(sqrt(9)));
            print(toInt(pow(2, 10)));
            print(toInt(pow(3, 3)));
       )",
                 "4\n3\n1024\n27");

  runner.addTest("Builtin - min max",
                 R"(
            print(min(3, 7));
            print(min(10, 2));
            print(max(3, 7));
            print(max(10, 2));
            print(min(-5, 5));
            print(max(-5, 5));
       )",
                 "3\n2\n7\n10\n-5\n5");

  // 实用函数
  runner.addTest("Builtin - len",
                 R"(
            print(len("hello"));
            print(len([1, 2, 3, 4]));
            print(len({"a": 1, "b": 2}));
            print(len(""));
            print(len([]));
       )",
                 "5\n4\n2\n0\n0");

  runner.addTest("Builtin - char ord",
                 R"(
            print(char(65));
            print(char(97));
            print(ord("A"));
            print(ord("a"));
            print(ord("0"));
       )",
                 "A\na\n65\n97\n48");

  runner.addTest("Builtin - range",
                 R"(
            list<int> r1 = range(0, 5);
            print(r1.length);
            print(r1[0] .. "," .. r1[4]);

            list<int> r2 = range(1, 10, 2);
            print(r2.length);
            print(r2[0] .. "," .. r2[2]);

            list<int> r3 = range(5, 0, -1);
            print(r3.length);
            print(r3[0] .. "," .. r3[4]);
       )",
                 "5\n0,4\n5\n1,5\n5\n5,1");

  runner.addTest("Builtin - pcall",
                 R"(
            int divide(int a, int b){
              if(b == 0){ error("division by zero"); }
              return a/b;
            }

            mutivar ok2, result2 = pcall(divide, 10, 0);
            print(ok2, result2);
            mutivar ok, result = pcall(divide, 10, 1);
            print(ok, result);
       )",
                 "false division by zero\ntrue 10");
}

// =========================================================
// 13. Defer语句测试 (Defer Statement)
// =========================================================
void registerDeferTests(TestRunner &runner) {
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
}

void registerFiberTests(TestRunner &runner) {

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
  // 闘包与 Fiber
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

// =========================================================
// Stack Reallocation Safety Tests - 栈扩容安全性回归测试
// =========================================================
// 这些测试专门验证栈扩容后指针修复的正确性
// 注意：MAX_FRAMES = 64，所以递归深度不能超过 64 层
// 栈扩容主要测试的是 stack (Value 数组) 的扩容，而不是 frames 的扩容

void registerStackReallocationTests(TestRunner &runner) {

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
  // 6. defer 与栈使用
  // ---------------------------------------------------------

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

  runner.addTest("Stack Realloc - Defer with Closure",
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
}

// =========================================================
// 主函数
// =========================================================
int main(int argc, char *argv[]) {
  TestRunner runner;
  registerBasics(runner);
  registerControlFlow(runner);
  registerFunctions(runner);
  registerClasses(runner);
  registerLists(runner);
  registerMaps(runner);
  registerStrings(runner);
  registerModules(runner);
  registerInvokeTests(runner);
  registerEdgeCases(runner);
  registerIntegrationTests(runner);
  registerBuiltinFunctions(runner);
  registerDeferTests(runner);
  registerFiberTests(runner);
  registerStackReallocationTests(runner);
  runner.runAll();

  //  TestRunner runner;
  //  registerFib38Bench(runner);
  //  runner.runAll();
  return 0;
}