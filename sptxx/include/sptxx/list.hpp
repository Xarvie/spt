// list.hpp - SPT 数组（LUA_TARRAY）的 C++ 绑定
// list<T> 持有强类型元素；list<void>（object_list）允许异构元素。
// 通过 registry ref 持有 Lua 数组，析构时自动释放。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "error.hpp"
#include "stack.hpp"
#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

namespace sptxx {

// 强类型数组包装。T 为元素类型。
template <typename T = void> class list {
public:
  // 默认构造：无效引用，后续可被赋值。
  list() : L_(nullptr), ref_(LUA_NOREF) {}

  // 从已有的 registry ref 构造。构造时验证 ref 指向 LUA_TARRAY。
  list(lua_State *L, int ref) : L_(L), ref_(ref) {
    if (!valid())
      throw error("invalid list reference");
    lua_getref(L_, ref_);
    if (lua_gettablemode(L_, -1) != 1) { // TABLE_ARRAY
      lua_pop(L_, 1);
      throw error("reference is not a list (TABLE_ARRAY)");
    }
    lua_pop(L_, 1);
  }

  list(const list &other) : L_(other.L_), ref_(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L_, other.ref_);
      ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
  }

  list(list &&other) noexcept : L_(other.L_), ref_(other.ref_) {
    other.L_ = nullptr;
    other.ref_ = LUA_NOREF;
  }

  list &operator=(const list &other) {
    if (this != &other) {
      release();
      L_ = other.L_;
      if (other.valid()) {
        lua_getref(L_, other.ref_);
        ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
      } else {
        ref_ = LUA_NOREF;
      }
    }
    return *this;
  }

  list &operator=(list &&other) noexcept {
    if (this != &other) {
      release();
      L_ = other.L_;
      ref_ = other.ref_;
      other.L_ = nullptr;
      other.ref_ = LUA_NOREF;
    }
    return *this;
  }

  ~list() { release(); }

  bool valid() const { return L_ != nullptr && ref_ != LUA_NOREF && ref_ != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  lua_State *lua_state() const { return L_; }
  int registry_index() const { return ref_; }

  // ---- 容量 ----

  std::size_t size() const {
    require_valid();
    lua_getref(L_, ref_);
    lua_Integer n = lua_arraylen(L_, -1);
    lua_pop(L_, 1);
    return static_cast<std::size_t>(n);
  }

  std::size_t capacity() const {
    require_valid();
    lua_getref(L_, ref_);
    lua_Integer c = lua_arraycapacity(L_, -1);
    lua_pop(L_, 1);
    return static_cast<std::size_t>(c);
  }

  bool empty() const {
    require_valid();
    lua_getref(L_, ref_);
    int e = lua_arrayisempty(L_, -1);
    lua_pop(L_, 1);
    return e != 0;
  }

  void resize(std::size_t n) {
    require_valid();
    lua_getref(L_, ref_);
    lua_arraysetlen(L_, -1, static_cast<lua_Integer>(n));
    lua_pop(L_, 1);
  }

  void reserve(std::size_t cap) {
    require_valid();
    lua_getref(L_, ref_);
    lua_arrayreserve(L_, -1, static_cast<lua_Integer>(cap));
    lua_pop(L_, 1);
  }

  // ---- 元素访问（0-based） ----

  T get(std::size_t index) const {
    require_valid();
    if (index >= size())
      throw error("list index out of range");
    lua_getref(L_, ref_);
    lua_geti(L_, -1, static_cast<lua_Integer>(index));
    T result = stack::get<T>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  void set(std::size_t index, const T &value) {
    require_valid();
    if (index >= size())
      throw error("list index out of range");
    lua_getref(L_, ref_);
    stack::push(L_, value);
    lua_seti(L_, -2, static_cast<lua_Integer>(index));
    lua_pop(L_, 1);
  }

  void set(std::size_t index, T &&value) {
    require_valid();
    if (index >= size())
      throw error("list index out of range");
    lua_getref(L_, ref_);
    stack::push(L_, std::move(value));
    lua_seti(L_, -2, static_cast<lua_Integer>(index));
    lua_pop(L_, 1);
  }

  void push_back(const T &value) {
    require_valid();
    std::size_t n = size();
    lua_getref(L_, ref_);
    stack::push(L_, value);
    lua_seti(L_, -2, static_cast<lua_Integer>(n));
    lua_pop(L_, 1);
  }

  void push_back(T &&value) {
    require_valid();
    std::size_t n = size();
    lua_getref(L_, ref_);
    stack::push(L_, std::move(value));
    lua_seti(L_, -2, static_cast<lua_Integer>(n));
    lua_pop(L_, 1);
  }

  T pop_back() {
    require_valid();
    std::size_t n = size();
    if (n == 0)
      throw error("cannot pop from empty list");
    T result = get(n - 1);
    resize(n - 1);
    return result;
  }

  // ---- 迭代器 ----

  class iterator {
  public:
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = const T *;
    using reference = T;
    using iterator_category = std::forward_iterator_tag;

    iterator() : lst_(nullptr), pos_(0) {}
    iterator(list *l, std::size_t p) : lst_(l), pos_(p) {}

    T operator*() const { return lst_->get(pos_); }

    iterator &operator++() {
      ++pos_;
      return *this;
    }
    iterator operator++(int) {
      iterator tmp = *this;
      ++pos_;
      return tmp;
    }

    bool operator==(const iterator &other) const { return lst_ == other.lst_ && pos_ == other.pos_; }
    bool operator!=(const iterator &other) const { return !(*this == other); }

  private:
    list *lst_;
    std::size_t pos_;
  };

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size()); }

private:
  lua_State *L_;
  int ref_;

  void require_valid() const {
    if (!valid())
      throw error("invalid list");
  }

  void release() {
    if (valid())
      luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
  }
};

// ---- list<void> 特化：异构 object_list ----

template <> class list<void> {
public:
  list() : L_(nullptr), ref_(LUA_NOREF) {}

  list(lua_State *L, int ref) : L_(L), ref_(ref) {
    if (!valid())
      throw error("invalid list reference");
    lua_getref(L_, ref_);
    if (lua_gettablemode(L_, -1) != 1) {
      lua_pop(L_, 1);
      throw error("reference is not a list (TABLE_ARRAY)");
    }
    lua_pop(L_, 1);
  }

  list(const list &other) : L_(other.L_), ref_(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L_, other.ref_);
      ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
  }

  list(list &&other) noexcept : L_(other.L_), ref_(other.ref_) {
    other.L_ = nullptr;
    other.ref_ = LUA_NOREF;
  }

  list &operator=(const list &other) {
    if (this != &other) {
      release();
      L_ = other.L_;
      if (other.valid()) {
        lua_getref(L_, other.ref_);
        ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
      } else {
        ref_ = LUA_NOREF;
      }
    }
    return *this;
  }

  list &operator=(list &&other) noexcept {
    if (this != &other) {
      release();
      L_ = other.L_;
      ref_ = other.ref_;
      other.L_ = nullptr;
      other.ref_ = LUA_NOREF;
    }
    return *this;
  }

  ~list() { release(); }

  bool valid() const { return L_ != nullptr && ref_ != LUA_NOREF && ref_ != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  lua_State *lua_state() const { return L_; }
  int registry_index() const { return ref_; }

  std::size_t size() const {
    require_valid();
    lua_getref(L_, ref_);
    lua_Integer n = lua_arraylen(L_, -1);
    lua_pop(L_, 1);
    return static_cast<std::size_t>(n);
  }

  std::size_t capacity() const {
    require_valid();
    lua_getref(L_, ref_);
    lua_Integer c = lua_arraycapacity(L_, -1);
    lua_pop(L_, 1);
    return static_cast<std::size_t>(c);
  }

  bool empty() const {
    require_valid();
    lua_getref(L_, ref_);
    int e = lua_arrayisempty(L_, -1);
    lua_pop(L_, 1);
    return e != 0;
  }

  void resize(std::size_t n) {
    require_valid();
    lua_getref(L_, ref_);
    lua_arraysetlen(L_, -1, static_cast<lua_Integer>(n));
    lua_pop(L_, 1);
  }

  void reserve(std::size_t cap) {
    require_valid();
    lua_getref(L_, ref_);
    lua_arrayreserve(L_, -1, static_cast<lua_Integer>(cap));
    lua_pop(L_, 1);
  }

  // 异构访问：调用方显式指定目标类型
  template <typename U> U get(std::size_t index) const {
    require_valid();
    if (index >= size())
      throw error("list index out of range");
    lua_getref(L_, ref_);
    lua_geti(L_, -1, static_cast<lua_Integer>(index));
    U result = stack::get<U>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename U> void set(std::size_t index, const U &value) {
    require_valid();
    if (index >= size())
      throw error("list index out of range");
    lua_getref(L_, ref_);
    stack::push(L_, value);
    lua_seti(L_, -2, static_cast<lua_Integer>(index));
    lua_pop(L_, 1);
  }

  template <typename U> void push_back(const U &value) {
    require_valid();
    std::size_t n = size();
    lua_getref(L_, ref_);
    stack::push(L_, value);
    lua_seti(L_, -2, static_cast<lua_Integer>(n));
    lua_pop(L_, 1);
  }

  template <typename U> U pop_back() {
    require_valid();
    std::size_t n = size();
    if (n == 0)
      throw error("cannot pop from empty list");
    U result = get<U>(n - 1);
    resize(n - 1);
    return result;
  }

private:
  lua_State *L_;
  int ref_;

  void require_valid() const {
    if (!valid())
      throw error("invalid list");
  }

  void release() {
    if (valid())
      luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
  }
};

// 便捷别名
using object_list = list<void>;

// ---- stack 特化：list 可在 Lua 栈与 C++ 间传递 ----

template <typename T> struct getter<list<T>> {
  static list<T> get(lua_State *L, int index) {
    lua_pushvalue(L, index);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return list<T>(L, ref);
  }
};

template <typename T> struct pusher<list<T>> {
  static void push(lua_State *L, const list<T> &value) {
    if (value.valid())
      lua_getref(L, value.registry_index());
    else
      lua_pushnil(L);
  }
};

} // namespace sptxx
