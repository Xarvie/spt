// test_edge_variadic.cpp - variadic_args 边界测试

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n";         \
      ++failures;                                                              \
    } else {                                                                   \
      std::cout << "PASS: " << msg << "\n";                                    \
    }                                                                          \
  } while (0)

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // ---- 1. va.size() == 0 时 begin == end ----
    lua.set_function("empty_va", [](sptxx::variadic_args va) {
      return va.size() == 0 && va.begin() == va.end() ? 1 : 0;
    });
    lua.do_string("r1 = empty_va();");
    CHECK(lua.get_global<int>("r1") == 1, "empty va: size==0 and begin==end");

    // ---- 2. va.size() 与实际参数数一致 ----
    lua.set_function("va_count", [](sptxx::variadic_args va) {
      return static_cast<int>(va.size());
    });
    lua.do_string("r2 = va_count(1, 2, 3, 4, 5);");
    CHECK(lua.get_global<int>("r2") == 5, "va.size() matches arg count = " << lua.get_global<int>("r2"));

    // ---- 3. va.get<int>(i) 按索引访问（variadic 必须为最后一个参数）----
    lua.set_function("va_at", [](int i, sptxx::variadic_args va) {
      return va.get<int>(i);
    });
    lua.do_string("r3 = va_at(1, 10, 20, 30);");
    CHECK(lua.get_global<int>("r3") == 20, "va.get<int>(1) = " << lua.get_global<int>("r3"));

    // ---- 4. va 混合类型迭代 ----
    lua.set_function("va_mixed", [](sptxx::variadic_args va) {
      int ints = 0;
      int strs = 0;
      lua_State *L = va.lua_state();
      for (auto it = va.begin(); it != va.end(); ++it) {
        if (lua_isinteger(L, it.stack_index())) {
          ++ints;
        } else {
          ++strs;
        }
      }
      return std::tuple<int, int>{ints, strs};
    });
    lua.do_string("vars a, b = va_mixed(1, \"x\", 2, \"y\", 3); r4a = a; r4b = b;");
    CHECK(lua.get_global<int>("r4a") == 3, "va mixed: ints = " << lua.get_global<int>("r4a"));
    CHECK(lua.get_global<int>("r4b") == 2, "va mixed: strs = " << lua.get_global<int>("r4b"));

    // ---- 5. 固定参数 + 空 variadic ----
    lua.set_function("fixed_plus_va", [](int base, sptxx::variadic_args va) {
      return base + static_cast<int>(va.size());
    });
    lua.do_string("r5 = fixed_plus_va(100);");
    CHECK(lua.get_global<int>("r5") == 100, "fixed + empty va = " << lua.get_global<int>("r5"));

    // ---- 6. 固定参数 + 多 variadic ----
    lua.do_string("r6 = fixed_plus_va(100, 1, 2, 3);");
    CHECK(lua.get_global<int>("r6") == 103, "fixed + 3 va = " << lua.get_global<int>("r6"));

    // ---- 7. variadic 求和（验证 push_all 不必需，直接迭代）----
    lua.set_function("forward_va", [](sptxx::variadic_args va) {
      int s = 0;
      lua_State *L = va.lua_state();
      for (auto it = va.begin(); it != va.end(); ++it) {
        if (lua_isinteger(L, it.stack_index()))
          s += it.get<int>();
      }
      return s;
    });
    lua.do_string("r7 = forward_va(1, 2, 3, 4);");
    CHECK(lua.get_global<int>("r7") == 10, "forward va sum = " << lua.get_global<int>("r7"));

    // ---- 8. variadic 搭配多返回值 ----
    lua.set_function("va_stats", [](sptxx::variadic_args va) {
      int sum = 0;
      int cnt = 0;
      lua_State *L = va.lua_state();
      for (auto it = va.begin(); it != va.end(); ++it) {
        if (lua_isinteger(L, it.stack_index())) {
          sum += it.get<int>();
          ++cnt;
        }
      }
      return std::tuple<int, int>{sum, cnt};
    });
    lua.do_string("vars s, c = va_stats(1, 2, 3, 4, 5); r8s = s; r8c = c;");
    CHECK(lua.get_global<int>("r8s") == 15, "va multi-return sum = " << lua.get_global<int>("r8s"));
    CHECK(lua.get_global<int>("r8c") == 5, "va multi-return count = " << lua.get_global<int>("r8c"));

    // ---- 9. 单个 variadic 参数 ----
    lua.do_string("r9 = va_count(42);");
    CHECK(lua.get_global<int>("r9") == 1, "single va arg = " << lua.get_global<int>("r9"));

    // ---- 10. variadic 全是 string ----
    lua.set_function("va_join", [](sptxx::variadic_args va) {
      std::string out;
      for (auto it = va.begin(); it != va.end(); ++it) {
        out += it.get<std::string>();
      }
      return out;
    });
    lua.do_string("r10 = va_join(\"a\", \"b\", \"c\");");
    CHECK(lua.get_global<std::string>("r10") == "abc", "va all strings = \"" << lua.get_global<std::string>("r10") << "\"");

    // ---- 11. variadic 参数不足：固定参数 + variadic，固定参数缺失应报错 ----
    {
      bool threw = false;
      try {
        lua.do_string("r11 = fixed_plus_va();"); // 缺 base 参数
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "missing fixed arg before variadic throws");
    }

    // ---- 12. va.get<int>(越界) —— 行为：读 nil，checkinteger 报错（do_string 抛异常）----
    {
      bool threw = false;
      try {
        lua.do_string("r12 = va_at(99, 1, 2);"); // i=99, va=[1,2], get<int>(99) 越界
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "va.get<int>(out-of-range) throws via do_string");
    }

    if (failures == 0) {
      std::cout << "=== All variadic edge tests passed! ===\n";
      return 0;
    }
    std::cerr << "=== " << failures << " test(s) FAILED ===\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
