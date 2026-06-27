// variant.hpp - std::variant<Ts...> 与 Lua 栈的双向转换
//
// push: 根据 variant 当前持有的 alternative，调用对应 pusher<T>::push；
//       valueless_by_exception 时 push nil。
// get:  按 variant 模板参数声明顺序逐个尝试，找到第一个与栈值兼容的类型，
//       调用 getter<T>::get 构造 variant；全部不匹配则 luaL_error。
//
// 兼容性判定使用 type_matcher<T>（不会 longjmp）：
// - bool           → LUA_TBOOLEAN
// - 整数           → lua_isinteger
// - 浮点           → lua_isnumber
// - std::string    → LUA_TSTRING（严格，不含数字）
// - std::optional  → nil 或 inner 匹配
// - std::vector    → LUA_TARRAY
// - std::map       → LUA_TTABLE
// - 自定义类型     → 默认放宽（return true），由 getter<T>::get 自行报错
//
// 注意：variant<T1, T2> 中若 T1 的 type_matcher 放宽（return true），
// 则 T1 总是被选中。如需精确区分自定义类型，请特化 type_matcher<T>。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "stack.hpp"
#include <array>
#include <map>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace sptxx {

// ---- type_matcher<T>：判断栈值是否兼容 T（不会 longjmp）----
// 用户可为自定义类型特化以提供精确匹配（如检查 metatable __name）。
template <typename T, typename = void> struct type_matcher {
  static bool matches(lua_State *L, int index) {
    (void)L;
    (void)index;
    return true; // 未知类型放宽
  }
};

template <> struct type_matcher<bool> {
  static bool matches(lua_State *L, int index) {
    return lua_isboolean(L, index) != 0;
  }
};

// std::string 严格匹配 LUA_TSTRING（不接受数字隐式转换）
template <> struct type_matcher<std::string> {
  static bool matches(lua_State *L, int index) {
    return lua_type(L, index) == LUA_TSTRING;
  }
};

template <> struct type_matcher<std::string_view> {
  static bool matches(lua_State *L, int index) {
    return lua_type(L, index) == LUA_TSTRING;
  }
};

template <> struct type_matcher<const char *> {
  static bool matches(lua_State *L, int index) {
    return lua_type(L, index) == LUA_TSTRING;
  }
};

// 整数（排除 bool，bool 已有显式特化）
template <typename T>
struct type_matcher<
    T, std::enable_if_t<std::is_integral_v<T> && !std::is_same_v<T, bool>>> {
  static bool matches(lua_State *L, int index) {
    return lua_isinteger(L, index) != 0;
  }
};

// 浮点
template <typename T>
struct type_matcher<T, std::enable_if_t<std::is_floating_point_v<T>>> {
  static bool matches(lua_State *L, int index) {
    return lua_isnumber(L, index) != 0;
  }
};

// std::optional<T>：nil 或 inner 匹配
template <typename T> struct type_matcher<std::optional<T>> {
  static bool matches(lua_State *L, int index) {
    return lua_isnil(L, index) || type_matcher<T>::matches(L, index);
  }
};

// STL 容器：vector/array 匹配 LUA_TARRAY；map/unordered_map 匹配 LUA_TTABLE
template <typename T> struct type_matcher<std::vector<T>> {
  static bool matches(lua_State *L, int index) {
    return lua_isarray(L, index) != 0;
  }
};

template <typename T, std::size_t N>
struct type_matcher<std::array<T, N>> {
  static bool matches(lua_State *L, int index) {
    return lua_isarray(L, index) != 0;
  }
};

template <typename K, typename V> struct type_matcher<std::map<K, V>> {
  static bool matches(lua_State *L, int index) {
    return lua_istable(L, index) != 0;
  }
};

template <typename K, typename V>
struct type_matcher<std::unordered_map<K, V>> {
  static bool matches(lua_State *L, int index) {
    return lua_istable(L, index) != 0;
  }
};

// ---- pusher<std::variant<Ts...>> ----
template <typename... Ts> struct pusher<std::variant<Ts...>> {
  static void push(lua_State *L, const std::variant<Ts...> &v) {
    if (v.valueless_by_exception()) {
      lua_pushnil(L);
      return;
    }
    std::visit([L](const auto &val) { stack::push(L, val); }, v);
  }
};

// ---- getter<std::variant<Ts...>>：按顺序匹配第一个兼容 alternative ----
namespace detail {
template <typename Variant, std::size_t... Is>
inline bool variant_get_impl(lua_State *L, int index, Variant &out,
                             std::index_sequence<Is...>) {
  bool done = false;
  auto try_one = [&](auto I) {
    constexpr std::size_t i = decltype(I)::value;
    using T = std::variant_alternative_t<i, Variant>;
    if (!done && type_matcher<T>::matches(L, index)) {
      out = Variant(stack::get<T>(L, index));
      done = true;
    }
  };
  (try_one(std::integral_constant<std::size_t, Is>{}), ...);
  return done;
}
} // namespace detail

template <typename... Ts> struct getter<std::variant<Ts...>> {
  static std::variant<Ts...> get(lua_State *L, int index) {
    using V = std::variant<Ts...>;
    V result;
    if (detail::variant_get_impl(L, index, result, std::index_sequence_for<Ts...>{}))
      return result;
    luaL_error(L, "no matching variant alternative for stack value at index %d", index);
    return result; // 不可达（luaL_error longjmp）
  }
};

} // namespace sptxx
