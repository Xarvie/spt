cmake_minimum_required (VERSION 3.15)
set(CMAKE_MACOSX_RPATH OFF)
project(spt LANGUAGES C CXX)
include(cmake/PickSource.cmake)
if(APPLE)
  find_library(COREFOUNDATION_LIBRARY CoreFoundation)
endif()
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS}")
set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS} -O3")
set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS} -O3")
set (ANTLR_VERSION "4.13.1")
#set(BUILD_SHARED_LIBS OFF)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_C_STANDARD  99)

if(MSVC)
    add_compile_options(/utf-8)
#    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

endif()

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
#if(MSVC)
#    target_compile_options(sptscript PRIVATE /O2 /Zi)
#    target_link_options(sptscript PRIVATE /DEBUG)
#    SET_TARGET_PROPERTIES(sptscript PROPERTIES LINK_FLAGS "/PROFILE")
#endif()