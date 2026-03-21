// function.hpp - Function binding system for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "error.hpp"
#include "stack.hpp"
#include <array>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sptxx {

namespace detail {

template <typename T> inline T extract_arg(lua_State *L, int index) {
  return stack::get<T>(L, index);
}

template <typename Tuple, std::size_t... I>
inline Tuple extract_args_impl(lua_State *L, int start_idx, std::index_sequence<I...>) {
  using T = std::tuple<std::decay_t<std::tuple_element_t<I, Tuple>>...>;
  T result{};
  int idx = start_idx;
  ((std::get<I>(result) = extract_arg<std::tuple_element_t<I, T>>(L, idx++)), ...);
  return result;
}

template <typename... Args>
inline std::tuple<std::decay_t<Args>...> extract_args_from_2(lua_State *L) {
  return extract_args_impl<std::tuple<std::decay_t<Args>...>>(L, 2,
                                                              std::index_sequence_for<Args...>{});
}

template <typename... Args>
inline std::tuple<std::decay_t<Args>...> extract_args_from_3(lua_State *L) {
  return extract_args_impl<std::tuple<std::decay_t<Args>...>>(L, 3,
                                                              std::index_sequence_for<Args...>{});
}

template <typename T> inline void push_return(lua_State *L, T &&value) {
  stack::push(L, std::forward<T>(value));
}

inline void push_return(lua_State *, std::nullptr_t) {}

template <typename R, typename Func, typename ArgsTuple> struct function_caller;

template <typename R, typename Func, typename... Args>
struct function_caller<R, Func, std::tuple<Args...>> {
  static int call(lua_State *L, Func &f) {
    auto args = extract_args_from_2<Args...>(L);
    if constexpr (std::is_void_v<R>) {
      std::apply(f, std::move(args));
      return 0;
    } else {
      R result = std::apply(f, std::move(args));
      push_return(L, std::move(result));
      return 1;
    }
  }
};

template <typename Func, typename... Args> struct function_caller<void, Func, std::tuple<Args...>> {
  static int call(lua_State *L, Func &f) {
    auto args = extract_args_from_2<Args...>(L);
    std::apply(f, std::move(args));
    return 0;
  }
};

template <typename... Rs, typename Func, typename... Args>
struct function_caller<std::tuple<Rs...>, Func, std::tuple<Args...>> {
  static int call(lua_State *L, Func &f) {
    auto args = extract_args_from_2<Args...>(L);
    auto result = std::apply(f, std::move(args));
    push_multi_return<Rs...>(L, std::move(result));
    return sizeof...(Rs);
  }

private:
  template <typename... Ts>
  static void push_multi_return(lua_State *L, std::tuple<Ts...> &&values) {
    push_multi_return_impl(L, std::move(values), std::index_sequence_for<Ts...>{});
  }

  template <typename... Ts, std::size_t... Is>
  static void push_multi_return_impl(lua_State *L, std::tuple<Ts...> &&values,
                                     std::index_sequence<Is...>) {
    (stack::push(L, std::move(std::get<Is>(values))), ...);
  }
};

template <typename Func> struct function_traits;

template <typename R, typename... Args> struct function_traits<R(Args...)> {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
  static constexpr std::size_t arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) noexcept> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const noexcept> : function_traits<R(Args...)> {};

template <typename T> struct function_traits : function_traits<decltype(&T::operator())> {};

template <typename T> struct function_traits<std::function<T>> : function_traits<T> {};

template <typename T> using function_traits_t = function_traits<std::decay_t<T>>;

template <typename Func> void push_function_wrapper(lua_State *L, Func &&f) {
  using FuncType = std::decay_t<Func>;
  using Traits = function_traits<FuncType>;
  using ReturnType = typename Traits::return_type;
  using ArgsTuple = typename Traits::args_tuple;

  void *storage = lua_newuserdatauv(L, sizeof(FuncType), 0);
  new (storage) FuncType(std::forward<Func>(f));
  
  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, [](lua_State *L) -> int {
    FuncType *self = static_cast<FuncType *>(lua_touserdata(L, 1));
    self->~FuncType();
    return 0;
  });
  lua_setfield(L, -2, "__gc");
  lua_setmetatable(L, -2);
  
  lua_pushcclosure(L, [](lua_State *L) -> int {
    FuncType *func = static_cast<FuncType *>(lua_touserdata(L, lua_upvalueindex(1)));
    try {
      return function_caller<ReturnType, FuncType, ArgsTuple>::call(L, *func);
    } catch (...) {
      return propagate_exception(L);
    }
  }, 1);
}

template <typename Func> lua_CFunction make_function_wrapper(Func &&f) {
  using FuncType = std::decay_t<Func>;
  using Traits = function_traits<FuncType>;
  using ReturnType = typename Traits::return_type;
  using ArgsTuple = typename Traits::args_tuple;

  static FuncType stored_func = std::forward<Func>(f);

  return [](lua_State *L) -> int {
    try {
      return function_caller<ReturnType, FuncType, ArgsTuple>::call(L, stored_func);
    } catch (...) {
      return propagate_exception(L);
    }
  };
}

template <typename R, typename... Args> lua_CFunction make_function_wrapper(R (*f)(Args...)) {
  using FuncPtr = R (*)(Args...);
  using ArgsTuple = std::tuple<Args...>;

  static FuncPtr stored_func = f;

  return [](lua_State *L) -> int {
    try {
      return function_caller<R, FuncPtr, ArgsTuple>::call(L, stored_func);
    } catch (...) {
      return propagate_exception(L);
    }
  };
}

template <typename R, typename... Args>
inline std::tuple<R, Args...> extract_args_for_call(lua_State *L, int start_idx) {
  return extract_args_impl<std::tuple<R, Args...>>(L, start_idx,
                                                   std::index_sequence_for<R, Args...>{});
}

template <typename... Args> inline void push_args(lua_State *L, Args &&...args) {
  (stack::push(L, std::forward<Args>(args)), ...);
}

template <typename T> struct is_tuple : std::false_type {};

template <typename... Ts> struct is_tuple<std::tuple<Ts...>> : std::true_type {};

template <typename T> inline constexpr bool is_tuple_v = is_tuple<T>::value;

template <typename Tuple, std::size_t... Is>
inline Tuple extract_multi_return_impl(lua_State *L, int base_idx, std::index_sequence<Is...>) {
  return Tuple{stack::get<std::tuple_element_t<Is, Tuple>>(L, base_idx + static_cast<int>(Is))...};
}

template <typename Tuple> inline Tuple extract_multi_return(lua_State *L, int base_idx) {
  return extract_multi_return_impl<Tuple>(L, base_idx,
                                          std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

} // namespace detail

template <typename Signature> class function_ref;

template <typename R, typename... Args> class function_ref<R(Args...)> {
private:
  lua_State *L;
  int ref;

public:
  function_ref() : L(nullptr), ref(LUA_NOREF) {}

  function_ref(lua_State *state, int reference) : L(state), ref(reference) {}

  function_ref(const function_ref &other) : L(other.L), ref(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L, other.ref);
      ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  }

  function_ref(function_ref &&other) noexcept : L(other.L), ref(other.ref) {
    other.L = nullptr;
    other.ref = LUA_NOREF;
  }

  function_ref &operator=(const function_ref &other) {
    if (this != &other) {
      if (valid()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
      }
      L = other.L;
      if (other.valid()) {
        lua_getref(L, other.ref);
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
      } else {
        ref = LUA_NOREF;
      }
    }
    return *this;
  }

  function_ref &operator=(function_ref &&other) noexcept {
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

  ~function_ref() {
    if (valid()) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }

  bool valid() const { return L != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }

  explicit operator bool() const { return valid(); }

  R operator()(Args... args) const {
    if (!valid()) {
      throw error("Invalid function reference");
    }

    lua_getref(L, ref);

    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1);
      throw error("Reference is not a function");
    }

    lua_pushnil(L);

    detail::push_args<Args...>(L, std::forward<Args>(args)...);

    int nargs = sizeof...(Args) + 1;
    int nresults = 1;

    if constexpr (std::is_void_v<R>) {
      nresults = 0;
    } else if constexpr (detail::is_tuple_v<R>) {
      nresults = std::tuple_size_v<R>;
    }

    int result = lua_pcall(L, nargs, nresults, 0);
    if (result != LUA_OK) {
      std::string err = lua_tostring(L, -1);
      lua_pop(L, 1);
      throw error("Lua error: " + err);
    }

    if constexpr (std::is_void_v<R>) {
      return;
    } else if constexpr (detail::is_tuple_v<R>) {
      auto ret = detail::extract_multi_return<R>(L, -static_cast<int>(nresults));
      lua_pop(L, static_cast<int>(nresults));
      return ret;
    } else {
      R ret = stack::get<R>(L, -1);
      lua_pop(L, 1);
      return ret;
    }
  }

  lua_State *lua_state() const { return L; }

  int registry_index() const { return ref; }
};

template <typename R, typename... Args> struct getter<function_ref<R(Args...)>> {
  static function_ref<R(Args...)> get(lua_State *L, int index) {
    if (lua_isnil(L, index)) {
      return function_ref<R(Args...)>();
    }
    if (!lua_isfunction(L, index)) {
      luaL_error(L, "Expected function at index %d", index);
    }
    lua_pushvalue(L, index);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return function_ref<R(Args...)>(L, ref);
  }
};

template <typename R, typename... Args> struct pusher<function_ref<R(Args...)>> {
  static void push(lua_State *L, const function_ref<R(Args...)> &value) {
    if (value.valid()) {
      lua_getref(L, value.registry_index());
    } else {
      lua_pushnil(L);
    }
  }
};

} // namespace sptxx
