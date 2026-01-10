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

option(BUILD_TESTS "Build tests" ON)          # 默认开启测试

# 设置可执行文件输出目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 设置库文件输出目录
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

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
        ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
        ${CMAKE_CURRENT_SOURCE_DIR}/src/TestRunner.cpp
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

add_executable(sptscript_test_old ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp)
target_link_libraries(sptscript_test_old PRIVATE sptscript_library_static)

if (BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif ()