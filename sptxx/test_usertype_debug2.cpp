// test_usertype_debug2.cpp - Debug usertype registration with stack traces

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
    
    Person() : name("Unknown"), age(0) {}
};

void print_stack(lua_State* L, const char* label) {
    int top = lua_gettop(L);
    std::cout << label << " - stack top: " << top << std::endl;
    for (int i = 1; i <= top; i++) {
        int type = lua_type(L, i);
        std::cout << "  [" << i << "] " << lua_typename(L, type);
        if (type == LUA_TSTRING) {
            std::cout << " = '" << lua_tostring(L, i) << "'";
        } else if (type == LUA_TNUMBER) {
            std::cout << " = " << lua_tonumber(L, i);
        } else if (type == LUA_TUSERDATA) {
            std::cout << " = " << lua_touserdata(L, i);
        }
        std::cout << std::endl;
    }
}

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Creating usertype ===" << std::endl;
        print_stack(lua.lua_state(), "Before new_usertype");
        
        auto person_type = lua.new_usertype<Person>("Person");
        print_stack(lua.lua_state(), "After new_usertype");
        
        std::cout << "\n=== Testing Person() call ===" << std::endl;
        
        // Test calling Person() directly
        int result = luaL_dostring(lua.lua_state(), "p = Person(); print('Created person')");
        if (result != LUA_OK) {
            std::cout << "Error: " << lua_tostring(lua.lua_state(), -1) << std::endl;
            lua_pop(lua.lua_state(), 1);
        }
        
        print_stack(lua.lua_state(), "After Person() call");
        
        std::cout << "\n=== Test Complete ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}