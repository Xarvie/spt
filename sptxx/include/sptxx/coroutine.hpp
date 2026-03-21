// coroutine.hpp - Coroutine support for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "error.hpp"
#include "stack.hpp"
#include <functional>
#include <optional>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sptxx {

enum class coroutine_status {
  running,
  suspended,
  normal,
  dead
};

class coroutine {
private:
  lua_State *L;
  lua_State *co;
  int ref;

public:
  coroutine() : L(nullptr), co(nullptr), ref(LUA_NOREF) {}

  coroutine(lua_State *main_state, lua_State *thread, int reference)
      : L(main_state), co(thread), ref(reference) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
      throw error("Invalid coroutine reference");
    }
    if (!co) {
      throw error("Coroutine thread is null");
    }
  }

  coroutine(const coroutine &) = delete;
  coroutine &operator=(const coroutine &) = delete;

  coroutine(coroutine &&other) noexcept : L(other.L), co(other.co), ref(other.ref) {
    other.L = nullptr;
    other.co = nullptr;
    other.ref = LUA_NOREF;
  }

  coroutine &operator=(coroutine &&other) noexcept {
    if (this != &other) {
      if (valid()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
      }
      L = other.L;
      co = other.co;
      ref = other.ref;
      other.L = nullptr;
      other.co = nullptr;
      other.ref = LUA_NOREF;
    }
    return *this;
  }

  ~coroutine() {
    if (valid()) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }

  bool valid() const { return L != nullptr && co != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }

  explicit operator bool() const { return valid(); }

  lua_State *thread_state() const { return co; }
  lua_State *main_state() const { return L; }
  int registry_index() const { return ref; }

  coroutine_status status() const {
    if (!valid()) {
      throw error("Invalid coroutine");
    }

    if (L == co) {
      return coroutine_status::running;
    }

    int s = lua_status(co);
    if (s == LUA_YIELD) {
      return coroutine_status::suspended;
    }

    if (s == LUA_OK) {
      lua_Debug ar;
      if (lua_getstack(co, 0, &ar)) {
        return coroutine_status::normal;
      }
      if (lua_gettop(co) > 0) {
        return coroutine_status::suspended;
      }
      return coroutine_status::dead;
    }

    return coroutine_status::dead;
  }

  std::string status_string() const {
    switch (status()) {
    case coroutine_status::running:
      return "running";
    case coroutine_status::suspended:
      return "suspended";
    case coroutine_status::normal:
      return "normal";
    case coroutine_status::dead:
      return "dead";
    }
    return "unknown";
  }

  bool is_yieldable() const {
    if (!valid()) {
      throw error("Invalid coroutine");
    }
    return lua_isyieldable(co) != 0;
  }

  bool is_dead() const { return status() == coroutine_status::dead; }

  bool is_suspended() const { return status() == coroutine_status::suspended; }

  bool is_running() const { return status() == coroutine_status::running; }

  template <typename... Args> bool resume(Args &&...args) {
    if (!valid()) {
      throw error("Invalid coroutine");
    }

    if (is_dead()) {
      throw error("Cannot resume dead coroutine");
    }

    int s = lua_status(co);
    if (s != LUA_OK && s != LUA_YIELD) {
      throw error("Cannot resume coroutine with error status");
    }

    constexpr int nargs = sizeof...(Args);
    if constexpr (nargs > 0) {
      if (!lua_checkstack(co, nargs)) {
        throw error("Cannot resume: too many arguments");
      }
      (stack::push(co, std::forward<Args>(args)), ...);
    }

    int nresults = 0;
    s = lua_resume(co, L, nargs, &nresults);

    if (s != LUA_OK && s != LUA_YIELD) {
      if (lua_gettop(co) > 0) {
        std::string err = lua_tostring(co, -1);
        lua_pop(co, 1);
        throw error("Coroutine error: " + err);
      }
      throw error("Coroutine error");
    }

    if (s == LUA_OK) {
      lua_settop(co, 0);
    }

    return s == LUA_YIELD;
  }

  template <typename R = void, typename... Args>
  std::pair<bool, std::optional<R>> resume_with_result(Args &&...args) {
    if (!valid()) {
      throw error("Invalid coroutine");
    }

    if (is_dead()) {
      throw error("Cannot resume dead coroutine");
    }

    int s = lua_status(co);
    if (s != LUA_OK && s != LUA_YIELD) {
      throw error("Cannot resume coroutine with error status");
    }

    constexpr int nargs = sizeof...(Args);
    if constexpr (nargs > 0) {
      if (!lua_checkstack(co, nargs)) {
        throw error("Cannot resume: too many arguments");
      }
      (stack::push(co, std::forward<Args>(args)), ...);
    }

    int nresults = 0;
    s = lua_resume(co, L, nargs, &nresults);

    if (s != LUA_OK && s != LUA_YIELD) {
      if (lua_gettop(co) > 0) {
        std::string err = lua_tostring(co, -1);
        lua_pop(co, 1);
        throw error("Coroutine error: " + err);
      }
      throw error("Coroutine error");
    }

    bool yielded = (s == LUA_YIELD);

    if constexpr (std::is_void_v<R>) {
      lua_pop(co, nresults);
      return {yielded, std::nullopt};
    } else {
      if (nresults > 0) {
        R result = stack::get<R>(co, -nresults);
        lua_pop(co, nresults);
        return {yielded, result};
      }
      return {yielded, std::nullopt};
    }
  }

  template <typename... Args> static int yield(lua_State *L, Args &&...args) {
    constexpr int nresults = sizeof...(Args);
    if constexpr (nresults > 0) {
      (stack::push(L, std::forward<Args>(args)), ...);
    }
    return lua_yield(L, nresults);
  }

  bool close() {
    if (!valid()) {
      throw error("Invalid coroutine");
    }

    coroutine_status s = status();
    if (s == coroutine_status::normal) {
      throw error("Cannot close a normal coroutine");
    }

    if (s == coroutine_status::running) {
      lua_geti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
      lua_State *main = lua_tothread(L, -1);
      lua_pop(L, 1);
      if (main == co) {
        throw error("Cannot close main thread");
      }
      lua_closethread(co, L);
    }

    if (s == coroutine_status::dead || s == coroutine_status::suspended) {
      int result = lua_closethread(co, L);
      return result == LUA_OK;
    }

    return true;
  }
};

namespace detail {
template <typename Func> struct coroutine_wrapper {
  Func func;
  lua_State *main_L;

  coroutine_wrapper(Func &&f, lua_State *L) : func(std::forward<Func>(f)), main_L(L) {}

  static int call(lua_State *co) {
    coroutine_wrapper *self = static_cast<coroutine_wrapper *>(lua_touserdata(co, lua_upvalueindex(1)));
    self->func(co);
    return 0;
  }
};
} // namespace detail

template <typename Allocator = std::allocator<void>>
class basic_state_with_coroutine : public basic_state<Allocator> {
public:
  using basic_state<Allocator>::basic_state;

  template <typename Func> coroutine create_coroutine(Func &&func) {
    lua_State *L = this->lua_state();

    lua_State *co = lua_newthread(L);
    if (!co) {
      throw error("Failed to create coroutine thread");
    }

    if constexpr (std::is_invocable_v<Func, lua_State *>) {
      auto *wrapper = new detail::coroutine_wrapper<Func>(std::forward<Func>(func), L);
      lua_pushlightuserdata(L, wrapper);
      lua_pushcclosure(L, detail::coroutine_wrapper<Func>::call, 1);
    } else if constexpr (std::is_invocable_v<Func>) {
      auto wrapper = [f = std::forward<Func>(func)](lua_State *) { f(); };
      auto *w = new detail::coroutine_wrapper<decltype(wrapper)>(std::move(wrapper), L);
      lua_pushlightuserdata(L, w);
      lua_pushcclosure(L, detail::coroutine_wrapper<decltype(wrapper)>::call, 1);
    } else {
      lua_pop(L, 1);
      throw error("Invalid coroutine function signature");
    }

    lua_xmove(L, co, 1);

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    return coroutine(L, co, ref);
  }

  coroutine create_coroutine_from_function(const char *name) {
    lua_State *L = this->lua_state();

    lua_getglobal(L, name);
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      throw error(std::string("'") + name + "' is not a function");
    }

    lua_State *co = lua_newthread(L);
    if (!co) {
      lua_pop(L, 1);
      throw error("Failed to create coroutine thread");
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_xmove(L, co, 1);

    return coroutine(L, co, ref);
  }

  coroutine get_coroutine_from_script(const char *code) {
    lua_State *L = this->lua_state();

    this->do_string(code);

    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      throw error("Script did not return a function for coroutine");
    }

    lua_State *co = lua_newthread(L);
    if (!co) {
      lua_pop(L, 1);
      throw error("Failed to create coroutine thread");
    }

    int ref = luaL_ref(L, LUA_REGISTRYINDEX);

    lua_xmove(L, co, 1);

    return coroutine(L, co, ref);
  }

  std::pair<coroutine, bool> get_running_coroutine() {
    lua_State *L = this->lua_state();
    int is_main = lua_pushthread(L);
    if (is_main) {
      lua_pop(L, 1);
      return {coroutine(), false};
    }

    lua_State *co = lua_tothread(L, -1);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return {coroutine(L, co, ref), true};
  }
};

using state_with_coroutine = basic_state_with_coroutine<>;

template <> struct getter<coroutine> {
  static coroutine get(lua_State *L, int index) {
    if (!lua_isthread(L, index)) {
      throw error("Expected thread at index " + std::to_string(index));
    }
    lua_State *co = lua_tothread(L, index);
    lua_pushvalue(L, index);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return coroutine(L, co, ref);
  }
};

template <> struct pusher<coroutine> {
  static void push(lua_State *L, const coroutine &co) {
    if (co.valid()) {
      lua_getref(L, co.registry_index());
    } else {
      lua_pushnil(L);
    }
  }
};

} // namespace sptxx
