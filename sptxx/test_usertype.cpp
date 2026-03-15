// test_usertype.cpp - Test usertype registration issue - Final Corrected Version

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Person {
  std::string name;
  int age;

  Person() : name("Unknown"), age(0) {}

  Person(const std::string &n, int a) : name(n), age(a) {}

  void introduce() const {
    std::cout << "[C++ Call] Hi, I'm " << name << " and I'm " << age << " years old." << std::endl;
  }
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

    std::cout << "=== Testing Usertype Registration Issue ===" << std::endl;

    std::cout << "\n1. Testing basic usertype creation & binding..." << std::endl;
    auto person_type = lua.new_usertype<Person>("Person");

    // 绑定机制
    person_type.constructor(); // 底层已经将构造绑定到了 __call
    person_type.set("name", &Person::name);
    person_type.set("age", &Person::age);
    person_type.set("introduce", &Person::introduce);

    std::cout << "Usertype created and bound successfully!" << std::endl;

    std::cout << "\n2. Testing Lua instance creation..." << std::endl;
    // 使用真正的原生语法 Person()，去触发 __call！
    safe_do_string(lua, "p = Person();");
    std::cout << "Lua instance creation succeeded!" << std::endl;

    std::cout << "\n3. Testing property assignment and method calls..." << std::endl;
    safe_do_string(lua, "p.name = 'Alice';\n"
                        "p.age = 25;\n"
                        "p.introduce();\n");
    std::cout << "Method call worked!" << std::endl;

    std::cout << "\n=== Usertype Test Complete ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}