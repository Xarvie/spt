// test_usertype_expanded.cpp - Test expanded usertype features

#include "sptxx.hpp"
#include <iostream>
#include <string>

// 扩展的测试类
struct Warrior {
  std::string name;
  int hp;
  int attack;

  // 默认构造
  Warrior() : name("Unknown"), hp(100), attack(10) {}

  // 带参数的构造
  Warrior(const std::string &n, int h, int a) : name(n), hp(h), attack(a) {}

  void introduce() const {
    std::cout << "[Warrior] I am " << name << ", HP: " << hp << ", ATK: " << attack << std::endl;
  }

  // 多参数方法测试
  void take_damage(int amount, const std::string &source) {
    hp -= amount;
    std::cout << "[Warrior] " << name << " took " << amount << " damage from " << source
              << "! HP left: " << hp << std::endl;
  }

  // 带返回值的方法测试
  bool is_alive() const { return hp > 0; }
};

void safe_do_string(sptxx::state &lua, const char *code) {
  if (luaL_dostring(lua.lua_state(), code) != LUA_OK) {
    std::string err = lua_tostring(lua.lua_state(), -1);
    lua_pop(lua.lua_state(), 1);
    throw std::runtime_error("Lua Error: " + err);
  }
}

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Expanded Usertype Features ===" << std::endl;

    std::cout << "\n1. Binding Warrior class..." << std::endl;
    // 显式指定构造参数的类型
    auto warrior_type = lua.new_usertype<Warrior>("Warrior");

    // 绑定有参构造 (注册对应类型的参数以便 C++ 解析)
    warrior_type.constructor<std::string, int, int>();

    // 绑定属性
    warrior_type.set("name", &Warrior::name);
    warrior_type.set("hp", &Warrior::hp);

    // 绑定多参数及返回值方法
    warrior_type.set("introduce", &Warrior::introduce);
    warrior_type.set("take_damage", &Warrior::take_damage);
    warrior_type.set("is_alive", &Warrior::is_alive);

    std::cout << "Class bound successfully!" << std::endl;

    std::cout << "\n2. Testing Custom Grammar Scripts..." << std::endl;

    // 严格按照 LangParser.g4 和 LangLexer.g4 编写的测试脚本
    safe_do_string(lua, R"(
        auto w = new Warrior("Arthur", 500, 50);

        w.introduce();
        print("Initial HP from script: " .. w.hp);

        w.take_damage(150, "Dragon Fire");

        auto alive = w.is_alive();
        if (alive) {
            print("Arthur is still standing!");
        } else {
            error("Arthur shouldn't be dead yet!");
        }

        w.take_damage(400, "Dark Magic");
        if (!w.is_alive()) {
            print("Arthur has fallen...");
        }
    )");

    std::cout << "\n=== All Expanded Usertype Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}