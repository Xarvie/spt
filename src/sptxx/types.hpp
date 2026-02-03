#ifndef SPTXX_TYPES_HPP
#define SPTXX_TYPES_HPP

#include "forward.hpp"

#include <array>
#include <cstring>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

SPTXX_NAMESPACE_BEGIN

// ============================================================================
// Type Trait Utilities
// ============================================================================

namespace detail {

// Remove cv-ref qualifiers
template <typename T> using remove_cvref_t = std::remove_cv_t<std::remove_reference_t<T>>;

// Check if type is in a list
template <typename T, typename... Ts>
struct is_one_of : std::disjunction<std::is_same<T, Ts>...> {};

template <typename T, typename... Ts>
inline constexpr bool is_one_of_v = is_one_of<T, Ts...>::value;

// Index sequence helpers
template <std::size_t... Is> using index_sequence = std::index_sequence<Is...>;

template <std::size_t N> using make_index_sequence = std::make_index_sequence<N>;

// ============================================================================
// Function Traits
// ============================================================================

template <typename T> struct function_traits;

// Regular function
template <typename R, typename... Args> struct function_traits<R(Args...)> {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
  static constexpr std::size_t arity = sizeof...(Args);
  static constexpr bool is_member = false;
  static constexpr bool is_const = false;
  static constexpr bool is_noexcept = false;

  template <std::size_t N> using arg = std::tuple_element_t<N, args_tuple>;
};

// Function pointer
template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};

// Member function pointer
template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {
  using class_type = C;
  static constexpr bool is_member = true;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {
  using class_type = const C;
  static constexpr bool is_member = true;
  static constexpr bool is_const = true;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) noexcept> : function_traits<R(Args...)> {
  using class_type = C;
  static constexpr bool is_member = true;
  static constexpr bool is_noexcept = true;
};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const noexcept> : function_traits<R(Args...)> {
  using class_type = const C;
  static constexpr bool is_member = true;
  static constexpr bool is_const = true;
  static constexpr bool is_noexcept = true;
};

// Volatile variants
template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) volatile> : function_traits<R (C::*)(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const volatile>
    : function_traits<R (C::*)(Args...) const> {};

// Lambda/functor (via operator())
template <typename T> struct function_traits : function_traits<decltype(&T::operator())> {};

// std::function
template <typename R, typename... Args>
struct function_traits<std::function<R(Args...)>> : function_traits<R(Args...)> {};

// Reference wrapper for function traits
template <typename T> struct function_traits<T &> : function_traits<T> {};

template <typename T> struct function_traits<T &&> : function_traits<T> {};

// ============================================================================
// Callable Detection
// ============================================================================

template <typename T, typename = void> struct is_callable : std::false_type {};

template <typename T>
struct is_callable<T, std::void_t<decltype(&T::operator())>> : std::true_type {};

template <typename R, typename... Args> struct is_callable<R (*)(Args...)> : std::true_type {};

template <typename R, typename... Args> struct is_callable<R (&)(Args...)> : std::true_type {};

template <typename R, typename C, typename... Args>
struct is_callable<R (C::*)(Args...)> : std::true_type {};

template <typename R, typename C, typename... Args>
struct is_callable<R (C::*)(Args...) const> : std::true_type {};

template <typename R, typename C, typename... Args>
struct is_callable<R (C::*)(Args...) noexcept> : std::true_type {};

template <typename R, typename C, typename... Args>
struct is_callable<R (C::*)(Args...) const noexcept> : std::true_type {};

template <typename T> inline constexpr bool is_callable_v = is_callable<T>::value;

// ============================================================================
// Member Data Pointer Traits
// ============================================================================

template <typename T> struct member_data_traits;

template <typename T, typename C> struct member_data_traits<T C::*> {
  using value_type = T;
  using class_type = C;
};

template <typename T> struct is_member_data_pointer : std::false_type {};

template <typename T, typename C>
struct is_member_data_pointer<T C::*> : std::negation<std::is_function<T>> {};

template <typename T>
inline constexpr bool is_member_data_pointer_v = is_member_data_pointer<T>::value;

} // namespace detail

// ============================================================================
// SPT Type Traits
// ============================================================================

// Check if T is nil type
template <typename T> struct is_nil : std::is_same<detail::remove_cvref_t<T>, nil_t> {};

template <typename T> inline constexpr bool is_nil_v = is_nil<T>::value;

// Check if T is none type
template <typename T> struct is_none : std::is_same<detail::remove_cvref_t<T>, none_t> {};

template <typename T> inline constexpr bool is_none_v = is_none<T>::value;

// Check if T is a boolean type
template <typename T> struct is_boolean : std::is_same<detail::remove_cvref_t<T>, bool> {};

template <typename T> inline constexpr bool is_boolean_v = is_boolean<T>::value;

// Check if T is an integer type (maps to spt_Int)
template <typename T>
struct is_integer : std::conjunction<std::is_integral<detail::remove_cvref_t<T>>,
                                     std::negation<std::is_same<detail::remove_cvref_t<T>, bool>>> {
};

template <typename T> inline constexpr bool is_integer_v = is_integer<T>::value;

// Check if T is a floating point type
template <typename T> struct is_floating : std::is_floating_point<detail::remove_cvref_t<T>> {};

template <typename T> inline constexpr bool is_floating_v = is_floating<T>::value;

// Check if T is a number (integer or float)
template <typename T> struct is_number : std::disjunction<is_integer<T>, is_floating<T>> {};

template <typename T> inline constexpr bool is_number_v = is_number<T>::value;

// Check if T is a string type
template <typename T, typename = void> struct is_string : std::false_type {};

template <> struct is_string<const char *> : std::true_type {};

template <> struct is_string<char *> : std::true_type {};

template <> struct is_string<std::string> : std::true_type {};

template <> struct is_string<std::string_view> : std::true_type {};

template <std::size_t N> struct is_string<char[N]> : std::true_type {};

template <std::size_t N> struct is_string<const char[N]> : std::true_type {};

template <typename T>
inline constexpr bool is_string_v = is_string<detail::remove_cvref_t<T>>::value;

// Check if T is a C function
template <typename T>
struct is_c_function : std::is_same<detail::remove_cvref_t<T>, cfunction_t> {};

template <typename T> inline constexpr bool is_c_function_v = is_c_function<T>::value;

// Check if T is light userdata
template <typename T>
struct is_lightuserdata
    : std::conjunction<std::is_pointer<detail::remove_cvref_t<T>>, std::negation<is_string<T>>,
                       std::negation<is_c_function<T>>> {};

template <typename T> inline constexpr bool is_lightuserdata_v = is_lightuserdata<T>::value;

// Check if T is a container (vector, array, etc.)
template <typename T, typename = void> struct is_container : std::false_type {};

template <typename T>
struct is_container<
    T, std::void_t<typename T::value_type, typename T::iterator,
                   decltype(std::declval<T>().begin()), decltype(std::declval<T>().end())>>
    : std::negation<is_string<T>> {};

template <typename T> inline constexpr bool is_container_v = is_container<T>::value;

// Check if T is associative container (map-like)
template <typename T, typename = void> struct is_associative_container : std::false_type {};

template <typename T>
struct is_associative_container<T, std::void_t<typename T::key_type, typename T::mapped_type>>
    : std::true_type {};

template <typename T>
inline constexpr bool is_associative_container_v = is_associative_container<T>::value;

// Check if T is a sequence container (vector, list, array)
template <typename T>
struct is_sequence_container
    : std::conjunction<is_container<T>, std::negation<is_associative_container<T>>> {};

template <typename T>
inline constexpr bool is_sequence_container_v = is_sequence_container<T>::value;

// Check if T is a tuple
template <typename T> struct is_tuple : std::false_type {};

template <typename... Args> struct is_tuple<std::tuple<Args...>> : std::true_type {};

template <typename T1, typename T2> struct is_tuple<std::pair<T1, T2>> : std::true_type {};

template <typename T> inline constexpr bool is_tuple_v = is_tuple<detail::remove_cvref_t<T>>::value;

// Check if T is optional
template <typename T> struct is_optional : std::false_type {};

template <typename T> struct is_optional<std::optional<T>> : std::true_type {};

template <typename T>
inline constexpr bool is_optional_v = is_optional<detail::remove_cvref_t<T>>::value;

// Check if T is a variant
template <typename T> struct is_variant : std::false_type {};

template <typename... Ts> struct is_variant<std::variant<Ts...>> : std::true_type {};

template <typename T>
inline constexpr bool is_variant_v = is_variant<detail::remove_cvref_t<T>>::value;

// ============================================================================
// Type to SPT Type Mapping
// ============================================================================

template <typename T, typename = void> struct spt_type_of {
  static constexpr type value = type::cinstance; // Default: userdata
};

template <> struct spt_type_of<nil_t> {
  static constexpr type value = type::nil;
};

template <> struct spt_type_of<none_t> {
  static constexpr type value = type::none;
};

template <> struct spt_type_of<bool> {
  static constexpr type value = type::boolean;
};

template <typename T> struct spt_type_of<T, std::enable_if_t<is_integer_v<T>>> {
  static constexpr type value = type::integer;
};

template <typename T> struct spt_type_of<T, std::enable_if_t<is_floating_v<T>>> {
  static constexpr type value = type::floating;
};

template <typename T> struct spt_type_of<T, std::enable_if_t<is_string_v<T>>> {
  static constexpr type value = type::string;
};

template <typename T> struct spt_type_of<T, std::enable_if_t<is_c_function_v<T>>> {
  static constexpr type value = type::closure;
};

template <typename T> struct spt_type_of<T, std::enable_if_t<is_sequence_container_v<T>>> {
  static constexpr type value = type::list;
};

template <typename T> struct spt_type_of<T, std::enable_if_t<is_associative_container_v<T>>> {
  static constexpr type value = type::map;
};

template <typename T>
inline constexpr type spt_type_of_v = spt_type_of<detail::remove_cvref_t<T>>::value;

// ============================================================================
// Type Information
// ============================================================================

template <typename T> struct type_info {
  static constexpr const char *name() {
    if constexpr (is_nil_v<T>)
      return "nil";
    else if constexpr (is_none_v<T>)
      return "none";
    else if constexpr (is_boolean_v<T>)
      return "boolean";
    else if constexpr (is_integer_v<T>)
      return "integer";
    else if constexpr (is_floating_v<T>)
      return "float";
    else if constexpr (is_string_v<T>)
      return "string";
    else if constexpr (is_c_function_v<T>)
      return "cfunction";
    else if constexpr (is_sequence_container_v<T>)
      return "list";
    else if constexpr (is_associative_container_v<T>)
      return "map";
    else if constexpr (is_optional_v<T>)
      return "optional";
    else if constexpr (is_variant_v<T>)
      return "variant";
    else
      return "userdata";
  }

  static constexpr type spt_type = spt_type_of_v<T>;
};

// ============================================================================
// Checked Type Wrappers
// ============================================================================

// Wrapper to indicate type-checked get
template <typename T> struct checked {
  using type = T;
  T value;

  checked() = default;

  explicit checked(T v) : value(std::move(v)) {}

  operator T &() & { return value; }

  operator const T &() const & { return value; }

  operator T &&() && { return std::move(value); }
};

// Wrapper to indicate unchecked get (faster, no type verification)
template <typename T> struct unchecked {
  using type = T;
  T value;

  unchecked() = default;

  explicit unchecked(T v) : value(std::move(v)) {}

  operator T &() & { return value; }

  operator const T &() const & { return value; }

  operator T &&() && { return std::move(value); }
};

// ============================================================================
// Property Wrappers
// ============================================================================

// Property wrapper for getters/setters
template <typename T, typename Getter, typename Setter = void> struct property {
  Getter getter;
  Setter setter;

  property(Getter g, Setter s) : getter(std::move(g)), setter(std::move(s)) {}
};

template <typename T, typename Getter> struct property<T, Getter, void> {
  Getter getter;

  explicit property(Getter g) : getter(std::move(g)) {}
};

// Factory functions
template <typename T, typename Getter, typename Setter> auto make_property(Getter &&g, Setter &&s) {
  return property<T, std::decay_t<Getter>, std::decay_t<Setter>>{std::forward<Getter>(g),
                                                                 std::forward<Setter>(s)};
}

template <typename T, typename Getter> auto make_readonly_property(Getter &&g) {
  return property<T, std::decay_t<Getter>, void>{std::forward<Getter>(g)};
}

// Variable wrapper (for binding member variables)
template <typename T> struct var_wrapper {
  T value;

  var_wrapper() = default;

  explicit var_wrapper(T v) : value(std::move(v)) {}
};

template <typename T> var_wrapper<T> var(T &&v) {
  return var_wrapper<std::decay_t<T>>{std::forward<T>(v)};
}

// ============================================================================
// Constructor Wrapper
// ============================================================================

template <typename... Args> struct constructor_list {};

template <typename T, typename... Args> struct constructor {
  using type = T;
  using args = std::tuple<Args...>;
  static constexpr std::size_t arity = sizeof...(Args);
};

template <typename... Args> using constructors = std::tuple<constructor<Args>...>;

// Factory constructor wrapper
template <typename Func> struct factory_wrapper {
  Func func;

  explicit factory_wrapper(Func f) : func(std::move(f)) {}
};

template <typename Func> auto factory(Func &&f) {
  return factory_wrapper<std::decay_t<Func>>{std::forward<Func>(f)};
}

// ============================================================================
// Base Classes Wrapper
// ============================================================================

template <typename... Bases> struct bases {};

// ============================================================================
// Call Result Type
// ============================================================================

template <typename... Ts> struct call_result {
  std::tuple<Ts...> values;
  status stat;

  call_result(status s) : stat(s) {}

  template <typename... Args>
  call_result(status s, Args &&...args) : values(std::forward<Args>(args)...), stat(s) {}

  SPTXX_NODISCARD bool valid() const noexcept { return stat == status::ok; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  template <std::size_t I> auto &get() & { return std::get<I>(values); }

  template <std::size_t I> const auto &get() const & { return std::get<I>(values); }

  template <std::size_t I> auto &&get() && { return std::get<I>(std::move(values)); }
};

template <> struct call_result<> {
  status stat;

  call_result(status s) : stat(s) {}

  SPTXX_NODISCARD bool valid() const noexcept { return stat == status::ok; }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }
};

template <typename T> struct call_result<T> {
  std::optional<T> value;
  status stat;

  call_result(status s) : stat(s) {}

  call_result(status s, T &&v) : value(std::move(v)), stat(s) {}

  call_result(status s, const T &v) : value(v), stat(s) {}

  SPTXX_NODISCARD bool valid() const noexcept { return stat == status::ok && value.has_value(); }

  SPTXX_NODISCARD explicit operator bool() const noexcept { return valid(); }

  T &get() & { return *value; }

  const T &get() const & { return *value; }

  T &&get() && { return std::move(*value); }

  T &operator*() & { return *value; }

  const T &operator*() const & { return *value; }

  T &&operator*() && { return std::move(*value); }
};

// ============================================================================
// Overload Resolution
// ============================================================================

template <typename... Funcs> struct overloaded : Funcs... {
  using Funcs::operator()...;

  overloaded(Funcs... fs) : Funcs(std::move(fs))... {}
};

template <typename... Funcs> overloaded<Funcs...> overload(Funcs &&...fs) {
  return overloaded<std::decay_t<Funcs>...>{std::forward<Funcs>(fs)...};
}

// ============================================================================
// Policy Types
// ============================================================================

// Ownership transfer policy
enum class ownership {
  copy,      // Copy the value
  reference, // Keep as reference
  move       // Move the value
};

// Reference semantics
template <typename T> struct as_returns {
  T &value;

  explicit as_returns(T &v) : value(v) {}
};

template <typename T> as_returns<T> ret(T &value) { return as_returns<T>(value); }

// Out parameter
template <typename T> struct out_param {
  T *ptr;

  explicit out_param(T &v) : ptr(&v) {}
};

template <typename T> out_param<T> out(T &value) { return out_param<T>(value); }

// ============================================================================
// Compile-time utilities
// ============================================================================

namespace detail {

// Compile-time string
template <std::size_t N> struct ct_string {
  char data[N];

  constexpr ct_string(const char (&str)[N]) {
    for (std::size_t i = 0; i < N; ++i) {
      data[i] = str[i];
    }
  }

  constexpr const char *c_str() const { return data; }

  constexpr std::size_t size() const { return N - 1; }
};

// Type list
template <typename... Ts> struct type_list {
  static constexpr std::size_t size = sizeof...(Ts);
};

template <typename List, std::size_t I> struct type_at;

template <typename T, typename... Ts> struct type_at<type_list<T, Ts...>, 0> {
  using type = T;
};

template <typename T, typename... Ts, std::size_t I>
struct type_at<type_list<T, Ts...>, I> : type_at<type_list<Ts...>, I - 1> {};

template <typename List, std::size_t I> using type_at_t = typename type_at<List, I>::type;

// Check if all types satisfy a predicate
template <template <typename> class Pred, typename... Ts>
struct all_of : std::conjunction<Pred<Ts>...> {};

template <template <typename> class Pred> struct all_of<Pred> : std::true_type {};

template <template <typename> class Pred, typename... Ts>
inline constexpr bool all_of_v = all_of<Pred, Ts...>::value;

// Check if any type satisfies a predicate
template <template <typename> class Pred, typename... Ts>
struct any_of : std::disjunction<Pred<Ts>...> {};

template <template <typename> class Pred> struct any_of<Pred> : std::false_type {};

template <template <typename> class Pred, typename... Ts>
inline constexpr bool any_of_v = any_of<Pred, Ts...>::value;

// Count types satisfying a predicate
template <template <typename> class Pred, typename... Ts> struct count_if;

template <template <typename> class Pred> struct count_if<Pred> {
  static constexpr std::size_t value = 0;
};

template <template <typename> class Pred, typename T, typename... Ts>
struct count_if<Pred, T, Ts...> {
  static constexpr std::size_t value = (Pred<T>::value ? 1 : 0) + count_if<Pred, Ts...>::value;
};

template <template <typename> class Pred, typename... Ts>
inline constexpr std::size_t count_if_v = count_if<Pred, Ts...>::value;

} // namespace detail

SPTXX_NAMESPACE_END

#endif // SPTXX_TYPES_HPP