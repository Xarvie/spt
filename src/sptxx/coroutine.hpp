#ifndef SPTXX_COROUTINE_HPP
#define SPTXX_COROUTINE_HPP

#include "state.hpp"

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Fiber Result (must be defined before fiber class)
// ============================================================================

class fiber_result {
public:
  fiber_result() = default;

  fiber_result(state_t *S, int start_index, int return_count, status s)
      : S_(S), start_index_(start_index), return_count_(return_count), status_(s) {}

  // Move only
  fiber_result(const fiber_result &) = delete;
  fiber_result &operator=(const fiber_result &) = delete;

  fiber_result(fiber_result &&other) noexcept
      : S_(other.S_), start_index_(other.start_index_), return_count_(other.return_count_),
        status_(other.status_) {
    other.S_ = nullptr;
    other.return_count_ = 0;
  }

  fiber_result &operator=(fiber_result &&other) noexcept {
    if (this != &other) {
      pop_results();
      S_ = other.S_;
      start_index_ = other.start_index_;
      return_count_ = other.return_count_;
      status_ = other.status_;
      other.S_ = nullptr;
      other.return_count_ = 0;
    }
    return *this;
  }

  ~fiber_result() { pop_results(); }

  // Status
  SPTXX_NODISCARD status get_status() const noexcept { return status_; }

  SPTXX_NODISCARD bool is_ok() const noexcept { return status_ == status::ok; }

  SPTXX_NODISCARD bool is_yielded() const noexcept { return status_ == status::yield; }

  SPTXX_NODISCARD bool is_error() const noexcept { return ::spt::is_error(status_); }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return is_ok() || is_yielded(); }

  // Return values
  SPTXX_NODISCARD int return_count() const noexcept { return return_count_; }

  template <typename T> SPTXX_NODISCARD T get(int index = 0) const {
    SPTXX_ASSERT(index < return_count_, "Return index out of bounds");
    return stack::get<T>(S_, start_index_ + index);
  }

  // Get error message
  SPTXX_NODISCARD std::string error_message() const {
    if (!is_error()) {
      return std::string{};
    }
    if (S_ && return_count_ > 0) {
      return stack::get<std::string>(S_, start_index_);
    }
    const char *last = S_ ? spt_getlasterror(S_) : nullptr;
    return last ? last : "unknown error";
  }

  // Abandon results
  void abandon() noexcept {
    S_ = nullptr;
    return_count_ = 0;
  }

private:
  void pop_results() {
    if (S_ && return_count_ > 0) {
      spt_pop(S_, return_count_);
    }
  }

  state_t *S_ = nullptr;
  int start_index_ = 0;
  int return_count_ = 0;
  status status_ = status::ok;
};

// ============================================================================
// Fiber - Coroutine wrapper
// ============================================================================

class fiber {
public:
  fiber() noexcept = default;

  // Construct from stack index (expects fiber on stack)
  fiber(state_t *parent, int index) : parent_(parent) {
    if (spt_isfiber(parent, index)) {
      fiber_state_ = spt_tofiber(parent, index);
      spt_pushvalue(parent, index);
      ref_ = reference(parent);
    }
  }

  // Construct from reference
  explicit fiber(state_t *parent, reference &&ref) noexcept
      : parent_(parent), ref_(std::move(ref)) {
    if (ref_.valid()) {
      stack_guard guard(parent_);
      ref_.push();
      fiber_state_ = spt_tofiber(parent_, -1);
    }
  }

  // Create new fiber from function
  static fiber create(state_t *S, const function &func) {
    func.push();
    state_t *fiber_state = spt_newfiber(S);
    return fiber(S, reference(S), fiber_state);
  }

  // Create new fiber from protected_function
  static fiber create(state_t *S, const protected_function &func) {
    func.push();
    state_t *fiber_state = spt_newfiber(S);
    return fiber(S, reference(S), fiber_state);
  }

  // Create from function at stack top
  static fiber create(state_t *S) {
    state_t *fiber_state = spt_newfiber(S);
    return fiber(S, reference(S), fiber_state);
  }

  // Move operations
  fiber(fiber &&other) noexcept
      : parent_(other.parent_), fiber_state_(other.fiber_state_), ref_(std::move(other.ref_)) {
    other.parent_ = nullptr;
    other.fiber_state_ = nullptr;
  }

  fiber &operator=(fiber &&other) noexcept {
    if (this != &other) {
      parent_ = other.parent_;
      fiber_state_ = other.fiber_state_;
      ref_ = std::move(other.ref_);
      other.parent_ = nullptr;
      other.fiber_state_ = nullptr;
    }
    return *this;
  }

  // Copy (creates new reference, but shares fiber state)
  fiber(const fiber &other)
      : parent_(other.parent_), fiber_state_(other.fiber_state_), ref_(other.ref_.copy()) {}

  fiber &operator=(const fiber &other) {
    if (this != &other) {
      parent_ = other.parent_;
      fiber_state_ = other.fiber_state_;
      ref_ = other.ref_.copy();
    }
    return *this;
  }

  // Validity
  SPTXX_NODISCARD bool valid() const noexcept { return ref_.valid() && fiber_state_ != nullptr; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // State access
  SPTXX_NODISCARD state_t *parent_state() const noexcept { return parent_; }

  SPTXX_NODISCARD state_t *fiber_state() const noexcept { return fiber_state_; }

  // Status
  SPTXX_NODISCARD fiber_status get_status() const {
    if (!valid())
      return fiber_status::error;
    return static_cast<fiber_status>(spt_fiberstatus(fiber_state_));
  }

  SPTXX_NODISCARD bool is_fresh() const { return get_status() == fiber_status::fresh; }

  SPTXX_NODISCARD bool is_running() const { return get_status() == fiber_status::running; }

  SPTXX_NODISCARD bool is_suspended() const { return get_status() == fiber_status::suspended; }

  SPTXX_NODISCARD bool is_done() const { return get_status() == fiber_status::done; }

  SPTXX_NODISCARD bool is_error() const { return get_status() == fiber_status::error; }

  SPTXX_NODISCARD bool is_resumable() const {
    if (!valid())
      return false;
    return spt_isresumable(fiber_state_) != 0;
  }

  // Resume the fiber
  template <typename... Args> fiber_result resume(Args &&...args) {
    if (!valid() || !is_resumable()) {
      return fiber_result(parent_, 0, 0, status::runtime);
    }

    // Push arguments onto parent stack
    int nargs = stack::push_all(parent_, std::forward<Args>(args)...);

    // Resume
    int top_before = spt_gettop(parent_) - nargs;
    int result = spt_resume(fiber_state_, parent_, nargs);
    status stat = static_cast<status>(result);

    int top_after = spt_gettop(parent_);
    int ret_count = top_after - top_before;

    return fiber_result(parent_, top_before + 1, ret_count, stat);
  }

  // Resume with no arguments
  fiber_result operator()() { return resume(); }

  // Resume with arguments
  template <typename... Args> fiber_result operator()(Args &&...args) {
    return resume(std::forward<Args>(args)...);
  }

  // Abort the fiber
  void abort(const char *error_msg = "fiber aborted") {
    if (!valid())
      return;

    spt_pushstring(parent_, error_msg);
    spt_fiberabort(fiber_state_);
  }

  // Get error (if in error state)
  SPTXX_NODISCARD object get_error() const {
    if (!valid() || !is_error()) {
      return object(parent_, nil);
    }
    spt_fibererror(fiber_state_);
    return object(reference(parent_));
  }

  // Push fiber onto stack
  void push() const { ref_.push(); }

private:
  // Private constructor for create()
  fiber(state_t *parent, reference &&ref, state_t *fiber_state)
      : parent_(parent), fiber_state_(fiber_state), ref_(std::move(ref)) {}

  state_t *parent_ = nullptr;
  state_t *fiber_state_ = nullptr;
  reference ref_;
};

// ============================================================================
// Yield Helpers (for use in C functions)
// ============================================================================

namespace yield {

// Yield from current fiber (use in CFunction)
inline int values(state_t *S, int nresults) { return spt_yield(S, nresults); }

// Yield nothing
inline int nothing(state_t *S) { return spt_yield(S, 0); }

// Yield single value
template <typename T> int value(state_t *S, T &&v) {
  stack::push(S, std::forward<T>(v));
  return spt_yield(S, 1);
}

// Yield multiple values
template <typename... Ts> int all(state_t *S, Ts &&...values) {
  int n = stack::push_all(S, std::forward<Ts>(values)...);
  return spt_yield(S, n);
}

} // namespace yield

// ============================================================================
// Stack Pusher/Getter for fiber
// ============================================================================

template <> struct stack_pusher<fiber> {
  static int push(state_t *S, const fiber &f) {
    if (f.valid()) {
      f.push();
    } else {
      spt_pushnil(S);
    }
    return 1;
  }
};

template <> struct stack_getter<fiber> {
  static fiber get(state_t *S, int idx) { return fiber(S, idx); }
};

template <> struct stack_checker<fiber> {
  static bool check(state_t *S, int idx) { return spt_isfiber(S, idx); }
};

// ============================================================================
// Coroutine utilities for state
// ============================================================================

// Create fiber from function
inline fiber make_fiber(state_t *S, const function &func) { return fiber::create(S, func); }

inline fiber make_fiber(state_view &s, const function &func) {
  return fiber::create(s.raw(), func);
}

// Create fiber from global function name
inline fiber make_fiber(state_t *S, const char *func_name) {
  spt_getglobal(S, func_name);
  return fiber::create(S);
}

inline fiber make_fiber(state_view &s, const char *func_name) {
  return make_fiber(s.raw(), func_name);
}

// ============================================================================
// Fiber Iterator (for range-based for loops)
// ============================================================================

class fiber_iterator {
public:
  struct sentinel {};

  explicit fiber_iterator(fiber &f) : fiber_(&f), done_(false) { advance(); }

  fiber_iterator &operator++() {
    advance();
    return *this;
  }

  SPTXX_NODISCARD const fiber_result &operator*() const { return result_; }

  SPTXX_NODISCARD bool operator!=(sentinel) const { return !done_; }

private:
  void advance() {
    if (!fiber_->is_resumable()) {
      done_ = true;
      return;
    }

    result_ = fiber_->resume();
    if (result_.is_error() || fiber_->is_done()) {
      done_ = true;
    }
  }

  fiber *fiber_;
  fiber_result result_;
  bool done_;
};

// Enable range-based for loop: for (auto& result : iterate(my_fiber)) { ... }
struct fiber_range {
  fiber &f;

  explicit fiber_range(fiber &fib) : f(fib) {}

  fiber_iterator begin() { return fiber_iterator(f); }

  fiber_iterator::sentinel end() { return {}; }
};

inline fiber_range iterate(fiber &f) { return fiber_range(f); }

// ============================================================================
// Async-style helpers
// ============================================================================

namespace async {

// Run fiber to completion, collecting all yielded values
template <typename T = object> std::vector<T> run_all(fiber &f) {
  std::vector<T> results;

  while (f.is_resumable()) {
    auto result = f.resume();
    if (result.is_error()) {
      break;
    }

    for (int i = 0; i < result.return_count(); ++i) {
      results.push_back(result.template get<T>(i));
    }
  }

  return results;
}

// Run fiber to completion, ignoring yielded values
inline status run(fiber &f) {
  status last = status::ok;

  while (f.is_resumable()) {
    auto result = f.resume();
    last = result.get_status();
    if (result.is_error()) {
      break;
    }
  }

  return last;
}

} // namespace async

SPTXX_NAMESPACE_END

#endif // SPTXX_COROUTINE_HPP
