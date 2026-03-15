// test_debug.cpp - Debug test to understand the issue

#include "sptxx.hpp"
#include <iostream>

int main() {
    try {
        sptxx::state lua;
        
        std::cout << "Creating list with capacity 10..." << std::endl;
        auto list = lua.create_list<int>(10);
        std::cout << "List created successfully" << std::endl;
        
        std::cout << "Getting list size..." << std::endl;
        size_t size = list.size();
        std::cout << "List size: " << size << std::endl;
        
        std::cout << "Setting list[0] = 100..." << std::endl;
        list.set(0, 100);
        std::cout << "Set successful" << std::endl;
        
        std::cout << "Getting list[0]..." << std::endl;
        int value = list.get(0);
        std::cout << "List[0] = " << value << std::endl;
        
        std::cout << "Debug test passed!" << std::endl;
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}