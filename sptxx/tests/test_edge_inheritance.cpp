// test_edge_inheritance.cpp - 继承链边界测试

#include "sptxx.hpp"
#include <iostream>
#include <string>

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

// 三层继承：A → B → C
struct A {
  int a_val = 10;
  int get_a() { return a_val; }
  void set_a(int v) { a_val = v; }
  int describe() { return 100; } // 基类方法
};
struct B : A {
  int b_val = 20;
  int get_b() { return b_val; }
  void set_b(int v) { b_val = v; }
  int describe() { return 200; } // 覆盖基类
};
struct C : B {
  int c_val = 30;
  int get_c() { return c_val; }
};

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    // 注册 A
    {
      auto ua = lua.new_usertype<A>("A");
      ua.constructor<>();
      ua.set("a_val", &A::a_val);
      ua.set("get_a", &A::get_a);
      ua.set("set_a", &A::set_a);
      ua.set("describe", &A::describe);
    }
    // 注册 B，继承 A
    {
      auto ub = lua.new_usertype<B>("B");
      ub.constructor<>();
      ub.base<A>("A");
      ub.set("b_val", &B::b_val);
      ub.set("get_b", &B::get_b);
      ub.set("set_b", &B::set_b);
      ub.set("describe", &B::describe); // 覆盖 A::describe
    }
    // 注册 C，继承 B
    {
      auto uc = lua.new_usertype<C>("C");
      uc.constructor<>();
      uc.base<B>("B");
      uc.set("c_val", &C::c_val);
      uc.set("get_c", &C::get_c);
    }

    // ---- 1. 三层继承：C 对象访问 A 的属性 ----
    lua.do_string("c1 = C(); r1 = c1.get_a();");
    CHECK(lua.get_global<int>("r1") == 10, "C reads A's get_a() = " << lua.get_global<int>("r1"));

    // ---- 2. 三层继承：C 对象访问 B 的属性 ----
    lua.do_string("r2 = c1.get_b();");
    CHECK(lua.get_global<int>("r2") == 20, "C reads B's get_b() = " << lua.get_global<int>("r2"));

    // ---- 3. 三层继承：C 对象访问 C 自己的属性 ----
    lua.do_string("r3 = c1.get_c();");
    CHECK(lua.get_global<int>("r3") == 30, "C reads own get_c() = " << lua.get_global<int>("r3"));

    // ---- 4. 派生类覆盖基类方法：B::describe 优先于 A::describe ----
    lua.do_string("b1 = B(); r4 = b1.describe();");
    CHECK(lua.get_global<int>("r4") == 200, "B::describe overrides A::describe = " << lua.get_global<int>("r4"));

    // ---- 5. C 继承 B::describe（B 覆盖了 A 的）----
    lua.do_string("r5 = c1.describe();");
    CHECK(lua.get_global<int>("r5") == 200, "C inherits B::describe (overridden) = " << lua.get_global<int>("r5"));

    // ---- 6. 基类属性 setter 在派生类上工作 ----
    lua.do_string("c1.set_a(999); r6 = c1.get_a();");
    CHECK(lua.get_global<int>("r6") == 999, "C uses A's set_a/get_a = " << lua.get_global<int>("r6"));

    // ---- 7. B 的 setter 在 C 上工作 ----
    lua.do_string("c1.set_b(888); r7 = c1.get_b();");
    CHECK(lua.get_global<int>("r7") == 888, "C uses B's set_b/get_b = " << lua.get_global<int>("r7"));

    // ---- 8. 直接成员属性访问（通过 getter/setter 链）----
    lua.do_string("a1 = A(); a1.a_val = 55; r8 = a1.a_val;");
    CHECK(lua.get_global<int>("r8") == 55, "A.a_val direct member access = " << lua.get_global<int>("r8"));

    // ---- 9. C 对象访问 A 的直接成员 ----
    lua.do_string("c1.a_val = 77; r9 = c1.a_val;");
    CHECK(lua.get_global<int>("r9") == 77, "C accesses A.a_val via __base chain = " << lua.get_global<int>("r9"));

    // ---- 10. B 对象访问 A 的方法 ----
    lua.do_string("b1.set_a(123); r10 = b1.get_a();");
    CHECK(lua.get_global<int>("r10") == 123, "B accesses A's set_a/get_a = " << lua.get_global<int>("r10"));

    // ---- 11. 派生类独有方法不被基类访问 ----
    {
      bool threw = false;
      try {
        lua.do_string("a1.get_b();"); // A 没有 get_b
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "A cannot access B's get_b (no such method)");
    }

    // ---- 12. 派生类独有属性不被基类访问 ----
    {
      bool threw = false;
      try {
        lua.do_string("a1.b_val = 1;"); // A 没有 b_val
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "A cannot set b_val (no such field)");
    }

    // ---- 13. 构造的 C 对象初始值正确 ----
    lua.do_string("c2 = C(); r13a = c2.get_a(); r13b = c2.get_b(); r13c = c2.get_c();");
    CHECK(lua.get_global<int>("r13a") == 10, "C() default a_val = 10");
    CHECK(lua.get_global<int>("r13b") == 20, "C() default b_val = 20");
    CHECK(lua.get_global<int>("r13c") == 30, "C() default c_val = 30");

    // ---- 14. B 对象不暴露 C 的成员 ----
    {
      bool threw = false;
      try {
        lua.do_string("b1.get_c();");
      } catch (const sptxx::error &) {
        threw = true;
      }
      CHECK(threw, "B cannot access C's get_c");
    }

    if (failures == 0) {
      std::cout << "=== All inheritance edge tests passed! ===\n";
      return 0;
    }
    std::cerr << "=== " << failures << " test(s) FAILED ===\n";
    return 1;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
