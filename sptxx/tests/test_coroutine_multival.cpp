// test_coroutine_multival.cpp - 测试协程多值 yield + 多值 return
// 协程 yield 多个值，最终 return 多个值，C++ 通过 std::tuple 接收。

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <tuple>

int main() {
  try {
    sptxx::state_with_coroutine lua;
    lua.open_libraries();

    // 协程：多次 yield 多个值，最后 return 多个值
    // SPT 协程函数不接收参数；resume 的参数会成为 yield() 的返回值。
    auto co = lua.get_coroutine_from_script(R"(
        return fn() -> vars {
            coroutine.yield(1, "a");
            coroutine.yield(2, "b");
            return 3, "c";
        };
    )");

    if (!co.valid()) {
      std::cerr << "FAIL: coroutine invalid\n";
      return 1;
    }

    // 1. 第一次 resume → yield (1, "a")
    {
      auto [yielded, result] = co.resume_with_result<std::tuple<int, std::string>>();
      if (!yielded) {
        std::cerr << "FAIL: first resume should yield\n";
        return 1;
      }
      if (!result) {
        std::cerr << "FAIL: first resume no result\n";
        return 1;
      }
      auto [a, b] = *result;
      if (a != 1 || b != "a") {
        std::cerr << "FAIL: first yield = (" << a << ", " << b << ") want (1, a)\n";
        return 1;
      }
      std::cout << "PASS: first yield multi-value: (" << a << ", " << b << ")\n";
    }

    // 2. 第二次 resume → yield (2, "b")
    {
      auto [yielded, result] = co.resume_with_result<std::tuple<int, std::string>>();
      if (!yielded) {
        std::cerr << "FAIL: second resume should yield\n";
        return 1;
      }
      if (!result) {
        std::cerr << "FAIL: second resume no result\n";
        return 1;
      }
      auto [a, b] = *result;
      if (a != 2 || b != "b") {
        std::cerr << "FAIL: second yield = (" << a << ", " << b << ") want (2, b)\n";
        return 1;
      }
      std::cout << "PASS: second yield multi-value: (" << a << ", " << b << ")\n";
    }

    // 3. 第三次 resume → return (3, "c")
    {
      auto [yielded, result] = co.resume_with_result<std::tuple<int, std::string>>();
      if (yielded) {
        std::cerr << "FAIL: third resume should return, not yield\n";
        return 1;
      }
      if (!result) {
        std::cerr << "FAIL: third resume no result\n";
        return 1;
      }
      auto [a, b] = *result;
      if (a != 3 || b != "c") {
        std::cerr << "FAIL: return = (" << a << ", " << b << ") want (3, c)\n";
        return 1;
      }
      std::cout << "PASS: return multi-value: (" << a << ", " << b << ")\n";
    }

    // 4. 协程已死
    if (!co.is_dead()) {
      std::cerr << "FAIL: coroutine should be dead\n";
      return 1;
    }
    std::cout << "PASS: coroutine is dead after completion\n";

    std::cout << "=== All coroutine multi-value tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
