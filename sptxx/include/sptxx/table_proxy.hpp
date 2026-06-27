// table_proxy.hpp - 链式访问代理
// 允许：lua["a"]["b"]["c"] = value;  和  int x = lua["a"]["b"].get<int>();
// 设计：每次 operator[] 解析当前代理为 table 快照（registry ref），
// 子代理持有该 ref + 新 key。链断（中间非 table）时子代理标记为 broken，
// 读返回 nil，写抛异常。
//
// 不做隐式 bool 检查存在性；用 .valid() 显式判断。
// 隐式转换 operator T() 用于 `int x = lua["a"];` 这类直接拷贝读。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "error.hpp"
#include "stack.hpp"
#include <string>
#include <type_traits>
#include <utility>

namespace sptxx {

class table_proxy {
public:
  // Root：parent 为 globals 表
  table_proxy(lua_State *L, std::string key)
      : L_(L), parent_ref_(LUA_NOREF), is_root_(true), key_(std::move(key)) {}

  table_proxy(const table_proxy &o)
      : L_(o.L_), parent_ref_(LUA_NOREF), is_root_(o.is_root_), key_(o.key_) {
    if (!is_root_ && o.parent_ref_ != LUA_NOREF) {
      lua_getref(L_, o.parent_ref_);
      parent_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
  }

  table_proxy(table_proxy &&o) noexcept
      : L_(o.L_), parent_ref_(o.parent_ref_), is_root_(o.is_root_),
        key_(std::move(o.key_)) {
    o.parent_ref_ = LUA_NOREF;
    o.is_root_ = false;
  }

  table_proxy &operator=(const table_proxy &o) {
    if (this != &o) {
      release();
      L_ = o.L_;
      is_root_ = o.is_root_;
      key_ = o.key_;
      if (!is_root_ && o.parent_ref_ != LUA_NOREF) {
        lua_getref(L_, o.parent_ref_);
        parent_ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
      } else {
        parent_ref_ = LUA_NOREF;
      }
    }
    return *this;
  }

  table_proxy &operator=(table_proxy &&o) noexcept {
    if (this != &o) {
      release();
      L_ = o.L_;
      parent_ref_ = o.parent_ref_;
      is_root_ = o.is_root_;
      key_ = std::move(o.key_);
      o.parent_ref_ = LUA_NOREF;
      o.is_root_ = false;
    }
    return *this;
  }

  ~table_proxy() { release(); }

  // operator[]：解析当前代理为 table 快照 → 创建子代理
  table_proxy operator[](const char *key) const {
    int ref = resolve_to_table_ref();
    return table_proxy(L_, ref, key);
  }

  // 显式 get<T>
  template <typename T> T get() const {
    push_value();
    T result = stack::get<T>(L_, -1);
    lua_pop(L_, 1);
    return result;
  }

  // 存在则返回值，否则返回 default
  template <typename T> T get_or(T &&default_value) const {
    bool ok = push_value();
    if (!ok) {
      return std::forward<T>(default_value);
    }
    T result = stack::get<T>(L_, -1);
    lua_pop(L_, 1);
    return result;
  }

  // 隐式转换：T x = lua["a"];
  template <typename T,
            typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, table_proxy>>>
  operator T() const {
    return get<T>();
  }

  // 赋值：lua["a"] = value;
  template <typename T> table_proxy &operator=(T &&value) {
    push_parent();
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 1);
      throw error("cannot assign to key '" + key_ +
                  "': parent table is nil (chain broken)");
    }
    stack::push(L_, std::forward<T>(value));
    lua_setfield(L_, -2, key_.c_str());
    lua_pop(L_, 1);
    return *this;
  }

  // 存在性检查（不抛异常）
  bool valid() const {
    bool ok = push_value();
    lua_pop(L_, 1);
    return ok;
  }

private:
  lua_State *L_;
  int parent_ref_; // 父 table 的 registry ref；非 root 且 broken 时为 LUA_NOREF
  bool is_root_;   // true：parent 为 globals；false：parent 为 parent_ref_ 指向的 table
  std::string key_;

  // 子代理构造
  table_proxy(lua_State *L, int parent_ref, std::string key)
      : L_(L), parent_ref_(parent_ref), is_root_(false), key_(std::move(key)) {}

  void release() {
    if (!is_root_ && parent_ref_ != LUA_NOREF) {
      luaL_unref(L_, LUA_REGISTRYINDEX, parent_ref_);
      parent_ref_ = LUA_NOREF;
    }
  }

  // 压入父 table（root→globals，broken→nil）
  void push_parent() const {
    if (is_root_) {
      lua_rawgeti(L_, LUA_REGISTRYINDEX, LUA_RIDX_GLOBALS);
    } else if (parent_ref_ != LUA_NOREF) {
      lua_getref(L_, parent_ref_);
    } else {
      lua_pushnil(L_);
    }
  }

  // 压入本代理指向的值。返回 true 表示非 nil。
  bool push_value() const {
    push_parent();
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 1);
      lua_pushnil(L_);
      return false;
    }
    lua_getfield(L_, -1, key_.c_str());
    lua_remove(L_, -2); // 移除父 table
    return !lua_isnil(L_, -1);
  }

  // 解析本代理为 table，返回其 registry ref；非 table 返回 LUA_NOREF（broken）
  int resolve_to_table_ref() const {
    push_value();
    if (!lua_istable(L_, -1)) {
      lua_pop(L_, 1);
      return LUA_NOREF;
    }
    int ref = luaL_ref(L_, LUA_REGISTRYINDEX);
    return ref;
  }
};

} // namespace sptxx
