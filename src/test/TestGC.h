#pragma once
#include "TestRunner.h"

// =========================================================
// 15. 垃圾回收测试 (Garbage Collection)
// =========================================================
// 这些测试验证 GC 的正确性，包括：
// - 基本对象回收
// - 循环引用处理
// - 闭包与 UpValue 回收
// - 字符串驻留
// - 容器对象回收
// - Fiber 相关 GC
// - 终结器调用
// - 内存泄露检测

inline void registerGCTests(TestRunner &runner) {

  // ---------------------------------------------------------
  // 1. 基础对象分配与回收
  // ---------------------------------------------------------

  runner.addTest("GC - Basic Object Allocation",
                 R"(
            // 分配大量临时对象，强制触发 GC
            for (int i = 0; i < 10000; i = i + 1) {
                string s = "temp_" .. i;
                list<int> l = [i, i+1, i+2];
                map<string, int> m = {"key": i};
            }
            print("OK");
       )",
                 "OK");

  runner.addTest("GC - Object Survival",
                 R"(
            // 保持引用的对象不应被回收
            list<string> kept = [];
            for (int i = 0; i < 1000; i = i + 1) {
                kept.push("item_" .. i);
                // 创建临时垃圾
                string garbage = "garbage_" .. i .. "_extra_data";
            }
            print(kept.length);
            print(kept[0]);
            print(kept[999]);
       )",
                 "1000\nitem_0\nitem_999");

  // ---------------------------------------------------------
  // 2. 循环引用处理
  // ---------------------------------------------------------

  runner.addTest("GC - Circular Reference Basic",
                 R"(
            class Node {
                any next;
                int value;
                void init(Node this, int v) {
                    this.value = v;
                    this.next = null;
                }
            }

            // 创建循环引用
            for (int i = 0; i < 1000; i = i + 1) {
                Node a = new Node(1);
                Node b = new Node(2);
                a.next = b;
                b.next = a;
                // a 和 b 在循环结束后应该被回收
            }
            print("OK");
       )",
                 "OK");

  runner.addTest("GC - Complex Circular Graph",
                 R"(
            class GraphNode {
                list<any> neighbors;
                int id;
                void init(GraphNode this, int id) {
                    this.id = id;
                    this.neighbors = [];
                }
                void connect(GraphNode this, any other) {
                    this.neighbors.push(other);
                }
            }

            // 创建复杂的循环图
            for (int round = 0; round < 100; round = round + 1) {
                list<any> nodes = [];
                for (int i = 0; i < 10; i = i + 1) {
                    nodes.push(new GraphNode(i));
                }
                // 全连接图
                for (int i = 0; i < 10; i = i + 1) {
                    for (int j = 0; j < 10; j = j + 1) {
                        if (i != j) {
                            nodes[i].connect(nodes[j]);
                        }
                    }
                }
                // nodes 离开作用域后应该被回收
            }
            print("OK");
       )",
                 "OK");

  runner.addTest("GC - Self Reference",
                 R"(
            class SelfRef {
                any self;
                void init(SelfRef this) {
                    this.self = this;
                }
            }

            for (int i = 0; i < 1000; i = i + 1) {
                SelfRef obj = new SelfRef();
                // obj.self 指向自己
            }
            print("OK");
       )",
                 "OK");

  // ---------------------------------------------------------
  // 3. 闭包与 UpValue 回收
  // ---------------------------------------------------------

  runner.addTest("GC - Closure UpValue Basic",
                 R"(
            list<function> closures = [];
            
            for (int i = 0; i < 100; i = i + 1) {
                int captured = i;
                closures.push(function() -> int { return captured; });
            }

            // 验证闭包仍然工作
            int sum = 0;
            for (int j = 0; j < closures.length; j = j + 1) {
                sum = sum + closures[j]();
            }
            // 0+1+2+...+99 = 4950
            print(sum);
       )",
                 "4950");

  runner.addTest("GC - Closure Chain",
                 R"(
            // 闭包链：每个闭包引用下一个
            auto makeChain = function(int depth) -> function {
                if (depth <= 0) {
                    return function() -> int { return 0; };
                }
                int val = depth;
                auto next = makeChain(depth - 1);
                return function() -> int {
                    return val + next();
                };
            };

            auto chain = makeChain(50);
            // 1+2+...+50 = 1275
            print(chain());

            // 创建并丢弃大量链
            for (int i = 0; i < 100; i = i + 1) {
                auto temp = makeChain(20);
                int _ = temp();
            }
            print("OK");
       )",
                 "1275\nOK");

  runner.addTest("GC - Closed UpValue",
                 R"(
            // 测试 UpValue 关闭后的正确性
            auto createClosures = function() -> list<function> {
                list<function> result = [];
                for (int i = 0; i < 10; i = i + 1) {
                    int val = i * 10;
                    result.push(function() -> int { return val; });
                }
                return result;
            };

            list<function> closures = createClosures();
            
            // 强制触发 GC
            for (int i = 0; i < 5000; i = i + 1) {
                string garbage = "garbage_" .. i;
            }

            // 验证闭包仍然正确（upvalue 已关闭）
            int sum = 0;
            for (int j = 0; j < closures.length; j = j + 1) {
                sum = sum + closures[j]();
            }
            // 0+10+20+...+90 = 450
            print(sum);
       )",
                 "450");

  // ---------------------------------------------------------
  // 4. 字符串驻留测试
  // ---------------------------------------------------------

  runner.addTest("GC - String Interning",
                 R"(
            // 创建大量相同的字符串，应该被驻留
            list<string> strings = [];
            for (int i = 0; i < 1000; i = i + 1) {
                strings.push("interned_string");
            }

            // 所有字符串应该相等
            bool allEqual = true;
            for (int i = 1; i < strings.length; i = i + 1) {
                if (strings[i] != strings[0]) {
                    allEqual = false;
                    break;
                }
            }
            print(allEqual);
       )",
                 "true");

  runner.addTest("GC - String Deinterning",
                 R"(
            // 创建临时字符串，应该被回收
            for (int i = 0; i < 10000; i = i + 1) {
                string temp = "unique_string_" .. i .. "_" .. (i * 17);
            }
            print("OK");
       )",
                 "OK");

  // ---------------------------------------------------------
  // 5. 容器对象回收
  // ---------------------------------------------------------

  runner.addTest("GC - List Growth and Shrink",
                 R"(
            list<any> l = [];
            
            // 增长
            for (int i = 0; i < 10000; i = i + 1) {
                l.push("item_" .. i);
            }
            print(l.length);

            // 清空
            l.clear();
            print(l.length);

            // 再次增长（旧数据应该被 GC）
            for (int i = 0; i < 100; i = i + 1) {
                l.push(i);
            }
            print(l.length);
       )",
                 "10000\n0\n100");

  runner.addTest("GC - Map Entry Removal",
                 R"(
            map<string, any> m = {};

            // 添加大量条目
            for (int i = 0; i < 1000; i = i + 1) {
                m["key_" .. i] = {"value": i, "data": "some_data_" .. i};
            }
            print(m.size);

            // 移除一半
            for (int i = 0; i < 500; i = i + 1) {
                m.remove("key_" .. i);
            }
            print(m.size);

            // 强制 GC
            for (int i = 0; i < 5000; i = i + 1) {
                string garbage = "g" .. i;
            }

            print(m.has("key_500"));
            print(m.has("key_0"));
       )",
                 "1000\n500\ntrue\nfalse");

  runner.addTest("GC - Nested Containers",
                 R"(
            // 创建深度嵌套的容器
            for (int round = 0; round < 100; round = round + 1) {
                map<string, any> root = {};
                any current = root;
                
                for (int depth = 0; depth < 10; depth = depth + 1) {
                    map<string, any> child = {"level": depth};
                    current["child"] = child;
                    current = child;
                }
            }
            print("OK");
       )",
                 "OK");

  // ---------------------------------------------------------
  // 6. Fiber 相关 GC
  // ---------------------------------------------------------

  runner.addTest("GC - Fiber Basic",
                 R"(
            // 创建并完成大量 fiber
            for (int i = 0; i < 100; i = i + 1) {
                auto f = Fiber.create(function(int x) -> int {
                    return x * 2;
                });
                int result = f.call(i);
            }
            print("OK");
       )",
                 "OK");

  runner.addTest("GC - Fiber with Closures",
                 R"(
            int counter = 0;

            for (int i = 0; i < 100; i = i + 1) {
                auto f = Fiber.create(function(any _) -> int {
                    counter = counter + 1;
                    list<function> closures = [];
                    for (int j = 0; j < 10; j = j + 1) {
                        int captured = j;
                        closures.push(function() -> int { return captured; });
                    }
                    int sum = 0;
                    for (int k = 0; k < closures.length; k = k + 1) {
                        sum = sum + closures[k]();
                    }
                    return sum;
                });
                f.call(null);
            }
            print(counter);
       )",
                 "100");

  runner.addTest("GC - Suspended Fiber",
                 R"(
            // 创建挂起的 fiber，然后让它们被回收
            list<any> fibers = [];
            
            for (int i = 0; i < 50; i = i + 1) {
                auto f = Fiber.create(function(int x) -> int {
                    Fiber.yield(x);
                    Fiber.yield(x * 2);
                    return x * 3;
                });
                f.call(i);  // 执行到第一个 yield
                fibers.push(f);
            }

            // 只保留部分
            list<any> kept = [];
            for (int i = 0; i < 10; i = i + 1) {
                kept.push(fibers[i]);
            }
            fibers.clear();

            // 强制 GC
            for (int i = 0; i < 5000; i = i + 1) {
                string garbage = "g" .. i;
            }

            // 验证保留的 fiber 仍然工作
            int sum = 0;
            for (int i = 0; i < kept.length; i = i + 1) {
                sum = sum + kept[i].call(0);  // 第二个 yield
            }
            // 0*2 + 1*2 + ... + 9*2 = 90
            print(sum);
       )",
                 "90");

  // ---------------------------------------------------------
  // 7. 类与实例 GC
  // ---------------------------------------------------------

  runner.addTest("GC - Class Instance Basic",
                 R"(
            class Point {
                int x;
                int y;
                void init(Point this, int x, int y) {
                    this.x = x;
                    this.y = y;
                }
            }

            for (int i = 0; i < 10000; i = i + 1) {
                Point p = new Point(i, i * 2);
            }
            print("OK");
       )",
                 "OK");

  runner.addTest("GC - Instance with Container Fields",
                 R"(
            class Container {
                list<any> items;
                map<string, any> data;
                
                void init(Container this) {
                    this.items = [];
                    this.data = {};
                }
                
                void add(Container this, any item) {
                    this.items.push(item);
                    this.data["item_" .. this.items.length] = item;
                }
            }

            for (int round = 0; round < 100; round = round + 1) {
                Container c = new Container();
                for (int i = 0; i < 100; i = i + 1) {
                    c.add("value_" .. i);
                }
            }
            print("OK");
       )",
                 "OK");

  // ---------------------------------------------------------
  // 8. Defer 与 GC 交互
  // ---------------------------------------------------------

  runner.addTest("GC - Defer with Allocations",
                 R"(
            int deferCount = 0;

            void allocateInDefer() {
                defer {
                    // 在 defer 中分配内存
                    list<int> l = [1, 2, 3, 4, 5];
                    string s = "deferred allocation";
                    deferCount = deferCount + 1;
                }
                
                // 触发一些 GC 压力
                for (int i = 0; i < 100; i = i + 1) {
                    string garbage = "garbage_" .. i;
                }
            }

            for (int i = 0; i < 100; i = i + 1) {
                allocateInDefer();
            }
            print(deferCount);
       )",
                 "100");

  // ---------------------------------------------------------
  // 9. 压力测试
  // ---------------------------------------------------------

  runner.addTest("GC - Stress Allocation",
                 R"(
            // 大量混合分配
            for (int i = 0; i < 10000; i = i + 1) {
                string s = "string_" .. i;
                list<any> l = [i, s, true, 3.14];
                map<string, any> m = {"index": i, "data": l};
                
                // 偶尔保持引用
                if (i % 1000 == 0) {
                    print(m["index"]);
                }
            }
       )",
                 "0\n1000\n2000\n3000\n4000\n5000\n6000\n7000\n8000\n9000");

  runner.addTest("GC - Rapid Create Destroy",
                 R"(
            // 快速创建销毁循环
            for (int i = 0; i < 100; i = i + 1) {
                list<any> temp = [];
                for (int j = 0; j < 1000; j = j + 1) {
                    temp.push({"x": j, "y": j * 2});
                }
                // temp 在每次循环结束时被丢弃
            }
            print("OK");
       )",
                 "OK");

  // ---------------------------------------------------------
  // 10. 边界情况
  // ---------------------------------------------------------

  runner.addTest("GC - Empty Collections",
                 R"(
            // 大量空集合
            for (int i = 0; i < 10000; i = i + 1) {
                list<any> l = [];
                map<string, any> m = {};
            }
            print("OK");
       )",
                 "OK");

  runner.addTest("GC - Nil Values",
                 R"(
            list<any> l = [];
            for (int i = 0; i < 1000; i = i + 1) {
                l.push(null);
            }
            
            map<string, any> m = {};
            for (int i = 0; i < 1000; i = i + 1) {
                m["key_" .. i] = null;
            }

            print(l.length);
            print(m.size);
       )",
                 "1000\n1000");

  runner.addTest("GC - Function References",
                 R"(
            // 函数作为值传递
            list<function> funcs = [];
            
            for (int i = 0; i < 100; i = i + 1) {
                auto f = function(int x) -> int { return x * 2; };
                funcs.push(f);
            }

            // 只保留部分
            list<function> kept = [];
            for (int i = 0; i < 10; i = i + 1) {
                kept.push(funcs[i]);
            }
            funcs.clear();

            // 触发 GC
            for (int i = 0; i < 5000; i = i + 1) {
                string garbage = "g" .. i;
            }

            // 验证保留的函数仍然工作
            print(kept[0](5));
       )",
                 "10");

  // ---------------------------------------------------------
  // 11. 模块相关 GC
  // ---------------------------------------------------------

  runner.addModuleTest("GC - Module Allocation", {{"gc_test_mod", R"(
                export list<any> createData(int count) {
                    list<any> result = [];
                    for (int i = 0; i < count; i = i + 1) {
                        result.push({"index": i, "data": "item_" .. i});
                    }
                    return result;
                }
            )"}},
                       R"(
            import { createData } from "gc_test_mod";
            
            // 创建并丢弃大量数据
            for (int i = 0; i < 100; i = i + 1) {
                list<any> temp = createData(100);
            }

            // 保留一些
            list<any> kept = createData(10);
            print(kept.length);
        )",
                       "10");

  // ---------------------------------------------------------
  // 12. 综合场景
  // ---------------------------------------------------------

  runner.addTest("GC - Complex Scenario",
                 R"(
            class Node {
                int value;
                list<any> children;
                any parent;

                void init(Node this, int v) {
                    this.value = v;
                    this.children = [];
                    this.parent = null;
                }

                void addChild(Node this, any child) {
                    this.children.push(child);
                    child.parent = this;
                }
            }

            // 构建树然后丢弃
            for (int round = 0; round < 50; round = round + 1) {
                Node root = new Node(0);
                
                for (int i = 1; i <= 10; i = i + 1) {
                    Node child = new Node(i);
                    root.addChild(child);
                    
                    for (int j = 1; j <= 5; j = j + 1) {
                        Node grandchild = new Node(i * 10 + j);
                        child.addChild(grandchild);
                    }
                }
                
                // 在某些轮次创建闭包引用树
                if (round % 10 == 0) {
                    auto capture = function() -> int {
                        return root.value;
                    };
                    print(capture());
                }
            }
       )",
                 "0\n0\n0\n0\n0");
}

// =========================================================
// GC 调试辅助测试
// =========================================================
// 这些测试用于验证 GC 调试模式的正确性
// 需要配合 VM 的调试输出使用

inline void registerGCDebugTests(TestRunner &runner) {

  runner.addTest("GCDebug - Allocation Tracking",
                 R"(
            // 简单分配，用于验证调试输出
            string s = "test";
            list<int> l = [1, 2, 3];
            map<string, int> m = {"a": 1};
            print("OK");
       )",
                 "OK");

  runner.addTest("GCDebug - Explicit Collection Point",
                 R"(
            // 创建可预测的垃圾
            for (int i = 0; i < 100; i = i + 1) {
                string garbage = "garbage_" .. i;
            }
            // 此时应该触发 GC（取决于阈值）
            print("After garbage");

            // 创建存活对象
            list<int> alive = [1, 2, 3];
            print(alive.length);
       )",
                 "After garbage\n3");
}
