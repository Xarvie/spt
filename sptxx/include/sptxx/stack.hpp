// stack.hpp - 栈操作与类型转换
// pusher<T>/getter<T> 是核心扩展机制：用户可为自定义类型特化

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace sptxx {

// 前向声明（用户可特化）
template <typename T> struct getter;
template <typename T> struct pusher;

namespace stack {

// 从栈 index 读取一个 T。等价于 getter<T>::get(L, index)。
template <typename T> inline T get(lua_State *L, int index) {
  return getter<std::decay_t<T>>::get(L, index);
}

// 将 value 压入栈顶。等价于 pusher<T>::push(L, value)。
template <typename T> inline void push(lua_State *L, T &&value) {
  pusher<std::decay_t<T>>::push(L, std::forward<T>(value));
}

} // namespace stack

// ---- 基本类型特化 ----

// void（仅 push，push nil）
template <> struct getter<void> {
  static void get(lua_State *L, int index) {
    if (!lua_isnil(L, index))
      luaL_error(L, "expected nil at index %d", index);
  }
};

template <> struct pusher<void> {
  static void push(lua_State *L) { lua_pushnil(L); }
  static void push(lua_State *L, void *) { lua_pushnil(L); }
};

// bool
template <> struct getter<bool> {
  static bool get(lua_State *L, int index) { return lua_toboolean(L, index) != 0; }
};

template <> struct pusher<bool> {
  static void push(lua_State *L, bool value) { lua_pushboolean(L, value ? 1 : 0); }
};

// 整数类型：int / long / long long / unsigned int / lua_Integer
#define SPTXX_INT_GETTER(T)                                                    \
  template <> struct getter<T> {                                               \
    static T get(lua_State *L, int index) {                                    \
      return static_cast<T>(luaL_checkinteger(L, index));                      \
    }                                                                          \
  };                                                                           \
  template <> struct pusher<T> {                                               \
    static void push(lua_State *L, T value) {                                  \
      lua_pushinteger(L, static_cast<lua_Integer>(value));                     \
    }                                                                          \
  };

// 注意：long long 与 lua_Integer 在多数平台是同一类型（typedef），
// 不能同时显式特化，否则 C2766 重复特化错误。lua_Integer 已覆盖 long long。
SPTXX_INT_GETTER(int)
SPTXX_INT_GETTER(long)
SPTXX_INT_GETTER(unsigned int)
SPTXX_INT_GETTER(lua_Integer)

#undef SPTXX_INT_GETTER

// 窄整型：char / signed char / unsigned char / short / unsigned short /
//         unsigned long / unsigned long long
// 注意：char、signed char、unsigned char 在 C++ 是三种不同类型；
//       unsigned long 与 unsigned int 即使同宽也是不同类型，可分别特化；
//       unsigned long long 与 lua_Integer（signed）不同，无 C2766 冲突。
#define SPTXX_NARROW_INT_GETTER(T)                                              \
  template <> struct getter<T> {                                               \
    static T get(lua_State *L, int index) {                                    \
      return static_cast<T>(luaL_checkinteger(L, index));                      \
    }                                                                          \
  };                                                                           \
  template <> struct pusher<T> {                                               \
    static void push(lua_State *L, T value) {                                  \
      lua_pushinteger(L, static_cast<lua_Integer>(value));                     \
    }                                                                          \
  };

SPTXX_NARROW_INT_GETTER(char)
SPTXX_NARROW_INT_GETTER(signed char)
SPTXX_NARROW_INT_GETTER(unsigned char)
SPTXX_NARROW_INT_GETTER(short)
SPTXX_NARROW_INT_GETTER(unsigned short)
SPTXX_NARROW_INT_GETTER(unsigned long)
SPTXX_NARROW_INT_GETTER(unsigned long long)

#undef SPTXX_NARROW_INT_GETTER

// std::string_view：push 拷贝字符串到 Lua（不持有原 view 生命周期）；
//                    get 从栈读取字符串构造 view（指针指向 Lua 内部字符串，在 Lua 修改前有效）。
template <> struct getter<std::string_view> {
  static std::string_view get(lua_State *L, int index) {
    size_t len = 0;
    const char *str = luaL_checklstring(L, index, &len);
    return std::string_view(str, len);
  }
};

template <> struct pusher<std::string_view> {
  static void push(lua_State *L, std::string_view value) {
    lua_pushlstring(L, value.data(), value.size());
  }
};

// 浮点类型：float / double
template <> struct getter<float> {
  static float get(lua_State *L, int index) {
    return static_cast<float>(luaL_checknumber(L, index));
  }
};

template <> struct pusher<float> {
  static void push(lua_State *L, float value) {
    lua_pushnumber(L, static_cast<lua_Number>(value));
  }
};

template <> struct getter<double> {
  static double get(lua_State *L, int index) { return luaL_checknumber(L, index); }
};

template <> struct pusher<double> {
  static void push(lua_State *L, double value) { lua_pushnumber(L, value); }
};

// C 字符串（仅 push；getter 返回内部指针，注意生命周期）
template <> struct getter<const char *> {
  static const char *get(lua_State *L, int index) { return luaL_checkstring(L, index); }
};

template <> struct pusher<const char *> {
  static void push(lua_State *L, const char *value) {
    if (value)
      lua_pushstring(L, value);
    else
      lua_pushnil(L);
  }
};

// std::string
template <> struct getter<std::string> {
  static std::string get(lua_State *L, int index) {
    size_t len = 0;
    const char *str = luaL_checklstring(L, index, &len);
    return std::string(str, len);
  }
};

template <> struct pusher<std::string> {
  static void push(lua_State *L, const std::string &value) {
    lua_pushlstring(L, value.data(), value.size());
  }
  static void push(lua_State *L, std::string &&value) {
    lua_pushlstring(L, value.data(), value.size());
  }
};

// std::optional<T>：nil ↔ nullopt
template <typename T> struct getter<std::optional<T>> {
  static std::optional<T> get(lua_State *L, int index) {
    if (lua_isnil(L, index))
      return std::nullopt;
    return stack::get<T>(L, index);
  }
};

template <typename T> struct pusher<std::optional<T>> {
  static void push(lua_State *L, const std::optional<T> &value) {
    if (value.has_value())
      stack::push(L, *value);
    else
      lua_pushnil(L);
  }
  static void push(lua_State *L, std::optional<T> &&value) {
    if (value.has_value())
      stack::push(L, std::move(*value));
    else
      lua_pushnil(L);
  }
};

} // namespace sptxx
