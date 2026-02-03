#ifndef SPTXX_OBJECT_HPP
#define SPTXX_OBJECT_HPP

#include "reference.hpp"

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Object - Generic wrapper for any SPT value
// ============================================================================

class object {
public:
  object() noexcept = default;

  // Construct from stack index
  object(state_t *S, int index) : ref_(S, index) {}

  // Construct from reference (takes ownership)
  explicit object(reference &&ref) noexcept : ref_(std::move(ref)) {}

  // Construct various types
  object(state_t *S, nil_t) : ref_(S, nil) {}

  template <typename T, typename = std::enable_if_t<!std::is_same_v<std::decay_t<T>, object>>>
  object(state_t *S, T &&value) {
    stack::push(S, std::forward<T>(value));
    ref_ = reference(S);
  }

  // Move operations
  object(object &&) noexcept = default;
  object &operator=(object &&) noexcept = default;

  // Copy creates new reference
  object(const object &other) : ref_(other.ref_.copy()) {}

  object &operator=(const object &other) {
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

  // Type information
  SPTXX_NODISCARD type get_type() const { return ref_.get_type(); }

  SPTXX_NODISCARD bool is_nil() const { return ref_.is_nil(); }

  SPTXX_NODISCARD bool is_boolean() const { return get_type() == type::boolean; }

  SPTXX_NODISCARD bool is_integer() const { return get_type() == type::integer; }

  SPTXX_NODISCARD bool is_float() const { return get_type() == type::floating; }

  SPTXX_NODISCARD bool is_number() const { return is_integer() || is_float(); }

  SPTXX_NODISCARD bool is_string() const { return get_type() == type::string; }

  SPTXX_NODISCARD bool is_list() const { return get_type() == type::list; }

  SPTXX_NODISCARD bool is_map() const { return get_type() == type::map; }

  SPTXX_NODISCARD bool is_function() const { return get_type() == type::closure; }

  SPTXX_NODISCARD bool is_class() const { return get_type() == type::class_type; }

  SPTXX_NODISCARD bool is_object() const { return get_type() == type::object; }

  SPTXX_NODISCARD bool is_cinstance() const { return get_type() == type::cinstance; }

  SPTXX_NODISCARD bool is_fiber() const { return get_type() == type::fiber; }

  // Type check
  template <typename T> SPTXX_NODISCARD bool is() const { return ref_.is<T>(); }

  // Value access
  template <typename T> SPTXX_NODISCARD T as() const { return ref_.as<T>(); }

  // Optional value access
  template <typename T> SPTXX_NODISCARD std::optional<T> as_optional() const {
    if (is<T>()) {
      return as<T>();
    }
    return std::nullopt;
  }

  // Push onto stack
  void push() const { ref_.push(); }

  // Get underlying reference
  SPTXX_NODISCARD const reference &get_ref() const noexcept { return ref_; }

  SPTXX_NODISCARD reference &get_ref() noexcept { return ref_; }

  // Take ownership of reference
  SPTXX_NODISCARD reference take_ref() && { return std::move(ref_); }

  // Comparison
  bool operator==(const object &other) const {
    if (!valid() && !other.valid())
      return true;
    if (!valid() || !other.valid())
      return false;

    state_t *S = state();
    stack_guard guard(S);
    push();
    other.push();
    return spt_equal(S, -2, -1) != 0;
  }

  bool operator!=(const object &other) const { return !(*this == other); }

  // Comparison with raw equality (identity)
  bool raw_equal(const object &other) const {
    if (!valid() && !other.valid())
      return true;
    if (!valid() || !other.valid())
      return false;

    state_t *S = state();
    stack_guard guard(S);
    push();
    other.push();
    return spt_rawequal(S, -2, -1) != 0;
  }

protected:
  reference ref_;
};

// ============================================================================
// Stack Pusher/Getter for object
// ============================================================================

template <> struct stack_pusher<object> {
  static int push(state_t *S, const object &obj) {
    if (obj.valid()) {
      obj.push();
    } else {
      spt_pushnil(S);
    }
    return 1;
  }
};

template <> struct stack_getter<object> {
  static object get(state_t *S, int idx) { return object(S, idx); }
};

template <> struct stack_checker<object> {
  static bool check(state_t *S, int idx) {
    (void)S;
    (void)idx;
    return true; // object can hold any type
  }
};

// ============================================================================
// make_object factory function
// ============================================================================

template <typename T> object make_object(state_t *S, T &&value) {
  return object(S, std::forward<T>(value));
}

inline object make_object(state_t *S) { return object(S, nil); }

// ============================================================================
// Object from Stack Factory
// ============================================================================

class stack_objects {
public:
  explicit stack_objects(state_t *S) : S_(S) {}

  // Get object at stack index
  SPTXX_NODISCARD object at(int idx) const { return object(S_, idx); }

  // Get top object
  SPTXX_NODISCARD object top() const { return at(-1); }

  // Get multiple objects from stack
  template <typename... Ts> auto get(int start_idx = 1) const {
    return get_impl<Ts...>(start_idx, std::make_index_sequence<sizeof...(Ts)>{});
  }

private:
  template <typename... Ts, std::size_t... Is>
  auto get_impl(int start_idx, std::index_sequence<Is...>) const {
    return std::make_tuple(stack::get<Ts>(S_, start_idx + static_cast<int>(Is))...);
  }

  state_t *S_;
};

// ============================================================================
// Lightweight Object View (non-owning)
// ============================================================================

class object_view {
public:
  object_view() noexcept = default;

  object_view(state_t *S, int index) noexcept : ref_(S, index) {}

  explicit object_view(const stack_reference &ref) noexcept : ref_(ref) {}

  // State access
  SPTXX_NODISCARD state_t *state() const noexcept { return ref_.state(); }

  // Stack index
  SPTXX_NODISCARD int stack_index() const noexcept { return ref_.stack_index(); }

  // Validity
  SPTXX_NODISCARD bool valid() const noexcept { return ref_.valid(); }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // Type information
  SPTXX_NODISCARD type get_type() const noexcept { return ref_.get_type(); }

  SPTXX_NODISCARD bool is_nil() const noexcept { return ref_.is_nil(); }

  // Type check
  template <typename T> SPTXX_NODISCARD bool is() const { return ref_.is<T>(); }

  // Value access
  template <typename T> SPTXX_NODISCARD T as() const { return ref_.as<T>(); }

  // Push copy onto stack
  void push() const { ref_.push(); }

  // Convert to owning object
  SPTXX_NODISCARD object to_object() const { return object(ref_.state(), ref_.stack_index()); }

private:
  stack_reference ref_;
};

// ============================================================================
// Nil Object Constant
// ============================================================================

struct nil_object_t {
  SPTXX_NODISCARD object operator()(state_t *S) const { return object(S, nil); }
};

inline constexpr nil_object_t nil_object{};

SPTXX_NAMESPACE_END

#endif // SPTXX_OBJECT_HPP
