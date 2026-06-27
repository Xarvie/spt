// test_static_members.cpp - 测试 usertype 静态成员（静态方法/静态字段）

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Counter {
  int v;
  static int instances;
  static int compute(int a, int b) { return a * 10 + b; }
  static std::string greet(const std::string &name) { return "hi " + name; }
  Counter(int x) : v(x) { ++instances; }
  int get() const { return v; }
};
int Counter::instances = 0;

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    auto wt = lua.new_usertype<Counter>("Counter");
    wt.constructor<int>();
    wt.set("get", &Counter::get);

    // ---- 静态方法（静态成员函数指针）----
    wt.set_static("compute", &Counter::compute);
    wt.set_static("greet", &Counter::greet);

    // ---- 静态字段（值）----
    wt.set_static("instances", Counter::instances); // 初始值 0
    wt.set_static("tag", std::string("COUNTER_v1"));

    // ---- 1. 调用静态方法 ----
    lua.do_string("r = Counter.compute(3, 4);");
    {
      int r = lua.get_global<int>("r");
      if (r != 34) { std::cerr << "FAIL: compute = " << r << " want 34\n"; return 1; }
      std::cout << "PASS: static method compute=" << r << "\n";
    }

    // ---- 2. 调用带 string 参数的静态方法 ----
    lua.do_string("g = Counter.greet(\"world\");");
    {
      std::string g = lua.get_global<std::string>("g");
      if (g != "hi world") { std::cerr << "FAIL: greet = " << g << "\n"; return 1; }
      std::cout << "PASS: static method greet=" << g << "\n";
    }

    // ---- 3. 读静态字段 ----
    lua.do_string("t = Counter.tag;");
    {
      std::string t = lua.get_global<std::string>("t");
      if (t != "COUNTER_v1") { std::cerr << "FAIL: tag = " << t << "\n"; return 1; }
      std::cout << "PASS: static field tag=" << t << "\n";
    }

    // ---- 4. 静态字段初始化值 ----
    lua.do_string("n0 = Counter.instances;");
    {
      int n0 = lua.get_global<int>("n0");
      if (n0 != 0) { std::cerr << "FAIL: instances = " << n0 << " want 0\n"; return 1; }
      std::cout << "PASS: static field initial instances=" << n0 << "\n";
    }

    // ---- 5. 构造后静态字段仍可访问（不随实例变化，需手动同步）----
    lua.do_string("c = Counter(7); cv = c.get();");
    {
      int cv = lua.get_global<int>("cv");
      if (cv != 7) { std::cerr << "FAIL: instance get = " << cv << "\n"; return 1; }
      // instances 仍是注册时的快照 0
      int n = lua["Counter"]["instances"].get<int>();
      if (n != 0) { std::cerr << "FAIL: instances snapshot = " << n << " want 0\n"; return 1; }
      std::cout << "PASS: instance + static snapshot ok (cv=" << cv << " instances=" << n << ")\n";
    }

    // ---- 6. 从 C++ 更新静态字段同步回 Lua ----
    Counter::instances = 99;
    lua["Counter"]["instances"] = Counter::instances;
    {
      int n = lua["Counter"]["instances"].get<int>();
      if (n != 99) { std::cerr << "FAIL: updated instances = " << n << " want 99\n"; return 1; }
      std::cout << "PASS: update static field instances=" << n << "\n";
    }

    // ---- 7. lambda 作为静态方法 ----
    wt.set_static("square", [](int x) { return x * x; });
    lua.do_string("sq = Counter.square(6);");
    {
      int sq = lua.get_global<int>("sq");
      if (sq != 36) { std::cerr << "FAIL: square = " << sq << "\n"; return 1; }
      std::cout << "PASS: lambda static method square=" << sq << "\n";
    }

    std::cout << "=== All static member tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
