// test_edge_variant.cpp - std::variant 边界测试

#include "sptxx.hpp"
#include <iostream>
#include <map>
#include <string>
#include <variant>
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

    // ---- 1. 单类型 variant<int> ----
    using VI = std::variant<int>;
    lua["v1"] = VI{42};
    {
      auto v = lua["v1"].get<VI>();
      CHECK(std::holds_alternative<int>(v) && std::get<int>(v) == 42,
            "single-type variant<int> roundtrip");
    }

    // ---- 2. variant<int, bool> 严格区分 ----
    using IB = std::variant<int, bool>;
    lua.set_function("ib_kind", [](IB v) -> int {
      if (std::holds_alternative<int>(v)) return 1;
      if (std::holds_alternative<bool>(v)) return 2;
      return 0;
    });
    lua.do_string("r2a = ib_kind(123); r2b = ib_kind(true);");
    CHECK(lua.get_global<int>("r2a") == 1, "variant<int,bool>: int matches int");
    CHECK(lua.get_global<int>("r2b") == 2, "variant<int,bool>: bool matches bool");

    // ---- 3. variant<int, double, string, bool> 四类型 ----
    using Q = std::variant<int, double, std::string, bool>;
    lua.set_function("q_kind", [](Q v) -> int {
      return static_cast<int>(v.index()) + 1;
    });
    lua.do_string("r3a = q_kind(10); r3b = q_kind(2.5); r3c = q_kind(\"s\"); r3d = q_kind(false);");
    CHECK(lua.get_global<int>("r3a") == 1, "variant 4-type: int = 1");
    CHECK(lua.get_global<int>("r3b") == 2, "variant 4-type: double = 2");
    CHECK(lua.get_global<int>("r3c") == 3, "variant 4-type: string = 3");
    CHECK(lua.get_global<int>("r3d") == 4, "variant 4-type: bool = 4");

    // ---- 4. variant<double, int> 顺序影响：double 在前总匹配 number ----
    using DI = std::variant<double, int>;
    lua.set_function("di_kind", [](DI v) -> int {
      if (std::holds_alternative<double>(v)) return 1;
      if (std::holds_alternative<int>(v)) return 2;
      return 0;
    });
    lua.do_string("r4a = di_kind(3); r4b = di_kind(3.14);");
    CHECK(lua.get_global<int>("r4a") == 1, "variant<double,int>: int arg picks double (double first)");
    CHECK(lua.get_global<int>("r4b") == 1, "variant<double,int>: float arg picks double");

    // ---- 5. variant<int, double> 顺序：int 先匹配整数 ----
    using ID = std::variant<int, double>;
    lua.set_function("id_kind", [](ID v) -> int {
      if (std::holds_alternative<int>(v)) return 1;
      if (std::holds_alternative<double>(v)) return 2;
      return 0;
    });
    lua.do_string("r5a = id_kind(7); r5b = id_kind(7.5);");
    CHECK(lua.get_global<int>("r5a") == 1, "variant<int,double>: int arg picks int");
    CHECK(lua.get_global<int>("r5b") == 2, "variant<int,double>: float arg picks double");

    // ---- 6. variant 持有 vector<int> ----
    using VV = std::variant<std::vector<int>, int>;
    lua.set_function("vv_kind", [](VV v) -> int {
      if (std::holds_alternative<std::vector<int>>(v)) return 1;
      if (std::holds_alternative<int>(v)) return 2;
      return 0;
    });
    lua.do_string("r6a = vv_kind([1, 2, 3]); r6b = vv_kind(99);");
    CHECK(lua.get_global<int>("r6a") == 1, "variant<vector,int>: array picks vector");
    CHECK(lua.get_global<int>("r6b") == 2, "variant<vector,int>: int picks int");

    // ---- 7. variant 作为返回值 ----
    using RS = std::variant<int, std::string>;
    lua.set_function("make_variant", [](int which) -> RS {
      if (which == 1) return RS{42};
      return RS{std::string{"text"}};
    });
    lua.do_string("r7a = make_variant(1); r7b = make_variant(2);");
    {
      RS a = lua["r7a"].get<RS>();
      RS b = lua["r7b"].get<RS>();
      CHECK(std::holds_alternative<int>(a) && std::get<int>(a) == 42, "return variant int");
      CHECK(std::holds_alternative<std::string>(b) && std::get<std::string>(b) == "text", "return variant string");
    }

    // ---- 8. 无匹配 alternative 报错（variant<int> 传 string）----
    {
      bool threw = false;
      try {
        lua.do_string("r8 = q_kind({});"); // 空 table 不匹配 int/double/string/bool
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "no matching alternative throws via do_string");
    }

    // ---- 9. variant<string, int>：string 严格匹配（数字不匹配 string）----
    using SI = std::variant<std::string, int>;
    lua.set_function("si_kind", [](SI v) -> int {
      if (std::holds_alternative<std::string>(v)) return 1;
      if (std::holds_alternative<int>(v)) return 2;
      return 0;
    });
    lua.do_string("r9a = si_kind(\"hello\"); r9b = si_kind(42);");
    CHECK(lua.get_global<int>("r9a") == 1, "variant<string,int>: string picks string");
    CHECK(lua.get_global<int>("r9b") == 2, "variant<string,int>: int picks int (string strict)");

    // ---- 10. variant<optional<int>, string>：nil → optional ----
    using OS = std::variant<std::optional<int>, std::string>;
    lua.set_function("os_kind", [](OS v) -> int {
      if (std::holds_alternative<std::optional<int>>(v)) {
        return std::get<0>(v).has_value() ? 1 : 0;
      }
      return 2;
    });
    lua.do_string("r10a = os_kind(nil); r10b = os_kind(5); r10c = os_kind(\"x\");");
    CHECK(lua.get_global<int>("r10a") == 0, "variant<optional,string>: nil → optional empty");
    CHECK(lua.get_global<int>("r10b") == 1, "variant<optional,string>: int → optional<int>");
    CHECK(lua.get_global<int>("r10c") == 2, "variant<optional,string>: string → string");

    // ---- 11. valueless_by_exception push nil ----
    {
      std::variant<int, std::string> vlv;
      bool got_valueless = false;
      try {
        struct Throwing {
          operator int() const { throw std::runtime_error("boom"); }
        };
        vlv = decltype(vlv){Throwing{}};
      } catch (...) {
        got_valueless = vlv.valueless_by_exception();
      }
      if (got_valueless) {
        lua["v11"] = vlv;
        lua.do_string("r11 = v11 == nil;");
        CHECK(lua.get_global<bool>("r11") == true, "valueless variant pushes nil");
      } else {
        std::cout << "PASS (skip): variant did not become valueless (impl-defined)\n";
      }
    }

    // ---- 12. variant roundtrip 保持 alternative ----
    using RT = std::variant<int, double, std::string>;
    lua["rt1"] = RT{std::string{"abc"}};
    lua["rt2"] = RT{3.14};
    lua["rt3"] = RT{7};
    {
      RT r1 = lua["rt1"].get<RT>();
      RT r2 = lua["rt2"].get<RT>();
      RT r3 = lua["rt3"].get<RT>();
      bool ok = r1.index() == 2 && std::get<std::string>(r1) == "abc";
      ok = ok && r2.index() == 1 && std::get<double>(r2) == 3.14;
      ok = ok && r3.index() == 0 && std::get<int>(r3) == 7;
      CHECK(ok, "variant roundtrip preserves alternative index");
    }

    if (failures == 0) {
      std::cout << "=== All variant edge tests passed! ===\n";
      return 0;
    }
    std::cerr << "=== " << failures << " test(s) FAILED ===\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
