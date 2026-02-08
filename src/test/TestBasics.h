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
  runner.addTest(
      "Syntax: List vs Map Creation",
      R"(
            // List构造
            list<int> a = [10, 20, 30];
            print(a[0]);
            print(a[2]);
            print(#a);

            // Map 构造
            map<any, int> b = {a:1, 1:10, 2:20, 3:30}; // 普通 Lua table 行为
            print(b[2]);
            print(b["a"]);
       )",
      "10\n30\n3\n20\n1"
  );

  // ---------------------------------------------------------
  // 2. List: 0-based, 定长, 支持 nil (空洞)
  // ---------------------------------------------------------
  runner.addTest(
      "List: Zero-Based & Nil Support",
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
      "100\nnil\n2\n99"
  );

//  // ---------------------------------------------------------
//  // 3. List: 越界写入报错 (禁止自动扩容)
//  // ---------------------------------------------------------
//  runner.addTest(
//      "List Error: Out of Bounds Write (Positive)",
//      R"(
//            var l = [1, 2, 3];
//            // 你的设定：List 是定长的，下标 0,1,2 有效
//            // 下标 3 越界，应该报错而不是自动扩容
//            l[3] = 4;
//       )",
//      ""
//  );
//
//  runner.addTest(
//      "List Error: Out of Bounds Write (Negative)",
//      R"(
//            var l = [1, 2];
//            l[-1] = 0;
//       )",
//      ""
//  );
//
//  // ---------------------------------------------------------
//  // 4. List: 键类型检查 (只允许整数)
//  // ---------------------------------------------------------
//  runner.addTest(
//      "List Error: Invalid Key Type",
//      R"(
//            var l = [1, 2];
//            l["key"] = 3; // List 只能用数字下标
//       )",
//      ""
//  );
//
//  // ---------------------------------------------------------
//  // 5. Map: 保持原有灵活性 (动态扩容, 任意键)
//  // ---------------------------------------------------------
//  runner.addTest(
//      "Map: Dynamic Behavior",
//      R"(
//            var m = {};
//            m[100] = 1;   // 稀疏数组/Hash
//            m["k"] = "v"; // 字符串键
//
//            print(m[100]);
//            print(m["k"]);
//
//            // 验证 Map 没有被错误地锁死
//            m[1000] = 2;
//            print(m[1000]);
//       )",
//      "1\nv\n2"
//  );
//
//  // ---------------------------------------------------------
//  // 6. 迭代器行为 (Pairs)
//  // ---------------------------------------------------------
//  runner.addTest(
//      "Iteration: List vs Map",
//      R"(
//            var l = [10, nil, 30];
//            print("--- List ---");
//            // List 应该按 0, 1, 2 顺序遍历，且包含 nil
//            for k,v in pairs(l) do
//                if (v == nil) { print(k .. ":nil"); }
//                else { print(k .. ":" .. v); }
//            end
//
//            var m = {1:10, 3:30}; // 假设你的 parser 支持这种 map 写法，或者 m={}...
//            print("--- Map ---");
//            // Map 迭代顺序不保证，且通常跳过 nil (取决于 luaH_next 原有逻辑)
//            for k,v in pairs(m) do
//                 print(v);
//            end
//       )",
//      "--- List ---\n0:10\n1:nil\n2:30\n--- Map ---\n10\n30"
//      // Map 的输出顺序可能不固定，视具体实现而定，主要验证 List 顺序
//  );
//
//  // ---------------------------------------------------------
//  // 7. 类型检查 (Type)
//  // ---------------------------------------------------------
//  runner.addTest(
//      "Type Info",
//      R"(
//            var l = [];
//            var m = {};
//            print(type(l));
//            print(type(m));
//       )",
//      "array\ntable"
//  );
}
