// test_usertype_extended.cpp - 测试 usertype 扩展特性
// 1. Lua 脚本侧写成员属性（setter / __newindex 路径）
// 2. __tostring 元方法
// 3. usertype 对象存入 SPT 数组

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Widget {
  std::string name;
  int value;

  Widget() : name("default"), value(0) {}
  Widget(const std::string &n, int v) : name(n), value(v) {}
};

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    auto wt = lua.new_usertype<Widget>("Widget");
    wt.constructor<std::string, int>();
    wt.set("name", &Widget::name);
    wt.set("value", &Widget::value);
    wt.set_tostring([](const Widget &w) -> std::string {
      return "Widget(" + w.name + ", " + std::to_string(w.value) + ")";
    });

    // ---- 1. Lua 侧 setter：obj.field = value ----
    // 注意：SPT 中 `auto` 和 `vars` 声明局部变量；要让 C++ 通过 get_global 读到，
    // 必须用裸赋值（写入全局）。
    lua.do_string(R"(
        w = Widget("init", 10);
        w.value = 99;
        w.name = "modified";
        v = w.value;
        n = w.name;
    )");
    {
      int v = lua.get_global<int>("v");
      std::string n = lua.get_global<std::string>("n");
      if (v != 99) { std::cerr << "FAIL: setter value = " << v << " want 99\n"; return 1; }
      if (n != "modified") { std::cerr << "FAIL: setter name = " << n << "\n"; return 1; }
      std::cout << "PASS: property setter from Lua: value=" << v << " name=" << n << "\n";
    }

    // ---- 2. __tostring ----
    lua.do_string(R"(
        s = tostring(w);
    )");
    {
      std::string s = lua.get_global<std::string>("s");
      if (s != "Widget(modified, 99)") {
        std::cerr << "FAIL: __tostring = '" << s << "' want 'Widget(modified, 99)'\n";
        return 1;
      }
      std::cout << "PASS: __tostring: " << s << "\n";
    }

    // ---- 3. usertype 对象存入 SPT 数组 ----
    lua.do_string(R"(
        a = Widget("alpha", 1);
        b = Widget("beta", 2);
        arr = [a, b];
        item0 = arr[0];
        item1 = arr[1];
        name0 = item0.name;
        name1 = item1.name;
    )");
    {
      std::string name0 = lua.get_global<std::string>("name0");
      std::string name1 = lua.get_global<std::string>("name1");
      if (name0 != "alpha") { std::cerr << "FAIL: arr[0].name = " << name0 << " want alpha\n"; return 1; }
      if (name1 != "beta") { std::cerr << "FAIL: arr[1].name = " << name1 << " want beta\n"; return 1; }
      std::cout << "PASS: usertype in SPT array: " << name0 << ", " << name1 << "\n";
    }

    std::cout << "=== All usertype extended tests passed! ===\n";
    return 0;
  } catch (const std::exception &e) {
    std::cerr << "test crashed: " << e.what() << "\n";
    return 1;
  }
}
