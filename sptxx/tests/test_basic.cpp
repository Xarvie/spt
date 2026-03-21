// test_basic.cpp - Basic test for sptxx C++ bindings

#include "sptxx.hpp"
#include <iostream>

// Simple function for testing
int add_function(int a, int b) {
    return a + b;
}

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        // Test basic function binding with function pointer
        lua.set_function("add", add_function);
        
        // Test global variable
        lua.set_global("test_value", 42);
        
        // Test list creation
        auto list = lua.create_list<int>(10);
        list.set(0, 100);
        std::cout << "List[0] = " << list.get(0) << std::endl;
        std::cout << "List size = " << list.size() << std::endl;
        
        // Test Lua execution
        lua.do_string("print('Hello from Lua!');");
        lua.do_string("result = add(10, 20);");
        int result = lua.get_global<int>("result");
        std::cout << "Lua result = " << result << std::endl;
        
        std::cout << "Basic tests passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}
