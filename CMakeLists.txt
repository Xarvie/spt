cmake_minimum_required (VERSION 3.15)
set(CMAKE_MACOSX_RPATH OFF)
project(sptscript LANGUAGES C CXX)
include(cmake/PickSource.cmake)
if(APPLE)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
endif()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -Os")
set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -Os")
set (ANTLR_VERSION "4.13.1")
#set(BUILD_SHARED_LIBS OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD  99)

#list(APPEND CMAKE_PREFIX_PATH "C:/dev/static_sdk") # 发布时需要
link_directories("C:/dev/static_sdk/lib")
if(MSVC)
    add_compile_options(/utf-8)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

#set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
PickSourceFiles(
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        sourceFiles
)
PickIncludeDirectories(${CMAKE_CURRENT_SOURCE_DIR}/src/
        includeDirs
        )
add_subdirectory(runtime)
add_executable(sptscript ${sourceFiles}
)
target_include_directories(sptscript PUBLIC ${includeDirs}
        ${CMAKE_CURRENT_SOURCE_DIR}/src/Ast/front
        ${CMAKE_CURRENT_SOURCE_DIR}/runtime/src/
)

target_link_libraries(sptscript PUBLIC antlr4_static)
