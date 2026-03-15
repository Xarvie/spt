#include <cassert>
#include <iostream>

extern "C" {
#include "../../src/Vm/lauxlib.h"
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lualib.h"
}

static int test_idx;

static int protected_geti(lua_State *L) {
  lua_geti(L, 1, test_idx);
  return 1;
}

int main() {
  std::cout << "=== Testing List out-of-bounds behavior ===" << std::endl;

  lua_State *L = luaL_newstate();
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);

  std::cout << "\nTest 1: Create list and set length" << std::endl;
  lua_createarray(L, 10);
  lua_pushinteger(L, 100);
  lua_seti(L, -2, 0);
  lua_pushinteger(L, 200);
  lua_seti(L, -2, 1);
  lua_pushinteger(L, 300);
  lua_seti(L, -2, 2);
  lua_arraysetlen(L, -1, 3);
  lua_setglobal(L, "arr");

  std::cout << "List created with loglen = 3" << std::endl;
  std::cout << "Test 1 PASSED!" << std::endl;

  std::cout << "\nTest 2: Valid read (index 0, 1, 2)" << std::endl;
  for (int i = 0; i < 3; i++) {
    lua_getglobal(L, "arr");
    lua_geti(L, -1, i);
    if (lua_isnil(L, -1)) {
      std::cout << "  arr[" << i << "] = nil (unexpected!)" << std::endl;
      return 1;
    } else {
      lua_Integer val = lua_tointeger(L, -1);
      std::cout << "  arr[" << i << "] = " << val << std::endl;
    }
    lua_pop(L, 2);
  }
  std::cout << "Test 2 PASSED!" << std::endl;

  std::cout << "\nTest 3: Out-of-bounds read should throw error" << std::endl;
  int test_indices[] = {3, 10, -1};
  for (int idx : test_indices) {
    lua_getglobal(L, "arr");
    test_idx = idx;
    lua_pushcfunction(L, protected_geti);
    lua_pushvalue(L, -2);
    int status = lua_pcall(L, 1, 1, 0);
    if (status == LUA_OK) {
      std::cout << "  arr[" << idx << "] = " << lua_tointeger(L, -1)
                << " (UNEXPECTED - should have errored!)" << std::endl;
      return 1;
    }
    std::cout << "  arr[" << idx << "] correctly threw error" << std::endl;
    lua_pop(L, 2);
  }
  std::cout << "Test 3 PASSED!" << std::endl;

  lua_close(L);

  std::cout << "\n=== All tests PASSED ===" << std::endl;
  std::cout << "List out-of-bounds READ throws error" << std::endl;

  return 0;
}
