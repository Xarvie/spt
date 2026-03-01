#pragma once
#include "TestRunner.h"

inline void registerListMapTest(TestRunner &runner) {

  // =======================================================
  // LIST 基础功能测试
  // =======================================================

  // ---------------------------------------------------------
  // List: 空 List 测试
  // ---------------------------------------------------------
  runner.addTest("List: Empty List",
                 R"(
            list<any> l = [];
            print(#l);
            print(l == null);
       )",
                 "0\nfalse");

  // ---------------------------------------------------------
  // List: 基本访问
  // ---------------------------------------------------------
  runner.addTest("List: Basic Access",
                 R"(
            list<int> l = [10, 20, 30, 40, 50];
            print(l[0]);
            print(l[2]);
            print(l[4]);
            print(#l);
       )",
                 "10\n30\n50\n5");

  // ---------------------------------------------------------
  // List: 基本修改
  // ---------------------------------------------------------
  runner.addTest("List: Basic Modification",
                 R"(
            list<int> l = [1, 2, 3];
            l[0] = 100;
            l[1] = 200;
            l[2] = 300;
            print(l[0]);
            print(l[1]);
            print(l[2]);
            print(#l);
       )",
                 "100\n200\n300\n3");

  // ---------------------------------------------------------
  // List: 类型一致性
  // ---------------------------------------------------------
  runner.addTest("List: Type Consistency",
                 R"(
            list<int> nums = [1, 2, 3];
            print(nums[0]);
            print(nums[1]);
            print(nums[2]);

            list<string> strs = ["a", "b", "c"];
            print(strs[0]);
            print(strs[2]);
       )",
                 "1\n2\n3\na\nc");

  // ---------------------------------------------------------
  // List: 混合类型（使用 any）
  // ---------------------------------------------------------
  runner.addTest("List: Mixed Types",
                 R"(
            list<any> mixed = [1, "hello", true, 3.14, null];
            print(mixed[0]);
            print(mixed[1]);
            print(mixed[2]);
            print(mixed[3]);
            print(mixed[4]);
            print(#mixed);
       )",
                 "1\nhello\ntrue\n3.14\nnil\n5");

  // ---------------------------------------------------------
  // List: Nil 元素处理
  // ---------------------------------------------------------
  runner.addTest("List: Nil Elements",
                 R"(
            list<any> l = [null, null, 42, null];
            print(#l);
            print(l[0]);
            print(l[1]);
            print(l[2]);
            print(l[3]);
       )",
                 "4\nnil\nnil\n42\nnil");

  runner.addTest("List: All Nil Elements",
                 R"(
            list l = [null, null, null];
            print(#l);
            for (i = 0, #l - 1) {
                if (l[i] == null) {
                    print("nil");
                }
            }
       )",
                 "3\nnil\nnil\nnil");

  // ---------------------------------------------------------
  // List: 嵌套 List
  // ---------------------------------------------------------
  runner.addTest("List: Nested Lists",
                 R"(
            list<list<int>> matrix = [[1, 2], [3, 4], [5, 6]];
            print(#matrix);
            print(#matrix[0]);
            print(matrix[0][0]);
            print(matrix[0][1]);
            print(matrix[1][0]);
            print(matrix[2][1]);
       )",
                 "3\n2\n1\n2\n3\n6");

  runner.addTest("List: Deep Nesting",
                 R"(
            list<list<list<int>>> deep = [[[1, 2], [3, 4]], [[5, 6], [7, 8]]];
            print(deep[0][0][0]);
            print(deep[0][1][1]);
            print(deep[1][0][0]);
            print(deep[1][1][1]);
       )",
                 "1\n4\n5\n8");

  runner.addTest("List: Modify Nested Elements",
                 R"(
            list<list<int>> matrix = [[1, 2], [3, 4]];
            matrix[0][1] = 99;
            print(matrix[0][0]);
            print(matrix[0][1]);
            print(matrix[1][0]);
       )",
                 "1\n99\n3");

  // =======================================================
  // LIST 迭代测试
  // =======================================================

  runner.addTest("List: For Loop Iteration",
                 R"(
            list<int> l = [10, 20, 30];
            for (i = 0, #l - 1) {
                print(l[i]);
            }
       )",
                 "10\n20\n30");

  runner.addTest("List: Iteration with Nil",
                 R"(
            list<any> l = [1, null, 3];
            for (i = 0, #l - 1) {
                if (l[i] == null) {
                    print("nil");
                } else {
                    print(l[i]);
                }
            }
       )",
                 "1\nnil\n3");

  runner.addTest("List: Reverse Iteration",
                 R"(
            list<int> l = [1, 2, 3, 4, 5];
            for (i = #l - 1, 0, -1) {
                print(l[i]);
            }
       )",
                 "5\n4\n3\n2\n1");

  runner.addTest("List: Pairs Iteration",
                 R"(
            list<int> l = [10, 20, 30];
            for (k, v : pairs(l)) {
                print(k .. ":" .. v);
            }
       )",
                 "0:10\n1:20\n2:30");

  runner.addTest("List: Pairs Empty",
                 R"(
            list<any> l = [];
            int count = 0;
            for (k, v : pairs(l)) {
                count += 1;
            }
            print(count);
       )",
                 "0");

  // =======================================================
  // LIST PUSH/POP 操作测试
  // =======================================================

  runner.addTest("List: Basic Push",
                 R"(
            list<any> l = [];
            table.push(l, 10);
            table.push(l, 20);
            table.push(l, 30);
            print(#l);
            print(l[0]);
            print(l[1]);
            print(l[2]);
       )",
                 "3\n10\n20\n30");

  runner.addTest("List: Basic Pop",
                 R"(
            list<int> l = [1, 2, 3];
            any val = table.pop(l);
            print(val);
            print(#l);
            val = table.pop(l);
            print(val);
            print(#l);
       )",
                 "3\n2\n2\n1");

  runner.addTest("List: Pop Empty",
                 R"(
            list<any> l = [];
            any val = table.pop(l);
            print(val);
            print(#l);
       )",
                 "nil\n0");

  runner.addTest("List: Push Pop Alternating",
                 R"(
            list<any> l = [];
            table.push(l, 1);
            table.push(l, 2);
            print(table.pop(l));
            table.push(l, 3);
            print(table.pop(l));
            print(table.pop(l));
            print(#l);
       )",
                 "2\n3\n1\n0");

  runner.addTest("List: Large Push Pop",
                 R"(
            list<any> l = [];
            for (i = 0, 99) {
                table.push(l, i);
            }
            print(#l);

            int sum = 0;
            for (i = 0, 49) {
                sum += table.pop(l);
            }
            print(#l);
            print(sum);
       )",
                 "100\n50\n3725");

  runner.addTest("List: Push Different Types",
                 R"(
            list<any> l = [];
            table.push(l, 42);
            table.push(l, "hello");
            table.push(l, true);
            table.push(l, null);
            print(#l);
            print(l[0]);
            print(l[1]);
            print(l[2]);
            print(l[3]);
       )",
                 "4\n42\nhello\ntrue\nnil");

  runner.addTest("List: Push Pop Maintains Order",
                 R"(
            list<any> l = [];
            for (i = 1, 5) {
                table.push(l, i * 10);
            }
            any result = [];
            for (i = 0, 4) {
                table.push(result, table.pop(l));
            }
            for (i = 0, #result - 1) {
                print(result[i]);
            }
       )",
                 "50\n40\n30\n20\n10");

  // =======================================================
  // LIST TABLE 库函数测试
  // =======================================================

  runner.addTest("List: table.insert",
                 R"(
            list<int> l = [1, 2, 3];
            table.insert(l, 1, 99);
            print(#l);
            print(l[0]);
            print(l[1]);
            print(l[2]);
            print(l[3]);
       )",
                 "4\n1\n99\n2\n3");

  runner.addTest("List: table.remove",
                 R"(
            list<int> l = [10, 20, 30, 40];
            any val = table.remove(l, 1);
            print(val);
            print(#l);
            print(l[0]);
            print(l[1]);
            print(l[2]);
       )",
                 "20\n3\n10\n30\n40");

  runner.addTest("List: table.concat",
                 R"(
            list<int> l = [1, 2, 3, 4, 5];
            any result = table.concat(l, ", ");
            print(result);
       )",
                 "1, 2, 3, 4, 5");

  runner.addTest("List: table.sort",
                 R"(
            list<int> l = [5, 2, 8, 1, 9, 3];
            table.sort(l);
            for (i = 0, #l - 1) {
                print(l[i]);
            }
       )",
                 "1\n2\n3\n5\n8\n9");

  runner.addTest("List: table.move",
                 R"(
            list<int> l1 = [1, 2, 3];
            list<int> l2 = [0, 0, 0, 0, 0];
            table.move(l1, 0, 2, 1, l2);
            print(l2[0]);
            print(l2[1]);
            print(l2[2]);
            print(l2[3]);
       )",
                 "0\n1\n2\n3");

  // =======================================================
  // LIST 函数参数和返回值测试
  // =======================================================

  runner.addTest("List: As Function Parameter",
                 R"(
            int sum(list arr) {
                int total = 0;
                for (i = 0, #arr - 1) {
                    total += arr[i];
                }
                return total;
            }

            list<int> nums = [1, 2, 3, 4, 5];
            print(sum(nums));
       )",
                 "15");

  runner.addTest("List: Return from Function",
                 R"(
            list makeList() {
                return [100, 200, 300];
            }

            any result = makeList();
            print(result[0]);
            print(result[2]);
            print(#result);
       )",
                 "100\n300\n3");

  runner.addTest("List: Modify in Function",
                 R"(
            void modifyList(list arr) {
                arr[0] = 999;
                table.push(arr, 888);
            }

            list<int> l = [1, 2, 3];
            modifyList(l);
            print(l[0]);
            print(#l);
            print(l[3]);
       )",
                 "999\n4\n888");

  runner.addTest("List: Factory Function",
                 R"(
            list createRange(int start, int end) {
                list result = [];
                for (i = start, end) {
                    table.push(result, i);
                }
                return result;
            }

            any range = createRange(5, 10);
            print(#range);
            print(range[0]);
            print(range[5]);
       )",
                 "6\n5\n10");

  // =======================================================
  // LIST 引用行为测试
  // =======================================================

  runner.addTest("List: Reference Behavior",
                 R"(
            list<int> a = [1, 2, 3];
            list<int> b = a;
            b[0] = 99;
            print(a[0]);
            print(b[0]);
       )",
                 "99\n99");

  runner.addTest("List: Reference with Push",
                 R"(
            list<int> a = [1, 2];
            list<int> b = a;
            table.push(b, 3);
            print(#a);
            print(#b);
            print(a[2]);
       )",
                 "3\n3\n3");

  runner.addTest("List: Comparison",
                 R"(
            list<int> a = [1, 2, 3];
            list<int> b = [1, 2, 3];
            list<int> c = a;

            print(a == c);
            print(a == b);
       )",
                 "true\nfalse");

  // =======================================================
  // LIST 边界和错误测试
  // =======================================================

  runner.addFailTest("List Error: Negative Index Read",
                     R"(
             list<int> l = [1, 2, 3];
             print(l[-1]);
        )");

  runner.addFailTest("List Error: Out of Bounds Read",
                     R"(
             list<int> l = [1, 2, 3];
             print(l[3]);
        )");

  runner.addFailTest("List Error: Float Index",
                     R"(
             list<int> l = [1, 2, 3];
             print(l[1.5]);
        )");

  runner.addFailTest("List Error: Negative Index Write",
                     R"(
             list<int> l = [1, 2, 3];
             l[-1] = 100;
        )");

  runner.addFailTest("List Error: Out of Bounds Write",
                     R"(
             list<int> l = [1, 2, 3];
             l[3] = 100;
        )");

  runner.addTest("List: Fixed Length After Creation",
                 R"(
            list<int> l = [1, 2, 3];
            print(#l);
            l[0] = 100;
            l[1] = 200;
            l[2] = 300;
            print(#l);
       )",
                 "3\n3");

  // =======================================================
  // MAP 基础功能测试
  // =======================================================

  runner.addTest("Map: Empty Map",
                 R"(
            map<any, any> m = {};
            print(#m);
            print(m == null);
       )",
                 "0\nfalse");

  runner.addTest("Map: String Keys",
                 R"(
            map<string, string> m = {"name":"Alice", "age":"30", "city":"NYC"};
            print(m["name"]);
            print(m["age"]);
            print(m["city"]);
       )",
                 "Alice\n30\nNYC");

  runner.addTest("Map: String Keys 2",
                 R"(
            map<any, string> m = {1:"one", 2:"two", 3:"three"};
            print(m["1"]);
            print(m["2"]);
            print(m["3"]);
       )",
                 "one\ntwo\nthree");

  runner.addTest("Map: Integer Keys",
                 R"(
            map<int, string> m = {[1]:"one", [2]:"two", [3]:"three"};
            print(m[1]);
            print(m[2]);
            print(m[3]);
       )",
                 "one\ntwo\nthree");
  runner.addTest("Map: Mixed Key Types",
                 R"(
            map<any, any> m = {};
            m[1] = "int key";
            m["str"] = "string key";
            m[true] = "bool key";

            print(m[1]);
            print(m["str"]);
            print(m[true]);
       )",
                 "int key\nstring key\nbool key");

  runner.addTest("Map: Float Keys",
                 R"(
            map<any, any> m = {};
            m[3.14] = "pi";
            m[2.71] = "e";

            print(m[3.14]);
            print(m[2.71]);
       )",
                 "pi\ne");

  runner.addTest("Map: Negative Keys",
                 R"(
            map<any, any> m = {};
            m[-1] = "negative";
            m[-100] = "very negative";

            print(m[-1]);
            print(m[-100]);
       )",
                 "negative\nvery negative");

  // =======================================================
  // MAP 操作测试
  // =======================================================

  runner.addTest("Map: Delete Key",
                 R"(
            map<string, string> m = {"1":"10", "2":"20", "3":"30"};
            print(m["1"]);
            m["1"] = null;
            print(m["1"]);
       )",
                 "10\nnil");

  runner.addTest("Map: Non-existent Key",
                 R"(
            map<string, string> m = {"1":"10"};
            print(m["2"]);
            print(m["1"]);
       )",
                 "nil\n10");

  runner.addTest("Map: Overwrite Existing Key",
                 R"(
            map m = {key:"old"};
            print(m["key"]);
            m["key"] = "new";
            print(m["key"]);
       )",
                 "old\nnew");

  runner.addTest("Map: Dynamic Key Addition",
                 R"(
            map<any, any> m = {};
            m["key1"] = "value1";
            print(m["key1"]);

            m["key2"] = "value2";
            print(m["key2"]);

            m[3] = "value3";
            print(m[3]);
       )",
                 "value1\nvalue2\nvalue3");

  runner.addTest("Map: Sparse Array Behavior",
                 R"(
            map<any, any> m = {};
            m[1] = "a";
            m[100] = "b";
            m[1000] = "c";

            print(m[1]);
            print(m[100]);
            print(m[1000]);
            print(m[2]);
       )",
                 "a\nb\nc\nnil");

  // =======================================================
  // MAP 嵌套测试
  // =======================================================

  runner.addTest("Map: Nested Maps",
                 R"(
            map<string, any> m = {
                "user1": {"name":"Alice", "age":"25"},
                "user2": {"name":"Bob", "age":"30"}
            };

            print(m["user1"]["name"]);
            print(m["user1"]["age"]);
            print(m["user2"]["name"]);
       )",
                 "Alice\n25\nBob");

  runner.addTest("Map: Modify Nested Values",
                 R"(
            map<string, any> m = {"data": {"x":"10", "y":"20"}};
            m["data"]["x"] = "99";
            print(m["data"]["x"]);
            print(m["data"]["y"]);
       )",
                 "99\n20");

  runner.addTest("Map: Deep Nesting",
                 R"(
            map<string, any> m = {
                "level1": {
                    "level2": {
                        "level3": {
                            "value": "deep"
                        }
                    }
                }
            };
            print(m["level1"]["level2"]["level3"]["value"]);
       )",
                 "deep");

  // =======================================================
  // MAP 迭代测试
  // =======================================================

  runner.addTest("Map: Pairs Iteration",
                 R"(
            map<string, string> m = {"a":"1", "b":"2", "c":"3"};
            int count = 0;
            for (k, v : pairs(m)) {
                count += 1;
            }
            print(count);
       )",
                 "3");

  runner.addTest("Map: Pairs Empty",
                 R"(
            map<any, any> m = {};
            int count = 0;
            for (k, v : pairs(m)) {
                count += 1;
            }
            print(count);
       )",
                 "0");

  // =======================================================
  // MAP 函数参数和返回值测试
  // =======================================================

  runner.addTest("Map: As Function Parameter",
                 R"(
            any getValue(any dict, any key) {
                return dict[key];
            }

            map<string, string> m = {"a":"100", "b":"200"};
            print(getValue(m, "a"));
            print(getValue(m, "b"));
       )",
                 "100\n200");

  runner.addTest("Map: Return from Function",
                 R"(
            map makeMap() {
                return {"x":"10", "y":"20"};
            }

            map result = makeMap();
            print(result["x"]);
            print(result["y"]);
       )",
                 "10\n20");

  runner.addTest("Map: Modify in Function",
                 R"(
            void modifyMap(map m) {
                m["new"] = "added";
            }

            map<string, string> m = {"old":"value"};
            modifyMap(m);
            print(m["old"]);
            print(m["new"]);
       )",
                 "value\nadded");

  // =======================================================
  // MAP 引用行为测试
  // =======================================================

  runner.addTest("Map: Reference Behavior",
                 R"(
            map<string, string> a = {"key":"value1"};
            list<int> b = a;
            b["key"] = "value2";
            print(a["key"]);
            print(b["key"]);
       )",
                 "value2\nvalue2");

  runner.addTest("Map: Float Index Access",
                 R"(
            map<any, any> m = {};
            m[0] = "zero";
            m[1] = "one";
            print(m[0]);
            print(m[1]);
       )",
                 "zero\none");

  // =======================================================
  // LIST 和 MAP 混合测试
  // =======================================================

  runner.addTest("List of Maps",
                 R"(
            list<any> users = [
                {"name":"Alice", "age":"25"},
                {"name":"Bob", "age":"30"},
                {"name":"Charlie", "age":"35"}
            ];

            print(users[0]["name"]);
            print(users[1]["age"]);
            print(users[2]["name"]);
       )",
                 "Alice\n30\nCharlie");

  runner.addTest("Map of Lists",
                 R"(
            map<string, any> data = {
                "numbers": [1, 2, 3],
                "strings": ["a", "b", "c"]
            };

            print(data["numbers"][0]);
            print(data["numbers"][2]);
            print(data["strings"][1]);
       )",
                 "1\n3\nb");

  runner.addTest("Complex Nested Structure",
                 R"(
            map<string, any> complex = {
                "users": [
                    {"name":"Alice", "scores":[85, 90, 95]},
                    {"name":"Bob", "scores":[75, 80, 85]}
                ]
            };

            print(complex["users"][0]["name"]);
            print(complex["users"][0]["scores"][0]);
            print(complex["users"][1]["scores"][2]);
       )",
                 "Alice\n85\n85");

  runner.addTest("Modify Mixed Structure",
                 R"(
            map<string, any> data = {
                "items": [10, 20, 30]
            };

            data["items"][1] = 99;
            print(data["items"][0]);
            print(data["items"][1]);
            print(data["items"][2]);
       )",
                 "10\n99\n30");

  runner.addTest("Mixed Structure with Push",
                 R"(
            map<string, any> data = {
                "items": [1, 2, 3]
            };
            table.push(data["items"], 4);
            print(#data["items"]);
            print(data["items"][3]);
       )",
                 "4\n4");

  runner.addTest("List in Map with Pop",
                 R"(
            map<string, any> data = {
                "stack": [10, 20, 30]
            };
            any val = table.pop(data["stack"]);
            print(val);
            print(#data["stack"]);
       )",
                 "30\n2");

  // =======================================================
  // 性能和压力测试
  // =======================================================

  runner.addTest("List: Large List Creation",
                 R"(
            list<any> l = [];
            for (i = 0, 999) {
                table.push(l, i);
            }
            print(#l);
            print(l[0]);
            print(l[500]);
            print(l[999]);
       )",
                 "1000\n0\n500\n999");

  runner.addTest("Map: Large Map Creation",
                 R"(
            map<any, any> m = {};
            for (i = 0, 999) {
                m[i] = i * 2;
            }
            print(m[0]);
            print(m[500]);
            print(m[999]);
       )",
                 "0\n1000\n1998");

  runner.addTest("List: Memory Efficiency Test",
                 R"(
            list<any> lists = [];
            for (i = 0, 9) {
                list<any> l = [];
                for (j = 0, 99) {
                    table.push(l, j);
                }
                table.push(lists, l);
            }
            print(#lists);
            print(#lists[0]);
            print(lists[5][50]);
       )",
                 "10\n100\n50");

  // =======================================================
  // 实用场景测试
  // =======================================================

  runner.addTest("Stack Implementation",
                 R"(
            list<any> stack = [];

            table.push(stack, 1);
            table.push(stack, 2);
            table.push(stack, 3);

            print(table.pop(stack));
            print(table.pop(stack));

            table.push(stack, 4);

            print(table.pop(stack));
            print(table.pop(stack));
            print(#stack);
       )",
                 "3\n2\n4\n1\n0");

  runner.addTest("Queue-like Behavior",
                 R"(
            list<any> queue = [];

            // Enqueue
            table.push(queue, "first");
            table.push(queue, "second");
            table.push(queue, "third");

            // Dequeue (using remove at index 0)
            any item = table.remove(queue, 0);
            print(item);
            print(#queue);
            print(queue[0]);
       )",
                 "first\n2\nsecond");

  runner.addTest("Simple Cache Implementation",
                 R"(
            map<string, any> cache = {};

            cache["user:1"] = "Alice";
            cache["user:2"] = "Bob";
            cache["user:3"] = "Charlie";

            print(cache["user:2"]);

            // Invalidate
            cache["user:2"] = null;
            print(cache["user:2"]);
       )",
                 "Bob\nnil");

  runner.addTest("Graph Adjacency List",
                 R"(
            map<string, any> graph = {
                "A": ["B", "C"],
                "B": ["A", "D"],
                "C": ["A"],
                "D": ["B"]
            };

            print(#graph["A"]);
            print(graph["A"][0]);
            print(graph["B"][1]);
       )",
                 "2\nB\nD");

  runner.addTest("Configuration System",
                 R"(
            map<string, any> config = {
                "database": {
                    "host": "localhost",
                    "port": "5432",
                    "name": "mydb"
                },
                "cache": {
                    "enabled": "true",
                    "ttl": "3600"
                }
            };

            print(config["database"]["host"]);
            print(config["cache"]["enabled"]);
       )",
                 "localhost\ntrue");

  // =======================================================
  // 特殊情况测试
  // =======================================================

  runner.addTest("String Concatenation with List Elements",
                 R"(
            list<int> l = [1, 2, 3];
            any result = "Values: " .. l[0] .. ", " .. l[1] .. ", " .. l[2];
            print(result);
       )",
                 "Values: 1, 2, 3");

  runner.addTest("Check if Containers are Empty",
                 R"(
            list<any> l = [];
            if (#l == 0) {
                print("List is empty");
            }

            map<any, any> m = {};
            if (#m == 0) {
                print("Map is empty");
            }

            list<int> l2 = [1];
            if (#l2 > 0) {
                print("List has elements");
            }
       )",
                 "List is empty\nMap is empty\nList has elements");

  runner.addTest("Direct Assignment of Containers",
                 R"(
            list<int> l1 = [1, 2, 3];
            list<int> l2 = l1;
            map<string, string> m1 = {"a":"1"};
            map<string, string> m2 = m1;

            print(l2[0]);
            print(m2["a"]);
       )",
                 "1\n1");

  runner.addTest("Boolean Values as Keys",
                 R"(
            map<any, any> m = {};
            m[true] = "yes";
            m[false] = "no";
            print(m[true]);
            print(m[false]);
       )",
                 "yes\nno");

  runner.addTest("Null as Map Value",
                 R"(
            map<string, any> m = {"key": null};
            print(m["key"]);
            if (m["key"] == null) {
                print("is null");
            }
       )",
                 "nil\nis null");

  runner.addTest("Empty List in Map",
                 R"(
            any m = {"emptyList": []};
            print(#m["emptyList"]);
            table.push(m["emptyList"], 42);
            print(#m["emptyList"]);
            print(m["emptyList"][0]);
       )",
                 "0\n1\n42");

  // =======================================================
  // 综合应用测试
  // =======================================================

  runner.addTest("Todo List Application",
                 R"(
            list<any> todos = [];

            table.push(todos, {"task": "Buy milk", "done": "false"});
            table.push(todos, {"task": "Call mom", "done": "false"});
            table.push(todos, {"task": "Write code", "done": "true"});

            print(#todos);
            print(todos[0]["task"]);

            todos[1]["done"] = "true";
            print(todos[1]["done"]);
       )",
                 "3\nBuy milk\ntrue");

  runner.addTest("Student Grade System",
                 R"(
            map<string, any> students = {
                "student1": {
                    "name": "Alice",
                    "grades": [85, 90, 88]
                },
                "student2": {
                    "name": "Bob",
                    "grades": [78, 82, 85]
                }
            };

            // Calculate average for student1
            list<int> grades = students["student1"]["grades"];
            int sum = 0;
            for (i = 0, #grades - 1) {
                sum += grades[i];
            }
            float avg = sum / #grades;
            print(avg);
       )",
                 "87.666666666667");

  runner.addTest("Inventory System",
                 R"(
            map<string, any> inventory = {
                "items": [],
                "count": "0"
            };

            table.push(inventory["items"], {"id": "1", "name": "Sword"});
            table.push(inventory["items"], {"id": "2", "name": "Shield"});
            inventory["count"] = "2";

            print(inventory["count"]);
            print(inventory["items"][0]["name"]);
            print(inventory["items"][1]["name"]);
       )",
                 "2\nSword\nShield");
}

inline void registerListMapFullTest(TestRunner &runner) {

  // =======================================================
  // 第一组：loglen / asize 分离 —— 基础语义验证
  // =======================================================

  // 空 list，loglen=0
  runner.addTest("Boundary: Empty list length is 0",
                 R"(
    list<any> l = [];
    print(#l);
  )",
                 "0");

  // 字面量初始化后 loglen == 元素个数
  runner.addTest("Boundary: Literal init loglen equals element count",
                 R"(
    list<int> l = [10, 20, 30, 40, 50];
    print(#l);
  )",
                 "5");

  // push 后 loglen 增长，不是 asize
  runner.addTest("Boundary: Push increments loglen not asize",
                 R"(
    list<any> l = [];
    table.push(l, 1);
    print(#l);
    table.push(l, 2);
    print(#l);
    table.push(l, 3);
    print(#l);
  )",
                 "1\n2\n3");

  // pop 后 loglen 减少
  runner.addTest("Boundary: Pop decrements loglen",
                 R"(
    list<int> l = [1, 2, 3];
    print(#l);
    table.pop(l);
    print(#l);
    table.pop(l);
    print(#l);
  )",
                 "3\n2\n1");

  // push 再 pop 到 0，loglen 归零
  runner.addTest("Boundary: Push then pop all, loglen back to 0",
                 R"(
    list<any> l = [];
    table.push(l, 100);
    table.push(l, 200);
    table.pop(l);
    table.pop(l);
    print(#l);
  )",
                 "0");

  // =======================================================
  // 第二组：amortized growth —— 扩容路径验证
  // =======================================================

  // 从 0 开始 push 超过初始容量（触发多次扩容），loglen 始终正确
  runner.addTest("Boundary: Amortized growth 1..16 loglen correct",
                 R"(
    list<any> l = [];
    for (i = 0, 15) {
        table.push(l, i);
    }
    print(#l);
    print(l[0]);
    print(l[7]);
    print(l[15]);
  )",
                 "16\n0\n7\n15");

  // push 100 个元素，验证 loglen 和内容完整性
  runner.addTest("Boundary: Amortized growth 100 elements integrity",
                 R"(
    list<any> l = [];
    for (i = 0, 99) {
        table.push(l, i * 3);
    }
    print(#l);
    print(l[0]);
    print(l[49]);
    print(l[99]);
  )",
                 "100\n0\n147\n297");

  // 扩容后读取边界元素（loglen-1），不能读到 asize 区域
  runner.addTest("Boundary: After growth, last valid index is loglen-1",
                 R"(
    list<any> l = [];
    for (i = 0, 7) {
        table.push(l, i + 10);
    }
    print(l[7]);
    print(#l);
  )",
                 "17\n8");

  // 扩容后立刻 pop，loglen 正确缩减
  runner.addTest("Boundary: Growth then immediate pop",
                 R"(
    list<any> l = [];
    for (i = 0, 9) {
        table.push(l, i);
    }
    any v = table.pop(l);
    print(v);
    print(#l);
  )",
                 "9\n9");

  // =======================================================
  // 第三组：[loglen, asize) 区间 GC 安全 —— pop 后槽位不可读
  // =======================================================

  // pop 之后原位置不再属于 list，越界读取应报错
  runner.addFailTest("Boundary: Read popped slot is out of bounds",
                     R"(
    list<int> l = [1, 2, 3];
    table.pop(l);
    print(l[2]);
  )");

  // remove 之后 loglen 缩减，原末尾不可读
  runner.addFailTest("Boundary: Read after remove shrinks loglen",
                     R"(
    list<int> l = [10, 20, 30];
    table.remove(l, 0);
    print(l[2]);
  )");

  // pop 到 0 后不能读任何位置
  runner.addFailTest("Boundary: Read index 0 on empty list after pop all",
                     R"(
    list<int> l = [5];
    table.pop(l);
    print(l[0]);
  )");

  // =======================================================
  // 第四组：越界写入必须报错
  // =======================================================

  // 写 loglen 位置（追加路径以外不允许直接赋值）
  runner.addFailTest("Boundary: Write at index == loglen via direct assign",
                     R"(
    list<int> l = [1, 2, 3];
    l[3] = 99;
  )");

  // 写负数下标
  runner.addFailTest("Boundary: Write negative index",
                     R"(
    list<int> l = [1, 2, 3];
    l[-1] = 0;
  )");

  // 写远超 loglen 的位置
  runner.addFailTest("Boundary: Write far out of bounds",
                     R"(
    list<int> l = [1, 2, 3];
    l[100] = 0;
  )");

  // 非整数下标写入
  runner.addFailTest("Boundary: Write float index",
                     R"(
    list<int> l = [1, 2, 3];
    l[1.5] = 99;
  )");

  // 字符串下标写入 list
  runner.addFailTest("Boundary: Write string key to list",
                     R"(
    list<any> l = [1, 2];
    l["key"] = 99;
  )");

  // =======================================================
  // 第五组：越界读取必须报错
  // =======================================================

  runner.addFailTest("Boundary: Read index == loglen",
                     R"(
    list<int> l = [1, 2, 3];
    print(l[3]);
  )");

  runner.addFailTest("Boundary: Read negative index",
                     R"(
    list<int> l = [1, 2, 3];
    print(l[-1]);
  )");

  runner.addFailTest("Boundary: Read large out of bounds",
                     R"(
    list<int> l = [1, 2, 3];
    print(l[999]);
  )");

  runner.addFailTest("Boundary: Read float index",
                     R"(
    list<int> l = [1, 2, 3];
    print(l[0.5]);
  )");

  // =======================================================
  // 第六组：#list 语义 —— 始终返回 loglen
  // =======================================================

  // 修改元素不改变 loglen
  runner.addTest("Boundary: Modify elements does not change loglen",
                 R"(
    list<int> l = [1, 2, 3, 4, 5];
    l[0] = 100;
    l[4] = 500;
    print(#l);
  )",
                 "5");

  // 交替 push/pop，# 始终正确
  runner.addTest("Boundary: Interleaved push pop hash correct",
                 R"(
    list<any> l = [];
    table.push(l, 1);
    table.push(l, 2);
    table.push(l, 3);
    table.pop(l);
    print(#l);
    table.push(l, 4);
    print(#l);
    table.pop(l);
    table.pop(l);
    print(#l);
  )",
                 "2\n3\n1");

  // table.insert 后 # 增加 1
  runner.addTest("Boundary: Insert increases loglen by 1",
                 R"(
    list<int> l = [10, 20, 30];
    table.insert(l, 1, 99);
    print(#l);
  )",
                 "4");

  // table.remove 后 # 减少 1
  runner.addTest("Boundary: Remove decreases loglen by 1",
                 R"(
    list<int> l = [10, 20, 30, 40];
    table.remove(l, 0);
    print(#l);
  )",
                 "3");

  // table.move 目标 list 的 # 不变（move 不改变目标 loglen）
  runner.addTest("Boundary: Move does not change source loglen",
                 R"(
    list<int> src = [1, 2, 3];
    list<int> dst = [0, 0, 0, 0, 0];
    table.move(src, 0, 2, 0, dst);
    print(#src);
    print(#dst);
  )",
                 "3\n5");

  // =======================================================
  // 第七组：table.pack / table.unpack loglen 语义
  // =======================================================

  // pack 创建的 list，# 等于参数数量
  runner.addTest("Boundary: Pack loglen equals argument count",
                 R"(
    any l = table.pack(10, 20, 30, 40);
    print(#l);
    print(l[0]);
    print(l[3]);
  )",
                 "4\n10\n40");

  // pack 0 个参数，loglen = 0
  runner.addTest("Boundary: Pack zero args loglen is 0",
                 R"(
    any l = table.pack();
    print(#l);
  )",
                 "0");

  // unpack 范围恰好是 [0, loglen)
  runner.addTest("Boundary: Unpack full range",
                 R"(
    list<int> l = [5, 6, 7];
    print(table.unpack(l, 0, #l));
  )",
                 "5 6 7");

  // unpack 空范围返回 0 个值
  runner.addTest("Boundary: Unpack empty range returns nothing",
                 R"(
    list<int> l = [1, 2, 3];
    int count = 0;
    any results = table.pack(table.unpack(l, 0, 0));
    print(#results);
  )",
                 "0");

  // =======================================================
  // 第八组：table.sort 与 loglen
  // =======================================================

  // sort 后 loglen 不变
  runner.addTest("Boundary: Sort preserves loglen",
                 R"(
    list<int> l = [5, 3, 1, 4, 2];
    table.sort(l);
    print(#l);
    print(l[0]);
    print(l[4]);
  )",
                 "5\n1\n5");

  // sort 单元素，loglen=1
  runner.addTest("Boundary: Sort single element",
                 R"(
    list<int> l = [42];
    table.sort(l);
    print(#l);
    print(l[0]);
  )",
                 "1\n42");

  // sort 两元素
  runner.addTest("Boundary: Sort two elements",
                 R"(
    list<int> l = [9, 1];
    table.sort(l);
    print(l[0]);
    print(l[1]);
    print(#l);
  )",
                 "1\n9\n2");

  // sort 自定义比较函数，loglen 不变
  runner.addTest("Boundary: Sort custom comparator loglen intact",
                 R"(
    list<int> l = [1, 5, 2, 4, 3];
    table.sort(l, function(any a, any b) -> bool { return a > b; });
    print(#l);
    print(l[0]);
    print(l[4]);
  )",
                 "5\n5\n1");

  // =======================================================
  // 第九组：table.insert 边界
  // =======================================================

  // insert 在下标 0 处插入
  runner.addTest("Boundary: Insert at index 0",
                 R"(
    list<int> l = [1, 2, 3];
    table.insert(l, 0, 99);
    print(#l);
    print(l[0]);
    print(l[1]);
    print(l[3]);
  )",
                 "4\n99\n1\n3");

  // insert 在末尾插入（等价于 append）
  runner.addTest("Boundary: Insert at loglen (append)",
                 R"(
    list<int> l = [1, 2, 3];
    table.insert(l, 3, 4);
    print(#l);
    print(l[3]);
  )",
                 "4\n4");

  // insert 在中间插入，元素正确移位
  runner.addTest("Boundary: Insert middle shifts elements correctly",
                 R"(
    list<int> l = [1, 2, 3, 4, 5];
    table.insert(l, 2, 99);
    print(#l);
    print(l[2]);
    print(l[3]);
    print(l[5]);
  )",
                 "6\n99\n3\n5");

  // insert 后再 pop，loglen 正确
  runner.addTest("Boundary: Insert then pop loglen correct",
                 R"(
    list<int> l = [1, 2, 3];
    table.insert(l, 1, 99);
    table.pop(l);
    print(#l);
    print(l[0]);
    print(l[1]);
  )",
                 "3\n1\n99");

  // =======================================================
  // 第十组：table.remove 边界
  // =======================================================

  // remove 首个元素
  runner.addTest("Boundary: Remove first element",
                 R"(
    list<int> l = [10, 20, 30];
    any v = table.remove(l, 0);
    print(v);
    print(#l);
    print(l[0]);
  )",
                 "10\n2\n20");

  // remove 最后一个元素（等价于 pop）
  runner.addTest("Boundary: Remove last element equals pop",
                 R"(
    list<int> l = [10, 20, 30];
    any v = table.remove(l, 2);
    print(v);
    print(#l);
    print(l[1]);
  )",
                 "30\n2\n20");

  // remove 中间元素，前后正确
  runner.addTest("Boundary: Remove middle element shifts correctly",
                 R"(
    list<int> l = [1, 2, 3, 4, 5];
    table.remove(l, 2);
    print(#l);
    print(l[1]);
    print(l[2]);
  )",
                 "4\n2\n4");

  // 连续 remove 直到空
  runner.addTest("Boundary: Repeated remove until empty",
                 R"(
    list<int> l = [1, 2, 3];
    table.remove(l, 0);
    table.remove(l, 0);
    table.remove(l, 0);
    print(#l);
  )",
                 "0");

  // =======================================================
  // 第十一组：nil 元素与 loglen
  // =======================================================

  // nil 元素占据 loglen
  runner.addTest("Boundary: Nil elements count in loglen",
                 R"(
    list<any> l = [null, null, null];
    print(#l);
  )",
                 "3");

  // push nil，loglen 增加
  runner.addTest("Boundary: Push nil increments loglen",
                 R"(
    list<any> l = [];
    table.push(l, null);
    table.push(l, null);
    print(#l);
  )",
                 "2");

  // pop nil，loglen 减少
  runner.addTest("Boundary: Pop nil decrements loglen",
                 R"(
    list<any> l = [null, null];
    table.pop(l);
    print(#l);
  )",
                 "1");

  // nil 和非 nil 混合，loglen 包含全部
  runner.addTest("Boundary: Mixed nil non-nil loglen correct",
                 R"(
    list<any> l = [1, null, 3, null, 5];
    print(#l);
    print(l[1]);
    print(l[3]);
  )",
                 "5\nnil\nnil");

  // =======================================================
  // 第十二组：pairs 遍历与 loglen
  // =======================================================

  // pairs 遍历严格在 [0, loglen) 范围内
  runner.addTest("Boundary: Pairs iterates exactly loglen elements",
                 R"(
    list<int> l = [10, 20, 30];
    int count = 0;
    int sum = 0;
    for (k, v : pairs(l)) {
        count += 1;
        sum += v;
    }
    print(count);
    print(sum);
  )",
                 "3\n60");

  // push 后 pairs 包含新元素
  runner.addTest("Boundary: Pairs after push includes new element",
                 R"(
    list<any> l = [1, 2];
    table.push(l, 3);
    int count = 0;
    for (k, v : pairs(l)) {
        count += 1;
    }
    print(count);
  )",
                 "3");

  // pop 后 pairs 不包含被 pop 的元素
  runner.addTest("Boundary: Pairs after pop excludes popped element",
                 R"(
    list<int> l = [1, 2, 3];
    table.pop(l);
    int count = 0;
    for (k, v : pairs(l)) {
        count += 1;
    }
    print(count);
  )",
                 "2");

  // pairs 遍历 nil 元素也计入
  runner.addTest("Boundary: Pairs counts nil elements",
                 R"(
    list<any> l = [1, null, 3];
    int count = 0;
    for (k, v : pairs(l)) {
        count += 1;
    }
    print(count);
  )",
                 "3");

  // pairs key 是 0-based 整数
  runner.addTest("Boundary: Pairs keys are 0-based integers",
                 R"(
    list<int> l = [100, 200, 300];
    for (k, v : pairs(l)) {
        print(k .. "=" .. v);
    }
  )",
                 "0=100\n1=200\n2=300");

  // =======================================================
  // 第十三组：引用语义与 loglen
  // =======================================================

  // 引用共享 loglen 变化
  runner.addTest("Boundary: Reference shares loglen after push",
                 R"(
    list<int> a = [1, 2, 3];
    list<int> b = a;
    table.push(b, 4);
    print(#a);
    print(#b);
  )",
                 "4\n4");

  // 引用共享 loglen pop
  runner.addTest("Boundary: Reference shares loglen after pop",
                 R"(
    list<int> a = [1, 2, 3];
    list<int> b = a;
    table.pop(b);
    print(#a);
    print(#b);
  )",
                 "2\n2");

  // 函数内修改 list，外部 loglen 反映
  runner.addTest("Boundary: Function modifies list loglen visible outside",
                 R"(
    void addItems(list l) {
        table.push(l, 100);
        table.push(l, 200);
    }
    list<any> l = [1];
    addItems(l);
    print(#l);
    print(l[1]);
    print(l[2]);
  )",
                 "3\n100\n200");

  // 函数内 pop，外部 loglen 减少
  runner.addTest("Boundary: Function pops list loglen visible outside",
                 R"(
    void removeOne(list l) {
        table.pop(l);
    }
    list<int> l = [1, 2, 3];
    removeOne(l);
    print(#l);
  )",
                 "2");

  // =======================================================
  // 第十四组：嵌套 list 的 loglen 独立性
  // =======================================================

  // 内层 push 不影响外层 loglen
  runner.addTest("Boundary: Inner list push independent from outer loglen",
                 R"(
    list<any> outer = [[1, 2], [3, 4]];
    print(#outer);
    table.push(outer[0], 99);
    print(#outer);
    print(#outer[0]);
  )",
                 "2\n2\n3");

  // 外层 push 不影响内层 loglen
  runner.addTest("Boundary: Outer list push independent from inner loglen",
                 R"(
    list<any> inner = [10, 20];
    list<any> outer = [inner];
    table.push(outer, [30, 40, 50]);
    print(#outer);
    print(#inner);
  )",
                 "2\n2");

  // pop 外层不影响内层
  runner.addTest("Boundary: Pop outer does not affect inner loglen",
                 R"(
    list<any> a = [1, 2];
    list<any> b = [3, 4, 5];
    list<any> outer = [a, b];
    table.pop(outer);
    print(#outer);
    print(#b);
  )",
                 "1\n3");

  // =======================================================
  // 第十五组：大规模 push/pop 压力测试
  // =======================================================

  // 千次 push，loglen 正确
  runner.addTest("Boundary: 1000 pushes loglen correct",
                 R"(
    list<any> l = [];
    for (i = 0, 999) {
        table.push(l, i);
    }
    print(#l);
    print(l[0]);
    print(l[999]);
  )",
                 "1000\n0\n999");

  // 千次 push 后 500 次 pop，loglen = 500
  runner.addTest("Boundary: 1000 push 500 pop loglen is 500",
                 R"(
    list<any> l = [];
    for (i = 0, 999) {
        table.push(l, i);
    }
    for (i = 0, 499) {
        table.pop(l);
    }
    print(#l);
    print(l[499]);
  )",
                 "500\n499");

  // 全部 pop，loglen = 0，再次 push 从 0 开始
  runner.addTest("Boundary: Pop all then push again starts from 0",
                 R"(
    list<any> l = [1, 2, 3];
    table.pop(l);
    table.pop(l);
    table.pop(l);
    print(#l);
    table.push(l, 99);
    print(#l);
    print(l[0]);
  )",
                 "0\n1\n99");

  // 反复扩缩容，最终 loglen 正确
  runner.addTest("Boundary: Repeated grow shrink loglen stable",
                 R"(
    list<any> l = [];
    for (i = 0, 63) { table.push(l, i); }
    for (i = 0, 31) { table.pop(l); }
    for (i = 0, 31) { table.push(l, i * 100); }
    print(#l);
    print(l[32]);
  )",
                 "64\n0");

  // =======================================================
  // 第十六组：table.concat 与 loglen 边界
  // =======================================================

  // concat 默认范围 [0, loglen-1]
  runner.addTest("Boundary: Concat default full range",
                 R"(
    list<int> l = [1, 2, 3, 4, 5];
    print(table.concat(l, "-"));
  )",
                 "1-2-3-4-5");

  // concat 空 list 返回空字符串
  runner.addTest("Boundary: Concat empty list",
                 R"(
    list<any> l = [];
    print(table.concat(l, ","));
  )",
                 "");

  // concat 单元素
  runner.addTest("Boundary: Concat single element",
                 R"(
    list<int> l = [42];
    print(table.concat(l, ","));
  )",
                 "42");

  // push 后 concat 包含新元素
  runner.addTest("Boundary: Concat after push includes new element",
                 R"(
    list<any> l = ["a", "b"];
    table.push(l, "c");
    print(table.concat(l, ""));
  )",
                 "abc");

  // pop 后 concat 不包含被 pop 的元素
  runner.addTest("Boundary: Concat after pop excludes popped",
                 R"(
    list<any> l = ["x", "y", "z"];
    table.pop(l);
    print(table.concat(l, ""));
  )",
                 "xy");

  // =======================================================
  // 第十七组：for 数值循环与 loglen
  // =======================================================

  // 用 #l 作上界的 for 循环，push 后上界扩大（注意：此处 #l 在循环开始时求值）
  runner.addTest("Boundary: For loop with loglen upper bound",
                 R"(
    list<int> l = [10, 20, 30, 40, 50];
    int sum = 0;
    for (i = 0, #l - 1) {
        sum += l[i];
    }
    print(sum);
  )",
                 "150");

  // 空 list 的 for 循环不执行
  runner.addTest("Boundary: For loop on empty list not executed",
                 R"(
    list<any> l = [];
    int count = 0;
    for (i = 0, #l - 1) {
        count += 1;
    }
    print(count);
  )",
                 "0");

  // 反向遍历，边界正确
  runner.addTest("Boundary: Reverse for loop boundary correct",
                 R"(
    list<int> l = [1, 2, 3, 4, 5];
    for (i = #l - 1, 0, -1) {
        print(l[i]);
    }
  )",
                 "5\n4\n3\n2\n1");

  // =======================================================
  // 第十八组：map 的 loglen 独立性（map 不使用 loglen）
  // =======================================================

  // map 的 # 运算符不统计键数量（返回 0），与 list loglen 无关
  runner.addTest("Boundary: Map length returns 0 (hash map not counted by #)",
                 R"(
    map<string, any> m = {"a": 1, "b": 2, "c": 3};
    print(#m);
  )",
                 "0");

  // map 内容可通过 pairs 计数验证，删 key 后 pairs 少一个
  runner.addTest("Boundary: Map content verified via pairs count after delete",
                 R"(
    map<string, any> m = {"x": 1, "y": 2, "z": 3};
    int count = 0;
    for (k, v : pairs(m)) { count += 1; }
    print(count);
    m["x"] = null;
    int count2 = 0;
    for (k, v : pairs(m)) { count2 += 1; }
    print(count2);
  )",
                 "3\n2");

  // list 嵌套在 map 中，list loglen 独立，map # 返回 0
  runner.addTest("Boundary: List in map has independent loglen map # is 0",
                 R"(
    map<string, any> m = {"list": [1, 2, 3]};
    table.push(m["list"], 4);
    print(#m["list"]);
    print(#m);
  )",
                 "4\n0");

  // =======================================================
  // 第十九组：逻辑长度与物理容量分离的极端边界
  // =======================================================

  // 1 个元素 push 后读 index 0 正确
  runner.addTest("Boundary: Single push read index 0",
                 R"(
    list<any> l = [];
    table.push(l, 777);
    print(l[0]);
    print(#l);
  )",
                 "777\n1");

  // push 1 个然后 pop，loglen=0，不能再读
  runner.addFailTest("Boundary: Push one pop one then read fails",
                     R"(
    list<any> l = [];
    table.push(l, 1);
    table.pop(l);
    print(l[0]);
  )");

  // 字面量 1 个元素，pop 后不能读 index 0
  runner.addFailTest("Boundary: Single literal pop then read fails",
                     R"(
    list<int> l = [42];
    table.pop(l);
    print(l[0]);
  )");

  // insert 在空 list 的 index 0 处
  runner.addTest("Boundary: Insert into empty list at 0",
                 R"(
    list<any> l = [];
    table.push(l, 0);
    table.insert(l, 0, 99);
    print(#l);
    print(l[0]);
    print(l[1]);
  )",
                 "2\n99\n0");

  // remove 只剩 1 个元素后，list 为空
  runner.addTest("Boundary: Remove last remaining element empties list",
                 R"(
    list<int> l = [42];
    table.remove(l, 0);
    print(#l);
  )",
                 "0");

  // =======================================================
  // 第二十组：综合场景 —— loglen/asize 分离的完整性
  // =======================================================

  // 模拟栈：push N 次，pop N 次，loglen=0
  runner.addTest("Boundary: Stack simulation push N pop N loglen 0",
                 R"(
    list<any> stack = [];
    int N = 20;
    for (i = 0, N - 1) {
        table.push(stack, i);
    }
    for (i = 0, N - 1) {
        table.pop(stack);
    }
    print(#stack);
  )",
                 "0");

  // 模拟队列：enqueue 5，dequeue 3，loglen=2
  runner.addTest("Boundary: Queue simulation 5 enqueue 3 dequeue loglen 2",
                 R"(
    list<any> queue = [];
    table.push(queue, "a");
    table.push(queue, "b");
    table.push(queue, "c");
    table.push(queue, "d");
    table.push(queue, "e");
    table.remove(queue, 0);
    table.remove(queue, 0);
    table.remove(queue, 0);
    print(#queue);
    print(queue[0]);
    print(queue[1]);
  )",
                 "2\nd\ne");

  // 多轮扩缩容后，存储内容仍然正确
  runner.addTest("Boundary: Multi-round grow shrink content integrity",
                 R"(
    list<any> l = [];
    // 第一轮：push 10
    for (i = 0, 9) { table.push(l, i); }
    // pop 5
    for (i = 0, 4) { table.pop(l); }
    // 第二轮：push 10
    for (i = 10, 19) { table.push(l, i); }
    print(#l);
    print(l[0]);
    print(l[4]);
    print(l[14]);
  )",
                 "15\n0\n4\n19");

  // 并发式 insert+remove 保持 loglen 一致
  runner.addTest("Boundary: Alternating insert remove loglen consistent",
                 R"(
    list<int> l = [1, 2, 3, 4, 5];
    table.insert(l, 2, 99);
    table.remove(l, 0);
    table.insert(l, 4, 88);
    table.remove(l, 5);
    print(#l);
    print(l[2]);
    print(l[4]);
  )",
                 "5\n3\n88");

  // table.sort 后 loglen 不变，所有元素可访问
  runner.addTest("Boundary: Sort then all elements accessible via loglen",
                 R"(
    list<int> l = [50, 10, 40, 20, 30];
    table.sort(l);
    int sum = 0;
    for (i = 0, #l - 1) {
        sum += l[i];
    }
    print(sum);
    print(#l);
  )",
                 "150\n5");

  // 验证 table.move 后源和目标的 loglen 都正确
  runner.addTest("Boundary: Move loglen of both src and dst correct",
                 R"(
    list<int> src = [1, 2, 3, 4, 5];
    list<int> dst = [0, 0, 0, 0, 0, 0, 0];
    table.move(src, 1, 3, 2, dst);
    print(#src);
    print(#dst);
    print(dst[2]);
    print(dst[3]);
    print(dst[4]);
  )",
                 "5\n7\n2\n3\n4");

  // 嵌套场景：map 内 list 反复 push/pop，loglen 正确
  runner.addTest("Boundary: Nested map-list push pop loglen correct",
                 R"(
    map<string, any> m = {"data": []};
    for (i = 0, 4) {
        table.push(m["data"], i * 10);
    }
    print(#m["data"]);
    table.pop(m["data"]);
    table.pop(m["data"]);
    print(#m["data"]);
    print(m["data"][0]);
    print(m["data"][2]);
  )",
                 "5\n3\n0\n20");
}
