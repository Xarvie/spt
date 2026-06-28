# =====================================================================
# SPT 工具链构建（纯 C 前端 + 魔改 Lua VM，无 ANTLR / 无 C++ 前端）
#
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build --output-on-failure
#
# 目录结构（Lua 风格平铺）：
#   src/           所有 .c / .h（VM + 前端 + codegen + CLI + JIT + 门面头 spt.h）
#   tests/         C API 测试
#   test/          .spt 集成测试
#   LangLexer.g4 / LangParser.g4  语法标准（仅作文档，不参与构建）
#
# 嵌入用法（C/C++）：
#   cmake -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=ON ..
#   cmake --build . --target install
#   # 外部工程：
#   #   find_package(spt REQUIRED)
#   #   target_link_libraries(my_app PRIVATE spt::core)
# =====================================================================
cmake_minimum_required(VERSION 3.20)
project(spt LANGUAGES C CXX)

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

option(BUILD_TESTS "Build tests" ON)
option(BUILD_SHARED_LIBS "Build SPT as a shared library (DLL/SO)" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(ENABLE_COVERAGE "Enable code coverage instrumentation (clang)" OFF)

# 安装前缀默认值（用户可覆盖）
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "Install prefix" FORCE)
endif()

include(GNUInstallDirs)

# ----------------------------------------------------------------------
# 辅助函数：给目标应用 sanitize / coverage 插桩选项
# 取代原先在 4+ 处重复的 target_compile_options / target_link_options。
# ----------------------------------------------------------------------
function(spt_apply_instrumentation target)
    if(NOT TARGET ${target})
        return()
    endif()

    # ---- Sanitize ----
    if(ENABLE_ASAN OR ENABLE_UBSAN)
        set(san_list "")
        if(ENABLE_ASAN)
            list(APPEND san_list "address")
        endif()
        if(ENABLE_UBSAN)
            list(APPEND san_list "undefined")
        endif()
        list(JOIN san_list "," san_flag_str)
        set(san_flag "-fsanitize=${san_flag_str}")

        if(MSVC OR (CMAKE_C_SIMULATE_ID STREQUAL "MSVC"))
            # MSVC / clang-cl: 仅支持 address sanitizer
            if(ENABLE_ASAN)
                # 查找 clang ASan 运行时库（仅计算一次，缓存到父作用域变量）
                if(NOT DEFINED _SPT_ASAN_LIB)
                    if(DEFINED ENV{LLVM_INSTALL_DIR})
                        set(_clang_lib_dir "$ENV{LLVM_INSTALL_DIR}/lib/clang")
                    else()
                        set(_clang_lib_dir "C:/env/LLVM/lib/clang")
                    endif()
                    file(GLOB _clang_versions LIST_DIRECTORIES true "${_clang_lib_dir}/*")
                    list(SORT _clang_versions ORDER DESCENDING)
                    list(GET _clang_versions 0 _clang_version_dir)
                    set(_SPT_ASAN_LIB "${_clang_version_dir}/lib/windows/clang_rt.asan_dynamic-x86_64.lib"
                        CACHE INTERNAL "clang ASan runtime lib")
                    set(_SPT_ASAN_THUNK "${_clang_version_dir}/lib/windows/clang_rt.asan_dynamic_runtime_thunk-x86_64.lib"
                        CACHE INTERNAL "clang ASan thunk lib")
                    if(NOT EXISTS ${_SPT_ASAN_LIB})
                        message(FATAL_ERROR "ASan library not found: ${_SPT_ASAN_LIB}")
                    endif()
                    message(STATUS "ASan library: ${_SPT_ASAN_LIB}")
                endif()

                target_compile_options(${target} PRIVATE /fsanitize=address)
                target_link_libraries(${target} PRIVATE ${_SPT_ASAN_LIB} ${_SPT_ASAN_THUNK})
            endif()
        else()
            # Clang/GCC: 完整 ASan+UBSan 支持
            target_compile_options(${target} PRIVATE ${san_flag} -fno-omit-frame-pointer -g)
            target_link_options(${target} PRIVATE ${san_flag})
        endif()
    endif()

    # ---- Coverage ----
    if(ENABLE_COVERAGE)
        target_compile_options(${target} PRIVATE -fprofile-instr-generate -fcoverage-mapping -g)
        target_link_options(${target} PRIVATE -fprofile-instr-generate)
    endif()
endfunction()

# ----------------------------------------------------------------------
# 核心库：所有 src/*.c（除 spt_script.c 入口外）
# 类型由 BUILD_SHARED_LIBS 控制（OFF=STATIC, ON=SHARED）
# spt_module.c 在库里而非 sptscript exe，因为：
#   1) 它用 LUALIB_API 标记，DLL 模式下需要 LUA_LIB 才能 dllexport
#   2) 模块加载器是嵌入用户也要用的库功能，不是 CLI 专属
# ----------------------------------------------------------------------
file(GLOB CORE_SRCS CONFIGURE_DEPENDS src/*.c)
list(REMOVE_ITEM CORE_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/src/spt_script.c)

add_library(spt_core ${CORE_SRCS})
# alias 用 spt::spt_core，与 install(EXPORT NAMESPACE spt::) 产生的名字一致
add_library(spt::spt_core ALIAS spt_core)

target_include_directories(spt_core
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/spt>
)

# SHARED 构建时：位置无关代码 + LUA_BUILD_AS_DLL 透传
if(BUILD_SHARED_LIBS)
    set_target_properties(spt_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # LUA_BUILD_AS_DLL: 对消费者公开（dllimport），LUA_CORE: 仅构建时私有（dllexport）
    target_compile_definitions(spt_core
        PUBLIC  LUA_BUILD_AS_DLL
        PRIVATE LUA_CORE LUA_LIB
    )
endif()

if(MSVC)
    target_compile_definitions(spt_core PRIVATE
        _CRT_SECURE_NO_WARNINGS
        _WINSOCK_DEPRECATED_NO_WARNINGS
    )
    target_compile_options(spt_core PRIVATE /W3)
else()
    target_compile_options(spt_core PRIVATE -Wall -Wextra)
endif()

if(UNIX)
    target_link_libraries(spt_core PUBLIC m)
endif()

if(WIN32)
    target_link_libraries(spt_core PUBLIC kernel32)
endif()

spt_apply_instrumentation(spt_core)

# ----------------------------------------------------------------------
# 解释器可执行文件（只编译入口，模块加载器已在 spt_core 里）
# ----------------------------------------------------------------------
add_executable(sptscript src/spt_script.c)
target_link_libraries(sptscript PRIVATE spt_core)

if(MSVC)
    target_compile_definitions(sptscript PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(sptscript PRIVATE /W3)
else()
    target_compile_options(sptscript PRIVATE -Wall -Wextra)
endif()

spt_apply_instrumentation(sptscript)

# ----------------------------------------------------------------------
# 测试
# ----------------------------------------------------------------------
if(BUILD_TESTS)
    enable_testing()

    # ---- .spt 集成测试：遍历 test/ 下所有 .spt ----
    file(GLOB_RECURSE SPT_TESTS CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} test/*.spt)
    foreach(t ${SPT_TESTS})
        get_filename_component(file ${t} NAME)
        get_filename_component(dir ${t} DIRECTORY)
        add_test(
            NAME ${t}
            COMMAND $<TARGET_FILE:sptscript> ${file}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/${dir}
        )
    endforeach()

    # ---- C API 测试 ----
    add_executable(TestCApi tests/TestCApi.c)
    target_link_libraries(TestCApi PRIVATE spt_core)
    add_test(NAME TestCApi
        COMMAND $<TARGET_FILE:TestCApi>
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    spt_apply_instrumentation(TestCApi)

    # ---- 声明 (declare) AST 契约测试 ----
    add_executable(TestDeclare tests/TestDeclare.c)
    target_link_libraries(TestDeclare PRIVATE spt_core)
    add_test(NAME TestDeclare
        COMMAND $<TARGET_FILE:TestDeclare>
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    spt_apply_instrumentation(TestDeclare)

    # ---- iter() 内置函数测试 ----
    add_executable(TestIter tests/TestIter.c)
    target_link_libraries(TestIter PRIVATE spt_core)
    add_test(NAME TestIter
        COMMAND $<TARGET_FILE:TestIter>
        WORKING_DIRECTORY ${CMAKE_BINARY_DIR})
    spt_apply_instrumentation(TestIter)

    message(STATUS "Tests enabled: ${SPT_TESTS} spt tests + TestCApi + TestDeclare + TestIter")
endif()

# ----------------------------------------------------------------------
# 状态汇总
# ----------------------------------------------------------------------
if(ENABLE_ASAN OR ENABLE_UBSAN)
    set(san_list "")
    if(ENABLE_ASAN)
        list(APPEND san_list "address")
    endif()
    if(ENABLE_UBSAN)
        list(APPEND san_list "undefined")
    endif()
    list(JOIN san_list "," san_flag_str)
    message(STATUS "Sanitize enabled: ${san_flag_str}")
endif()
if(ENABLE_COVERAGE)
    message(STATUS "Coverage instrumentation enabled (clang)")
endif()
if(BUILD_SHARED_LIBS)
    message(STATUS "Building spt_core as SHARED library (DLL/SO)")
else()
    message(STATUS "Building spt_core as STATIC library")
endif()

# ----------------------------------------------------------------------
# 安装规则：让外部 CMake 工程能 find_package(spt)
# ----------------------------------------------------------------------
install(TARGETS spt_core sptscript
    EXPORT sptTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spt
)

# 安装公共头文件（门面头 spt.h + 所有 src/*.h）
install(DIRECTORY src/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/spt FILES_MATCHING PATTERN "*.h")

# 生成并安装 CMake 包配置文件
install(EXPORT sptTargets
    FILE sptTargets.cmake
    NAMESPACE spt::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spt
)

include(CMakePackageConfigHelpers)
configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sptConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/sptConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spt
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/sptConfigVersion.cmake
    VERSION 0.1.0
    COMPATIBILITY SameMajorVersion
)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/sptConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/sptConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/spt
)
