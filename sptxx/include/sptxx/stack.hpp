// stack.hpp - Stack operations and type conversion for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include <optional>
#include <string>
#include <type_traits>

namespace sptxx {

// Forward declarations
template<typename T>
struct getter;
template<typename T>
struct pusher;

namespace stack {
    // Generic get function
    template<typename T>
    inline T get(lua_State* L, int index) {
        return getter<T>::get(L, index);
    }
    
    // Generic push function
    template<typename T>
    inline void push(lua_State* L, T&& value) {
        pusher<std::decay_t<T>>::push(L, std::forward<T>(value));
    }
}

// Specializations for basic types

// nil
template<>
struct getter<void> {
    static void get(lua_State* L, int index) {
        if (!lua_isnil(L, index)) {
            luaL_error(L, "Expected nil at index %d", index);
        }
    }
};

template<>
struct pusher<void> {
    static void push(lua_State* L, void*) {
        lua_pushnil(L);
    }
};

// boolean
template<>
struct getter<bool> {
    static bool get(lua_State* L, int index) {
        return lua_toboolean(L, index) != 0;
    }
};

template<>
struct pusher<bool> {
    static void push(lua_State* L, bool value) {
        lua_pushboolean(L, value);
    }
};

// integer types
template<>
struct getter<int> {
    static int get(lua_State* L, int index) {
        return static_cast<int>(luaL_checkinteger(L, index));
    }
};

template<>
struct pusher<int> {
    static void push(lua_State* L, int value) {
        lua_pushinteger(L, value);
    }
};

template<>
struct getter<long> {
    static long get(lua_State* L, int index) {
        return static_cast<long>(luaL_checkinteger(L, index));
    }
};

template<>
struct pusher<long> {
    static void push(lua_State* L, long value) {
        lua_pushinteger(L, value);
    }
};

template<>
struct getter<long long> {
    static long long get(lua_State* L, int index) {
        return static_cast<long long>(luaL_checkinteger(L, index));
    }
};

template<>
struct pusher<long long> {
    static void push(lua_State* L, long long value) {
        lua_pushinteger(L, value);
    }
};

// unsigned integer types
template<>
struct getter<unsigned int> {
    static unsigned int get(lua_State* L, int index) {
        return static_cast<unsigned int>(luaL_checkinteger(L, index));
    }
};

template<>
struct pusher<unsigned int> {
    static void push(lua_State* L, unsigned int value) {
        lua_pushinteger(L, value);
    }
};

// floating point types
template<>
struct getter<float> {
    static float get(lua_State* L, int index) {
        return static_cast<float>(luaL_checknumber(L, index));
    }
};

template<>
struct pusher<float> {
    static void push(lua_State* L, float value) {
        lua_pushnumber(L, value);
    }
};

template<>
struct getter<double> {
    static double get(lua_State* L, int index) {
        return static_cast<double>(luaL_checknumber(L, index));
    }
};

template<>
struct pusher<double> {
    static void push(lua_State* L, double value) {
        lua_pushnumber(L, value);
    }
};

// string types
template<>
struct getter<const char*> {
    static const char* get(lua_State* L, int index) {
        return luaL_checkstring(L, index);
    }
};

template<>
struct pusher<const char*> {
    static void push(lua_State* L, const char* value) {
        if (value) {
            lua_pushstring(L, value);
        } else {
            lua_pushnil(L);
        }
    }
};

template<>
struct getter<std::string> {
    static std::string get(lua_State* L, int index) {
        size_t len;
        const char* str = luaL_checklstring(L, index, &len);
        return std::string(str, len);
    }
};

template<>
struct pusher<std::string> {
    static void push(lua_State* L, const std::string& value) {
        lua_pushlstring(L, value.data(), value.size());
    }
    
    static void push(lua_State* L, std::string&& value) {
        lua_pushlstring(L, value.data(), value.size());
    }
};

template <typename T> struct getter<std::optional<T>> {
  static std::optional<T> get(lua_State *L, int index) {
    if (lua_isnil(L, index)) {
      return std::nullopt;
    }
    return stack::get<T>(L, index);
  }
};

template <typename T> struct pusher<std::optional<T>> {
  static void push(lua_State *L, const std::optional<T> &value) {
    if (value.has_value()) {
      stack::push(L, value.value());
    } else {
      lua_pushnil(L);
    }
  }

  static void push(lua_State *L, std::optional<T> &&value) {
    if (value.has_value()) {
      stack::push(L, std::move(value.value()));
    } else {
      lua_pushnil(L);
    }
  }
};

} // namespace sptxx