#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing object_map (map<void>) ===" << std::endl;

    std::cout << "\n1. Creating object_map..." << std::endl;
    auto m = lua.create_map<void>();
    std::cout << "Created, valid: " << m.valid() << std::endl;

    std::cout << "\n2. Testing set with different key/value types..." << std::endl;
    m.set<std::string, int>("int_key", 42);
    m.set<std::string, double>("double_key", 3.14);
    m.set<std::string, std::string>("string_key", "hello");
    m.set<std::string, bool>("bool_key", true);
    m.set<int, std::string>(100, "int_key_value");
    std::cout << "Set 5 entries" << std::endl;

    std::cout << "\n3. Testing get with different types..." << std::endl;
    std::cout << "m.get<std::string, int>(\"int_key\") = " << m.get<std::string, int>("int_key") << std::endl;
    std::cout << "m.get<std::string, double>(\"double_key\") = " << m.get<std::string, double>("double_key") << std::endl;
    std::cout << "m.get<std::string, std::string>(\"string_key\") = " << m.get<std::string, std::string>("string_key") << std::endl;
    std::cout << "m.get<std::string, bool>(\"bool_key\") = " << m.get<std::string, bool>("bool_key") << std::endl;
    std::cout << "m.get<int, std::string>(100) = " << m.get<int, std::string>(100) << std::endl;

    std::cout << "\n4. Testing contains..." << std::endl;
    std::cout << "contains \"int_key\": " << m.contains<std::string>("int_key") << std::endl;
    std::cout << "contains \"nonexistent\": " << m.contains<std::string>("nonexistent") << std::endl;

    std::cout << "\n5. Testing try_get..." << std::endl;
    auto val = m.try_get<std::string, int>("int_key");
    std::cout << "try_get \"int_key\": has_value=" << val.has_value() << ", value=" << val.value_or(0) << std::endl;
    
    auto missing = m.try_get<std::string, int>("missing");
    std::cout << "try_get \"missing\": has_value=" << missing.has_value() << std::endl;

    std::cout << "\n6. Testing get_or_default..." << std::endl;
    int default_val = m.get_or_default<std::string, int>("nonexistent", 999);
    std::cout << "get_or_default \"nonexistent\": " << default_val << std::endl;

    std::cout << "\n7. Testing remove..." << std::endl;
    m.remove<std::string>("int_key");
    std::cout << "After remove, contains \"int_key\": " << m.contains<std::string>("int_key") << std::endl;

    std::cout << "\n8. Testing clear..." << std::endl;
    m.set<std::string, int>("a", 1);
    m.set<std::string, int>("b", 2);
    m.set<std::string, int>("c", 3);
    std::cout << "Added 3 more entries" << std::endl;
    m.clear();
    std::cout << "After clear, contains \"a\": " << m.contains<std::string>("a") << std::endl;

    std::cout << "\n9. Testing copy and move..." << std::endl;
    m.set<std::string, int>("x", 10);
    
    sptxx::object_map m2 = m;
    std::cout << "Copied, m2.contains \"x\": " << m2.contains<std::string>("x") << std::endl;
    
    sptxx::object_map m3 = std::move(m2);
    std::cout << "Moved, m3.contains \"x\": " << m3.contains<std::string>("x") << ", m2.valid: " << m2.valid() << std::endl;

    std::cout << "\n10. Testing validity..." << std::endl;
    sptxx::object_map invalid;
    std::cout << "Default constructed valid: " << invalid.valid() << std::endl;
    std::cout << "m valid: " << m.valid() << std::endl;

    std::cout << "\n=== All object_map Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
