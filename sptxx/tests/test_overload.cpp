// test_overload.cpp - 测试函数重载 overload
// 一个名字注册多个 C++ 函数，根据 Lua 栈参数类型/数量分派。

#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // 注册重载集：int / string / (int,int)
    lua.set_function("print_it", sptxx::overload(
        [](int x) -> std::string { return "int: " + std::to_string(x); },
        [](std::string s) -> std::string { return "str: " + s; },
        [](int a, int b) -> int { return a + b; }
    ));

    // 1. 单 int 重载
    lua.do_string("r1 = print_it(42);");
    {
      std::string r = lua.get_global<std::string>("r1");
      if (r != "int: 42") { std::cerr << "FAIL: int overload = '" << r << "' want 'int: 42'\n"; return 1; }
      std::cout << "PASS: int overload: " << r << "\n";
    }

    // 2. string 重载
    lua.do_string("r2 = print_it(\"hello\");");
    {
      std::string r = lua.get_global<std::string>("r2");
      if (r != "str: hello") { std::cerr << "FAIL: string overload = '" << r << "' want 'str: hello'\n"; return 1; }
      std::cout << "PASS: string overload: " << r << "\n";
    }

    // 3. (int, int) 重载
    lua.do_string("r3 = print_it(3, 4);");
    {
      int r = lua.get_global<int>("r3");
      if (r != 7) { std::cerr << "FAIL: (int,int) overload = " << r << " want 7\n"; return 1; }
      std::cout << "PASS: (int,int) overload: " << r << "\n";
    }

    // 4. 无匹配重载 → 报错
    bool failed = false;
    try {
      lua.do_string("r4 = print_it(true);");
    } catch (const std::exception &) {
      failed = true;
    }
    if (!failed) { std::cerr << "FAIL: bool arg should have no matching overload\n"; return 1; }
    std::cout << "PASS: no-matching overload correctly rejected\n";

    // 5. usertype 方法重载
    struct Calc {
      int base;
      Calc(int b) : base(b) {}
      int apply(int x) const { return base + x; }
      int apply(int x, int y) const { return base + x + y; }
    };
    auto wt = lua.new_usertype<Calc>("Calc");
    wt.constructor<int>();
    // 注册两个重载方法
    wt.set_overload("apply",
        static_cast<int (Calc::*)(int) const>(&Calc::apply),
        static_cast<int (Calc::*)(int, int) const>(&Calc::apply));

    lua.do_string(R"(
        c = Calc(100);
        a1 = c.apply(5);
        a2 = c.apply(5, 6);
    )");
    {
      int a1 = lua.get_global<int>("a1");
      int a2 = lua.get_global<int>("a2");
      if (a1 != 105) { std::cerr << "FAIL: method overload(5) = " << a1 << " want 105\n"; return 1; }
      if (a2 != 111) { std::cerr << "FAIL: method overload(5,6) = " << a2 << " want 111\n"; return 1; }
      std::cout << "PASS: method overload: apply(5)=" << a1 << " apply(5,6)=" << a2 << "\n";
    }

    std::cout << "=== All overload tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
