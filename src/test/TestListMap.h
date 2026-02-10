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
            any m = {
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
            any m = {"data": {"x":"10", "y":"20"}};
            m["data"]["x"] = "99";
            print(m["data"]["x"]);
            print(m["data"]["y"]);
       )",
                 "99\n20");

  runner.addTest("Map: Deep Nesting",
                 R"(
            any m = {
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
            any users = [
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
            any data = {
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
            any complex = {
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
            any data = {
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
            any data = {
                "items": [1, 2, 3]
            };
            table.push(data["items"], 4);
            print(#data["items"]);
            print(data["items"][3]);
       )",
                 "4\n4");

  runner.addTest("List in Map with Pop",
                 R"(
            any data = {
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
            any graph = {
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
            any config = {
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
            any students = {
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
            any grades = students["student1"]["grades"];
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
            any inventory = {
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