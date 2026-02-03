#ifndef SPTXX_STATE_HPP
#define SPTXX_STATE_HPP

#include "function.hpp"

#include <memory>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Forward Declarations
// ============================================================================

class state;
class state_view;

// ============================================================================
// State View - Non-owning wrapper
// ============================================================================

class state_view {
public:
  state_view() noexcept = default;

  explicit state_view(state_t *S) noexcept : S_(S) {}

  // Raw state access
  SPTXX_NODISCARD state_t *raw() const noexcept { return S_; }

  SPTXX_NODISCARD operator state_t *() const noexcept { return S_; }

  // Validity
  SPTXX_NODISCARD bool valid() const noexcept { return S_ != nullptr; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  // ========================================================================
  // Stack Operations
  // ========================================================================

  SPTXX_NODISCARD int stack_top() const noexcept { return spt_gettop(S_); }

  void set_stack_top(int idx) noexcept { spt_settop(S_, idx); }

  void pop(int n = 1) noexcept { spt_pop(S_, n); }

  bool check_stack(int n) const noexcept { return spt_checkstack(S_, n) != 0; }

  // Push values
  template <typename T> void push(T &&value) { stack::push(S_, std::forward<T>(value)); }

  template <typename... Ts> int push_all(Ts &&...values) {
    return stack::push_all(S_, std::forward<Ts>(values)...);
  }

  // Get values
  template <typename T> SPTXX_NODISCARD T get(int idx = -1) const { return stack::get<T>(S_, idx); }

  // Type check
  template <typename T> SPTXX_NODISCARD bool is(int idx = -1) const {
    return stack::check<T>(S_, idx);
  }

  SPTXX_NODISCARD type get_type(int idx = -1) const noexcept { return stack::get_type(S_, idx); }

  // ========================================================================
  // Global Variables
  // ========================================================================

  // Get global value
  template <typename T> SPTXX_NODISCARD T get_global(const char *name) const {
    stack_guard guard(S_);
    spt_getglobal(S_, name);
    return stack::get<T>(S_, -1);
  }

  SPTXX_NODISCARD object get_global(const char *name) const {
    spt_getglobal(S_, name);
    return object(reference(S_));
  }

  // Set global value
  template <typename T> void set_global(const char *name, T &&value) {
    stack::push(S_, std::forward<T>(value));
    spt_setglobal(S_, name);
  }

  // Check if global exists
  SPTXX_NODISCARD bool has_global(const char *name) const { return spt_hasglobal(S_, name) != 0; }

  // Global table access
  SPTXX_NODISCARD global_table globals() const { return global_table(S_); }

  // Convenience operator[]
  SPTXX_NODISCARD global_table::proxy operator[](const char *name) {
    return global_table(S_)[name];
  }

  // ========================================================================
  // Registry
  // ========================================================================

  SPTXX_NODISCARD registry get_registry() const { return registry(S_); }

  // ========================================================================
  // Function Calls
  // ========================================================================

  // Call global function
  template <typename... Args> protected_function_result call(const char *name, Args &&...args) {
    spt_getglobal(S_, name);
    protected_function func{reference(S_)};
    return func(std::forward<Args>(args)...);
  }

  // Call method on object at stack index
  template <typename... Args>
  protected_function_result call_method(int obj_idx, const char *method, Args &&...args) {
    int abs_idx = spt_absindex(S_, obj_idx);
    spt_getprop(S_, abs_idx, method);
    spt_pushvalue(S_, abs_idx); // Push self

    int nargs = 1 + stack::push_all(S_, std::forward<Args>(args)...);

    int top_before = spt_gettop(S_) - nargs - 1;
    int result = spt_pcall(S_, nargs, multi_return, 0);
    status stat = static_cast<status>(result);

    int top_after = spt_gettop(S_);
    int ret_count = top_after - top_before;

    return protected_function_result(S_, top_before + 1, ret_count, stat);
  }

  // ========================================================================
  // Script Execution
  // ========================================================================

  // Load and execute string
  SPTXX_NODISCARD load_result load(const char *source, const char *name = "chunk") {
    spt_Chunk *chunk = spt_loadstring(S_, source, name);
    if (!chunk) {
      return load_result(S_, status::compile);
    }
    spt_pushchunk(S_, chunk);
    spt_freechunk(chunk);
    return load_result(S_, status::ok);
  }

  // Execute string directly
  SPTXX_NODISCARD status do_string(const char *source, const char *name = "chunk") {
    return static_cast<status>(spt_dostring(S_, source, name));
  }

  // Load and execute file
  SPTXX_NODISCARD load_result load_file(const char *filename) {
    spt_Chunk *chunk = spt_loadfile(S_, filename);
    if (!chunk) {
      return load_result(S_, status::file);
    }
    spt_pushchunk(S_, chunk);
    spt_freechunk(chunk);
    return load_result(S_, status::ok);
  }

  // Execute file directly
  SPTXX_NODISCARD status do_file(const char *filename) {
    return static_cast<status>(spt_dofile(S_, filename));
  }

  // ========================================================================
  // Module System
  // ========================================================================

  // Add module search path
  void add_path(const char *path) { spt_addpath(S_, path); }

  // Import module
  SPTXX_NODISCARD object import(const char *name) {
    int result = spt_import(S_, name);
    if (result != SPT_OK) {
      return object(S_, nil);
    }
    return object(reference(S_));
  }

  // Reload module
  SPTXX_NODISCARD status reload(const char *name) {
    return static_cast<status>(spt_reload(S_, name));
  }

  // Tick modules (for hot reload)
  void tick_modules() { spt_tickmodules(S_); }

  // ========================================================================
  // Collection Creation
  // ========================================================================

  // Create new list
  SPTXX_NODISCARD list new_list(int capacity = 0) { return list::create(S_, capacity); }

  template <typename T> SPTXX_NODISCARD list new_list(std::initializer_list<T> init) {
    return list::create(S_, init);
  }

  // Create new map
  SPTXX_NODISCARD map new_map(int capacity = 0) { return map::create(S_, capacity); }

  template <typename K, typename V>
  SPTXX_NODISCARD map new_map(std::initializer_list<std::pair<K, V>> init) {
    return map::create(S_, init);
  }

  // ========================================================================
  // Function Registration
  // ========================================================================

  // Register single function as global (raw C function)
  void set_function(const char *name, cfunction_t func) {
    spt_pushcfunction(S_, func);
    spt_setglobal(S_, name);
  }

  // Register wrapped C++ function
  // Uses template-based static storage pattern since SPT doesn't provide
  // upvalue access from C functions (unlike Lua's lua_upvalueindex)
  template <typename F> void set_function(const char *name, F &&func) {
    auto wrapper = wrap(std::forward<F>(func));
    push_wrapped_function(name, std::move(wrapper));
  }

  // Register function table as library
  void register_lib(const char *libname, const spt_Reg *funcs) { spt_register(S_, libname, funcs); }

  // ========================================================================
  // C Module Definition
  // ========================================================================

  void define_module(const char *name, const spt_Reg *funcs) { spt_defmodule(S_, name, funcs); }

  // ========================================================================
  // Garbage Collection
  // ========================================================================

  int gc(gc_mode mode, int data = 0) { return spt_gc(S_, static_cast<int>(mode), data); }

  void gc_collect() { gc(gc_mode::collect); }

  void gc_stop() { gc(gc_mode::stop); }

  void gc_restart() { gc(gc_mode::restart); }

  SPTXX_NODISCARD bool gc_is_running() const {
    return spt_gc(S_, static_cast<int>(gc_mode::is_running), 0) != 0;
  }

  SPTXX_NODISCARD int gc_count_kb() const {
    return spt_gc(S_, static_cast<int>(gc_mode::count_kb), 0);
  }

  SPTXX_NODISCARD int gc_count_bytes() const {
    return spt_gc(S_, static_cast<int>(gc_mode::count_bytes), 0);
  }

  SPTXX_NODISCARD int gc_object_count() const {
    return spt_gc(S_, static_cast<int>(gc_mode::obj_count), 0);
  }

  // ========================================================================
  // Error Handling
  // ========================================================================

  void set_error_handler(spt_ErrorHandler handler, void *ud = nullptr) {
    spt_seterrorhandler(S_, handler, ud);
  }

  void set_print_handler(spt_PrintHandler handler, void *ud = nullptr) {
    spt_setprinthandler(S_, handler, ud);
  }

  SPTXX_NODISCARD const char *last_error() const { return spt_getlasterror(S_); }

  SPTXX_NODISCARD std::string stack_trace() const {
    spt_stacktrace(S_);
    std::string result = stack::get<std::string>(S_, -1);
    spt_pop(S_, 1);
    return result;
  }

  [[noreturn]] void error(const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    spt_pushvfstring(S_, fmt, args);
    va_end(args);
    spt_throw(S_);
#if defined(_MSC_VER)
    __assume(false);
#else
    __builtin_unreachable();
#endif
  }

  // ========================================================================
  // User Data
  // ========================================================================

  void set_userdata(void *ud) { spt_setuserdata(S_, ud); }

  SPTXX_NODISCARD void *get_userdata() const { return spt_getuserdata(S_); }

  template <typename T> void set_userdata(T *ptr) { spt_setuserdata(S_, static_cast<void *>(ptr)); }

  template <typename T> SPTXX_NODISCARD T *get_userdata() const {
    return static_cast<T *>(spt_getuserdata(S_));
  }

  // ========================================================================
  // State Information
  // ========================================================================

  SPTXX_NODISCARD state_t *main_state() const { return spt_getmain(S_); }

  SPTXX_NODISCARD state_t *current_state() const { return spt_getcurrent(S_); }

  SPTXX_NODISCARD bool is_main_fiber() const { return S_ == spt_getmain(S_); }

protected:
  // Push a wrapped C++ function using closure with upvalue
  //
  // Now that SPT_UPVALUEINDEX is implemented, we can use proper closures
  // with upvalues instead of static storage. This is the standard pattern
  // used in Lua and provides better memory management and thread safety.
  template <typename Wrapper> void push_wrapped_function(const char *name, Wrapper wrapper) {
    // Create storage in a GC-managed cinstance
    using storage_type = detail::func_storage<Wrapper>;
    void *mem = spt_newcinstance(S_, sizeof(storage_type));
    new (mem) storage_type(std::move(wrapper));

    // Push closure with storage as upvalue
    spt_pushcclosure(S_, detail::generic_cfunc_dispatcher, 1);
    spt_setglobal(S_, name);
  }

  state_t *S_ = nullptr;
};

// ============================================================================
// State - Owning wrapper
// ============================================================================

class state : public state_view {
public:
  // Create new state with default configuration
  state() : state_view(spt_newstate()) {
    if (S_) {
      owned_ = true;
    }
  }

  // Create new state with custom configuration
  state(std::size_t stack_size, std::size_t heap_size, bool enable_gc = true)
      : state_view(spt_newstateex(stack_size, heap_size, enable_gc)) {
    if (S_) {
      owned_ = true;
    }
  }

  // Take ownership of existing state
  explicit state(state_t *S, bool own = true) noexcept : state_view(S), owned_(own) {}

  // Non-copyable
  state(const state &) = delete;
  state &operator=(const state &) = delete;

  // Movable
  state(state &&other) noexcept : state_view(other.S_), owned_(other.owned_) {
    other.S_ = nullptr;
    other.owned_ = false;
  }

  state &operator=(state &&other) noexcept {
    if (this != &other) {
      close();
      S_ = other.S_;
      owned_ = other.owned_;
      other.S_ = nullptr;
      other.owned_ = false;
    }
    return *this;
  }

  ~state() { close(); }

  // Close the state
  void close() {
    if (S_ && owned_) {
      spt_close(S_);
    }
    S_ = nullptr;
    owned_ = false;
  }

  // Release ownership
  state_t *release() noexcept {
    state_t *s = S_;
    S_ = nullptr;
    owned_ = false;
    return s;
  }

  // Open standard libraries
  state &open_libs() {
    if (S_) {
      spt_openlibs(S_);
    }
    return *this;
  }

  // Ownership check
  SPTXX_NODISCARD bool owned() const noexcept { return owned_; }

private:
  bool owned_ = false;
};

// ============================================================================
// Unique State (RAII wrapper with custom deleter support)
// ============================================================================

struct state_deleter {
  void operator()(state_t *S) const {
    if (S) {
      spt_close(S);
    }
  }
};

using unique_state = std::unique_ptr<state_t, state_deleter>;

inline unique_state make_state() { return unique_state(spt_newstate()); }

inline unique_state make_state(std::size_t stack_size, std::size_t heap_size,
                               bool enable_gc = true) {
  return unique_state(spt_newstateex(stack_size, heap_size, enable_gc));
}

// ============================================================================
// State Helper Functions
// ============================================================================

namespace detail {

// Get state from various sources
inline state_t *get_state(state_t *S) { return S; }

inline state_t *get_state(const state &s) { return s.raw(); }

inline state_t *get_state(const state_view &s) { return s.raw(); }

inline state_t *get_state(const unique_state &s) { return s.get(); }

} // namespace detail

// ============================================================================
// Script Execution Helper
// ============================================================================

class script {
public:
  script() = default;

  explicit script(const std::string &source, const std::string &name = "script")
      : source_(source), name_(name) {}

  // Set source
  script &source(const std::string &src) {
    source_ = src;
    return *this;
  }

  script &source(std::string &&src) {
    source_ = std::move(src);
    return *this;
  }

  // Set name
  script &name(const std::string &n) {
    name_ = n;
    return *this;
  }

  // Execute in state
  template <typename State> protected_function_result operator()(State &s) const {
    state_t *S = detail::get_state(s);

    spt_Chunk *chunk = spt_loadstring(S, source_.c_str(), name_.c_str());
    if (!chunk) {
      return protected_function_result(S, 0, 0, status::compile);
    }

    spt_pushchunk(S, chunk);
    spt_freechunk(chunk);

    int top_before = spt_gettop(S) - 1;
    int result = spt_pcall(S, 0, multi_return, 0);
    status stat = static_cast<status>(result);

    int top_after = spt_gettop(S);
    int ret_count = top_after - top_before;

    return protected_function_result(S, top_before + 1, ret_count, stat);
  }

  // Get source
  SPTXX_NODISCARD const std::string &get_source() const { return source_; }

  SPTXX_NODISCARD const std::string &get_name() const { return name_; }

private:
  std::string source_;
  std::string name_ = "script";
};

// ============================================================================
// Inline Script Literal
// ============================================================================

inline script operator""_spt(const char *str, std::size_t len) {
  return script(std::string(str, len));
}

SPTXX_NAMESPACE_END

#endif // SPTXX_STATE_HPP