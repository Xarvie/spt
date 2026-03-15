#include "sptxx.hpp"
#include <iostream>

int main() {
  std::cout << "=== Testing function and global persistence ===" << std::endl;

  sptxx::state lua;
  lua.open_libraries();

  std::cout << "\nTest 1: print works after open_libraries" << std::endl;
  try {
    lua.do_string("print('hello from SPT!');");
    std::cout << "Test 1 PASSED!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 1 FAILED: " << e.what() << std::endl;
  }

  std::cout << "\nTest 2: global variable in one do_string, access in another" << std::endl;
  try {
    lua.do_string("global int x = 100;");
    lua.do_string("print('x =', x);");
    std::cout << "Test 2 PASSED!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 2 FAILED: " << e.what() << std::endl;
  }

  std::cout << "\nTest 3: global function in one do_string, call in another" << std::endl;
  try {
    lua.do_string("global int add(int a, int b) { return a + b; }");
    lua.do_string("print('add(1,2) =', add(1, 2));");
    std::cout << "Test 3 PASSED!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 3 FAILED: " << e.what() << std::endl;
  }

  std::cout << "\nTest 4: local variable in one do_string, access in another (should fail)"
            << std::endl;
  try {
    lua.do_string("int local_x = 200;");
    lua.do_string("print('local_x =', local_x);");
    std::cout << "Test 4 PASSED (unexpected!)" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 4 EXPECTED FAILURE: " << e.what() << std::endl;
  }

  std::cout << "\nTest 5: multiple statements in one do_string" << std::endl;
  try {
    lua.do_string("int a = 1; int b = 2; print('a + b =', a + b);");
    std::cout << "Test 5 PASSED!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 5 FAILED: " << e.what() << std::endl;
  }

  std::cout << "\nTest 6: global function then local code in same do_string" << std::endl;
  try {
    lua.do_string("global int mul(int a, int b) { return a * b; }");
    lua.do_string("int result = mul(3, 4); print('result =', result);");
    std::cout << "Test 6 PASSED!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 6 FAILED: " << e.what() << std::endl;
  }

  std::cout << "\nTest 7: call mul from another do_string" << std::endl;
  try {
    lua.do_string("print('mul(5,6) =', mul(5, 6));");
    std::cout << "Test 7 PASSED!" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Test 7 FAILED: " << e.what() << std::endl;
  }

  return 0;
}
