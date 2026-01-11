#pragma once
#include "TestRunner.h"

// =========================================================
// 5. 数据结构 - List (Lists)
// =========================================================

inline void registerLists(TestRunner &runner) {
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

inline void registerMaps(TestRunner &runner) {
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

inline void registerStrings(TestRunner &runner) {
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
