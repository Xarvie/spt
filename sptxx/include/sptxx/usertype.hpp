// usertype.hpp - 类绑定系统
// 支持：变参构造函数、成员属性、成员方法、运算符重载、三种所有权语义。
//
// 存储格式：userdata 内仅存 T*（8 字节）。这与用户特化 pusher<T> 的常见写法
// （存 T*）一致，保证 operator 结果能被后续 method/operator 正确读取。
//
// 所有权：
// - owned:   使用类型 metatable（如 "Player"），__gc 会 delete T*。
// - unowned: 使用 "Player#unowned" metatable，__gc 不操作。
// - shared:  使用类型 metatable，但把 shared_ptr<T>* 登记到 side-registry；
//            __gc 优先查 side-registry，命中则删除 shared_ptr（refcount--），
//            未命中则 delete T*（owned 路径）。
//
// SPT Slot 0 约定：方法调用 a.b(args) 时 receiver=a 在 index 1，用户参数从 index 2。
// 因此 method wrapper 从 index 1 读 self，从 index 2+ 读参数。
// 运算符是 metamethod，不走 Slot 0：__add(a,b) 中 a 在 index 1、b 在 index 2。

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include "error.hpp"
#include "function.hpp"
#include "stack.hpp"
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <tuple>
#include <type_traits>
#include <utility>

namespace sptxx {

enum class ownership {
  owned,   // Lua 拥有对象，GC 时 delete
  unowned, // C++ 拥有对象，GC 不处理
  shared   // shared_ptr 共享，GC 时 refcount--
};

enum class operator_type {
  add, sub, mul, div, mod, pow, unm, idiv,
  band, bor, bxor, bnot, shl, shr,
  concat, len, eq, lt, le, tostring
};

namespace detail {

inline const char *operator_metafield(operator_type op) {
  switch (op) {
    case operator_type::add:    return "__add";
    case operator_type::sub:    return "__sub";
    case operator_type::mul:    return "__mul";
    case operator_type::div:    return "__div";
    case operator_type::mod:    return "__mod";
    case operator_type::pow:    return "__pow";
    case operator_type::unm:    return "__unm";
    case operator_type::idiv:   return "__idiv";
    case operator_type::band:   return "__band";
    case operator_type::bor:    return "__bor";
    case operator_type::bxor:   return "__bxor";
    case operator_type::bnot:   return "__bnot";
    case operator_type::shl:    return "__shl";
    case operator_type::shr:    return "__shr";
    case operator_type::concat: return "__concat";
    case operator_type::len:    return "__len";
    case operator_type::eq:     return "__eq";
    case operator_type::lt:     return "__lt";
    case operator_type::le:     return "__le";
    case operator_type::tostring: return "__tostring";
  }
  return nullptr;
}

// shared_ptr side-registry：用 lightuserdata(ud 指针) 作 key，
// value 为 lightuserdata(shared_ptr<T>*)。
// 存放在 registry 中，key 为 "sptxx.shared_registry"。
inline void ensure_shared_registry(lua_State *L) {
  lua_getfield(L, LUA_REGISTRYINDEX, "sptxx.shared_registry");
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_setfield(L, LUA_REGISTRYINDEX, "sptxx.shared_registry");
  } else {
    lua_pop(L, 1);
  }
}

inline void shared_registry_set(lua_State *L, void *ud_key, void *sp_ptr) {
  lua_getfield(L, LUA_REGISTRYINDEX, "sptxx.shared_registry");
  lua_pushlightuserdata(L, ud_key);
  lua_pushlightuserdata(L, sp_ptr);
  lua_settable(L, -3);
  lua_pop(L, 1);
}

// 返回 sp_ptr（若存在），并从 registry 移除。不存在返回 nullptr。
inline void *shared_registry_take(lua_State *L, void *ud_key) {
  lua_getfield(L, LUA_REGISTRYINDEX, "sptxx.shared_registry");
  lua_pushlightuserdata(L, ud_key);
  lua_gettable(L, -2); // 取 value
  void *sp_ptr = lua_isnil(L, -1) ? nullptr : lua_touserdata(L, -1);
  lua_pop(L, 1);
  if (sp_ptr) {
    lua_pushlightuserdata(L, ud_key);
    lua_pushnil(L);
    lua_settable(L, -3);
  }
  lua_pop(L, 1);
  return sp_ptr;
}

// 通用：从 userdata 中读出 T*。所有 usertype 对象的 userdata 都只存 T*。
template <typename T> inline T *get_object_ptr(lua_State *L, int index) {
  void *ud = lua_touserdata(L, index);
  if (!ud)
    return nullptr;
  return *static_cast<T **>(ud);
}

} // namespace detail

// ---- method_overload_set：usertype 方法重载分派 ----
template <typename T, typename... MethodPtrs> struct method_overload_set {
  std::tuple<MethodPtrs...> methods;

  template <typename... Args>
  method_overload_set(Args &&...args) : methods(std::forward<Args>(args)...) {}
  method_overload_set() = default;

  int dispatch(lua_State *L) {
    return dispatch_impl(L, std::index_sequence_for<MethodPtrs...>{});
  }

  template <std::size_t... Is> int dispatch_impl(lua_State *L, std::index_sequence<Is...>) {
    int result = -1;
    bool done = false;
    ((!done && ((result = try_method<Is>(L)) >= 0) ? (done = true, true) : false), ...);
    if (result < 0)
      throw error("no matching method overload for given arguments");
    return result;
  }

  template <std::size_t I> int try_method(lua_State *L) {
    using MethodPtr = std::tuple_element_t<I, std::tuple<MethodPtrs...>>;
    using Traits = detail::function_traits<MethodPtr>;
    using ReturnType = typename Traits::return_type;
    using ArgsTuple = typename Traits::args_tuple;
    if (!detail::check_args<ArgsTuple>(L))
      return -1;
    T *obj = detail::get_object_ptr<T>(L, 1);
    if (!obj)
      return luaL_error(L, "null object in method overload");
    auto method = std::get<I>(methods);
    return call_method_impl<ReturnType, MethodPtr>(L, obj, method, ArgsTuple{});
  }

  template <typename R, typename MethodPtr, typename... Args>
  int call_method_impl(lua_State *L, T *obj, MethodPtr method, std::tuple<Args...>) {
    auto args = detail::extract_args_from_2<Args...>(L);
    if constexpr (std::is_void_v<R>) {
      std::apply([obj, method](auto &&...a) { (obj->*method)(std::forward<decltype(a)>(a)...); },
                 std::move(args));
      return 0;
    } else {
      R result = std::apply(
          [obj, method](auto &&...a) { return (obj->*method)(std::forward<decltype(a)>(a)...); },
          std::move(args));
      stack::push(L, std::move(result));
      return 1;
    }
  }
};

template <typename T> class usertype {
public:
  usertype(lua_State *L, const char *name) : L_(L), type_name_(name) {
    detail::ensure_shared_registry(L_);

    // 主 metatable（owned 语义）
    if (luaL_getmetatable(L_, name) == LUA_TTABLE) {
      lua_pop(L_, 1);
      return; // 已注册
    }
    lua_pop(L_, 1);

    luaL_newmetatable(L_, name);
    lua_pushstring(L_, name);
    lua_setfield(L_, -2, "__name");

    lua_pushcfunction(L_, &usertype::gc_owned);
    lua_setfield(L_, -2, "__gc");

    lua_pushcfunction(L_, &usertype::index_handler);
    lua_setfield(L_, -2, "__index");

    lua_pushcfunction(L_, &usertype::newindex_handler);
    lua_setfield(L_, -2, "__newindex");

    lua_newtable(L_);
    lua_setfield(L_, -2, "__getters");
    lua_newtable(L_);
    lua_setfield(L_, -2, "__setters");
    lua_newtable(L_);
    lua_setfield(L_, -2, "__methods");

    lua_pushvalue(L_, -1);
    lua_setfield(L_, -2, "__index_self"); // 便于 index_handler 回退
    lua_pop(L_, 1);

    // unowned metatable：继承主 metatable 的 __index/__newindex，但 __gc 为空
    std::string unowned_name = std::string(name) + "#unowned";
    luaL_newmetatable(L_, unowned_name.c_str());
    lua_pushstring(L_, unowned_name.c_str());
    lua_setfield(L_, -2, "__name");

    lua_pushcfunction(L_, &usertype::gc_unowned);
    lua_setfield(L_, -2, "__gc");

    // 复用主 metatable 的 __index / __newindex / __getters / __setters / __methods
    luaL_getmetatable(L_, name);
    const char *fields[] = {"__index", "__newindex", "__getters", "__setters",
                            "__methods", nullptr};
    for (int i = 0; fields[i]; ++i) {
      lua_getfield(L_, -1, fields[i]);
      lua_setfield(L_, -3, fields[i]);
    }
    lua_pop(L_, 1);
    lua_pop(L_, 1);
  }

  usertype(const usertype &) = delete;
  usertype &operator=(const usertype &) = delete;
  usertype(usertype &&) noexcept = default;
  usertype &operator=(usertype &&) noexcept = default;
  ~usertype() = default;

  lua_State *lua_state() const { return L_; }
  const std::string &name() const { return type_name_; }

  // ---- 构造函数注册 ----
  template <typename... Args> void constructor() {
    auto ctor_wrapper = [](lua_State *L) -> int {
      const char *name = lua_tostring(L, lua_upvalueindex(1));
      try {
        void *ud = lua_newuserdatauv(L, sizeof(T *), 0);
        *static_cast<T **>(ud) = nullptr; // 先置空：若下方 extract/construct 抛异常，
                                          // __gc 会读到 nullptr 而跳过 delete，避免 UB
        luaL_getmetatable(L, name);
        lua_setmetatable(L, -2);

        auto args = detail::extract_args_from_2<Args...>(L);
        T *obj = std::apply(
            [](auto &&...unpacked) { return new T(std::forward<decltype(unpacked)>(unpacked)...); },
            std::move(args));
        *static_cast<T **>(ud) = obj;
        return 1;
      } catch (...) {
        return detail::propagate_exception(L);
      }
    };

    // 确保类型表存在
    if (lua_getglobal(L_, type_name_.c_str()) != LUA_TTABLE) {
      lua_pop(L_, 1);
      lua_newtable(L_);
      lua_pushvalue(L_, -1);
      lua_setglobal(L_, type_name_.c_str());
    }

    if (!lua_getmetatable(L_, -1)) {
      // lua_getmetatable 返回 0 时什么都没压栈，不要 pop
      lua_newtable(L_);
    }

    lua_pushstring(L_, type_name_.c_str());
    lua_pushcclosure(L_, ctor_wrapper, 1);
    lua_setfield(L_, -2, "__call");

    lua_setmetatable(L_, -2);
    lua_pop(L_, 1);
  }

  // ---- 成员属性 ----
  template <typename U> void set(const char *name, U T::*member) {
    luaL_getmetatable(L_, type_name_.c_str());

    // getter
    lua_getfield(L_, -1, "__getters");
    void *gstorage = lua_newuserdatauv(L_, sizeof(U T::*), 0);
    std::memcpy(gstorage, &member, sizeof(U T::*));
    lua_pushcclosure(L_, &usertype::member_getter<U>, 1);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 1);

    // setter
    lua_getfield(L_, -1, "__setters");
    void *sstorage = lua_newuserdatauv(L_, sizeof(U T::*), 0);
    std::memcpy(sstorage, &member, sizeof(U T::*));
    lua_pushcclosure(L_, &usertype::member_setter<U>, 1);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 1);

    lua_pop(L_, 1);
  }

  // ---- 成员方法 ----
  template <typename R, typename... Args> void set(const char *name, R (T::*func)(Args...)) {
    using FuncType = R (T::*)(Args...);
    register_method_closure<FuncType, R, Args...>(name, func);
  }
  template <typename R, typename... Args>
  void set(const char *name, R (T::*func)(Args...) const) {
    using FuncType = R (T::*)(Args...) const;
    register_method_closure<FuncType, R, Args...>(name, func);
  }

  // ---- 属性函数 getter/setter（替代成员指针，支持计算属性/只读/带副作用）----
  // const getter + setter
  template <typename R, typename V>
  void set(const char *name, R (T::*getter)() const, void (T::*setter)(V)) {
    register_property_getter<R>(name, getter);
    register_property_setter<V>(name, setter);
  }
  // 非 const getter + setter
  template <typename R, typename V>
  void set(const char *name, R (T::*getter)(), void (T::*setter)(V)) {
    register_property_getter<R>(name, getter);
    register_property_setter<V>(name, setter);
  }
  // 只读属性：const getter
  template <typename R>
  void set_readonly(const char *name, R (T::*getter)() const) {
    register_property_getter<R>(name, getter);
    // 不注册 setter → newindex_handler 遇到无 setter 的字段会报错
  }
  // 只读属性：非 const getter
  template <typename R>
  void set_readonly(const char *name, R (T::*getter)()) {
    register_property_getter<R>(name, getter);
  }

  // ---- 方法重载：注册多个同名成员函数，按参数分派 ----
  template <typename... MethodPtrs>
  void set_overload(const char *name, MethodPtrs... methods) {
    using MOS = method_overload_set<T, MethodPtrs...>;
    luaL_getmetatable(L_, type_name_.c_str());
    lua_getfield(L_, -1, "__methods");
    void *storage = lua_newuserdatauv(L_, sizeof(MOS), 0);
    new (storage) MOS{std::move(methods)...};

    // __gc metatable 析构 MOS
    lua_createtable(L_, 0, 1);
    lua_pushcfunction(L_, [](lua_State *L) -> int {
      MOS *self = static_cast<MOS *>(lua_touserdata(L, 1));
      self->~MOS();
      return 0;
    });
    lua_setfield(L_, -2, "__gc");
    lua_setmetatable(L_, -2);

    lua_pushcclosure(
        L_,
        [](lua_State *L) -> int {
          MOS *mos = static_cast<MOS *>(lua_touserdata(L, lua_upvalueindex(1)));
          try {
            return mos->dispatch(L);
          } catch (...) {
            return detail::propagate_exception(L);
          }
        },
        1);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 2);
  }

  // ---- 继承链：声明 T 派生自 Base ----
  // 单继承假设：Derived* 与 Base* 地址相同，因此 Base 的 getter/setter/method
  // 闭包直接通过 get_object_ptr<Base>(L,1) 读取同一 userdata 即可。
  // base_name 必须是 Base 在 Lua 中注册的类型名（如 "Base"）。
  // 同时在 owned 与 unowned 两个 metatable 上设置 __base，使两种所有权语义
  // 都支持继承。
  template <typename Base> void base(const char *base_name) {
    static_assert(std::is_base_of_v<Base, T>,
                  "T must derive from Base; sptxx usertype supports single inheritance only");
    auto set_on = [&](const char *mt_name) {
      luaL_getmetatable(L_, mt_name);
      if (lua_isnil(L_, -1)) { lua_pop(L_, 1); return; }
      lua_pushstring(L_, base_name);
      lua_setfield(L_, -2, "__base");
      lua_pop(L_, 1);
    };
    set_on(type_name_.c_str());
    set_on((type_name_ + "#unowned").c_str());
  }

  // ---- 静态成员：注册到类型同名的全局 table 上 ----
  // Type.static_method(args)  /  Type.static_field
  // 可调用类型（函数指针/lambda/functor）→ 静态方法；
  // 其它类型 → 静态字段。依赖 is_function_like_v 编译期分派。
  template <typename U> void set_static(const char *name, U &&val) {
    ensure_type_global();
    using Decayed = std::decay_t<U>;
    if constexpr (detail::is_overload_set_v<Decayed>) {
      detail::push_overload_wrapper(L_, std::forward<U>(val));
    } else if constexpr (detail::is_function_like_v<Decayed>) {
      detail::push_function_wrapper(L_, std::forward<U>(val));
    } else {
      stack::push(L_, std::forward<U>(val));
    }
    lua_setfield(L_, -2, name);
    lua_pop(L_, 1);
  }

  // ---- push 对象到 Lua ----
  void push_owned(T *obj) {
    void *ud = lua_newuserdatauv(L_, sizeof(T *), 0);
    *static_cast<T **>(ud) = obj;
    luaL_getmetatable(L_, type_name_.c_str());
    lua_setmetatable(L_, -2);
  }

  void push_unowned(T *obj) {
    void *ud = lua_newuserdatauv(L_, sizeof(T *), 0);
    *static_cast<T **>(ud) = obj;
    std::string unowned_name = type_name_ + "#unowned";
    luaL_getmetatable(L_, unowned_name.c_str());
    lua_setmetatable(L_, -2);
  }

  void push_shared(std::shared_ptr<T> obj) {
    void *ud = lua_newuserdatauv(L_, sizeof(T *), 0);
    T *raw = obj.get();
    *static_cast<T **>(ud) = raw;
    luaL_getmetatable(L_, type_name_.c_str());
    lua_setmetatable(L_, -2);

    // 把 shared_ptr 堆拷贝登记到 side-registry，GC 时释放
    auto *sp = new std::shared_ptr<T>(std::move(obj));
    detail::shared_registry_set(L_, ud, sp);
  }

  // ---- 运算符重载 ----
  template <operator_type Op, typename Func> void set_operator(Func &&func) {
    using FuncType = std::decay_t<Func>;
    constexpr bool is_unary = (Op == operator_type::unm || Op == operator_type::bnot ||
                               Op == operator_type::len || Op == operator_type::tostring);

    luaL_getmetatable(L_, type_name_.c_str());
    void *storage = lua_newuserdatauv(L_, sizeof(FuncType), 0);
    new (storage) FuncType(std::forward<Func>(func));

    if constexpr (is_unary) {
      lua_pushcclosure(L_, &usertype::unary_operator_wrapper<FuncType>, 1);
    } else {
      lua_pushcclosure(L_, &usertype::binary_operator_wrapper<FuncType>, 1);
    }
    lua_setfield(L_, -2, detail::operator_metafield(Op));

    // unowned metatable 也要能看到运算符
    std::string unowned_name = type_name_ + "#unowned";
    if (luaL_getmetatable(L_, unowned_name.c_str()) == LUA_TTABLE) {
      void *storage2 = lua_newuserdatauv(L_, sizeof(FuncType), 0);
      new (storage2) FuncType(std::forward<Func>(func));
      if constexpr (is_unary) {
        lua_pushcclosure(L_, &usertype::unary_operator_wrapper<FuncType>, 1);
      } else {
        lua_pushcclosure(L_, &usertype::binary_operator_wrapper<FuncType>, 1);
      }
      lua_setfield(L_, -2, detail::operator_metafield(Op));
      lua_pop(L_, 1);
    } else {
      lua_pop(L_, 1);
    }

    lua_pop(L_, 1);
  }

  template <typename Func> void set_add(Func &&f)    { set_operator<operator_type::add>(std::forward<Func>(f)); }
  template <typename Func> void set_sub(Func &&f)    { set_operator<operator_type::sub>(std::forward<Func>(f)); }
  template <typename Func> void set_mul(Func &&f)    { set_operator<operator_type::mul>(std::forward<Func>(f)); }
  template <typename Func> void set_div(Func &&f)    { set_operator<operator_type::div>(std::forward<Func>(f)); }
  template <typename Func> void set_mod(Func &&f)    { set_operator<operator_type::mod>(std::forward<Func>(f)); }
  template <typename Func> void set_pow(Func &&f)    { set_operator<operator_type::pow>(std::forward<Func>(f)); }
  template <typename Func> void set_unm(Func &&f)    { set_operator<operator_type::unm>(std::forward<Func>(f)); }
  template <typename Func> void set_idiv(Func &&f)   { set_operator<operator_type::idiv>(std::forward<Func>(f)); }
  template <typename Func> void set_band(Func &&f)   { set_operator<operator_type::band>(std::forward<Func>(f)); }
  template <typename Func> void set_bor(Func &&f)    { set_operator<operator_type::bor>(std::forward<Func>(f)); }
  template <typename Func> void set_bxor(Func &&f)   { set_operator<operator_type::bxor>(std::forward<Func>(f)); }
  template <typename Func> void set_bnot(Func &&f)   { set_operator<operator_type::bnot>(std::forward<Func>(f)); }
  template <typename Func> void set_shl(Func &&f)    { set_operator<operator_type::shl>(std::forward<Func>(f)); }
  template <typename Func> void set_shr(Func &&f)    { set_operator<operator_type::shr>(std::forward<Func>(f)); }
  template <typename Func> void set_concat(Func &&f) { set_operator<operator_type::concat>(std::forward<Func>(f)); }
  template <typename Func> void set_len(Func &&f)    { set_operator<operator_type::len>(std::forward<Func>(f)); }
  template <typename Func> void set_eq(Func &&f)     { set_operator<operator_type::eq>(std::forward<Func>(f)); }
  template <typename Func> void set_lt(Func &&f)     { set_operator<operator_type::lt>(std::forward<Func>(f)); }
  template <typename Func> void set_le(Func &&f)     { set_operator<operator_type::le>(std::forward<Func>(f)); }
  template <typename Func> void set_tostring(Func &&f) { set_operator<operator_type::tostring>(std::forward<Func>(f)); }

private:
  lua_State *L_;
  std::string type_name_;

  // 确保类型同名的全局 table 存在（用于静态成员注册），并压入栈顶。
  // 若已存在则直接用；不存在则新建空 table 并 setglobal。
  void ensure_type_global() {
    if (lua_getglobal(L_, type_name_.c_str()) != LUA_TTABLE) {
      lua_pop(L_, 1);
      lua_newtable(L_);
      lua_pushvalue(L_, -1);
      lua_setglobal(L_, type_name_.c_str());
    }
  }

  // ---- __gc ----
  static int gc_owned(lua_State *L) {
    void *ud = lua_touserdata(L, 1);
    if (!ud) return 0;
    // shared 优先：若 side-registry 命中，释放 shared_ptr（refcount--）
    if (void *sp_ptr = detail::shared_registry_take(L, ud)) {
      auto *sp = static_cast<std::shared_ptr<T> *>(sp_ptr);
      delete sp;
      return 0;
    }
    // owned 路径：delete T*
    T *obj = *static_cast<T **>(ud);
    if (obj) delete obj;
    return 0;
  }

  static int gc_unowned(lua_State *L) {
    // unowned：不释放对象。但若误登记为 shared 也要清理 registry。
    void *ud = lua_touserdata(L, 1);
    if (ud) detail::shared_registry_take(L, ud); // 仅清理，不 delete
    return 0;
  }

  // ---- __index / __newindex ----
  // 沿 __base 链查找：getter → method → metatable 自身字段 → __base → 递归。
  static int index_handler(lua_State *L) {
    const char *key = lua_tostring(L, 2);
    if (!key) return 0;

    lua_getmetatable(L, 1); // mt（栈顶）
    for (;;) {
      // 1. getter
      lua_getfield(L, -1, "__getters");
      lua_getfield(L, -1, key);
      if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, 1);
        lua_call(L, 1, 1);
        return 1;
      }
      lua_pop(L, 2);

      // 2. method
      lua_getfield(L, -1, "__methods");
      lua_getfield(L, -1, key);
      if (!lua_isnil(L, -1)) {
        return 1;
      }
      lua_pop(L, 2);

      // 3. metatable 自身字段（如运算符、__name）
      lua_getfield(L, -1, key);
      if (!lua_isnil(L, -1)) {
        return 1;
      }
      lua_pop(L, 1);

      // 4. __base 链
      lua_getfield(L, -1, "__base");
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2); // __base nil + mt
        return 0;
      }
      const char *base_name = lua_tostring(L, -1);
      lua_pop(L, 2); // __base + 当前 mt
      if (!base_name) return 0;
      luaL_getmetatable(L, base_name); // 压入 base mt 作为新栈顶
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return 0;
      }
    }
  }

  static int newindex_handler(lua_State *L) {
    const char *key = lua_tostring(L, 2);
    if (!key) return luaL_error(L, "field name must be a string");

    lua_getmetatable(L, 1); // mt（栈顶）
    for (;;) {
      lua_getfield(L, -1, "__setters");
      lua_getfield(L, -1, key);
      if (!lua_isnil(L, -1)) {
        lua_pushvalue(L, 1);
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
        return 0;
      }
      lua_pop(L, 2);

      // __base 链
      lua_getfield(L, -1, "__base");
      if (lua_isnil(L, -1)) {
        lua_pop(L, 2);
        return luaL_error(L, "cannot set field '%s'", key);
      }
      const char *base_name = lua_tostring(L, -1);
      lua_pop(L, 2);
      if (!base_name)
        return luaL_error(L, "cannot set field '%s'", key);
      luaL_getmetatable(L, base_name);
      if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return luaL_error(L, "cannot set field '%s'", key);
      }
    }
  }

  // ---- 成员 getter/setter ----
  template <typename U> static int member_getter(lua_State *L) {
    T *obj = detail::get_object_ptr<T>(L, 1);
    if (!obj) return luaL_error(L, "null object in getter");

    U T::*member;
    std::memcpy(&member, lua_touserdata(L, lua_upvalueindex(1)), sizeof(member));
    stack::push(L, obj->*member);
    return 1;
  }

  template <typename U> static int member_setter(lua_State *L) {
    T *obj = detail::get_object_ptr<T>(L, 1);
    if (!obj) return luaL_error(L, "null object in setter");

    U T::*member;
    std::memcpy(&member, lua_touserdata(L, lua_upvalueindex(1)), sizeof(member));
    obj->*member = stack::get<std::decay_t<U>>(L, 2);
    return 0;
  }

  // ---- 属性函数 getter/setter wrapper ----
  // GetterPtr 形如 R (T::*)() const 或 R (T::*)()
  template <typename GetterPtr, typename R> static int property_getter(lua_State *L) {
    T *obj = detail::get_object_ptr<T>(L, 1);
    if (!obj) return luaL_error(L, "null object in property getter");
    GetterPtr g;
    std::memcpy(&g, lua_touserdata(L, lua_upvalueindex(1)), sizeof(g));
    if constexpr (std::is_void_v<R>) {
      (obj->*g)();
      return 0;
    } else {
      R result = (obj->*g)();
      stack::push(L, std::move(result));
      return 1;
    }
  }

  // SetterPtr 形如 void (T::*)(V)
  template <typename SetterPtr, typename V> static int property_setter(lua_State *L) {
    T *obj = detail::get_object_ptr<T>(L, 1);
    if (!obj) return luaL_error(L, "null object in property setter");
    SetterPtr s;
    std::memcpy(&s, lua_touserdata(L, lua_upvalueindex(1)), sizeof(s));
    V value = stack::get<std::decay_t<V>>(L, 2);
    (obj->*s)(std::move(value));
    return 0;
  }

  // ---- 属性函数注册 ----
  template <typename R, typename GetterType>
  void register_property_getter(const char *name, GetterType getter) {
    luaL_getmetatable(L_, type_name_.c_str());
    lua_getfield(L_, -1, "__getters");
    void *storage = lua_newuserdatauv(L_, sizeof(GetterType), 0);
    std::memcpy(storage, &getter, sizeof(getter));
    lua_pushcclosure(L_, &usertype::property_getter<GetterType, R>, 1);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 2);
  }

  template <typename V, typename SetterType>
  void register_property_setter(const char *name, SetterType setter) {
    luaL_getmetatable(L_, type_name_.c_str());
    lua_getfield(L_, -1, "__setters");
    void *storage = lua_newuserdatauv(L_, sizeof(SetterType), 0);
    std::memcpy(storage, &setter, sizeof(setter));
    lua_pushcclosure(L_, &usertype::property_setter<SetterType, V>, 1);
    lua_setfield(L_, -2, name);
    lua_pop(L_, 2);
  }

  // ---- 方法注册 ----
  template <typename FuncType, typename R, typename... Args>
  void register_method_closure(const char *name, FuncType func) {
    luaL_getmetatable(L_, type_name_.c_str());
    lua_getfield(L_, -1, "__methods");

    void *storage = lua_newuserdatauv(L_, sizeof(FuncType), 0);
    std::memcpy(storage, &func, sizeof(FuncType));
    lua_pushcclosure(
        L_,
        [](lua_State *L) -> int {
          T *obj = detail::get_object_ptr<T>(L, 1);
          if (!obj) return luaL_error(L, "null object in method call");

          FuncType f;
          std::memcpy(&f, lua_touserdata(L, lua_upvalueindex(1)), sizeof(f));

          try {
            auto args = detail::extract_args_from_2<Args...>(L);
            if constexpr (std::is_void_v<R>) {
              std::apply([obj, f](auto &&...unpacked) { (obj->*f)(std::forward<decltype(unpacked)>(unpacked)...); },
                         std::move(args));
              return 0;
            } else {
              R result = std::apply(
                  [obj, f](auto &&...unpacked) { return (obj->*f)(std::forward<decltype(unpacked)>(unpacked)...); },
                  std::move(args));
              stack::push(L, std::move(result));
              return 1;
            }
          } catch (...) {
            return detail::propagate_exception(L);
          }
        },
        1);
    lua_setfield(L_, -2, name);

    // unowned metatable 也需共享 __methods（已通过构造时复制，无需重复）
    lua_pop(L_, 2);
  }

  // ---- 运算符 wrapper ----
  // 二元：__add(a, b) → a 在 index 1，b 在 index 2（metamethod，无 Slot 0）
  template <typename FuncType> static int binary_operator_wrapper(lua_State *L) {
    T *a = detail::get_object_ptr<T>(L, 1);
    if (!a) return luaL_error(L, "null object in binary operator");

    FuncType *func = static_cast<FuncType *>(lua_touserdata(L, lua_upvalueindex(1)));

    try {
      if constexpr (std::is_invocable_v<FuncType, const T &, const T &>) {
        T *b = detail::get_object_ptr<T>(L, 2);
        if (!b) return luaL_error(L, "null second operand in binary operator");
        auto result = (*func)(*a, *b);
        stack::push(L, std::move(result));
        return 1;
      } else if constexpr (std::is_invocable_v<FuncType, lua_State *, const T &>) {
        return (*func)(L, *a);
      } else if constexpr (detail::function_traits<FuncType>::arity >= 2) {
        // 标量第二操作数：按函数参数类型选择 lua_tointeger/lua_tonumber，
        // 用 static_cast 显式转换，避免 lua_Integer→float/double 的 C4244 隐式窄化警告
        using SecondArg = std::decay_t<
            std::tuple_element_t<1, typename detail::function_traits<FuncType>::args_tuple>>;
        if constexpr (std::is_floating_point_v<SecondArg>) {
          auto result = (*func)(*a, static_cast<SecondArg>(lua_tonumber(L, 2)));
          stack::push(L, std::move(result));
          return 1;
        } else {
          auto result = (*func)(*a, static_cast<SecondArg>(lua_tointeger(L, 2)));
          stack::push(L, std::move(result));
          return 1;
        }
      } else {
        return (*func)(L, *a);
      }
    } catch (...) {
      return detail::propagate_exception(L);
    }
  }

  // 一元：__unm(a) → a 在 index 1
  template <typename FuncType> static int unary_operator_wrapper(lua_State *L) {
    T *a = detail::get_object_ptr<T>(L, 1);
    if (!a) return luaL_error(L, "null object in unary operator");

    FuncType *func = static_cast<FuncType *>(lua_touserdata(L, lua_upvalueindex(1)));

    try {
      if constexpr (std::is_invocable_v<FuncType, const T &>) {
        auto result = (*func)(*a);
        stack::push(L, std::move(result));
        return 1;
      } else {
        return (*func)(L, *a);
      }
    } catch (...) {
      return detail::propagate_exception(L);
    }
  }
};

} // namespace sptxx
