// test_state_extended.cpp - 测试 do_file() 和 basic_state 附加外部 lua_State*
#include "sptxx.hpp"
#include <cstdio>
#include <iostream>
#include <string>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

int main() {
  try {
    // ---- 1. do_file() ----
    {
      sptxx::state lua;
      lua.open_libraries();

      // 写临时脚本文件
      // 注意：SPT 中 `vars` 声明局部变量；要让 C++ 通过 get_global 读到，
      // 必须用裸赋值（写入全局）。
      const char *filename = "sptxx_do_file_test.spt";
      FILE *f = fopen(filename, "w");
      if (!f) {
        std::cerr << "FAIL: cannot create temp file\n";
        return 1;
      }
      fputs("file_val = 12345;\n", f);
      fclose(f);

      lua.do_file(filename);
      remove(filename); // 清理

      int val = lua.get_global<int>("file_val");
      if (val != 12345) {
        std::cerr << "FAIL: do_file value = " << val << " want 12345\n";
        return 1;
      }
      std::cout << "PASS: do_file: file_val = " << val << "\n";
    }

    // ---- 2. basic_state 附加外部 lua_State*（不持有）----
    {
      lua_State *raw_L = luaL_newstate();
      luaL_openlibs(raw_L);

      {
        sptxx::state lua(raw_L, false); // 不持有
        lua.set_global("ext_val", 99);
        // 裸赋值写入全局，避免 vars 局部变量
        lua.do_string("ext_doubled = ext_val * 2;");
      }
      // basic_state 析构后 raw_L 仍然有效（未关闭）
      lua_getglobal(raw_L, "ext_doubled");
      if (lua_isnil(raw_L, -1)) {
        std::cerr << "FAIL: external state closed prematurely\n";
        lua_close(raw_L);
        return 1;
      }
      int doubled = (int)lua_tointeger(raw_L, -1);
      lua_pop(raw_L, 1);
      if (doubled != 198) {
        std::cerr << "FAIL: ext_doubled = " << doubled << " want 198\n";
        lua_close(raw_L);
        return 1;
      }
      std::cout << "PASS: external state (no ownership): ext_doubled = " << doubled << "\n";
      lua_close(raw_L); // 手动关闭
    }

    // ---- 3. basic_state 附加外部 lua_State*（持有）----
    {
      lua_State *raw_L = luaL_newstate();
      luaL_openlibs(raw_L);

      {
        sptxx::state lua(raw_L, true); // 持有，析构时 lua_close
        lua.set_global("owned_val", 777);
        int v = lua.get_global<int>("owned_val");
        if (v != 777) {
          std::cerr << "FAIL: owned state get = " << v << "\n";
          return 1;
        }
      }
      // raw_L 已被 basic_state 析构关闭，无需手动 close
      std::cout << "PASS: external state (owned): closed on destruction\n";
    }

    std::cout << "=== All state extended tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
