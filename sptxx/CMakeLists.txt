# CMakeLists.txt for sptxx - SPT Lua 5.5 C++ bindings

cmake_minimum_required(VERSION 3.10)
project(sptxx VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find SPT Lua library
find_library(SPT_LUA_LIBRARY 
    NAMES sptscript_library_shared sptscript_library_static
    PATHS ../build/lib
    NO_DEFAULT_PATH
)
find_path(SPT_LUA_INCLUDE_DIR lua.h PATHS ../src/Vm)

if(NOT SPT_LUA_LIBRARY OR NOT SPT_LUA_INCLUDE_DIR)
    message(FATAL_ERROR "SPT Lua library not found")
endif()

set(LUA_LIBRARY ${SPT_LUA_LIBRARY})
set(LUA_INCLUDE_DIR ${SPT_LUA_INCLUDE_DIR})

# Create sptxx library (header-only)
add_library(sptxx INTERFACE)
target_include_directories(sptxx INTERFACE 
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(sptxx INTERFACE ${LUA_LIBRARY})

# Add test executables
add_executable(sptxx_test test_basic.cpp)
target_include_directories(sptxx_test PRIVATE include)
target_link_libraries(sptxx_test PRIVATE sptxx)

add_executable(sptxx_test_simple test_simple.cpp)
target_include_directories(sptxx_test_simple PRIVATE include)
target_link_libraries(sptxx_test_simple PRIVATE sptxx)

add_executable(sptxx_test_debug test_debug.cpp)
target_include_directories(sptxx_test_debug PRIVATE include)
target_link_libraries(sptxx_test_debug PRIVATE sptxx)

add_executable(sptxx_test_debug2 test_debug2.cpp)
target_include_directories(sptxx_test_debug2 PRIVATE include)
target_link_libraries(sptxx_test_debug2 PRIVATE sptxx)

add_executable(sptxx_test_ref test_ref.cpp)
target_include_directories(sptxx_test_ref PRIVATE include)
target_link_libraries(sptxx_test_ref PRIVATE sptxx)

add_executable(sptxx_test_comprehensive test_comprehensive.cpp)
target_include_directories(sptxx_test_comprehensive PRIVATE include)
target_link_libraries(sptxx_test_comprehensive PRIVATE sptxx)

add_executable(sptxx_test_map_validation test_map_validation.cpp)
target_include_directories(sptxx_test_map_validation PRIVATE include)
target_link_libraries(sptxx_test_map_validation PRIVATE sptxx)

add_executable(sptxx_test_usertype test_usertype.cpp)
target_include_directories(sptxx_test_usertype PRIVATE include)
target_link_libraries(sptxx_test_usertype PRIVATE sptxx)

add_executable(sptxx_test_usertype_debug test_usertype_debug.cpp)
target_include_directories(sptxx_test_usertype_debug PRIVATE include)
target_link_libraries(sptxx_test_usertype_debug PRIVATE sptxx)

add_executable(sptxx_test_syntax test_syntax.cpp)
target_include_directories(sptxx_test_syntax PRIVATE include)
target_link_libraries(sptxx_test_syntax PRIVATE sptxx)

add_executable(sptxx_test_registry_ref test_registry_ref.cpp)
target_include_directories(sptxx_test_registry_ref PRIVATE include)
target_link_libraries(sptxx_test_registry_ref PRIVATE sptxx)

add_executable(sptxx_test_usertype_debug2 test_usertype_debug2.cpp)
target_include_directories(sptxx_test_usertype_debug2 PRIVATE include)
target_link_libraries(sptxx_test_usertype_debug2 PRIVATE sptxx)

add_executable(sptxx_test_usertype_trace test_usertype_trace.cpp)
target_include_directories(sptxx_test_usertype_trace PRIVATE include)
target_link_libraries(sptxx_test_usertype_trace PRIVATE sptxx)

add_executable(sptxx_test_usertype_minimal test_usertype_minimal.cpp)
target_include_directories(sptxx_test_usertype_minimal PRIVATE include)
target_link_libraries(sptxx_test_usertype_minimal PRIVATE sptxx)

add_executable(sptxx_test_usertype_debug3 test_usertype_debug3.cpp)
target_include_directories(sptxx_test_usertype_debug3 PRIVATE include)
target_link_libraries(sptxx_test_usertype_debug3 PRIVATE sptxx)

# Install rules
install(DIRECTORY include/ DESTINATION include)
install(TARGETS sptxx EXPORT sptxx-config)
install(EXPORT sptxx-config DESTINATION lib/cmake/sptxx)
