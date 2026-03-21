// test_lua_function.cpp - Test Lua function calling and function_ref

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <tuple>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Lua Function Calling ===" << std::endl;

    std::cout << "\n1. Testing C++ function binding..." << std::endl;
    lua.set_function("cpp_add", [](int a, int b) -> int { return a + b; });
    lua.do_string("result = cpp_add(10, 20);");
    int result = lua.get_global<int>("result");
    std::cout << "cpp_add(10, 20) = " << result << std::endl;
    if (result != 30) {
      throw std::runtime_error("C++ function binding failed!");
    }
    std::cout << "C++ function binding works!" << std::endl;

    std::cout << "\n2. Testing function_ref extraction..." << std::endl;
    lua.set_function("multiply", [](int x, int y) -> int { return x * y; });

    auto func = lua.get_function<int(int, int)>("multiply");
    int product = func(6, 7);
    std::cout << "function_ref multiply(6, 7) = " << product << std::endl;
    if (product != 42) {
      throw std::runtime_error("function_ref result mismatch!");
    }
    std::cout << "function_ref extraction works!" << std::endl;

    std::cout << "\n3. Testing function_ref copy..." << std::endl;
    auto func_copy = func;
    int product2 = func_copy(5, 8);
    std::cout << "func_copy(5, 8) = " << product2 << std::endl;
    if (product2 != 40) {
      throw std::runtime_error("function_ref copy failed!");
    }
    std::cout << "function_ref copy works!" << std::endl;

    std::cout << "\n4. Testing function_ref validity check..." << std::endl;
    sptxx::function_ref<int(int, int)> empty_func;
    if (empty_func.valid()) {
      throw std::runtime_error("Empty function_ref should not be valid!");
    }
    std::cout << "Empty function_ref correctly reports invalid" << std::endl;

    std::cout << "\n5. Testing passing function_ref to Lua..." << std::endl;
    lua.set_global("callback", func);
    lua.do_string("cb_result = callback(3, 4);");
    int callback_result = lua.get_global<int>("cb_result");
    std::cout << "callback(3, 4) from Lua = " << callback_result << std::endl;
    if (callback_result != 12) {
      throw std::runtime_error("callback from Lua failed!");
    }
    std::cout << "Passing function_ref to Lua works!" << std::endl;

    std::cout << "\n6. Testing function_ref with string..." << std::endl;
    lua.set_function("greet",
                     [](std::string name) -> std::string { return "Hello, " + name + "!"; });
    auto greet_func = lua.get_function<std::string(std::string)>("greet");
    std::string greeting = greet_func("World");
    std::cout << "greet('World') = " << greeting << std::endl;
    if (greeting != "Hello, World!") {
      throw std::runtime_error("String function_ref failed!");
    }
    std::cout << "String function_ref works!" << std::endl;

    std::cout << "\n=== All Lua Function Calling Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
