// test_stack_extended.cpp - 测试栈类型特化：窄整型 + std::string_view
// 验证 char/signed char/unsigned char/short/unsigned short/unsigned long/
//       unsigned long long/std::string_view 的 push/get 往返。

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <string_view>

static int pass_fail = 0;

#define CHECK_EQ(actual, expected, msg)                                        \
  do {                                                                         \
    if ((actual) != (expected)) {                                              \
      std::cerr << "FAIL: " << msg << " — got " << (actual) << " want "        \
                << (expected) << "\n";                                         \
      pass_fail = 1;                                                           \
    } else {                                                                   \
      std::cout << "PASS: " << msg << "\n";                                    \
    }                                                                          \
  } while (0)

int main() {
  sptxx::state lua;
  lua.open_libraries();

  // ---- 窄整型往返 ----
  lua.set_global("c", char{'Z'});
  CHECK_EQ(lua.get_global<char>("c"), char{'Z'}, "char round-trip");

  lua.set_global("sc", signed char{-42});
  CHECK_EQ(lua.get_global<signed char>("sc"), signed char{-42}, "signed char round-trip");

  lua.set_global("uc", unsigned char{250});
  CHECK_EQ(lua.get_global<unsigned char>("uc"), unsigned char{250}, "unsigned char round-trip");

  lua.set_global("s", short{-1234});
  CHECK_EQ(lua.get_global<short>("s"), short{-1234}, "short round-trip");

  lua.set_global("us", unsigned short{60000});
  CHECK_EQ(lua.get_global<unsigned short>("us"), unsigned short{60000}, "unsigned short round-trip");

  lua.set_global("ul", unsigned long{4000000000UL});
  CHECK_EQ(lua.get_global<unsigned long>("ul"), unsigned long{4000000000UL}, "unsigned long round-trip");

  lua.set_global("ull", unsigned long long{18000000000000000000ULL});
  CHECK_EQ(lua.get_global<unsigned long long>("ull"), unsigned long long{18000000000000000000ULL},
           "unsigned long long round-trip");

  // ---- std::string_view 往返 ----
  lua.set_global("sv", std::string_view{"hello_view"});
  std::string_view sv = lua.get_global<std::string_view>("sv");
  CHECK_EQ(sv, std::string_view{"hello_view"}, "string_view round-trip");

  // ---- 通过 Lua 脚本验证窄整型在 Lua 侧是 integer ----
  // 注意：SPT 中 `vars` 声明的是局部变量，`auto` 也是局部；
  // 要让 C++ 通过 get_global 读到，必须用裸赋值（写入全局）。
  lua.do_string("n = c + 1;");
  CHECK_EQ(lua.get_global<int>("n"), static_cast<int>('Z') + 1, "char usable in Lua arithmetic");

  // ---- string_view 空串 ----
  lua.set_global("empty_sv", std::string_view{""});
  std::string_view esv = lua.get_global<std::string_view>("empty_sv");
  CHECK_EQ(esv.size(), size_t{0}, "empty string_view round-trip");

  if (pass_fail) {
    std::cerr << "=== Some stack extended tests FAILED ===\n";
    return 1;
  }
  std::cout << "=== All stack extended tests passed! ===\n";
  return 0;
}
