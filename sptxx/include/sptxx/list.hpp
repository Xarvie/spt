// list.hpp - List type support for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "stack.hpp"
#include "error.hpp"
#include <cstddef>
#include <iterator>

namespace sptxx {

template<typename T = void>
class list {
private:
  lua_State *L;
  int ref; // 统一使用极速 O(1) 句柄

public:
  list() : L(nullptr), ref(LUA_NOREF) {}

  list(lua_State *state, int reference) : L(state), ref(reference) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
      throw error("Invalid list reference");
    }
    lua_getref(L, ref);
    if (lua_gettablemode(L, -1) != 1) {
      lua_pop(L, 1);
      throw error("Reference is not a list (TABLE_ARRAY)");
    }
    lua_pop(L, 1);
  }

  list(const list &other) : L(other.L), ref(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L, other.ref);
      ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  }

  list(list &&other) noexcept : L(other.L), ref(other.ref) {
    other.L = nullptr;
    other.ref = LUA_NOREF;
  }

  list &operator=(const list &other) {
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

  list &operator=(list &&other) noexcept {
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

  ~list() {
    if (valid()) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }

  bool valid() const { return L != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  std::size_t size() const {
    if (!valid())
      throw error("Invalid list");
    lua_getref(L, ref);
    lua_Integer len = lua_arraylen(L, -1);
    lua_pop(L, 1);
    return static_cast<std::size_t>(len);
  }

  std::size_t capacity() const {
    if (!valid())
      throw error("Invalid list");
    lua_getref(L, ref);
    lua_Integer cap = lua_arraycapacity(L, -1);
    lua_pop(L, 1);
    return static_cast<std::size_t>(cap);
  }

  bool empty() const {
    if (!valid())
      throw error("Invalid list");
    lua_getref(L, ref);
    int is_empty = lua_arrayisempty(L, -1);
    lua_pop(L, 1);
    return is_empty != 0;
  }

  void resize(std::size_t new_size) {
    if (!valid())
      throw error("Invalid list");
    lua_getref(L, ref);
    lua_arraysetlen(L, -1, static_cast<lua_Integer>(new_size));
    lua_pop(L, 1);
  }

  void reserve(std::size_t capacity) {
    if (!valid())
      throw error("Invalid list");
    lua_getref(L, ref);
    lua_arrayreserve(L, -1, static_cast<lua_Integer>(capacity));
    lua_pop(L, 1);
  }

  template <typename U = T>
  auto get(std::size_t index) const -> typename std::enable_if<!std::is_void_v<U>, U>::type {
    if (!valid())
      throw error("Invalid list");
    if (index >= size())
      throw error("List index out of range");

    lua_getref(L, ref);
    lua_geti(L, -1, static_cast<lua_Integer>(index));
    U result = stack::get<U>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  void set(std::size_t index, const T &value) {
    if (!valid())
      throw error("Invalid list");
    if (index >= size())
      throw error("List index out of range");

    lua_getref(L, ref);
    stack::push(L, value);
    lua_seti(L, -2, static_cast<lua_Integer>(index));
    lua_pop(L, 1);
  }

  void set(std::size_t index, T &&value) {
    if (!valid())
      throw error("Invalid list");
    if (index >= size())
      throw error("List index out of range");

    lua_getref(L, ref);
    stack::push(L, std::move(value));
    lua_seti(L, -2, static_cast<lua_Integer>(index));
    lua_pop(L, 1);
  }

  void push_back(const T &value) {
    if (!valid())
      throw error("Invalid list");
    std::size_t current_size = size();

    lua_getref(L, ref);
    stack::push(L, value);
    lua_seti(L, -2, static_cast<lua_Integer>(current_size));
    lua_pop(L, 1);
  }

  T pop_back() {
    if (!valid())
      throw error("Invalid list");
    std::size_t current_size = size();
    if (current_size == 0)
      throw error("Cannot pop from empty list");

    T result = get(current_size - 1);
    resize(current_size - 1);
    return result;
  }

  class iterator {
  private:
    list *lst;
    std::size_t pos;

  public:
    using difference_type = std::ptrdiff_t;
    using value_type = T;
    using pointer = T *;
    using reference = T &;
    using iterator_category = std::random_access_iterator_tag;

    iterator(list *l, std::size_t p) : lst(l), pos(p) {}

    T operator*() const { return lst->get(pos); }

    iterator &operator++() {
      ++pos;
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      ++(*this);
      return tmp;
    }

    bool operator==(const iterator &other) const { return lst == other.lst && pos == other.pos; }

    bool operator!=(const iterator &other) const { return !(*this == other); }
  };

  iterator begin() { return iterator(this, 0); }
  iterator end() { return iterator(this, size()); }

  lua_State *lua_state() const { return L; }

  int registry_index() const { return ref; }
};

template<>
class list<void> {
private:
  lua_State *L;
  int ref;

public:
  list() : L(nullptr), ref(LUA_NOREF) {}

  list(lua_State *state, int reference) : L(state), ref(reference) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
      throw error("Invalid list reference");
    }
    lua_getref(L, ref);
    if (lua_gettablemode(L, -1) != 1) {
      lua_pop(L, 1);
      throw error("Reference is not a list (TABLE_ARRAY)");
    }
    lua_pop(L, 1);
  }

  list(const list &other) : L(other.L), ref(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L, other.ref);
      ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  }

  list(list &&other) noexcept : L(other.L), ref(other.ref) {
    other.L = nullptr;
    other.ref = LUA_NOREF;
  }

  list &operator=(const list &other) {
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

  list &operator=(list &&other) noexcept {
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

  ~list() {
    if (valid()) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }

  bool valid() const { return L != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  std::size_t size() const {
    if (!valid()) throw error("Invalid list");
    lua_getref(L, ref);
    lua_Integer len = lua_arraylen(L, -1);
    lua_pop(L, 1);
    return static_cast<std::size_t>(len);
  }

  std::size_t capacity() const {
    if (!valid()) throw error("Invalid list");
    lua_getref(L, ref);
    lua_Integer cap = lua_arraycapacity(L, -1);
    lua_pop(L, 1);
    return static_cast<std::size_t>(cap);
  }

  bool empty() const {
    if (!valid()) throw error("Invalid list");
    lua_getref(L, ref);
    int is_empty = lua_arrayisempty(L, -1);
    lua_pop(L, 1);
    return is_empty != 0;
  }

  void resize(std::size_t new_size) {
    if (!valid()) throw error("Invalid list");
    lua_getref(L, ref);
    lua_arraysetlen(L, -1, static_cast<lua_Integer>(new_size));
    lua_pop(L, 1);
  }

  void reserve(std::size_t capacity) {
    if (!valid()) throw error("Invalid list");
    lua_getref(L, ref);
    lua_arrayreserve(L, -1, static_cast<lua_Integer>(capacity));
    lua_pop(L, 1);
  }

  template <typename U> U get(std::size_t index) const {
    if (!valid()) throw error("Invalid list");
    if (index >= size()) throw error("List index out of range");
    lua_getref(L, ref);
    lua_geti(L, -1, static_cast<lua_Integer>(index));
    U result = stack::get<U>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  template <typename U> void set(std::size_t index, const U &value) {
    if (!valid()) throw error("Invalid list");
    if (index >= size()) throw error("List index out of range");
    lua_getref(L, ref);
    stack::push(L, value);
    lua_seti(L, -2, static_cast<lua_Integer>(index));
    lua_pop(L, 1);
  }

  template <typename U> void push_back(const U &value) {
    if (!valid()) throw error("Invalid list");
    std::size_t current_size = size();
    lua_getref(L, ref);
    stack::push(L, value);
    lua_seti(L, -2, static_cast<lua_Integer>(current_size));
    lua_pop(L, 1);
  }

  template <typename U> U pop_back() {
    if (!valid()) throw error("Invalid list");
    std::size_t current_size = size();
    if (current_size == 0) throw error("Cannot pop from empty list");
    U result = get<U>(current_size - 1);
    resize(current_size - 1);
    return result;
  }

  lua_State *lua_state() const { return L; }
  int registry_index() const { return ref; }
};

using object_list = list<void>;

// 允许从 Lua 栈中提取 List
template <typename T> struct getter<list<T>> {
  static list<T> get(lua_State *L, int index) {
    // 将指定索引的 table 拷贝到栈顶
    lua_pushvalue(L, index);
    // 生成 Registry 句柄 (List 析构时会自动 unref)
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return list<T>(L, ref);
  }
};

// 允许将 List 推送到 Lua 栈
template <typename T> struct pusher<list<T>> {
  static void push(lua_State *L, const list<T> &value) {
    if (value.valid()) {
      lua_getref(L, value.registry_index());
    } else {
      lua_pushnil(L);
    }
  }
};

} // namespace sptxx