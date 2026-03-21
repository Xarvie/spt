#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Map clear() Method ===" << std::endl;

    std::cout << "\n1. Creating a map with multiple entries..." << std::endl;
    auto m = lua.create_map<int>();
    m.set<std::string>("one", 1);
    m.set<std::string>("two", 2);
    m.set<std::string>("three", 3);
    m.set<std::string>("four", 4);
    m.set<std::string>("five", 5);

    std::cout << "Map contents before clear:" << std::endl;
    int count = 0;
    for (const auto &[key, value] : m) {
      std::cout << "  [" << key << "] = " << value << std::endl;
      count++;
    }
    std::cout << "Total entries: " << count << std::endl;

    if (count != 5) {
      throw std::runtime_error("Map should have 5 entries before clear!");
    }

    std::cout << "\n2. Calling clear()..." << std::endl;
    m.clear();

    std::cout << "clear() returned successfully!" << std::endl;

    std::cout << "\n3. Verifying map is empty after clear()..." << std::endl;
    count = 0;
    for (const auto &[key, value] : m) {
      std::cout << "  [" << key << "] = " << value << std::endl;
      count++;
    }
    std::cout << "Total entries after clear: " << count << std::endl;

    if (count != 0) {
      throw std::runtime_error("Map should be empty after clear()!");
    }

    std::cout << "\n4. Testing clear() on empty map..." << std::endl;
    auto empty_map = lua.create_map<int>();
    empty_map.clear();
    std::cout << "clear() on empty map works!" << std::endl;

    std::cout << "\n5. Testing clear() and re-add..." << std::endl;
    auto reuse_map = lua.create_map<std::string>();
    reuse_map.set<std::string>("a", "A");
    reuse_map.set<std::string>("b", "B");
    reuse_map.clear();
    reuse_map.set<std::string>("c", "C");
    reuse_map.set<std::string>("d", "D");

    count = 0;
    for (const auto &[key, value] : reuse_map) {
      std::cout << "  [" << key << "] = " << value << std::endl;
      count++;
    }
    if (count != 2) {
      throw std::runtime_error("Reused map should have 2 entries!");
    }

    std::cout << "\n=== All Map clear() Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
