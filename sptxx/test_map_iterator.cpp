// test_map_iterator.cpp - Test Map iterator support

#include "sptxx.hpp"
#include <iostream>
#include <map>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Map Iterator ===" << std::endl;

    std::cout << "\n1. Testing basic iteration..." << std::endl;
    auto m = lua.create_map<int>();
    m.set<std::string>("one", 1);
    m.set<std::string>("two", 2);
    m.set<std::string>("three", 3);

    std::cout << "Iterating over map:" << std::endl;
    int count = 0;
    for (const auto &[key, value] : m) {
      std::cout << "  [" << key << "] = " << value << std::endl;
      count++;
    }
    if (count != 3) {
      throw std::runtime_error("Iterator should have visited 3 elements!");
    }
    std::cout << "Basic iteration works!" << std::endl;

    std::cout << "\n2. Testing empty map iteration..." << std::endl;
    auto empty_map = lua.create_map<int>();
    int empty_count = 0;
    for (const auto &[key, value] : empty_map) {
      (void)key;
      (void)value;
      empty_count++;
    }
    if (empty_count != 0) {
      throw std::runtime_error("Empty map should have 0 elements!");
    }
    std::cout << "Empty map iteration works!" << std::endl;

    std::cout << "\n3. Testing get_or_default..." << std::endl;
    int val1 = m.get_or_default<std::string>("one", 999);
    int val2 = m.get_or_default<std::string>("missing", 999);
    std::cout << "get_or_default('one') = " << val1 << std::endl;
    std::cout << "get_or_default('missing') = " << val2 << std::endl;
    if (val1 != 1 || val2 != 999) {
      throw std::runtime_error("get_or_default failed!");
    }
    std::cout << "get_or_default works!" << std::endl;

    std::cout << "\n4. Testing try_get..." << std::endl;
    auto opt1 = m.try_get<std::string>("two");
    auto opt2 = m.try_get<std::string>("nonexistent");
    if (!opt1.has_value() || opt1.value() != 2) {
      throw std::runtime_error("try_get for existing key failed!");
    }
    if (opt2.has_value()) {
      throw std::runtime_error("try_get for missing key should return nullopt!");
    }
    std::cout << "try_get('two') = " << opt1.value() << std::endl;
    std::cout << "try_get('nonexistent') = nullopt" << std::endl;
    std::cout << "try_get works!" << std::endl;

    std::cout << "\n5. Testing modification during iteration..." << std::endl;
    auto mod_map = lua.create_map<std::string>();
    mod_map.set<std::string>("a", "A");
    mod_map.set<std::string>("b", "B");
    mod_map.set<std::string>("c", "C");

    std::cout << "Collecting keys first..." << std::endl;
    std::vector<std::string> keys;
    for (const auto &[k, v] : mod_map) {
      keys.push_back(k);
      (void)v;
    }

    std::cout << "Modifying values after iteration..." << std::endl;
    for (const auto &k : keys) {
      std::string current = mod_map.get<std::string>(k);
      mod_map.set<std::string>(k, current + "_modified");
    }

    std::cout << "Verifying modifications:" << std::endl;
    for (const auto &[k, v] : mod_map) {
      std::cout << "  [" << k << "] = " << v << std::endl;
    }
    std::cout << "Modification after iteration works!" << std::endl;

    std::cout << "\n6. Testing integer keys..." << std::endl;
    auto int_key_map = lua.create_map<std::string>();
    int_key_map.set<int>(1, "one");
    int_key_map.set<int>(2, "two");
    int_key_map.set<int>(100, "hundred");

    std::cout << "Iterating with integer keys:" << std::endl;
    auto int_it = int_key_map.begin<int>();
    auto int_end = int_key_map.end<int>();
    while (int_it != int_end) {
      const auto &[k, v] = *int_it;
      std::cout << "  [" << k << "] = " << v << std::endl;
      ++int_it;
    }
    std::cout << "Integer key iteration works!" << std::endl;

    std::cout << "\n7. Testing contains..." << std::endl;
    if (!m.contains<std::string>("one")) {
      throw std::runtime_error("contains('one') should be true!");
    }
    if (m.contains<std::string>("not_in_map")) {
      throw std::runtime_error("contains('not_in_map') should be false!");
    }
    std::cout << "contains works!" << std::endl;

    std::cout << "\n8. Testing remove..." << std::endl;
    auto remove_map = lua.create_map<int>();
    remove_map.set<std::string>("to_remove", 123);
    remove_map.set<std::string>("to_keep", 456);

    if (!remove_map.contains<std::string>("to_remove")) {
      throw std::runtime_error("Key should exist before removal!");
    }
    remove_map.remove<std::string>("to_remove");
    if (remove_map.contains<std::string>("to_remove")) {
      throw std::runtime_error("Key should not exist after removal!");
    }
    if (!remove_map.contains<std::string>("to_keep")) {
      throw std::runtime_error("Other key should still exist!");
    }
    std::cout << "remove works!" << std::endl;

    std::cout << "\n9. Testing map from Lua..." << std::endl;
    lua.do_string("lua_map = { \"x\": 10, \"y\": 20, \"z\": 30 };");
    lua_getglobal(lua.lua_state(), "lua_map");
    auto lua_created_map =
        sptxx::map<int>(lua.lua_state(), luaL_ref(lua.lua_state(), LUA_REGISTRYINDEX));

    std::cout << "Iterating Lua-created map:" << std::endl;
    int lua_count = 0;
    for (const auto &[k, v] : lua_created_map) {
      std::cout << "  [" << k << "] = " << v << std::endl;
      lua_count++;
    }
    if (lua_count != 3) {
      throw std::runtime_error("Lua-created map should have 3 elements!");
    }
    std::cout << "Map from Lua works!" << std::endl;

    std::cout << "\n=== All Map Iterator Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
