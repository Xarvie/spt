// test_ref.cpp - Test luaL_ref and lua_getref (Updated for High-Perf Registry)

#include "sptxx.hpp"
#include <iostream>

int main() {
  try {
    sptxx::state lua;

    std::cout << "Creating array..." << std::endl;
    lua_createarray(lua.lua_state(), 10);
    lua_arraysetlen(lua.lua_state(), -1, 10);
    std::cout << "Array created" << std::endl;

    // Check type
    int type = lua_type(lua.lua_state(), -1);
    std::cout << "Type before ref: " << type << std::endl;

    // Get reference
    int ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);
    std::cout << "Reference: " << ref << std::endl;

    // Retrieve from registry using the NEW dedicated API
    lua_getref(lua.lua_state(), ref);
    std::cout << "Retrieved from registry" << std::endl;

    // Check type after retrieval
    type = lua_type(lua.lua_state(), -1);
    std::cout << "Type after retrieval: " << type << std::endl;

    // Try to get length
    if (type == LUA_TARRAY) {
      lua_Integer len = lua_arraylen(lua.lua_state(), -1);
      std::cout << "Length: " << len << std::endl;
    } else {
      std::cout << "ERROR: Not an array! The reference mechanism failed." << std::endl;
      return 1; // Return error code if it fails
    }

    // Clean up
    lua_pop(lua.lua_state(), 1);                         // pop the retrieved value
    luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, ref); // Free the ref

    std::cout << "Ref test passed!" << std::endl;
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}