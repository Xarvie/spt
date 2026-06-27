// map.hpp - SPT 表（TABLE_MAP）的 C++ 绑定
// map<V> 持有强类型值；map<void>（object_map）允许异构值。
// 通过 registry ref 持有 Lua 表，析构时自动释放。
// 迭代器基于 lua_next；K 默认为 std::string，可显式指定 begin<int>() 等。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "error.hpp"
#include "stack.hpp"
#include <cstddef>
#include <iterator>
#include <optional>
#include <string>
#include <utility>

namespace sptxx {

// 强类型 Map。V 为值类型；键类型由各方法模板参数显式指定。
template <typename V = void> class map {
public:
  map() : L_(nullptr), ref_(LUA_NOREF) {}

  // 从已有的 registry ref 构造。验证 ref 指向 TABLE_MAP。
  map(lua_State *L, int ref) : L_(L), ref_(ref) {
    if (!valid())
      throw error("invalid map reference");
    lua_getref(L_, ref_);
    if (!lua_ismap(L_, -1)) {
      lua_pop(L_, 1);
      throw error("reference is not a map (TABLE_MAP)");
    }
    lua_pop(L_, 1);
  }

  map(const map &other) : L_(other.L_), ref_(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L_, other.ref_);
      ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
  }

  map(map &&other) noexcept : L_(other.L_), ref_(other.ref_) {
    other.L_ = nullptr;
    other.ref_ = LUA_NOREF;
  }

  map &operator=(const map &other) {
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

  map &operator=(map &&other) noexcept {
    if (this != &other) {
      release();
      L_ = other.L_;
      ref_ = other.ref_;
      other.L_ = nullptr;
      other.ref_ = LUA_NOREF;
    }
    return *this;
  }

  ~map() { release(); }

  bool valid() const { return L_ != nullptr && ref_ != LUA_NOREF && ref_ != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  lua_State *lua_state() const { return L_; }
  int registry_index() const { return ref_; }

  // SPT 设计中 # 对 TABLE_MAP 总是返回 0；这里保持一致语义。
  std::size_t size() const {
    require_valid();
    return 0;
  }

  // ---- 元素访问 ----
  // 对于 map<V>，仅需指定 Key 类型；V 由类模板参数固定。
  template <typename Key> V get(const Key &key) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 2);
      throw error("key not found in map");
    }
    V result = stack::get<V>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename Key> V get_or_default(const Key &key, const V &default_value) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 2);
      return default_value;
    }
    V result = stack::get<V>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename Key> std::optional<V> try_get(const Key &key) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 2);
      return std::nullopt;
    }
    V result = stack::get<V>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename Key> bool contains(const Key &key) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    bool exists = !lua_isnil(L_, -1);
    lua_pop(L_, 2);
    return exists;
  }

  template <typename Key> void set(const Key &key, const V &value) {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    stack::push(L_, value);
    lua_settable(L_, -3);
    lua_pop(L_, 1);
  }

  template <typename Key> void set(const Key &key, V &&value) {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    stack::push(L_, std::move(value));
    lua_settable(L_, -3);
    lua_pop(L_, 1);
  }

  template <typename Key> void remove(const Key &key) {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_pushnil(L_);
    lua_settable(L_, -3);
    lua_pop(L_, 1);
  }

  // 清空表。边遍历边删除当前 key，避免 lua_next 因 key 被移除而失效。
  void clear() {
    require_valid();
    lua_getref(L_, ref_);
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
      lua_pop(L_, 1);            // 弹出 value，保留 key
      lua_pushvalue(L_, -1);     // 复制 key
      lua_pushnil(L_);           // 新值 = nil
      lua_settable(L_, -4);      // t[key] = nil
    }
    lua_pop(L_, 1);
  }

  // ---- 迭代器 ----
  // K 为键类型，默认 std::string。V 为值类型，由类模板参数固定。
  template <typename K = std::string> class iterator {
  public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<K, V>;
    using pointer = const value_type *;
    using reference = const value_type &;
    using iterator_category = std::forward_iterator_tag;

    iterator() : m_(nullptr), key_ref_(LUA_NOREF), at_end_(true) {}

    iterator(map *m, bool end) : m_(m), key_ref_(LUA_NOREF), at_end_(end) {
      if (!end && m_ && m_->valid())
        advance();
    }

    iterator(const iterator &other)
        : m_(other.m_), key_ref_(LUA_NOREF), at_end_(other.at_end_), current_(other.current_) {
      if (!at_end_ && other.key_ref_ != LUA_NOREF && m_ && m_->valid()) {
        lua_getref(m_->L_, other.key_ref_);
        key_ref_ = luaL_ref(m_->L_, LUA_REGISTRYINDEX);
      }
    }

    iterator(iterator &&other) noexcept
        : m_(other.m_), key_ref_(other.key_ref_), at_end_(other.at_end_),
          current_(std::move(other.current_)) {
      other.m_ = nullptr;
      other.key_ref_ = LUA_NOREF;
      other.at_end_ = true;
    }

    iterator &operator=(const iterator &other) {
      if (this != &other) {
        release_key();
        m_ = other.m_;
        at_end_ = other.at_end_;
        current_ = other.current_;
        if (!at_end_ && other.key_ref_ != LUA_NOREF && m_ && m_->valid()) {
          lua_getref(m_->L_, other.key_ref_);
          key_ref_ = luaL_ref(m_->L_, LUA_REGISTRYINDEX);
        } else {
          key_ref_ = LUA_NOREF;
        }
      }
      return *this;
    }

    iterator &operator=(iterator &&other) noexcept {
      if (this != &other) {
        release_key();
        m_ = other.m_;
        key_ref_ = other.key_ref_;
        at_end_ = other.at_end_;
        current_ = std::move(other.current_);
        other.m_ = nullptr;
        other.key_ref_ = LUA_NOREF;
        other.at_end_ = true;
      }
      return *this;
    }

    ~iterator() { release_key(); }

    const value_type &operator*() const { return current_; }
    const value_type *operator->() const { return &current_; }

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
      if (at_end_ && other.at_end_)
        return true;
      return m_ == other.m_ && at_end_ == other.at_end_;
    }
    bool operator!=(const iterator &other) const { return !(*this == other); }

  private:
    friend class map;

    map *m_;
    int key_ref_;
    bool at_end_;
    value_type current_;

    void release_key() {
      if (key_ref_ != LUA_NOREF && m_ && m_->valid())
        luaL_unref(m_->L_, LUA_REGISTRYINDEX, key_ref_);
      key_ref_ = LUA_NOREF;
    }

    void advance() {
      if (!m_ || !m_->valid() || at_end_) {
        at_end_ = true;
        return;
      }
      lua_getref(m_->L_, m_->ref_);
      if (key_ref_ == LUA_NOREF) {
        lua_pushnil(m_->L_);
      } else {
        lua_getref(m_->L_, key_ref_);
      }
      if (lua_next(m_->L_, -2) == 0) {
        at_end_ = true;
        lua_pop(m_->L_, 1);
        return;
      }
      // 栈：[t, key, value]
      current_.second = stack::get<V>(m_->L_, -1);
      current_.first = stack::get<K>(m_->L_, -2);
      // 保存新的 key 到 registry
      release_key();
      lua_pushvalue(m_->L_, -2); // 复制 key
      key_ref_ = luaL_ref(m_->L_, LUA_REGISTRYINDEX);
      lua_pop(m_->L_, 3); // 弹出 t, key, value
    }
  };

  template <typename K = std::string> iterator<K> begin() { return iterator<K>(this, false); }
  template <typename K = std::string> iterator<K> end() { return iterator<K>(this, true); }

private:
  lua_State *L_;
  int ref_;

  void require_valid() const {
    if (!valid())
      throw error("invalid map");
  }

  void release() {
    if (valid())
      luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
  }
};

// ---- map<void> 特化：异构 object_map ----
// 值类型由各方法模板参数显式指定：set<Key, U>(key, value), get<Key, U>(key)。
template <> class map<void> {
public:
  map() : L_(nullptr), ref_(LUA_NOREF) {}

  map(lua_State *L, int ref) : L_(L), ref_(ref) {
    if (!valid())
      throw error("invalid map reference");
    lua_getref(L_, ref_);
    if (!lua_ismap(L_, -1)) {
      lua_pop(L_, 1);
      throw error("reference is not a map (TABLE_MAP)");
    }
    lua_pop(L_, 1);
  }

  map(const map &other) : L_(other.L_), ref_(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L_, other.ref_);
      ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
  }

  map(map &&other) noexcept : L_(other.L_), ref_(other.ref_) {
    other.L_ = nullptr;
    other.ref_ = LUA_NOREF;
  }

  map &operator=(const map &other) {
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

  map &operator=(map &&other) noexcept {
    if (this != &other) {
      release();
      L_ = other.L_;
      ref_ = other.ref_;
      other.L_ = nullptr;
      other.ref_ = LUA_NOREF;
    }
    return *this;
  }

  ~map() { release(); }

  bool valid() const { return L_ != nullptr && ref_ != LUA_NOREF && ref_ != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  lua_State *lua_state() const { return L_; }
  int registry_index() const { return ref_; }

  std::size_t size() const {
    require_valid();
    return 0;
  }

  // 异构访问：调用方显式指定 Key 和 Value 类型
  template <typename Key, typename U> U get(const Key &key) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 2);
      throw error("key not found in map");
    }
    U result = stack::get<U>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename Key, typename U>
  U get_or_default(const Key &key, const U &default_value) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 2);
      return default_value;
    }
    U result = stack::get<U>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename Key, typename U> std::optional<U> try_get(const Key &key) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    if (lua_isnil(L_, -1)) {
      lua_pop(L_, 2);
      return std::nullopt;
    }
    U result = stack::get<U>(L_, -1);
    lua_pop(L_, 2);
    return result;
  }

  template <typename Key> bool contains(const Key &key) const {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_gettable(L_, -2);
    bool exists = !lua_isnil(L_, -1);
    lua_pop(L_, 2);
    return exists;
  }

  template <typename Key, typename U> void set(const Key &key, const U &value) {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    stack::push(L_, value);
    lua_settable(L_, -3);
    lua_pop(L_, 1);
  }

  template <typename Key, typename U> void set(const Key &key, U &&value) {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    stack::push(L_, std::move(value));
    lua_settable(L_, -3);
    lua_pop(L_, 1);
  }

  template <typename Key> void remove(const Key &key) {
    require_valid();
    lua_getref(L_, ref_);
    stack::push(L_, key);
    lua_pushnil(L_);
    lua_settable(L_, -3);
    lua_pop(L_, 1);
  }

  void clear() {
    require_valid();
    lua_getref(L_, ref_);
    lua_pushnil(L_);
    while (lua_next(L_, -2) != 0) {
      lua_pop(L_, 1);
      lua_pushvalue(L_, -1);
      lua_pushnil(L_);
      lua_settable(L_, -4);
    }
    lua_pop(L_, 1);
  }

private:
  lua_State *L_;
  int ref_;

  void require_valid() const {
    if (!valid())
      throw error("invalid map");
  }

  void release() {
    if (valid())
      luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
  }
};

// 便捷别名
using object_map = map<void>;

// ---- stack 特化：map 可在 Lua 栈与 C++ 间传递 ----

template <typename V> struct getter<map<V>> {
  static map<V> get(lua_State *L, int index) {
    lua_pushvalue(L, index);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return map<V>(L, ref);
  }
};

template <typename V> struct pusher<map<V>> {
  static void push(lua_State *L, const map<V> &value) {
    if (value.valid())
      lua_getref(L, value.registry_index());
    else
      lua_pushnil(L);
  }
};

} // namespace sptxx
