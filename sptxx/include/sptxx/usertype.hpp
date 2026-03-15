// usertype.hpp - Class binding system for SPT Lua 5.5 C++ bindings
// Fully supports variadic templates for constructors and methods

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "error.hpp"
#include "function.hpp"
#include "stack.hpp"
#include <cstring>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>

namespace sptxx {

template<typename T>
class usertype {
private:
  lua_State *L;
  std::string type_name;

public:
  usertype(usertype &&other) noexcept : L(other.L), type_name(std::move(other.type_name)) {}

  usertype &operator=(usertype &&other) noexcept {
    if (this != &other) {
      L = other.L;
      type_name = std::move(other.type_name);
    }
    return *this;
  }

  usertype(const usertype &) = delete;
  usertype &operator=(const usertype &) = delete;

  usertype(lua_State *state, const char *name) : L(state), type_name(name) {
    luaL_newmetatable(L, name);

    lua_pushstring(L, name);
    lua_setfield(L, -2, "__name");

    lua_pushcfunction(L, &gc_method);
    lua_setfield(L, -2, "__gc");

    lua_pushcfunction(L, &index_handler);
    lua_setfield(L, -2, "__index");

    lua_pushcfunction(L, &newindex_handler);
    lua_setfield(L, -2, "__newindex");

    lua_newtable(L);
    lua_setfield(L, -2, "__getters");

    lua_newtable(L);
    lua_setfield(L, -2, "__setters");

    lua_newtable(L);
    lua_setfield(L, -2, "__methods");

    lua_pop(L, 1);
  }

  ~usertype() = default;

  static int gc_method(lua_State *L) {
    T **ptr = static_cast<T **>(lua_touserdata(L, 1));
    if (ptr && *ptr) {
      delete *ptr;
      *ptr = nullptr;
    }
    return 0;
  }

  static int index_handler(lua_State *L) {
    const char *key = lua_tostring(L, 2);
    if (!key)
      return 0;

    lua_getmetatable(L, 1);

    lua_getfield(L, -1, "__getters");
    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
      lua_pushvalue(L, 1);
      lua_call(L, 1, 1);
      return 1;
    }
    lua_pop(L, 2);

    lua_getfield(L, -1, "__methods");
    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
      return 1;
    }
    lua_pop(L, 2);

    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
      return 1;
    }
    return 0;
  }

  static int newindex_handler(lua_State *L) {
    const char *key = lua_tostring(L, 2);
    if (!key)
      return luaL_error(L, "field name must be a string");

    lua_getmetatable(L, 1);
    lua_getfield(L, -1, "__setters");
    lua_getfield(L, -1, key);
    if (!lua_isnil(L, -1)) {
      lua_pushvalue(L, 1);
      lua_pushvalue(L, 3);
      lua_call(L, 2, 0);
      return 0;
    }
    return luaL_error(L, "cannot set field '%s'", key);
  }

  template <typename U> void set(const char *name, U T::*member) {
    luaL_getmetatable(L, type_name.c_str());

    lua_getfield(L, -1, "__getters");
    {
      void *storage = lua_newuserdatauv(L, sizeof(U T::*), 0);
      std::memcpy(storage, &member, sizeof(U T::*));
      lua_pushcclosure(L, &member_getter<U>, 1);
      lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);

    lua_getfield(L, -1, "__setters");
    {
      void *storage = lua_newuserdatauv(L, sizeof(U T::*), 0);
      std::memcpy(storage, &member, sizeof(U T::*));
      lua_pushcclosure(L, &member_setter<U>, 1);
      lua_setfield(L, -2, name);
    }
    lua_pop(L, 1);
    lua_pop(L, 1);
  }

  template <typename R, typename... Args> void set(const char *name, R (T::*func)(Args...)) {
    using FuncType = R (T::*)(Args...);
    register_method_closure<FuncType, R, Args...>(name, func);
  }

  template <typename R, typename... Args> void set(const char *name, R (T::*func)(Args...) const) {
    using FuncType = R (T::*)(Args...) const;
    register_method_closure<FuncType, R, Args...>(name, func);
  }

  template <typename... Args> void constructor() {
    auto ctor_wrapper = [](lua_State *L) -> int {
      const char *name = lua_tostring(L, lua_upvalueindex(1));
      try {
        void *obj = lua_newuserdatauv(L, sizeof(T *), 0);
        luaL_getmetatable(L, name);
        lua_setmetatable(L, -2);
        T **ptr = static_cast<T **>(obj);

        // 防御性初始化，防止异常抛出时 GC 回收野指针崩溃
        *ptr = nullptr;

        // 动态适配 SPT Lua 的隐式 Receiver
        int start_idx = 2; // 默认标准 Lua
        if (lua_gettop(L) >= 2 && lua_isnil(L, 2)) {
          start_idx = 3; // 跳过 VM 自动塞入的 nil receiver
        }

        auto args = detail::extract_args<Args...>(L, start_idx);
        *ptr = std::apply(
            [](auto &&...unpacked) { return new T(std::forward<decltype(unpacked)>(unpacked)...); },
            std::move(args));

        return 1;
      } catch (...) {
        return detail::propagate_exception(L);
      }
    };

    if (lua_getglobal(L, type_name.c_str()) != LUA_TTABLE) {
      lua_pop(L, 1);
      lua_newtable(L);
      lua_pushvalue(L, -1);
      lua_setglobal(L, type_name.c_str());
    }

    if (!lua_getmetatable(L, -1)) {
      lua_newtable(L);
    }

    lua_pushstring(L, type_name.c_str());
    lua_pushcclosure(L, ctor_wrapper, 1);
    lua_setfield(L, -2, "__call");

    lua_setmetatable(L, -2);
    lua_pop(L, 1);
  }

private:
  template <typename U> static int member_getter(lua_State *L) {
    T **ptr = static_cast<T **>(lua_touserdata(L, 1));
    if (!ptr || !*ptr)
      return luaL_error(L, "null object in getter");

    U T::*member;
    std::memcpy(&member, lua_touserdata(L, lua_upvalueindex(1)), sizeof(member));

    stack::push(L, (*ptr)->*member);
    return 1;
  }

  template <typename U> static int member_setter(lua_State *L) {
    T **ptr = static_cast<T **>(lua_touserdata(L, 1));
    if (!ptr || !*ptr)
      return luaL_error(L, "null object in setter");

    U T::*member;
    std::memcpy(&member, lua_touserdata(L, lua_upvalueindex(1)), sizeof(member));

    (*ptr)->*member = stack::get<std::decay_t<U>>(L, 2);
    return 0;
  }

  template <typename FuncType>
  void store_method_upvalue(const char *name, FuncType func, lua_CFunction wrapper) {
    luaL_getmetatable(L, type_name.c_str());
    lua_getfield(L, -1, "__methods");

    void *storage = lua_newuserdatauv(L, sizeof(FuncType), 0);
    std::memcpy(storage, &func, sizeof(FuncType));
    lua_pushcclosure(L, wrapper, 1);
    lua_setfield(L, -2, name);

    lua_pop(L, 2);
  }

  template <typename FuncType, typename R, typename... Args>
  void register_method_closure(const char *name, FuncType func) {
    store_method_upvalue(name, func, [](lua_State *L) -> int {
      T **ptr = static_cast<T **>(lua_touserdata(L, 1));
      if (!ptr || !*ptr)
        return luaL_error(L, "null object in method call");

      FuncType f;
      std::memcpy(&f, lua_touserdata(L, lua_upvalueindex(1)), sizeof(f));

      try {
        auto args = detail::extract_args<Args...>(L, 2);

        if constexpr (std::is_void_v<R>) {
          std::apply(
              [ptr, f](auto &&...unpacked) {
                ((*ptr)->*f)(std::forward<decltype(unpacked)>(unpacked)...);
              },
              std::move(args));
          return 0;
        } else {
          R result = std::apply(
              [ptr, f](auto &&...unpacked) {
                return ((*ptr)->*f)(std::forward<decltype(unpacked)>(unpacked)...);
              },
              std::move(args));
          stack::push(L, std::move(result));
          return 1;
        }
      } catch (...) {
        return detail::propagate_exception(L);
      }
    });
  }
};

} // namespace sptxx