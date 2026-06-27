// test_inheritance.cpp - 测试 usertype 继承链
// Derived 对象能访问 Base 的成员属性和方法。

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Base {
  int base_val;
  Base(int v) : base_val(v) {}
  int base_method() { return base_val * 2; }
  int base_computed() const { return base_val + 100; }
};

struct Derived : Base {
  int derived_val;
  Derived(int b, int d) : Base(b), derived_val(d) {}
  int derived_method() { return derived_val + base_val; }
};

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // 先注册 Base
    auto wt_base = lua.new_usertype<Base>("Base");
    wt_base.constructor<int>();
    wt_base.set("base_val", &Base::base_val);
    wt_base.set("base_method", &Base::base_method);
    wt_base.set_readonly("base_computed", &Base::base_computed);

    // 再注册 Derived，声明继承 Base
    auto wt_derived = lua.new_usertype<Derived>("Derived");
    wt_derived.constructor<int, int>();
    wt_derived.base<Base>("Base");
    wt_derived.set("derived_val", &Derived::derived_val);
    wt_derived.set("derived_method", &Derived::derived_method);

    // ---- 1. Derived 访问 Base 成员属性 ----
    lua.do_string(R"(
        d = Derived(10, 20);
        bv = d.base_val;
        dv = d.derived_val;
    )");
    {
      int bv = lua.get_global<int>("bv");
      int dv = lua.get_global<int>("dv");
      if (bv != 10) { std::cerr << "FAIL: base_val = " << bv << " want 10\n"; return 1; }
      if (dv != 20) { std::cerr << "FAIL: derived_val = " << dv << " want 20\n"; return 1; }
      std::cout << "PASS: inherited field: base_val=" << bv << " derived_val=" << dv << "\n";
    }

    // ---- 2. Derived 调用 Base 方法 ----
    lua.do_string("bm = d.base_method();");
    {
      int bm = lua.get_global<int>("bm");
      if (bm != 20) { std::cerr << "FAIL: base_method = " << bm << " want 20\n"; return 1; }
      std::cout << "PASS: inherited method: base_method=" << bm << "\n";
    }

    // ---- 3. Derived 调用自己方法 ----
    lua.do_string("dm = d.derived_method();");
    {
      int dm = lua.get_global<int>("dm");
      if (dm != 30) { std::cerr << "FAIL: derived_method = " << dm << " want 30\n"; return 1; }
      std::cout << "PASS: derived method: derived_method=" << dm << "\n";
    }

    // ---- 4. Derived 访问 Base 只读属性 ----
    lua.do_string("bc = d.base_computed;");
    {
      int bc = lua.get_global<int>("bc");
      if (bc != 110) { std::cerr << "FAIL: base_computed = " << bc << " want 110\n"; return 1; }
      std::cout << "PASS: inherited readonly property: base_computed=" << bc << "\n";
    }

    // ---- 5. Derived 写 Base 成员属性 ----
    lua.do_string("d.base_val = 99; bv2 = d.base_val;");
    {
      int bv2 = lua.get_global<int>("bv2");
      if (bv2 != 99) { std::cerr << "FAIL: write base_val = " << bv2 << " want 99\n"; return 1; }
      std::cout << "PASS: write inherited field: base_val=" << bv2 << "\n";
    }

    std::cout << "=== All inheritance tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
