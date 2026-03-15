// test_usertype_debug3.cpp - Debug with verbose output

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
        std::cout << "  [" << i << "] " << lua_typename(L, type) << std::endl;
    }
}

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Creating usertype ===" << std::endl;
        print_stack(lua.lua_state(), "Before new_usertype");
        
        // Manually do what new_usertype does
        std::cout << "\n--- Manual usertype creation ---" << std::endl;
        
        // Step 1: Create metatable
        std::cout << "Step 1: luaL_newmetatable" << std::endl;
        luaL_newmetatable(lua.lua_state(), "Person");
        print_stack(lua.lua_state(), "After luaL_newmetatable");
        
        // Step 2: Set __name
        std::cout << "Step 2: Set __name" << std::endl;
        lua_pushstring(lua.lua_state(), "Person");
        lua_setfield(lua.lua_state(), -2, "__name");
        print_stack(lua.lua_state(), "After __name");
        
        // Step 3: Set __gc
        std::cout << "Step 3: Set __gc" << std::endl;
        lua_pushcfunction(lua.lua_state(), [](lua_State* L) -> int {
            std::cout << "__gc called!" << std::endl;
            return 0;
        });
        lua_setfield(lua.lua_state(), -2, "__gc");
        print_stack(lua.lua_state(), "After __gc");
        
        // Step 4: Set __index to self
        std::cout << "Step 4: Set __index" << std::endl;
        lua_pushvalue(lua.lua_state(), -1);
        lua_setfield(lua.lua_state(), -2, "__index");
        print_stack(lua.lua_state(), "After __index");
        
        // Step 5: Pop metatable
        std::cout << "Step 5: Pop metatable" << std::endl;
        lua_pop(lua.lua_state(), 1);
        print_stack(lua.lua_state(), "After pop");
        
        // Step 6: Register "new" constructor
        std::cout << "Step 6: Register 'new'" << std::endl;
        luaL_getmetatable(lua.lua_state(), "Person");
        print_stack(lua.lua_state(), "After getting metatable");
        
        lua_pushcfunction(lua.lua_state(), [](lua_State* L) -> int {
            std::cout << "Constructor 'new' called!" << std::endl;
            void* obj = lua_newuserdatauv(L, sizeof(Person*), 0);
            std::cout << "  userdata created at " << obj << std::endl;
            
            // Get metatable and set it
            luaL_getmetatable(L, "Person");
            std::cout << "  metatable retrieved" << std::endl;
            lua_setmetatable(L, -2);
            std::cout << "  metatable set" << std::endl;
            
            Person** ptr = static_cast<Person**>(obj);
            *ptr = new Person();
            std::cout << "  Person object created" << std::endl;
            
            return 1;
        });
        lua_setfield(lua.lua_state(), -2, "new");
        print_stack(lua.lua_state(), "After setting 'new'");
        lua_pop(lua.lua_state(), 1);
        print_stack(lua.lua_state(), "After pop metatable");
        
        // Step 7: Register global constructor
        std::cout << "Step 7: Register global Person()" << std::endl;
        lua_pushstring(lua.lua_state(), "Person");
        lua_pushcclosure(lua.lua_state(), [](lua_State* L) -> int {
            const char* name = lua_tostring(L, lua_upvalueindex(1));
            std::cout << "Global Person() called with name: " << name << std::endl;
            
            luaL_getmetatable(L, name);
            std::cout << "  metatable type: " << lua_typename(L, -1) << std::endl;
            if (lua_isnil(L, -1)) {
                std::cerr << "  ERROR: metatable is nil!" << std::endl;
                return luaL_error(L, "metatable not found");
            }
            
            lua_getfield(L, -1, "new");
            std::cout << "  'new' type: " << lua_typename(L, -1) << std::endl;
            if (lua_isnil(L, -1)) {
                std::cerr << "  ERROR: 'new' is nil!" << std::endl;
                return luaL_error(L, "'new' not found");
            }
            
            // Remove metatable, leaving just 'new' function
            lua_remove(L, -2);
            std::cout << "  calling 'new'..." << std::endl;
            
            int result = lua_pcall(L, 0, 1, 0);
            if (result != LUA_OK) {
                std::cerr << "  ERROR in pcall: " << lua_tostring(L, -1) << std::endl;
                return lua_error(L);
            }
            
            std::cout << "  'new' returned, result type: " << lua_typename(L, -1) << std::endl;
            return 1;
        }, 1);
        lua_setglobal(lua.lua_state(), "Person");
        print_stack(lua.lua_state(), "After registering Person()");
        
        // Step 8: Test calling Person()
        std::cout << "\n=== Testing Person() call ===" << std::endl;
        int result = luaL_dostring(lua.lua_state(), "p = Person(); print('Created person')");
        if (result != LUA_OK) {
            std::cout << "Error: " << lua_tostring(lua.lua_state(), -1) << std::endl;
            lua_pop(lua.lua_state(), 1);
            return 1;
        }
        
        std::cout << "\nSuccess!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}