// state.hpp - Lua state 管理与顶层 API
// basic_state 持有 lua_State*，提供 set/get_global、set_function、
// create_list/create_map、new_usertype/get_usertype、call、do_string/do_file 等。
// state = basic_state<>（默认分配器）。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include "error.hpp"
#include "function.hpp"
#include "list.hpp"
#include "map.hpp"
#include "stack.hpp"
#include "table_proxy.hpp"
#include "usertype.hpp"
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace sptxx {

template <typename Allocator = std::allocator<void>> class basic_state {
public:
  basic_state() : L_(luaL_newstate()), own_state_(true) {
    if (!L_)
      throw error("failed to create Lua state");
  }

  explicit basic_state(lua_State *L, bool take_ownership = false)
      : L_(L), own_state_(take_ownership) {
    if (!L_)
      throw error("Lua state cannot be null");
  }

  basic_state(const basic_state &) = delete;
  basic_state &operator=(const basic_state &) = delete;

  basic_state(basic_state &&other) noexcept : L_(other.L_), own_state_(other.own_state_) {
    other.L_ = nullptr;
    other.own_state_ = false;
  }

  basic_state &operator=(basic_state &&other) noexcept {
    if (this != &other) {
      if (own_state_ && L_)
        lua_close(L_);
      L_ = other.L_;
      own_state_ = other.own_state_;
      other.L_ = nullptr;
      other.own_state_ = false;
    }
    return *this;
  }

  ~basic_state() {
    if (own_state_ && L_)
      lua_close(L_);
  }

  lua_State *lua_state() const { return L_; }
  operator lua_State *() const { return L_; }

  int get_top() const { return lua_gettop(L_); }
  void set_top(int index) { lua_settop(L_, index); }

  // 打开所有标准库（base, coroutine, table, string, math, io, os, debug, utf8, package）
  void open_libraries() { luaL_openlibs(L_); }

  int do_string(const char *code, const char *chunkname = nullptr) {
    (void)chunkname;
    int status = luaL_dostring(L_, code);
    detail::handle_lua_error(L_, status);
    return status;
  }

  int do_file(const char *filename) {
    int status = luaL_dofile(L_, filename);
    detail::handle_lua_error(L_, status);
    return status;
  }

  // ---- 全局变量 ----

  template <typename T> T get_global(const char *name) {
    lua_getglobal(L_, name);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 1);
      throw error(std::string("global '") + name + "' not found");
    }
    T result = stack::get<T>(L_, -1);
    lua_pop(L_, 1);
    return result;
  }

  // table_proxy 链式访问入口：lua["a"]["b"] = value;  /  int x = lua["a"].get<int>();
  table_proxy operator[](const char *key) { return table_proxy(L_, key); }

  template <typename T> void set_global(const char *name, T &&value) {
    stack::push(L_, std::forward<T>(value));
    lua_setglobal(L_, name);
  }

  template <typename T = void> std::optional<T> get_global_or(const char *name) {
    lua_getglobal(L_, name);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 1);
      return std::nullopt;
    }
    T result = stack::get<T>(L_, -1);
    lua_pop(L_, 1);
    return result;
  }

  // ---- 容器创建 ----

  template <typename T = void> list<T> create_list(std::size_t capacity = 0) {
    lua_createarray(L_, static_cast<int>(capacity));
    if (capacity > 0)
      lua_arraysetlen(L_, -1, static_cast<lua_Integer>(capacity));
    int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    return list<T>(L_, ref);
  }

  template <typename T = void> map<T> create_map() {
    lua_createtable(L_, 0, 0);
    return map<T>(L_, luaL_ref(L_, LUA_REGISTRYINDEX));
  }

  // ---- 函数绑定 ----

  template <typename Func> void set_function(const char *name, Func &&func) {
    detail::push_function_wrapper(L_, std::forward<Func>(func));
    lua_setglobal(L_, name);
  }

  template <typename Signature> function_ref<Signature> get_function(const char *name) {
    lua_getglobal(L_, name);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 1);
      throw error(std::string("function '") + name + "' not found");
    }
    if (!lua_isfunction(L_, -1)) {
      lua_pop(L_, 1);
      throw error(std::string("'") + name + "' is not a function");
    }
    int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    return function_ref<Signature>(L_, ref);
  }

  // 调用全局 Lua 函数。R 可为 void、单值、或 std::tuple（多返回值）。
  template <typename R = void, typename... Args> R call(const char *name, Args &&...args) {
    lua_getglobal(L_, name);
    if (!lua_isfunction(L_, -1)) {
      lua_pop(L_, 1);
      throw error(std::string("'") + name + "' is not a function");
    }

    lua_pushnil(L_); // Slot 0 receiver
    detail::push_args(L_, std::forward<Args>(args)...);

    int nargs = static_cast<int>(sizeof...(Args)) + 1; // +1 for receiver
    int nresults;
    if constexpr (std::is_void_v<R>) {
      nresults = 0;
    } else if constexpr (detail::is_tuple_v<R>) {
      nresults = static_cast<int>(std::tuple_size_v<R>);
    } else {
      nresults = 1;
    }

    int status = lua_pcall(L_, nargs, nresults, 0);
    if (status != LUA_OK) {
      const char *msg = lua_tostring(L_, -1);
      std::string err = msg ? std::string(msg) : std::string("unknown Lua error");
      lua_pop(L_, 1);
      throw runtime_error("calling '" + std::string(name) + "': " + err);
    }

    if constexpr (std::is_void_v<R>) {
      return;
    } else if constexpr (detail::is_tuple_v<R>) {
      R ret = detail::extract_multi_return<R>(L_, -nresults);
      lua_pop(L_, nresults);
      return ret;
    } else {
      R ret = stack::get<R>(L_, -1);
      lua_pop(L_, 1);
      return ret;
    }
  }

  // ---- usertype ----

  template <typename T> usertype<T> new_usertype(const char *name) {
    return usertype<T>(L_, name);
  }

  template <typename T> usertype<T> get_usertype(const char *name) {
    if (luaL_getmetatable(L_, name) == LUA_TNIL) {
      lua_pop(L_, 1);
      throw error(std::string("usertype '") + name + "' not found");
    }
    lua_pop(L_, 1);
    return usertype<T>(L_, name);
  }

private:
  lua_State *L_;
  bool own_state_;
};

using state = basic_state<>;

} // namespace sptxx
