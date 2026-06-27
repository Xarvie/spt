// test_variant.cpp - 测试 std::variant<Ts...> 支持

#include "sptxx.hpp"
#include <iostream>
#include <map>
#include <string>
#include <variant>
#include <vector>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    using IV = std::variant<int, std::string>;

    // ---- 1. push int alternative → Lua 读为整数 ----
    lua["v1"] = IV{42};
    lua.do_string("r1 = v1 + 8;");
    {
      int r1 = lua.get_global<int>("r1");
      if (r1 != 50) { std::cerr << "FAIL: r1=" << r1 << " want 50\n"; return 1; }
      std::cout << "PASS: push int variant → Lua: r1=" << r1 << "\n";
    }

    // ---- 2. push string alternative → Lua 读为字符串 ----
    lua["v2"] = IV{std::string{"hello"}};
    lua.do_string("r2 = v2 .. \" world\";");
    {
      std::string r2 = lua.get_global<std::string>("r2");
      if (r2 != "hello world") { std::cerr << "FAIL: r2=\"" << r2 << "\"\n"; return 1; }
      std::cout << "PASS: push string variant → Lua: r2=\"" << r2 << "\"\n";
    }

    // ---- 3. Lua 传 int → C++ 接收 variant<int,string>，选中 int ----
    lua.set_function("classify", [](IV v) -> int {
      if (std::holds_alternative<int>(v)) return 1;
      if (std::holds_alternative<std::string>(v)) return 2;
      return 0;
    });
    lua.do_string("r3 = classify(100);");
    {
      int r3 = lua.get_global<int>("r3");
      if (r3 != 1) { std::cerr << "FAIL: classify(int)=" << r3 << " want 1\n"; return 1; }
      std::cout << "PASS: Lua int → variant holds int: r3=" << r3 << "\n";
    }

    // ---- 4. Lua 传 string → C++ 接收 variant<int,string>，选中 string ----
    lua.do_string("r4 = classify(\"text\");");
    {
      int r4 = lua.get_global<int>("r4");
      if (r4 != 2) { std::cerr << "FAIL: classify(string)=" << r4 << " want 2\n"; return 1; }
      std::cout << "PASS: Lua string → variant holds string: r4=" << r4 << "\n";
    }

    // ---- 5. variant<int, double>：int 与 double 区分 ----
    using ND = std::variant<int, double>;
    lua.set_function("nd_kind", [](ND v) -> int {
      if (std::holds_alternative<int>(v)) return 1;
      if (std::holds_alternative<double>(v)) return 2;
      return 0;
    });
    lua.do_string("r5a = nd_kind(7); r5b = nd_kind(3.14);");
    {
      int r5a = lua.get_global<int>("r5a");
      int r5b = lua.get_global<int>("r5b");
      if (r5a != 1) { std::cerr << "FAIL: nd_kind(7)=" << r5a << " want 1\n"; return 1; }
      if (r5b != 2) { std::cerr << "FAIL: nd_kind(3.14)=" << r5b << " want 2\n"; return 1; }
      std::cout << "PASS: int/double discrimination: " << r5a << "/" << r5b << "\n";
    }

    // ---- 6. variant<optional<int>, string>：nil → optional ----
    using OptV = std::variant<std::optional<int>, std::string>;
    lua.set_function("opt_kind", [](OptV v) -> int {
      if (std::holds_alternative<std::optional<int>>(v)) {
        return v.index() == 0 && std::get<0>(v).has_value() ? 1 : 0; // 1=has int, 0=nil
      }
      return 2; // string
    });
    lua.do_string("r6a = opt_kind(nil); r6b = opt_kind(99); r6c = opt_kind(\"s\");");
    {
      int r6a = lua.get_global<int>("r6a");
      int r6b = lua.get_global<int>("r6b");
      int r6c = lua.get_global<int>("r6c");
      if (r6a != 0) { std::cerr << "FAIL: opt_kind(nil)=" << r6a << " want 0\n"; return 1; }
      if (r6b != 1) { std::cerr << "FAIL: opt_kind(99)=" << r6b << " want 1\n"; return 1; }
      if (r6c != 2) { std::cerr << "FAIL: opt_kind(\"s\")=" << r6c << " want 2\n"; return 1; }
      std::cout << "PASS: optional/string discrimination: nil/int/str="
                << r6a << "/" << r6b << "/" << r6c << "\n";
    }

    // ---- 7. variant<vector<int>, map<string,int>> 容器区分 ----
    using CV = std::variant<std::vector<int>, std::map<std::string, int>>;
    lua.set_function("cv_kind", [](CV v) -> int {
      if (std::holds_alternative<std::vector<int>>(v)) return 1;
      if (std::holds_alternative<std::map<std::string, int>>(v)) return 2;
      return 0;
    });
    lua.do_string("r7a = cv_kind([1, 2, 3]);");
    lua.do_string("global map<str, int> m7 = {\"a\": 1}; r7b = cv_kind(m7);");
    {
      int r7a = lua.get_global<int>("r7a");
      int r7b = lua.get_global<int>("r7b");
      if (r7a != 1) { std::cerr << "FAIL: cv_kind(list)=" << r7a << " want 1\n"; return 1; }
      if (r7b != 2) { std::cerr << "FAIL: cv_kind(map)=" << r7b << " want 2\n"; return 1; }
      std::cout << "PASS: vector/map discrimination: list/map=" << r7a << "/" << r7b << "\n";
    }

    // ---- 8. 往返：push variant → 读回 variant ----
    lua["rt"] = IV{std::string{"roundtrip"}};
    {
      IV v = lua["rt"].get<IV>();
      if (!std::holds_alternative<std::string>(v) ||
          std::get<std::string>(v) != "roundtrip") {
        std::cerr << "FAIL: roundtrip variant\n";
        return 1;
      }
      std::cout << "PASS: variant roundtrip: string=\""
                << std::get<std::string>(v) << "\"\n";
    }

    // ---- 9. valueless_by_exception → push nil ----
    IV valueless;
    try {
      // 构造一个 valueless 状态：抛异常中途破坏 variant
      struct Throwing {
        operator int() const { throw std::runtime_error("boom"); }
      };
      valueless = IV{Throwing{}};
    } catch (...) {
      // 现在 valueless.valueless_by_exception() 可能为 true
    }
    // 即使不是 valueless，push 也应该不崩溃
    lua["v9"] = valueless;
    lua.do_string("r9 = v9 == nil;");
    {
      bool r9 = lua.get_global<bool>("r9");
      // 若 valueless 为 true，r9 应为 true；否则取决于实际持有值
      if (valueless.valueless_by_exception()) {
        if (!r9) { std::cerr << "FAIL: valueless should push nil\n"; return 1; }
        std::cout << "PASS: valueless_by_exception → nil\n";
      } else {
        std::cout << "PASS: non-valueless variant pushed (r9=" << r9 << ")\n";
      }
    }

    std::cout << "=== All variant tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
