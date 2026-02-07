#ifndef SPTXX_REFERENCE_HPP
#define SPTXX_REFERENCE_HPP

#include "error.hpp"

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Stack Reference (Non-owning, temporary)
// ============================================================================

class stack_reference {
public:
  stack_reference() noexcept = default;

  stack_reference(state_t *S, int index) noexcept : S_(S), index_(spt_absindex(S, index)) {}

  stack_reference(const stack_reference &) noexcept = default;
  stack_reference &operator=(const stack_reference &) noexcept = default;

  // State access
  SPTXX_NODISCARD state_t *state() const noexcept { return S_; }

  // Stack index
  SPTXX_NODISCARD int stack_index() const noexcept { return index_; }

  // Validity check
  SPTXX_NODISCARD bool valid() const noexcept {
    return S_ != nullptr && stack::is_valid(S_, index_);
  }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // Type information
  SPTXX_NODISCARD type get_type() const noexcept {
    return valid() ? stack::get_type(S_, index_) : type::none;
  }

  SPTXX_NODISCARD bool is_nil() const noexcept { return !valid() || stack::is_nil(S_, index_); }

  // Push a copy onto stack
  void push() const {
    if (valid()) {
      stack::push_value(S_, index_);
    } else {
      spt_pushnil(S_);
    }
  }

  // Get value
  template <typename T> SPTXX_NODISCARD T as() const { return stack::get<T>(S_, index_); }

  // Check type
  template <typename T> SPTXX_NODISCARD bool is() const { return stack::check<T>(S_, index_); }

protected:
  state_t *S_ = nullptr;
  int index_ = 0;
};

// ============================================================================
// Reference (Owning, GC-protected reference)
// ============================================================================

class reference {
public:
  reference() noexcept = default;

  // Create reference from stack top (pops value)
  explicit reference(state_t *S) : S_(S), ref_(spt_ref(S)) {}

  // Create reference from stack index
  reference(state_t *S, int index) : S_(S) {
    spt_pushvalue(S, index);
    ref_ = spt_ref(S);
  }

  // Create nil reference
  reference(state_t *S, nil_t) : S_(S), ref_(nil_ref) {}

  // Non-copyable
  reference(const reference &) = delete;
  reference &operator=(const reference &) = delete;

  // Movable
  reference(reference &&other) noexcept : S_(other.S_), ref_(other.ref_) {
    other.S_ = nullptr;
    other.ref_ = no_ref;
  }

  reference &operator=(reference &&other) noexcept {
    if (this != &other) {
      release();
      S_ = other.S_;
      ref_ = other.ref_;
      other.S_ = nullptr;
      other.ref_ = no_ref;
    }
    return *this;
  }

  ~reference() { release(); }

  // State access
  SPTXX_NODISCARD state_t *state() const noexcept { return S_; }

  // Reference ID
  SPTXX_NODISCARD int ref_id() const noexcept { return ref_; }

  // Validity check
  SPTXX_NODISCARD bool valid() const noexcept { return S_ != nullptr && ref_ != no_ref; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // Push referenced value onto stack
  void push() const {
    if (valid()) {
      spt_getref(S_, ref_);
    } else if (S_) {
      spt_pushnil(S_);
    }
  }

  // Get stack index of pushed value (temporary!)
  SPTXX_NODISCARD int push_temporary() const {
    push();
    return spt_gettop(S_);
  }

  // Type information
  SPTXX_NODISCARD type get_type() const {
    if (!valid())
      return type::none;
    stack_guard guard(S_);
    push();
    return stack::get_type(S_, -1);
  }

  SPTXX_NODISCARD bool is_nil() const {
    return !valid() || ref_ == nil_ref || get_type() == type::nil;
  }

  // Get value
  template <typename T> SPTXX_NODISCARD T as() const {
    stack_guard guard(S_);
    push();
    return stack::get<T>(S_, -1);
  }

  // Check type
  template <typename T> SPTXX_NODISCARD bool is() const {
    stack_guard guard(S_);
    push();
    return stack::check<T>(S_, -1);
  }

  // Release the reference
  void release() {
    if (S_ && ref_ != no_ref && ref_ != nil_ref) {
      spt_unref(S_, ref_);
    }
    S_ = nullptr;
    ref_ = no_ref;
  }

  // Reset with new value from stack top
  void reset(state_t *S) {
    release();
    S_ = S;
    ref_ = spt_ref(S);
  }

  // Copy to another reference
  SPTXX_NODISCARD reference copy() const {
    if (!valid()) {
      return reference{};
    }
    stack_guard guard(S_);
    push();
    return reference(S_);
  }

protected:
  state_t *S_ = nullptr;
  int ref_ = no_ref;
};

// ============================================================================
// Main Reference (Keeps main state alive)
// ============================================================================

class main_reference : public reference {
public:
  main_reference() noexcept = default;

  explicit main_reference(state_t *S) : reference(S) { main_state_ = spt_getmain(S); }

  main_reference(state_t *S, int index) : reference(S, index) { main_state_ = spt_getmain(S); }

  main_reference(main_reference &&other) noexcept
      : reference(std::move(other)), main_state_(other.main_state_) {
    other.main_state_ = nullptr;
  }

  main_reference &operator=(main_reference &&other) noexcept {
    if (this != &other) {
      reference::operator=(std::move(other));
      main_state_ = other.main_state_;
      other.main_state_ = nullptr;
    }
    return *this;
  }

  SPTXX_NODISCARD state_t *main_state() const noexcept { return main_state_; }

private:
  state_t *main_state_ = nullptr;
};

// ============================================================================
// Unique Reference (Type-safe wrapper)
// ============================================================================

template <type ExpectedType> class typed_reference : public reference {
public:
  using reference::reference;

  typed_reference(state_t *S, int index) : reference(S, index) {
#if defined(SPTXX_DEBUG_MODE)
    type actual = get_type();
    SPTXX_ASSERT(actual == ExpectedType || actual == type::nil, "Type mismatch in typed_reference");
#endif
  }

  typed_reference(reference &&ref) : reference(std::move(ref)) {
#if defined(SPTXX_DEBUG_MODE)
    type actual = get_type();
    SPTXX_ASSERT(actual == ExpectedType || actual == type::nil, "Type mismatch in typed_reference");
#endif
  }

  static constexpr type expected_type = ExpectedType;
};

// Type aliases for common typed references
using list_reference = typed_reference<type::list>;
using map_reference = typed_reference<type::map>;
using function_reference = typed_reference<type::closure>;
using object_reference = typed_reference<type::object>;
using class_reference = typed_reference<type::class_type>;
using fiber_reference = typed_reference<type::fiber>;

// ============================================================================
// Reference Helpers
// ============================================================================

namespace detail {

// Create reference from stack value
inline reference make_ref(state_t *S, int index) { return reference(S, index); }

// Create reference from pushed value
inline reference make_ref_pop(state_t *S) { return reference(S); }

} // namespace detail

// ============================================================================
// Global Variable Access
// ============================================================================

class global_table {
public:
  explicit global_table(state_t *S) : S_(S) {}

  // Get global value
  template <typename T> SPTXX_NODISCARD T get(const char *name) const {
    stack_guard guard(S_);
    spt_getglobal(S_, name);
    return stack::get<T>(S_, -1);
  }

  // Get global as reference
  SPTXX_NODISCARD reference get_ref(const char *name) const {
    spt_getglobal(S_, name);
    return reference(S_);
  }

  // Set global value
  template <typename T> void set(const char *name, T &&value) {
    stack::push(S_, std::forward<T>(value));
    spt_setglobal(S_, name);
  }

  // Check if global exists
  SPTXX_NODISCARD bool has(const char *name) const { return spt_hasglobal(S_, name) != 0; }

  // Raw access (returns type)
  SPTXX_NODISCARD type raw_get(const char *name) const {
    return static_cast<type>(spt_getglobal(S_, name));
  }

  // Access via operator[]
  class proxy {
  public:
    proxy(state_t *S, const char *name) : S_(S), name_(name) {}

    template <typename T> proxy &operator=(T &&value) {
      stack::push(S_, std::forward<T>(value));
      spt_setglobal(S_, name_);
      return *this;
    }

    template <typename T> operator T() const {
      stack_guard guard(S_);
      spt_getglobal(S_, name_);
      return stack::get<T>(S_, -1);
    }

    SPTXX_NODISCARD reference as_ref() const {
      spt_getglobal(S_, name_);
      return reference(S_);
    }

  private:
    state_t *S_;
    const char *name_;
  };

  SPTXX_NODISCARD proxy operator[](const char *name) { return proxy(S_, name); }

private:
  state_t *S_;
};

// ============================================================================
// Registry Access
// ============================================================================

class registry {
public:
  explicit registry(state_t *S) : S_(S) {}

  // Get value from registry by key
  template <typename T> SPTXX_NODISCARD T get(const char *key) const {
    stack_guard guard(S_);
    spt_getfield(S_, registry_index, key);
    return stack::get<T>(S_, -1);
  }

  // Set value in registry
  template <typename T> void set(const char *key, T &&value) {
    stack::push(S_, std::forward<T>(value));
    spt_setfield(S_, registry_index, key);
  }

  // Check if key exists
  SPTXX_NODISCARD bool has(const char *key) const {
    stack_guard guard(S_);
    spt_getfield(S_, registry_index, key);
    return !spt_isnoneornil(S_, -1);
  }

private:
  state_t *S_;
};

SPTXX_NAMESPACE_END

#endif // SPTXX_REFERENCE_HPP
