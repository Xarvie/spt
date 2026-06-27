// error.hpp - SPT Lua 5.5 C++ 绑定的错误处理
// 提供 error 异常基类与 Lua/C++ 异常双向桥接

#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <exception>
#include <string>

namespace sptxx {

// 所有 sptxx 抛出的异常的基类
class error : public std::exception {
public:
  explicit error(std::string msg) : msg_(std::move(msg)) {}
  explicit error(const char *msg) : msg_(msg) {}

  const char *what() const noexcept override { return msg_.c_str(); }

private:
  std::string msg_;
};

// 运行时错误（Lua 错误、无效引用等）
class runtime_error : public error {
public:
  using error::error;
};

// 类型错误（类型不匹配、引用非预期类型等）
class type_error : public error {
public:
  using error::error;
};

namespace detail {

// 将当前 C++ 异常转换为 Lua 错误（压入消息字符串并调用 lua_error）。
// 仅供 C 回调（被 Lua 调用的 C 函数）的 catch(...) 块使用。
inline int propagate_exception(lua_State *L) {
  std::string msg;
  try {
    throw;
  } catch (const error &e) {
    msg = e.what();
  } catch (const std::exception &e) {
    msg = e.what();
  } catch (...) {
    msg = "unknown C++ exception";
  }
  lua_pushstring(L, msg.c_str());
  return lua_error(L);
}

// 检查 Lua 调用状态码，非 LUA_OK 时抛出 runtime_error。
inline void handle_lua_error(lua_State *L, int status) {
  if (status != LUA_OK) {
    const char *msg = lua_tostring(L, -1);
    std::string err = msg ? std::string(msg) : std::string("unknown Lua error");
    lua_pop(L, 1);
    throw runtime_error(std::move(err));
  }
}

} // namespace detail

} // namespace sptxx
