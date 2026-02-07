#pragma once
#include "TestRunner.h"

// =========================================================
// 12. 内置函数测试 (Built-in Functions)
// =========================================================

inline void registerBuiltinFunctions(TestRunner &runner) {
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
                 "true\nfalse\ntrue\ntrue\nfalse");

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
            int i = 4;
            i~/=3.1;
            print(i);
            print(4~/3.1);
            print(math.abs(-5));
            print(math.abs(5));
            print(math.abs(-3.14));
            print(math.floor(3.7));
            print(math.floor(3.2));
            print(math.ceil(3.2));
            print(math.ceil(3.7));
            print(math.round(3.4));
            print(math.round(3.5));
            print(math.round(3.6));
       )",
                 "1\n1\n5\n5\n3.14\n3\n3\n4\n4\n3\n4\n4");

  runner.addTest("Builtin - sqrt pow",
                 R"(
            print(toInt(math.sqrt(16)));
            print(toInt(math.sqrt(9)));
            print(toInt(math.pow(2, 10)));
            print(toInt(math.pow(3, 3)));
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

            vars ok2, result2 = pcall(divide, 10, 0);
            print(ok2, result2);
            vars ok, result = pcall(divide, 10, 1);
            print(ok, result);
       )",
                 "false division by zero\ntrue 10");
}
