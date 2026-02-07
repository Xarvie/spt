#ifndef SPTXX_FUNCTION_HPP
#define SPTXX_FUNCTION_HPP

#include "collections.hpp"

#include <atomic>
#include <memory>
#include <tuple>
#include <unordered_map>
#include <utility>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Function Argument Helper
// ============================================================================

namespace detail {

// Extract arguments from stack
template <typename... Args, std::size_t... Is>
auto get_args_impl(state_t *S, int start, std::index_sequence<Is...>) {
  return std::make_tuple(stack::get<Args>(S, start + static_cast<int>(Is))...);
}

template <typename... Args> auto get_args(state_t *S, int start = 1) {
  return get_args_impl<Args...>(S, start, std::make_index_sequence<sizeof...(Args)>{});
}

// Empty args case
inline auto get_args(state_t *, int, std::index_sequence<>) { return std::tuple<>(); }

// Call function with tuple of arguments
template <typename F, typename Tuple, std::size_t... Is>
auto apply_impl(F &&f, Tuple &&t, std::index_sequence<Is...>)
    -> decltype(f(std::get<Is>(std::forward<Tuple>(t))...)) {
  return f(std::get<Is>(std::forward<Tuple>(t))...);
}

template <typename F, typename Tuple> auto tuple_apply(F &&f, Tuple &&t) {
  constexpr auto size = std::tuple_size_v<std::remove_reference_t<Tuple>>;
  return apply_impl(std::forward<F>(f), std::forward<Tuple>(t), std::make_index_sequence<size>{});
}

// Special case for empty tuple
template <typename F> auto tuple_apply(F &&f, std::tuple<> &) { return f(); }

template <typename F> auto tuple_apply(F &&f, std::tuple<> &&) { return f(); }

// Handle return values
template <typename T> int push_return(state_t *S, T &&value) {
  return stack::push(S, std::forward<T>(value));
}

inline int push_return(state_t *, std::tuple<> &) { return 0; }

inline int push_return(state_t *, std::tuple<> &&) { return 0; }

template <typename... Ts, std::size_t... Is>
int push_tuple_elements(state_t *S, std::tuple<Ts...> &t, std::index_sequence<Is...>) {
  if constexpr (sizeof...(Ts) == 0) {
    (void)S;
    (void)t;
    return 0;
  } else {
    return (stack::push(S, std::get<Is>(t)) + ...);
  }
}

template <typename... Ts> int push_return(state_t *S, std::tuple<Ts...> &t) {
  return push_tuple_elements(S, t, std::make_index_sequence<sizeof...(Ts)>{});
}

template <typename... Ts> int push_return(state_t *S, std::tuple<Ts...> &&t) {
  return push_tuple_elements(S, t, std::make_index_sequence<sizeof...(Ts)>{});
}

// Filter out special types from argument list
template <typename T>
struct is_special_arg : std::disjunction<std::is_same<remove_cvref_t<T>, this_state>,
                                         std::is_same<remove_cvref_t<T>, variadic_args>> {};

template <typename T> inline constexpr bool is_special_arg_v = is_special_arg<T>::value;

// Get actual argument value (handle special types)
template <typename T> auto get_arg_value(state_t *S, int &idx) {
  using clean_type = remove_cvref_t<T>;

  if constexpr (std::is_same_v<clean_type, this_state>) {
    return this_state(S);
  } else if constexpr (std::is_same_v<clean_type, variadic_args>) {
    int top = spt_gettop(S);
    int count = top - idx + 1;
    int start = idx;
    idx = top + 1; // Consume all remaining args
    return variadic_args(S, start, count);
  } else {
    return stack::get<T>(S, idx++);
  }
}

// Count non-special arguments (fixed for empty parameter pack)
template <typename... Args> constexpr std::size_t count_real_args() {
  if constexpr (sizeof...(Args) == 0) {
    return 0;
  } else {
    return ((is_special_arg_v<Args> ? 0 : 1) + ...);
  }
}

// ============================================================================
// Helper: Extract self pointer from stack index 1 (CInstance or LightUserData)
//
// FIX: Unified self-extraction logic. Previous version had inconsistent
//      handling between member_function_wrapper and const_member_function_wrapper
//      (const variant was missing lightuserdata fallback, causing crashes when
//       objects were passed as light userdata).
// ============================================================================

template <typename C> C *extract_self(state_t *S) {
  if (spt_iscinstance(S, 1)) {
    return static_cast<C *>(spt_tocinstance(S, 1));
  }
  if (spt_islightuserdata(S, 1)) {
    return static_cast<C *>(spt_tolightuserdata(S, 1));
  }
  return nullptr;
}

template <typename C> const C *extract_const_self(state_t *S) {
  return const_cast<const C *>(extract_self<C>(S));
}

// Wrapper for free functions
template <typename R, typename... Args> struct function_wrapper {
  using func_type = R (*)(Args...);
  func_type func;

  explicit function_wrapper(func_type f) : func(f) {}

  int operator()(state_t *S) const {
    if constexpr (sizeof...(Args) == 0) {
      if constexpr (std::is_void_v<R>) {
        func();
        return 0;
      } else {
        auto result = func();
        return push_return(S, result);
      }
    } else {
      int idx = 1;
      auto args = std::make_tuple(get_arg_value<Args>(S, idx)...);

      if constexpr (std::is_void_v<R>) {
        tuple_apply(func, args);
        return 0;
      } else {
        auto result = tuple_apply(func, args);
        return push_return(S, result);
      }
    }
  }
};

// Specialization for functions with no arguments
template <typename R> struct function_wrapper<R> {
  using func_type = R (*)();
  func_type func;

  explicit function_wrapper(func_type f) : func(f) {}

  int operator()(state_t *S) const {
    if constexpr (std::is_void_v<R>) {
      func();
      (void)S;
      return 0;
    } else {
      auto result = func();
      return push_return(S, result);
    }
  }
};

// Wrapper for member functions
//
// FIX: Uses unified extract_self<C> helper instead of inline casts.
template <typename R, typename C, typename... Args> struct member_function_wrapper {
  using func_type = R (C::*)(Args...);
  func_type func;

  explicit member_function_wrapper(func_type f) : func(f) {}

  int operator()(state_t *S) const {
    C *self = extract_self<C>(S);
    if (!self) {
      spt_error(S, "invalid self: expected CInstance or LightUserData at index 1, got %s",
                spt_typename(S, spt_type(S, 1)));
      return 0;
    }
    if constexpr (sizeof...(Args) == 0) {
      if constexpr (std::is_void_v<R>) {
        (self->*func)();
        return 0;
      } else {
        auto result = (self->*func)();
        return push_return(S, result);
      }
    } else {
      int idx = 2; // Start after self
      auto args = std::make_tuple(get_arg_value<Args>(S, idx)...);

      if constexpr (std::is_void_v<R>) {
        tuple_apply([this, self](auto &&...a) { (self->*func)(std::forward<decltype(a)>(a)...); },
                    args);
        return 0;
      } else {
        auto result = tuple_apply(
            [this, self](auto &&...a) { return (self->*func)(std::forward<decltype(a)>(a)...); },
            args);
        return push_return(S, result);
      }
    }
  }
};

// Wrapper for const member functions
//
// FIX: Now uses extract_const_self<C> which includes lightuserdata fallback.
//      Previous version only checked spt_tocinstance, causing nullptr dereference
//      when called on light userdata objects.
template <typename R, typename C, typename... Args> struct const_member_function_wrapper {
  using func_type = R (C::*)(Args...) const;
  func_type func;

  explicit const_member_function_wrapper(func_type f) : func(f) {}

  int operator()(state_t *S) const {
    const C *self = extract_const_self<C>(S);
    if (!self) {
      spt_error(S, "invalid self: expected CInstance or LightUserData at index 1, got %s",
                spt_typename(S, spt_type(S, 1)));
      return 0;
    }

    if constexpr (sizeof...(Args) == 0) {
      if constexpr (std::is_void_v<R>) {
        (self->*func)();
        return 0;
      } else {
        auto result = (self->*func)();
        return push_return(S, result);
      }
    } else {
      int idx = 2;
      auto args = std::make_tuple(get_arg_value<Args>(S, idx)...);

      if constexpr (std::is_void_v<R>) {
        tuple_apply([this, self](auto &&...a) { (self->*func)(std::forward<decltype(a)>(a)...); },
                    args);
        return 0;
      } else {
        auto result = tuple_apply(
            [this, self](auto &&...a) { return (self->*func)(std::forward<decltype(a)>(a)...); },
            args);
        return push_return(S, result);
      }
    }
  }
};

// Wrapper for lambdas and functors
template <typename F> struct functor_wrapper {
  F func;

  explicit functor_wrapper(F f) : func(std::move(f)) {}

  int operator()(state_t *S) const {
    using traits = function_traits<F>;
    using return_type = typename traits::return_type;
    return call_impl<return_type>(S, std::make_index_sequence<traits::arity>{});
  }

private:
  template <typename R, std::size_t... Is>
  int call_impl(state_t *S, std::index_sequence<Is...>) const {
    if constexpr (sizeof...(Is) == 0) {
      if constexpr (std::is_void_v<R>) {
        func();
        (void)S;
        return 0;
      } else {
        auto result = func();
        return push_return(S, result);
      }
    } else {
      int idx = 1;
      using traits = function_traits<F>;
      using args_tuple = typename traits::args_tuple;

      auto args = std::tuple<std::tuple_element_t<Is, args_tuple>...>(
          get_arg_value<std::tuple_element_t<Is, args_tuple>>(S, idx)...);

      if constexpr (std::is_void_v<R>) {
        tuple_apply(func, args);
        return 0;
      } else {
        auto result = tuple_apply(func, args);
        return push_return(S, result);
      }
    }
  }
};

// ============================================================================
// Static Function Storage System
// ============================================================================

// Type-erased function storage base
struct func_storage_base {
  virtual ~func_storage_base() = default;
  virtual int call(state_t *S) = 0;
};

template <typename Wrapper> struct func_storage : func_storage_base {
  Wrapper wrapper;

  explicit func_storage(Wrapper w) : wrapper(std::move(w)) {}

  int call(state_t *S) override { return wrapper(S); }
};

// Unique ID generator for function registrations
inline std::atomic<std::uintptr_t> func_id_counter{1};

inline std::uintptr_t generate_func_id() {
  return func_id_counter.fetch_add(1, std::memory_order_relaxed);
}

// Registry for function wrappers keyed by unique ID
struct func_wrapper_registry {
  static inline std::unordered_map<std::uintptr_t, std::unique_ptr<func_storage_base>> storage;

  template <typename Wrapper> static std::uintptr_t register_wrapper(Wrapper wrapper) {
    auto id = generate_func_id();
    storage[id] = std::make_unique<func_storage<Wrapper>>(std::move(wrapper));
    return id;
  }

  static func_storage_base *get(std::uintptr_t id) {
    auto it = storage.find(id);
    return it != storage.end() ? it->second.get() : nullptr;
  }
};

// Static holder for wrapper instances - used for unique wrapper types
template <typename Wrapper> struct static_func_holder {
  static inline Wrapper *ptr = nullptr;
};

// Typed dispatcher that uses static storage
template <typename Wrapper> inline int typed_func_dispatcher(state_t *S) {
  Wrapper *wrapper = static_func_holder<Wrapper>::ptr;
  if (!wrapper) {
    spt_error(S, "invalid function binding - wrapper is null");
    return 0;
  }

#if defined(SPTXX_EXCEPTIONS_ENABLED)
  try {
    return (*wrapper)(S);
  } catch (const std::exception &e) {
    spt_error(S, "%s", e.what());
    return 0;
  } catch (...) {
    spt_error(S, "unknown C++ exception");
    return 0;
  }
#else
  return (*wrapper)(S);
#endif
}

// Proper dispatcher using Upvalue access
inline int generic_cfunc_dispatcher(state_t *S) {
  void *ptr = spt_tocinstance(S, SPT_UPVALUEINDEX(1));
  if (!ptr) {
    spt_error(S, "invalid function binding");
    return 0;
  }

  auto *storage = static_cast<func_storage_base *>(ptr);

#if defined(SPTXX_EXCEPTIONS_ENABLED)
  try {
    return storage->call(S);
  } catch (const std::exception &e) {
    spt_error(S, "%s", e.what());
    return 0;
  } catch (...) {
    spt_error(S, "unknown C++ exception");
    return 0;
  }
#else
  return storage->call(S);
#endif
}

// GC destructor for function storage
inline int func_storage_gc(state_t *S) {
  void *ptr = spt_tocinstance(S, 1);
  if (ptr) {
    auto *storage = static_cast<func_storage_base *>(ptr);
    storage->~func_storage_base();
    // The memory was allocated with std::malloc in spt_newcinstance,
    // so we must free it here after calling the destructor.
    // Note: If the VM already frees NativeInstance::data, this may double-free.
    // In that case, only the destructor call above is needed and this free
    // should be removed. Keeping it for safety since NativeInstance with
    // nullptr class has no default free path.
    std::free(ptr);
  }
  return 0;
}

// Lazily create a class with __gc for func_storage cinstances, stored in the registry.
inline void ensure_func_storage_class(state_t *S) {
  spt_getfield(S, registry_index, "__sptxx_func_storage_class");
  if (!spt_isnoneornil(S, -1)) {
    spt_pop(S, 1); // already created
    return;
  }
  spt_pop(S, 1); // pop nil

  // Create a new class and set its __gc
  spt_newclass(S, "__FuncStorage");
  int class_idx = spt_gettop(S);
  spt_pushcfunction(S, func_storage_gc);
  spt_setmagicmethod(S, class_idx, SPT_MM_GC);

  // Store in registry for reuse
  spt_pushvalue(S, class_idx);
  spt_setfield(S, registry_index, "__sptxx_func_storage_class");

  // Remove class from stack
  spt_remove(S, class_idx);
}

} // namespace detail

// ============================================================================
// Function - Callable wrapper
// ============================================================================

class function {
public:
  function() noexcept = default;

  // Construct from stack index
  function(state_t *S, int index) : ref_(S, index) {
#if defined(SPTXX_DEBUG_MODE)
    SPTXX_ASSERT(spt_isfunction(S, index) || spt_isnoneornil(S, index), "Expected function type");
#endif
  }

  // Construct from reference
  explicit function(reference &&ref) noexcept : ref_(std::move(ref)) {}

  // Move operations
  function(function &&) noexcept = default;
  function &operator=(function &&) noexcept = default;

  // Copy creates new reference
  function(const function &other) : ref_(other.ref_.copy()) {}

  function &operator=(const function &other) {
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

  // Call the function (raw, may throw on error)
  template <typename... Args> void operator()(Args &&...args) const {
    call_impl<void>(std::forward<Args>(args)...);
  }

  // Call with explicit return type
  template <typename R, typename... Args> R call(Args &&...args) const {
    return call_impl<R>(std::forward<Args>(args)...);
  }

  // Push onto stack
  void push() const { ref_.push(); }

  // Get arity
  SPTXX_NODISCARD int arity() const {
    if (!valid())
      return 0;
    stack_guard guard(state());
    push();
    return spt_getarity(state(), -1);
  }

  // Is C function?
  SPTXX_NODISCARD bool is_c_function() const {
    if (!valid())
      return false;
    stack_guard guard(state());
    push();
    return spt_iscfunction(state(), -1) != 0;
  }

  // Get reference
  SPTXX_NODISCARD const reference &get_ref() const noexcept { return ref_; }

private:
  template <typename R, typename... Args> R call_impl(Args &&...args) const {
    state_t *S = state();
    int top_before = spt_gettop(S);

    push();
    int nargs = 0;
    if constexpr (sizeof...(Args) > 0) {
      nargs = stack::push_all(S, std::forward<Args>(args)...);
    }

    int result = spt_call(S, nargs, std::is_void_v<R> ? 0 : 1);
    if (result != SPT_OK) {
      const char *err = spt_getlasterror(S);
      spt_settop(S, top_before);
#if defined(SPTXX_EXCEPTIONS_ENABLED)
      throw runtime_error(err ? err : "function call failed");
#endif
    }

    if constexpr (!std::is_void_v<R>) {
      R ret = stack::get<R>(S, -1);
      spt_pop(S, 1);
      return ret;
    }
  }

  reference ref_;
};

// ============================================================================
// Protected Function - Safe callable with error handling
// ============================================================================

class protected_function {
public:
  protected_function() noexcept = default;

  // Construct from stack index
  protected_function(state_t *S, int index) : ref_(S, index) {}

  // Construct from reference
  explicit protected_function(reference &&ref) noexcept : ref_(std::move(ref)) {}

  // Construct from function
  explicit protected_function(const function &f) {
    if (f.valid()) {
      f.push();
      ref_ = reference(f.state());
    }
  }

  // Move/copy
  protected_function(protected_function &&) noexcept = default;
  protected_function &operator=(protected_function &&) noexcept = default;

  protected_function(const protected_function &other) : ref_(other.ref_.copy()) {}

  protected_function &operator=(const protected_function &other) {
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

  // Call the function (returns result object)
  template <typename... Args> protected_function_result operator()(Args &&...args) const {
    return call(std::forward<Args>(args)...);
  }

  template <typename... Args> protected_function_result call(Args &&...args) const {
    if (!valid()) {
      return protected_function_result(nullptr, 0, 0, status::runtime);
    }

    state_t *S = state();
    int top_before = spt_gettop(S);

    push();
    int nargs = 0;
    if constexpr (sizeof...(Args) > 0) {
      nargs = stack::push_all(S, std::forward<Args>(args)...);
    }

    int result = spt_pcall(S, nargs, multi_return, error_handler_);
    status stat = static_cast<status>(result);

    int top_after = spt_gettop(S);
    int ret_count = top_after - top_before;

    return protected_function_result(S, top_before + 1, ret_count, stat);
  }

  // Call with specific return count
  template <typename... Args> protected_function_result call_n(int nresults, Args &&...args) const {
    if (!valid()) {
      return protected_function_result(nullptr, 0, 0, status::runtime);
    }

    state_t *S = state();
    int top_before = spt_gettop(S);

    push();
    int nargs = 0;
    if constexpr (sizeof...(Args) > 0) {
      nargs = stack::push_all(S, std::forward<Args>(args)...);
    }

    int result = spt_pcall(S, nargs, nresults, error_handler_);
    status stat = static_cast<status>(result);

    int top_after = spt_gettop(S);
    int ret_count = top_after - top_before;

    return protected_function_result(S, top_before + 1, ret_count, stat);
  }

  // Set error handler
  void set_error_handler(int index) { error_handler_ = index; }

  // Push onto stack
  void push() const { ref_.push(); }

  // Get reference
  SPTXX_NODISCARD const reference &get_ref() const noexcept { return ref_; }

private:
  reference ref_;
  int error_handler_ = 0;
};

// ============================================================================
// Stack Pusher/Getter for function types
// ============================================================================

template <> struct stack_pusher<function> {
  static int push(state_t *S, const function &f) {
    if (f.valid()) {
      f.push();
    } else {
      spt_pushnil(S);
    }
    return 1;
  }
};

template <> struct stack_getter<function> {
  static function get(state_t *S, int idx) { return function(S, idx); }
};

template <> struct stack_checker<function> {
  static bool check(state_t *S, int idx) { return spt_isfunction(S, idx); }
};

template <> struct stack_pusher<protected_function> {
  static int push(state_t *S, const protected_function &f) {
    if (f.valid()) {
      f.push();
    } else {
      spt_pushnil(S);
    }
    return 1;
  }
};

template <> struct stack_getter<protected_function> {
  static protected_function get(state_t *S, int idx) { return protected_function(S, idx); }
};

template <> struct stack_checker<protected_function> {
  static bool check(state_t *S, int idx) { return spt_isfunction(S, idx); }
};

// ============================================================================
// Function Binding
// ============================================================================

// Wrap a free function
template <typename R, typename... Args> auto wrap(R (*f)(Args...)) {
  return detail::function_wrapper<R, Args...>(f);
}

// Wrap a member function
template <typename R, typename C, typename... Args> auto wrap(R (C::*f)(Args...)) {
  return detail::member_function_wrapper<R, C, Args...>(f);
}

// Wrap a const member function
template <typename R, typename C, typename... Args> auto wrap(R (C::*f)(Args...) const) {
  return detail::const_member_function_wrapper<R, C, Args...>(f);
}

// Wrap a noexcept member function
template <typename R, typename C, typename... Args> auto wrap(R (C::*f)(Args...) noexcept) {
  return detail::member_function_wrapper<R, C, Args...>(reinterpret_cast<R (C::*)(Args...)>(f));
}

// Wrap a const noexcept member function
template <typename R, typename C, typename... Args> auto wrap(R (C::*f)(Args...) const noexcept) {
  return detail::const_member_function_wrapper<R, C, Args...>(
      reinterpret_cast<R (C::*)(Args...) const>(f));
}

// Wrap a lambda/functor
template <typename F,
          typename = std::enable_if_t<detail::is_callable_v<std::decay_t<F>> &&
                                      !std::is_function_v<std::remove_pointer_t<std::decay_t<F>>>>>
auto wrap(F &&f) {
  return detail::functor_wrapper<std::decay_t<F>>(std::forward<F>(f));
}

// ============================================================================
// Function Result Helper
// ============================================================================

template <typename... Ts> struct returns {
  std::tuple<Ts...> values;

  returns(Ts... vs) : values(std::move(vs)...) {}

  template <std::size_t I> auto &get() { return std::get<I>(values); }

  template <std::size_t I> const auto &get() const { return std::get<I>(values); }

  static constexpr std::size_t size() { return sizeof...(Ts); }
};

template <typename... Ts> returns<std::decay_t<Ts>...> make_returns(Ts &&...values) {
  return returns<std::decay_t<Ts>...>(std::forward<Ts>(values)...);
}

// Push returns tuple
template <typename... Ts> struct stack_pusher<returns<Ts...>> {
  static int push(state_t *S, const returns<Ts...> &r) {
    if constexpr (sizeof...(Ts) == 0) {
      (void)S;
      (void)r;
      return 0;
    } else {
      return push_impl(S, r.values, std::make_index_sequence<sizeof...(Ts)>{});
    }
  }

private:
  template <std::size_t... Is>
  static int push_impl(state_t *S, const std::tuple<Ts...> &t, std::index_sequence<Is...>) {
    return (stack::push(S, std::get<Is>(t)) + ...);
  }
};

// ============================================================================
// Yielding Functions
// ============================================================================

template <typename F> struct yielding_wrapper {
  F func;

  explicit yielding_wrapper(F f) : func(std::move(f)) {}
};

template <typename F> yielding_wrapper<std::decay_t<F>> as_yielding(F &&f) {
  return yielding_wrapper<std::decay_t<F>>(std::forward<F>(f));
}

// ============================================================================
// Variadic Arguments Helper
// ============================================================================

class variadic_results {
public:
  explicit variadic_results(state_t *S) : S_(S), count_(0) {}

  template <typename T> variadic_results &push(T &&value) {
    count_ += stack::push(S_, std::forward<T>(value));
    return *this;
  }

  template <typename... Ts> variadic_results &push_all(Ts &&...values) {
    if constexpr (sizeof...(Ts) > 0) {
      count_ += stack::push_all(S_, std::forward<Ts>(values)...);
    }
    return *this;
  }

  SPTXX_NODISCARD int count() const noexcept { return count_; }

private:
  state_t *S_;
  int count_;
};

// For use in C functions
inline variadic_results make_variadic_results(state_t *S) { return variadic_results(S); }

// ============================================================================
// C Function Registration Helper
// ============================================================================

template <auto Func> struct static_function {
  static int invoke(state_t *S) {
    auto wrapper = wrap(Func);
    return wrapper(S);
  }

  static constexpr cfunction_t get() { return &invoke; }
};

template <auto Func> constexpr cfunction_t make_cfunction() { return static_function<Func>::get(); }

SPTXX_NAMESPACE_END

#endif // SPTXX_FUNCTION_HPP