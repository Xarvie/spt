#ifndef SPTXX_STACK_HPP
#define SPTXX_STACK_HPP

#include "types.hpp"

#include <algorithm>
#include <map>
#include <unordered_map>
#include <vector>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Stack Guard (RAII)
// ============================================================================

struct stack_guard {
  state_t *S;
  int top;

  explicit stack_guard(state_t *s) noexcept : S(s), top(spt_gettop(s)) {}

  ~stack_guard() noexcept { spt_settop(S, top); }

  stack_guard(const stack_guard &) = delete;
  stack_guard &operator=(const stack_guard &) = delete;

  // Disable guard (don't restore stack on destruction)
  void release() noexcept { top = spt_gettop(S); }

  // Get number of new items pushed since guard creation
  SPTXX_NODISCARD int pushed() const noexcept { return spt_gettop(S) - top; }
};

// ============================================================================
// Stack Probe (Check without modification)
// ============================================================================

namespace stack {

// Get stack top
inline int top(state_t *S) noexcept { return spt_gettop(S); }

// Set stack top
inline void set_top(state_t *S, int idx) noexcept { spt_settop(S, idx); }

// Pop n values
inline void pop(state_t *S, int n = 1) noexcept { spt_pop(S, n); }

// Push a copy of value at index
inline void push_value(state_t *S, int idx) noexcept { spt_pushvalue(S, idx); }

// Check available stack space
inline bool check(state_t *S, int n) noexcept { return spt_checkstack(S, n) != 0; }

// Get absolute index
inline int abs_index(state_t *S, int idx) noexcept { return spt_absindex(S, idx); }

// Get type at index
inline type get_type(state_t *S, int idx) noexcept { return static_cast<type>(spt_type(S, idx)); }

// Check if index is valid
inline bool is_valid(state_t *S, int idx) noexcept { return get_type(S, idx) != type::none; }

// Check if value at index is nil
inline bool is_nil(state_t *S, int idx) noexcept { return spt_isnil(S, idx); }

// Check if value at index is none or nil
inline bool is_none_or_nil(state_t *S, int idx) noexcept { return spt_isnoneornil(S, idx); }

} // namespace stack

// ============================================================================
// Stack Pusher - Push C++ values to SPT stack
// ============================================================================

template <typename T, typename> struct stack_pusher {
  static_assert(sizeof(T) == 0, "No stack_pusher specialization for this type");
};

// nil_t
template <> struct stack_pusher<nil_t> {
  static int push(state_t *S, nil_t) {
    spt_pushnil(S);
    return 1;
  }
};

// none_t (pushes nothing)
template <> struct stack_pusher<none_t> {
  static int push(state_t *S, none_t) {
    (void)S;
    return 0;
  }
};

// bool
template <> struct stack_pusher<bool> {
  static int push(state_t *S, bool b) {
    spt_pushbool(S, b ? 1 : 0);
    return 1;
  }
};

// Integer types
template <typename T> struct stack_pusher<T, std::enable_if_t<is_integer_v<T>>> {
  static int push(state_t *S, T n) {
    spt_pushint(S, static_cast<integer_t>(n));
    return 1;
  }
};

// Floating point types
template <typename T> struct stack_pusher<T, std::enable_if_t<is_floating_v<T>>> {
  static int push(state_t *S, T n) {
    spt_pushfloat(S, static_cast<number_t>(n));
    return 1;
  }
};

// const char*
template <> struct stack_pusher<const char *> {
  static int push(state_t *S, const char *s) {
    if (s == nullptr) {
      spt_pushnil(S);
    } else {
      spt_pushstring(S, s);
    }
    return 1;
  }
};

// char*
template <> struct stack_pusher<char *> {
  static int push(state_t *S, char *s) { return stack_pusher<const char *>::push(S, s); }
};

// char array
template <std::size_t N> struct stack_pusher<char[N]> {
  static int push(state_t *S, const char (&s)[N]) {
    spt_pushlstring(S, s, N - 1);
    return 1;
  }
};

// std::string
template <> struct stack_pusher<std::string> {
  static int push(state_t *S, const std::string &s) {
    spt_pushlstring(S, s.data(), s.size());
    return 1;
  }
};

// std::string_view
template <> struct stack_pusher<std::string_view> {
  static int push(state_t *S, std::string_view s) {
    spt_pushlstring(S, s.data(), s.size());
    return 1;
  }
};

// C function
template <> struct stack_pusher<cfunction_t> {
  static int push(state_t *S, cfunction_t f) {
    spt_pushcfunction(S, f);
    return 1;
  }
};

// void* (light userdata)
template <> struct stack_pusher<void *> {
  static int push(state_t *S, void *p) {
    spt_pushlightuserdata(S, p);
    return 1;
  }
};

// const void*
template <> struct stack_pusher<const void *> {
  static int push(state_t *S, const void *p) {
    spt_pushlightuserdata(S, const_cast<void *>(p));
    return 1;
  }
};

// std::vector -> SPT list
template <typename T, typename Alloc> struct stack_pusher<std::vector<T, Alloc>> {
  static int push(state_t *S, const std::vector<T, Alloc> &vec) {
    spt_newlist(S, static_cast<int>(vec.size()));
    int idx = spt_gettop(S);
    for (const auto &item : vec) {
      stack_pusher<T>::push(S, item);
      spt_listappend(S, idx);
    }
    return 1;
  }
};

// std::array -> SPT list
template <typename T, std::size_t N> struct stack_pusher<std::array<T, N>> {
  static int push(state_t *S, const std::array<T, N> &arr) {
    spt_newlist(S, static_cast<int>(N));
    int idx = spt_gettop(S);
    for (const auto &item : arr) {
      stack_pusher<T>::push(S, item);
      spt_listappend(S, idx);
    }
    return 1;
  }
};

// std::map -> SPT map
template <typename K, typename V, typename Comp, typename Alloc>
struct stack_pusher<std::map<K, V, Comp, Alloc>> {
  static int push(state_t *S, const std::map<K, V, Comp, Alloc> &m) {
    spt_newmap(S, static_cast<int>(m.size()));
    int idx = spt_gettop(S);
    for (const auto &[k, v] : m) {
      stack_pusher<K>::push(S, k);
      stack_pusher<V>::push(S, v);
      spt_setmap(S, idx);
    }
    return 1;
  }
};

// std::unordered_map -> SPT map
template <typename K, typename V, typename Hash, typename Eq, typename Alloc>
struct stack_pusher<std::unordered_map<K, V, Hash, Eq, Alloc>> {
  static int push(state_t *S, const std::unordered_map<K, V, Hash, Eq, Alloc> &m) {
    spt_newmap(S, static_cast<int>(m.size()));
    int idx = spt_gettop(S);
    for (const auto &[k, v] : m) {
      stack_pusher<K>::push(S, k);
      stack_pusher<V>::push(S, v);
      spt_setmap(S, idx);
    }
    return 1;
  }
};

// std::pair -> SPT list with 2 elements
template <typename T1, typename T2> struct stack_pusher<std::pair<T1, T2>> {
  static int push(state_t *S, const std::pair<T1, T2> &p) {
    spt_newlist(S, 2);
    int idx = spt_gettop(S);
    stack_pusher<T1>::push(S, p.first);
    spt_listappend(S, idx);
    stack_pusher<T2>::push(S, p.second);
    spt_listappend(S, idx);
    return 1;
  }
};

// std::tuple -> SPT list
template <typename... Ts> struct stack_pusher<std::tuple<Ts...>> {
  static int push(state_t *S, const std::tuple<Ts...> &t) {
    spt_newlist(S, static_cast<int>(sizeof...(Ts)));
    int idx = spt_gettop(S);
    push_elements(S, idx, t, std::make_index_sequence<sizeof...(Ts)>{});
    return 1;
  }

private:
  template <std::size_t... Is>
  static void push_elements(state_t *S, int idx, const std::tuple<Ts...> &t,
                            std::index_sequence<Is...>) {
    ((stack_pusher<std::tuple_element_t<Is, std::tuple<Ts...>>>::push(S, std::get<Is>(t)),
      spt_listappend(S, idx)),
     ...);
  }
};

// std::optional
template <typename T> struct stack_pusher<std::optional<T>> {
  static int push(state_t *S, const std::optional<T> &opt) {
    if (opt.has_value()) {
      return stack_pusher<T>::push(S, *opt);
    } else {
      spt_pushnil(S);
      return 1;
    }
  }
};

// ============================================================================
// Stack Getter - Get C++ values from SPT stack
// ============================================================================

template <typename T, typename> struct stack_getter {
  static_assert(sizeof(T) == 0, "No stack_getter specialization for this type");
};

// nil_t
template <> struct stack_getter<nil_t> {
  static nil_t get(state_t *S, int idx) {
    (void)S;
    (void)idx;
    return nil;
  }
};

// bool
template <> struct stack_getter<bool> {
  static bool get(state_t *S, int idx) { return spt_tobool(S, idx) != 0; }
};

// Integer types
template <typename T> struct stack_getter<T, std::enable_if_t<is_integer_v<T>>> {
  static T get(state_t *S, int idx) { return static_cast<T>(spt_toint(S, idx)); }
};

// Floating point types
template <typename T> struct stack_getter<T, std::enable_if_t<is_floating_v<T>>> {
  static T get(state_t *S, int idx) { return static_cast<T>(spt_tofloat(S, idx)); }
};

// const char*
template <> struct stack_getter<const char *> {
  static const char *get(state_t *S, int idx) { return spt_tostring(S, idx, nullptr); }
};

// std::string
template <> struct stack_getter<std::string> {
  static std::string get(state_t *S, int idx) {
    size_t len = 0;
    const char *s = spt_tostring(S, idx, &len);
    if (s == nullptr) {
      return std::string{};
    }
    return std::string(s, len);
  }
};

// std::string_view
template <> struct stack_getter<std::string_view> {
  static std::string_view get(state_t *S, int idx) {
    size_t len = 0;
    const char *s = spt_tostring(S, idx, &len);
    if (s == nullptr) {
      return std::string_view{};
    }
    return std::string_view(s, len);
  }
};

// void* (light userdata or cinstance)
template <> struct stack_getter<void *> {
  static void *get(state_t *S, int idx) {
    if (spt_islightuserdata(S, idx)) {
      return spt_tolightuserdata(S, idx);
    }
    if (spt_iscinstance(S, idx)) {
      return spt_tocinstance(S, idx);
    }
    return nullptr;
  }
};

// std::vector from SPT list
template <typename T, typename Alloc> struct stack_getter<std::vector<T, Alloc>> {
  static std::vector<T, Alloc> get(state_t *S, int idx) {
    std::vector<T, Alloc> result;

    if (!spt_islist(S, idx)) {
      return result;
    }

    int len = spt_listlen(S, idx);
    result.reserve(static_cast<size_t>(len));

    for (int i = 0; i < len; ++i) {
      spt_listgeti(S, idx, i);
      result.push_back(stack_getter<T>::get(S, -1));
      spt_pop(S, 1);
    }

    return result;
  }
};

// std::map from SPT map
template <typename K, typename V, typename Comp, typename Alloc>
struct stack_getter<std::map<K, V, Comp, Alloc>> {
  static std::map<K, V, Comp, Alloc> get(state_t *S, int idx) {
    std::map<K, V, Comp, Alloc> result;

    if (!spt_ismap(S, idx)) {
      return result;
    }

    idx = spt_absindex(S, idx);

    spt_pushnil(S);
    while (spt_mapnext(S, idx)) {
      K key = stack_getter<K>::get(S, -2);
      V value = stack_getter<V>::get(S, -1);
      result[std::move(key)] = std::move(value);
      spt_pop(S, 1);
    }

    return result;
  }
};

// std::unordered_map from SPT map
template <typename K, typename V, typename Hash, typename Eq, typename Alloc>
struct stack_getter<std::unordered_map<K, V, Hash, Eq, Alloc>> {
  static std::unordered_map<K, V, Hash, Eq, Alloc> get(state_t *S, int idx) {
    std::unordered_map<K, V, Hash, Eq, Alloc> result;

    if (!spt_ismap(S, idx)) {
      return result;
    }

    idx = spt_absindex(S, idx);

    spt_pushnil(S);
    while (spt_mapnext(S, idx)) {
      K key = stack_getter<K>::get(S, -2);
      V value = stack_getter<V>::get(S, -1);
      result[std::move(key)] = std::move(value);
      spt_pop(S, 1);
    }

    return result;
  }
};

// std::pair from SPT list
template <typename T1, typename T2> struct stack_getter<std::pair<T1, T2>> {
  static std::pair<T1, T2> get(state_t *S, int idx) {
    if (!spt_islist(S, idx)) {
      return std::pair<T1, T2>{};
    }

    spt_listgeti(S, idx, 0);
    T1 first = stack_getter<T1>::get(S, -1);
    spt_pop(S, 1);

    spt_listgeti(S, idx, 1);
    T2 second = stack_getter<T2>::get(S, -1);
    spt_pop(S, 1);

    return std::make_pair(std::move(first), std::move(second));
  }
};

// std::tuple from SPT list
template <typename... Ts> struct stack_getter<std::tuple<Ts...>> {
  static std::tuple<Ts...> get(state_t *S, int idx) {
    return get_elements(S, idx, std::make_index_sequence<sizeof...(Ts)>{});
  }

private:
  template <std::size_t... Is>
  static std::tuple<Ts...> get_elements(state_t *S, int idx, std::index_sequence<Is...>) {
    return std::make_tuple(get_element<Is, std::tuple_element_t<Is, std::tuple<Ts...>>>(S, idx)...);
  }

  template <std::size_t I, typename T> static T get_element(state_t *S, int idx) {
    spt_listgeti(S, idx, static_cast<int>(I));
    T result = stack_getter<T>::get(S, -1);
    spt_pop(S, 1);
    return result;
  }
};

// std::optional
template <typename T> struct stack_getter<std::optional<T>> {
  static std::optional<T> get(state_t *S, int idx) {
    if (spt_isnoneornil(S, idx)) {
      return std::nullopt;
    }
    return stack_getter<T>::get(S, idx);
  }
};

// ============================================================================
// Stack Checker - Type checking
// ============================================================================

template <typename T, typename> struct stack_checker {
  static bool check(state_t *S, int idx) {
    return spt_iscinstance(S, idx);
  }
};

template <> struct stack_checker<nil_t> {
  static bool check(state_t *S, int idx) { return spt_isnil(S, idx); }
};

template <> struct stack_checker<bool> {
  static bool check(state_t *S, int idx) { return spt_isbool(S, idx); }
};

template <typename T> struct stack_checker<T, std::enable_if_t<is_integer_v<T>>> {
  static bool check(state_t *S, int idx) { return spt_isint(S, idx); }
};

template <typename T> struct stack_checker<T, std::enable_if_t<is_floating_v<T>>> {
  static bool check(state_t *S, int idx) { return spt_isfloat(S, idx) || spt_isint(S, idx); }
};

template <> struct stack_checker<std::string> {
  static bool check(state_t *S, int idx) { return spt_isstring(S, idx); }
};

template <> struct stack_checker<std::string_view> {
  static bool check(state_t *S, int idx) { return spt_isstring(S, idx); }
};

template <> struct stack_checker<const char *> {
  static bool check(state_t *S, int idx) { return spt_isstring(S, idx); }
};

template <typename T, typename Alloc> struct stack_checker<std::vector<T, Alloc>> {
  static bool check(state_t *S, int idx) { return spt_islist(S, idx); }
};

template <typename K, typename V, typename Comp, typename Alloc>
struct stack_checker<std::map<K, V, Comp, Alloc>> {
  static bool check(state_t *S, int idx) { return spt_ismap(S, idx); }
};

template <typename K, typename V, typename Hash, typename Eq, typename Alloc>
struct stack_checker<std::unordered_map<K, V, Hash, Eq, Alloc>> {
  static bool check(state_t *S, int idx) { return spt_ismap(S, idx); }
};

template <typename T> struct stack_checker<std::optional<T>> {
  static bool check(state_t *S, int idx) {
    return spt_isnoneornil(S, idx) || stack_checker<T>::check(S, idx);
  }
};

// ============================================================================
// Helper Functions
// ============================================================================

namespace stack {

// Push any value
template <typename T> inline int push(state_t *S, T &&value) {
  using clean_type = detail::remove_cvref_t<T>;
  return stack_pusher<clean_type>::push(S, std::forward<T>(value));
}

// Push multiple values
template <typename... Ts> inline int push_all(state_t *S, Ts &&...values) {
  if constexpr (sizeof...(Ts) == 0) {
    return 0;
  } else {
    return (push(S, std::forward<Ts>(values)) + ...);
  }
}

// Get value at index
template <typename T> inline auto get(state_t *S, int idx) {
  using clean_type = detail::remove_cvref_t<T>;
  return stack_getter<clean_type>::get(S, idx);
}

// Check type at index
template <typename T> inline bool check(state_t *S, int idx) {
  using clean_type = detail::remove_cvref_t<T>;
  return stack_checker<clean_type>::check(S, idx);
}

// Get with type check
template <typename T> inline std::optional<T> get_if(state_t *S, int idx) {
  if (check<T>(S, idx)) {
    return get<T>(S, idx);
  }
  return std::nullopt;
}

// Remove element at index
inline void remove(state_t *S, int idx) { spt_remove(S, idx); }

// Insert top at index
inline void insert(state_t *S, int idx) { spt_insert(S, idx); }

// Replace element at index with top
inline void replace(state_t *S, int idx) { spt_replace(S, idx); }

// Copy element
inline void copy(state_t *S, int from, int to) { spt_copy(S, from, to); }

// Rotate stack elements
inline void rotate(state_t *S, int idx, int n) { spt_rotate(S, idx, n); }

// Move values between states
inline void xmove(state_t *from, state_t *to, int n) { spt_xmove(from, to, n); }

} // namespace stack

SPTXX_NAMESPACE_END

#endif // SPTXX_STACK_HPP