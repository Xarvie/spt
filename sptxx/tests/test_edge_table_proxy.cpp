// test_edge_table_proxy.cpp - table_proxy 链式访问边界测试

#include "sptxx.hpp"
#include <iostream>
#include <map>
#include <string>
#include <vector>

static int failures = 0;
#define CHECK(cond, msg)                                                       \
  do {                                                                         \
    if (!(cond)) {                                                             \
      std::cerr << "FAIL: " << msg << " (line " << __LINE__ << ")\n";         \
      ++failures;                                                              \
    } else {                                                                   \
      std::cout << "PASS: " << msg << "\n";                                    \
    }                                                                          \
  } while (0)

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // ---- 1. 深层嵌套链 a.b.c.d 读写 ----
    lua.do_string("a = {\"b\": {\"c\": {\"d\": 42}}};");
    {
      int d = lua["a"]["b"]["c"]["d"].get<int>();
      CHECK(d == 42, "deep chain read a.b.c.d = " << d);
    }

    // ---- 2. 深层链写入 ----
    lua["a"]["b"]["c"]["d"] = 99;
    {
      int d = lua["a"]["b"]["c"]["d"].get<int>();
      CHECK(d == 99, "deep chain write then read = " << d);
    }

    // ---- 3. 链尾写入新 key ----
    lua["a"]["b"]["c"]["new_key"] = 123;
    {
      int v = lua["a"]["b"]["c"]["new_key"].get<int>();
      CHECK(v == 123, "write new key at chain tail = " << v);
    }

    // ---- 4. 断链读取 get_or 返回默认（中间节点不存在）----
    {
      int v = lua["missing"]["x"]["y"].get_or(777);
      CHECK(v == 777, "broken chain get_or returns default = " << v);
    }

    // ---- 5. 断链读取 get_or（中间节点为非 table）----
    lua.do_string("num = 100;");
    {
      int v = lua["num"]["sub"]["deep"].get_or(-1);
      CHECK(v == -1, "non-table middle get_or = " << v);
    }

    // ---- 6. 断链 valid() 返回 false ----
    {
      bool ok = lua["missing"]["x"].valid();
      CHECK(!ok, "broken chain valid() = false");
    }

    // ---- 7. 存在的 key valid() 返回 true ----
    {
      bool ok = lua["a"]["b"]["c"]["d"].valid();
      CHECK(ok, "existing chain valid() = true");
    }

    // ---- 8. 断链写入抛异常 ----
    {
      bool threw = false;
      try {
        lua["missing"]["x"]["y"] = 1;
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "broken chain write throws sptxx::error");
    }

    // ---- 9. 非表中间节点写入抛异常 ----
    {
      bool threw = false;
      try {
        lua["num"]["sub"] = 5;
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "non-table middle write throws");
    }

    // ---- 10. get_or 对存在 key 返回实际值 ----
    {
      int v = lua["a"]["b"]["c"]["d"].get_or(0);
      CHECK(v == 99, "get_or on existing key returns actual = " << v);
    }

    // ---- 11. 隐式转换 operator T() ----
    {
      int v = lua["a"]["b"]["c"]["d"];
      CHECK(v == 99, "implicit conversion operator T() = " << v);
    }

    // ---- 12. 覆盖已有值 ----
    lua["a"]["b"]["c"]["d"] = 1000;
    {
      int v = lua["a"]["b"]["c"]["d"].get<int>();
      CHECK(v == 1000, "overwrite existing value = " << v);
    }

    // ---- 13. 写入 string 类型 ----
    lua["a"]["b"]["c"]["d"] = std::string{"text"};
    {
      std::string v = lua["a"]["b"]["c"]["d"].get<std::string>();
      CHECK(v == "text", "write/read string via chain = \"" << v << "\"");
    }

    // ---- 14. 写入 vector ----
    lua["a"]["b"]["vec"] = std::vector<int>{1, 2, 3};
    {
      std::vector<int> v = lua["a"]["b"]["vec"].get<std::vector<int>>();
      CHECK(v.size() == 3 && v[0] == 1 && v[2] == 3, "write/read vector via chain");
    }

    // ---- 15. 根级写入与读取 ----
    lua["root_var"] = 42;
    {
      int v = lua["root_var"].get<int>();
      CHECK(v == 42, "root-level write/read = " << v);
    }

    // ---- 16. 根级 valid ----
    {
      CHECK(lua["root_var"].valid(), "root valid() true");
      CHECK(!lua["nonexistent_root"].valid(), "root valid() false for missing");
    }

    // ---- 17. 空字符串 key ----
    lua[""] = 7;
    {
      int v = lua[""].get<int>();
      CHECK(v == 7, "empty string key roundtrip = " << v);
    }

    // ---- 18. table_proxy 拷贝后仍可用 ----
    {
      auto p1 = lua["a"]["b"];
      auto p2 = p1; // 拷贝
      int v = p2["c"]["d"].get<std::string>().size(); // d 现在是 "text"
      CHECK(v == 4, "copied proxy still works, len=\"text\" = " << v);
    }

    // ---- 19. table_proxy 移动后仍可用 ----
    {
      auto p1 = lua["a"]["b"];
      auto p2 = std::move(p1);
      std::string v = p2["c"]["d"].get<std::string>();
      CHECK(v == "text", "moved proxy still works");
    }

    // ---- 20. 链中间创建新 table 后可继续链 ----
    lua["fresh"] = std::map<std::string, int>{{"k", 1}};
    lua["fresh"]["k2"] = 2;
    {
      int v1 = lua["fresh"]["k"].get<int>();
      int v2 = lua["fresh"]["k2"].get<int>();
      CHECK(v1 == 1 && v2 == 2, "extend existing table with new keys");
    }

    if (failures == 0) {
      std::cout << "=== All table_proxy edge tests passed! ===\n";
      return 0;
    }
    std::cerr << "=== " << failures << " test(s) FAILED ===\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
