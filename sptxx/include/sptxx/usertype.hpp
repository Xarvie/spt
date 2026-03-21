// usertype.hpp - Class binding system for SPT Lua 5.5 C++ bindings
// Fully supports variadic templates for constructors and methods
// Supports operator overloading via Lua metamethods

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "error.hpp"
#include "function.hpp"
#include "stack.hpp"
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>

namespace sptxx {

enum class operator_type {
  add,        // __add: +
  sub,        // __sub: -
  mul,        // __mul: *
  div,        // __div: /
  mod,        // __mod: %
  pow,        // __pow: ^
  unm,        // __unm: - (unary)
  idiv,       // __idiv: //
  band,       // __band: &
  bor,        // __bor: |
  bxor,       // __bxor: ~
  bnot,       // __bnot: ~ (unary)
  shl,        // __shl: <<
  shr,        // __shr: >>
  concat,     // __concat: ..
  len,        // __len: #
  eq,         // __eq: ==
  lt,         // __lt: <
  le          // __le: <=
};

namespace detail {
inline const char* operator_metafield(operator_type op) {
  switch (op) {
    case operator_type::add:    return "__add";
    case operator_type::sub:    return "__sub";
    case operator_type::mul:    return "__mul";
    case operator_type::div:    return "__div";
    case operator_type::mod:    return "__mod";
    case operator_type::pow:    return "__pow";
    case operator_type::unm:    return "__unm";
    case operator_type::idiv:   return "__idiv";
    case operator_type::band:   return "__band";
    case operator_type::bor:    return "__bor";
    case operator_type::bxor:   return "__bxor";
    case operator_type::bnot:   return "__bnot";
    case operator_type::shl:    return "__shl";
    case operator_type::shr:    return "__shr";
    case operator_type::concat: return "__concat";
    case operator_type::len:    return "__len";
    case operator_type::eq:     return "__eq";
    case operator_type::lt:     return "__lt";
    case operator_type::le:     return "__le";
    default: return nullptr;
  }
}
} // namespace detail

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

        *ptr = nullptr;

        auto args = detail::extract_args_from_3<Args...>(L);
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

  template <operator_type Op, typename Func>
  void set_operator(Func &&func) {
    using FuncType = std::decay_t<Func>;
    constexpr bool is_unary = (Op == operator_type::unm || 
                               Op == operator_type::bnot || 
                               Op == operator_type::len);
    
    luaL_getmetatable(L, type_name.c_str());
    
    void *storage = lua_newuserdatauv(L, sizeof(FuncType), 0);
    new (storage) FuncType(std::forward<Func>(func));
    
    if constexpr (is_unary) {
      lua_pushcclosure(L, &unary_operator_wrapper<FuncType>, 1);
    } else {
      lua_pushcclosure(L, &binary_operator_wrapper<FuncType>, 1);
    }
    
    lua_setfield(L, -2, detail::operator_metafield(Op));
    lua_pop(L, 1);
  }

  template <typename Func> void set_add(Func &&func) { set_operator<operator_type::add>(std::forward<Func>(func)); }
  template <typename Func> void set_sub(Func &&func) { set_operator<operator_type::sub>(std::forward<Func>(func)); }
  template <typename Func> void set_mul(Func &&func) { set_operator<operator_type::mul>(std::forward<Func>(func)); }
  template <typename Func> void set_div(Func &&func) { set_operator<operator_type::div>(std::forward<Func>(func)); }
  template <typename Func> void set_mod(Func &&func) { set_operator<operator_type::mod>(std::forward<Func>(func)); }
  template <typename Func> void set_pow(Func &&func) { set_operator<operator_type::pow>(std::forward<Func>(func)); }
  template <typename Func> void set_unm(Func &&func) { set_operator<operator_type::unm>(std::forward<Func>(func)); }
  template <typename Func> void set_idiv(Func &&func) { set_operator<operator_type::idiv>(std::forward<Func>(func)); }
  template <typename Func> void set_band(Func &&func) { set_operator<operator_type::band>(std::forward<Func>(func)); }
  template <typename Func> void set_bor(Func &&func) { set_operator<operator_type::bor>(std::forward<Func>(func)); }
  template <typename Func> void set_bxor(Func &&func) { set_operator<operator_type::bxor>(std::forward<Func>(func)); }
  template <typename Func> void set_bnot(Func &&func) { set_operator<operator_type::bnot>(std::forward<Func>(func)); }
  template <typename Func> void set_shl(Func &&func) { set_operator<operator_type::shl>(std::forward<Func>(func)); }
  template <typename Func> void set_shr(Func &&func) { set_operator<operator_type::shr>(std::forward<Func>(func)); }
  template <typename Func> void set_concat(Func &&func) { set_operator<operator_type::concat>(std::forward<Func>(func)); }
  template <typename Func> void set_len(Func &&func) { set_operator<operator_type::len>(std::forward<Func>(func)); }
  template <typename Func> void set_eq(Func &&func) { set_operator<operator_type::eq>(std::forward<Func>(func)); }
  template <typename Func> void set_lt(Func &&func) { set_operator<operator_type::lt>(std::forward<Func>(func)); }
  template <typename Func> void set_le(Func &&func) { set_operator<operator_type::le>(std::forward<Func>(func)); }

  template <operator_type... Ops>
  void set_comparable() {
    (set_operator<Ops>([](const T& a, const T& b) { return a < b; }), ...);
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
        auto args = detail::extract_args_from_2<Args...>(L);

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

  template <typename FuncType>
  static int binary_operator_wrapper(lua_State *L) {
    T **ptr1 = static_cast<T **>(lua_touserdata(L, 1));
    
    if (!ptr1 || !*ptr1)
      return luaL_error(L, "null object in operator");

    FuncType *func = static_cast<FuncType *>(lua_touserdata(L, lua_upvalueindex(1)));
    
    try {
      if constexpr (std::is_invocable_v<FuncType, const T&, const T&>) {
        T **ptr2 = static_cast<T **>(lua_touserdata(L, 2));
        if (!ptr2 || !*ptr2)
          return luaL_error(L, "null object in operator");
        auto result = (*func)(**ptr1, **ptr2);
        stack::push(L, std::move(result));
        return 1;
      } else if constexpr (std::is_invocable_v<FuncType, const T&, float>) {
        float arg2 = static_cast<float>(lua_tonumber(L, 2));
        auto result = (*func)(**ptr1, arg2);
        stack::push(L, std::move(result));
        return 1;
      } else if constexpr (std::is_invocable_v<FuncType, const T&, double>) {
        double arg2 = lua_tonumber(L, 2);
        auto result = (*func)(**ptr1, arg2);
        stack::push(L, std::move(result));
        return 1;
      } else if constexpr (std::is_invocable_v<FuncType, const T&, int>) {
        int arg2 = static_cast<int>(lua_tointeger(L, 2));
        auto result = (*func)(**ptr1, arg2);
        stack::push(L, std::move(result));
        return 1;
      } else if constexpr (std::is_invocable_v<FuncType, lua_State*, const T&>) {
        return (*func)(L, **ptr1);
      } else {
        T **ptr2 = static_cast<T **>(lua_touserdata(L, 2));
        if (!ptr2 || !*ptr2)
          return luaL_error(L, "null object in operator");
        return (*func)(L, **ptr1, **ptr2);
      }
    } catch (...) {
      return detail::propagate_exception(L);
    }
  }

  template <typename FuncType>
  static int unary_operator_wrapper(lua_State *L) {
    T **ptr = static_cast<T **>(lua_touserdata(L, 1));
    
    if (!ptr || !*ptr)
      return luaL_error(L, "null object in operator");

    FuncType *func = static_cast<FuncType *>(lua_touserdata(L, lua_upvalueindex(1)));
    
    try {
      if constexpr (std::is_invocable_v<FuncType, const T&>) {
        auto result = (*func)(**ptr);
        stack::push(L, std::move(result));
        return 1;
      } else {
        auto result = (*func)(L, **ptr);
        return result;
      }
    } catch (...) {
      return detail::propagate_exception(L);
    }
  }
};

} // namespace sptxx
