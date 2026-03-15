#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <vector>

std::vector<std::string> captured_args;

extern "C" {
#include "../../src/Vm/lauxlib.h"
#include "../../src/Vm/lua.h"
}

static int capture_args(lua_State *L) {
  int nargs = lua_gettop(L);
  captured_args.clear();
  std::cout << "capture_args called with " << nargs << " args:" << std::endl;
  for (int i = 1; i <= nargs; i++) {
    if (lua_isinteger(L, i)) {
      lua_Integer val = lua_tointeger(L, i);
      captured_args.push_back("arg" + std::to_string(i) + " = " + std::to_string(val) + " (int)");
      std::cout << "  arg" << i << " = " << val << " (int)" << std::endl;
    } else if (lua_isstring(L, i)) {
      const char *val = lua_tostring(L, i);
      captured_args.push_back("arg" + std::to_string(i) + " = " + std::string(val) + " (string)");
      std::cout << "  arg" << i << " = " << val << " (string)" << std::endl;
    } else if (lua_istable(L, i)) {
      captured_args.push_back("arg" + std::to_string(i) + " = (table)");
      std::cout << "  arg" << i << " = (table)" << std::endl;
    } else if (lua_isnil(L, i)) {
      captured_args.push_back("arg" + std::to_string(i) + " = nil");
      std::cout << "  arg" << i << " = nil" << std::endl;
    } else {
      captured_args.push_back("arg" + std::to_string(i) + " = (other)");
      std::cout << "  arg" << i << " = (other)" << std::endl;
    }
  }
  lua_pushinteger(L, 999);
  return 1;
}

int main() {
  std::cout << "=== Testing __call metamethod parameter positions ===" << std::endl;

  lua_State *L = luaL_newstate();
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);
  luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
  lua_pop(L, 1);

  std::cout << "\nTest 1: Direct C function call (verify Slot 0)" << std::endl;
  lua_pushcfunction(L, capture_args);
  lua_setglobal(L, "capture");
  luaL_dostring(L, "capture(10, 20);");

  if (captured_args.size() == 3) {
    std::cout << "Test 1 PASSED: 3 args received (receiver + 2 actual args)" << std::endl;
  } else {
    std::cout << "Test 1 FAILED: Expected 3 args, got " << captured_args.size() << std::endl;
  }

  std::cout << "\nTest 2: __call metamethod" << std::endl;
  luaL_dostring(L, "Callable = {};");

  lua_getglobal(L, "Callable");
  lua_pushcfunction(L, capture_args);
  lua_setfield(L, -2, "__call");
  lua_pushvalue(L, -1);
  lua_setmetatable(L, -2);
  lua_pop(L, 1);

  luaL_dostring(L, "result = Callable(100, 200);");

  std::cout << "\nAnalyzing __call args:" << std::endl;
  if (captured_args.size() >= 2) {
    std::cout << "  " << captured_args[0] << std::endl;
    std::cout << "  " << captured_args[1] << std::endl;
    if (captured_args.size() >= 3) {
      std::cout << "  " << captured_args[2] << std::endl;
    }
  }

  lua_getglobal(L, "result");
  int result = (int)lua_tointeger(L, -1);
  lua_pop(L, 1);
  std::cout << "result = " << result << std::endl;

  lua_close(L);

  std::cout << "\nAll tests completed!" << std::endl;
  return 0;
}
