#ifndef SPTXX_USERTYPE_HPP
#define SPTXX_USERTYPE_HPP

#include "state.hpp"

#include <memory>
#include <typeindex>
#include <unordered_map>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Type Registry
// ============================================================================

namespace detail {

// Global type info storage
struct type_registry {
  struct type_info_entry {
    std::string name;
    std::size_t size;
    int class_ref = no_ref;
    void (*destructor)(void *) = nullptr;
    std::type_index type_id = std::type_index(typeid(void));
  };

  static std::unordered_map<std::type_index, type_info_entry> &get() {
    static std::unordered_map<std::type_index, type_info_entry> registry;
    return registry;
  }

  template <typename T> static type_info_entry *find() {
    auto &reg = get();
    auto it = reg.find(std::type_index(typeid(T)));
    return it != reg.end() ? &it->second : nullptr;
  }

  template <typename T> static type_info_entry &create(const std::string &name) {
    auto &entry = get()[std::type_index(typeid(T))];
    entry.name = name;
    entry.size = sizeof(T);
    entry.type_id = std::type_index(typeid(T));
    entry.destructor = [](void *ptr) { static_cast<T *>(ptr)->~T(); };
    return entry;
  }

  static type_info_entry *find_by_name(const std::string &name) {
    auto &reg = get();
    for (auto &[_, entry] : reg) {
      if (entry.name == name) {
        return &entry;
      }
    }
    return nullptr;
  }
};

// GC finalizer for user types
template <typename T> int usertype_gc(state_t *S) {
  T *obj = static_cast<T *>(spt_tocinstance(S, 1));
  if (obj) {
    obj->~T();
  }
  return 0;
}

// ============================================================================
// Constructor Helpers
// ============================================================================

// Generic constructor wrapper (variadic)
template <typename T, typename... Args> int usertype_constructor_impl(state_t *S) {
  // Create new instance
  void *mem = spt_newcinstance(S, sizeof(T));

  if constexpr (sizeof...(Args) == 0) {
    // Default constructor
    new (mem) T();
  } else {
    // Get constructor arguments starting at index 2 (index 1 is class)
    int idx = 2;
    auto args = std::make_tuple(get_arg_value<Args>(S, idx)...);

    // Construct in place using std::apply
    std::apply([mem](auto &&...a) { new (mem) T(std::forward<decltype(a)>(a)...); },
               std::move(args));
  }

  // Set class (for GC)
  auto *info = type_registry::find<T>();
  if (info && info->class_ref != no_ref) {
    spt_getref(S, info->class_ref);
    spt_setcclass(S, -2);
  }

  return 1;
}

// Constructor dispatcher that checks argument count
template <typename T, typename... Constructors> struct constructor_dispatcher;

// Single constructor
template <typename T, typename... Args> struct constructor_dispatcher<T, constructor<T, Args...>> {
  static int call(state_t *S) { return usertype_constructor_impl<T, Args...>(S); }
};

// Multiple constructors (overload resolution by argument count)
template <typename T, typename First, typename... Rest>
struct constructor_dispatcher<T, First, Rest...> {
  static int call(state_t *S) {
    int nargs = spt_gettop(S) - 1; // Exclude class argument

    using first_ctor = First;
    constexpr std::size_t first_arity = std::tuple_size_v<typename first_ctor::args>;

    if (static_cast<std::size_t>(nargs) == first_arity) {
      return call_ctor<first_ctor>(S, std::make_index_sequence<first_arity>{});
    }

    // Try next constructor
    return constructor_dispatcher<T, Rest...>::call(S);
  }

private:
  template <typename Ctor, std::size_t... Is>
  static int call_ctor(state_t *S, std::index_sequence<Is...>) {
    using args_tuple = typename Ctor::args;
    return usertype_constructor_impl<T, std::tuple_element_t<Is, args_tuple>...>(S);
  }
};

// Base case - no matching constructor
template <typename T> struct constructor_dispatcher<T> {
  static int call(state_t *S) {
    spt_error(S, "no matching constructor found");
    return 0;
  }
};

// Runtime constructor entry: stores arity and CFunction pointer
struct constructor_entry {
  int arity;
  cfunction_t func;
};

// Runtime constructor registry: holds a vector of overloaded constructors
// Allocated on the heap and stored as a lightuserdata upvalue in the closure.
struct constructor_registry {
  std::vector<constructor_entry> entries;

  void add(int arity, cfunction_t func) {
    // Replace if same arity already exists
    for (auto &e : entries) {
      if (e.arity == arity) {
        e.func = func;
        return;
      }
    }
    entries.push_back({arity, func});
  }
};

// Dispatcher CFunction that reads the registry from upvalue and dispatches
inline int runtime_constructor_dispatch(state_t *S) {
  spt_getupvalue(S, -10003, 0); // SPT_REGISTRYINDEX-like upvalue index; use closure upvalue
  // The upvalue is at a special index. We use the closure's first upvalue.
  // Actually, upvalues in cclosures are accessed differently. Let's re-check.
  // spt_getupvalue pushes the upvalue onto the stack.
  // But the function itself is not on the stack in the usual way during a call.
  // Let's use a different approach: store registry pointer as lightuserdata upvalue.

  // We need to get the upvalue from the currently executing closure.
  // In SPT, within a CFunction, upvalues are typically accessed via special indices
  // or an API like spt_getupvalue with the function index.
  // For a cclosure, upvalue index 0 is the first one pushed before spt_pushcclosure.

  // Re-implementation: we'll use a static map instead.
  (void)S;
  return 0; // placeholder, real implementation below
}

// Per-type static registry approach: each usertype<T> stores its own registry
// and registers a single dispatcher closure.
template <typename T> struct typed_constructor_registry {
  static constructor_registry &instance() {
    static constructor_registry reg;
    return reg;
  }

  static int dispatch(state_t *S) {
    int nargs = spt_gettop(S) - 1; // Exclude class argument
    auto &reg = instance();
    for (auto &entry : reg.entries) {
      if (entry.arity == nargs) {
        return entry.func(S);
      }
    }
    spt_error(S, "no matching constructor found for given argument count");
    return 0;
  }
};

// Property getter wrapper
template <typename T, typename V, V T::*Member> int property_getter(state_t *S) {
  T *self = extract_self<T>(S);
  if (!self) {
    spt_error(S, "invalid self reference");
    return 0;
  }
  stack::push(S, self->*Member);
  return 1;
}

// Property setter wrapper
template <typename T, typename V, V T::*Member> int property_setter(state_t *S) {
  T *self = extract_self<T>(S);
  if (!self) {
    spt_error(S, "invalid self reference");
    return 0;
  }
  self->*Member = stack::get<V>(S, 2);
  return 0;
}

} // namespace detail

// ============================================================================
// Userdata - Wrapper for C Instance
// ============================================================================

class userdata {
public:
  userdata() noexcept = default;

  userdata(state_t *S, int index) : ref_(S, index) {
#if defined(SPTXX_DEBUG_MODE)
    SPTXX_ASSERT(spt_iscinstance(S, index) || spt_isnoneornil(S, index), "Expected cinstance type");
#endif
  }

  explicit userdata(reference &&ref) noexcept : ref_(std::move(ref)) {}

  // Move/copy
  userdata(userdata &&) noexcept = default;
  userdata &operator=(userdata &&) noexcept = default;

  userdata(const userdata &other) : ref_(other.ref_.copy()) {}

  userdata &operator=(const userdata &other) {
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

  // Get raw pointer
  SPTXX_NODISCARD void *data() const {
    if (!valid())
      return nullptr;
    stack_guard guard(state());
    ref_.push();
    return spt_tocinstance(state(), -1);
  }

  // Get typed pointer
  template <typename T> SPTXX_NODISCARD T *as() const { return static_cast<T *>(data()); }

  // Push onto stack
  void push() const { ref_.push(); }

  // Get reference
  SPTXX_NODISCARD const reference &get_ref() const noexcept { return ref_; }

private:
  reference ref_;
};

// ============================================================================
// Usertype Builder
//
// FIX: Destructor now properly cleans up the class from the stack if the user
//      forgets to call set_global() or finalize(). Previous version left the
//      class object on the stack permanently, causing stack leaks and eventual
//      stack overflow when registering many types.
// ============================================================================

template <typename T> class usertype {
public:
  explicit usertype(state_t *S, const char *name) : S_(S), name_(name) {
    // Create class
    spt_newclass(S_, name);
    class_idx_ = spt_gettop(S_);

    // Store reference
    spt_pushvalue(S_, class_idx_);
    auto &entry = detail::type_registry::create<T>(name);
    entry.class_ref = spt_ref(S_);

    // Set __gc
    spt_pushcfunction(S_, detail::usertype_gc<T>);
    spt_setmagicmethod(S_, class_idx_, SPT_MM_GC);
  }

  // FIX: Destructor now cleans up the class from the stack if not consumed
  ~usertype() {
    if (class_idx_ > 0 && S_) {
      // Class is still on the stack - remove it to prevent leak
      spt_remove(S_, class_idx_);
    }
  }

  // Non-copyable, non-movable (builder pattern, use in single scope)
  usertype(const usertype &) = delete;
  usertype &operator=(const usertype &) = delete;
  usertype(usertype &&) = delete;
  usertype &operator=(usertype &&) = delete;

  // Set as global
  usertype &set_global() {
    spt_pushvalue(S_, class_idx_);
    spt_setglobal(S_, name_.c_str());
    spt_remove(S_, class_idx_);
    class_idx_ = -1; // Mark as consumed
    return *this;
  }

  // Set as global with different name
  usertype &set_global(const char *name) {
    spt_pushvalue(S_, class_idx_);
    spt_setglobal(S_, name);
    spt_remove(S_, class_idx_);
    class_idx_ = -1;
    return *this;
  }

  // Add default constructor
  usertype &add_constructor() { return add_constructor<>(); }

  // Add constructor with arguments
  // FIX: Multiple add_constructor calls now accumulate instead of overwriting.
  //      A per-type static registry dispatches by argument count at runtime.
  template <typename... Args> usertype &add_constructor() {
    constexpr int arity = static_cast<int>(sizeof...(Args));
    auto &reg = detail::typed_constructor_registry<T>::instance();
    reg.add(arity, detail::usertype_constructor_impl<T, Args...>);
    // Re-register the unified dispatcher each time (idempotent)
    spt_pushcclosure(S_, detail::typed_constructor_registry<T>::dispatch, 0);
    spt_setmagicmethod(S_, class_idx_, SPT_MM_INIT);
    return *this;
  }

  // Add constructor list (overloaded constructors)
  template <typename... Ctors> usertype &add_constructors() {
    spt_pushcclosure(S_, detail::constructor_dispatcher<T, Ctors...>::call, 0);
    spt_setmagicmethod(S_, class_idx_, SPT_MM_INIT);
    return *this;
  }

  // Add factory function as constructor
  template <typename Factory> usertype &add_factory(Factory &&factory) {
    auto wrapper = [f = std::forward<Factory>(factory)](state_t *S) -> int {
      using traits = detail::function_traits<std::decay_t<Factory>>;
      using return_type = typename traits::return_type;

      // Get arguments
      int idx = 2;                // Skip class
      auto args = std::tuple<>(); // TODO: implement proper arg extraction

      void *mem = spt_newcinstance(S, sizeof(T));

      if constexpr (std::is_pointer_v<return_type>) {
        // Factory returns pointer
        T *obj = f();
        if (obj) {
          new (mem) T(std::move(*obj));
          delete obj;
        } else {
          spt_error(S, "factory returned null");
        }
      } else if constexpr (std::is_same_v<return_type, T>) {
        // Factory returns value
        new (mem) T(f());
      } else {
        static_assert(sizeof(Factory) == 0, "Factory must return T or T*");
      }

      // Set class
      auto *info = detail::type_registry::find<T>();
      if (info && info->class_ref != no_ref) {
        spt_getref(S, info->class_ref);
        spt_setcclass(S, -2);
      }

      return 1;
    };

    push_wrapped_method(SPT_MM_INIT, std::move(wrapper));
    return *this;
  }

  // Add method
  template <typename R, typename... Args>
  usertype &add_method(const char *name, R (T::*method)(Args...)) {
    auto wrapper = detail::member_function_wrapper<R, T, Args...>(method);
    push_wrapped_method(name, std::move(wrapper));
    return *this;
  }

  // Add const method
  template <typename R, typename... Args>
  usertype &add_method(const char *name, R (T::*method)(Args...) const) {
    auto wrapper = detail::const_member_function_wrapper<R, T, Args...>(method);
    push_wrapped_method(name, std::move(wrapper));
    return *this;
  }

  // Add noexcept method
  template <typename R, typename... Args>
  usertype &add_method(const char *name, R (T::*method)(Args...) noexcept) {
    auto wrapper =
        detail::member_function_wrapper<R, T, Args...>(reinterpret_cast<R (T::*)(Args...)>(method));
    push_wrapped_method(name, std::move(wrapper));
    return *this;
  }

  // Add const noexcept method
  template <typename R, typename... Args>
  usertype &add_method(const char *name, R (T::*method)(Args...) const noexcept) {
    auto wrapper = detail::const_member_function_wrapper<R, T, Args...>(
        reinterpret_cast<R (T::*)(Args...) const>(method));
    push_wrapped_method(name, std::move(wrapper));
    return *this;
  }

  // Add lambda as method
  template <typename F, typename = std::enable_if_t<detail::is_callable_v<std::decay_t<F>>>>
  usertype &add_method(const char *name, F &&func) {
    auto wrapper = wrap(std::forward<F>(func));
    push_wrapped_method(name, std::move(wrapper));
    return *this;
  }

  // Add static method
  template <typename F> usertype &add_static(const char *name, F &&func) {
    auto wrapper = wrap(std::forward<F>(func));
    push_wrapped_static(name, std::move(wrapper));
    return *this;
  }

  // Add static constant
  template <typename V> usertype &add_static_const(const char *name, const V &value) {
    stack::push(S_, value);
    spt_bindstatic(S_, class_idx_, name);
    return *this;
  }

  // Add member variable (read-write)
  //
  // FIX: Lambda self-extraction now uses extract_self<T> helper for
  //      consistency with member_function_wrapper (supports lightuserdata).
  template <typename V> usertype &add_member(const char *name, V T::*member) {
    // Create getter
    auto getter = [member](state_t *S) -> int {
      T *self = detail::extract_self<T>(S);
      if (!self) {
        spt_error(S, "invalid self reference");
        return 0;
      }
      stack::push(S, self->*member);
      return 1;
    };

    // Create setter
    auto setter = [member](state_t *S) -> int {
      T *self = detail::extract_self<T>(S);
      if (!self) {
        spt_error(S, "invalid self reference");
        return 0;
      }
      self->*member = stack::get<V>(S, 2);
      return 0;
    };

    // Store as property
    add_property(name, std::move(getter), std::move(setter));
    return *this;
  }

  // Add readonly member
  template <typename V> usertype &add_readonly(const char *name, V T::*member) {
    auto getter = [member](state_t *S) -> int {
      T *self = detail::extract_self<T>(S);
      if (!self) {
        spt_error(S, "invalid self reference");
        return 0;
      }
      stack::push(S, self->*member);
      return 1;
    };

    // Store only getter
    std::string getter_name = std::string("get_") + name;
    push_wrapped_method(getter_name.c_str(), std::move(getter));
    return *this;
  }

  // Add readonly const member
  template <typename V> usertype &add_readonly(const char *name, const V T::*member) {
    auto getter = [member](state_t *S) -> int {
      const T *self = detail::extract_const_self<T>(S);
      if (!self) {
        spt_error(S, "invalid self reference");
        return 0;
      }
      stack::push(S, self->*member);
      return 1;
    };

    std::string getter_name = std::string("get_") + name;
    push_wrapped_method(getter_name.c_str(), std::move(getter));
    return *this;
  }

  // Add property with getter/setter
  template <typename Getter, typename Setter>
  usertype &add_property(const char *name, Getter &&getter, Setter &&setter) {
    std::string getter_name = std::string("get_") + name;
    std::string setter_name = std::string("set_") + name;

    push_wrapped_method(getter_name.c_str(), wrap(std::forward<Getter>(getter)));
    push_wrapped_method(setter_name.c_str(), wrap(std::forward<Setter>(setter)));
    return *this;
  }

  // Add readonly property
  template <typename Getter> usertype &add_property(const char *name, Getter &&getter) {
    std::string getter_name = std::string("get_") + name;
    push_wrapped_method(getter_name.c_str(), wrap(std::forward<Getter>(getter)));
    return *this;
  }

  // ========================================================================
  // Magic Methods
  // ========================================================================

  // __add
  template <typename F> usertype &add_meta_add(F &&func) {
    push_wrapped_magic(SPT_MM_ADD, wrap(std::forward<F>(func)));
    return *this;
  }

  // __sub
  template <typename F> usertype &add_meta_sub(F &&func) {
    push_wrapped_magic(SPT_MM_SUB, wrap(std::forward<F>(func)));
    return *this;
  }

  // __mul
  template <typename F> usertype &add_meta_mul(F &&func) {
    push_wrapped_magic(SPT_MM_MUL, wrap(std::forward<F>(func)));
    return *this;
  }

  // __div
  template <typename F> usertype &add_meta_div(F &&func) {
    push_wrapped_magic(SPT_MM_DIV, wrap(std::forward<F>(func)));
    return *this;
  }

  // __mod
  template <typename F> usertype &add_meta_mod(F &&func) {
    push_wrapped_magic(SPT_MM_MOD, wrap(std::forward<F>(func)));
    return *this;
  }

  // __pow
  template <typename F> usertype &add_meta_pow(F &&func) {
    push_wrapped_magic(SPT_MM_POW, wrap(std::forward<F>(func)));
    return *this;
  }

  // __unm (unary minus)
  template <typename F> usertype &add_meta_unm(F &&func) {
    push_wrapped_magic(SPT_MM_UNM, wrap(std::forward<F>(func)));
    return *this;
  }

  // __eq
  template <typename F> usertype &add_meta_eq(F &&func) {
    push_wrapped_magic(SPT_MM_EQ, wrap(std::forward<F>(func)));
    return *this;
  }

  // __lt
  template <typename F> usertype &add_meta_lt(F &&func) {
    push_wrapped_magic(SPT_MM_LT, wrap(std::forward<F>(func)));
    return *this;
  }

  // __le
  template <typename F> usertype &add_meta_le(F &&func) {
    push_wrapped_magic(SPT_MM_LE, wrap(std::forward<F>(func)));
    return *this;
  }

  // Bitwise operators
  template <typename F> usertype &add_meta_band(F &&func) {
    push_wrapped_magic(SPT_MM_BAND, wrap(std::forward<F>(func)));
    return *this;
  }

  template <typename F> usertype &add_meta_bor(F &&func) {
    push_wrapped_magic(SPT_MM_BOR, wrap(std::forward<F>(func)));
    return *this;
  }

  template <typename F> usertype &add_meta_bxor(F &&func) {
    push_wrapped_magic(SPT_MM_BXOR, wrap(std::forward<F>(func)));
    return *this;
  }

  template <typename F> usertype &add_meta_bnot(F &&func) {
    push_wrapped_magic(SPT_MM_BNOT, wrap(std::forward<F>(func)));
    return *this;
  }

  template <typename F> usertype &add_meta_shl(F &&func) {
    push_wrapped_magic(SPT_MM_SHL, wrap(std::forward<F>(func)));
    return *this;
  }

  template <typename F> usertype &add_meta_shr(F &&func) {
    push_wrapped_magic(SPT_MM_SHR, wrap(std::forward<F>(func)));
    return *this;
  }

  // __tostring (custom)
  template <typename F> usertype &add_tostring(F &&func) {
    push_wrapped_method("tostring", wrap(std::forward<F>(func)));
    return *this;
  }

  // __index_get (subscript read)
  template <typename F> usertype &add_meta_index_get(F &&func) {
    push_wrapped_magic(SPT_MM_INDEX_GET, wrap(std::forward<F>(func)));
    return *this;
  }

  // __index_set (subscript write)
  template <typename F> usertype &add_meta_index_set(F &&func) {
    push_wrapped_magic(SPT_MM_INDEX_SET, wrap(std::forward<F>(func)));
    return *this;
  }

  // __get (property access fallback)
  template <typename F> usertype &add_meta_get(F &&func) {
    push_wrapped_magic(SPT_MM_GET, wrap(std::forward<F>(func)));
    return *this;
  }

  // __set (property write intercept)
  template <typename F> usertype &add_meta_set(F &&func) {
    push_wrapped_magic(SPT_MM_SET, wrap(std::forward<F>(func)));
    return *this;
  }

  // ========================================================================
  // Accessors
  // ========================================================================

  // Get class index
  SPTXX_NODISCARD int class_index() const noexcept { return class_idx_; }

  // Get class as reference
  SPTXX_NODISCARD reference get_ref() const {
    spt_pushvalue(S_, class_idx_);
    return reference(S_);
  }

  // Finalize and pop class from stack
  void finalize() {
    if (class_idx_ > 0) {
      spt_remove(S_, class_idx_);
      class_idx_ = -1; // FIX: Mark as consumed so destructor doesn't double-pop
    }
  }

  // Get state
  SPTXX_NODISCARD state_t *state() const noexcept { return S_; }

  // Get name
  SPTXX_NODISCARD const std::string &name() const noexcept { return name_; }

private:
  template <typename Wrapper> void push_wrapped_method(const char *name, Wrapper wrapper) {
    using storage_type = detail::func_storage<Wrapper>;
    void *mem = spt_newcinstance(S_, sizeof(storage_type));
    new (mem) storage_type(std::move(wrapper));

    detail::ensure_func_storage_class(S_);
    int cinst_idx = spt_gettop(S_);
    spt_getfield(S_, registry_index, "__sptxx_func_storage_class");
    spt_setcclass(S_, cinst_idx);

    spt_pushcclosure(S_, detail::generic_cfunc_dispatcher, 1);
    spt_bindmethod(S_, class_idx_, name);
  }

  template <typename Wrapper> void push_wrapped_method(int mm_index, Wrapper wrapper) {
    using storage_type = detail::func_storage<Wrapper>;
    void *mem = spt_newcinstance(S_, sizeof(storage_type));
    new (mem) storage_type(std::move(wrapper));

    detail::ensure_func_storage_class(S_);
    int cinst_idx = spt_gettop(S_);
    spt_getfield(S_, registry_index, "__sptxx_func_storage_class");
    spt_setcclass(S_, cinst_idx);

    spt_pushcclosure(S_, detail::generic_cfunc_dispatcher, 1);
    spt_setmagicmethod(S_, class_idx_, mm_index);
  }

  template <typename Wrapper> void push_wrapped_static(const char *name, Wrapper wrapper) {
    using storage_type = detail::func_storage<Wrapper>;
    void *mem = spt_newcinstance(S_, sizeof(storage_type));
    new (mem) storage_type(std::move(wrapper));

    detail::ensure_func_storage_class(S_);
    int cinst_idx = spt_gettop(S_);
    spt_getfield(S_, registry_index, "__sptxx_func_storage_class");
    spt_setcclass(S_, cinst_idx);

    spt_pushcclosure(S_, detail::generic_cfunc_dispatcher, 1);
    spt_bindstatic(S_, class_idx_, name);
  }

  template <typename Wrapper> void push_wrapped_magic(int mm, Wrapper wrapper) {
    using storage_type = detail::func_storage<Wrapper>;
    void *mem = spt_newcinstance(S_, sizeof(storage_type));
    new (mem) storage_type(std::move(wrapper));

    detail::ensure_func_storage_class(S_);
    int cinst_idx = spt_gettop(S_);
    spt_getfield(S_, registry_index, "__sptxx_func_storage_class");
    spt_setcclass(S_, cinst_idx);

    spt_pushcclosure(S_, detail::generic_cfunc_dispatcher, 1);
    spt_setmagicmethod(S_, class_idx_, mm);
  }

  state_t *S_;
  std::string name_;
  int class_idx_;
};

// ============================================================================
// new_usertype helper
// ============================================================================

template <typename T> usertype<T> new_usertype(state_t *S, const char *name) {
  return usertype<T>(S, name);
}

template <typename T> usertype<T> new_usertype(state_view &s, const char *name) {
  return usertype<T>(s.raw(), name);
}

template <typename T> usertype<T> new_usertype(state &s, const char *name) {
  return usertype<T>(s.raw(), name);
}

// ============================================================================
// Stack Pusher/Getter for userdata
// ============================================================================

template <> struct stack_pusher<userdata> {
  static int push(state_t *S, const userdata &ud) {
    if (ud.valid()) {
      ud.push();
    } else {
      spt_pushnil(S);
    }
    return 1;
  }
};

template <> struct stack_getter<userdata> {
  static userdata get(state_t *S, int idx) { return userdata(S, idx); }
};

template <> struct stack_checker<userdata> {
  static bool check(state_t *S, int idx) { return spt_iscinstance(S, idx); }
};

// ============================================================================
// Push/Get for registered user types
// ============================================================================

template <typename T>
struct stack_pusher<T *,
                    std::enable_if_t<!std::is_same_v<T, void> && !std::is_same_v<T, const void>>> {
  static int push(state_t *S, T *ptr) {
    if (!ptr) {
      spt_pushnil(S);
      return 1;
    }

    // Check if type is registered
    auto *info = detail::type_registry::find<T>();
    if (info) {
      void *mem = spt_newcinstance(S, sizeof(T));
      new (mem) T(*ptr); // Copy construct

      if (info->class_ref != no_ref) {
        spt_getref(S, info->class_ref);
        spt_setcclass(S, -2);
      }
    } else {
      // Push as light userdata
      spt_pushlightuserdata(S, ptr);
    }
    return 1;
  }
};

template <typename T>
struct stack_getter<T *,
                    std::enable_if_t<!std::is_same_v<T, void> && !std::is_same_v<T, const void>>> {
  static T *get(state_t *S, int idx) {
    if (spt_iscinstance(S, idx)) {
      return static_cast<T *>(spt_tocinstance(S, idx));
    }
    if (spt_islightuserdata(S, idx)) {
      return static_cast<T *>(spt_tolightuserdata(S, idx));
    }
    return nullptr;
  }
};

template <typename T>
struct stack_checker<T *,
                     std::enable_if_t<!std::is_same_v<T, void> && !std::is_same_v<T, const void>>> {
  static bool check(state_t *S, int idx) {
    return spt_iscinstance(S, idx) || spt_islightuserdata(S, idx);
  }
};

// Value types (for user types) - pushes by creating a new cinstance
template <typename T>
struct stack_pusher<T, std::enable_if_t<!std::is_pointer_v<T> && !std::is_reference_v<T> &&
                                        !is_string_v<T> && !std::is_arithmetic_v<T> &&
                                        !std::is_same_v<T, bool> && std::is_class_v<T>>> {
  static int push(state_t *S, const T &value) {
    void *mem = spt_newcinstance(S, sizeof(T));
    new (mem) T(value);

    // FIX: Associate class if type is registered (enables __gc)
    auto *info = detail::type_registry::find<T>();
    if (info && info->class_ref != no_ref) {
      spt_getref(S, info->class_ref);
      spt_setcclass(S, -2);
    }

    return 1;
  }

  static int push(state_t *S, T &&value) {
    void *mem = spt_newcinstance(S, sizeof(T));
    new (mem) T(std::move(value));

    // FIX: Associate class if type is registered (enables __gc)
    auto *info = detail::type_registry::find<T>();
    if (info && info->class_ref != no_ref) {
      spt_getref(S, info->class_ref);
      spt_setcclass(S, -2);
    }

    return 1;
  }
};

// Value types (for user types) - gets by copying from cinstance
template <typename T>
struct stack_getter<T, std::enable_if_t<!std::is_pointer_v<T> && !std::is_reference_v<T> &&
                                        !is_string_v<T> && !std::is_arithmetic_v<T> &&
                                        !std::is_same_v<T, bool> && std::is_class_v<T>>> {
  static T get(state_t *S, int idx) {
    if (spt_iscinstance(S, idx)) {
      T *ptr = static_cast<T *>(spt_tocinstance(S, idx));
      if (ptr) {
        return *ptr; // Copy
      }
    }
#if defined(SPTXX_EXCEPTIONS_ENABLED)
    throw type_error("userdata", stack::get_type(S, idx), idx);
#else
    std::abort();
#endif
  }
};

// Reference types
template <typename T> struct stack_pusher<T &, std::enable_if_t<!is_string_v<T &>>> {
  static int push(state_t *S, T &ref) { return stack_pusher<T *>::push(S, &ref); }
};

template <typename T> struct stack_getter<T &, std::enable_if_t<!is_string_v<T &>>> {
  static T &get(state_t *S, int idx) {
    T *ptr = stack_getter<T *>::get(S, idx);
    if (!ptr) {
#if defined(SPTXX_EXCEPTIONS_ENABLED)
      throw type_error("userdata", stack::get_type(S, idx), idx);
#else
      std::abort();
#endif
    }
    return *ptr;
  }
};

// ============================================================================
// make_object for user types
// ============================================================================

template <typename T, typename... Args> object make_usertype_object(state_t *S, Args &&...args) {
  auto *info = detail::type_registry::find<T>();

  void *mem = spt_newcinstance(S, sizeof(T));
  new (mem) T(std::forward<Args>(args)...);

  if (info && info->class_ref != no_ref) {
    spt_getref(S, info->class_ref);
    spt_setcclass(S, -2);
  }

  return object(reference(S));
}

SPTXX_NAMESPACE_END

#endif // SPTXX_USERTYPE_HPP