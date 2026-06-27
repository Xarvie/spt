// containers.hpp - STL 容器与 Lua table/array 的双向转换
// - std::vector<T>      ↔ SPT LUA_TARRAY（0-based 数组）
// - std::array<T, N>    → LUA_TARRAY（仅 push；get 需显式 N）
// - std::map<K,V>       ↔ 普通 Lua table（hash）
// - std::unordered_map  ↔ 普通 Lua table
//
// 用法：
//   lua.set_function("sum_vec", [](std::vector<int> v) { ... });
//   lua["data"] = std::vector<int>{1,2,3};
//   auto m = lua["cfg"].get<std::map<std::string,int>>();

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "stack.hpp"
#include <array>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

namespace sptxx {

namespace detail {
// 同时接受 SPT 数组（LUA_TARRAY）和普通 table（LUA_TTABLE）。
inline bool is_table_or_array(lua_State *L, int index) {
  int t = lua_type(L, index);
  return t == LUA_TTABLE || t == LUA_TARRAY;
}
} // namespace detail

// ---- std::vector<T> ----

template <typename T> struct pusher<std::vector<T>> {
  static void push(lua_State *L, const std::vector<T> &v) {
    lua_createarray(L, static_cast<int>(v.size()));
    if (!v.empty())
      lua_arraysetlen(L, -1, static_cast<lua_Integer>(v.size()));
    for (std::size_t i = 0; i < v.size(); ++i) {
      stack::push(L, v[i]);
      lua_seti(L, -2, static_cast<lua_Integer>(i)); // SPT 数组 0-based
    }
  }
};

template <typename T> struct getter<std::vector<T>> {
  static std::vector<T> get(lua_State *L, int index) {
    if (!detail::is_table_or_array(L, index))
      luaL_error(L, "expected array/table for std::vector conversion");
    std::vector<T> result;
    // 优先用 SPT 数组长度；若为普通 table 退化为 lua_len
    lua_Integer len = 0;
    if (lua_gettablemode(L, index) == 1) {
      len = lua_arraylen(L, index);
    } else {
      lua_len(L, index);
      len = lua_tointeger(L, -1);
      lua_pop(L, 1);
    }
    result.reserve(static_cast<std::size_t>(len > 0 ? len : 0));
    int t = lua_absindex(L, index);
    for (lua_Integer i = 0; i < len; ++i) {
      lua_geti(L, t, i);
      result.push_back(stack::get<T>(L, -1));
      lua_pop(L, 1);
    }
    return result;
  }
};

// ---- std::array<T, N>（仅 push）----

template <typename T, std::size_t N> struct pusher<std::array<T, N>> {
  static void push(lua_State *L, const std::array<T, N> &a) {
    lua_createarray(L, static_cast<int>(N));
    if (N > 0)
      lua_arraysetlen(L, -1, static_cast<lua_Integer>(N));
    for (std::size_t i = 0; i < N; ++i) {
      stack::push(L, a[i]);
      lua_seti(L, -2, static_cast<lua_Integer>(i));
    }
  }
};

template <typename T, std::size_t N> struct getter<std::array<T, N>> {
  static std::array<T, N> get(lua_State *L, int index) {
    if (!detail::is_table_or_array(L, index))
      luaL_error(L, "expected array/table for std::array conversion");
    std::array<T, N> result{};
    int t = lua_absindex(L, index);
    for (std::size_t i = 0; i < N; ++i) {
      lua_geti(L, t, static_cast<lua_Integer>(i));
      result[i] = stack::get<T>(L, -1);
      lua_pop(L, 1);
    }
    return result;
  }
};

// ---- std::map<K, V> ----

template <typename K, typename V> struct pusher<std::map<K, V>> {
  static void push(lua_State *L, const std::map<K, V> &m) {
    lua_createtable(L, 0, static_cast<int>(m.size()));
    for (const auto &kv : m) {
      stack::push(L, kv.first);
      stack::push(L, kv.second);
      lua_settable(L, -3);
    }
  }
};

template <typename K, typename V> struct getter<std::map<K, V>> {
  static std::map<K, V> get(lua_State *L, int index) {
    if (!detail::is_table_or_array(L, index))
      luaL_error(L, "expected table for std::map conversion");
    std::map<K, V> result;
    int t = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, t) != 0) {
      K k = stack::get<K>(L, -2);
      V v = stack::get<V>(L, -1);
      result.emplace(std::move(k), std::move(v));
      lua_pop(L, 1); // pop value，保留 key 供下次迭代
    }
    return result;
  }
};

// ---- std::unordered_map<K, V> ----

template <typename K, typename V> struct pusher<std::unordered_map<K, V>> {
  static void push(lua_State *L, const std::unordered_map<K, V> &m) {
    lua_createtable(L, 0, static_cast<int>(m.size()));
    for (const auto &kv : m) {
      stack::push(L, kv.first);
      stack::push(L, kv.second);
      lua_settable(L, -3);
    }
  }
};

template <typename K, typename V> struct getter<std::unordered_map<K, V>> {
  static std::unordered_map<K, V> get(lua_State *L, int index) {
    if (!detail::is_table_or_array(L, index))
      luaL_error(L, "expected table for std::unordered_map conversion");
    std::unordered_map<K, V> result;
    int t = lua_absindex(L, index);
    lua_pushnil(L);
    while (lua_next(L, t) != 0) {
      K k = stack::get<K>(L, -2);
      V v = stack::get<V>(L, -1);
      result.emplace(std::move(k), std::move(v));
      lua_pop(L, 1);
    }
    return result;
  }
};

} // namespace sptxx
