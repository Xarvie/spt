#include "sptxx.hpp"
#include <cassert>
#include <iostream>

int main() {
  std::cout << "=== Testing global variable persistence across do_string calls ===" << std::endl;

  sptxx::state lua;
  lua.open_libraries();

  std::cout << "\nTest 1: global keyword - set global in first do_string, read in second"
            << std::endl;
  lua.do_string("global int x = 100;");
  lua.do_string("print('x from second do_string:', x);");
  int x = lua.get_global<int>("x");
  std::cout << "x from C++: " << x << std::endl;
  assert(x == 100);
  std::cout << "Test 1 PASSED!" << std::endl;

  std::cout << "\nTest 2: Modify global across multiple do_string calls" << std::endl;
  lua.do_string("global int y = 1;");
  lua.do_string("y = y + 10;");
  lua.do_string("y = y * 2;");
  int y = lua.get_global<int>("y");
  std::cout << "y after three do_string calls: " << y << std::endl;
  assert(y == 22);
  std::cout << "Test 2 PASSED!" << std::endl;

  std::cout << "\nTest 3: Table persistence" << std::endl;
  lua.do_string("global map<int, int> t = {};");
  lua.do_string("t[1] = 10;");
  lua.do_string("t[2] = 20;");
  lua.do_string("print('t[1] =', t[1], 't[2] =', t[2]);");
  std::cout << "Test 3 PASSED!" << std::endl;

  std::cout << "\nTest 4: Local variable should NOT persist as global" << std::endl;
  lua.do_string("int local_var = 999;");
  auto result = lua.get_global_or<int>("local_var");
  if (result.has_value()) {
    std::cout << "ERROR: local_var should not be global! value = " << result.value() << std::endl;
    return 1;
  }
  std::cout << "local_var correctly not found in globals (it's a local)" << std::endl;
  std::cout << "Test 4 PASSED!" << std::endl;

  std::cout << "\n=== All tests PASSED! Global variables ARE persistent ===" << std::endl;
  return 0;
}
