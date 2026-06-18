# =====================================================================
# spt-lsp/server — SPT 语言服务器（纯 C）
#
# 独立构建（无需整套 VM；直接复用前端源码作为唯一解析真相）：
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
#   ctest --test-dir build --output-on-failure
# 产物： build/bin/sptscript-lsp(.exe)
#
# 作为父项目子目录： add_subdirectory(spt-lsp/server)
#
# 分层：
#   third_party/cjson   vendored cJSON 1.7.19 (MIT)
#   ../../src/frontend  复用的前端（词法/语法/AST/arena/诊断 + LSP 桥）
#   src/rpc             Content-Length 分帧 + JSON-RPC 2.0
#   src/lsp             生命周期 + 分派 + 文档存储 + 主循环
#   src/analysis        语义模型（符号/解析/引用）+ 工作区索引
#   src/features        各 LSP 功能 provider
# =====================================================================
cmake_minimum_required(VERSION 3.16)
project(spt_lsp_server LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
# MSVC 多配置生成器会在 RUNTIME_OUTPUT_DIRECTORY 下再建 Debug/Release 子目录，
# 强制所有配置输出到同一 bin/，与 VSCode 客户端默认路径 ../server/build/bin/ 对齐。
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_BINARY_DIR}/bin)

set(FRONTEND_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../src/frontend)

# ---- vendored cJSON ----
add_library(spt_cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(spt_cjson PUBLIC third_party/cjson)
if(NOT MSVC)
  target_compile_options(spt_cjson PRIVATE -w)
endif()

# ---- 复用的前端（纯 C，不含 VM/codegen） ----
add_library(spt_lsp_frontend STATIC
  ${FRONTEND_DIR}/spt_arena.c
  ${FRONTEND_DIR}/spt_ast.c
  ${FRONTEND_DIR}/spt_diag.c
  ${FRONTEND_DIR}/spt_lexer.c
  ${FRONTEND_DIR}/spt_parser.c
  ${FRONTEND_DIR}/spt_lsp_bridge.c
)
target_include_directories(spt_lsp_frontend PUBLIC ${FRONTEND_DIR})
if(NOT MSVC)
  target_compile_options(spt_lsp_frontend PRIVATE -w)
else()
  target_compile_definitions(spt_lsp_frontend PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# ---- 服务器核心库 ----
add_library(spt_lsp_core STATIC
  src/rpc/spt_rpc.c
  src/lsp/server.c
  src/lsp/trace.c
  src/lsp/protocol.c
  src/lsp/documents.c
  src/analysis/semantic.c
  src/analysis/workspace.c
  src/features/diagnostics.c
  src/features/symbols.c
  src/features/semantic_tokens.c
  src/features/hover.c
  src/features/definition.c
  src/features/completion.c
  src/features/references.c
  src/features/rename.c
  src/features/signature.c
  src/features/format.c
)
target_include_directories(spt_lsp_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/src/rpc
  ${CMAKE_CURRENT_SOURCE_DIR}/src/lsp
  ${CMAKE_CURRENT_SOURCE_DIR}/src/analysis
  ${CMAKE_CURRENT_SOURCE_DIR}/src/features
)
target_link_libraries(spt_lsp_core PUBLIC spt_cjson spt_lsp_frontend)

if(MSVC)
  target_compile_definitions(spt_lsp_core PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(spt_lsp_core PRIVATE /W3)
else()
  target_compile_options(spt_lsp_core PRIVATE -Wall -Wextra)
endif()

# ---- 可执行文件（名称与 VSCode 客户端默认一致：sptscript-lsp） ----
add_executable(sptscript-lsp src/main.c)
target_link_libraries(sptscript-lsp PRIVATE spt_lsp_core)

# ---- 测试 ----
option(SPT_LSP_BUILD_TESTS "Build spt-lsp server tests" ON)
if(SPT_LSP_BUILD_TESTS)
  enable_testing()
  set(SPT_LSP_TESTS test_rpc test_server test_documents test_diagnostics test_features test_crash_semtok test_incomplete)
  # test_workspace 用到 POSIX mkdtemp/mkdir（MSVC/MinGW 无），仅在非 Windows 构建。
  if(NOT WIN32)
    list(APPEND SPT_LSP_TESTS test_workspace)
  endif()
  foreach(t ${SPT_LSP_TESTS})
    add_executable(${t} test/${t}.c)
    target_link_libraries(${t} PRIVATE spt_lsp_core)
    if(NOT MSVC)
      target_compile_definitions(${t} PRIVATE _DEFAULT_SOURCE=1 _XOPEN_SOURCE=700)
    endif()
    add_test(NAME ${t} COMMAND ${t})
  endforeach()
endif()
