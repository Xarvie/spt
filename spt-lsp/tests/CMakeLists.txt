cmake_minimum_required(VERSION 3.15)

set(GENERATED_DIR ${PROJECT_SOURCE_DIR}/generated)

set(ANTLR_GENERATED_SOURCES
    ${GENERATED_DIR}/LangLexer.cpp
    ${GENERATED_DIR}/LangParser.cpp
    ${GENERATED_DIR}/LangParserBaseVisitor.cpp
    ${GENERATED_DIR}/LangParserVisitor.cpp
)

set(LSP_CORE_SOURCES
    ${PROJECT_SOURCE_DIR}/src/LspService.cpp
)

set(LSP_TEST_SOURCES
    test_main.cpp
    test_parser.cpp
    test_ast_builder.cpp
    test_behavior_chain.cpp
    ${ANTLR_GENERATED_SOURCES}
    ${LSP_CORE_SOURCES}
)

add_executable(lsp_tests ${LSP_TEST_SOURCES})

add_executable(lsp_tests2 test_behavior_chain2.cpp ${ANTLR_GENERATED_SOURCES} ${LSP_CORE_SOURCES})
target_include_directories(lsp_tests2 PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/generated
        ${PROJECT_SOURCE_DIR}/runtime/src
        ${PROJECT_SOURCE_DIR}/external
        ${PROJECT_SOURCE_DIR}/external/nlohmann
)
target_link_libraries(lsp_tests2 PRIVATE antlr4_static)
if (NOT MSVC)
    target_link_options(lsp_tests2 PRIVATE "-static-libgcc" "-static-libstdc++")
endif ()

target_include_directories(lsp_tests PRIVATE 
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/generated
    ${PROJECT_SOURCE_DIR}/runtime/src
    ${PROJECT_SOURCE_DIR}/external
    ${PROJECT_SOURCE_DIR}/external/nlohmann
)

target_link_libraries(lsp_tests PRIVATE antlr4_static)

if(NOT MSVC)
    target_link_options(lsp_tests PRIVATE "-static-libgcc" "-static-libstdc++")
endif()

enable_testing()
add_test(NAME lsp_tests COMMAND lsp_tests)
