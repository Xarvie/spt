// test_multireturn.cpp - Test multiple return value support

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <tuple>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Multiple Return Values ===" << std::endl;

    std::cout << "\n1. Testing single value return..." << std::endl;
    lua.set_function("single_val", []() -> int { return 42; });
    lua.do_string("single = single_val();");
    int single = lua.get_global<int>("single");
    if (single != 42) {
      throw std::runtime_error("Single return failed!");
    }
    std::cout << "Single return: " << single << " (correct)" << std::endl;

    std::cout << "\n2. Testing function with two parameters..." << std::endl;
    lua.set_function("add", [](int a, int b) -> int { return a + b; });
    lua.do_string("sum = add(10, 20);");
    int sum = lua.get_global<int>("sum");
    if (sum != 30) {
      throw std::runtime_error("Add function failed!");
    }
    std::cout << "add(10, 20) = " << sum << " (correct)" << std::endl;

    std::cout << "\n3. Testing function with string..." << std::endl;
    lua.set_function("greet",
                     [](std::string name) -> std::string { return "Hello, " + name + "!"; });
    lua.do_string("greeting = greet('World');");
    std::string greeting = lua.get_global<std::string>("greeting");
    if (greeting != "Hello, World!") {
      throw std::runtime_error("Greet function failed!");
    }
    std::cout << "greet('World') = " << greeting << " (correct)" << std::endl;

    std::cout << "\n4. Testing void function..." << std::endl;
    bool void_called = false;
    lua.set_function("void_func", [&void_called]() { void_called = true; });
    lua.do_string("void_func();");
    if (!void_called) {
      throw std::runtime_error("Void function was not called!");
    }
    std::cout << "Void function called successfully" << std::endl;

    std::cout << "\n=== All Multiple Return Value Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
