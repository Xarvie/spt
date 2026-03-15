// function.hpp - Function binding system for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "stack.hpp"
#include "error.hpp"
#include <tuple>
#include <utility>
#include <type_traits>

namespace sptxx {

namespace detail {

// Helper to extract arguments from Lua stack
template <typename T> inline T extract_arg(lua_State *L, int index) {
  return stack::get<T>(L, index);
}

// 支持动态起点 start_idx
template <typename Tuple, std::size_t... I>
inline Tuple extract_args_impl(lua_State *L, int start_idx, std::index_sequence<I...>) {
  return Tuple(extract_arg<std::tuple_element_t<I, Tuple>>(L, start_idx + static_cast<int>(I))...);
}

// 使用 std::decay_t 强制按值拷贝，防止 Lua 栈字符串被 pop 后产生悬垂引用崩溃
template <typename... Args>
inline std::tuple<std::decay_t<Args>...> extract_args(lua_State *L, int start_idx) {
  return extract_args_impl<std::tuple<std::decay_t<Args>...>>(L, start_idx,
                                                              std::index_sequence_for<Args...>{});
}

// Helper to push return values
template <typename T> inline void push_return(lua_State *L, T &&value) {
  stack::push(L, std::forward<T>(value));
}

inline void push_return_void(lua_State *L) {
  // Nothing to push for void return
}

// Main function call handler
template <typename Func, typename... Args>
inline auto call_function(Func &&f, lua_State *L) -> decltype(f(std::declval<Args>()...)) {
  // 全局普通函数没有隐式 receiver 时从 1 开始
  auto args = extract_args<Args...>(L, 1);
  return std::apply(std::forward<Func>(f), std::move(args));
}

// Function wrapper generator
template <typename Func> lua_CFunction make_function_wrapper(Func &&f) {
  static std::decay_t<Func> stored_func = std::forward<Func>(f);

  return [](lua_State *L) -> int {
    try {
      int arg1 = luaL_checkinteger(L, 2);
      int arg2 = luaL_checkinteger(L, 3);
      int result = stored_func(arg1, arg2);
      lua_pushinteger(L, result);
      return 1;
    } catch (...) {
      return propagate_exception(L);
    }
  };
}

} // namespace detail

} // namespace sptxx