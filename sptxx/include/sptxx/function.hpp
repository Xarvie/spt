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

} // namespace detail

} // namespace sptxx
