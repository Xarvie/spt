#include "sptxx.hpp"
#include <iostream>
#include <string>

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing object_list (list<void>) ===" << std::endl;

    std::cout << "\n1. Creating object_list..." << std::endl;
    auto lst = lua.create_list<void>();
    std::cout << "Created, size: " << lst.size() << std::endl;

    std::cout << "\n2. Testing push_back with different types..." << std::endl;
    lst.push_back<int>(42);
    lst.push_back<double>(3.14);
    lst.push_back<std::string>("hello");
    lst.push_back<bool>(true);
    std::cout << "After 4 pushes, size: " << lst.size() << std::endl;

    std::cout << "\n3. Testing get with different types..." << std::endl;
    std::cout << "lst.get<int>(0) = " << lst.get<int>(0) << std::endl;
    std::cout << "lst.get<double>(1) = " << lst.get<double>(1) << std::endl;
    std::cout << "lst.get<std::string>(2) = " << lst.get<std::string>(2) << std::endl;
    std::cout << "lst.get<bool>(3) = " << lst.get<bool>(3) << std::endl;

    std::cout << "\n4. Testing set with different types..." << std::endl;
    lst.set<int>(0, 100);
    lst.set<double>(1, 2.718);
    lst.set<std::string>(2, "world");
    lst.set<bool>(3, false);
    std::cout << "After set:" << std::endl;
    std::cout << "lst.get<int>(0) = " << lst.get<int>(0) << std::endl;
    std::cout << "lst.get<double>(1) = " << lst.get<double>(1) << std::endl;
    std::cout << "lst.get<std::string>(2) = " << lst.get<std::string>(2) << std::endl;
    std::cout << "lst.get<bool>(3) = " << lst.get<bool>(3) << std::endl;

    std::cout << "\n5. Testing pop_back..." << std::endl;
    auto last = lst.pop_back<bool>();
    std::cout << "Popped: " << last << ", size now: " << lst.size() << std::endl;

    std::cout << "\n6. Testing capacity operations..." << std::endl;
    std::cout << "capacity: " << lst.capacity() << std::endl;
    lst.reserve(100);
    std::cout << "After reserve(100), capacity: " << lst.capacity() << std::endl;

    std::cout << "\n7. Testing resize..." << std::endl;
    lst.resize(10);
    std::cout << "After resize(10), size: " << lst.size() << std::endl;

    std::cout << "\n8. Testing empty..." << std::endl;
    std::cout << "empty: " << lst.empty() << std::endl;
    lst.resize(0);
    std::cout << "After resize(0), empty: " << lst.empty() << std::endl;

    std::cout << "\n9. Testing copy and move..." << std::endl;
    lst.push_back<int>(1);
    lst.push_back<int>(2);
    
    sptxx::object_list lst2 = lst;
    std::cout << "Copied, lst2.size: " << lst2.size() << std::endl;
    
    sptxx::object_list lst3 = std::move(lst2);
    std::cout << "Moved, lst3.size: " << lst3.size() << ", lst2.valid: " << lst2.valid() << std::endl;

    std::cout << "\n10. Testing validity..." << std::endl;
    sptxx::object_list invalid;
    std::cout << "Default constructed valid: " << invalid.valid() << std::endl;
    std::cout << "lst valid: " << lst.valid() << std::endl;

    std::cout << "\n=== All object_list Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "\n[TEST FAILED] Error: " << e.what() << std::endl;
    return 1;
  }
}
