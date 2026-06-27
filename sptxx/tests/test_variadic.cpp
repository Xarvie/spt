// test_variadic.cpp - 测试 variadic_args 可变参数

#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // ---- 1. 纯 variadic_args ----
    lua.set_function("sum_all", [](sptxx::variadic_args va) {
      int s = 0;
      for (auto it = va.begin(); it != va.end(); ++it) s += it.get<int>();
      return s;
    });
    lua.do_string("r1 = sum_all(1, 2, 3, 4, 5);");
    {
      int r1 = lua.get_global<int>("r1");
      if (r1 != 15) { std::cerr << "FAIL: sum_all = " << r1 << " want 15\n"; return 1; }
      std::cout << "PASS: pure variadic sum_all=" << r1 << "\n";
    }

    // ---- 2. 空参数 ----
    lua.do_string("r2 = sum_all();");
    {
      int r2 = lua.get_global<int>("r2");
      if (r2 != 0) { std::cerr << "FAIL: sum_all() = " << r2 << " want 0\n"; return 1; }
      std::cout << "PASS: empty variadic sum_all()=" << r2 << "\n";
    }

    // ---- 3. 固定参数 + variadic_args ----
    lua.set_function("join_base", [](int base, sptxx::variadic_args va) {
      int s = base;
      for (auto it = va.begin(); it != va.end(); ++it) s += it.get<int>();
      return s;
    });
    lua.do_string("r3 = join_base(100, 1, 2, 3);");
    {
      int r3 = lua.get_global<int>("r3");
      if (r3 != 106) { std::cerr << "FAIL: join_base = " << r3 << " want 106\n"; return 1; }
      std::cout << "PASS: fixed+variadic join_base=" << r3 << "\n";
    }

    // ---- 4. 固定参数 + 空 variadic ----
    lua.do_string("r4 = join_base(42);");
    {
      int r4 = lua.get_global<int>("r4");
      if (r4 != 42) { std::cerr << "FAIL: join_base(42) = " << r4 << "\n"; return 1; }
      std::cout << "PASS: fixed+empty variadic join_base(42)=" << r4 << "\n";
    }

    // ---- 5. 字符串拼接 ----
    lua.set_function("concat_all", [](sptxx::variadic_args va) {
      std::string out;
      for (auto it = va.begin(); it != va.end(); ++it) out += it.get<std::string>();
      return out;
    });
    lua.do_string("r5 = concat_all(\"hello\", \" \", \"world\");");
    {
      std::string r5 = lua.get_global<std::string>("r5");
      if (r5 != "hello world") { std::cerr << "FAIL: concat_all = " << r5 << "\n"; return 1; }
      std::cout << "PASS: string variadic concat_all=\"" << r5 << "\"\n";
    }

    // ---- 6. va.size() 与 va.get(i)（多返回值，SPT 需 vars 声明）----
    lua.set_function("describe", [](sptxx::variadic_args va) {
      return std::tuple<int, int>{va.size(), va.get<int>(0)};
    });
    lua.do_string("vars a, b = describe(7, 8, 9); r6a = a; r6b = b;");
    {
      int r6a = lua.get_global<int>("r6a");
      int r6b = lua.get_global<int>("r6b");
      if (r6a != 3) { std::cerr << "FAIL: size = " << r6a << " want 3\n"; return 1; }
      if (r6b != 7) { std::cerr << "FAIL: first = " << r6b << " want 7\n"; return 1; }
      std::cout << "PASS: size/get: size=" << r6a << " first=" << r6b << "\n";
    }

    // ---- 7. variadic + overload 兼容（overload 不冲突）----
    lua.set_function("only_int", [](int x) { return x * 2; });
    lua.do_string("r7 = only_int(21);");
    {
      int r7 = lua.get_global<int>("r7");
      if (r7 != 42) { std::cerr << "FAIL: only_int = " << r7 << "\n"; return 1; }
      std::cout << "PASS: non-variadic still works only_int=" << r7 << "\n";
    }

    std::cout << "=== All variadic tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
