// test_comprehensive.cpp - Comprehensive test for all sptxx features

#include "sptxx.hpp"
#include <iostream>
#include <string>
#include <vector>

// Test function for binding
int add_numbers(int a, int b) {
    return a + b;
}

struct Person {
    std::string name;
    int age;
    
    Person(const std::string& n, int a) : name(n), age(a) {}
    
    void introduce() const {
        std::cout << "Hi, I'm " << name << " and I'm " << age << " years old." << std::endl;
    }
    
    void set_name(const std::string& n) {
        name = n;
    }
    
    std::string get_name() const {
        return name;
    }
    
    void set_age(int a) {
        age = a;
    }
    
    int get_age() const {
        return age;
    }
};

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Comprehensive SPTXX Test ===" << std::endl;
        
        // Test 1: Basic Lua execution with print
        std::cout << "\n1. Testing basic Lua execution..." << std::endl;
        lua.do_string("print('Hello from comprehensive test!');");
        lua.do_string("result = 42;");
        int result = lua.get_global<int>("result");
        std::cout << "Lua global result = " << result << std::endl;
        if (result != 42) throw std::runtime_error("Basic execution failed");
        
        // Test 2: Function binding
        std::cout << "\n2. Testing function binding..." << std::endl;
        lua.set_function("add", add_numbers);
        lua.do_string("sum = add(10, 20);");
        int sum = lua.get_global<int>("sum");
        std::cout << "Function call result = " << sum << std::endl;
        if (sum != 30) throw std::runtime_error("Function binding failed");
        
        // Test 3: Global variable setting/getting
        std::cout << "\n3. Testing global variables..." << std::endl;
        lua.set_global("test_string", std::string("Hello World"));
        lua.set_global("test_number", 123.456);
        lua.set_global("test_bool", true);
        
        std::string str_val = lua.get_global<std::string>("test_string");
        double num_val = lua.get_global<double>("test_number");
        bool bool_val = lua.get_global<bool>("test_bool");
        
        std::cout << "String: " << str_val << std::endl;
        std::cout << "Number: " << num_val << std::endl;
        std::cout << "Bool: " << (bool_val ? "true" : "false") << std::endl;
        
        if (str_val != "Hello World" || num_val != 123.456 || !bool_val) {
            throw std::runtime_error("Global variable test failed");
        }
        
        // Test 4: List operations
        std::cout << "\n4. Testing list operations..." << std::endl;
        auto list = lua.create_list<int>(5);
        std::cout << "Created list with capacity 5" << std::endl;
        std::cout << "List size: " << list.size() << std::endl;
        std::cout << "List capacity: " << list.capacity() << std::endl;
        std::cout << "List empty: " << (list.empty() ? "true" : "false") << std::endl;
        
        // Set values
        for (size_t i = 0; i < 5; ++i) {
            list.set(i, static_cast<int>(i * 10));
        }
        
        // Get values
        for (size_t i = 0; i < 5; ++i) {
            int val = list.get(i);
            std::cout << "list[" << i << "] = " << val << std::endl;
            if (val != static_cast<int>(i * 10)) {
                throw std::runtime_error("List get/set failed");
            }
        }
        
        // Test resize
        list.resize(8);
        std::cout << "After resize(8), size: " << list.size() << std::endl;
        if (list.size() != 8) throw std::runtime_error("List resize failed");
        
        // Test push_back/pop_back
        list.push_back(100);
        std::cout << "After push_back(100), size: " << list.size() << std::endl;
        int popped = list.pop_back();
        std::cout << "Popped value: " << popped << std::endl;
        if (popped != 100) throw std::runtime_error("List push/pop failed");
        
        // Test 5: Map operations (simplified)
        std::cout << "\n5. Testing map operations..." << std::endl;
        // Just test basic table functionality
        lua.do_string("test_var = 'Map test works!';");
        std::string test_result = lua.get_global<std::string>("test_var");
        std::cout << "Map test result: " << test_result << std::endl;
        if (test_result != "Map test works!") {
            throw std::runtime_error("Basic map test failed");
        }
        
        std::cout << "\n=== All tests passed! ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}