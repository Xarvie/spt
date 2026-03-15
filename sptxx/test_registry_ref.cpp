// test_registry_ref.cpp - Test registry reference issues

#include "sptxx.hpp"
#include <iostream>

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Testing Registry Reference Issues ===" << std::endl;
        
        // Test 1: Basic array creation and registry reference
        std::cout << "\n1. Testing basic array registry reference..." << std::endl;
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
        
        if (ref == LUA_NOREF || ref == LUA_REFNIL) {
            std::cout << "ERROR: Reference is invalid (LUA_NOREF or LUA_REFNIL)!" << std::endl;
        } else {
            // Retrieve from registry
            lua_getref(lua.lua_state(), ref);
            std::cout << "Retrieved from registry" << std::endl;
            
            // Check type again
            type = lua_type(lua.lua_state(), -1);
            std::cout << "Type after retrieval: " << type << std::endl;
            if (type == LUA_TARRAY) {
                std::cout << "Still an array!" << std::endl;
            } else {
                std::cout << "NOT an array after retrieval!" << std::endl;
            }
            
            // Get length again
            if (type != LUA_TNIL) {
                len = lua_arraylen(lua.lua_state(), -1);
                std::cout << "Length after retrieval: " << len << std::endl;
            }
            
            lua_pop(lua.lua_state(), 1); // pop the retrieved value
        }
        
        // Test 2: Table registry reference
        std::cout << "\n2. Testing table registry reference..." << std::endl;
        lua_createtable(lua.lua_state(), 0, 0);
        lua_pushinteger(lua.lua_state(), 42);
        lua_setfield(lua.lua_state(), -2, "test_value");
        
        int table_ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);
        std::cout << "Table stored in registry with ref: " << table_ref << std::endl;
        
        if (table_ref == LUA_NOREF || table_ref == LUA_REFNIL) {
            std::cout << "ERROR: Table reference is invalid!" << std::endl;
        } else {
            lua_getref(lua.lua_state(), table_ref);
            std::cout << "Table retrieved from registry" << std::endl;
            
            type = lua_type(lua.lua_state(), -1);
            std::cout << "Table type after retrieval: " << type << std::endl;
            
            if (type == LUA_TTABLE) {
                lua_getfield(lua.lua_state(), -1, "test_value");
                if (lua_isinteger(lua.lua_state(), -1)) {
                    lua_Integer val = lua_tointeger(lua.lua_state(), -1);
                    std::cout << "Table value retrieved: " << val << std::endl;
                }
                lua_pop(lua.lua_state(), 1);
            }
            
            lua_pop(lua.lua_state(), 1);
        }
        
        // Test 3: Multiple references
        std::cout << "\n3. Testing multiple references..." << std::endl;
        lua_createarray(lua.lua_state(), 5);
        int ref1 = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);
        
        lua_createtable(lua.lua_state(), 0, 0);
        int ref2 = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);
        
        std::cout << "Array ref: " << ref1 << ", Table ref: " << ref2 << std::endl;
        
        if (ref1 > 0 && ref2 > 0 && ref1 != ref2) {
            std::cout << "Multiple references work correctly!" << std::endl;
        } else {
            std::cout << "Multiple references have issues!" << std::endl;
        }
        
        // Clean up
        if (ref1 > 0) luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, ref1);
        if (ref2 > 0) luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, ref2);
        if (ref > 0) luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, ref);
        if (table_ref > 0) luaL_unref(lua.lua_state(), LUA_REGISTRYINDEX, table_ref);
        
        std::cout << "\n=== Registry Reference Test Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}