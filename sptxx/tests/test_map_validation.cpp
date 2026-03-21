// test_map_validation.cpp - Test MAP mode validation issue - Strict Version

#include "sptxx.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

// 辅助函数：确保脚本绝对执行成功
void safe_do_string(sptxx::state &lua, const char *code) {
  if (luaL_dostring(lua.lua_state(), code) != LUA_OK) {
    std::string err = lua_tostring(lua.lua_state(), -1);
    lua_pop(lua.lua_state(), 1);
    throw std::runtime_error("Lua Script Error: " + err);
  }
}

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Map Validation Issue ===" << std::endl;

    // Test 1: Create map using create_map() and check if it's valid
    std::cout << "\n1. Testing create_map() validation..." << std::endl;
    auto map = lua.create_map<int>();
    map.set<std::string>("test_key", 42);
    int value = map.get<std::string>("test_key");
    if (value != 42) {
      throw std::runtime_error("Map get/set validation failed! Expected 42.");
    }
    std::cout << "Map created and get/set works perfectly!" << std::endl;

    // Test 2: Create table manually and check if it's recognized as MAP
    std::cout << "\n2. Testing manual table creation {} ..." << std::endl;
    safe_do_string(lua, "manual_table = {};");

    lua_getglobal(lua.lua_state(), "manual_table");
    if (!lua_ismap(lua.lua_state(), -1)) {
      throw std::runtime_error(
          "Validation Failed: Manual table '{}' is NOT recognized as MAP mode!");
    }
    std::cout << "Manual table '{}' is correctly recognized as MAP mode!" << std::endl;
    lua_pop(lua.lua_state(), 1);

    // Test 3: Check what lua_createtable actually creates
    std::cout << "\n3. Testing lua_createtable directly..." << std::endl;
    lua_createtable(lua.lua_state(), 0, 0);
    if (!lua_ismap(lua.lua_state(), -1)) {
      throw std::runtime_error(
          "Validation Failed: lua_createtable does NOT create MAP mode table!");
    }
    std::cout << "lua_createtable correctly creates MAP mode table!" << std::endl;
    lua_pop(lua.lua_state(), 1);

    // Test 4: Negative Test - Array MUST NOT be a MAP
    std::cout << "\n4. Testing Negative case: Array is NOT a map..." << std::endl;
    lua_createarray(lua.lua_state(), 0); // 使用你 lapi.c 里的专属 array 创建接口
    if (lua_ismap(lua.lua_state(), -1)) {
      throw std::runtime_error("Validation Failed: Array was mistakenly recognized as a MAP!");
    }
    std::cout << "Array is correctly rejected by lua_ismap!" << std::endl;
    lua_pop(lua.lua_state(), 1);

    std::cout << "\n=== ALL Map Validation Tests Passed! ===" << std::endl;
    return 0; // 只有到这里才能光荣地返回 0

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1; // 只要发生任何异常或不符合预期，坚决返回 1 拦截构建
  }
}