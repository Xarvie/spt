#pragma once
#include "TestRunner.h"

// =========================================================
// 1. 基础语法与运算 (Basics)
// =========================================================

inline void registerBasics(TestRunner &runner) {

  runner.addTest(
      "Comparison NaN",
      R"(
            float nanX = math.sqrt(-1);
            print(nanX == nanX); // false
            print(nanX != nanX); // true
            print(nanX < nanX); // false
            print(nanX > nanX); // false
            print(nanX <= nanX); // false
            print(nanX >= nanX); // false
            print(nanX == 1); // false
            print(nanX != 1); // true
            print(nanX < 1); // false
            print(nanX > 1); // false
            print(nanX <= 1); // false
            print(nanX >= 1); // false
       )",
      "false\ntrue\nfalse\nfalse\nfalse\nfalse\nfalse\ntrue\nfalse\nfalse\nfalse\nfalse");

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

  // ---------------------------------------------------------
  // 1. 基础语法区分：[] vs {}
  // ---------------------------------------------------------
  runner.addTest("Syntax: List vs Map Creation",
                 R"(
            // List构造
            list<int> a = [10, 20, 30];
            print(a[0]);
            print(a[2]);
            print(#a);

            // Map 构造
            map<any, int> b = { 1:10, 2:20, 3:30}; // 普通 Lua table 行为
            print(b["2"]);
            print(b["1"]);
       )",
                 "10\n30\n3\n20\n10");

  // ---------------------------------------------------------
  // 2. List: 0-based, 定长, 支持 nil (空洞)
  // ---------------------------------------------------------
  runner.addTest("List: Zero-Based & Nil Support",
                 R"(
            // 创建带空洞的 List
            var l = [100, null];

            print(l[0]);
            print(l[1]); // 应该是 nil


            // 长度应该是物理容量
            print(#l);

            // 修改元素
            l[0] = 99;
            print(l[0]);
       )",
                 "100\nnil\n2\n99");

  // ---------------------------------------------------------
  // 3. List: 越界写入报错 (禁止自动扩容)
  // ---------------------------------------------------------
  runner.addFailTest("List Error: Out of Bounds Write (Positive)",
                     R"(
            var l = [1, 2, 3];
            // 你的设定：List 是定长的，下标 0,1,2 有效
            // 下标 3 越界，应该报错而不是自动扩容
            l[3] = 4;
       )");

  runner.addFailTest("List Error: Out of Bounds Write (Negative)",
                     R"(
            var l = [1, 2];
            l[-1] = 0;
       )");

  // ---------------------------------------------------------
  // 4. List: 键类型检查 (只允许整数)
  // ---------------------------------------------------------
  runner.addFailTest("List Error: Invalid Key Type",
                     R"(
            var l = [1, 2];
            l["key"] = 3; // List 只能用数字下标
       )");

  // ---------------------------------------------------------
  // 5. Map: 保持原有灵活性 (动态扩容, 任意键)
  // ---------------------------------------------------------
  runner.addTest("Map: Dynamic Behavior",
                 R"(
            var m = {};
            m[100] = 1;   // 稀疏数组/Hash
            m["k"] = "v"; // 字符串键

            print(m[100]);
            print(m["k"]);

            // 验证 Map 没有被错误地锁死
            m[1000] = 2;
            print(m[1000]);
       )",
                 "1\nv\n2");

  // ---------------------------------------------------------
  // 6. 迭代器行为 (Pairs)
  // ---------------------------------------------------------
  runner.addTest("Iteration: List vs Map",
                 R"(
            l = [10, nil, 30];
            print("--- List ---");
            // List 应该按 0, 1, 2 顺序遍历，且包含 nil
            for(k,v : pairs(l)){
                if (v == nil) { print(k .. ":nil"); }
                else { print(k .. ":" .. v); }
            }

            var m = {1:30, 3:30};
            print("--- Map ---");

            for(k,v : pairs(m)){
                 print(v);
            }
       )",
                 "--- List ---\n0:10\n1:nil\n2:30\n--- Map ---\n30\n30");
}

inline void registerBasicsExtendedTemp(TestRunner &runner) {

  // ---------------------------------------------------------
  // List: 空 List 测试
  // ---------------------------------------------------------
  runner.addTest("List: Empty List",
                 R"(
            var l = [];
            print(#l);
            print(l == null); // false, empty list is not null
       )",
                 "0\nfalse");

  runner.addFailTest("List Error: Negative Index Read",
                     R"(
             var l = [1, 2, 3];
             print(l[-1]);
        )");

  runner.addFailTest("List Error: Out of Bounds Read",
                     R"(
             var l = [1, 2, 3];
             print(l[3]); // 索引 0,1,2 有效，3 越界
        )");

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
            var mixed = [1, "hello", true, 3.14, null];
            print(mixed[0]);
            print(mixed[1]);
            print(mixed[2]);
            print(mixed[3]);
            print(mixed[4]);
       )",
                 "1\nhello\ntrue\n3.14\nnil");

  // ---------------------------------------------------------
  // List: 嵌套 List
  // ---------------------------------------------------------
  runner.addTest("List: Nested Lists",
                 R"(
            var matrix = [[1, 2], [3, 4], [5, 6]];
            print(#matrix);
            print(#matrix[0]);
            print(matrix[0][0]);
            print(matrix[0][1]);
            print(matrix[1][0]);
            print(matrix[2][1]);
       )",
                 "3\n2\n1\n2\n3\n6");

  // ---------------------------------------------------------
  // List: 修改嵌套元素
  // ---------------------------------------------------------
  runner.addTest("List: Modify Nested Elements",
                 R"(
            var matrix = [[1, 2], [3, 4]];
            matrix[0][1] = 99;
            print(matrix[0][0]);
            print(matrix[0][1]);
            print(matrix[1][0]);
       )",
                 "1\n99\n3");

  // ---------------------------------------------------------
  // List: 连续的 nil 元素
  // ---------------------------------------------------------
  runner.addTest("List: Multiple Nil Elements",
                 R"(
            var l = [null, null, 42, null];
            print(#l);
            print(l[0]);
            print(l[1]);
            print(l[2]);
            print(l[3]);
       )",
                 "4\nnil\nnil\n42\nnil");

  // ---------------------------------------------------------
  // List: 循环遍历
  // ---------------------------------------------------------
  runner.addTest("List: For Loop Iteration",
                 R"(
            var l = [10, 20, 30];
            for (i = 0, #l - 1) {
                print(l[i]);
            }
       )",
                 "10\n20\n30");

  // ---------------------------------------------------------
  // List: 带 nil 的循环遍历
  // ---------------------------------------------------------
  runner.addTest("List: Iteration with Nil",
                 R"(
            var l = [1, null, 3];
            for (i = 0, #l - 1) {
                if (l[i] == null) {
                    print("nil");
                } else {
                    print(l[i]);
                }
            }
       )",
                 "1\nnil\n3");

  // ---------------------------------------------------------
  // List: 作为函数参数
  // ---------------------------------------------------------
  runner.addTest("List: As Function Parameter",
                 R"(
            int sum(list arr) {
                int total = 0;
                for (i = 0, #arr - 1) {
                    total += arr[i];
                }
                return total;
            }

            var nums = [1, 2, 3, 4, 5];
            print(sum(nums));
       )",
                 "15");

  // ---------------------------------------------------------
  // List: 函数返回 List
  // ---------------------------------------------------------
  runner.addTest("List: Return from Function",
                 R"(
            list makeList() {
                return [100, 200, 300];
            }

            var result = makeList();
            print(result[0]);
            print(result[2]);
            print(#result);
       )",
                 "100\n300\n3");

  // ---------------------------------------------------------
  // List: 引用行为（修改会影响原 list）
  // ---------------------------------------------------------
  runner.addTest("List: Reference Behavior",
                 R"(
            var a = [1, 2, 3];
            var b = a;
            b[0] = 99;
            print(a[0]); // 应该是 99，因为是引用
            print(b[0]);
       )",
                 "99\n99");

  // ---------------------------------------------------------
  // List: 比较操作
  // ---------------------------------------------------------
  runner.addTest("List: Comparison",
                 R"(
            var a = [1, 2, 3];
            var b = [1, 2, 3];
            var c = a;

            print(a == c);  // 同一引用
            print(a == b);  // 不同实例，内容相同（取决于实现）
       )",
                 "true\nfalse");

  // =======================================================
  // Map 深入测试
  // =======================================================

  // ---------------------------------------------------------
  // Map: 空 Map 测试
  // ---------------------------------------------------------
  runner.addTest("Map: Empty Map",
                 R"(
            var m = {};
            print(#m);
            print(m == null);
       )",
                 "0\nfalse");

  // ---------------------------------------------------------
  // Map: 删除键
  // ---------------------------------------------------------
  runner.addTest("Map: Delete Key",
                 R"(
            var m = {"1":"10", "2":"20", "3":"30"};
            print(m["1"]);
            m["1"] = null;  // 删除键
            print(m["1"]);
       )",
                 "10\nnil");

  // ---------------------------------------------------------
  // Map: 不存在的键
  // ---------------------------------------------------------
  runner.addTest("Map: Non-existent Key",
                 R"(
            var m = {"1":"10"};
            print(m["2"]);  // 不存在的键返回 nil
            print(m["1"]);
       )",
                 "nil\n10");

  // ---------------------------------------------------------
  // Map: 字符串键
  // ---------------------------------------------------------
  runner.addTest("Map: String Keys",
                 R"(
            var m = {"name":"Alice", "age":"30", "city":"NYC"};
            print(m["name"]);
            print(m["age"]);
            print(m["city"]);
       )",
                 "Alice\n30\nNYC");

  // ---------------------------------------------------------
  // Map: 混合键类型
  // ---------------------------------------------------------
  runner.addTest("Map: Mixed Key Types",
                 R"(
            var m = {};
            m[1] = "int key";
            m["str"] = "string key";
            m[true] = "bool key";

            print(m[1]);
            print(m["str"]);
            print(m[true]);
       )",
                 "int key\nstring key\nbool key");

  // ---------------------------------------------------------
  // Map: 浮点数键
  // ---------------------------------------------------------
  runner.addTest("Map: Float Keys",
                 R"(
            var m = {};
            m[3.14] = "pi";
            m[2.71] = "e";

            print(m[3.14]);
            print(m[2.71]);
       )",
                 "pi\ne");

  // ---------------------------------------------------------
  // Map: 负数键
  // ---------------------------------------------------------
  runner.addTest("Map: Negative Keys",
                 R"(
            var m = {};
            m[-1] = "negative";
            m[-100] = "very negative";

            print(m[-1]);
            print(m[-100]);
       )",
                 "negative\nvery negative");

  // ---------------------------------------------------------
  // Map: 稀疏数组行为
  // ---------------------------------------------------------
  runner.addTest("Map: Sparse Array Behavior",
                 R"(
            var m = {};
            m[1] = "a";
            m[100] = "b";
            m[1000] = "c";

            print(m[1]);
            print(m[100]);
            print(m[1000]);
            print(m[2]);  // 不存在
       )",
                 "a\nb\nc\nnil");

  // ---------------------------------------------------------
  // Map: 嵌套 Map
  // ---------------------------------------------------------
  runner.addTest("Map: Nested Maps",
                 R"(
            var m = {
                "user1": {"name":"Alice", "age":"25"},
                "user2": {"name":"Bob", "age":"30"}
            };

            print(m["user1"]["name"]);
            print(m["user1"]["age"]);
            print(m["user2"]["name"]);
       )",
                 "Alice\n25\nBob");

  // ---------------------------------------------------------
  // Map: 修改嵌套值
  // ---------------------------------------------------------
  runner.addTest("Map: Modify Nested Values",
                 R"(
            var m = {"data": {"x":"10", "y":"20"}};
            m["data"]["x"] = "99";
            print(m["data"]["x"]);
            print(m["data"]["y"]);
       )",
                 "99\n20");

  // ---------------------------------------------------------
  // Map: 动态添加键
  // ---------------------------------------------------------
  runner.addTest("Map: Dynamic Key Addition",
                 R"(
            var m = {};
            m["key1"] = "value1";
            print(m["key1"]);

            m["key2"] = "value2";
            print(m["key2"]);

            m[3] = "value3";
            print(m[3]);
       )",
                 "value1\nvalue2\nvalue3");

  // ---------------------------------------------------------
  // Map: 作为函数参数
  // ---------------------------------------------------------
  runner.addTest("Map: As Function Parameter",
                 R"(
            any getValue(any dict, any key) {
                return dict[key];
            }

            var m = {"a":"100", "b":"200"};
            print(getValue(m, "a"));
            print(getValue(m, "b"));
       )",
                 "100\n200");

  // ---------------------------------------------------------
  // Map: 函数返回 Map
  // ---------------------------------------------------------
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

  // ---------------------------------------------------------
  // Map: 引用行为
  // ---------------------------------------------------------
  runner.addTest("Map: Reference Behavior",
                 R"(
            var a = {"key":"value1"};
            var b = a;
            b["key"] = "value2";
            print(a["key"]);  // 应该是 value2
            print(b["key"]);
       )",
                 "value2\nvalue2");

  // =======================================================
  // List 和 Map 混合测试
  // =======================================================

  // ---------------------------------------------------------
  // List 包含 Map
  // ---------------------------------------------------------
  runner.addTest("List of Maps",
                 R"(
            var users = [
                {"name":"Alice", "age":"25"},
                {"name":"Bob", "age":"30"},
                {"name":"Charlie", "age":"35"}
            ];

            print(users[0]["name"]);
            print(users[1]["age"]);
            print(users[2]["name"]);
       )",
                 "Alice\n30\nCharlie");

  // ---------------------------------------------------------
  // Map 包含 List
  // ---------------------------------------------------------
  runner.addTest("Map of Lists",
                 R"(
            var data = {
                "numbers": [1, 2, 3],
                "strings": ["a", "b", "c"]
            };

            print(data["numbers"][0]);
            print(data["numbers"][2]);
            print(data["strings"][1]);
       )",
                 "1\n3\nb");

  // ---------------------------------------------------------
  // 复杂嵌套结构
  // ---------------------------------------------------------
  runner.addTest("Complex Nested Structure",
                 R"(
            var complex = {
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

  // ---------------------------------------------------------
  // 修改混合结构
  // ---------------------------------------------------------
  runner.addTest("Modify Mixed Structure",
                 R"(
            var data = {
                "items": [10, 20, 30]
            };

            data["items"][1] = 99;
            print(data["items"][0]);
            print(data["items"][1]);
            print(data["items"][2]);
       )",
                 "10\n99\n30");

  // =======================================================
  // 边界情况和特殊场景（只保留不需要边界检查的）
  // =======================================================

  // ---------------------------------------------------------
  // List: 初始化后长度固定
  // ---------------------------------------------------------
  runner.addTest("List: Fixed Length After Creation",
                 R"(
            var l = [1, 2, 3];
            print(#l);
            l[0] = 100;
            l[1] = 200;
            l[2] = 300;
            print(#l);  // 长度不变
       )",
                 "3\n3");

  // ---------------------------------------------------------
  // Pairs 迭代：空容器
  // ---------------------------------------------------------
  runner.addTest("Pairs: Empty Containers",
                 R"(
            var l = [];
            var count = 0;
            for (k, v : pairs(l)) {
                count += 1;
            }
            print(count);

            var m = {};
            count = 0;
            for (k, v : pairs(m)) {
                count += 1;
            }
            print(count);
       )",
                 "0\n0");

  // ---------------------------------------------------------
  // 字符串连接：List 元素
  // ---------------------------------------------------------
  runner.addTest("String Concatenation with List Elements",
                 R"(
            var l = [1, 2, 3];
            var result = "Values: " .. l[0] .. ", " .. l[1] .. ", " .. l[2];
            print(result);
       )",
                 "Values: 1, 2, 3");

  // ---------------------------------------------------------
  // 条件判断：容器是否为空
  // ---------------------------------------------------------
  runner.addTest("Check if Containers are Empty",
                 R"(
            var l = [];
            if (#l == 0) {
                print("List is empty");
            }

            var m = {};
            if (#m == 0) {
                print("Map is empty");
            }

            var l2 = [1];
            if (#l2 > 0) {
                print("List has elements");
            }
       )",
                 "List is empty\nMap is empty\nList has elements");

  // ---------------------------------------------------------
  // List: 全是 nil 的情况
  // ---------------------------------------------------------
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
  // Map: 覆盖已有键
  // ---------------------------------------------------------
  runner.addTest("Map: Overwrite Existing Key",
                 R"(
            map m = {key:"old"};
            print(m["key"]);
            m["key"] = "new";
            print(m["key"]);
       )",
                 "old\nnew");

  runner.addFailTest("List Error: Float Index",
                     R"(
             var l = [1, 2, 3];
             print(l[1.5]);  // 浮点数不能作为 List 索引
        )");

  // ---------------------------------------------------------
  // Map: 浮点索引访问（Map 允许任意类型键）
  // ---------------------------------------------------------
  runner.addTest("Map: Float Index Access",
                 R"(
            var m = {};
            m[0] = "zero";
            m[1] = "one";
            // 浮点数 0.0 和整数 0 应该是不同的键（取决于实现）
            print(m[0]);
            print(m[1]);
       )",
                 "zero\none");

  // ---------------------------------------------------------
  // 测试 List 和 Map 的直接赋值
  // ---------------------------------------------------------
  runner.addTest("Direct Assignment of Containers",
                 R"(
            var l1 = [1, 2, 3];
            var l2 = l1;
            var m1 = {"a":"1"};
            var m2 = m1;

            print(l2[0]);
            print(m2["a"]);
       )",
                 "1\n1");
}