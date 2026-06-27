// test_exception_bridge.cpp - 测试 C++ 异常经 sptxx 桥接到 Lua 错误
// 注册的 C++ 函数抛异常 → 被 Lua pcall 捕获，消息正确传播。
//
// 设计说明：SPT 的 `vars x, y = pcall(f);` 声明的是 *局部* 变量，
// 出了 do_string 作用域就消失，C++ get_global 读不到。
// 因此用 C++ 回调函数捕获 pcall 的多返回值。

#include "sptxx.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

struct Capture {
  bool ok = true;
  std::string err;
};

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    Capture cap_std, cap_sptxx, cap_generic, cap_normal;

    lua.set_function("throw_std", []() -> void {
      throw std::runtime_error("cpp_runtime_error");
    });
    lua.set_function("throw_sptxx", []() -> void {
      throw sptxx::error("cpp_sptxx_error");
    });
    lua.set_function("throw_generic", []() -> void {
      throw 42; // 非 std::exception
    });
    lua.set_function("normal_fn", []() -> int { return 100; });

    lua.set_function("cap_std",     [&cap_std](bool ok, std::string e)     { cap_std = {ok, e}; });
    lua.set_function("cap_sptxx",   [&cap_sptxx](bool ok, std::string e)   { cap_sptxx = {ok, e}; });
    lua.set_function("cap_generic", [&cap_generic](bool ok, std::string e) { cap_generic = {ok, e}; });
    lua.set_function("cap_normal",  [&cap_normal](bool ok, int v)          { cap_normal = {ok, std::to_string(v)}; });

    // SPT 脚本：用 vars 接 pcall 多返回值，然后通过回调送回 C++
    lua.do_string(R"(
        vars ok1, err1 = pcall(throw_std);
        cap_std(ok1, err1);

        vars ok2, err2 = pcall(throw_sptxx);
        cap_sptxx(ok2, err2);

        vars ok3, err3 = pcall(throw_generic);
        cap_generic(ok3, err3);

        vars ok4, val4 = pcall(normal_fn);
        cap_normal(ok4, val4);
    )");

    int pass = 0, fail = 0;
    auto check = [&](bool cond, const char *msg) {
      if (cond) { std::cout << "PASS: " << msg << "\n"; pass++; }
      else      { std::cerr << "FAIL: " << msg << "\n"; fail++; }
    };

    // 1. std::runtime_error
    check(!cap_std.ok, "pcall catches std::exception");
    check(cap_std.err.find("cpp_runtime_error") != std::string::npos,
          "std exception message propagated");

    // 2. sptxx::error
    check(!cap_sptxx.ok, "pcall catches sptxx::error");
    check(cap_sptxx.err.find("cpp_sptxx_error") != std::string::npos,
          "sptxx error message propagated");

    // 3. 非 std::exception（throw 42）
    check(!cap_generic.ok, "pcall catches generic throw");
    check(cap_generic.err.find("unknown C++ exception") != std::string::npos,
          "generic exception message");

    // 4. 正常函数 pcall 成功
    check(cap_normal.ok, "pcall succeeds on normal fn");
    check(cap_normal.err == "100", "normal return value");

    std::cout << "=== exception_bridge: " << pass << " passed, " << fail << " failed ===\n";
    return fail ? 1 : 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
