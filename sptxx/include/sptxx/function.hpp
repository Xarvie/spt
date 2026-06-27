// function.hpp - 函数绑定与 Lua 函数引用
// - push_function_wrapper: 把任意可调用对象包装成 userdata + cclosure，
//   每次 set_function 都独立存储（无 static 共享 bug）。
// - function_ref<R(Args...)>: 持有 Lua 函数的 registry 引用，可被 C++ 调用。
//
// SPT Slot 0 约定：普通函数调用 a(args...) 时，index 1 是 receiver（nil），
// 用户参数从 index 2 开始。因此 C++ 函数包装器从 index 2 提取参数；
// function_ref 调用 Lua 时先压 nil 作为 receiver，再压参数。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "error.hpp"
#include "stack.hpp"
#include "variadic.hpp"
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sptxx {

namespace detail {

// ---- 参数提取 ----

template <typename T> inline T extract_arg(lua_State *L, int index) {
  return stack::get<T>(L, index);
}

template <typename Tuple, std::size_t... I>
inline Tuple extract_args_impl(lua_State *L, int start_idx, std::index_sequence<I...>) {
  return Tuple{extract_arg<std::tuple_element_t<I, Tuple>>(L, start_idx + static_cast<int>(I))...};
}

// 从 index 2 开始提取（跳过 Slot 0 receiver）。供被 Lua 调用的 C++ 函数使用。
template <typename... Args>
inline std::tuple<std::decay_t<Args>...> extract_args_from_2(lua_State *L) {
  return extract_args_impl<std::tuple<std::decay_t<Args>...>>(L, 2,
                                                              std::index_sequence_for<Args...>{});
}

// ---- 多返回值推送 ----

template <typename... Ts> inline void push_args(lua_State *L, Ts &&...args) {
  (stack::push(L, std::forward<Ts>(args)), ...);
}

template <typename Tuple, std::size_t... Is>
inline void push_multi_return_impl(lua_State *L, Tuple &&values, std::index_sequence<Is...>) {
  (stack::push(L, std::get<Is>(std::forward<Tuple>(values))), ...);
}

template <typename... Ts>
inline void push_multi_return(lua_State *L, std::tuple<Ts...> &&values) {
  push_multi_return_impl(L, std::move(values), std::index_sequence_for<Ts...>{});
}

// ---- 函数特征 ----

template <typename Func> struct function_traits;

template <typename R, typename... Args> struct function_traits<R(Args...)> {
  using return_type = R;
  using args_tuple = std::tuple<Args...>;
  static constexpr std::size_t arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R (*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...)> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) noexcept> : function_traits<R(Args...)> {};

template <typename R, typename C, typename... Args>
struct function_traits<R (C::*)(Args...) const noexcept> : function_traits<R(Args...)> {};

template <typename T>
struct function_traits : function_traits<decltype(&T::operator())> {};

template <typename T> struct function_traits<std::function<T>> : function_traits<T> {};

template <typename T> using function_traits_t = function_traits<std::decay_t<T>>;

// ---- 调用器 ----

template <typename R, typename Func, typename ArgsTuple> struct function_caller;

template <typename R, typename Func, typename... Args>
struct function_caller<R, Func, std::tuple<Args...>> {
  static int call(lua_State *L, Func &f) {
    auto args = extract_args_from_2<Args...>(L);
    if constexpr (std::is_void_v<R>) {
      std::apply(f, std::move(args));
      return 0;
    } else {
      R result = std::apply(f, std::move(args));
      stack::push(L, std::move(result));
      return 1;
    }
  }
};

// 多返回值：R = std::tuple<Rs...>
template <typename... Rs, typename Func, typename... Args>
struct function_caller<std::tuple<Rs...>, Func, std::tuple<Args...>> {
  static int call(lua_State *L, Func &f) {
    auto args = extract_args_from_2<Args...>(L);
    auto result = std::apply(f, std::move(args));
    push_multi_return(L, std::move(result));
    return static_cast<int>(sizeof...(Rs));
  }
};

// ---- 重载：参数类型预检查（避免 luaL_check* 直接 lua_error 无法 try/catch）----

template <typename T> inline bool check_arg(lua_State *L, int index) {
  using DT = std::decay_t<T>;
  if constexpr (std::is_same_v<DT, bool>) {
    return lua_isboolean(L, index) != 0;
  } else if constexpr (std::is_integral_v<DT>) {
    return lua_isinteger(L, index) != 0;
  } else if constexpr (std::is_floating_point_v<DT>) {
    return lua_isnumber(L, index) != 0;
  } else if constexpr (std::is_same_v<DT, std::string> ||
                      std::is_same_v<DT, const char *> ||
                      std::is_same_v<DT, std::string_view>) {
    return lua_isstring(L, index) != 0;
  } else {
    (void)L;
    (void)index;
    return true; // 未知/自定义类型放宽，交给 getter 报错
  }
}

template <typename ArgsTuple, std::size_t... Is>
inline bool check_args_impl(lua_State *L, int start, std::index_sequence<Is...>) {
  return (check_arg<std::tuple_element_t<Is, ArgsTuple>>(
              L, start + static_cast<int>(Is)) &&
          ...);
}

// 检查 Lua 栈（跳过 Slot 0 receiver）参数是否匹配 ArgsTuple。
// 若最后一个参数为 variadic_args，则按"至少 N-1 个固定参数"校验，
// 且只对固定参数做类型检查。
template <typename ArgsTuple> inline bool check_args(lua_State *L) {
  int available = lua_gettop(L) - 1; // 跳 Slot 0
  constexpr std::size_t size = std::tuple_size_v<ArgsTuple>;
  if constexpr (size > 0) {
    using LastT = std::decay_t<std::tuple_element_t<size - 1, ArgsTuple>>;
    if constexpr (std::is_same_v<LastT, variadic_args>) {
      if (available < static_cast<int>(size) - 1)
        return false;
      return check_args_impl<ArgsTuple>(
          L, 2, std::make_index_sequence<size - 1>{});
    } else {
      if (available != static_cast<int>(size))
        return false;
      return check_args_impl<ArgsTuple>(
          L, 2, std::make_index_sequence<size>{});
    }
  } else {
    return available == 0;
  }
}

// ---- overload_set：持有多个可调用对象，按参数分派 ----

template <typename... Funcs> struct overload_set {
  std::tuple<Funcs...> funcs;

  template <typename... Args>
  overload_set(Args &&...args) : funcs(std::forward<Args>(args)...) {}

  int dispatch(lua_State *L) {
    return dispatch_impl(L, std::index_sequence_for<Funcs...>{});
  }

  template <std::size_t... Is> int dispatch_impl(lua_State *L, std::index_sequence<Is...>) {
    int result = -1;
    bool done = false;
    ((!done && ((result = try_one<Is>(L)) >= 0) ? (done = true, true) : false), ...);
    if (result < 0)
      throw error("no matching overload for given arguments");
    return result;
  }

  template <std::size_t I> int try_one(lua_State *L) {
    using FuncType = std::tuple_element_t<I, std::tuple<Funcs...>>;
    using Traits = function_traits<FuncType>;
    using ReturnType = typename Traits::return_type;
    using ArgsTuple = typename Traits::args_tuple;
    if (!check_args<ArgsTuple>(L))
      return -1;
    auto &func = std::get<I>(funcs);
    return function_caller<ReturnType, FuncType, ArgsTuple>::call(L, func);
  }
};

template <typename T> struct is_overload_set : std::false_type {};
template <typename... Fs> struct is_overload_set<overload_set<Fs...>> : std::true_type {};
template <typename T> inline constexpr bool is_overload_set_v = is_overload_set<std::decay_t<T>>::value;

// ---- is_function_like：判断是否为可调用类型（函数指针/lambda/functor）----
// 用于 set_static 区分静态方法 vs 静态字段。成员函数指针不算（需要对象）。
template <typename T, typename = void> struct is_function_like : std::false_type {};
template <typename R, typename... Args>
struct is_function_like<R(Args...)> : std::true_type {};
template <typename R, typename... Args>
struct is_function_like<R (*)(Args...)> : std::true_type {};
template <typename T>
struct is_function_like<
    T, std::void_t<decltype(&std::decay_t<T>::operator())>> : std::true_type {};
template <typename T>
inline constexpr bool is_function_like_v = is_function_like<std::decay_t<T>>::value;

// ---- overload_set 的栈包装（类似 push_function_wrapper）----

template <typename... Funcs>
void push_overload_wrapper(lua_State *L, overload_set<Funcs...> &&os) {
  using OSType = overload_set<Funcs...>;
  void *storage = lua_newuserdatauv(L, sizeof(OSType), 0);
  new (storage) OSType(std::move(os));

  lua_createtable(L, 0, 1);
  lua_pushcfunction(L, [](lua_State *L) -> int {
    OSType *self = static_cast<OSType *>(lua_touserdata(L, 1));
    self->~OSType();
    return 0;
  });
  lua_setfield(L, -2, "__gc");
  lua_setmetatable(L, -2);

  lua_pushcclosure(
      L,
      [](lua_State *L) -> int {
        OSType *os = static_cast<OSType *>(lua_touserdata(L, lua_upvalueindex(1)));
        try {
          return os->dispatch(L);
        } catch (...) {
          return propagate_exception(L);
        }
      },
      1);
}

// ---- 函数包装器：userdata + __gc + cclosure ----
// 每次调用都创建独立的 userdata 存储 functor，避免 static 共享 bug。

template <typename Func> void push_function_wrapper(lua_State *L, Func &&f) {
  using FuncType = std::decay_t<Func>;
  if constexpr (is_overload_set_v<FuncType>) {
    push_overload_wrapper(L, std::forward<Func>(f));
    return;
  } else {
    using Traits = function_traits<FuncType>;
    using ReturnType = typename Traits::return_type;
    using ArgsTuple = typename Traits::args_tuple;

    // 1. 创建 userdata 存储 functor
    void *storage = lua_newuserdatauv(L, sizeof(FuncType), 0);
    new (storage) FuncType(std::forward<Func>(f));

    // 2. 设置 metatable，仅含 __gc 析构 userdata
    lua_createtable(L, 0, 1);
    lua_pushcfunction(L, [](lua_State *L) -> int {
      FuncType *self = static_cast<FuncType *>(lua_touserdata(L, 1));
      self->~FuncType();
      return 0;
    });
    lua_setfield(L, -2, "__gc");
    lua_setmetatable(L, -2);

    // 3. 创建 cclosure，userdata 作为 upvalue
    lua_pushcclosure(
        L,
        [](lua_State *L) -> int {
          FuncType *func = static_cast<FuncType *>(lua_touserdata(L, lua_upvalueindex(1)));
          try {
            return function_caller<ReturnType, FuncType, ArgsTuple>::call(L, *func);
          } catch (...) {
            return propagate_exception(L);
          }
        },
        1);
  }
}

// ---- 多返回值提取（function_ref 调用 Lua 后用） ----

template <typename T> struct is_tuple : std::false_type {};
template <typename... Ts> struct is_tuple<std::tuple<Ts...>> : std::true_type {};
template <typename T> inline constexpr bool is_tuple_v = is_tuple<T>::value;

template <typename Tuple, std::size_t... Is>
inline Tuple extract_multi_return_impl(lua_State *L, int base_idx, std::index_sequence<Is...>) {
  return Tuple{stack::get<std::tuple_element_t<Is, Tuple>>(
      L, base_idx + static_cast<int>(Is))...};
}

template <typename Tuple> inline Tuple extract_multi_return(lua_State *L, int base_idx) {
  return extract_multi_return_impl<Tuple>(L, base_idx,
                                          std::make_index_sequence<std::tuple_size_v<Tuple>>{});
}

} // namespace detail

// ---- overload 工厂函数（公开 API，返回 detail::overload_set）----

template <typename... Funcs>
detail::overload_set<std::decay_t<Funcs>...> overload(Funcs &&...funcs) {
  return detail::overload_set<std::decay_t<Funcs>...>{std::forward<Funcs>(funcs)...};
}

// ---- function_ref<R(Args...)>：持有 Lua 函数引用 ----

template <typename Signature> class function_ref;

template <typename R, typename... Args> class function_ref<R(Args...)> {
public:
  function_ref() : L_(nullptr), ref_(LUA_NOREF) {}

  function_ref(lua_State *L, int ref) : L_(L), ref_(ref) {}

  function_ref(const function_ref &other) : L_(other.L_), ref_(LUA_NOREF) {
    if (other.valid()) {
      lua_getref(L_, other.ref_);
      ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }
  }

  function_ref(function_ref &&other) noexcept : L_(other.L_), ref_(other.ref_) {
    other.L_ = nullptr;
    other.ref_ = LUA_NOREF;
  }

  function_ref &operator=(const function_ref &other) {
    if (this != &other) {
      release();
      L_ = other.L_;
      if (other.valid()) {
        lua_getref(L_, other.ref_);
        ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
      } else {
        ref_ = LUA_NOREF;
      }
    }
    return *this;
  }

  function_ref &operator=(function_ref &&other) noexcept {
    if (this != &other) {
      release();
      L_ = other.L_;
      ref_ = other.ref_;
      other.L_ = nullptr;
      other.ref_ = LUA_NOREF;
    }
    return *this;
  }

  ~function_ref() { release(); }

  bool valid() const { return L_ != nullptr && ref_ != LUA_NOREF && ref_ != LUA_REFNIL; }
  explicit operator bool() const { return valid(); }

  lua_State *lua_state() const { return L_; }
  int registry_index() const { return ref_; }

  // 调用 Lua 函数：压入 func + nil(receiver) + args，然后 pcall。
  R operator()(Args... args) const {
    if (!valid())
      throw error("invalid function reference");

    lua_getref(L_, ref_);
    if (!lua_isfunction(L_, -1)) {
      lua_pop(L_, 1);
      throw error("reference is not a function");
    }

    lua_pushnil(L_); // Slot 0 receiver：普通调用为 nil
    detail::push_args(L_, std::forward<Args>(args)...);

    int nargs = static_cast<int>(sizeof...(Args)) + 1; // +1 for receiver
    int nresults;
    if constexpr (std::is_void_v<R>) {
      nresults = 0;
    } else if constexpr (detail::is_tuple_v<R>) {
      nresults = static_cast<int>(std::tuple_size_v<R>);
    } else {
      nresults = 1;
    }

    int status = lua_pcall(L_, nargs, nresults, 0);
    if (status != LUA_OK) {
      const char *msg = lua_tostring(L_, -1);
      std::string err = msg ? std::string(msg) : std::string("unknown Lua error");
      lua_pop(L_, 1);
      throw runtime_error(std::move(err));
    }

    if constexpr (std::is_void_v<R>) {
      return;
    } else if constexpr (detail::is_tuple_v<R>) {
      R ret = detail::extract_multi_return<R>(L_, -nresults);
      lua_pop(L_, nresults);
      return ret;
    } else {
      R ret = stack::get<R>(L_, -1);
      lua_pop(L_, 1);
      return ret;
    }
  }

private:
  lua_State *L_;
  int ref_;

  void release() {
    if (valid())
      luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
  }
};

// ---- stack 特化 ----

template <typename R, typename... Args> struct getter<function_ref<R(Args...)>> {
  static function_ref<R(Args...)> get(lua_State *L, int index) {
    if (lua_isnil(L, index))
      return function_ref<R(Args...)>();
    if (!lua_isfunction(L, index))
      luaL_error(L, "expected function at index %d", index);
    lua_pushvalue(L, index);
    int ref = luaL_ref(L, LUA_REGISTRYINDEX);
    return function_ref<R(Args...)>(L, ref);
  }
};

template <typename R, typename... Args> struct pusher<function_ref<R(Args...)>> {
  static void push(lua_State *L, const function_ref<R(Args...)> &value) {
    if (value.valid())
      lua_getref(L, value.registry_index());
    else
      lua_pushnil(L);
  }
};

} // namespace sptxx
