// test_usertype_minimal.cpp - Minimal test

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
    
    Person() : name("Unknown"), age(0) {}
};

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "Creating usertype..." << std::endl;
        
        // Just create the usertype, don't register methods
        auto person_type = lua.new_usertype<Person>("Person");
        
        std::cout << "Usertype created, now testing Person()..." << std::endl;
        
        // Try to call Person()
        int result = luaL_dostring(lua.lua_state(), "p = Person(); print('success')");
        if (result != LUA_OK) {
            std::cout << "Error: " << lua_tostring(lua.lua_state(), -1) << std::endl;
            return 1;
        }
        
        std::cout << "Success!" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}