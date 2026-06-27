// test_containers.cpp - 测试 STL 容器适配（std::vector / std::map）

#include "sptxx.hpp"
#include <iostream>
#include <map>
#include <string>
#include <vector>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // ---- 1. C++ push vector → Lua 读元素 ----
    lua["nums"] = std::vector<int>{10, 20, 30};
    lua.do_string("v0 = nums[0]; v1 = nums[1]; v2 = nums[2];");
    {
      int v0 = lua.get_global<int>("v0");
      int v1 = lua.get_global<int>("v1");
      int v2 = lua.get_global<int>("v2");
      if (v0 != 10 || v1 != 20 || v2 != 30) {
        std::cerr << "FAIL: vector read v0=" << v0 << " v1=" << v1 << " v2=" << v2 << "\n";
        return 1;
      }
      std::cout << "PASS: push vector → Lua: " << v0 << "," << v1 << "," << v2 << "\n";
    }

    // ---- 2. Lua 创建数组 → C++ 接收 vector ----
    lua.set_function("sum_vec", [](std::vector<int> v) {
      int s = 0;
      for (int x : v) s += x;
      return s;
    });
    lua.do_string("list<int> arr = [1, 2, 3, 4]; r2 = sum_vec(arr);");
    {
      int r2 = lua.get_global<int>("r2");
      if (r2 != 10) { std::cerr << "FAIL: sum_vec = " << r2 << " want 10\n"; return 1; }
      std::cout << "PASS: Lua array → C++ vector: sum=" << r2 << "\n";
    }

    // ---- 3. C++ push map → Lua 读字段 ----
    lua["config"] = std::map<std::string, int>{{"a", 1}, {"b", 2}, {"c", 3}};
    lua.do_string("ca = config.a; cb = config.b; cc = config.c;");
    {
      int ca = lua.get_global<int>("ca");
      int cb = lua.get_global<int>("cb");
      int cc = lua.get_global<int>("cc");
      if (ca != 1 || cb != 2 || cc != 3) {
        std::cerr << "FAIL: map read ca=" << ca << " cb=" << cb << " cc=" << cc << "\n";
        return 1;
      }
      std::cout << "PASS: push map → Lua: " << ca << "," << cb << "," << cc << "\n";
    }

    // ---- 4. Lua 创建 map → C++ 接收 map ----
    lua.set_function("count_keys", [](std::map<std::string, int> m) {
      return static_cast<int>(m.size());
    });
    lua.do_string("global map<str, int> mm = {\"x\": 1, \"y\": 2, \"z\": 3}; n = count_keys(mm);");
    {
      int n = lua.get_global<int>("n");
      if (n != 3) { std::cerr << "FAIL: count_keys = " << n << " want 3\n"; return 1; }
      std::cout << "PASS: Lua map → C++ map: keys=" << n << "\n";
    }

    // ---- 5. 往返：push vector → 读回 vector ----
    lua["roundtrip"] = std::vector<int>{5, 10, 15, 20};
    {
      std::vector<int> v = lua["roundtrip"].get<std::vector<int>>();
      if (v.size() != 4 || v[0] != 5 || v[1] != 10 || v[2] != 15 || v[3] != 20) {
        std::cerr << "FAIL: roundtrip vector size=" << v.size() << "\n";
        return 1;
      }
      std::cout << "PASS: vector roundtrip: size=" << v.size() << " vals=";
      for (size_t i = 0; i < v.size(); ++i) std::cout << v[i] << (i + 1 < v.size() ? "," : "");
      std::cout << "\n";
    }

    // ---- 6. 往返：push map → 读回 map ----
    lua["m2"] = std::map<std::string, int>{{"alpha", 100}, {"beta", 200}};
    {
      std::map<std::string, int> m = lua["m2"].get<std::map<std::string, int>>();
      if (m.size() != 2 || m["alpha"] != 100 || m["beta"] != 200) {
        std::cerr << "FAIL: roundtrip map size=" << m.size() << "\n";
        return 1;
      }
      std::cout << "PASS: map roundtrip: size=" << m.size()
                << " alpha=" << m["alpha"] << " beta=" << m["beta"] << "\n";
    }

    // ---- 7. 空容器 ----
    lua["empty_v"] = std::vector<int>{};
    {
      std::vector<int> v = lua["empty_v"].get<std::vector<int>>();
      if (!v.empty()) { std::cerr << "FAIL: empty vector size=" << v.size() << "\n"; return 1; }
      std::cout << "PASS: empty vector roundtrip\n";
    }

    std::cout << "=== All container tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
