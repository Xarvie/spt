#ifndef SPTXX_HPP
#define SPTXX_HPP

// ============================================================================
// Include Order Matters!
// ============================================================================

// 1. Configuration and basic macros
#include "config.hpp"

// 2. Forward declarations and basic types
#include "forward.hpp"

// 3. Type traits and type conversion utilities
#include "types.hpp"

// 4. Stack operations (push/get values)
#include "stack.hpp"

// 5. Error handling and exceptions
#include "error.hpp"

// 6. Reference management (GC-safe references)
#include "reference.hpp"

// 7. Generic object wrapper
#include "object.hpp"

// 8. List and Map operations
#include "collections.hpp"

// 9. Function binding
#include "function.hpp"

// 10. State management
#include "state.hpp"

// 11. User type binding
#include "usertype.hpp"

// 12. Coroutine/Fiber support
#include "coroutine.hpp"

// ============================================================================
// Convenience Aliases
// ============================================================================

SPTXX_NAMESPACE_BEGIN

// State types
using lua_State = state_t; // For Lua-like naming compatibility

// Type tags for explicit type specification
template <typename T> struct as_type {
  using type = T;
};

// Helper to create objects with explicit type
template <typename T> constexpr as_type<T> as{};

// ============================================================================
// State Extensions
// ============================================================================

// Extension methods added to state_view via free functions

// Create a new usertype and return builder
template <typename T, typename State> usertype<T> new_usertype(State &&s, const char *name) {
  return usertype<T>(detail::get_state(std::forward<State>(s)), name);
}

// Create map from key-value pairs
template <typename... Pairs> map make_map(state_t *S, Pairs &&...pairs) {
  static_assert(sizeof...(Pairs) % 2 == 0, "make_map requires key-value pairs");

  auto m = map::create(S);
  if constexpr (sizeof...(Pairs) > 0) {
    make_map_impl(m, std::forward<Pairs>(pairs)...);
  }
  return m;
}

namespace detail {

template <typename Map, typename K, typename V, typename... Rest>
void make_map_impl(Map &m, K &&key, V &&value, Rest &&...rest) {
  m.set(std::forward<K>(key), std::forward<V>(value));
  if constexpr (sizeof...(Rest) > 0) {
    make_map_impl(m, std::forward<Rest>(rest)...);
  }
}

} // namespace detail

// ============================================================================
// Environment/Scope RAII Helper
// ============================================================================

class environment {
public:
  environment(state_t *S, const char *name) : S_(S), env_(map::create(S)) {
    // Store in registry
    env_.push();
    spt_setfield(S, registry_index, name);
  }

  // Set value in environment
  template <typename T> void set(const char *key, T &&value) {
    env_.set(key, std::forward<T>(value));
  }

  // Get value from environment
  template <typename T> T get(const char *key) const { return env_.template get<T>(key); }

  // Get underlying map
  SPTXX_NODISCARD map &get_map() { return env_; }

  SPTXX_NODISCARD const map &get_map() const { return env_; }

private:
  state_t *S_;
  map env_;
};

// ============================================================================
// Module Builder
// ============================================================================

class module_builder {
public:
  module_builder(state_t *S, const char *name) : S_(S), name_(name), exports_(map::create(S)) {}

  // Add function to module
  template <typename F> module_builder &add(const char *name, F &&func) {
    auto wrapper = wrap(std::forward<F>(func));

    using storage_type = detail::func_storage<decltype(wrapper)>;
    void *mem = spt_newcinstance(S_, sizeof(storage_type));
    new (mem) storage_type(std::move(wrapper));

    // Ensure the cinstance has a class with __gc so the destructor runs
    detail::ensure_func_storage_class(S_);
    int cinst_idx = spt_gettop(S_);
    spt_getfield(S_, registry_index, "__sptxx_func_storage_class");
    spt_setcclass(S_, cinst_idx);

    spt_pushcclosure(S_, detail::generic_cfunc_dispatcher, 1);
    exports_.set(name, function(S_, -1));
    spt_pop(S_, 1);

    return *this;
  }

  // Add constant to module
  template <typename T> module_builder &add_const(const char *name, T &&value) {
    exports_.set(name, std::forward<T>(value));
    return *this;
  }

  // Finish building and register as importable module
  void finish() {
    // Create C function array for spt_defmodule
    // For now, just set as global
    exports_.push();
    spt_setglobal(S_, name_.c_str());
  }

  // Get exports map
  SPTXX_NODISCARD map &exports() { return exports_; }

private:
  state_t *S_;
  std::string name_;
  map exports_;
};

// Create module builder
inline module_builder create_module(state_t *S, const char *name) {
  return module_builder(S, name);
}

inline module_builder create_module(state_view &s, const char *name) {
  return module_builder(s.raw(), name);
}

// ============================================================================
// Multiple Return Values Helper
// ============================================================================

template <typename... Ts> class multi {
public:
  multi(Ts... values) : values_(std::move(values)...) {}

  template <std::size_t I> auto &get() & { return std::get<I>(values_); }

  template <std::size_t I> const auto &get() const & { return std::get<I>(values_); }

  static constexpr std::size_t size() { return sizeof...(Ts); }

  const std::tuple<Ts...> &as_tuple() const { return values_; }

private:
  std::tuple<Ts...> values_;
};

template <typename... Ts> multi<std::decay_t<Ts>...> make_multi(Ts &&...values) {
  return multi<std::decay_t<Ts>...>(std::forward<Ts>(values)...);
}

template <typename... Ts> struct stack_pusher<multi<Ts...>> {
  static int push(state_t *S, const multi<Ts...> &m) {
    return push_impl(S, m.as_tuple(), std::make_index_sequence<sizeof...(Ts)>{});
  }

private:
  template <std::size_t... Is>
  static int push_impl(state_t *S, const std::tuple<Ts...> &t, std::index_sequence<Is...>) {
    if constexpr (sizeof...(Is) == 0) {
      return 0;
    } else {
      return (stack::push(S, std::get<Is>(t)) + ...);
    }
  }
};

// ============================================================================
// Readonly/Writeonly Property Helpers
// ============================================================================

template <typename Getter> struct readonly_property {
  Getter get;

  explicit readonly_property(Getter g) : get(std::move(g)) {}
};

template <typename Setter> struct writeonly_property {
  Setter set;

  explicit writeonly_property(Setter s) : set(std::move(s)) {}
};

template <typename Getter> auto make_readonly(Getter &&g) {
  return readonly_property<std::decay_t<Getter>>(std::forward<Getter>(g));
}

template <typename Setter> auto make_writeonly(Setter &&s) {
  return writeonly_property<std::decay_t<Setter>>(std::forward<Setter>(s));
}

// ============================================================================
// Destructor Registration Helper
// ============================================================================

template <typename T> struct destructor {
  void operator()(T *ptr) const { ptr->~T(); }
};

// ============================================================================
// Base Class Specification Helper
// ============================================================================

template <typename... Bases> struct base_classes {
  using type = std::tuple<Bases...>;
};

// ============================================================================
// Convenience Type Checkers
// ============================================================================

template <typename T> bool is_type(state_t *S, int idx) { return stack::check<T>(S, idx); }

template <typename T> bool is_type(const object &obj) { return obj.template is<T>(); }

// ============================================================================
// Protected Scope (for cleanup on scope exit)
// ============================================================================

class protected_scope {
public:
  explicit protected_scope(state_t *S) : S_(S), top_(spt_gettop(S)) {}

  ~protected_scope() {
    if (S_) {
      spt_settop(S_, top_);
    }
  }

  // Commit the scope (don't restore on destruction)
  void commit() { S_ = nullptr; }

  // Get number of new items
  SPTXX_NODISCARD int added() const { return S_ ? spt_gettop(S_) - top_ : 0; }

private:
  state_t *S_;
  int top_;
};

// ============================================================================
// Debug Utilities
// ============================================================================

namespace debug {

// Dump stack contents for debugging
inline void dump_stack(state_t *S) {
  int top = spt_gettop(S);
  std::printf("=== Stack Dump (top = %d) ===\n", top);

  for (int i = 1; i <= top; ++i) {
    int t = spt_type(S, i);
    std::printf("[%d] (%s): ", i, spt_typename(S, t));

    switch (t) {
    case SPT_TNIL:
      std::printf("nil\n");
      break;
    case SPT_TBOOL:
      std::printf("%s\n", spt_tobool(S, i) ? "true" : "false");
      break;
    case SPT_TINT:
      std::printf("%lld\n", static_cast<long long>(spt_toint(S, i)));
      break;
    case SPT_TFLOAT:
      std::printf("%g\n", spt_tofloat(S, i));
      break;
    case SPT_TSTRING: {
      size_t len;
      const char *str = spt_tostring(S, i, &len);
      std::printf("\"%.*s\"\n", static_cast<int>(len), str);
      break;
    }
    default:
      std::printf("<%p>\n", spt_topointer(S, i));
      break;
    }
  }
  std::printf("============================\n");
}

// Get type name at stack index
inline const char *type_name_at(state_t *S, int idx) { return spt_typename(S, spt_type(S, idx)); }

} // namespace debug

// ============================================================================
// Version Information
// ============================================================================

namespace version {

constexpr int major = SPTXX_VERSION_MAJOR;
constexpr int minor = SPTXX_VERSION_MINOR;
constexpr int patch = SPTXX_VERSION_PATCH;
constexpr const char *string = SPTXX_VERSION_STRING;
constexpr int number = SPTXX_VERSION_NUM;

// Get SPT C API version
inline const char *spt_version_string() { return spt_version(); }

inline int spt_version_number() { return spt_versionnum(); }

} // namespace version

SPTXX_NAMESPACE_END

#endif // SPTXX_HPP