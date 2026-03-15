// test_usertype.cpp - Test usertype registration issue - Simplified version

#include "sptxx.hpp"
#include <iostream>
#include <string>

struct Person {
    std::string name;
    int age;
    
    Person() : name("Unknown"), age(0) {}
    Person(const std::string& n, int a) : name(n), age(a) {}
    
    void introduce() const {
        std::cout << "Hi, I'm " << name << " and I'm " << age << " years old." << std::endl;
    }
    
    void set_name(const std::string& n) { name = n; }
    std::string get_name() const { return name; }
    void set_age(int a) { age = a; }
    int get_age() const { return age; }
};

int main() {
    try {
        sptxx::state lua;
        lua.open_libraries();
        
        std::cout << "=== Testing Usertype Registration Issue ===" << std::endl;
        
        // Test 1: Basic usertype creation
        std::cout << "\n1. Testing basic usertype creation..." << std::endl;
        try {
            auto person_type = lua.new_usertype<Person>("Person");
            std::cout << "Usertype created successfully!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Usertype creation failed: " << e.what() << std::endl;
            std::cout << "This indicates the usertype registration issue." << std::endl;
            return 1;
        }
        
        // Test 2: Try to create an instance in Lua
        std::cout << "\n2. Testing Lua instance creation..." << std::endl;
        try {
            lua.do_string("p = Person();");
            std::cout << "Lua instance creation succeeded!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Lua instance creation failed: " << e.what() << std::endl;
            return 1;
        }
        
        // Test 3: Try to call methods
        std::cout << "\n3. Testing method calls..." << std::endl;
        try {
            lua.do_string("print('Person created successfully');");
            std::cout << "Method call worked!" << std::endl;
        } catch (const std::exception& e) {
            std::cout << "Method call failed: " << e.what() << std::endl;
            return 1;
        }
        
        std::cout << "\n=== Usertype Test Complete ===" << std::endl;
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}