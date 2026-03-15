// test_map_validation.cpp - Test MAP mode validation issue

#include "sptxx.hpp"
#include <iostream>

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Testing Map Validation Issue ===" << std::endl;
        
        // Test 1: Create map using create_map() and check if it's valid
        std::cout << "\n1. Testing create_map() validation..." << std::endl;
        try {
            auto map = lua.create_map<int>();
            std::cout << "Map created successfully!" << std::endl;
            
            // Try to set a value
            map.set<std::string>("test_key", 42);
            int value = map.get<std::string>("test_key");
            std::cout << "Map get/set works: " << value << std::endl;
            
        } catch (const std::exception& e) {
            std::cout << "Map creation failed: " << e.what() << std::endl;
            std::cout << "This indicates the MAP validation issue." << std::endl;
        }
        
        // Test 2: Create table manually and check if it's recognized as MAP
        std::cout << "\n2. Testing manual table creation..." << std::endl;
        lua.do_string("manual_table = {};");
        
        // Check the table mode using Lua API
        lua_getglobal(lua.lua_state(), "manual_table");
        if (lua_ismap(lua.lua_state(), -1)) {
            std::cout << "Manual table is recognized as MAP mode!" << std::endl;
        } else {
            std::cout << "Manual table is NOT recognized as MAP mode!" << std::endl;
        }
        lua_pop(lua.lua_state(), 1);
        
        // Test 3: Check what lua_createtable actually creates
        std::cout << "\n3. Testing lua_createtable directly..." << std::endl;
        lua_createtable(lua.lua_state(), 0, 0);
        if (lua_ismap(lua.lua_state(), -1)) {
            std::cout << "lua_createtable creates MAP mode table!" << std::endl;
        } else {
            std::cout << "lua_createtable does NOT create MAP mode table!" << std::endl;
        }
        lua_pop(lua.lua_state(), 1);
        
        std::cout << "\n=== Map Validation Test Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}