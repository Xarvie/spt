# 添加可执行文件
add_executable(tests_all
               catch_amalgamated.cpp
               test_all.cpp
)

# 链接主库
target_link_libraries(tests_all PRIVATE sptscript_library_static)

# 设置包含目录
target_include_directories(tests_all PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# 为测试添加编译器选项
target_compile_options(tests_all PRIVATE
                       $<$<CXX_COMPILER_ID:MSVC>:/W4>
                       $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

# 设置测试属性
set_target_properties(tests_all PROPERTIES
                      CXX_STANDARD 17
                      CXX_STANDARD_REQUIRED ON
                      CXX_EXTENSIONS OFF
)

# 基础测试配置
#add_test(NAME AllUnitTests
#         COMMAND tests_all
#         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#)

## 按标签分组测试
add_test(NAME OldTests
         COMMAND tests_all "[old]"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 基准测试需要不同的编译选项
add_executable(test_benchmarks
               catch_amalgamated.cpp
               test_benchmark.cpp
)

target_link_libraries(test_benchmarks PRIVATE sptscript_library_static)
target_include_directories(test_benchmarks PRIVATE
                           ${CMAKE_CURRENT_SOURCE_DIR}/../src
)

# 基准测试需要优化
target_compile_options(test_benchmarks PRIVATE
                       $<$<CONFIG:Debug>:-O1>
                       $<$<CONFIG:Release>:-O2>
)

add_test(NAME BenchmarkTests
         COMMAND test_benchmarks "[benchmark]"
         WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# 设置基准测试属性
set_tests_properties(BenchmarkTests PROPERTIES
                     LABELS "benchmark"
                     TIMEOUT 300  # 基准测试可能需要更长时间
                     COST 2.0     # 较高成本，影响并行调度
)

# 为测试设置超时
set_tests_properties(OldTests
                     PROPERTIES
                     TIMEOUT 60
                     LABELS "unit"
                     PASS_REGULAR_EXPRESSION "All tests passed"
)

# 可以添加自定义的测试目标
add_custom_target(run-tests
                  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
                  DEPENDS tests_all
                  COMMENT "Running all tests"
)

add_dependencies(run-tests test_benchmarks)

# 配置测试报告的生成
option(GENERATE_TEST_REPORTS "Generate test reports in XML format" ON)
if (GENERATE_TEST_REPORTS)
    add_test(NAME GenerateTestReport
             COMMAND tests_all -r junit -o test_report.xml
             WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )

    # 将测试报告添加到清理列表
    set_property(DIRECTORY APPEND PROPERTY
                 ADDITIONAL_CLEAN_FILES test_report.xml
    )
endif ()