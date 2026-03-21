// map.hpp - Map type support for SPT Lua 5.5 C++ bindings

#pragma once

extern "C" {
#include "../../src/Vm/lua.h"
#include "../../src/Vm/lauxlib.h"
}

#include "error.hpp"
#include "stack.hpp"
#include <cstddef>
#include <iterator>
#include <optional>
#include <utility>

namespace sptxx {

template<typename T = void>
class map {
private:
  lua_State *L;
  int ref;

public:
  // Constructors
  map() : L(nullptr), ref(LUA_NOREF) {}

  map(lua_State *state, int reference) : L(state), ref(reference) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
      throw error("Invalid map reference");
    }
    // Verify it's actually a map (TABLE_MAP)
    lua_getref(L, ref); // 构造函数这里已经是正确的 API
    if (!lua_ismap(L, -1)) {
      lua_pop(L, 1);
      throw error("Reference is not a map (TABLE_MAP)");
    }
    lua_pop(L, 1);
  }

  // Copy constructor
  map(const map &other) : L(other.L), ref(LUA_NOREF) {
    if (other.valid()) {
      // 修复: 使用专用的 getref API
      lua_getref(L, other.ref);
      ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  }

  // Move constructor
  map(map &&other) noexcept : L(other.L), ref(other.ref) {
    other.L = nullptr;
    other.ref = LUA_NOREF;
  }

  // Assignment operators
  map &operator=(const map &other) {
    if (this != &other) {
      if (valid()) {
        luaL_unref(L, LUA_REGISTRYINDEX, ref);
      }
      L = other.L;
      if (other.valid()) {
        // 修复: 使用专用的 getref API
        lua_getref(L, other.ref);
        ref = luaL_ref(L, LUA_REGISTRYINDEX);
      } else {
        ref = LUA_NOREF;
      }
    }
    return *this;
  }

  map &operator=(map &&other) noexcept {
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

  ~map() {
    if (valid()) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }

  // Validity check
  bool valid() const { return L != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }

  explicit operator bool() const { return valid(); }

  // Size (always returns 0 for maps according to SPT design)
  std::size_t size() const {
    if (!valid()) {
      throw error("Invalid map");
    }
    // According to SPT design, # operator on TABLE_MAP always returns 0
    return 0;
  }

  // Element access
  template <typename Key> T get(const Key &key) const {
    if (!valid()) {
      throw error("Invalid map");
    }
    // 修复: 使用专用的 getref API
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      throw error("Key not found in map");
    }
    T result = stack::get<T>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  template <typename Key> bool contains(const Key &key) const {
    if (!valid()) {
      throw error("Invalid map");
    }
    // 修复: 使用专用的 getref API
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    bool exists = !lua_isnil(L, -1);
    lua_pop(L, 2);
    return exists;
  }

  template <typename Key> void set(const Key &key, const T &value) {
    if (!valid()) {
      throw error("Invalid map");
    }
    // 修复: 使用专用的 getref API
    lua_getref(L, ref);
    stack::push(L, key);
    stack::push(L, value);
    lua_settable(L, -3);
    lua_pop(L, 1);
  }

  template <typename Key> void set(const Key &key, T &&value) {
    if (!valid()) {
      throw error("Invalid map");
    }
    // 修复: 使用专用的 getref API
    lua_getref(L, ref);
    stack::push(L, key);
    stack::push(L, std::move(value));
    lua_settable(L, -3);
    lua_pop(L, 1);
  }

  template <typename Key> void remove(const Key &key) {
    if (!valid()) {
      throw error("Invalid map");
    }
    // 修复: 使用专用的 getref API
    lua_getref(L, ref);
    stack::push(L, key);
    lua_pushnil(L);
    lua_settable(L, -3);
    lua_pop(L, 1);
  }

  void clear() {
    if (!valid()) {
      throw error("Invalid map");
    }
    lua_getref(L, ref);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      lua_pop(L, 1);
      lua_pushvalue(L, -1);
      lua_pushnil(L);
      lua_settable(L, -4);
    }
    lua_pop(L, 1);
  }

  template <typename Key> T get_or_default(const Key &key, const T &default_value) const {
    if (!valid()) {
      throw error("Invalid map");
    }
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return default_value;
    }
    T result = stack::get<T>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  template <typename Key> std::optional<T> try_get(const Key &key) const {
    if (!valid()) {
      throw error("Invalid map");
    }
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return std::nullopt;
    }
    T result = stack::get<T>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  template <typename Key, typename V = T> class iterator {
  private:
    map *m;
    int iter_key_ref;
    bool at_end;
    std::pair<Key, V> current;

    void advance() {
      if (!m || !m->valid() || at_end) {
        at_end = true;
        return;
      }

      lua_getref(m->L, m->ref);
      if (iter_key_ref == LUA_NOREF) {
        lua_pushnil(m->L);
      } else {
        lua_getref(m->L, iter_key_ref);
      }

      if (lua_next(m->L, -2) == 0) {
        at_end = true;
        lua_pop(m->L, 1);
        return;
      }

      current.second = stack::get<V>(m->L, -1);
      current.first = stack::get<Key>(m->L, -2);

      if (iter_key_ref != LUA_NOREF) {
        luaL_unref(m->L, LUA_REGISTRYINDEX, iter_key_ref);
      }
      lua_pushvalue(m->L, -2);
      iter_key_ref = luaL_ref(m->L, LUA_REGISTRYINDEX);

      lua_pop(m->L, 3);
    }

  public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<Key, V>;
    using pointer = value_type *;
    using reference = value_type &;
    using iterator_category = std::forward_iterator_tag;

    iterator() : m(nullptr), iter_key_ref(LUA_NOREF), at_end(true) {}

    iterator(map *map_ptr, bool end = false) : m(map_ptr), iter_key_ref(LUA_NOREF), at_end(end) {
      if (!end && m && m->valid()) {
        advance();
      }
    }

    iterator(const iterator &other)
        : m(other.m), iter_key_ref(LUA_NOREF), at_end(other.at_end), current(other.current) {
      if (!at_end && other.iter_key_ref != LUA_NOREF) {
        lua_getref(m->L, other.iter_key_ref);
        iter_key_ref = luaL_ref(m->L, LUA_REGISTRYINDEX);
      }
    }

    iterator(iterator &&other) noexcept
        : m(other.m), iter_key_ref(other.iter_key_ref), at_end(other.at_end),
          current(std::move(other.current)) {
      other.m = nullptr;
      other.iter_key_ref = LUA_NOREF;
      other.at_end = true;
    }

    iterator &operator=(const iterator &other) {
      if (this != &other) {
        if (iter_key_ref != LUA_NOREF) {
          luaL_unref(m->L, LUA_REGISTRYINDEX, iter_key_ref);
        }
        m = other.m;
        at_end = other.at_end;
        current = other.current;
        if (!at_end && other.iter_key_ref != LUA_NOREF) {
          lua_getref(m->L, other.iter_key_ref);
          iter_key_ref = luaL_ref(m->L, LUA_REGISTRYINDEX);
        } else {
          iter_key_ref = LUA_NOREF;
        }
      }
      return *this;
    }

    iterator &operator=(iterator &&other) noexcept {
      if (this != &other) {
        if (iter_key_ref != LUA_NOREF) {
          luaL_unref(m->L, LUA_REGISTRYINDEX, iter_key_ref);
        }
        m = other.m;
        iter_key_ref = other.iter_key_ref;
        at_end = other.at_end;
        current = std::move(other.current);
        other.m = nullptr;
        other.iter_key_ref = LUA_NOREF;
        other.at_end = true;
      }
      return *this;
    }

    ~iterator() {
      if (iter_key_ref != LUA_NOREF && m && m->valid()) {
        luaL_unref(m->L, LUA_REGISTRYINDEX, iter_key_ref);
      }
    }

    const value_type &operator*() const { return current; }

    const value_type *operator->() const { return &current; }

    iterator &operator++() {
      advance();
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      advance();
      return tmp;
    }

    bool operator==(const iterator &other) const {
      if (at_end && other.at_end)
        return true;
      return m == other.m && at_end == other.at_end;
    }

    bool operator!=(const iterator &other) const { return !(*this == other); }
  };

  template <typename Key = std::string> iterator<Key, T> begin() {
    return iterator<Key, T>(this, false);
  }

  template <typename Key = std::string> iterator<Key, T> end() {
    return iterator<Key, T>(this, true);
  }

  // Raw Lua state access
  lua_State *lua_state() const { return L; }

  int registry_index() const { return ref; }
};

// Specialization for void (generic object map)
template<>
class map<void> {
private:
  lua_State *L;
  int ref;

public:
  map() : L(nullptr), ref(LUA_NOREF) {}

  map(lua_State *state, int reference) : L(state), ref(reference) {
    if (ref == LUA_NOREF || ref == LUA_REFNIL) {
      throw error("Invalid map reference");
    }
    lua_getref(L, ref);
    if (!lua_ismap(L, -1)) {
      lua_pop(L, 1);
      throw error("Reference is not a map (TABLE_MAP)");
    }
    lua_pop(L, 1);
  }

  map(const map &other) : L(other.L), ref(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L, other.ref);
      ref = luaL_ref(L, LUA_REGISTRYINDEX);
    }
  }

  map(map &&other) noexcept : L(other.L), ref(other.ref) {
    other.L = nullptr;
    other.ref = LUA_NOREF;
  }

  map &operator=(const map &other) {
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

  map &operator=(map &&other) noexcept {
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

  ~map() {
    if (valid()) {
      luaL_unref(L, LUA_REGISTRYINDEX, ref);
    }
  }

  bool valid() const { return L != nullptr && ref != LUA_NOREF && ref != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  std::size_t size() const {
    if (!valid()) throw error("Invalid map");
    return 0;
  }

  template <typename Key, typename U> U get(const Key &key) const {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      throw error("Key not found in map");
    }
    U result = stack::get<U>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  template <typename Key> bool contains(const Key &key) const {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    bool exists = !lua_isnil(L, -1);
    lua_pop(L, 2);
    return exists;
  }

  template <typename Key, typename U> void set(const Key &key, const U &value) {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    stack::push(L, value);
    lua_settable(L, -3);
    lua_pop(L, 1);
  }

  template <typename Key, typename U> void set(const Key &key, U &&value) {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    stack::push(L, std::move(value));
    lua_settable(L, -3);
    lua_pop(L, 1);
  }

  template <typename Key> void remove(const Key &key) {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    lua_pushnil(L);
    lua_settable(L, -3);
    lua_pop(L, 1);
  }

  void clear() {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    lua_pushnil(L);
    while (lua_next(L, -2) != 0) {
      lua_pop(L, 1);
      lua_pushvalue(L, -1);
      lua_pushnil(L);
      lua_settable(L, -4);
    }
    lua_pop(L, 1);
  }

  template <typename Key, typename U> U get_or_default(const Key &key, const U &default_value) const {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return default_value;
    }
    U result = stack::get<U>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  template <typename Key, typename U> std::optional<U> try_get(const Key &key) const {
    if (!valid()) throw error("Invalid map");
    lua_getref(L, ref);
    stack::push(L, key);
    lua_gettable(L, -2);
    if (lua_isnil(L, -1)) {
      lua_pop(L, 2);
      return std::nullopt;
    }
    U result = stack::get<U>(L, -1);
    lua_pop(L, 2);
    return result;
  }

  lua_State *lua_state() const { return L; }
  int registry_index() const { return ref; }
};

using object_map = map<void>;

// 允许从 Lua 栈中提取 Map
template <typename T> struct getter<map<T>> {
  static map<T> get(lua_State *L, int index) {
    lua_pushvalue(L, index);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return map<T>(L, ref);
  }
};

// 允许将 Map 推送到 Lua 栈
template <typename T> struct pusher<map<T>> {
  static void push(lua_State *L, const map<T> &value) {
    if (value.valid()) {
      lua_getref(L, value.registry_index());
    } else {
      lua_pushnil(L);
    }
  }
};

} // namespace sptxx