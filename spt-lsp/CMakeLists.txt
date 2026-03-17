cmake_minimum_required(VERSION 3.15)
project(sptScriptLsp LANGUAGES CXX C)
# --- 设置 C 标准 ---
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED True)
set(CMAKE_C_EXTENSIONS OFF)

# --- 设置 C++ 标准 ---
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_CXX_EXTENSIONS OFF)
if(MSVC)
    add_compile_options("/source-charset:utf-8")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
foreach (config Debug Release MinSizeRel RelWithDebInfo)
    string(TOUPPER ${config} config_upper)
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR})
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR})
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${config_upper} ${CMAKE_BINARY_DIR})
endforeach ()
#if (MINGW)
#    add_compile_options(-fexec-charset=GBK -finput-charset=UTF-8)
#endif ()
include_directories(${PROJECT_SOURCE_DIR}/external)
include_directories(${PROJECT_SOURCE_DIR}/external/nlohmann)
include_directories(${PROJECT_SOURCE_DIR}/include)


add_subdirectory(runtime)

set(ANTLR_GENERATED_DIR ${PROJECT_SOURCE_DIR}/generated)

set(LSP_SERVER_SOURCES
    ${ANTLR_GENERATED_DIR}/LangLexer.cpp
    ${ANTLR_GENERATED_DIR}/LangParser.cpp
    ${ANTLR_GENERATED_DIR}/LangParserBaseVisitor.cpp
    src/main.cpp
    src/LspService.cpp
)
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libstdc++")
# --- 定义可执行目标 ---
add_executable(sptscript-lsp ${LSP_SERVER_SOURCES})
target_include_directories(sptscript-lsp PRIVATE ${PROJECT_SOURCE_DIR}/generated)
target_include_directories(sptscript-lsp PRIVATE ${PROJECT_SOURCE_DIR}/runtime/src)

# --- 链接依赖库 ---
target_link_libraries(sptscript-lsp PRIVATE antlr4_static)
if(NOT MSVC)
target_link_options(sptscript-lsp PRIVATE "-static-libgcc" "-static-libstdc++" "-static")
endif()

option(BUILD_TESTS "Build tests" ON)
if(BUILD_TESTS)
    add_subdirectory(tests)
endif()