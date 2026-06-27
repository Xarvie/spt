// test_table_proxy.cpp - 测试 table_proxy 链式访问

#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // ---- 1. 顶层读写 ----
    lua["x"] = 42;
    {
      int x = lua["x"].get<int>();
      if (x != 42) { std::cerr << "FAIL: x = " << x << " want 42\n"; return 1; }
      std::cout << "PASS: top-level rw: x=" << x << "\n";
    }

    // ---- 2. 隐式转换 ----
    {
      int x = lua["x"];
      if (x != 42) { std::cerr << "FAIL: implicit x = " << x << "\n"; return 1; }
      std::cout << "PASS: implicit conversion: x=" << x << "\n";
    }

    // ---- 3. 嵌套 table 链式写 ----
    lua.do_string("global map<str, any> cfg = { \"server\": { \"host\": \"localhost\", \"port\": 8080 } };");
    {
      int port = lua["cfg"]["server"]["port"].get<int>();
      std::string host = lua["cfg"]["server"]["host"].get<std::string>();
      if (port != 8080) { std::cerr << "FAIL: port = " << port << "\n"; return 1; }
      if (host != "localhost") { std::cerr << "FAIL: host = " << host << "\n"; return 1; }
      std::cout << "PASS: nested read: host=" << host << " port=" << port << "\n";
    }

    // ---- 4. 链式写嵌套字段 ----
    lua["cfg"]["server"]["port"] = 9090;
    {
      int port = lua["cfg"]["server"]["port"].get<int>();
      if (port != 9090) { std::cerr << "FAIL: port = " << port << " want 9090\n"; return 1; }
      // 验证 Lua 侧也看到新值
      lua.do_string("p = cfg.server.port;");
      int p2 = lua.get_global<int>("p");
      if (p2 != 9090) { std::cerr << "FAIL: lua-side p = " << p2 << "\n"; return 1; }
      std::cout << "PASS: nested write: port=" << port << " (lua sees " << p2 << ")\n";
    }

    // ---- 5. get_or 默认值 ----
    {
      int missing = lua["cfg"]["server"]["missing_key"].get_or(12345);
      if (missing != 12345) { std::cerr << "FAIL: missing = " << missing << "\n"; return 1; }
      std::cout << "PASS: get_or default: missing=" << missing << "\n";
    }

    // ---- 6. valid() 存在性 ----
    {
      bool exists = lua["cfg"]["server"]["port"].valid();
      bool absent = lua["cfg"]["server"]["nonexistent"].valid();
      if (!exists) { std::cerr << "FAIL: port should exist\n"; return 1; }
      if (absent) { std::cerr << "FAIL: nonexistent should be invalid\n"; return 1; }
      std::cout << "PASS: valid(): port=" << exists << " nonexistent=" << absent << "\n";
    }

    // ---- 7. 字符串字段写读 ----
    lua["cfg"]["server"]["host"] = "example.com";
    {
      std::string host = lua["cfg"]["server"]["host"].get<std::string>();
      if (host != "example.com") { std::cerr << "FAIL: host = " << host << "\n"; return 1; }
      std::cout << "PASS: string write: host=" << host << "\n";
    }

    // ---- 8. 链断保护：中间非 table 时读返回 nil，valid=false ----
    {
      bool broken = lua["nonexistent_global"]["deep"]["deeper"].valid();
      if (broken) { std::cerr << "FAIL: broken chain should be invalid\n"; return 1; }
      std::cout << "PASS: broken chain: valid()=" << broken << "\n";
    }

    std::cout << "=== All table_proxy tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
