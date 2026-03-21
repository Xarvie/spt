// test_containers_advanced.cpp - Test advanced map/list interop with custom AST syntax

#include "sptxx.hpp"
#include <iostream>
#include <string>

// 一个接收容器并进行打印的 C++ 测试类
struct DataProcessor {
  DataProcessor() = default;

  // 接收从脚本传来的 List
  void process_list(const sptxx::list<std::string>& lst) {
    std::cout << "[C++] Processing List (Size: " << lst.size() << "):" << std::endl;
    for (size_t i = 0; i < lst.size(); ++i) {
      std::cout << "  - [" << i << "] = " << lst.get(i) << std::endl;
    }
  }

  // 接收从脚本传来的 Map
  void process_map(const sptxx::map<int>& m) {
    std::cout << "[C++] Processing Map:" << std::endl;
    // 简单验证几个已知的 key
    if (m.contains<std::string>("power")) {
      std::cout << "  - power = " << m.get<std::string>("power") << std::endl;
    }
    if (m.contains<std::string>("speed")) {
      std::cout << "  - speed = " << m.get<std::string>("speed") << std::endl;
    }
  }
};

void safe_do_string(sptxx::state& lua, const char* code) {
  if (luaL_dostring(lua.lua_state(), code) != LUA_OK) {
    std::string err = lua_tostring(lua.lua_state(), -1);
    lua_pop(lua.lua_state(), 1);
    throw std::runtime_error("Lua Error: " + err);
  }
}

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing Advanced Container Interop ===" << std::endl;

    // 1. 注册 DataProcessor 类
    auto dp_type = lua.new_usertype<DataProcessor>("DataProcessor");
    dp_type.set("process_list", &DataProcessor::process_list);
    dp_type.set("process_map", &DataProcessor::process_map);

    // 2. 在 C++ 中创建容器，并注入到全局环境中
    auto cpp_list = lua.create_list<std::string>();
    cpp_list.push_back("Alpha");
    cpp_list.push_back("Beta");
    lua.set_global("globalList", cpp_list);

    auto cpp_map = lua.create_map<int>();
    cpp_map.set<std::string>("init_val", 100);
    lua.set_global("globalMap", cpp_map);

    // 3. 执行符合 LangParser.g4 语法的脚本
    std::cout << "\nExecuting Custom Syntax Script..." << std::endl;

    safe_do_string(lua, R"(
            // 实例化 C++ 类
            auto processor = new DataProcessor();

            // 验证并修改 C++ 传进来的 List
            print("Script sees globalList size: " .. #globalList);

            // 【修复这里】：SPT Lua 数组是严格 0 索引且防越界的！
            // 原先有 2 个元素，合法索引是 0 和 1。我们修改索引 1 的元素。
            globalList[1] = "Gamma";

            processor.process_list(globalList);

            // 使用 AST 支持的字面量创建新的 List 并传给 C++
            auto scriptList = ["Delta", "Echo", "Foxtrot"];
            processor.process_list(scriptList);

            // 使用 AST 支持的字面量创建新的 Map 并传给 C++
            auto scriptMap = {
                "power": 9000,
                "speed": 120
            };
            processor.process_map(scriptMap);
        )");

    std::cout << "\n=== All Container Interop Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}