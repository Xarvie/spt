#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing List Type Validation ===" << std::endl;

    std::cout << "\n1. Testing valid list construction..." << std::endl;
    auto lst = lua.create_list<int>();
    lst.push_back(1);
    lst.push_back(2);
    lst.push_back(3);
    std::cout << "List created successfully with " << lst.size() << " elements" << std::endl;

    std::cout << "\n2. Testing that map cannot be used as list..." << std::endl;
    auto m = lua.create_map<int>();
    m.set<std::string>("key", 42);

    lua_getref(lua.lua_state(), m.registry_index());
    int map_ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);

    bool caught_exception = false;
    try {
      sptxx::list<int> invalid_list(lua.lua_state(), map_ref);
      std::cout << "ERROR: Should have thrown exception!" << std::endl;
    } catch (const sptxx::error &e) {
      caught_exception = true;
      std::cout << "Correctly caught exception: " << e.what() << std::endl;
    }
    luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, map_ref);

    if (!caught_exception) {
      throw std::runtime_error("List should reject map reference!");
    }

    std::cout << "\n3. Testing that nil reference is rejected..." << std::endl;
    caught_exception = false;
    try {
      sptxx::list<int> invalid_list(lua.lua_state(), LUA_NOREF);
    } catch (const sptxx::error &e) {
      caught_exception = true;
      std::cout << "Correctly caught exception: " << e.what() << std::endl;
    }
    if (!caught_exception) {
      throw std::runtime_error("List should reject LUA_NOREF!");
    }

    std::cout << "\n4. Testing that regular table (map) is rejected..." << std::endl;
    lua.do_string("regular_table = { a: 1, b: 2 };");
    lua_getglobal(lua.lua_state(), "regular_table");
    int table_ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);

    caught_exception = false;
    try {
      sptxx::list<int> invalid_list(lua.lua_state(), table_ref);
    } catch (const sptxx::error &e) {
      caught_exception = true;
      std::cout << "Correctly caught exception: " << e.what() << std::endl;
    }
    luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, table_ref);

    if (!caught_exception) {
      throw std::runtime_error("List should reject regular table (map) reference!");
    }

    std::cout << "\n5. Testing that array literal is accepted..." << std::endl;
    lua.do_string("array_literal = [1, 2, 3, 4, 5];");
    lua_getglobal(lua.lua_state(), "array_literal");
    int array_ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);

    bool success = false;
    try {
      sptxx::list<int> valid_list(lua.lua_state(), array_ref);
      std::cout << "Array literal accepted, size: " << valid_list.size() << std::endl;
      success = true;
    } catch (const sptxx::error &e) {
      std::cout << "ERROR: Array literal should be accepted! " << e.what() << std::endl;
    }
    luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, array_ref);

    if (!success) {
      throw std::runtime_error("Array literal should be accepted as list!");
    }

    std::cout << "\n=== All List Type Validation Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
