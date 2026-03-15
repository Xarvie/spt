// test_usertype_debug.cpp - Debug usertype registration

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
        
        // Step 1: Create metatable manually
        luaL_newmetatable(lua.lua_state(), "Person");
        std::cout << "Metatable created, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        // Step 2: Set __index
        lua_pushvalue(lua.lua_state(), -1);
        lua_setfield(lua.lua_state(), -2, "__index");
        std::cout << "__index set, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        // Step 3: Set __name
        lua_pushstring(lua.lua_state(), "Person");
        lua_setfield(lua.lua_state(), -2, "__name");
        std::cout << "__name set, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        // Step 4: Store in registry
        int ref = luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX);
        std::cout << "Stored in registry with ref: " << ref << ", stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        // Step 5: Get metatable back and set "new"
        lua_rawgeti(lua.lua_state(), LUA_REGISTRYINDEX, ref);
        std::cout << "Retrieved metatable, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        std::cout << "Metatable type: " << lua_typename(lua.lua_state(), -1) << std::endl;
        
        // Step 6: Create constructor function
        lua_pushcfunction(lua.lua_state(), [](lua_State* L) -> int {
            std::cout << "Constructor called!" << std::endl;
            void* obj = lua_newuserdatauv(L, sizeof(void*), 0);
            *(void**)obj = new Person();
            return 1;
        });
        lua_setfield(lua.lua_state(), -2, "new");
        std::cout << "\"new\" method set, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        lua_pop(lua.lua_state(), 1); // pop metatable
        std::cout << "Metatable popped, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        // Step 7: Register global constructor
        lua_pushstring(lua.lua_state(), "Person");
        lua_pushcclosure(lua.lua_state(), [](lua_State* L) -> int {
            const char* name = lua_tostring(L, lua_upvalueindex(1));
            std::cout << "Global constructor called for: " << name << std::endl;
            luaL_getmetatable(L, name);
            lua_getfield(L, -1, "new");
            lua_call(L, 0, 1);
            return 1;
        }, 1);
        lua_setglobal(lua.lua_state(), "Person");
        std::cout << "Global constructor registered, stack top: " << lua_gettop(lua.lua_state()) << std::endl;
        
        // Step 8: Test calling Person()
        std::cout << "\nTesting Person() call..." << std::endl;
        lua.do_string("p = Person(); print('Created person')");
        std::cout << "Person() call succeeded!" << std::endl;
        
        std::cout << "\nDebug test passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}