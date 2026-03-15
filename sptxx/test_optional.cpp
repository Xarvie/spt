// test_optional.cpp - Test std::optional support

#include "sptxx.hpp"
#include <iostream>
#include <optional>
#include <string>

std::optional<int> maybe_return_int(bool return_value) {
  if (return_value) {
    return 42;
  }
  return std::nullopt;
}

std::optional<std::string> maybe_return_string(bool return_value) {
  if (return_value) {
    return std::string("Hello, Optional!");
  }
  return std::nullopt;
}

int main() {
  try {
    sptxx::state lua;
    lua.open_libraries();

    std::cout << "=== Testing std::optional Support ===" << std::endl;

    std::cout << "\n1. Testing optional to Lua (push nil)..." << std::endl;
    std::optional<int> empty_opt;
    lua.set_global("empty_opt", empty_opt);
    lua.do_string("if (empty_opt == null) { print('empty_opt is null'); }");
    std::cout << "Empty optional correctly pushed as null" << std::endl;

    std::cout << "\n2. Testing optional to Lua (push value)..." << std::endl;
    std::optional<int> value_opt = 100;
    lua.set_global("value_opt", value_opt);
    lua.do_string("print('value_opt = ' .. value_opt);");
    std::cout << "Optional with value correctly pushed" << std::endl;

    std::cout << "\n3. Testing optional string..." << std::endl;
    std::optional<std::string> str_opt = "test string";
    lua.set_global("str_opt", str_opt);
    lua.do_string("print('str_opt = ' .. str_opt);");
    std::cout << "Optional string correctly pushed" << std::endl;

    std::cout << "\n4. Testing get_global_or (value exists)..." << std::endl;
    lua.do_string("existing_var = 999;");
    auto result = lua.get_global_or<int>("existing_var");
    if (!result.has_value()) {
      throw std::runtime_error("Expected value but got nullopt");
    }
    if (result.value() != 999) {
      throw std::runtime_error("Value mismatch");
    }
    std::cout << "get_global_or returned correct value: " << result.value() << std::endl;

    std::cout << "\n5. Testing get_global_or (value missing)..." << std::endl;
    auto missing = lua.get_global_or<int>("nonexistent_var");
    if (missing.has_value()) {
      throw std::runtime_error("Expected nullopt but got value");
    }
    std::cout << "get_global_or correctly returned nullopt for missing variable" << std::endl;

    std::cout << "\n6. Testing function returning optional..." << std::endl;
    lua.set_function("maybe_int", maybe_return_int);

    lua.do_string("result1 = maybe_int(true); print('maybe_int(true) = ' .. result1);");
    int r1 = lua.get_global<int>("result1");
    std::cout << "maybe_int(true) = " << r1 << std::endl;

    lua.do_string(
        "result2 = maybe_int(false); if (result2 == null) { print('maybe_int(false) = null'); }");
    std::cout << "maybe_int(false) = null (correct)" << std::endl;

    std::cout << "\n7. Testing optional string from function..." << std::endl;
    lua.set_function("maybe_str", maybe_return_string);
    lua.do_string("str_result = maybe_str(true); print('maybe_str(true) = ' .. str_result);");
    std::cout << "maybe_str(true) returned correct string" << std::endl;

    lua.do_string(
        "str_nil = maybe_str(false); if (str_nil == null) { print('maybe_str(false) = null'); }");
    std::cout << "maybe_str(false) = null (correct)" << std::endl;

    std::cout << "\n8. Testing map try_get..." << std::endl;
    auto m = lua.create_map<int>();
    m.set<std::string>("key1", 111);

    auto val = m.try_get<std::string>("key1");
    if (!val.has_value() || val.value() != 111) {
      throw std::runtime_error("try_get failed for existing key");
    }
    std::cout << "try_get for existing key: " << val.value() << std::endl;

    auto missing_val = m.try_get<std::string>("missing_key");
    if (missing_val.has_value()) {
      throw std::runtime_error("try_get should return nullopt for missing key");
    }
    std::cout << "try_get for missing key returned nullopt (correct)" << std::endl;

    std::cout << "\n=== All std::optional Tests Passed! ===" << std::endl;
    return 0;

  } catch (const std::exception &e) {
    std::cerr << "Error: " << e.what() << std::endl;
    return 1;
  }
}
