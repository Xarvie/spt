// test_ref.cpp - Test luaL_ref and lua_rawgeti

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
        
        // Retrieve from registry
        lua_rawgeti(lua.lua_state(), LUA_REGISTRYINDEX, ref);
        std::cout << "Retrieved from registry" << std::endl;
        
        // Check type after retrieval
        type = lua_type(lua.lua_state(), -1);
        std::cout << "Type after retrieval: " << type << std::endl;
        
        // Try to get length
        if (type == LUA_TARRAY) {
            lua_Integer len = lua_arraylen(lua.lua_state(), -1);
            std::cout << "Length: " << len << std::endl;
        } else {
            std::cout << "Not an array!" << std::endl;
        }
        
        lua_pop(lua.lua_state(), 1); // pop the retrieved value
        
        std::cout << "Ref test passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}