// map.hpp - Map type support for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "stack.hpp"
#include "error.hpp"
#include <cstddef>

namespace sptxx {

template<typename T = void>
class map {
private:
    lua_State* L;
    int ref;
    
public:
    // Constructors
    map() : L(nullptr), ref(LUA_NOREF) {}
    
    map(lua_State* state, int reference) : L(state), ref(reference) {
        if (ref == LUA_NOREF || ref == LUA_REFNIL) {
            throw error("Invalid map reference");
        }
        // Verify it's actually a map (TABLE_MAP)
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        if (!lua_ismap(L, -1)) {
            lua_pop(L, 1);
            throw error("Reference is not a map (TABLE_MAP)");
        }
        lua_pop(L, 1);
    }
    
    // Copy constructor
    map(const map& other) : L(other.L), ref(LUA_NOREF) {
        if (other.valid()) {
            lua_rawgeti(L, LUA_REGISTRYINDEX, other.ref);
            ref = luaL_ref(L, LUA_REGISTRYINDEX);
        }
    }
    
    // Move constructor
    map(map&& other) noexcept : L(other.L), ref(other.ref) {
        other.L = nullptr;
        other.ref = LUA_NOREF;
    }
    
    // Assignment operators
    map& operator=(const map& other) {
        if (this != &other) {
            if (valid()) {
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
            L = other.L;
            if (other.valid()) {
                lua_rawgeti(L, LUA_REGISTRYINDEX, other.ref);
                ref = luaL_ref(L, LUA_REGISTRYINDEX);
            } else {
                ref = LUA_NOREF;
            }
        }
        return *this;
    }
    
    map& operator=(map&& other) noexcept {
        if (this != &other) {
            if (valid()) {
                luaL_unref(L, LUA_REGISTRYINDEX, ref);
            }
            L = other.L;
            ref = other.ref;
            other.L = nullptr;
            other.ref = LUA_NOREF;
        }
        return *this;
    }
    
    ~map() {
        if (valid()) {
            luaL_unref(L, LUA_REGISTRYINDEX, ref);
        }
    }
    
    // Validity check
    bool valid() const { return L != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }
    explicit operator bool() const { return valid(); }
    
    // Size (always returns 0 for maps according to SPT design)
    std::size_t size() const {
        if (!valid()) {
            throw error("Invalid map");
        }
        // According to SPT design, # operator on TABLE_MAP always returns 0
        return 0;
    }
    
    // Element access
    template<typename Key>
    T get(const Key& key) const {
        if (!valid()) {
            throw error("Invalid map");
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        stack::push(L, key);
        lua_gettable(L, -2);
        if (lua_isnil(L, -1)) {
            lua_pop(L, 2);
            throw error("Key not found in map");
        }
        T result = stack::get<T>(L, -1);
        lua_pop(L, 2);
        return result;
    }
    
    template<typename Key>
    bool contains(const Key& key) const {
        if (!valid()) {
            throw error("Invalid map");
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        stack::push(L, key);
        lua_gettable(L, -2);
        bool exists = !lua_isnil(L, -1);
        lua_pop(L, 2);
        return exists;
    }
    
    template<typename Key>
    void set(const Key& key, const T& value) {
        if (!valid()) {
            throw error("Invalid map");
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        stack::push(L, key);
        stack::push(L, value);
        lua_settable(L, -3);
        lua_pop(L, 1);
    }
    
    template<typename Key>
    void set(const Key& key, T&& value) {
        if (!valid()) {
            throw error("Invalid map");
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        stack::push(L, key);
        stack::push(L, std::move(value));
        lua_settable(L, -3);
        lua_pop(L, 1);
    }
    
    template<typename Key>
    void remove(const Key& key) {
        if (!valid()) {
            throw error("Invalid map");
        }
        lua_rawgeti(L, LUA_REGISTRYINDEX, ref);
        stack::push(L, key);
        lua_pushnil(L);
        lua_settable(L, -3);
        lua_pop(L, 1);
    }
    
    // Raw Lua state access
    lua_State* lua_state() const { return L; }
    int registry_index() const { return ref; }
};

// Specialization for void (generic object map)
template<>
class map<void> {
private:
    lua_State* L;
    int ref;
    
public:
    map() : L(nullptr), ref(LUA_NOREF) {}
    map(lua_State* state, int reference) : L(state), ref(reference) {}
    
    // ... similar implementation but using generic object handling
};

using object_map = map<void>;

} // namespace sptxx