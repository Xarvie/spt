#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state_with_coroutine lua;
    lua.open_libraries();

    luaL_requiref(lua.lua_state(), "coroutine", luaopen_coroutine, 1);
    lua_pop(lua.lua_state(), 1);

    std::cout << "=== Testing coroutine.wrap BUG DEMO ===" << std::endl;

    std::cout << "\nTest: Compare results between resume and wrap" << std::endl;
    std::cout << "If BUG exists, wrap will return wrong result\n" << std::endl;

    std::cout << "1. Using coroutine.resume (correct):" << std::endl;
    lua.do_string(R"(
co1 = coroutine.create(function(int a, int b) -> int {
    return a + b;
});
vars ok, result1 = coroutine.resume(co1, 10, 20);
print('resume result: ' .. result1);
)");

    std::cout << "\n2. Using coroutine.wrap:" << std::endl;
    lua.do_string(R"(
co2 = coroutine.wrap(function(int a, int b) -> int {
    return a + b;
});
vars result2 = co2(10, 20);
print('wrap result: ' .. result2);
)");

    std::cout << "\n=== Expected: both should return 30 ===" << std::endl;
    std::cout << "=== If wrap returns different value, the BUG is CONFIRMED ===" << std::endl;

    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
