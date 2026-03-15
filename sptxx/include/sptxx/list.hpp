// list.hpp - List type support for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "stack.hpp"
#include "error.hpp"
#include <cstddef>
#include <iterator>

namespace sptxx {

template<typename T = void>
class list {
private:
    lua_State* L;
    void* key; // light userdata key for registry
    
public:
    // Constructors
    list() : L(nullptr), key(nullptr) {}
    
    list(lua_State* state, void* k) : L(state), key(k) {
        if (key == nullptr) {
            throw error("Invalid list key");
        }
    }
    
    // Copy constructor
    list(const list& other) : L(other.L), key(nullptr) {
        if (other.valid()) {
            // Get the array from registry using key
            lua_pushlightuserdata(L, other.key);
            lua_rawget(L, LUA_REGISTRYINDEX);
            // Store it with a new key
            key = lua_newuserdatauv(L, 0, 0); // create a new unique key
            lua_pushvalue(L, -2); // push the array
            lua_rawsetp(L, LUA_REGISTRYINDEX, key); // registry[key] = array
            lua_pop(L, 1); // pop the array
        }
    }
    
    // Move constructor
    list(list&& other) noexcept : L(other.L), key(other.key) {
        other.L = nullptr;
        other.key = nullptr;
    }
    
    // Assignment operators
    list& operator=(const list& other) {
        if (this != &other) {
            if (valid()) {
                // Remove old entry from registry
                lua_pushlightuserdata(L, key);
                lua_pushnil(L);
                lua_rawset(L, LUA_REGISTRYINDEX);
            }
            L = other.L;
            if (other.valid()) {
                // Get the array from registry using other's key
                lua_pushlightuserdata(L, other.key);
                lua_rawget(L, LUA_REGISTRYINDEX);
                // Store it with a new key
                key = lua_newuserdatauv(L, 0, 0); // create a new unique key
                lua_pushvalue(L, -2); // push the array
                lua_rawsetp(L, LUA_REGISTRYINDEX, key); // registry[key] = array
                lua_pop(L, 1); // pop the array
            } else {
                key = nullptr;
            }
        }
        return *this;
    }
    
    list& operator=(list&& other) noexcept {
        if (this != &other) {
            if (valid()) {
                // Remove old entry from registry
                lua_pushlightuserdata(L, key);
                lua_pushnil(L);
                lua_rawset(L, LUA_REGISTRYINDEX);
            }
            L = other.L;
            key = other.key;
            other.L = nullptr;
            other.key = nullptr;
        }
        return *this;
    }
    
    ~list() {
        if (valid()) {
            // Remove entry from registry
            lua_pushlightuserdata(L, key);
            lua_pushnil(L);
            lua_rawset(L, LUA_REGISTRYINDEX);
        }
    }
    
    // Validity check
    bool valid() const { return L != nullptr && key != nullptr; }
    explicit operator bool() const { return valid(); }
    
    // Size and capacity
    std::size_t size() const {
        if (!valid()) {
            throw error("Invalid list");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_Integer len = lua_arraylen(L, -1);
        lua_pop(L, 1);
        return static_cast<std::size_t>(len);
    }
    
    std::size_t capacity() const {
        if (!valid()) {
            throw error("Invalid list");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_Integer cap = lua_arraycapacity(L, -1);
        lua_pop(L, 1);
        return static_cast<std::size_t>(cap);
    }
    
    bool empty() const {
        if (!valid()) {
            throw error("Invalid list");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        int is_empty = lua_arrayisempty(L, -1);
        lua_pop(L, 1);
        return is_empty != 0;
    }
    
    // Resize operations
    void resize(std::size_t new_size) {
        if (!valid()) {
            throw error("Invalid list");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_arraysetlen(L, -1, static_cast<lua_Integer>(new_size));
        lua_pop(L, 1);
    }
    
    void reserve(std::size_t capacity) {
        if (!valid()) {
            throw error("Invalid list");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_arrayreserve(L, -1, static_cast<lua_Integer>(capacity));
        lua_pop(L, 1);
    }
    
    // Element access
    template<typename U = T>
    auto get(std::size_t index) const -> typename std::enable_if<!std::is_void_v<U>, U>::type {
        if (!valid()) {
            throw error("Invalid list");
        }
        if (index >= size()) {
            throw error("List index out of range");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        lua_geti(L, -1, static_cast<lua_Integer>(index));
        U result = stack::get<U>(L, -1);
        lua_pop(L, 2);
        return result;
    }
    
    void set(std::size_t index, const T& value) {
        if (!valid()) {
            throw error("Invalid list");
        }
        if (index >= size()) {
            throw error("List index out of range");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        stack::push(L, value);
        lua_seti(L, -2, static_cast<lua_Integer>(index));
        lua_pop(L, 1);
    }
    
    void set(std::size_t index, T&& value) {
        if (!valid()) {
            throw error("Invalid list");
        }
        if (index >= size()) {
            throw error("List index out of range");
        }
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        stack::push(L, std::move(value));
        lua_seti(L, -2, static_cast<lua_Integer>(index));
        lua_pop(L, 1);
    }
    
    // Push/pop operations (like table.push/table.pop)
    void push_back(const T& value) {
        if (!valid()) {
            throw error("Invalid list");
        }
        // Get current size
        std::size_t current_size = size();
        // Get the list from registry
        lua_pushlightuserdata(L, key);
        lua_rawget(L, LUA_REGISTRYINDEX);
        // Push the value
        stack::push(L, value);
        // Set at index = current_size (which will trigger automatic growth)
        lua_seti(L, -2, static_cast<lua_Integer>(current_size));
        lua_pop(L, 1); // pop the list
    }
    
    T pop_back() {
        if (!valid()) {
            throw error("Invalid list");
        }
        std::size_t current_size = size();
        if (current_size == 0) {
            throw error("Cannot pop from empty list");
        }
        // Get the last element
        T result = get(current_size - 1);
        // Resize to remove the last element
        resize(current_size - 1);
        return result;
    }
    
    // Iterator support (basic)
    class iterator {
    private:
        list* lst;
        std::size_t pos;
        
    public:
        using difference_type = std::ptrdiff_t;
        using value_type = T;
        using pointer = T*;
        using reference = T&;
        using iterator_category = std::random_access_iterator_tag;
        
        iterator(list* l, std::size_t p) : lst(l), pos(p) {}
        
        T operator*() const {
            return lst->get(pos);
        }
        
        iterator& operator++() {
            ++pos;
            return *this;
        }
        
        iterator operator++(int) {
            iterator tmp = *this;
            ++(*this);
            return tmp;
        }
        
        bool operator==(const iterator& other) const {
            return lst == other.lst && pos == other.pos;
        }
        
        bool operator!=(const iterator& other) const {
            return !(*this == other);
        }
    };
    
    iterator begin() {
        return iterator(this, 0);
    }
    
    iterator end() {
        return iterator(this, size());
    }
    
    // Raw Lua state access
    lua_State* lua_state() const { return L; }
    void* registry_key() const { return key; }
};

// Specialization for void (generic object list)
template<>
class list<void> {
private:
    lua_State* L;
    int ref;
    
public:
    list() : L(nullptr), ref(LUA_NOREF) {}
    list(lua_State* state, int reference) : L(state), ref(reference) {}
    
    // ... similar implementation but using generic object handling
    // For brevity, we'll implement the basic version first
};

using object_list = list<void>;

} // namespace sptxx
