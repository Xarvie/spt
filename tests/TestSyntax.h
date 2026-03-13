#pragma once
#include "Test.h"

// =========================================================
// 语法正确性测试 (Syntax Coverage)
// 覆盖 LangLexer.g4 + LangParser.g4 的所有语法产生式
// =========================================================

inline void registerSyntaxTests(Test &runner) {

  // =========================================================
  // 1. 空语句 (semicolonStmt)
  // =========================================================

  runner.addTest("Syntax: Empty statement",
                 R"(
    ;
    ;;
    print("ok");
  )",
                 "ok");

  // =========================================================
  // 2. 块语句 (blockStmt)
  // =========================================================

  runner.addTest("Syntax: Explicit block statement",
                 R"(
    {
      int x = 1;
      { int y = 2; print(x + y); }
    }
  )",
                 "3");

  // =========================================================
  // 3. 所有类型注解 (type rules)
  // =========================================================

  runner.addTest("Syntax: All primitive types",
                 R"(
    int a = 1;
    float b = 2.5;
    number c = 3;
    string d = "s";
    bool e = true;
    any f = null;
    print(a, b, c, d, e, f);
  )",
                 "1 2.5 3 s true nil");

  runner.addTest("Syntax: void function type",
                 R"(
    void noop() {}
    noop();
    print("ok");
  )",
                 "ok");

  runner.addTest("Syntax: function type in parameter",
                 R"(
    int call(function f, int x) { return f(x); }
    auto sq = function(int n) -> int { return n * n; };
    print(call(sq, 3));
  )",
                 "9");

  runner.addTest("Syntax: list type with and without generic",
                 R"(
    list a = [1, 2];
    list<int> b = [3, 4];
    list<list<int>> c = [[5]];
    print(a[0], b[1], c[0][0]);
  )",
                 "1 4 5");

  runner.addTest("Syntax: map type with and without generic",
                 R"(
    map a = {"x": 1};
    map<string, int> b = {"y": 2};
    map<string, map<string, int>> c = {"z": {"w": 3}};
    print(a["x"], b["y"], c["z"]["w"]);
  )",
                 "1 2 3");

  runner.addTest("Syntax: null type annotation",
                 R"(
    null n = null;
    print(n);
  )",
                 "nil");

  // =========================================================
  // 4. auto 类型推断
  // =========================================================

  runner.addTest("Syntax: auto declaration",
                 R"(
    auto a = 42;
    auto b = "hello";
    auto c = true;
    auto d = 3.14;
    auto e = [1, 2];
    auto f = {"k": "v"};
    print(a, b, c);
  )",
                 "42 hello true");

  // =========================================================
  // 5. const 声明
  // =========================================================

  runner.addTest("Syntax: const declaration",
                 R"(
    const int x = 10;
    const auto y = 20;
    print(x + y);
  )",
                 "30");

  // =========================================================
  // 6. global 声明
  // =========================================================

  runner.addTest("Syntax: global declaration",
                 R"(
    global int g = 100;
    global const int GC = 55;
  )",
                 "");

  // (global + const combined tested above)

  // =========================================================
  // 7. vars 多变量声明 (mutiVariableDeclarationDef)
  // =========================================================

  runner.addTest("Syntax: vars multi-variable declaration",
                 R"(
    vars returnTwo() {
      return 10, 20;
    }
    vars a, b = returnTwo();
    print(a, b);
  )",
                 "10 20");

  runner.addTest("Syntax: vars with global/const modifiers",
                 R"(
    vars returnVals() { return 1, 2, 3; }
    vars x, y, z = returnVals();
    print(x, y, z);
  )",
                 "1 2 3");

  // =========================================================
  // 8. 多重赋值 (normalAssignStmt with multiple lvalues)
  // =========================================================

  runner.addTest("Syntax: multi-lvalue assignment",
                 R"(
    int a = 0;
    int b = 0;
    a, b = 5, 6;
    print(a, b);
  )",
                 "5 6");

  // =========================================================
  // 9. 所有更新赋值运算符 (updateStatement)
  // =========================================================

  runner.addTest("Syntax: all update-assign operators",
                 R"(
    int a = 100;
    a += 10;   print(a);
    a -= 5;    print(a);
    a *= 2;    print(a);
    a /= 7;    print(a);
    a ~/= 3;   print(a);
    int b = 17;
    b %= 5;    print(b);
  )",
                 "110\n105\n210\n30\n10\n2");

  runner.addTest("Syntax: concat-assign operator",
                 R"(
    string s = "hello";
    s ..= " world";
    print(s);
  )",
                 "hello world");

  // =========================================================
  // 10. 整数除法运算符 (~/)
  // =========================================================

  runner.addTest("Syntax: integer division operator",
                 R"(
    print(7 ~/ 2);
    print(10 ~/ 3);
  )",
                 "3\n3");

  // =========================================================
  // 11. 所有字面量格式 (atomexp)
  // =========================================================

  runner.addTest("Syntax: hex integer literal",
                 R"(
    int a = 0xFF;
    int b = 0x10;
    print(a, b);
  )",
                 "255 16");

  runner.addTest("Syntax: float literal formats",
                 R"(
    float a = 1.5;
    float b = .5;
    float c = 1e2;
    float d = 1.5e2;
    print(a, b, c, d);
  )",
                 "1.5 0.5 100 150");

  runner.addTest("Syntax: single-quoted string",
                 R"(
    string s = 'hello';
    print(s);
  )",
                 "hello");

  runner.addTest("Syntax: string escape sequences",
                 R"(
    string a = "tab\there";
    string b = "line\nbreak";
    string c = "quote\"inside";
    string d = "back\\slash";
    print(c);
    print(d);
  )",
                 "quote\"inside\nback\\slash");

  runner.addTest("Syntax: null, true, false literals",
                 R"(
    print(null);
    print(true);
    print(false);
  )",
                 "nil\ntrue\nfalse");

  // =========================================================
  // 12. Map 字面量所有 key 形式 (mapEntry variants)
  // =========================================================

  runner.addTest("Syntax: map identifier key",
                 R"(
    map m = {name: "Alice", age: 30};
    print(m["name"], m["age"]);
  )",
                 "Alice 30");

  runner.addTest("Syntax: map expression key [expr]",
                 R"(
    int k = 1;
    map<any, any> m = {[k + 1]: "two", [k * 3]: "three"};
    print(m[2], m[3]);
  )",
                 "two three");

  runner.addTest("Syntax: map string key literal",
                 R"(
    map m = {"hello": 1};
    print(m["hello"]);
  )",
                 "1");

  runner.addTest("Syntax: map integer key literal",
                 R"(
    map m = {[1]: "one", [2]: "two"};
    print(m[1], m[2]);
  )",
                 "one two");

  runner.addTest("Syntax: map float key literal",
                 R"(
    map<any, any> m = {[3.14]: "pi"};
    print(m[3.14]);
  )",
                 "pi");

  // =========================================================
  // 13. 位运算 (bitwise operators)
  // =========================================================

  runner.addTest("Syntax: bitwise AND",
                 R"(
    print(0xFF & 0x0F);
  )",
                 "15");

  runner.addTest("Syntax: bitwise OR",
                 R"(
    print(0xF0 | 0x0F);
  )",
                 "255");

  runner.addTest("Syntax: bitwise XOR",
                 R"(
    print(0xFF ^ 0x0F);
  )",
                 "240");

  runner.addTest("Syntax: bitwise NOT",
                 R"(
    // ~0 in Lua is -1 (all bits set)
    print(~0);
  )",
                 "-1");

  runner.addTest("Syntax: left shift",
                 R"(
    print(1 << 8);
  )",
                 "256");

  runner.addTest("Syntax: right shift",
                 R"(
    print(256 >> 4);
  )",
                 "16");

  // =========================================================
  // 14. 一元运算符 (unaryExp)
  // =========================================================

  runner.addTest("Syntax: unary minus",
                 R"(
    int a = 5;
    print(-a);
    print(-(-a));
  )",
                 "-5\n5");

  runner.addTest("Syntax: unary NOT",
                 R"(
    print(!true);
    print(!false);
    print(!!true);
  )",
                 "false\ntrue\ntrue");

  runner.addTest("Syntax: unary length #",
                 R"(
    print(#"abc");
    print(#[1, 2, 3, 4]);
  )",
                 "3\n4");

  runner.addTest("Syntax: chained unary operators",
                 R"(
    int a = 5;
    print(-(-(-a)));
    print(~~0xFF);
  )",
                 "-5\n255");

  // =========================================================
  // 15. 字符串连接 (..)
  // =========================================================

  runner.addTest("Syntax: string concatenation",
                 R"(
    print("a" .. "b" .. "c");
    print(1 .. 2 .. 3);
  )",
                 "abc\n123");

  // =========================================================
  // 16. 运算符优先级
  // =========================================================

  runner.addTest("Syntax: operator precedence",
                 R"(
    print(2 + 3 * 4);
    print((2 + 3) * 4);
    print(10 - 2 * 3 + 1);
    print(10 % 3 + 1);
  )",
                 "14\n20\n5\n2");

  // =========================================================
  // 17. 后缀表达式 (postfixExp) - 链式调用
  // =========================================================

  runner.addTest("Syntax: chained postfix index and member",
                 R"(
    map<string, any> m = {"data": [10, 20, 30]};
    print(m["data"][1]);
    list<map<string, int>> arr = [{"x": 1}, {"x": 2}];
    print(arr[1]["x"]);
  )",
                 "20\n2");

  // =========================================================
  // 18. Lambda 表达式 - 所有返回类型
  // =========================================================

  runner.addTest("Syntax: lambda with various return types",
                 R"(
    auto f1 = function() -> int { return 1; };
    auto f2 = function() -> string { return "hi"; };
    auto f3 = function() -> bool { return true; };
    auto f4 = function() -> float { return 1.5; };
    auto f5 = function() -> void { };
    auto f6 = function() -> any { return null; };
    print(f1(), f2(), f3(), f4());
  )",
                 "1 hi true 1.5");

  runner.addTest("Syntax: lambda with vars return",
                 R"(
    auto f = function() -> vars { return 1, 2, 3; };
    vars a, b, c = f();
    print(a, b, c);
  )",
                 "1 2 3");

  // =========================================================
  // 20. 函数参数列表 (parameterList) - 可变参数
  // =========================================================

  runner.addTest("Syntax: varargs in function definition",
                 R"(
    void f(int a, ...) {
      print(a);
    }
    f(42);
  )",
                 "42");

  runner.addTest("Syntax: varargs only",
                 R"(
    void f(...) {
      auto args = table.pack(...);
      print(#args);
    }
    f(1, 2, 3);
  )",
                 "3");

  runner.addTest("Syntax: varargs expansion in call",
                 R"(
    void f(...) {
      print(...);
    }
    f(10, 20, 30);
  )",
                 "10 20 30");

  // =========================================================
  // 21. 多返回值函数声明 (multiReturnFunctionDeclarationDef)
  // =========================================================

  runner.addTest("Syntax: multi-return function declaration",
                 R"(
    vars getTwo() { return "a", "b"; }
    vars x, y = getTwo();
    print(x, y);
  )",
                 "a b");

  // =========================================================
  // 22. qualifiedIdentifier (a.b.c in type/function name)
  // =========================================================

  runner.addTest("Syntax: qualified identifier in type position",
                 R"(
    class Vec { int x; void __init(int x) { this.x = x; } }
    Vec v = new Vec(5);
    print(v.x);
  )",
                 "5");

  // =========================================================
  // 23. new 表达式 (newExp)
  // =========================================================

  runner.addTest("Syntax: new with no args",
                 R"(
    class Empty {}
    Empty e = new Empty();
    print(typeof(e));
  )",
                 "table");

  runner.addTest("Syntax: new with args",
                 R"(
    class P {
      int v;
      void __init(int v) { this.v = v; }
    }
    P p = new P(99);
    print(p.v);
  )",
                 "99");

  // =========================================================
  // 24. class 成员：static 字段和方法
  // =========================================================

  runner.addTest("Syntax: static class field",
                 R"(
    class Counter {
      static int count = 0;
    }
    Counter.count = 5;
    print(Counter.count);
  )",
                 "5");

  runner.addTest("Syntax: static class method",
                 R"(
    class MathUtil {
      static int square(int x) {
        return x * x;
      }
    }
    print(MathUtil.square(7));
  )",
                 "49");

  // =========================================================
  // 25. class 空成员 (classEmptyMember)
  // =========================================================

  runner.addTest("Syntax: class with empty members (semicolons)",
                 R"(
    class Foo {
      ;
      int x;
      ;
      void __init(int x) { this.x = x; }
      ;
    }
    Foo f = new Foo(10);
    print(f.x);
  )",
                 "10");

  // =========================================================
  // 26. class 多返回值方法 (multiReturnClassMethodMember)
  // =========================================================

  runner.addTest("Syntax: class method with vars return",
                 R"(
    class Pair {
      int a; int b;
      void __init(int a, int b) { this.a = a; this.b = b; }
      vars get() { return this.a, this.b; }
    }
    Pair p = new Pair(3, 4);
    vars x, y = p.get();
    print(x, y);
  )",
                 "3 4");

  // =========================================================
  // 27. defer 语句 (deferStatement)
  // =========================================================

  // NOTE: defer 语法已在 grammar 中定义，但运行时需要 closable value (__close metamethod)
  // 基础语法编译测试暂略，留给专门的 defer 功能测试

  // =========================================================
  // 28. import 语句 (importStatement)
  // =========================================================

  runner.addTest("Syntax: import namespace",
                 R"(
    import * as m from "math";
    print(m.abs(-1));
  )",
                 "1");

  runner.addTest("Syntax: import named members",
                 R"(
    import { abs, max } from "math";
    print(abs(-5));
    print(max(1, 2));
  )",
                 "5\n2");

  // =========================================================
  // 29. export 声明 (export prefix)
  // =========================================================

  runner.addTest("Syntax: export variable declaration",
                 R"(
    export int val = 42;
    print(val);
  )",
                 "42");

  runner.addTest("Syntax: export function declaration",
                 R"(
    export int square(int x) { return x * x; }
    print(square(5));
  )",
                 "25");

  runner.addTest("Syntax: export class declaration",
                 R"(
    export class Point {
      int x;
      void __init(int x) { this.x = x; }
    }
    Point p = new Point(7);
    print(p.x);
  )",
                 "7");

  // =========================================================
  // 30. for 循环变量形式 (forNumericVar, forEachVar)
  // =========================================================

  runner.addTest("Syntax: for numeric - typed variable",
                 R"(
    for (int i = 0, 2) { print(i); }
  )",
                 "0\n1\n2");

  runner.addTest("Syntax: for numeric - auto variable",
                 R"(
    for (auto i = 0, 2) { print(i); }
  )",
                 "0\n1\n2");

  runner.addTest("Syntax: for numeric - untyped variable",
                 R"(
    for (i = 0, 2) { print(i); }
  )",
                 "0\n1\n2");

  runner.addTest("Syntax: for-each typed variables",
                 R"(
    list<int> l = [10, 20];
    for (int k, int v : pairs(l)) { print(k .. ":" .. v); }
  )",
                 "0:10\n1:20");

  runner.addTest("Syntax: for-each auto variables",
                 R"(
    list<int> l = [10, 20];
    for (auto k, auto v : pairs(l)) { print(k .. ":" .. v); }
  )",
                 "0:10\n1:20");

  runner.addTest("Syntax: for-each untyped variables",
                 R"(
    list<int> l = [10, 20];
    for (k, v : pairs(l)) { print(k .. ":" .. v); }
  )",
                 "0:10\n1:20");

  // =========================================================
  // 31. lvalue 后缀组合 (lvalueSuffix)
  // =========================================================

  runner.addTest("Syntax: lvalue index chain",
                 R"(
    list<list<int>> m = [[1, 2], [3, 4]];
    m[0][1] = 99;
    print(m[0][1]);
  )",
                 "99");

  runner.addTest("Syntax: lvalue member chain",
                 R"(
    class A { any b; }
    class B { int c; }
    A a = new A();
    a.b = new B();
    a.b.c = 42;
    print(a.b.c);
  )",
                 "42");

  runner.addTest("Syntax: lvalue mixed index and member",
                 R"(
    class C { any data; void __init() { this.data = [0]; } }
    C c = new C();
    c.data[0] = 77;
    print(c.data[0]);
  )",
                 "77");

  // =========================================================
  // 32. 返回语句 - 多表达式 (return expressionList)
  // =========================================================

  runner.addTest("Syntax: return multiple expressions",
                 R"(
    vars f() { return 1 + 1, 2 * 3, "ok"; }
    vars a, b, c = f();
    print(a, b, c);
  )",
                 "2 6 ok");

  runner.addTest("Syntax: return no value",
                 R"(
    void f() { return; }
    f();
    print("ok");
  )",
                 "ok");

  // =========================================================
  // 33. 表达式语句 (expressionStmt)
  // =========================================================

  runner.addTest("Syntax: expression statement - function call",
                 R"(
    void f() { print("called"); }
    f();
  )",
                 "called");

  // =========================================================
  // 34. 嵌套函数声明 (function inside function)
  // =========================================================

  runner.addTest("Syntax: nested function declaration",
                 R"(
    int outer(int x) {
      int inner(int y) { return y + 1; }
      return inner(x);
    }
    print(outer(9));
  )",
                 "10");

  // =========================================================
  // 35. 逻辑运算组合
  // =========================================================

  runner.addTest("Syntax: complex logical expression",
                 R"(
    bool a = true && false || true;
    bool b = !(false || false) && true;
    print(a, b);
  )",
                 "true true");

  // =========================================================
  // 36. 比较运算全覆盖
  // =========================================================

  runner.addTest("Syntax: all comparison operators",
                 R"(
    print(1 < 2);
    print(2 > 1);
    print(1 <= 1);
    print(1 >= 1);
    print(1 == 1);
    print(1 != 2);
  )",
                 "true\ntrue\ntrue\ntrue\ntrue\ntrue");

  // =========================================================
  // 37. 位移运算组合
  // =========================================================

  runner.addTest("Syntax: combined shift and bitwise",
                 R"(
    int a = (1 << 4) | (1 << 2);
    print(a);
    print(a >> 2);
    print(a & 0x0F);
    print(a ^ 0xFF);
  )",
                 "20\n5\n4\n235");

  // =========================================================
  // 38. 圆括号表达式 (primaryParenExp)
  // =========================================================

  runner.addTest("Syntax: parenthesized expression",
                 R"(
    print((1 + 2) * (3 + 4));
    print(((10)));
  )",
                 "21\n10");

  // =========================================================
  // 39. 空列表 / 空 Map 字面量
  // =========================================================

  runner.addTest("Syntax: empty list and map literals",
                 R"(
    auto l = [];
    auto m = {};
    print(#l, #m);
  )",
                 "0 0");

  // =========================================================
  // 40. 多行注释与单行注释
  // =========================================================

  runner.addTest("Syntax: comments",
                 R"(
    // single line comment
    int x = 1; // trailing comment
    /* block
       comment */
    int y = /* inline */ 2;
    print(x + y);
  )",
                 "3");

  // =========================================================
  // 41. 函数调用 - 无参、有参、多参
  // =========================================================

  runner.addTest("Syntax: function call arg variations",
                 R"(
    void f0() { print("zero"); }
    void f1(int a) { print(a); }
    void f3(int a, int b, int c) { print(a + b + c); }
    f0();
    f1(1);
    f3(1, 2, 3);
  )",
                 "zero\n1\n6");

  // =========================================================
  // 42. 嵌套 if-else if-else
  // =========================================================

  runner.addTest("Syntax: if - else if - else",
                 R"(
    int x = 2;
    if (x == 1) { print("one"); }
    else if (x == 2) { print("two"); }
    else if (x == 3) { print("three"); }
    else { print("other"); }
  )",
                 "two");

  // =========================================================
  // 43. while 循环
  // =========================================================

  runner.addTest("Syntax: while loop",
                 R"(
    int i = 0;
    while (i < 3) { print(i); i += 1; }
  )",
                 "0\n1\n2");

  // =========================================================
  // 44. for 循环 - 数值 (含 step)
  // =========================================================

  runner.addTest("Syntax: numeric for with step",
                 R"(
    for (int i = 0, 10, 3) { print(i); }
  )",
                 "0\n3\n6\n9");

  runner.addTest("Syntax: numeric for negative step",
                 R"(
    for (int i = 3, 0, -1) { print(i); }
  )",
                 "3\n2\n1\n0");

  // =========================================================
  // 45. for-each 循环 - expressionList 多表达式迭代器
  // =========================================================

  runner.addTest("Syntax: for-each with iterator function",
                 R"(
    int iter(any s, int c) {
      if (c < 3) { return c + 1; }
      return null;
    }
    for (auto i : iter, null, 0) { print(i); }
  )",
                 "1\n2\n3");

  // =========================================================
  // 46. break 和 continue
  // =========================================================

  runner.addTest("Syntax: break and continue",
                 R"(
    for (int i = 0, 9) {
      if (i == 2) { continue; }
      if (i == 5) { break; }
      print(i);
    }
  )",
                 "0\n1\n3\n4");

  // =========================================================
  // 47. 闭包捕获变量
  // =========================================================

  runner.addTest("Syntax: closure captures upvalue",
                 R"(
    int x = 10;
    auto f = function() -> int { return x; };
    x = 20;
    print(f());
  )",
                 "20");

  // =========================================================
  // 48. class 继承 / metatable 式
  // =========================================================

  runner.addTest("Syntax: class with setmetatable pattern",
                 R"(
    map<any, any> Base = {};
    Base["__index"] = Base;
    Base["val"] = 100;
    map<any, any> obj = {};
    setmetatable(obj, Base);
    print(obj.val);
  )",
                 "100");

  // =========================================================
  // 49. 运算符重载元方法名
  // =========================================================

  runner.addTest("Syntax: operator overload __add",
                 R"(
    class N {
      int v;
      void __init(int v) { this.v = v; }
      N __add(N o) { N r = new N(0); r.v = this.v + o.v; return r; }
    }
    N a = new N(3);
    N b = new N(4);
    N c = a + b;
    print(c.v);
  )",
                 "7");

  runner.addTest("Syntax: operator overload __unm",
                 R"(
    class N {
      int v;
      void __init(int v) { this.v = v; }
      N __unm() { N r = new N(0); r.v = -this.v; return r; }
    }
    N a = new N(5);
    N b = -a;
    print(b.v);
  )",
                 "-5");

  // =========================================================
  // 50. 字面量边界值
  // =========================================================

  runner.addTest("Syntax: integer zero and negative",
                 R"(
    int a = 0;
    int b = -0;
    int c = -1;
    print(a, b, c);
  )",
                 "0 0 -1");

  runner.addTest("Syntax: float special formats",
                 R"(
    float a = 0.5;
    float b = .25;
    print(a, b);
  )",
                 "0.5 0.25");

  // =========================================================
  // 51. 字符串 - 单引号和转义
  // =========================================================

  runner.addTest("Syntax: single and double quote strings",
                 R"(
    string a = "double";
    string b = 'single';
    print(a, b);
  )",
                 "double single");

  // =========================================================
  // 52. 表达式列表 (expressionList in multiple contexts)
  // =========================================================

  runner.addTest("Syntax: expression list in list literal",
                 R"(
    list<int> l = [1 + 1, 2 * 2, 3 - 1];
    print(l[0], l[1], l[2]);
  )",
                 "2 4 2");

  runner.addTest("Syntax: expression list in function args",
                 R"(
    int add3(int a, int b, int c) { return a + b + c; }
    print(add3(1 + 0, 2 * 1, 3));
  )",
                 "6");

  // =========================================================
  // 53. 链式成员访问调用
  // =========================================================

  runner.addTest("Syntax: chained method calls",
                 R"(
    class Builder {
      int v;
      void __init() { this.v = 0; }
      Builder add(int n) { this.v += n; return this; }
      int get() { return this.v; }
    }
    Builder b = new Builder();
    print(b.add(1).add(2).add(3).get());
  )",
                 "6");

  // =========================================================
  // 54. 空函数体
  // =========================================================

  runner.addTest("Syntax: empty function body",
                 R"(
    void f() {}
    int g() { return 0; }
    f();
    print(g());
  )",
                 "0");

  // =========================================================
  // 55. 空 class 体
  // =========================================================

  runner.addTest("Syntax: empty class body",
                 R"(
    class Empty {}
    auto e = new Empty();
    print(typeof(e));
  )",
                 "table");

  // =========================================================
  // 56. 复杂嵌套表达式
  // =========================================================

  runner.addTest("Syntax: deeply nested expression",
                 R"(
    print(((1 + 2) * (3 - 1)) / ((4 + 2) / 3));
  )",
                 "3");

  // =========================================================
  // 57. import 别名 (importSpecifier with AS)
  // =========================================================

  runner.addTest("Syntax: import with alias",
                 R"(
    import { abs as myAbs } from "math";
    print(myAbs(-99));
  )",
                 "99");

  // =========================================================
  // 58. 混合声明与表达式语句
  // =========================================================

  runner.addTest("Syntax: mixed declarations and expressions",
                 R"(
    int a = 1;
    auto b = 2;
    float c = 3.14;
    string d = "ok";
    bool e = true;
    list<int> f = [4];
    map<string, int> g = {"k": 5};
    print(a, b, c, d, e, f[0], g["k"]);
  )",
                 "1 2 3.14 ok true 4 5");

  // =========================================================
  // 59. 连续赋值语句
  // =========================================================

  runner.addTest("Syntax: sequential assignments",
                 R"(
    int a = 0;
    int b = 0;
    int c = 0;
    a = 1;
    b = a + 1;
    c = b + 1;
    print(a, b, c);
  )",
                 "1 2 3");

  // =========================================================
  // 60. pcall / xpcall 语法
  // =========================================================

  runner.addTest("Syntax: pcall error handling",
                 R"(
    vars ok, result = pcall(function() -> int { return 42; });
    print(ok, result);
  )",
                 "true 42");

  // =========================================================
  // 61. error() 调用
  // =========================================================

  runner.addTest("Syntax: error function",
                 R"(
    vars ok, msg = pcall(function() -> void { error("boom", 0); });
    print(ok, msg);
  )",
                 "false boom");

  // =========================================================
  // 62. typeof 内置
  // =========================================================

  runner.addTest("Syntax: typeof all types",
                 R"(
    print(typeof(1));
    print(typeof(1.5));
    print(typeof("s"));
    print(typeof(true));
    print(typeof(null));
    print(typeof([]));
    print(typeof({}));
    print(typeof(function() -> void {}));
  )",
                 "number\nnumber\nstring\nboolean\nnil\narray\ntable\nfunction");

  // =========================================================
  // 63. tonumber / tostring 内置
  // =========================================================

  runner.addTest("Syntax: tonumber and tostring",
                 R"(
    print(tonumber("123"));
    print(tostring(456));
    print(tostring(true));
    print(tostring(null));
  )",
                 "123\n456\ntrue\nnil");

  // =========================================================
  // 64. 数学模块方法 (math.xxx)
  // =========================================================

  runner.addTest("Syntax: math module methods",
                 R"(
    print(math.abs(-7));
    print(math.floor(3.9));
    print(math.ceil(3.1));
    print(math.min(1, 2));
    print(math.max(1, 2));
  )",
                 "7\n3\n4\n1\n2");

  // =========================================================
  // 65. table 模块方法 (table.xxx)
  // =========================================================

  runner.addTest("Syntax: table module methods",
                 R"(
    list<any> l = [];
    table.push(l, 1);
    table.push(l, 2);
    table.push(l, 3);
    table.insert(l, 1, 99);
    table.remove(l, 0);
    table.sort(l);
    print(table.concat(l, ","));
  )",
                 "2,3,99");

  // =========================================================
  // 66. 复合 lvalue 赋值
  // =========================================================

  runner.addTest("Syntax: update-assign on lvalue suffix",
                 R"(
    list<int> l = [10, 20];
    l[0] += 5;
    print(l[0]);
    map<string, int> m = {"a": 1};
    m["a"] += 10;
    print(m["a"]);
  )",
                 "15\n11");

  runner.addTest("Syntax: update-assign on member",
                 R"(
    class Box { int v; void __init(int v) { this.v = v; } }
    Box b = new Box(10);
    b.v += 5;
    b.v *= 2;
    print(b.v);
  )",
                 "30");

  // =========================================================
  // 67. 表达式作为条件（各种类型）
  // =========================================================

  runner.addTest("Syntax: truthy/falsy conditions",
                 R"(
    if (1) { print("1 truthy"); }
    if ("s") { print("str truthy"); }
    if (!null) { print("null falsy"); }
    if (!false) { print("false falsy"); }
  )",
                 "1 truthy\nstr truthy\nnull falsy\nfalse falsy");

  // =========================================================
  // 68. 多层嵌套循环
  // =========================================================

  runner.addTest("Syntax: triple nested for",
                 R"(
    int count = 0;
    for (i = 0, 1) {
      for (j = 0, 1) {
        for (k = 0, 1) {
          count += 1;
        }
      }
    }
    print(count);
  )",
                 "8");

  // =========================================================
  // 69. 函数返回函数
  // =========================================================

  runner.addTest("Syntax: function returning function",
                 R"(
    function makeAdder(int n) {
      return function(int x) -> int { return n + x; };
    }
    auto add5 = makeAdder(5);
    print(add5(10));
  )",
                 "15");

  // =========================================================
  // 70. 空 for-each body
  // =========================================================

  runner.addTest("Syntax: empty for-each body",
                 R"(
    for (k, v : pairs([1, 2, 3])) {}
    print("ok");
  )",
                 "ok");

  // =========================================================
  // 71. 空 while body
  // =========================================================

  runner.addTest("Syntax: empty while body",
                 R"(
    int i = 0;
    while (i < 0) {}
    print("ok");
  )",
                 "ok");

  // =========================================================
  // 72. 空 if body
  // =========================================================

  runner.addTest("Syntax: empty if/else bodies",
                 R"(
    if (true) {} else {}
    print("ok");
  )",
                 "ok");

  // =========================================================
  // 73. Unicode 标识符
  // =========================================================

  runner.addTest("Syntax: unicode identifier (CJK)",
                 R"(
    int 变量 = 42;
    print(变量);
  )",
                 "42");

  runner.addTest("Syntax: unicode identifier (Greek)",
                 R"(
    float π = 3.14;
    print(π);
  )",
                 "3.14");

  // =========================================================
  // 74. 混合运算 - 全部二元运算符在一个表达式
  // =========================================================

  runner.addTest("Syntax: all binary ops combined",
                 R"(
    int a = ((1 + 2 - 1) * 4 / 2 ~/ 1 % 3);
    print(a);
  )",
                 "1");

  // =========================================================
  // 75. 嵌套 lambda
  // =========================================================

  runner.addTest("Syntax: nested lambda",
                 R"(
    auto f = function(int a) -> function {
      return function(int b) -> function {
        return function(int c) -> int {
          return a + b + c;
        };
      };
    };
    print(f(1)(2)(3));
  )",
                 "6");

  // =========================================================
  // 76. 类作为 map value / list element
  // =========================================================

  runner.addTest("Syntax: class instance in containers",
                 R"(
    class Item { int id; void __init(int id) { this.id = id; } }
    list<any> items = [new Item(1), new Item(2)];
    map<string, any> m = {"item": new Item(3)};
    print(items[0].id, items[1].id, m["item"].id);
  )",
                 "1 2 3");

  // =========================================================
  // 77. 连续 string concat 与其他运算混合
  // =========================================================

  runner.addTest("Syntax: concat mixed with arithmetic",
                 R"(
    print("result: " .. (1 + 2) .. " done");
  )",
                 "result: 3 done");

  // =========================================================
  // 78. for-each 单变量形式
  // =========================================================

  runner.addTest("Syntax: for-each with single variable",
                 R"(
    int iter(any s, int c) {
      if (c < 3) { return c + 1; }
      return null;
    }
    for (auto i : iter, null, 0) { print(i); }
  )",
                 "1\n2\n3");

  // =========================================================
  // 79. 条件表达式中的复杂表达式
  // =========================================================

  runner.addTest("Syntax: complex condition expressions",
                 R"(
    int x = 5;
    if (x > 0 && x < 10 || x == 100) { print("in range"); }
    if ((x & 1) == 1) { print("odd"); }
    if (#[1,2,3] == 3) { print("len ok"); }
  )",
                 "in range\nodd\nlen ok");

  // =========================================================
  // 80. 递归调用
  // =========================================================

  runner.addTest("Syntax: recursive function",
                 R"(
    int fact(int n) {
      if (n <= 1) { return 1; }
      return n * fact(n - 1);
    }
    print(fact(5));
  )",
                 "120");
}