// test_debug2.cpp - More detailed debug test

#include "sptxx.hpp"
#include <iostream>

int main() {
    try {
        sptxx::state lua;
        
        std::cout << "Creating array directly on stack..." << std::endl;
        lua_createarray(lua.lua_state(), 10);
        lua_arraysetlen(lua.lua_state(), -1, 10);
        std::cout << "Array created and length set" << std::endl;
        
        // Get the type of the value on top of stack
        int type = lua_type(lua.lua_state(), -1);
        std::cout << "Type on stack: " << type << std::endl;
        if (type == LUA_TARRAY) {
            std::cout << "It's an array!" << std::endl;
        }
        
        // Get length
        lua_Integer len = lua_arraylen(lua.lua_state(), -1);
        std::cout << "Length from stack: " << len << std::endl;
        
        // Store in registry
        int ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);
        std::cout << "Stored in registry with ref: " << ref << std::endl;
        
        // Retrieve from registry
        lua_rawgeti(lua.lua_state(), LUA_REGISTRYINDEX, ref);
        std::cout << "Retrieved from registry" << std::endl;
        
        // Check type again
        type = lua_type(lua.lua_state(), -1);
        std::cout << "Type after retrieval: " << type << std::endl;
        if (type == LUA_TARRAY) {
            std::cout << "Still an array!" << std::endl;
        }
        
        // Get length again
        len = lua_arraylen(lua.lua_state(), -1);
        std::cout << "Length after retrieval: " << len << std::endl;
        
        lua_pop(lua.lua_state(), 1); // pop the array
        
        std::cout << "Debug2 test passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}