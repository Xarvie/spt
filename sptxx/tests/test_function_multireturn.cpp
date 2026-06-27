// test_function_multireturn.cpp - 测试 function_ref 和 state::call 的多返回值
// Lua 函数返回多个值 → C++ 接收为 std::tuple。

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <tuple>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // SPT 多返回值函数：用匿名函数 + 裸赋值创建全局函数。
    // 注意：命名函数声明 `vars f() {...}` 在 do_string chunk 中不会注册为
    // LUA_TFUNCTION 类型的全局；改用 `f = function() -> vars {...};` 更可靠。
    lua.do_string(R"(
        two_values = function() -> vars {
            return 42, "hello";
        };
        multi_fn = function(int n) -> vars {
            return n * 2, "doubled";
        };
    )");

    // 1. state::call<std::tuple<...>> 多返回值
    {
      auto [a, b] = lua.call<std::tuple<int, std::string>>("two_values");
      if (a != 42 || b != "hello") {
        std::cerr << "FAIL: state::call tuple = (" << a << ", " << b << ")\n";
        return 1;
      }
      std::cout << "PASS: state::call multi-return: (" << a << ", " << b << ")\n";
    }

    // 2. state::call 带参数 + 多返回值
    {
      auto [a, b] = lua.call<std::tuple<int, std::string>>("multi_fn", 21);
      if (a != 42 || b != "doubled") {
        std::cerr << "FAIL: state::call(args) tuple = (" << a << ", " << b << ")\n";
        return 1;
      }
      std::cout << "PASS: state::call(args) multi-return: (" << a << ", " << b << ")\n";
    }

    // 3. function_ref<std::tuple<...>()> 多返回值
    {
      auto fn = lua.get_function<std::tuple<int, std::string>()>("two_values");
      if (!fn.valid()) {
        std::cerr << "FAIL: function_ref invalid\n";
        return 1;
      }
      auto [a, b] = fn();
      if (a != 42 || b != "hello") {
        std::cerr << "FAIL: function_ref tuple = (" << a << ", " << b << ")\n";
        return 1;
      }
      std::cout << "PASS: function_ref multi-return: (" << a << ", " << b << ")\n";
    }

    // 4. function_ref 带参数 + 多返回值
    {
      auto fn = lua.get_function<std::tuple<int, std::string>(int)>("multi_fn");
      auto [a, b] = fn(21);
      if (a != 42 || b != "doubled") {
        std::cerr << "FAIL: function_ref(args) tuple = (" << a << ", " << b << ")\n";
        return 1;
      }
      std::cout << "PASS: function_ref(args) multi-return: (" << a << ", " << b << ")\n";
    }

    // 5. 三返回值
    lua.do_string(R"(
        three = function() -> vars {
            return 1, 2, 3;
        };
    )");
    {
      auto [a, b, c] = lua.call<std::tuple<int, int, int>>("three");
      if (a != 1 || b != 2 || c != 3) {
        std::cerr << "FAIL: triple = (" << a << ", " << b << ", " << c << ")\n";
        return 1;
      }
      std::cout << "PASS: triple multi-return: (" << a << ", " << b << ", " << c << ")\n";
    }

    std::cout << "=== All multi-return tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
