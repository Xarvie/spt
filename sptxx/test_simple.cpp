// test_simple.cpp - Simple test to isolate the issue

#include "sptxx.hpp"
#include <iostream>

int main() {
    try {
        sptxx::state lua;
        
        // Test creating array directly
        lua_createarray(lua.lua_state(), 10);
        std::cout << "Created array" << std::endl;
        
        // Test setting logical length
        lua_arraysetlen(lua.lua_state(), -1, 10);
        std::cout << "Set logical length" << std::endl;
        
        // Test getting length
        lua_Integer len = lua_arraylen(lua.lua_state(), -1);
        std::cout << "Array length: " << len << std::endl;
        
        // Test setting element
        lua_pushinteger(lua.lua_state(), 100);
        lua_seti(lua.lua_state(), -2, 0);  // 0-based index
        std::cout << "Set element" << std::endl;
        
        // Test getting element
        lua_geti(lua.lua_state(), -1, 0);
        if (lua_isinteger(lua.lua_state(), -1)) {
            lua_Integer val = lua_tointeger(lua.lua_state(), -1);
            std::cout << "Got element: " << val << std::endl;
        }
        lua_pop(lua.lua_state(), 2);  // pop array and value
        
        std::cout << "Simple test passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}