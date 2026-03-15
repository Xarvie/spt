// test_syntax.cpp - Test SPT Lua syntax capabilities

#include "sptxx.hpp"
#include <iostream>

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Testing SPT Lua Syntax Capabilities ===" << std::endl;
        
        // Test 1: Table constructor syntax
        std::cout << "\n1. Testing table constructor syntax..." << std::endl;
        try {
            lua.do_string("test_table = {name = 'Alice', age = 25};");
            std::cout << "Table constructor syntax works!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Table constructor syntax failed: " << e.what() << std::endl;
        }
        
        // Test 2: Nested property access
        std::cout << "\n2. Testing nested property access..." << std::endl;
        try {
            lua.do_string("obj = {}; obj.nested = {}; obj.nested.value = 42;");
            // Try to access nested property directly
            lua.do_string("result = obj.nested.value;");
            int result = lua.get_global<int>("result");
            std::cout << "Nested property access works: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Nested property access failed: " << e.what() << std::endl;
        }
        
        // Test 3: Array literal syntax
        std::cout << "\n3. Testing array literal syntax..." << std::endl;
        try {
            lua.do_string("arr = [1, 2, 3, 4, 5];");
            std::cout << "Array literal syntax works!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Array literal syntax failed: " << e.what() << std::endl;
        }
        
        // Test 4: Function call syntax
        std::cout << "\n4. Testing function call syntax..." << std::endl;
        try {
            lua.do_string("function test_func(a, b) return a + b; end");
            lua.do_string("result = test_func(10, 20);");
            int result = lua.get_global<int>("result");
            std::cout << "Function call syntax works: " << result << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Function call syntax failed: " << e.what() << std::endl;
        }
        
        // Test 5: Method call syntax with receiver
        std::cout << "\n5. Testing method call syntax..." << std::endl;
        try {
            lua.do_string("table = {}; table.push = function(self, value) print('Pushed: ' .. value); end");
            lua.do_string("table:push('hello');");
            std::cout << "Method call syntax works!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Method call syntax failed: " << e.what() << std::endl;
        }
        
        std::cout << "\n=== Syntax Test Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}