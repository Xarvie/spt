#pragma once
#include "TestRunner.h"

// =========================================================
// 11. 综合测试 (Integration Tests)
// =========================================================

inline void registerIntegrationTests(TestRunner &runner) {
  runner.addTest("Integration - Simple Calculator",
                 R"(
            class Calculator {
                int value;
                void __init() {
                    this.value = 0;
                }
                void set(int v) {
                    this.value = v;
                }
                void add(int v) {
                    this.value = this.value + v;
                }
                void sub(int v) {
                    this.value = this.value - v;
                }
                void mul(int v) {
                    this.value = this.value * v;
                }
                int result() {
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
                void __init(int v) {
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
                void __init() {
                    this.listeners = [];
                }
                void on(function callback) {
                    this.listeners.push(callback);
                }
                void emit(any data) {
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
                void __init() {
                    this.state = "idle";
                }
                void transition(string event) {
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
                string getState() {
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
