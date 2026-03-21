cmake_minimum_required (VERSION 3.15)
set(CMAKE_MACOSX_RPATH OFF)
project(spt LANGUAGES C CXX)
include(cmake/PickSource.cmake)
if(APPLE)
    find_library(COREFOUNDATION_LIBRARY CoreFoundation)
endif()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")

set (ANTLR_VERSION "4.13.1")
#set(BUILD_SHARED_LIBS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD  99)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=address  -fno-omit-frame-pointer")
#set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fsanitize=address  -fno-omit-frame-pointer")
#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -fsanitize=address")
option(BUILD_TESTS "Build tests" ON)          # 默认开启测试

# 设置输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# 覆盖所有配置的输出目录
foreach(config Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/bin)
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/lib)
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR}/lib)
endforeach()

if(MSVC)
    add_compile_options(/utf-8)
    #    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else ()
    set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
    set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -O3")
endif()

PickSourceFiles(
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        sourceFiles
        ${CMAKE_CURRENT_SOURCE_DIR}/src/sptscript.cpp
)
PickIncludeDirectories(${CMAKE_CURRENT_SOURCE_DIR}/src/
        includeDirs
)
add_subdirectory(runtime)

add_library(sptscript_library_static STATIC ${sourceFiles})
target_include_directories(sptscript_library_static PUBLIC ${includeDirs}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Ast/front
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/
)
target_link_libraries(sptscript_library_static PUBLIC antlr4_static)
add_library(sptscript_library_shared SHARED ${sourceFiles})

target_include_directories(sptscript_library_shared PUBLIC ${includeDirs}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Ast/front
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/
)
target_link_libraries(sptscript_library_shared PRIVATE antlr4_shared)
target_compile_definitions(sptscript_library_shared PUBLIC SPT_BUILD_AS_DLL)
target_compile_definitions(sptscript_library_shared PRIVATE SPT_EXPORTS)

add_executable(sptscript ${CMAKE_CURRENT_SOURCE_DIR}/src/sptscript.cpp)
target_link_libraries(sptscript PRIVATE sptscript_library_static)

if (BUILD_TESTS)
    enable_testing()

    # C API 测试
    add_executable(TestCApi tests/TestCApi.c)
    target_include_directories(TestCApi PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src/Vm
            ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src
    )
    target_link_libraries(TestCApi PRIVATE sptscript_library_static)

    add_test(NAME TestCApi
            COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/TestCApi
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR})

    message(STATUS "C API test enabled: TestCApi")

    # ====================================================================
    # 新增：集成 sptxx 的 C++ 包装层测试
    # ====================================================================
    # 自动搜索 sptxx 目录下所有的 test_*.cpp 文件
    file(GLOB SPTXX_TEST_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/sptxx/tests/test_*.cpp")

    foreach (TEST_SRC ${SPTXX_TEST_SOURCES})
        # 提取文件名作为目标名称 (例如 test_registry_ref)
        get_filename_component(TEST_NAME ${TEST_SRC} NAME_WE)

        # 加上 sptxx_ 前缀以避免与主项目重名冲突
        set(TARGET_NAME "sptxx_${TEST_NAME}")

        add_executable(${TARGET_NAME} ${TEST_SRC})

        # 包含 sptxx 自己的头文件，以及它依赖的 runtime 核心头文件
        target_include_directories(${TARGET_NAME} PRIVATE
                ${CMAKE_CURRENT_SOURCE_DIR}/sptxx/include
                ${CMAKE_CURRENT_SOURCE_DIR}/src/Vm
                ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src
        )

        # 强制链接刚刚编译出来的静态核心库！这样就能找到你新增的 lua_getref 了
        target_link_libraries(${TARGET_NAME} PRIVATE sptscript_library_static)

        # 注册到 CTest，可以通过 ctest 命令批量跑
        add_test(NAME ${TARGET_NAME}
                COMMAND ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${TARGET_NAME}
                WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    endforeach ()

    message(STATUS "sptxx tests integration enabled.")
endif ()