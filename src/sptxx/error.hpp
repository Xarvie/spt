#ifndef SPTXX_ERROR_HPP
#define SPTXX_ERROR_HPP

#include "stack.hpp"

#include <exception>
#include <sstream>
#include <stdexcept>
#include <string>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Base Error Class
// ============================================================================

class error : public std::exception {
public:
  error() = default;

  explicit error(std::string message) : message_(std::move(message)) {}

  error(status s, std::string message) : status_(s), message_(std::move(message)) {}

  error(const error &) = default;
  error(error &&) noexcept = default;
  error &operator=(const error &) = default;
  error &operator=(error &&) noexcept = default;

  ~error() noexcept override = default;

  SPTXX_NODISCARD const char *what() const noexcept override { return message_.c_str(); }

  SPTXX_NODISCARD status get_status() const noexcept { return status_; }

  SPTXX_NODISCARD const std::string &message() const noexcept { return message_; }

protected:
  status status_ = status::runtime;
  std::string message_;
};

// ============================================================================
// Specific Error Types
// ============================================================================

class type_error : public error {
public:
  type_error(type expected, type actual, int index = 0)
      : error(build_message(expected, actual, index)), expected_(expected), actual_(actual),
        index_(index) {}

  type_error(const char *expected_name, type actual, int index = 0)
      : error(build_message(expected_name, actual, index)), expected_(type::none), actual_(actual),
        index_(index) {}

  SPTXX_NODISCARD type expected() const noexcept { return expected_; }

  SPTXX_NODISCARD type actual() const noexcept { return actual_; }

  SPTXX_NODISCARD int index() const noexcept { return index_; }

private:
  static std::string build_message(type expected, type actual, int index) {
    std::ostringstream oss;
    oss << "type error: expected " << type_name(expected) << ", got " << type_name(actual);
    if (index != 0) {
      oss << " at index " << index;
    }
    return oss.str();
  }

  static std::string build_message(const char *expected_name, type actual, int index) {
    std::ostringstream oss;
    oss << "type error: expected " << expected_name << ", got " << type_name(actual);
    if (index != 0) {
      oss << " at index " << index;
    }
    return oss.str();
  }

  type expected_;
  type actual_;
  int index_;
};

class stack_error : public error {
public:
  explicit stack_error(std::string message) : error(std::move(message)) {}

  stack_error(int required, int available)
      : error(build_message(required, available)), required_(required), available_(available) {}

  SPTXX_NODISCARD int required() const noexcept { return required_; }

  SPTXX_NODISCARD int available() const noexcept { return available_; }

private:
  static std::string build_message(int required, int available) {
    std::ostringstream oss;
    oss << "stack error: required " << required << " slots, but only " << available << " available";
    return oss.str();
  }

  int required_ = 0;
  int available_ = 0;
};

class argument_error : public error {
public:
  argument_error(int arg, std::string message) : error(build_message(arg, message)), arg_(arg) {}

  SPTXX_NODISCARD int argument() const noexcept { return arg_; }

private:
  static std::string build_message(int arg, const std::string &msg) {
    std::ostringstream oss;
    oss << "argument #" << arg << ": " << msg;
    return oss.str();
  }

  int arg_;
};

class syntax_error : public error {
public:
  syntax_error(std::string message, int line = -1, int column = -1)
      : error(status::syntax, build_message(message, line, column)), line_(line), column_(column) {}

  SPTXX_NODISCARD int line() const noexcept { return line_; }

  SPTXX_NODISCARD int column() const noexcept { return column_; }

private:
  static std::string build_message(const std::string &msg, int line, int column) {
    std::ostringstream oss;
    oss << "syntax error";
    if (line >= 0) {
      oss << " at line " << line;
      if (column >= 0) {
        oss << ", column " << column;
      }
    }
    oss << ": " << msg;
    return oss.str();
  }

  int line_;
  int column_;
};

class compile_error : public error {
public:
  explicit compile_error(std::string message) : error(status::compile, std::move(message)) {}

  compile_error(std::string message, int line, int column, std::string source)
      : error(status::compile, build_message(message, line, column, source)), line_(line),
        column_(column), source_(std::move(source)) {}

  SPTXX_NODISCARD int line() const noexcept { return line_; }

  SPTXX_NODISCARD int column() const noexcept { return column_; }

  SPTXX_NODISCARD const std::string &source() const noexcept { return source_; }

private:
  static std::string build_message(const std::string &msg, int line, int column,
                                   const std::string &src) {
    std::ostringstream oss;
    oss << "compile error";
    if (!src.empty()) {
      oss << " in " << src;
    }
    if (line >= 0) {
      oss << " at line " << line;
      if (column >= 0) {
        oss << ":" << column;
      }
    }
    oss << ": " << msg;
    return oss.str();
  }

  int line_ = -1;
  int column_ = -1;
  std::string source_;
};

class runtime_error : public error {
public:
  explicit runtime_error(std::string message) : error(status::runtime, std::move(message)) {}
};

class memory_error : public error {
public:
  memory_error() : error(status::memory, "memory allocation failed") {}

  explicit memory_error(std::string message) : error(status::memory, std::move(message)) {}
};

class file_error : public error {
public:
  explicit file_error(std::string filename)
      : error(status::file, "cannot open file: " + filename), filename_(std::move(filename)) {}

  file_error(std::string filename, std::string message)
      : error(status::file, "file error (" + filename + "): " + message),
        filename_(std::move(filename)) {}

  SPTXX_NODISCARD const std::string &filename() const noexcept { return filename_; }

private:
  std::string filename_;
};

// ============================================================================
// Error Handling Utilities
// ============================================================================

namespace detail {

// Throw appropriate exception based on status
inline void throw_error(status s, const char *msg = nullptr) {
#if defined(SPTXX_EXCEPTIONS_ENABLED)
  std::string message = msg ? msg : "unknown error";
  switch (s) {
  case status::ok:
  case status::yield:
    return;
  case status::runtime:
    throw runtime_error(message);
  case status::syntax:
    throw syntax_error(message);
  case status::compile:
    throw compile_error(message);
  case status::memory:
    throw memory_error(message);
  case status::file:
    throw file_error(message);
  default:
    throw error(s, message);
  }
#else
  (void)s;
  (void)msg;
#endif
}

// Check and throw on error
inline void check_status(state_t *S, status s) {
#if defined(SPTXX_EXCEPTIONS_ENABLED)
  if (is_error(s)) {
    const char *msg = spt_getlasterror(S);
    throw_error(s, msg);
  }
#else
  (void)S;
  (void)s;
#endif
}

// Type check with exception
template <typename T> inline void check_type(state_t *S, int idx) {
#if defined(SPTXX_EXCEPTIONS_ENABLED)
  if (!stack_checker<T>::check(S, idx)) {
    type actual = static_cast<type>(spt_type(S, idx));
    throw type_error(type_info<T>::name(), actual, idx);
  }
#else
  (void)S;
  (void)idx;
#endif
}

// Stack space check
inline void ensure_stack(state_t *S, int n) {
#if defined(SPTXX_STACK_CHECK_ENABLED)
  if (!spt_checkstack(S, n)) {
#if defined(SPTXX_EXCEPTIONS_ENABLED)
    throw stack_error(n, 0);
#else
    std::abort();
#endif
  }
#else
  (void)S;
  (void)n;
#endif
}

} // namespace detail

// ============================================================================
// Protected Call Result
// ============================================================================

class protected_function_result {
public:
  protected_function_result() = default;

  protected_function_result(state_t *S, int start_index, int return_count, status s)
      : S_(S), start_index_(start_index), return_count_(return_count), status_(s) {}

  // Move only
  protected_function_result(const protected_function_result &) = delete;
  protected_function_result &operator=(const protected_function_result &) = delete;

  protected_function_result(protected_function_result &&other) noexcept
      : S_(other.S_), start_index_(other.start_index_), return_count_(other.return_count_),
        status_(other.status_) {
    other.S_ = nullptr;
    other.return_count_ = 0;
  }

  protected_function_result &operator=(protected_function_result &&other) noexcept {
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

  ~protected_function_result() { pop_results(); }

  // Status checks
  SPTXX_NODISCARD bool valid() const noexcept { return status_ == status::ok; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  SPTXX_NODISCARD status get_status() const noexcept { return status_; }

  SPTXX_NODISCARD int return_count() const noexcept { return return_count_; }

  // Get return value at index (0-based)

  template <typename T> SPTXX_NODISCARD T get(int index = 0) const {
    std::cerr << "[RESULT::GET] start_index_=" << start_index_
              << ", return_count_=" << return_count_ << ", index=" << index << std::endl;

    SPTXX_ASSERT(valid(), "Cannot get result from failed call");
    SPTXX_ASSERT(index < return_count_, "Return index out of bounds");

    int actual_idx = start_index_ + index;
    std::cerr << "[RESULT::GET] actual_idx = " << actual_idx << std::endl;

    // 打印该位置的值
    int t = spt_type(S_, actual_idx);
    std::cerr << "[RESULT::GET] type at idx " << actual_idx << " = " << t << std::endl;

    T result = stack::get<T>(S_, actual_idx);

    // 对于 double 类型，打印值
    if constexpr (std::is_same_v<T, double>) {
      std::cerr << "[RESULT::GET] got double value: " << result << std::endl;
    }

    return result;
  }

  // Get error message (if failed)
  SPTXX_NODISCARD std::string error_message() const {
    if (valid()) {
      return std::string{};
    }
    if (S_ && return_count_ > 0) {
      return stack::get<std::string>(S_, start_index_);
    }
    const char *last = S_ ? spt_getlasterror(S_) : nullptr;
    return last ? last : "unknown error";
  }

  // Get underlying state
  SPTXX_NODISCARD state_t *state() const noexcept { return S_; }

  // Abandon results (don't pop on destruction)
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
// Load Result
// ============================================================================

class load_result {
public:
  load_result() = default;

  load_result(state_t *S, status s) : S_(S), status_(s), pushed_(s == status::ok ? 1 : 0) {}

  load_result(const load_result &) = delete;
  load_result &operator=(const load_result &) = delete;

  load_result(load_result &&other) noexcept
      : S_(other.S_), status_(other.status_), pushed_(other.pushed_) {
    other.S_ = nullptr;
    other.pushed_ = 0;
  }

  load_result &operator=(load_result &&other) noexcept {
    if (this != &other) {
      pop();
      S_ = other.S_;
      status_ = other.status_;
      pushed_ = other.pushed_;
      other.S_ = nullptr;
      other.pushed_ = 0;
    }
    return *this;
  }

  ~load_result() { pop(); }

  SPTXX_NODISCARD bool valid() const noexcept { return status_ == status::ok; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  SPTXX_NODISCARD status get_status() const noexcept { return status_; }

  SPTXX_NODISCARD std::string error_message() const {
    if (valid()) {
      return std::string{};
    }
    const char *last = S_ ? spt_getlasterror(S_) : nullptr;
    return last ? last : "unknown error";
  }

  // Call the loaded chunk
  protected_function_result call(int nresults = multi_return) {
    if (!valid() || !S_) {
      return protected_function_result(S_, 0, 0, status_);
    }

    int top_before = spt_gettop(S_) - 1; // -1 for the function
    int result = spt_pcall(S_, 0, nresults, 0);
    status call_status = static_cast<status>(result);

    int top_after = spt_gettop(S_);
    int ret_count = top_after - top_before;

    pushed_ = 0; // Consumed

    return protected_function_result(S_, top_before + 1, ret_count, call_status);
  }

  // Abandon the loaded chunk (don't pop)
  void abandon() noexcept { pushed_ = 0; }

private:
  void pop() {
    if (S_ && pushed_ > 0) {
      spt_pop(S_, pushed_);
    }
  }

  state_t *S_ = nullptr;
  status status_ = status::runtime;
  int pushed_ = 0;
};

// ============================================================================
// Error Handler Registration
// ============================================================================

class error_handler_scope {
public:
  error_handler_scope(state_t *S, spt_ErrorHandler handler, void *ud = nullptr) : S_(S) {
    // Save old handler would require C API support
    spt_seterrorhandler(S, handler, ud);
  }

  ~error_handler_scope() {
    // Restore old handler
    spt_seterrorhandler(S_, nullptr, nullptr);
  }

  error_handler_scope(const error_handler_scope &) = delete;
  error_handler_scope &operator=(const error_handler_scope &) = delete;

private:
  state_t *S_;
};

SPTXX_NAMESPACE_END

#endif // SPTXX_ERROR_HPP
