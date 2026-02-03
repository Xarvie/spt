#ifndef SPTXX_TABLE_HPP
#define SPTXX_TABLE_HPP

#include "object.hpp"

#include <initializer_list>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Forward Declarations
// ============================================================================

class list;
class map;

// ============================================================================
// Table Proxy - Lazy access to table elements
// ============================================================================

template <typename Table, typename Key> class table_proxy {
public:
  table_proxy(const Table &tbl, Key key) : table_(tbl), key_(std::move(key)) {}

  // Assignment (set value)
  template <typename T> table_proxy &operator=(T &&value) {
    table_.set(key_, std::forward<T>(value));
    return *this;
  }

  // Implicit conversion (get value)
  template <typename T> operator T() const { return table_.template get<T>(key_); }

  // Get as object
  SPTXX_NODISCARD object as_object() const { return table_.get_object(key_); }

  // Check type
  template <typename T> SPTXX_NODISCARD bool is() const {
    stack_guard guard(table_.state());
    table_.push_element(key_);
    return stack::check<T>(table_.state(), -1);
  }

  // Get type
  SPTXX_NODISCARD type get_type() const {
    stack_guard guard(table_.state());
    table_.push_element(key_);
    return stack::get_type(table_.state(), -1);
  }

  // Chained access
  template <typename K> SPTXX_NODISCARD auto operator[](K &&k) const {
    // This would need to create a new proxy for nested access
    // For simplicity, get as map/list first
    return as_object();
  }

private:
  const Table &table_;
  Key key_;
};

// ============================================================================
// List - Dynamic array type
// ============================================================================

class list {
public:
  list() noexcept = default;

  // Create from stack index
  list(state_t *S, int index) : ref_(S, index) {
#if defined(SPTXX_DEBUG_MODE)
    SPTXX_ASSERT(spt_islist(S, index) || spt_isnoneornil(S, index), "Expected list type");
#endif
  }

  // Create from reference
  explicit list(reference &&ref) noexcept : ref_(std::move(ref)) {}

  // Create new empty list
  static list create(state_t *S, int capacity = 0) {
    spt_newlist(S, capacity);
    return list(reference(S));
  }

  // Create from initializer list
  template <typename T> static list create(state_t *S, std::initializer_list<T> init) {
    spt_newlist(S, static_cast<int>(init.size()));
    int idx = spt_gettop(S);
    for (const auto &item : init) {
      stack::push(S, item);
      spt_listappend(S, idx);
    }
    return list(reference(S));
  }

  // Create from vector
  template <typename T> static list create(state_t *S, const std::vector<T> &vec) {
    spt_newlist(S, static_cast<int>(vec.size()));
    int idx = spt_gettop(S);
    for (const auto &item : vec) {
      stack::push(S, item);
      spt_listappend(S, idx);
    }
    return list(reference(S));
  }

  // Move only
  list(list &&) noexcept = default;
  list &operator=(list &&) noexcept = default;

  // Copy creates new reference
  list(const list &other) : ref_(other.ref_.copy()) {}

  list &operator=(const list &other) {
    if (this != &other) {
      ref_ = other.ref_.copy();
    }
    return *this;
  }

  // State access
  SPTXX_NODISCARD state_t *state() const noexcept { return ref_.state(); }

  // Validity
  SPTXX_NODISCARD bool valid() const noexcept { return ref_.valid(); }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // Size
  SPTXX_NODISCARD int size() const {
    if (!valid())
      return 0;
    stack_guard guard(state());
    ref_.push();
    return spt_listlen(state(), -1);
  }

  SPTXX_NODISCARD bool empty() const { return size() == 0; }

  // Element access
  template <typename T> SPTXX_NODISCARD T get(int index) const {
    stack_guard guard(state());
    push_element(index);
    return stack::get<T>(state(), -1);
  }

  SPTXX_NODISCARD object get_object(int index) const {
    state_t *S = state();
    ref_.push();
    spt_listgeti(S, -1, index);
    object result{reference(S)};
    spt_pop(S, 1); // Pop list
    return result;
  }

  // Element modification
  template <typename T> void set(int index, T &&value) {
    state_t *S = state();
    ref_.push();
    int list_idx = spt_gettop(S);
    stack::push(S, std::forward<T>(value));
    spt_listseti(S, list_idx, index);
    spt_pop(S, 1); // Pop list
  }

  // Append
  template <typename T> void append(T &&value) {
    state_t *S = state();
    ref_.push();
    int list_idx = spt_gettop(S);
    stack::push(S, std::forward<T>(value));
    spt_listappend(S, list_idx);
    spt_pop(S, 1); // Pop list
  }

  // Insert at position
  template <typename T> void insert(int index, T &&value) {
    state_t *S = state();
    ref_.push();
    int list_idx = spt_gettop(S);
    stack::push(S, std::forward<T>(value));
    spt_listinsert(S, list_idx, index);
    spt_pop(S, 1); // Pop list
  }

  // Remove at position
  template <typename T = object> T remove(int index) {
    state_t *S = state();
    ref_.push();
    int list_idx = spt_gettop(S);
    spt_listremove(S, list_idx, index);
    T result = stack::get<T>(S, -1);
    spt_pop(S, 2); // Pop result and list
    return result;
  }

  // Clear
  void clear() {
    state_t *S = state();
    stack_guard guard(S);
    ref_.push();
    spt_listclear(S, -1);
  }

  // Operator[]
  SPTXX_NODISCARD table_proxy<list, int> operator[](int index) const {
    return table_proxy<list, int>(*this, index);
  }

  // Push onto stack
  void push() const { ref_.push(); }

  // Push element onto stack (for internal use)
  void push_element(int index) const {
    ref_.push();
    spt_listgeti(state(), -1, index);
    spt_remove(state(), -2); // Remove list, keep value
  }

  // Convert to vector
  template <typename T> SPTXX_NODISCARD std::vector<T> to_vector() const {
    stack_guard guard(state());
    ref_.push();
    return stack::get<std::vector<T>>(state(), -1);
  }

  // Iterator support
  class iterator {
  public:
    using difference_type = int;
    using value_type = object;
    using pointer = object *;
    using reference = object;
    using iterator_category = std::forward_iterator_tag;

    iterator(const list *lst, int idx) : list_(lst), index_(idx) {}

    reference operator*() const { return list_->get_object(index_); }

    iterator &operator++() {
      ++index_;
      return *this;
    }

    iterator operator++(int) {
      iterator tmp = *this;
      ++index_;
      return tmp;
    }

    bool operator==(const iterator &other) const { return index_ == other.index_; }

    bool operator!=(const iterator &other) const { return index_ != other.index_; }

  private:
    const list *list_;
    int index_;
  };

  SPTXX_NODISCARD iterator begin() const { return iterator(this, 0); }

  SPTXX_NODISCARD iterator end() const { return iterator(this, size()); }

private:
  reference ref_;
};

// ============================================================================
// Map - Key-value container
// ============================================================================

class map {
public:
  map() noexcept = default;

  // Create from stack index
  map(state_t *S, int index) : ref_(S, index) {
#if defined(SPTXX_DEBUG_MODE)
    SPTXX_ASSERT(spt_ismap(S, index) || spt_isnoneornil(S, index), "Expected map type");
#endif
  }

  // Create from reference
  explicit map(reference &&ref) noexcept : ref_(std::move(ref)) {}

  // Create new empty map
  static map create(state_t *S, int capacity = 0) {
    spt_newmap(S, capacity);
    return map(reference(S));
  }

  // Create from initializer list
  template <typename K, typename V>
  static map create(state_t *S, std::initializer_list<std::pair<K, V>> init) {
    spt_newmap(S, static_cast<int>(init.size()));
    int idx = spt_gettop(S);
    for (const auto &[k, v] : init) {
      stack::push(S, k);
      stack::push(S, v);
      spt_setmap(S, idx);
    }
    return map(reference(S));
  }

  // Move only
  map(map &&) noexcept = default;
  map &operator=(map &&) noexcept = default;

  // Copy creates new reference
  map(const map &other) : ref_(other.ref_.copy()) {}

  map &operator=(const map &other) {
    if (this != &other) {
      ref_ = other.ref_.copy();
    }
    return *this;
  }

  // State access
  SPTXX_NODISCARD state_t *state() const noexcept { return ref_.state(); }

  // Validity
  SPTXX_NODISCARD bool valid() const noexcept { return ref_.valid(); }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // Size
  SPTXX_NODISCARD int size() const {
    if (!valid())
      return 0;
    stack_guard guard(state());
    ref_.push();
    return spt_maplen(state(), -1);
  }

  SPTXX_NODISCARD bool empty() const { return size() == 0; }

  // Get by string key
  template <typename T> SPTXX_NODISCARD T get(const char *key) const {
    stack_guard guard(state());
    push_element(key);
    return stack::get<T>(state(), -1);
  }

  template <typename T> SPTXX_NODISCARD T get(const std::string &key) const {
    return get<T>(key.c_str());
  }

  // Get by any key
  template <typename T, typename K> SPTXX_NODISCARD T get(const K &key) const {
    stack_guard guard(state());
    push_element(key);
    return stack::get<T>(state(), -1);
  }

  // Get as object
  SPTXX_NODISCARD object get_object(const char *key) const {
    state_t *S = state();
    ref_.push();
    spt_getfield(S, -1, key);
    object result{reference(S)};
    spt_pop(S, 1); // Pop map
    return result;
  }

  template <typename K> SPTXX_NODISCARD object get_object(const K &key) const {
    state_t *S = state();
    ref_.push();
    int map_idx = spt_gettop(S);
    stack::push(S, key);
    spt_getmap(S, map_idx);
    object result{reference(S)};
    spt_pop(S, 1); // Pop map
    return result;
  }

  // Set by string key
  template <typename T> void set(const char *key, T &&value) {
    state_t *S = state();
    ref_.push();
    int map_idx = spt_gettop(S);
    stack::push(S, std::forward<T>(value));
    spt_setfield(S, map_idx, key);
    spt_pop(S, 1); // Pop map
  }

  template <typename T> void set(const std::string &key, T &&value) {
    set(key.c_str(), std::forward<T>(value));
  }

  // Set by any key
  template <typename K, typename V> void set(const K &key, V &&value) {
    state_t *S = state();
    ref_.push();
    int map_idx = spt_gettop(S);
    stack::push(S, key);
    stack::push(S, std::forward<V>(value));
    spt_setmap(S, map_idx);
    spt_pop(S, 1); // Pop map
  }

  // Check if key exists
  SPTXX_NODISCARD bool has(const char *key) const {
    state_t *S = state();
    stack_guard guard(S);
    ref_.push();
    spt_pushstring(S, key);
    return spt_haskey(S, -2) != 0;
  }

  template <typename K> SPTXX_NODISCARD bool has(const K &key) const {
    state_t *S = state();
    stack_guard guard(S);
    ref_.push();
    stack::push(S, key);
    return spt_haskey(S, -2) != 0;
  }

  // Remove key
  template <typename T = object> T remove(const char *key) {
    state_t *S = state();
    ref_.push();
    int map_idx = spt_gettop(S);
    spt_pushstring(S, key);
    spt_mapremove(S, map_idx);
    T result = stack::get<T>(S, -1);
    spt_pop(S, 2); // Pop result and map
    return result;
  }

  // Clear
  void clear() {
    state_t *S = state();
    stack_guard guard(S);
    ref_.push();
    spt_mapclear(S, -1);
  }

  // Get keys as list
  SPTXX_NODISCARD list keys() const {
    state_t *S = state();
    ref_.push();
    spt_mapkeys(S, -1);
    list result{reference(S)};
    spt_pop(S, 1); // Pop map
    return result;
  }

  // Get values as list
  SPTXX_NODISCARD list values() const {
    state_t *S = state();
    ref_.push();
    spt_mapvalues(S, -1);
    list result{reference(S)};
    spt_pop(S, 1); // Pop map
    return result;
  }

  // Operator[]
  SPTXX_NODISCARD table_proxy<map, std::string> operator[](const char *key) const {
    return table_proxy<map, std::string>(*this, std::string(key));
  }

  SPTXX_NODISCARD table_proxy<map, std::string> operator[](const std::string &key) const {
    return table_proxy<map, std::string>(*this, key);
  }

  template <typename K> SPTXX_NODISCARD table_proxy<map, K> operator[](const K &key) const {
    return table_proxy<map, K>(*this, key);
  }

  // Push onto stack
  void push() const { ref_.push(); }

  // Push element onto stack (for internal use)
  void push_element(const char *key) const {
    ref_.push();
    spt_getfield(state(), -1, key);
    spt_remove(state(), -2); // Remove map, keep value
  }

  template <typename K> void push_element(const K &key) const {
    ref_.push();
    stack::push(state(), key);
    spt_getmap(state(), -2);
    spt_remove(state(), -2); // Remove map, keep value
  }

  // Convert to std::map
  template <typename K, typename V> SPTXX_NODISCARD std::map<K, V> to_map() const {
    stack_guard guard(state());
    ref_.push();
    return stack::get<std::map<K, V>>(state(), -1);
  }

  // Convert to std::unordered_map
  template <typename K, typename V>
  SPTXX_NODISCARD std::unordered_map<K, V> to_unordered_map() const {
    stack_guard guard(state());
    ref_.push();
    return stack::get<std::unordered_map<K, V>>(state(), -1);
  }

  // Iterator support (key-value pairs)
  class iterator {
  public:
    using difference_type = std::ptrdiff_t;
    using value_type = std::pair<object, object>;
    using pointer = value_type *;
    using reference = value_type;
    using iterator_category = std::forward_iterator_tag;

    iterator() : map_(nullptr), done_(true) {}

    iterator(const map *m, bool start) : map_(m), done_(!start) {
      if (start && map_->valid()) {
        // Push map and nil to start iteration
        map_->push();
        spt_pushnil(map_->state());
        advance();
      } else {
        done_ = true;
      }
    }

    ~iterator() { cleanup(); }

    reference operator*() const { return current_; }

    iterator &operator++() {
      advance();
      return *this;
    }

    bool operator==(const iterator &other) const { return done_ == other.done_; }

    bool operator!=(const iterator &other) const { return done_ != other.done_; }

  private:
    void advance() {
      if (done_)
        return;

      state_t *S = map_->state();
      // Stack: map, prev_key
      if (spt_mapnext(S, -2)) {
        // Stack: map, next_key, value
        current_.first = object(S, -2);
        current_.second = object(S, -1);
        spt_pop(S, 1); // Pop value, keep key for next iteration
      } else {
        // Iteration complete
        done_ = true;
        spt_pop(S, 1); // Pop map
      }
    }

    void cleanup() {
      if (!done_ && map_ && map_->valid()) {
        // Pop remaining stack items
        spt_pop(map_->state(), 2); // Pop key and map
      }
    }

    const map *map_;
    bool done_;
    value_type current_;
  };

  SPTXX_NODISCARD iterator begin() const { return iterator(this, true); }

  SPTXX_NODISCARD iterator end() const { return iterator(this, false); }

private:
  reference ref_;
};

// ============================================================================
// Stack Pusher/Getter for list and map
// ============================================================================

template <> struct stack_pusher<list> {
  static int push(state_t *S, const list &lst) {
    lst.push();
    return 1;
  }
};

template <> struct stack_getter<list> {
  static list get(state_t *S, int idx) { return list(S, idx); }
};

template <> struct stack_checker<list> {
  static bool check(state_t *S, int idx) { return spt_islist(S, idx); }
};

template <> struct stack_pusher<map> {
  static int push(state_t *S, const map &m) {
    m.push();
    return 1;
  }
};

template <> struct stack_getter<map> {
  static map get(state_t *S, int idx) { return map(S, idx); }
};

template <> struct stack_checker<map> {
  static bool check(state_t *S, int idx) { return spt_ismap(S, idx); }
};

SPTXX_NAMESPACE_END

#endif // SPTXX_TABLE_HPP
