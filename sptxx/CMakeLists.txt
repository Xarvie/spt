# =====================================================================
# sptxx — SPT Lua 5.5 C++17 绑定库（header-only）+ 测试
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# 依赖：spt-lang（../spt-lang）作为子目录引入，提供 spt_core 静态库
#       及其 PUBLIC include 目录（spt-lang/src/，含 lua.h/lauxlib.h/lualib.h）。
# =====================================================================
cmake_minimum_required(VERSION 3.20)
project(sptxx LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 输出目录统一
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
foreach(config Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/bin)
endforeach()

option(BUILD_TESTS "Build sptxx tests" ON)

# ----------------------------------------------------------------------
# 引入 spt-lang（提供 spt_core 目标，PUBLIC 暴露 src/ include 目录）
# ----------------------------------------------------------------------
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../spt-lang
                 ${CMAKE_CURRENT_BINARY_DIR}/spt-lang)

# sptxx 本身是 header-only 库；这里定义一个 INTERFACE 目标方便外部链接。
add_library(sptxx INTERFACE)
target_link_libraries(sptxx INTERFACE spt_core)
target_include_directories(sptxx INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# ----------------------------------------------------------------------
# 测试
# ----------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()

    file(GLOB SPTXX_TEST_SOURCES
         "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")

    foreach(TEST_SRC ${SPTXX_TEST_SOURCES})
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)
        set(TARGET_NAME "sptxx_${TEST_NAME}")
        add_executable(${TARGET_NAME} ${TEST_SRC})
        target_link_libraries(${TARGET_NAME} PRIVATE sptxx)
        # spt_core 的 PUBLIC include 已含 spt-lang/src/，故 #include <lua.h> 可用
        if(MSVC)
            target_compile_definitions(${TARGET_NAME} PRIVATE
                _CRT_SECURE_NO_WARNINGS
                _WINSOCK_DEPRECATED_NO_WARNINGS
            )
            target_compile_options(${TARGET_NAME} PRIVATE /W3 /permissive-)
        else()
            target_compile_options(${TARGET_NAME} PRIVATE -Wall -Wextra)
        endif()
        add_test(NAME ${TARGET_NAME}
            COMMAND $<TARGET_FILE:${TARGET_NAME}>
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    endforeach()

    message(STATUS "sptxx tests: ${SPTXX_TEST_SOURCES}")
endif()
