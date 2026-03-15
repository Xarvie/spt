// test_usertype_trace.cpp - Debug with stack traces

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
    
    Person() : name("Unknown"), age(0) {}
    Person(const std::string& n, int a) : name(n), age(a) {}
    
    void introduce() const {
        std::cout << "Hi, I'm " << name << " and I'm " << age << " years old." << std::endl;
    }
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
        } else if (type == LUA_TTABLE) {
            std::cout << " = table";
        } else if (type == LUA_TFUNCTION) {
            std::cout << " = function";
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
        
        std::cout << "\n=== Setting properties ===" << std::endl;
        person_type.set("name", &Person::name);
        print_stack(lua.lua_state(), "After set name");
        
        person_type.set("age", &Person::age);
        print_stack(lua.lua_state(), "After set age");
        
        std::cout << "\n=== Testing Person() call ===" << std::endl;
        
        // Test calling Person() directly
        std::cout << "Calling: p = Person()" << std::endl;
        int result = luaL_dostring(lua.lua_state(), "p = Person()");
        if (result != LUA_OK) {
            std::cout << "Error creating Person: " << lua_tostring(lua.lua_state(), -1) << std::endl;
            lua_pop(lua.lua_state(), 1);
            return 1;
        }
        std::cout << "Person() succeeded!" << std::endl;
        print_stack(lua.lua_state(), "After Person()");
        
        std::cout << "\n=== Testing property access ===" << std::endl;
        std::cout << "Calling: p.name = 'Alice'" << std::endl;
        result = luaL_dostring(lua.lua_state(), "p.name = 'Alice'");
        if (result != LUA_OK) {
            std::cout << "Error setting name: " << lua_tostring(lua.lua_state(), -1) << std::endl;
            lua_pop(lua.lua_state(), 1);
            return 1;
        }
        std::cout << "p.name = 'Alice' succeeded!" << std::endl;
        
        std::cout << "\n=== Test Complete ===" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}