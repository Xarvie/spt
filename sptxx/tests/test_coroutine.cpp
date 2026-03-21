#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state_with_coroutine lua;
    lua.open_libraries();

    luaL_requiref(lua.lua_state(), "coroutine", luaopen_coroutine, 1);
    lua_pop(lua.lua_state(), 1);

    std::cout << "=== Testing Coroutine Support ===" << std::endl;

    std::cout << "\n1. Testing coroutine from Lua function..." << std::endl;
    lua.do_string("co_func = function() -> int { vars x = 0; x = x + 1; coroutine.yield(x); x = x + 1; coroutine.yield(x); return x + 1; };");

    auto co = lua.create_coroutine_from_function("co_func");
    std::cout << "Created coroutine from Lua function" << std::endl;
    std::cout << "Initial status: " << co.status_string() << std::endl;

    if (co.status() != sptxx::coroutine_status::suspended) {
      throw std::runtime_error("New coroutine should be suspended!");
    }

    bool yielded = co.resume();
    std::cout << "After first resume: yielded=" << yielded << ", status=" << co.status_string() << std::endl;
    if (!yielded || co.status() != sptxx::coroutine_status::suspended) {
      throw std::runtime_error("First resume failed!");
    }

    yielded = co.resume();
    std::cout << "After second resume: yielded=" << yielded << ", status=" << co.status_string() << std::endl;
    if (!yielded || co.status() != sptxx::coroutine_status::suspended) {
      throw std::runtime_error("Second resume failed!");
    }

    yielded = co.resume();
    std::cout << "After third resume: yielded=" << yielded << ", status=" << co.status_string() << std::endl;
    if (yielded || !co.is_dead()) {
      throw std::runtime_error("Third resume should complete coroutine!");
    }

    std::cout << "\n2. Testing coroutine status..." << std::endl;
    lua.do_string("co2_func = function() -> int { coroutine.yield(1); coroutine.yield(2); return 0; };");
    auto co2 = lua.create_coroutine_from_function("co2_func");

    std::cout << "New coroutine status: " << co2.status_string() << std::endl;
    if (!co2.is_suspended()) {
      throw std::runtime_error("New coroutine should be suspended!");
    }

    co2.resume();
    std::cout << "After first resume: " << co2.status_string() << std::endl;

    co2.resume();
    std::cout << "After second resume: " << co2.status_string() << std::endl;

    co2.resume();
    std::cout << "After third resume: " << co2.status_string() << std::endl;

    if (!co2.is_dead()) {
      throw std::runtime_error("Completed coroutine should be dead!");
    }

    std::cout << "\n3. Testing error handling in coroutine..." << std::endl;
    lua.do_string("error_co = function() -> void { error('Test error in coroutine'); };");
    auto co3 = lua.create_coroutine_from_function("error_co");

    bool caught_error = false;
    try {
      co3.resume();
    } catch (const sptxx::error &e) {
      caught_error = true;
      std::cout << "Correctly caught error: " << e.what() << std::endl;
    }
    if (!caught_error) {
      throw std::runtime_error("Should have caught error from coroutine!");
    }

    std::cout << "\n4. Testing coroutine close..." << std::endl;
    lua.do_string("co4_func = function() -> void { coroutine.yield(); coroutine.yield(); };");
    auto co4 = lua.create_coroutine_from_function("co4_func");

    co4.resume();
    std::cout << "Suspended coroutine status: " << co4.status_string() << std::endl;

    bool closed = co4.close();
    std::cout << "Close result: " << closed << std::endl;
    if (!closed) {
      throw std::runtime_error("Should be able to close suspended coroutine!");
    }

    std::cout << "\n5. Testing coroutine validity..." << std::endl;
    sptxx::coroutine invalid_co;
    if (invalid_co.valid()) {
      throw std::runtime_error("Default constructed coroutine should be invalid!");
    }
    std::cout << "Invalid coroutine check passed" << std::endl;

    std::cout << "\n6. Testing yield with values..." << std::endl;
    lua.do_string("co5_func = function() -> int { coroutine.yield(10); return 100; };");
    auto co5 = lua.create_coroutine_from_function("co5_func");

    yielded = co5.resume();
    std::cout << "Yielded: " << yielded << std::endl;

    lua_State *co_state = co5.thread_state();
    int top = lua_gettop(co_state);
    std::cout << "Stack top after yield: " << top << std::endl;

    yielded = co5.resume();
    std::cout << "Final yielded: " << yielded << ", status: " << co5.status_string() << std::endl;

    std::cout << "\n7. Testing is_yieldable..." << std::endl;
    lua.do_string("co6_func = function() -> bool { return coroutine.isyieldable(); };");
    auto co6 = lua.create_coroutine_from_function("co6_func");
    std::cout << "is_yieldable: " << co6.is_yieldable() << std::endl;

    std::cout << "\n8. Testing coroutine from script..." << std::endl;
    auto co7 = lua.get_coroutine_from_script("return function() -> int { vars i = 0; while (i < 3) { i = i + 1; coroutine.yield(i); } return i; };");
    
    for (int i = 1; i <= 3; i++) {
      yielded = co7.resume();
      std::cout << "Resume " << i << ": yielded=" << yielded << std::endl;
    }
    
    yielded = co7.resume();
    std::cout << "Final resume: yielded=" << yielded << ", is_dead=" << co7.is_dead() << std::endl;

    std::cout << "\n=== All Coroutine Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
