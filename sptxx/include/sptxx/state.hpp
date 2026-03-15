// state.hpp - Core state management for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
#include "../../src/Vm/lualib.h"
}

#include "stack.hpp"
#include "error.hpp"
#include "function.hpp"
#include "list.hpp"
#include "map.hpp"
#include "usertype.hpp"
#include <iostream>
#include <memory>
#include <string>
#include <type_traits>

namespace sptxx {

namespace detail {
    // Helper to handle receiver slot automatically
    inline void ensure_receiver_slot(lua_State* L) {
        // Push nil as receiver at slot 0 (index 1)
        lua_pushnil(L);
        lua_insert(L, 1);
    }
    
    inline void remove_receiver_slot(lua_State* L) {
        // Remove the receiver slot (index 1)
        lua_remove(L, 1);
    }
}

template<typename Allocator = std::allocator<void>>
class basic_state {
private:
    lua_State* L;
    bool own_state;

public:
    // Constructors
    basic_state() : L(luaL_newstate()), own_state(true) {
        if (!L) {
            throw std::runtime_error("Failed to create Lua state");
        }
    }
    
    explicit basic_state(lua_State* state, bool take_ownership = false) 
        : L(state), own_state(take_ownership) {
        if (!L) {
            throw std::invalid_argument("Lua state cannot be null");
        }
    }
    
    // Non-copyable
    basic_state(const basic_state&) = delete;
    basic_state& operator=(const basic_state&) = delete;
    
    // Movable
    basic_state(basic_state&& other) noexcept 
        : L(other.L), own_state(other.own_state) {
        other.L = nullptr;
        other.own_state = false;
    }
    
    basic_state& operator=(basic_state&& other) noexcept {
        if (this != &other) {
            if (own_state && L) {
                lua_close(L);
            }
            L = other.L;
            own_state = other.own_state;
            other.L = nullptr;
            other.own_state = false;
        }
        return *this;
    }
    
    ~basic_state() {
        if (own_state && L) {
            lua_close(L);
        }
    }
    
    // Accessors
    lua_State* lua_state() const { return L; }
    operator lua_State*() const { return L; }
    
    // Basic stack operations
    int get_top() const { return lua_gettop(L); }
    void set_top(int index) { lua_settop(L, index); }
    
    // Open standard libraries
    void open_libraries() {
        // Load base library (includes print, etc.)
        luaL_requiref(L, "_G", luaopen_base, 1);
        lua_pop(L, 1); // pop the _G table
        
        // Load table library
        luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
        lua_pop(L, 1); // pop the table library
    }
    
    // Execute code
    int do_string(const char* code, const char* chunkname = nullptr) {
        // For execution, we don't need receiver slot
        int result = luaL_dostring(L, code);
        detail::handle_lua_error(L, result);
        return result;
    }
    
    int do_file(const char* filename) {
        // For execution, we don't need receiver slot
        int result = luaL_dofile(L, filename);
        detail::handle_lua_error(L, result);
        return result;
    }
    
    // Get global variable (automatically handles receiver)
    template<typename T>
    T get_global(const char* name) {
        lua_getglobal(L, name);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 1);
            throw error(std::string("Global variable '") + name + "' not found");
        }
        
        // Extract value from stack
        T result = stack::get<T>(L, -1);
        lua_pop(L, 1);
        return result;
    }
    
    // Set global variable (automatically handles receiver)
    template<typename T>
    void set_global(const char* name, T&& value) {
        // Push value with automatic receiver handling
        stack::push(L, std::forward<T>(value));
        lua_setglobal(L, name);
    }
    
    // Create list and map
    template<typename T = void>
    auto create_list(size_t capacity = 0) {
        lua_createarray(L, static_cast<int>(capacity));
        if (capacity > 0) {
            // Set logical length to capacity so we can write to indices [0, capacity-1]
            lua_arraysetlen(L, -1, static_cast<lua_Integer>(capacity));
        }
        // Store in registry using a unique light userdata key
        void* key = lua_newuserdatauv(L, 0, 0); // create a unique key
        lua_pushvalue(L, -2); // push the array (now at -2 because of the new key)
        lua_rawsetp(L, LUA_REGISTRYINDEX, key); // registry[key] = array
        lua_pop(L, 1); // pop the array, leaving the key on stack
        return list<T>(L, key);
    }
    
    template<typename T = void>
    auto create_map() {
        lua_createtable(L, 0, 0);
        return map<T>(L, luaL_ref(L, LUA_REGISTRYINDEX));
    }
    
    // Function registration
    template<typename Func>
    void set_function(const char* name, Func&& func) {
        auto wrapper = detail::make_function_wrapper(std::forward<Func>(func));
        lua_pushcfunction(L, wrapper);
        lua_setglobal(L, name);
    }
    
    // Usertype registration
    template<typename T>
    usertype<T> new_usertype(const char* name) {
        usertype<T> ut(L, name);
        
        // Register default constructor automatically
        ut.constructor();
        
        // Register the constructor function in the global namespace
        // so Lua can call Person() to create instances
        // Push the name as upvalue and create the closure
        lua_pushstring(L, name);
        lua_pushcclosure(L, &usertype_constructor, 1);
        lua_setglobal(L, name);
        
        return ut;
    }
    
private:
    // Static helper function for usertype constructor
    static int usertype_constructor(lua_State* L) {
        const char* name = lua_tostring(L, lua_upvalueindex(1));
        
        // Get the metatable
        luaL_getmetatable(L, name);
        if (lua_isnil(L, -1)) {
            return luaL_error(L, "metatable for '%s' not found", name);
        }
        
        // Get the "new" function from metatable
        lua_getfield(L, -1, "new");
        if (lua_isnil(L, -1)) {
            lua_pop(L, 2);  // pop new and metatable
            return luaL_error(L, "constructor 'new' not found for '%s'", name);
        }
        
        // Stack is now: [metatable, new_func]
        // Remove the metatable, leaving just the function
        lua_remove(L, -2);  // Stack: [new_func]
        
        // Call the "new" function directly using lua_call
        // The function will create userdata and return it
        lua_call(L, 0, 1);
        
        // Return 1 - the userdata object is on the stack
        return 1;
    }
    
public:
};

// Type alias for convenience
using state = basic_state<>;

} // namespace sptxx