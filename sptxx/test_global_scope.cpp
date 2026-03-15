#include <iostream>
#include <string>
#include <vector>

extern "C" {
#include "../../src/Vm/lauxlib.h"
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lualib.h"
}

bool run_test(lua_State *L, const char *name, const char *code) {
  std::cout << "\n" << name << std::endl;
  std::cout << "Code: " << code << std::endl;
  int result = luaL_dostring(L, code);
  if (result == LUA_OK) {
    std::cout << "PASSED" << std::endl;
    return true;
  } else {
    std::cout << "FAILED: " << lua_tostring(L, -1) << std::endl;
    lua_pop(L, 1);
    return false;
  }
}

int main() {
  std::cout << "=== Testing SPT global function persistence ===" << std::endl;

  lua_State *L = luaL_newstate();
  luaL_openlibs(L);

  std::vector<std::pair<const char *, const char *>> tests = {
      {"Test 1: Global function in same do_string",
       "global int add(int a, int b) { return a + b; } print(\"add(1,2) =\", add(1, 2));"},

      {"Test 2: Define global function", "global int mul(int a, int b) { return a * b; }"},

      {"Test 3: Call global function from another do_string", "print(\"mul(3,4) =\", mul(3, 4));"},

      {"Test 4: Define global variable", "global int x = 100;"},

      {"Test 5: Access global variable from another do_string", "print(\"x =\", x);"},
  };

  int passed = 0;
  for (auto &test : tests) {
    if (run_test(L, test.first, test.second)) {
      passed++;
    }
  }

  lua_close(L);

  std::cout << "\n=== Results: " << passed << "/" << tests.size()
            << " tests passed ===" << std::endl;
  return passed == tests.size() ? 0 : 1;
}
