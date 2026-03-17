#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace lsp_test {

struct Test {
    std::string name;
    std::function<void()> func;
};

inline std::vector<Test>& getTests() {
    static std::vector<Test> tests;
    return tests;
}

inline int& getFailedCount() {
    static int count = 0;
    return count;
}

inline int& getPassedCount() {
    static int count = 0;
    return count;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> func) {
        getTests().push_back({name, func});
    }
};

#define TEST(name) \
    void test_##name(); \
    static lsp_test::TestRegistrar registrar_##name(#name, test_##name); \
    void test_##name()

#define ASSERT(cond) \
    do { \
        if (!(cond)) { \
            std::fprintf(stderr, "  FAILED: %s:%d: ASSERT(%s)\n", __FILE__, __LINE__, #cond); \
            lsp_test::getFailedCount()++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        if (!((a) == (b))) { \
            std::fprintf(stderr, "  FAILED: %s:%d: ASSERT_EQ(%s, %s)\n", __FILE__, __LINE__, #a, #b); \
            std::fprintf(stderr, "    lhs = %s\n", std::to_string(a).c_str()); \
            std::fprintf(stderr, "    rhs = %s\n", std::to_string(b).c_str()); \
            lsp_test::getFailedCount()++; \
            return; \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        if ((a) == (b)) { \
            std::fprintf(stderr, "  FAILED: %s:%d: ASSERT_NE(%s, %s)\n", __FILE__, __LINE__, #a, #b); \
            lsp_test::getFailedCount()++; \
            return; \
        } \
    } while(0)

#define ASSERT_TRUE(x) ASSERT(x)
#define ASSERT_FALSE(x) ASSERT(!(x))
#define ASSERT_NULL(x) ASSERT((x) == nullptr)
#define ASSERT_NOT_NULL(x) ASSERT((x) != nullptr)

#define ASSERT_NO_ERRORS(file) \
    do { \
        const auto& diags = (file).getDiagnostics(); \
        if (!diags.empty()) { \
            std::fprintf(stderr, "  FAILED: %s:%d: ASSERT_NO_ERRORS - found %zu errors:\n", \
                         __FILE__, __LINE__, diags.size()); \
            for (const auto& d : diags) { \
                std::fprintf(stderr, "    Line %u: %s\n", d.range.start.line, d.message.c_str()); \
            } \
            lsp_test::getFailedCount()++; \
            return; \
        } \
    } while(0)

#define ASSERT_HAS_ERRORS(file) \
    do { \
        if ((file).getDiagnostics().empty()) { \
            std::fprintf(stderr, "  FAILED: %s:%d: ASSERT_HAS_ERRORS - no errors found\n", __FILE__, __LINE__); \
            lsp_test::getFailedCount()++; \
            return; \
        } \
    } while(0)

inline int runAllTests() {
    std::printf("Running %zu tests...\n\n", getTests().size());
    
    for (const auto& test : getTests()) {
        int prevFailed = getFailedCount();
        std::printf("[TEST] %s\n", test.name.c_str());
        test.func();
        
        if (getFailedCount() == prevFailed) {
            getPassedCount()++;
            std::printf("  PASSED\n\n");
        } else {
            std::printf("  FAILED\n\n");
        }
    }
    
    std::printf("========================================\n");
    std::printf("Results: %d passed, %d failed\n", getPassedCount(), getFailedCount());
    std::printf("========================================\n");
    
    return getFailedCount() > 0 ? 1 : 0;
}

}
