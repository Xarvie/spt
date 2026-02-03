#include "TestRunner.h"
#include "catch_amalgamated.hpp"

TEST_CASE("test_map", "[stdlibs][map]") {
  spt::test::TestRunner runner;

  // =========================================================
  // 6.1 Map - Int as Key, Various Values
  // =========================================================
  runner.runTest("Map Int Key - Int Value",
                 R"(
            map<int, int> m = {};
            m[1] = 100;
            m[2] = 200;
            m[3] = 300;
            print(m[1]);
            print(m[2]);
            print(m[3]);
            print(m.size);
       )",
                 "100\n200\n300\n3");

  runner.runTest("Map Int Key - Float Value",
                 R"(
            map<int, float> m = {};
            m[1] = 3.14;
            m[2] = 2.71;
            m[3] = 1.61;
            print(m[1]);
            print(m[2]);
            print(m[3]);
       )",
                 "3.14\n2.71\n1.61");

  runner.runTest("Map Int Key - String Value",
                 R"(
            map<int, string> m = {};
            m[1] = "apple";
            m[2] = "banana";
            m[3] = "cherry";
            print(m[1]);
            print(m[2]);
            print(m[3]);
       )",
                 "apple\nbanana\ncherry");

  runner.runTest("Map Int Key - Bool Value",
                 R"(
            map<int, bool> m = {};
            m[1] = true;
            m[2] = false;
            m[3] = true;
            print(m[1]);
            print(m[2]);
            print(m[3]);
       )",
                 "true\nfalse\ntrue");

  runner.runTest("Map Int Key - List Value",
                 R"(
            map<int, list<int>> m = {};
            m[1] = [1, 2, 3];
            m[2] = [4, 5, 6];
            m[3] = [7, 8, 9];
            print(m[1][0]);
            print(m[2][1]);
            print(m[3][2]);
       )",
                 "1\n5\n9");

  runner.runTest("Map Int Key - Map Value",
                 R"(
            map<int, map<string, int>> m = {};
            m[1] = {"a": 10, "b": 20};
            m[2] = {"x": 100, "y": 200};
            print(m[1]["a"]);
            print(m[1]["b"]);
            print(m[2]["x"]);
            print(m[2]["y"]);
       )",
                 "10\n20\n100\n200");

  // =========================================================
  // 6.2 Map - String as Key, Various Values
  // =========================================================
  runner.runTest("Map String Key - Int Value",
                 R"(
            map<string, int> m = {};
            m["one"] = 1;
            m["two"] = 2;
            m["three"] = 3;
            print(m["one"]);
            print(m["two"]);
            print(m["three"]);
       )",
                 "1\n2\n3");

  runner.runTest("Map String Key - Float Value",
                 R"(
            map<string, float> m = {};
            m["pi"] = 3.14;
            m["e"] = 2.71;
            m["golden"] = 1.61;
            print(m["pi"]);
            print(m["e"]);
            print(m["golden"]);
       )",
                 "3.14\n2.71\n1.61");

  runner.runTest("Map String Key - String Value",
                 R"(
            map<string, string> m = {};
            m["name"] = "Alice";
            m["city"] = "Beijing";
            m["country"] = "China";
            print(m["name"]);
            print(m["city"]);
            print(m["country"]);
       )",
                 "Alice\nBeijing\nChina");

  runner.runTest("Map String Key - Bool Value",
                 R"(
            map<string, bool> m = {};
            m["active"] = true;
            m["visible"] = false;
            m["enabled"] = true;
            print(m["active"]);
            print(m["visible"]);
            print(m["enabled"]);
       )",
                 "true\nfalse\ntrue");

  runner.runTest("Map String Key - List Value",
                 R"(
            map<string, list<int>> m = {};
            m["even"] = [2, 4, 6];
            m["odd"] = [1, 3, 5];
            print(m["even"][0]);
            print(m["even"][2]);
            print(m["odd"][1]);
       )",
                 "2\n6\n3");

  // =========================================================
  // 6.3 Map - Bool as Key, Various Values
  // =========================================================
  runner.runTest("Map Bool Key - Int Value",
                 R"(
            map<bool, int> m = {};
            m[true] = 100;
            m[false] = 200;
            print(m[true]);
            print(m[false]);
            print(m.size);
       )",
                 "100\n200\n2");

  runner.runTest("Map Bool Key - String Value",
                 R"(
            map<bool, string> m = {};
            m[true] = "yes";
            m[false] = "no";
            print(m[true]);
            print(m[false]);
       )",
                 "yes\nno");

  runner.runTest("Map Bool Key - List Value",
                 R"(
            map<bool, list<int>> m = {};
            m[true] = [1, 2, 3];
            m[false] = [4, 5, 6];
            print(m[true].length);
            print(m[false][0]);
       )",
                 "3\n4");

  // =========================================================
  // 6.4 Map - Float as Key, Various Values
  // =========================================================
  runner.runTest("Map Float Key - Int Value",
                 R"(
            map<float, int> m = {};
            m[1.5] = 10;
            m[2.5] = 20;
            m[3.5] = 30;
            print(m[1.5]);
            print(m[2.5]);
            print(m[3.5]);
       )",
                 "10\n20\n30");

  runner.runTest("Map Float Key - String Value",
                 R"(
            map<float, string> m = {};
            m[1.0] = "one";
            m[2.0] = "two";
            print(m[1.0]);
            print(m[2.0]);
       )",
                 "one\ntwo");

  // =========================================================
  // 6.5 Map - Mixed Keys
  // =========================================================
  runner.runTest("Map Mixed Key Types",
                 R"(
            map<any, int> m = {};
            m[1] = 100;
            m["key"] = 200;
            m[true] = 300;
            print(m[1]);
            print(m["key"]);
            print(m[true]);
            print(m.size);
       )",
                 "100\n200\n300\n3");

  // =========================================================
  // 6.6 Map - Special Cases
  // =========================================================
  runner.runTest("Map Update Value",
                 R"(
            map<int, int> m = {};
            m[1] = 10;
            print(m[1]);
            m[1] = 100;
            print(m[1]);
            m[1] = 1000;
            print(m[1]);
       )",
                 "10\n100\n1000");

  runner.runTest("Map Overwrite Different Type",
                 R"(
            map<string, any> m = {};
            m["value"] = 42;
            print(m["value"]);
            m["value"] = "changed";
            print(m["value"]);
            m["value"] = true;
            print(m["value"]);
       )",
                 "42\nchanged\ntrue");

  runner.runTest("Map Key Collision - Same Value",
                 R"(
            map<string, int> m = {};
            m["a"] = 1;
            m["a"] = 2;
            m["a"] = 3;
            print(m.size);
            print(m["a"]);
       )",
                 "1\n3");

  runner.runTest("Map With Negative Int Key",
                 R"(
            map<int, string> m = {};
            m[-1] = "minus one";
            m[-10] = "minus ten";
            m[0] = "zero";
            print(m[-1]);
            print(m[-10]);
            print(m[0]);
       )",
                 "minus one\nminus ten\nzero");

  runner.runTest("Map Large Int Keys",
                 R"(
            map<int, int> m = {};
            m[1000000] = 1;
            m[2000000] = 2;
            m[999999] = 3;
            print(m[1000000]);
            print(m[2000000]);
            print(m[999999]);
       )",
                 "1\n2\n3");

  // =========================================================
  // 6.7 Map - Complex Keys (List, Map, Function, Class, Closure, Fiber)
  // =========================================================
  runner.runTest("Map List as Key",
                 R"(
            map<list<int>, int> m = {};
            list<int> k1 = [1, 2, 3];
            list<int> k2 = [4, 5, 6];
            m[k1] = 100;
            m[k2] = 200;
            print(m[k1]);
            print(m[k2]);
            print(m.size);
       )",
                 "100\n200\n2");

  runner.runTest("Map List as Key - Same List Reference",
                 R"(
            map<list<int>, int> m = {};
            list<int> k = [1, 2, 3];
            m[k] = 100;
            m[k] = 200;
            print(m.size);
            print(m[k]);
       )",
                 "1\n200");

  runner.runTest("Map List as Key - Different Lists With Same Content",
                 R"(
            map<list<int>, int> m = {};
            list<int> k1 = [1, 2, 3];
            list<int> k2 = [1, 2, 3];
            m[k1] = 100;
            m[k2] = 200;
            print(m.size);
            print(m[k1]);
            print(m[k2]);
       )",
                 "2\n100\n200");

  runner.runTest("Map Map as Key",
                 R"(
            map<map<string, int>, int> m = {};
            map<string, int> k1 = {"a": 1, "b": 2};
            map<string, int> k2 = {"x": 10, "y": 20};
            m[k1] = 100;
            m[k2] = 200;
            print(m[k1]);
            print(m[k2]);
            print(m.size);
       )",
                 "100\n200\n2");

  runner.runTest("Map Function as Key",
                 R"(
            map<function, int> m = {};
            auto f1 = function(int x) -> int { return x * 2; };
            auto f2 = function(int x) -> int { return x * 3; };
            m[f1] = 100;
            m[f2] = 200;
            print(f1(5));
            print(f2(5));
            print(m[f1]);
            print(m[f2]);
            print(m.size);
       )",
                 "10\n15\n100\n200\n2");

  runner.runTest("Map Function as Key - Same Reference",
                 R"(
            map<function, int> m = {};
            auto f = function(int x) -> int { return x + 1; };
            m[f] = 100;
            m[f] = 200;
            print(m.size);
            print(m[f]);
            print(f(10));
       )",
                 "1\n200\n11");

  runner.runTest("Map Class as Key",
                 R"(
            map<any, int> m = {};
            class Point { int x; int y; void __init(int x, int y) { this.x = x; this.y = y; } }
            class Circle { float radius; void __init(float r) { this.radius = r; } }
            m[Point] = 1;
            m[Circle] = 2;
            print(m[Point]);
            print(m[Circle]);
            print(m.size);
       )",
                 "1\n2\n2");

  runner.runTest("Map Closure as Key",
                 R"(
            map<any, int> m = {};
            var counter1;
            var counter2;
            {
                int count = 0;
                counter1 = function() -> int { count = count + 1; return count; };
            }
            {
                int count = 100;
                counter2 = function() -> int { count = count + 1; return count; };
            }
            m[counter1] = 10;
            m[counter2] = 20;
            print(counter1());
            print(counter2());
            print(m[counter1]);
            print(m[counter2]);
            print(m.size);
       )",
                 "1\n101\n10\n20\n2");

  runner.runTest("Map Fiber as Key",
                 R"(
            map<any, int> m = {};
            auto f1 = Fiber.create(function(int x) -> int {
                Fiber.yield(x * 2);
                return x * 3;
            });
            auto f2 = Fiber.create(function(int x) -> int {
                Fiber.yield(x + 10);
                return x + 20;
            });
            m[f1] = 100;
            m[f2] = 200;
            print(f1.call(5));
            print(f1.call(5));
            print(m[f1]);
            print(m[f2]);
            print(m.size);
       )",
                 "10\n15\n100\n200\n2");

  runner.runTest("Map Mixed Complex Keys",
                 R"(
            map<any, string> m = {};
            list<int> k1 = [1, 2];
            map<string, int> k2 = {"a": 1};
            auto f = function() -> int { return 42; };
            class MyClass { int x; }
            
            m[k1] = "list key";
            m[k2] = "map key";
            m[f] = "function key";
            m[MyClass] = "class key";
            
            print(m.size);
            print(m[k1]);
            print(m[k2]);
            print(m[f]);
            print(m[MyClass]);
       )",
                 "4\nlist key\nmap key\nfunction key\nclass key");

  runner.runTest("Map List Key Update",
                 R"(
            map<list<int>, int> m = {};
            list<int> k = [1, 2, 3];
            m[k] = 10;
            print(m[k]);
            m[k] = 100;
            print(m[k]);
            m[k] = 1000;
            print(m[k]);
       )",
                 "10\n100\n1000");

  runner.runTest("Map Function Key Update",
                 R"(
            map<function, string> m = {};
            auto f = function(int x) -> int { return x + 1; };
            m[f] = "first";
            print(m[f]);
            m[f] = "second";
            print(m[f]);
            m[f] = "third";
            print(m[f]);
       )",
                 "first\nsecond\nthird");

  runner.runTest("Map Class Key Value - Instance as Value",
                 R"(
            map<any, string> m = {};
            class Point { int x; int y; void __init(int x, int y) { this.x = x; this.y = y; } }
            m[Point] = "Point class";
            
            Point p = new Point(10, 20);
            print(p.x);
            print(p.y);
            print(m[Point]);
       )",
                 "10\n20\nPoint class");
}