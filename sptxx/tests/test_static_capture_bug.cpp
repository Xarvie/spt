#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing static capture bug in make_function_wrapper ===" << std::endl;

    std::cout << "\n1. Testing with std::function (same type)..." << std::endl;
    
    std::function<int(int)> func1 = [](int x) -> int {
      std::cout << "  func1 called with " << x << std::endl;
      return x * 10;
    };
    
    std::function<int(int)> func2 = [](int x) -> int {
      std::cout << "  func2 called with " << x << std::endl;
      return x * 100;
    };

    lua.set_function("test_func1", func1);
    lua.set_function("test_func2", func2);

    std::cout << "\nCalling test_func1(5)..." << std::endl;
    lua.do_string("vars r1 = test_func1(5); print('Result: ' .. r1);");
    std::cout << "Expected: 50 (func1)" << std::endl;

    std::cout << "\nCalling test_func2(5)..." << std::endl;
    lua.do_string("vars r2 = test_func2(5); print('Result: ' .. r2);");
    std::cout << "Expected: 500 (func2)" << std::endl;
    std::cout << "If result is 50, the bug is CONFIRMED!" << std::endl;

    std::cout << "\n2. Testing with lambdas (different types)..." << std::endl;
    
    auto lambda1 = [](int x) -> int {
      std::cout << "  lambda1 called with " << x << std::endl;
      return x + 1;
    };
    
    auto lambda2 = [](int x) -> int {
      std::cout << "  lambda2 called with " << x << std::endl;
      return x + 2;
    };

    lua.set_function("test_lambda1", lambda1);
    lua.set_function("test_lambda2", lambda2);

    std::cout << "\nCalling test_lambda1(10)..." << std::endl;
    lua.do_string("vars r3 = test_lambda1(10); print('Result: ' .. r3);");
    std::cout << "Expected: 11 (lambda1)" << std::endl;

    std::cout << "\nCalling test_lambda2(10)..." << std::endl;
    lua.do_string("vars r4 = test_lambda2(10); print('Result: ' .. r4);");
    std::cout << "Expected: 12 (lambda2)" << std::endl;

    std::cout << "\n=== Test Complete ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
