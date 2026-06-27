// test_edge_containers.cpp - STL 容器适配边界测试

#include "sptxx.hpp"
#include <iostream>
#include <map>
#include <string>
#include <unordered_map>
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

    // ---- 1. 空容器 roundtrip ----
    lua["e1"] = std::vector<int>{};
    {
      auto v = lua["e1"].get<std::vector<int>>();
      CHECK(v.empty(), "empty vector roundtrip");
    }

    // ---- 2. 单元素容器 ----
    lua["e2"] = std::vector<int>{42};
    {
      auto v = lua["e2"].get<std::vector<int>>();
      CHECK(v.size() == 1 && v[0] == 42, "single-element vector");
    }

    // ---- 3. 大容器（1000 元素）----
    {
      std::vector<int> big;
      big.reserve(1000);
      for (int i = 0; i < 1000; ++i) big.push_back(i * 2);
      lua["e3"] = big;
      auto v = lua["e3"].get<std::vector<int>>();
      bool ok = v.size() == 1000;
      if (ok) {
        for (int i = 0; i < 1000; ++i) {
          if (v[i] != i * 2) { ok = false; break; }
        }
      }
      CHECK(ok, "large vector (1000 elements) roundtrip");
    }

    // ---- 4. 嵌套 vector<vector<int>> ----
    {
      std::vector<std::vector<int>> nested = {{1, 2}, {3, 4, 5}, {6}};
      lua["e4"] = nested;
      auto v = lua["e4"].get<std::vector<std::vector<int>>>();
      bool ok = v.size() == 3 && v[0].size() == 2 && v[1].size() == 3 && v[2].size() == 1;
      if (ok) {
        ok = v[0][0] == 1 && v[0][1] == 2 && v[1][0] == 3 && v[1][2] == 5 && v[2][0] == 6;
      }
      CHECK(ok, "nested vector<vector<int>> roundtrip");
    }

    // ---- 5. vector<string> ----
    {
      std::vector<std::string> vs = {"alpha", "beta", "gamma"};
      lua["e5"] = vs;
      auto v = lua["e5"].get<std::vector<std::string>>();
      CHECK(v.size() == 3 && v[0] == "alpha" && v[1] == "beta" && v[2] == "gamma",
            "vector<string> roundtrip");
    }

    // ---- 6. vector<double> ----
    {
      std::vector<double> vd = {1.5, 2.5, 3.14};
      lua["e6"] = vd;
      auto v = lua["e6"].get<std::vector<double>>();
      CHECK(v.size() == 3 && v[0] == 1.5 && v[1] == 2.5 && v[2] == 3.14,
            "vector<double> roundtrip");
    }

    // ---- 7. map<string, vector<int>> 嵌套 ----
    {
      std::map<std::string, std::vector<int>> mv = {
          {"a", {1, 2}}, {"b", {3, 4, 5}}};
      lua["e7"] = mv;
      auto m = lua["e7"].get<std::map<std::string, std::vector<int>>>();
      bool ok = m.size() == 2;
      if (ok) {
        ok = m["a"].size() == 2 && m["a"][1] == 2 && m["b"].size() == 3 && m["b"][2] == 5;
      }
      CHECK(ok, "map<string, vector<int>> nested roundtrip");
    }

    // ---- 8. unordered_map roundtrip ----
    {
      std::unordered_map<std::string, int> um = {{"x", 1}, {"y", 2}, {"z", 3}};
      lua["e8"] = um;
      auto m = lua["e8"].get<std::unordered_map<std::string, int>>();
      CHECK(m.size() == 3 && m["x"] == 1 && m["y"] == 2 && m["z"] == 3,
            "unordered_map roundtrip");
    }

    // ---- 9. std::array roundtrip ----
    {
      std::array<int, 4> arr = {10, 20, 30, 40};
      lua["e9"] = arr;
      auto a = lua["e9"].get<std::array<int, 4>>();
      CHECK(a[0] == 10 && a[1] == 20 && a[2] == 30 && a[3] == 40,
            "std::array roundtrip");
    }

    // ---- 10. 空 map roundtrip ----
    {
      std::map<std::string, int> em;
      lua["e10"] = em;
      auto m = lua["e10"].get<std::map<std::string, int>>();
      CHECK(m.empty(), "empty map roundtrip");
    }

    // ---- 11. Lua 创建数组 → C++ 接收（0-based 索引验证）----
    lua.set_function("e11_verify", [](std::vector<int> v) {
      return v.size() == 4 && v[0] == 10 && v[3] == 40 ? 1 : 0;
    });
    lua.do_string("e11r = e11_verify([10, 20, 30, 40]);");
    CHECK(lua.get_global<int>("e11r") == 1, "Lua array 0-based index verified");

    // ---- 12. Lua 创建 map → C++ 接收 ----
    lua.set_function("e12_verify", [](std::map<std::string, int> m) {
      return m.size() == 2 && m["k1"] == 100 && m["k2"] == 200 ? 1 : 0;
    });
    lua.do_string("global map<str, int> e12m = {\"k1\": 100, \"k2\": 200}; e12r = e12_verify(e12m);");
    CHECK(lua.get_global<int>("e12r") == 1, "Lua map → C++ map verified");

    // ---- 13. 容器作为函数返回值 ----
    lua.set_function("e13_gen", []() {
      return std::vector<int>{1, 2, 3, 4, 5};
    });
    lua.do_string("e13r = e13_gen(); e13s = #e13r; e13v = e13r[0] + e13r[4];");
    CHECK(lua.get_global<int>("e13s") == 5, "vector return size in Lua = " << lua.get_global<int>("e13s"));
    CHECK(lua.get_global<int>("e13v") == 6, "vector return elements accessible");

    // ---- 14. 容器内自定义类型不支持（验证 int 元素为默认）----
    // 跳过：自定义类型需 usertype 注册

    // ---- 15. vector<bool> 特殊性 ----
    {
      std::vector<bool> vb = {true, false, true};
      lua["e15"] = vb;
      // vector<bool> 的 pusher 走 stack::push<bool>，每个元素压 boolean
      lua.do_string("e15a = e15[0]; e15b = e15[1]; e15c = e15[2];");
      CHECK(lua.get_global<bool>("e15a") == true, "vector<bool>[0] = true");
      CHECK(lua.get_global<bool>("e15b") == false, "vector<bool>[1] = false");
      CHECK(lua.get_global<bool>("e15c") == true, "vector<bool>[2] = true");
    }

    // ---- 16. 越界读取（SPT 数组越界访问直接报错）----
    lua["e16"] = std::vector<int>{1, 2};
    {
      bool threw = false;
      try {
        lua.do_string("e16v = e16[10];");
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "out-of-range SPT array access throws");
    }

    // ---- 17. map with int key（Lua number key）----
    {
      std::map<int, std::string> mk = {{1, "one"}, {2, "two"}};
      lua["e17"] = mk;
      auto m = lua["e17"].get<std::map<int, std::string>>();
      CHECK(m.size() == 2 && m[1] == "one" && m[2] == "two", "map<int,string> roundtrip");
    }

    if (failures == 0) {
      std::cout << "=== All containers edge tests passed! ===\n";
      return 0;
    }
    std::cerr << "=== " << failures << " test(s) FAILED ===\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
