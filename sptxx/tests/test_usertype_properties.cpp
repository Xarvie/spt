// test_usertype_properties.cpp - 测试 usertype 属性函数 getter/setter
// 1. getter/setter 函数对（替代成员指针）
// 2. 只读属性（getter only，写会报错）
// 3. 计算属性（getter 返回计算值）
// 4. 非 const getter

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Point {
  int x_, y_;
  Point(int x = 0, int y = 0) : x_(x), y_(y) {}
  int get_x() const { return x_; }
  void set_x(int v) { x_ = v; }
  int get_y() const { return y_; }
  int sum() const { return x_ + y_; }
  int mutable_read() { return x_ * 2; } // 非 const getter
};

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    auto wt = lua.new_usertype<Point>("Point");
    wt.constructor<int, int>();
    // 属性函数对
    wt.set("x", &Point::get_x, &Point::set_x);
    // 只读属性
    wt.set_readonly("y", &Point::get_y);
    // 计算属性（只读）
    wt.set_readonly("sum", &Point::sum);
    // 非 const getter 只读属性
    wt.set_readonly("dbl", &Point::mutable_read);

    // ---- 1. getter/setter 函数对 ----
    lua.do_string(R"(
        p = Point(10, 20);
        x1 = p.x;
        p.x = 99;
        x2 = p.x;
    )");
    {
      int x1 = lua.get_global<int>("x1");
      int x2 = lua.get_global<int>("x2");
      if (x1 != 10) { std::cerr << "FAIL: getter x1 = " << x1 << " want 10\n"; return 1; }
      if (x2 != 99) { std::cerr << "FAIL: setter x2 = " << x2 << " want 99\n"; return 1; }
      std::cout << "PASS: property getter/setter: x1=" << x1 << " x2=" << x2 << "\n";
    }

    // ---- 2. 只读属性：读 OK ----
    lua.do_string("y1 = p.y;");
    {
      int y1 = lua.get_global<int>("y1");
      if (y1 != 20) { std::cerr << "FAIL: readonly y1 = " << y1 << " want 20\n"; return 1; }
      std::cout << "PASS: readonly property read: y1=" << y1 << "\n";
    }

    // ---- 3. 只读属性：写会报错 ----
    bool write_failed = false;
    try {
      lua.do_string("p.y = 999;");
    } catch (const std::exception &) {
      write_failed = true;
    }
    if (!write_failed) {
      std::cerr << "FAIL: readonly property write should error\n";
      return 1;
    }
    std::cout << "PASS: readonly property write correctly rejected\n";

    // ---- 4. 计算属性 ----
    lua.do_string("s = p.sum;");
    {
      int s = lua.get_global<int>("s");
      // p.x=99, p.y=20 → sum=119
      if (s != 119) { std::cerr << "FAIL: computed sum = " << s << " want 119\n"; return 1; }
      std::cout << "PASS: computed property: sum=" << s << "\n";
    }

    // ---- 5. 非 const getter ----
    lua.do_string("d = p.dbl;");
    {
      int d = lua.get_global<int>("d");
      // p.x=99 → dbl=198
      if (d != 198) { std::cerr << "FAIL: non-const getter dbl = " << d << " want 198\n"; return 1; }
      std::cout << "PASS: non-const getter property: dbl=" << d << "\n";
    }

    std::cout << "=== All usertype property tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
