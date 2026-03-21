// state.hpp - Core state management for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
#include "../../src/Vm/lualib.h"
}

#include "error.hpp"
#include "function.hpp"
#include "list.hpp"
#include "map.hpp"
#include "stack.hpp"
#include "usertype.hpp"
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>

namespace sptxx {

template<typename Allocator = std::allocator<void>>
class basic_state {
private:
  lua_State *L;
  bool own_state;

public:
  basic_state() : L(luaL_newstate()), own_state(true) {
    if (!L) {
      throw std::runtime_error("Failed to create Lua state");
    }
  }

  explicit basic_state(lua_State *state, bool take_ownership = false)
      : L(state), own_state(take_ownership) {
    if (!L) {
      throw std::invalid_argument("Lua state cannot be null");
    }
  }

  basic_state(const basic_state &) = delete;
  basic_state &operator=(const basic_state &) = delete;

  basic_state(basic_state &&other) noexcept : L(other.L), own_state(other.own_state) {
    other.L = nullptr;
    other.own_state = false;
  }

  basic_state &operator=(basic_state &&other) noexcept {
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

  lua_State *lua_state() const { return L; }

  operator lua_State *() const { return L; }

  int get_top() const { return lua_gettop(L); }

  void set_top(int index) { lua_settop(L, index); }

  void open_libraries() {
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);

    luaL_requiref(L, LUA_TABLIBNAME, luaopen_table, 1);
    lua_pop(L, 1);
  }

  int do_string(const char *code, const char *chunkname = nullptr) {
    int result = luaL_dostring(L, code);
    detail::handle_lua_error(L, result);
    return result;
  }

  int do_file(const char *filename) {
    int result = luaL_dofile(L, filename);
    detail::handle_lua_error(L, result);
    return result;
  }

  template <typename T> T get_global(const char *name) {
    lua_getglobal(L, name);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      throw error(std::string("Global variable '") + name + "' not found");
    }
    T result = stack::get<T>(L, -1);
    lua_pop(L, 1);
    return result;
  }

  template <typename T> void set_global(const char *name, T &&value) {
    stack::push(L, std::forward<T>(value));
    lua_setglobal(L, name);
  }

  template <typename T = void> auto create_list(size_t capacity = 0) {
    lua_createarray(L, static_cast<int>(capacity));
    if (capacity > 0) {
      lua_arraysetlen(L, -1, static_cast<lua_Integer>(capacity));
    }
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return list<T>(L, ref);
  }

  template <typename T = void> auto create_map() {
    lua_createtable(L, 0, 0);
    return map<T>(L, luaL_ref(L, LUA_REGISTRYINDEX));
  }

  template <typename Func> void set_function(const char *name, Func &&func) {
    detail::push_function_wrapper(L, std::forward<Func>(func));
    lua_setglobal(L, name);
  }

  template <typename T> usertype<T> new_usertype(const char *name) {
    usertype<T> ut(L, name);
    ut.constructor();
    return ut;
  }

  template <typename R = void, typename... Args> R call(const char *name, Args &&...args) {
    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      throw error(std::string("'") + name + "' is not a function");
    }

    lua_pushnil(L);

    detail::push_args<Args...>(L, std::forward<Args>(args)...);

    int nargs = sizeof...(Args) + 1;
    int nresults = 1;
    if constexpr (std::is_void_v<R>) {
      nresults = 0;
    } else if constexpr (detail::is_tuple_v<R>) {
      nresults = static_cast<int>(std::tuple_size_v<R>);
    }

    int result = lua_pcall(L, nargs, nresults, 0);
    if (result != LUA_OK) {
      std::string err = lua_tostring(L, -1);
      lua_pop(L, 1);
      throw error("Lua error calling '" + std::string(name) + "': " + err);
    }

    if constexpr (std::is_void_v<R>) {
      return;
    } else if constexpr (detail::is_tuple_v<R>) {
      auto ret = detail::extract_multi_return<R>(L, -nresults);
      lua_pop(L, nresults);
      return ret;
    } else {
      R ret = stack::get<R>(L, -1);
      lua_pop(L, 1);
      return ret;
    }
  }

  template <typename Signature> function_ref<Signature> get_function(const char *name) {
    lua_getglobal(L, name);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      throw error(std::string("Function '") + name + "' not found");
    }
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      throw error(std::string("'") + name + "' is not a function");
    }
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return function_ref<Signature>(L, ref);
  }

  template <typename T = void> std::optional<T> get_global_or(const char *name) {
    lua_getglobal(L, name);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 1);
      return std::nullopt;
    }
    T result = stack::get<T>(L, -1);
    lua_pop(L, 1);
    return result;
  }
};

using state = basic_state<>;

} // namespace sptxx
