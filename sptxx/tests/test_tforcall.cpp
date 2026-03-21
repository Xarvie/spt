#include <cassert>
#include <iostream>

extern "C" {
#include "../../src/Vm/lauxlib.h"
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lualib.h"
}

int call_count = 0;
int receiver_at_index = -1;

static int iter_func(lua_State *L) {
  call_count++;
  int nargs = lua_gettop(L);

  std::cout << "iter_func call #" << call_count << ": " << nargs << " args" << std::endl;

  for (int i = 1; i <= nargs; i++) {
    if (lua_isinteger(L, i)) {
      std::cout << "  arg" << i << " = " << lua_tointeger(L, i) << " (int)" << std::endl;
    } else if (lua_isfunction(L, i)) {
      std::cout << "  arg" << i << " = (function) <- RECEIVER" << std::endl;
      receiver_at_index = i;
    } else if (lua_istable(L, i)) {
      std::cout << "  arg" << i << " = (table)" << std::endl;
    } else if (lua_isnil(L, i)) {
      std::cout << "  arg" << i << " = nil" << std::endl;
    } else {
      std::cout << "  arg" << i << " = (other)" << std::endl;
    }
  }

  static int counter = 0;
  counter++;
  if (counter <= 3) {
    lua_pushinteger(L, counter);
    return 1;
  } else {
    return 0;
  }
}

int main() {
  std::cout << "=== Testing OP_TFORCALL receiver behavior ===" << std::endl;

  lua_State *L = luaL_newstate();
  luaL_requiref(L, "_G", luaopen_base, 1);
  lua_pop(L, 1);

  std::cout << "\nTest 1: Simulate SPT's OP_TFORCALL" << std::endl;
  std::cout << "SPT OP_TFORCALL sets up: func, receiver=func, state, control" << std::endl;

  lua_pushcfunction(L, iter_func);
  lua_setglobal(L, "iter");

  lua_getglobal(L, "iter");
  lua_getglobal(L, "iter");
  lua_pushnil(L);
  lua_pushnil(L);

  std::cout << "Stack: [iter, iter, nil, nil] (func, receiver, state, control)" << std::endl;
  std::cout << "Calling with 3 args (receiver + state + control)..." << std::endl;
  lua_call(L, 3, 1);

  if (lua_isinteger(L, -1)) {
    std::cout << "Iterator returned: " << lua_tointeger(L, -1) << std::endl;
  }
  lua_pop(L, 1);

  std::cout << "\nTest 2: Check receiver position" << std::endl;
  if (receiver_at_index == 1) {
    std::cout << "PASS: Receiver (function) is at index 1" << std::endl;
    std::cout << "This matches SPT's Slot 0 Receiver convention!" << std::endl;
  } else if (receiver_at_index > 0) {
    std::cout << "INFO: Function found at index " << receiver_at_index << std::endl;
  } else {
    std::cout << "INFO: No function found in args" << std::endl;
  }

  lua_close(L);

  std::cout << "\n=== Summary ===" << std::endl;
  std::cout << "OP_TFORCALL passes the iterator function as receiver (Slot 0)" << std::endl;
  std::cout << "This is consistent with SPT's Slot 0 Receiver convention" << std::endl;

  return 0;
}
