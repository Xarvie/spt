#ifndef SPTXX_FORWARD_HPP
#define SPTXX_FORWARD_HPP

#include "config.hpp"
#include <spt.h>

// Standard library includes (must be outside namespace)
#include <functional>
#include <optional>
#include <string_view>
#include <variant>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Core Types from C API
// ============================================================================

using state_t = spt_State;
using ast_t = spt_Ast;
using chunk_t = spt_Chunk;
using compiler_t = spt_Compiler;
using integer_t = spt_Int;
using number_t = spt_Float;
using cfunction_t = spt_CFunction;
using kfunction_t = spt_KFunction;
using kcontext_t = spt_KContext;
using stack_index_t = spt_Index;

// ============================================================================
// Status Codes
// ============================================================================

enum class status : int {
  ok = SPT_OK,
  yield = SPT_YIELD,
  runtime = SPT_ERRRUN,
  syntax = SPT_ERRSYNTAX,
  compile = SPT_ERRCOMPILE,
  memory = SPT_ERRMEM,
  error = SPT_ERRERR,
  file = SPT_ERRFILE
};

inline bool is_ok(status s) noexcept { return s == status::ok; }

inline bool is_error(status s) noexcept { return s != status::ok && s != status::yield; }

// ============================================================================
// Type Tags
// ============================================================================

enum class type : int {
  none = SPT_TNONE,
  nil = SPT_TNIL,
  boolean = SPT_TBOOL,
  integer = SPT_TINT,
  floating = SPT_TFLOAT,
  string = SPT_TSTRING,
  list = SPT_TLIST,
  map = SPT_TMAP,
  object = SPT_TOBJECT,
  closure = SPT_TCLOSURE,
  class_type = SPT_TCLASS,
  upvalue = SPT_TUPVALUE,
  fiber = SPT_TFIBER,
  cinstance = SPT_TCINSTANCE,
  lightuserdata = SPT_TLIGHTUSERDATA
};

inline const char *type_name(type t) noexcept {
  switch (t) {
  case type::none:
    return "none";
  case type::nil:
    return "nil";
  case type::boolean:
    return "boolean";
  case type::integer:
    return "integer";
  case type::floating:
    return "float";
  case type::string:
    return "string";
  case type::list:
    return "list";
  case type::map:
    return "map";
  case type::object:
    return "object";
  case type::closure:
    return "closure";
  case type::class_type:
    return "class";
  case type::upvalue:
    return "upvalue";
  case type::fiber:
    return "fiber";
  case type::cinstance:
    return "cinstance";
  case type::lightuserdata:
    return "lightuserdata";
  default:
    return "unknown";
  }
}

// ============================================================================
// Fiber States
// ============================================================================

enum class fiber_status : int {
  fresh = SPT_FIBER_NEW,
  running = SPT_FIBER_RUNNING,
  suspended = SPT_FIBER_SUSPENDED,
  done = SPT_FIBER_DONE,
  error = SPT_FIBER_ERROR
};

// ============================================================================
// Magic Methods
// ============================================================================

enum class magic_method : int {
  init = SPT_MM_INIT,
  gc = SPT_MM_GC,
  get = SPT_MM_GET,
  set = SPT_MM_SET,
  index_get = SPT_MM_INDEX_GET,
  index_set = SPT_MM_INDEX_SET,
  add = SPT_MM_ADD,
  sub = SPT_MM_SUB,
  mul = SPT_MM_MUL,
  div = SPT_MM_DIV,
  mod = SPT_MM_MOD,
  pow = SPT_MM_POW,
  unm = SPT_MM_UNM,
  idiv = SPT_MM_IDIV,
  eq = SPT_MM_EQ,
  lt = SPT_MM_LT,
  le = SPT_MM_LE,
  band = SPT_MM_BAND,
  bor = SPT_MM_BOR,
  bxor = SPT_MM_BXOR,
  bnot = SPT_MM_BNOT,
  shl = SPT_MM_SHL,
  shr = SPT_MM_SHR,
  max = SPT_MM_MAX
};

// ============================================================================
// GC Operations
// ============================================================================

enum class gc_mode : int {
  stop = SPT_GCSTOP,
  restart = SPT_GCRESTART,
  collect = SPT_GCCOLLECT,
  count_kb = SPT_GCCOUNT,
  count_bytes = SPT_GCCOUNTB,
  step = SPT_GCSTEP,
  set_pause = SPT_GCSETPAUSE,
  set_stepmul = SPT_GCSETSTEPMUL,
  is_running = SPT_GCISRUNNING,
  obj_count = SPT_GCOBJCOUNT
};

// ============================================================================
// Special Indices
// ============================================================================

constexpr stack_index_t registry_index = SPT_REGISTRYINDEX;
// constexpr stack_index_t globals_index = SPT_GLOBALSINDEX;
constexpr int multi_return = SPT_MULTRET;
constexpr int no_ref = SPT_NOREF;
constexpr int nil_ref = SPT_REFNIL;

// ============================================================================
// Forward Declarations - Core Classes
// ============================================================================

// State Management
class state;
class state_view;

// References
class reference;
class main_reference;
class stack_reference;

// Stack Operations (default arg only here, not in definition)
struct stack_guard;
template <typename T, typename = void> struct stack_pusher;
template <typename T, typename = void> struct stack_getter;
template <typename T, typename = void> struct stack_checker;

// Value Types
class object;
class list;
class map;
class function;
class protected_function;
class coroutine;
class userdata;
template <typename T> class usertype;

// Proxies
template <typename Table, typename Key> class proxy;
class global_proxy;

// Results
template <typename... Args> struct function_result;
class protected_function_result;
class load_result;

// Errors
class error;
class type_error;
class stack_error;

// Utilities
struct nil_t;
struct none_t;
struct variadic_args;
struct this_state;

// ============================================================================
// Special Value Markers
// ============================================================================

struct nil_t {
  constexpr nil_t() noexcept = default;

  constexpr bool operator==(nil_t) const noexcept { return true; }

  constexpr bool operator!=(nil_t) const noexcept { return false; }
};

inline constexpr nil_t nil{};

struct none_t {
  constexpr none_t() noexcept = default;
};

inline constexpr none_t none{};

// ============================================================================
// Policy Tags
// ============================================================================

// Ownership policies
struct no_safety_tag {};

struct safety_check_tag {};

inline constexpr no_safety_tag no_safety{};
inline constexpr safety_check_tag safety_check{};

// Reference policies
struct copy_tag {};

struct reference_tag {};

struct move_tag {};

inline constexpr copy_tag copy{};
inline constexpr reference_tag as_reference{};
inline constexpr move_tag as_move{};

// Readonly policy
struct readonly_tag {};

// Note: readonly constexpr instance removed to avoid conflict with readonly() function in sptxx.hpp

// Writeonly policy
struct writeonly_tag {};

// Note: writeonly constexpr instance removed to avoid conflict with writeonly() function in
// sptxx.hpp

// Yielding policy for coroutines
struct yielding_tag {};

inline constexpr yielding_tag yielding{};

// Variadic indicator
struct variadic_args {
  state_t *S;
  int start_index;
  int count;

  variadic_args(state_t *s, int start, int n) : S(s), start_index(start), count(n) {}
};

// This state indicator (for getting state in bound functions)
struct this_state {
  state_t *S;

  explicit this_state(state_t *s) : S(s) {}

  operator state_t *() const noexcept { return S; }
};

// ============================================================================
// Basic Type Aliases
// ============================================================================

using c_closure = cfunction_t;
using light_userdata = void *;

// String view type (C++17)
using string_view = ::std::string_view;

// Optional type
template <typename T> using optional = ::std::optional<T>;

// Variant type
template <typename... Ts> using variant = ::std::variant<Ts...>;

// Function type
template <typename Signature> using std_function = ::std::function<Signature>;

SPTXX_NAMESPACE_END

#endif // SPTXX_FORWARD_HPP
