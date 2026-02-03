#pragma once
#include "TestRunner.h"

// =========================================================
// 4. 类与对象 (Classes & Objects)
// =========================================================

inline void registerClasses(TestRunner &runner) {
  runner.addTest("Class Basic",
                 R"(
            class Point {
                int x;
                int y;
                void __init(int x, int y) {
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
                void __init(int start) {
                    this.value = start;
                }
                void increment() {
                    this.value = this.value + 1;
                }
                void add(int n) {
                    this.value = this.value + n;
                }
                int get() {
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
                void __init(int x, int y) {
                    this.x = x;
                    this.y = y;
                }
                void move(int dx, int dy) {
                    this.x = this.x + dx;
                    this.y = this.y + dy;
                }
                void scale(int factor) {
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
                void __init(int v) {
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
                void __init(any d) {
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
                void __init(int v) {
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
                void __init(int v) {
                    this.value = v;
                }
                int double() {
                    return this.value * 2;
                }
                int addTo(int other) {
                    return this.value + other;
                }
            }
            Calculator calc = new Calculator(15);
            print(calc.double());
            print(calc.addTo(10));
       )",
                 "30\n25");

  runner.addTest("Method Returning Values",
                 R"(
            class Calculator {
                vars values() {
                    return 1,2;
                }
            }
            Calculator calc = new Calculator();
            print(calc.values());

       )",
                 "1 2");

  runner.addTest("Nested Object Access",
                 R"(
            class Inner {
                int value;
                void __init(int v) {
                    this.value = v;
                }
            }
            class Outer {
                any inner;
                void __init(int v) {
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
                void __init(int v) {
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
