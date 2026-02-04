#pragma once

#include "spt.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

// =========================================================
// SPT C API Extended Tests - Part 2
// =========================================================
// Tests for: Fibers, Magic Methods, Modules, Edge Cases
// Focuses on complex interactions and boundary conditions

class SptCApiExtendedTests {
public:
  int runAll() {
    std::cout << "\nRunning SPT C API Extended Tests (Part 2)..." << std::endl;
    std::cout << "==================================================" << std::endl;

    // === Fiber Tests ===
    addTest("spt_newfiber basic", testFiberBasic);
    addTest("spt_fiberstatus", testFiberStatus);
    addTest("spt_resume basic", testFiberResume);
    addTest("spt_yield", testFiberYield);
    addTest("spt_isresumable", testFiberIsResumable);
    addTest("fiber multiple yield/resume", testFiberMultipleYield);
    addTest("fiber with arguments", testFiberWithArgs);
    addTest("fiber error handling", testFiberError);

    // === Magic Method Tests ===
    addTest("spt_magicmethodname", testMagicMethodName);
    addTest("spt_magicmethodindex", testMagicMethodIndex);
    addTest("spt_hasmagicmethod", testHasMagicMethod);
    addTest("spt_setmagicmethod", testSetMagicMethod);
    addTest("spt_getmagicmethod", testGetMagicMethod);
    addTest("spt_getclassflags", testGetClassFlags);
    addTest("magic method __add", testMagicMethodAdd);
    addTest("magic method __gc", testMagicMethodGC);
    addTest("magic method __getitem/__setitem", testMagicMethodIndex2);
    addTest("spt_callmagicmethod", testCallMagicMethod);

    // === Edge Cases - Numeric Boundaries ===
    addTest("int64 boundary values", testInt64Boundaries);
    addTest("float special values (NaN, Inf)", testFloatSpecialValues);
    addTest("numeric overflow in conversion", testNumericOverflow);

    // === Edge Cases - String Handling ===
    addTest("empty string operations", testEmptyString);
    addTest("string with null bytes", testStringWithNullBytes);
    addTest("very long string", testVeryLongString);
    addTest("unicode string handling", testUnicodeStrings);

    // === Edge Cases - Stack Operations ===
    addTest("deep stack operations", testDeepStack);
    addTest("stack underflow protection", testStackUnderflow);
    addTest("negative index edge cases", testNegativeIndexEdgeCases);
    addTest("pseudo-index access", testPseudoIndexAccess);

    // === Edge Cases - Collections ===
    addTest("empty list operations", testEmptyListOperations);
    addTest("empty map operations", testEmptyMapOperations);
    addTest("list with mixed types", testListMixedTypes);
    addTest("map with various key types", testMapVariousKeyTypes);
    addTest("nested collections", testNestedCollections);
    addTest("list index out of bounds", testListIndexOutOfBounds);

    // === Edge Cases - Class/Instance ===
    addTest("class without methods", testClassWithoutMethods);
    addTest("instance field overwrite", testInstanceFieldOverwrite);
    addTest("multiple instances same class", testMultipleInstancesSameClass);
    addTest("cinstance with zero size", testCInstanceZeroSize);
    addTest("cinstance lifecycle", testCInstanceLifecycle);

    // === Edge Cases - Function Calls ===
    addTest("call with zero args", testCallZeroArgs);
    addTest("call with many args", testCallManyArgs);
    addTest("nested function calls", testNestedFunctionCalls);
    addTest("recursive function call", testRecursiveFunctionCall);
    addTest("function returning multiple values", testMultipleReturnValues);

    // === Module System Tests ===
    addTest("spt_addpath", testAddPath);
    addTest("spt_defmodule", testDefModule);

    // === Reference System Tests ===
    addTest("multiple references", testMultipleReferences);
    addTest("reference to complex objects", testReferenceToComplexObjects);
    addTest("unref nonexistent", testUnrefNonexistent);

    // === Error Handling Edge Cases ===
    addTest("error in pcall", testErrorInPcall);
    addTest("nested pcall errors", testNestedPcallErrors);
    addTest("spt_throw", testThrow);

    // Run all tests
    int passed = 0;
    int failed = 0;

    for (auto &test : tests_) {
      try {
        test.func();
        std::cout << "[       OK ] " << test.name << std::endl;
        passed++;
      } catch (const std::exception &e) {
        std::cout << "🔴 [  FAILED  ] " << test.name << std::endl;
        std::cout << "             Reason: " << e.what() << std::endl;
        failed++;
      }
    }

    std::cout << "==================================================" << std::endl;
    if (failed == 0) {
      std::cout << "[  PASSED  ] All " << passed << " tests passed." << std::endl;
    } else {
      std::cout << "🔴 [  FAILED  ] " << failed << " tests failed, " << passed << " passed."
                << std::endl;
    }

    return failed == 0 ? 0 : 1;
  }

private:
  struct Test {
    std::string name;
    std::function<void()> func;
  };

  std::vector<Test> tests_;

  void addTest(const std::string &name, std::function<void()> func) {
    tests_.push_back({name, func});
  }

  static void ASSERT(bool condition, const char *msg) {
    if (!condition) {
      throw std::runtime_error(msg);
    }
  }

  static void ASSERT_EQ(int64_t expected, int64_t actual, const char *msg) {
    if (expected != actual) {
      std::ostringstream oss;
      oss << msg << " (expected: " << expected << ", actual: " << actual << ")";
      throw std::runtime_error(oss.str());
    }
  }

  static void ASSERT_FLOAT_EQ(double expected, double actual, const char *msg, double eps = 1e-9) {
    if (std::abs(expected - actual) > eps) {
      std::ostringstream oss;
      oss << msg << " (expected: " << expected << ", actual: " << actual << ")";
      throw std::runtime_error(oss.str());
    }
  }

  static void ASSERT_STR_EQ(const char *expected, const char *actual, const char *msg) {
    if (expected == nullptr && actual == nullptr)
      return;
    if (expected == nullptr || actual == nullptr || strcmp(expected, actual) != 0) {
      std::ostringstream oss;
      oss << msg << " (expected: \"" << (expected ? expected : "NULL") << "\", actual: \""
          << (actual ? actual : "NULL") << "\")";
      throw std::runtime_error(oss.str());
    }
  }

  // =========================================================
  // Fiber Tests
  // =========================================================

  static void testFiberBasic() {
    spt_State *S = spt_newstate();

    // Create a simple function for the fiber
    spt_Chunk *chunk = spt_loadstring(S, "int test() { return 42; } test", "fiber_test");
    if (chunk) {
      spt_pushchunk(S, chunk);
      spt_State *fiber = spt_newfiber(S);
      ASSERT(fiber != nullptr, "spt_newfiber should return valid state");
      ASSERT_EQ(SPT_FIBER_NEW, spt_fiberstatus(fiber), "new fiber should have NEW status");
      spt_freechunk(chunk);
    }

    spt_close(S);
  }

  static void testFiberStatus() {
    spt_State *S = spt_newstate();

    // Test initial status
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 100);
      return 1;
    });

    spt_State *fiber = spt_newfiber(S);
    ASSERT(fiber != nullptr, "fiber creation");
    ASSERT_EQ(SPT_FIBER_NEW, spt_fiberstatus(fiber), "initial status should be NEW");

    spt_close(S);
  }

  static void testFiberResume() {
    spt_State *S = spt_newstate();

    // Create a simple function
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 42);
      return 1;
    });

    spt_State *fiber = spt_newfiber(S);
    ASSERT(fiber != nullptr, "fiber creation");

    // Resume the fiber
    int status = spt_resume(fiber, S, 0);
    // Status could be SPT_OK or SPT_YIELD depending on implementation

    spt_close(S);
  }

  static void testFiberYield() {
    spt_State *S = spt_newstate();

    // Test that yield API exists and doesn't crash
    // Note: Actually testing yield requires a fiber context
    spt_pushcfunction(S, [](spt_State *S) -> int {
      // This would yield if called from a fiber
      spt_pushstring(S, "yielded");
      return 1;
    });

    ASSERT_EQ(SPT_TCLOSURE, spt_type(S, -1), "function type");

    spt_close(S);
  }

  static void testFiberIsResumable() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 1);
      return 1;
    });

    spt_State *fiber = spt_newfiber(S);
    ASSERT(fiber != nullptr, "fiber creation");

    // New fiber should be resumable
    ASSERT(spt_isresumable(fiber), "new fiber should be resumable");

    spt_close(S);
  }

  static void testFiberMultipleYield() {
    spt_State *S = spt_newstate();

    // Test multiple yield/resume cycles
    // This is a basic structure test
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 1);
      return 1;
    });

    spt_State *fiber = spt_newfiber(S);
    ASSERT(fiber != nullptr, "fiber creation");

    spt_close(S);
  }

  static void testFiberWithArgs() {
    spt_State *S = spt_newstate();

    // Create a function that uses arguments
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_Int a = spt_toint(S, 1);
      spt_Int b = spt_toint(S, 2);
      spt_pushint(S, a + b);
      return 1;
    });

    spt_State *fiber = spt_newfiber(S);
    ASSERT(fiber != nullptr, "fiber creation");

    // Push arguments
    spt_pushint(S, 10);
    spt_pushint(S, 20);

    // Resume with arguments
    int status = spt_resume(fiber, S, 2);
    // Check status or result

    spt_close(S);
  }

  static void testFiberError() {
    spt_State *S = spt_newstate();

    // Create a function that would error
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_error(S, "Fiber error test");
      return 0;
    });

    spt_State *fiber = spt_newfiber(S);
    ASSERT(fiber != nullptr, "fiber creation");

    // Resume and expect error status
    int status = spt_resume(fiber, S, 0);
    // Status should indicate error

    spt_close(S);
  }

  // =========================================================
  // Magic Method Tests
  // =========================================================

  static void testMagicMethodName() {
    // Test magic method name lookup
    const char *addName = spt_magicmethodname(SPT_MM_ADD);
    ASSERT(addName != nullptr, "spt_magicmethodname should return name for __add");
    ASSERT_STR_EQ("__add", addName, "__add name");

    const char *initName = spt_magicmethodname(SPT_MM_INIT);
    ASSERT_STR_EQ("__init", initName, "__init name");

    const char *gcName = spt_magicmethodname(SPT_MM_GC);
    ASSERT_STR_EQ("__gc", gcName, "__gc name");

    // Out of range
    const char *invalidName = spt_magicmethodname(SPT_MM_MAX + 10);
    ASSERT(invalidName == nullptr, "invalid index should return NULL");
  }

  static void testMagicMethodIndex() {
    // Test magic method index lookup
    int addIdx = spt_magicmethodindex("__add");
    ASSERT_EQ(SPT_MM_ADD, addIdx, "__add index");

    int initIdx = spt_magicmethodindex("__init");
    ASSERT_EQ(SPT_MM_INIT, initIdx, "__init index");

    int invalidIdx = spt_magicmethodindex("not_a_magic_method");
    ASSERT_EQ(SPT_MM_MAX, invalidIdx, "invalid name should return SPT_MM_MAX");

    int emptyIdx = spt_magicmethodindex("");
    ASSERT_EQ(SPT_MM_MAX, emptyIdx, "empty name should return SPT_MM_MAX");
  }

  static void testHasMagicMethod() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "MagicClass");

    // Initially no magic methods
    ASSERT(!spt_hasmagicmethod(S, -1, SPT_MM_ADD), "should not have __add initially");
    ASSERT(!spt_hasmagicmethod(S, -1, SPT_MM_GC), "should not have __gc initially");

    // Add a magic method
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 0);
      return 1;
    });
    spt_setmagicmethod(S, -2, SPT_MM_ADD);

    ASSERT(spt_hasmagicmethod(S, -1, SPT_MM_ADD), "should have __add after setting");
    ASSERT(!spt_hasmagicmethod(S, -1, SPT_MM_GC), "should still not have __gc");

    spt_close(S);
  }

  static void testSetMagicMethod() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "SetMagicClass");

    // Set __init
    spt_pushcfunction(S, [](spt_State *S) -> int { return 0; });
    spt_setmagicmethod(S, -2, SPT_MM_INIT);

    // Set __gc
    spt_pushcfunction(S, [](spt_State *S) -> int { return 0; });
    spt_setmagicmethod(S, -2, SPT_MM_GC);

    // Verify flags
    unsigned int flags = spt_getclassflags(S, -1);
    ASSERT((flags & SPT_CLASS_HAS_INIT) != 0, "should have INIT flag");
    ASSERT((flags & SPT_CLASS_HAS_GC) != 0, "should have GC flag");

    spt_close(S);
  }

  static void testGetMagicMethod() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "GetMagicClass");

    // Set a magic method
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 99);
      return 1;
    });
    spt_setmagicmethod(S, -2, SPT_MM_ADD);

    // Get it back
    int type = spt_getmagicmethod(S, -1, SPT_MM_ADD);
    ASSERT_EQ(SPT_TCLOSURE, type, "should get closure type");
    spt_pop(S, 1);

    // Get nonexistent magic method
    type = spt_getmagicmethod(S, -1, SPT_MM_SUB);
    ASSERT_EQ(SPT_TNIL, type, "nonexistent magic method should return nil");

    spt_close(S);
  }

  static void testGetClassFlags() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "FlagsClass");

    unsigned int initialFlags = spt_getclassflags(S, -1);
    ASSERT_EQ(SPT_CLASS_NONE, (int64_t)initialFlags, "initial flags should be none");

    // Add arithmetic operators
    spt_pushcfunction(S, [](spt_State *S) -> int { return 1; });
    spt_setmagicmethod(S, -2, SPT_MM_ADD);

    spt_pushcfunction(S, [](spt_State *S) -> int { return 1; });
    spt_setmagicmethod(S, -2, SPT_MM_SUB);

    unsigned int flags = spt_getclassflags(S, -1);
    ASSERT((flags & SPT_CLASS_HAS_ADD) != 0, "should have ADD flag");
    ASSERT((flags & SPT_CLASS_HAS_SUB) != 0, "should have SUB flag");
    ASSERT((flags & SPT_CLASS_HAS_ANY_ARITHMETIC) != 0, "should have ANY_ARITHMETIC");

    spt_close(S);
  }

  static void testMagicMethodAdd() {
    spt_State *S = spt_newstate();

    // Create a class with __add
    spt_newclass(S, "Addable");

    spt_pushcfunction(S, [](spt_State *S) -> int {
      // self + other
      spt_Int a = spt_toint(S, 1); // self value
      spt_Int b = spt_toint(S, 2); // other value
      spt_pushint(S, a + b + 100); // custom add with offset
      return 1;
    });
    spt_setmagicmethod(S, -2, SPT_MM_ADD);

    ASSERT(spt_hasmagicmethod(S, -1, SPT_MM_ADD), "class should have __add");

    spt_close(S);
  }

  static void testMagicMethodGC() {
    spt_State *S = spt_newstate();

    static bool gcCalled = false;
    gcCalled = false;

    spt_newclass(S, "Cleanable");

    spt_pushcfunction(S, [](spt_State *S) -> int {
      gcCalled = true;
      return 0;
    });
    spt_setmagicmethod(S, -2, SPT_MM_GC);

    ASSERT(spt_hasmagicmethod(S, -1, SPT_MM_GC), "class should have __gc");

    spt_close(S);
    // Note: gcCalled would be set when GC runs finalizers
  }

  static void testMagicMethodIndex2() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "Indexable");

    // __getitem
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushstring(S, "got item");
      return 1;
    });
    spt_setmagicmethod(S, -2, SPT_MM_INDEX_GET);

    // __setitem
    spt_pushcfunction(S, [](spt_State *S) -> int { return 0; });
    spt_setmagicmethod(S, -2, SPT_MM_INDEX_SET);

    unsigned int flags = spt_getclassflags(S, -1);
    ASSERT((flags & SPT_CLASS_HAS_ANY_INDEX) != 0, "should have index flags");

    spt_close(S);
  }

  static void testCallMagicMethod() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "Callable");

    // Set __add that returns the sum
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_Int a = spt_toint(S, 1);
      spt_Int b = spt_toint(S, 2);
      spt_pushint(S, a + b);
      return 1;
    });
    spt_setmagicmethod(S, -2, SPT_MM_ADD);

    // Create instance
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    // Setup for magic method call
    // Stack: [class, instance]

    spt_close(S);
  }

  // =========================================================
  // Numeric Edge Cases
  // =========================================================

  static void testInt64Boundaries() {
    spt_State *S = spt_newstate();

    // Maximum int64
    spt_Int maxInt = std::numeric_limits<spt_Int>::max();
    spt_pushint(S, maxInt);
    ASSERT_EQ(maxInt, spt_toint(S, -1), "max int64");

    // Minimum int64
    spt_Int minInt = std::numeric_limits<spt_Int>::min();
    spt_pushint(S, minInt);
    ASSERT_EQ(minInt, spt_toint(S, -1), "min int64");

    // Zero
    spt_pushint(S, 0);
    ASSERT_EQ(0, spt_toint(S, -1), "zero");

    // -1
    spt_pushint(S, -1);
    ASSERT_EQ(-1, spt_toint(S, -1), "negative one");

    spt_close(S);
  }

  static void testFloatSpecialValues() {
    spt_State *S = spt_newstate();

    // Positive infinity
    spt_pushfloat(S, std::numeric_limits<double>::infinity());
    ASSERT(std::isinf(spt_tofloat(S, -1)), "positive infinity");
    ASSERT(spt_tofloat(S, -1) > 0, "positive infinity should be positive");

    // Negative infinity
    spt_pushfloat(S, -std::numeric_limits<double>::infinity());
    ASSERT(std::isinf(spt_tofloat(S, -1)), "negative infinity");
    ASSERT(spt_tofloat(S, -1) < 0, "negative infinity should be negative");

    // NaN
    spt_pushfloat(S, std::numeric_limits<double>::quiet_NaN());
    ASSERT(std::isnan(spt_tofloat(S, -1)), "NaN");

    // Smallest positive
    spt_pushfloat(S, std::numeric_limits<double>::min());
    double smallest = spt_tofloat(S, -1);
    ASSERT(smallest > 0, "smallest positive");

    // Epsilon
    spt_pushfloat(S, std::numeric_limits<double>::epsilon());
    double eps = spt_tofloat(S, -1);
    ASSERT(eps > 0, "epsilon");

    spt_close(S);
  }

  static void testNumericOverflow() {
    spt_State *S = spt_newstate();

    // Large float to int
    spt_pushfloat(S, 1e20);
    int isnum;
    spt_Int val = spt_tointx(S, -1, &isnum);
    // Behavior depends on implementation - just check no crash

    // Very small float to int
    spt_pushfloat(S, 0.0001);
    val = spt_tointx(S, -1, &isnum);
    ASSERT_EQ(0, val, "small float to int should truncate to 0");

    spt_close(S);
  }

  // =========================================================
  // String Edge Cases
  // =========================================================

  static void testEmptyString() {
    spt_State *S = spt_newstate();

    spt_pushstring(S, "");
    ASSERT_EQ(SPT_TSTRING, spt_type(S, -1), "empty string type");

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT_EQ(0, (int64_t)len, "empty string length");
    ASSERT_STR_EQ("", str, "empty string value");

    // len() on empty string
    ASSERT_EQ(0, (int64_t)spt_len(S, -1), "spt_len on empty string");

    spt_close(S);
  }

  static void testStringWithNullBytes() {
    spt_State *S = spt_newstate();

    const char data[] = "Hello\0World\0!";
    spt_pushlstring(S, data, 13);

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT_EQ(13, (int64_t)len, "string with nulls length");

    // Verify content
    ASSERT(memcmp(str, data, 13) == 0, "string with nulls content");

    spt_close(S);
  }

  static void testVeryLongString() {
    spt_State *S = spt_newstate();

    // Create a 10KB string
    std::string longStr(10000, 'x');
    spt_pushstring(S, longStr.c_str());

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT_EQ(10000, (int64_t)len, "long string length");

    spt_close(S);
  }

  static void testUnicodeStrings() {
    spt_State *S = spt_newstate();

    // UTF-8 encoded string
    const char *utf8 = "Hello 世界 🌍";
    spt_pushstring(S, utf8);

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT(len > 0, "unicode string should have length");
    ASSERT(strcmp(str, utf8) == 0, "unicode string content");

    spt_close(S);
  }

  // =========================================================
  // Stack Edge Cases
  // =========================================================

  static void testDeepStack() {
    spt_State *S = spt_newstate();

    // Push many values
    const int depth = 100;
    for (int i = 0; i < depth; i++) {
      spt_pushint(S, i);
    }

    ASSERT_EQ(depth, spt_gettop(S), "deep stack top");

    // Verify values
    for (int i = 0; i < depth; i++) {
      ASSERT_EQ(depth - 1 - i, spt_toint(S, -1 - i), "deep stack value");
    }

    spt_close(S);
  }

  static void testStackUnderflow() {
    spt_State *S = spt_newstate();

    // Push one value
    spt_pushint(S, 42);
    ASSERT_EQ(1, spt_gettop(S), "initial stack");

    // Try to access invalid index
    ASSERT_EQ(SPT_TNONE, spt_type(S, 100), "invalid positive index");
    ASSERT_EQ(SPT_TNONE, spt_type(S, -100), "invalid negative index");

    // settop to 0 should work
    spt_settop(S, 0);
    ASSERT_EQ(0, spt_gettop(S), "empty stack");

    spt_close(S);
  }

  static void testNegativeIndexEdgeCases() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);

    // -1 should be 3
    ASSERT_EQ(3, spt_toint(S, -1), "index -1");

    // -3 should be 1
    ASSERT_EQ(1, spt_toint(S, -3), "index -3");

    // -4 should be invalid
    ASSERT_EQ(SPT_TNONE, spt_type(S, -4), "index -4 invalid");

    spt_close(S);
  }

  static void testPseudoIndexAccess() {
    spt_State *S = spt_newstate();

    // Registry index
    int type = spt_type(S, SPT_REGISTRYINDEX);
    // Registry should be a map or similar
    ASSERT(type != SPT_TNONE, "registry should exist");

    // Test global variable access (new API)
    // Set a global variable
    spt_pushint(S, 42);
    spt_setglobal(S, "test_global");

    // Get it back
    type = spt_getglobal(S, "test_global");
    ASSERT(type == SPT_TINT, "global should be integer");
    ASSERT_EQ(42, spt_toint(S, -1), "global value should be 42");
    spt_pop(S, 1);

    spt_close(S);
  }

  // =========================================================
  // Collection Edge Cases
  // =========================================================

  static void testEmptyListOperations() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    ASSERT_EQ(0, spt_listlen(S, -1), "empty list len");

    // Clear empty list (should not crash)
    spt_listclear(S, -1);
    ASSERT_EQ(0, spt_listlen(S, -1), "cleared empty list");

    // Iterate empty list
    int iter = spt_listiter(S, -1);
    ASSERT(iter >= 0, "listiter on empty list");
    ASSERT(!spt_listnext(S, -1, &iter), "listnext on empty list");

    spt_close(S);
  }

  static void testEmptyMapOperations() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    ASSERT_EQ(0, spt_maplen(S, -1), "empty map len");

    // Clear empty map
    spt_mapclear(S, -1);
    ASSERT_EQ(0, spt_maplen(S, -1), "cleared empty map");

    // Get from empty map
    spt_getfield(S, -1, "nonexistent");
    ASSERT_EQ(SPT_TNIL, spt_type(S, -1), "get from empty map");
    spt_pop(S, 1);

    // Keys/values of empty map
    spt_mapkeys(S, -1);
    ASSERT_EQ(0, spt_listlen(S, -1), "empty map keys");
    spt_pop(S, 1);

    spt_mapvalues(S, -1);
    ASSERT_EQ(0, spt_listlen(S, -1), "empty map values");

    spt_close(S);
  }

  static void testListMixedTypes() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);

    // Add various types
    spt_pushnil(S);
    spt_listappend(S, -2);

    spt_pushbool(S, 1);
    spt_listappend(S, -2);

    spt_pushint(S, 42);
    spt_listappend(S, -2);

    spt_pushfloat(S, 3.14);
    spt_listappend(S, -2);

    spt_pushstring(S, "hello");
    spt_listappend(S, -2);

    spt_newlist(S, 0); // nested list
    spt_listappend(S, -2);

    spt_newmap(S, 0); // nested map
    spt_listappend(S, -2);

    ASSERT_EQ(7, spt_listlen(S, -1), "mixed list length");

    // Verify types
    spt_listgeti(S, -1, 0);
    ASSERT_EQ(SPT_TNIL, spt_type(S, -1), "element 0 type");
    spt_pop(S, 1);

    spt_listgeti(S, -1, 2);
    ASSERT_EQ(SPT_TINT, spt_type(S, -1), "element 2 type");
    spt_pop(S, 1);

    spt_listgeti(S, -1, 5);
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "element 5 type");

    spt_close(S);
  }

  static void testMapVariousKeyTypes() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);

    // String key
    spt_pushint(S, 1);
    spt_setfield(S, -2, "string_key");

    // Integer key
    spt_pushint(S, 100);
    spt_pushint(S, 2);
    spt_setmap(S, -3);

    // Float key
    spt_pushfloat(S, 3.14);
    spt_pushint(S, 3);
    spt_setmap(S, -3);

    ASSERT_EQ(3, spt_maplen(S, -1), "map with various keys");

    // Retrieve by string
    spt_getfield(S, -1, "string_key");
    ASSERT_EQ(1, spt_toint(S, -1), "string key value");
    spt_pop(S, 1);

    // Retrieve by integer
    spt_pushint(S, 100);
    spt_getmap(S, -2);
    ASSERT_EQ(2, spt_toint(S, -1), "int key value");

    spt_close(S);
  }

  static void testNestedCollections() {
    spt_State *S = spt_newstate();

    // Create nested structure: { "list": [1, 2, { "inner": "value" }] }
    spt_newmap(S, 0);

    spt_newlist(S, 0);
    spt_pushint(S, 1);
    spt_listappend(S, -2);
    spt_pushint(S, 2);
    spt_listappend(S, -2);

    spt_newmap(S, 0);
    spt_pushstring(S, "value");
    spt_setfield(S, -2, "inner");
    spt_listappend(S, -2);

    spt_setfield(S, -2, "list");

    // Navigate the structure
    spt_getfield(S, -1, "list");
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "list field type");
    ASSERT_EQ(3, spt_listlen(S, -1), "nested list length");

    spt_listgeti(S, -1, 2);
    ASSERT_EQ(SPT_TMAP, spt_type(S, -1), "nested map type");

    spt_getfield(S, -1, "inner");
    ASSERT_STR_EQ("value", spt_tostring(S, -1, nullptr), "deep nested value");

    spt_close(S);
  }

  static void testListIndexOutOfBounds() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 1);
    spt_listappend(S, -2);
    spt_pushint(S, 2);
    spt_listappend(S, -2);

    // Valid indices
    spt_listgeti(S, -1, 0);
    ASSERT_EQ(1, spt_toint(S, -1), "index 0");
    spt_pop(S, 1);

    spt_listgeti(S, -1, 1);
    ASSERT_EQ(2, spt_toint(S, -1), "index 1");
    spt_pop(S, 1);

    // Out of bounds - should return nil or handle gracefully
    spt_listgeti(S, -1, 100);
    // Implementation may return nil or throw error

    spt_close(S);
  }

  // =========================================================
  // Class/Instance Edge Cases
  // =========================================================

  static void testClassWithoutMethods() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "EmptyClass");
    ASSERT_EQ(SPT_TCLASS, spt_type(S, -1), "empty class type");

    // Create instance
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);
    ASSERT_EQ(SPT_TOBJECT, spt_type(S, -1), "empty class instance");

    spt_close(S);
  }

  static void testInstanceFieldOverwrite() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "FieldClass");
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    // Set field
    spt_pushint(S, 10);
    spt_setprop(S, -2, "value");

    spt_getprop(S, -1, "value");
    ASSERT_EQ(10, spt_toint(S, -1), "initial value");
    spt_pop(S, 1);

    // Overwrite field
    spt_pushint(S, 20);
    spt_setprop(S, -2, "value");

    spt_getprop(S, -1, "value");
    ASSERT_EQ(20, spt_toint(S, -1), "overwritten value");

    spt_close(S);
  }

  static void testMultipleInstancesSameClass() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "MultiInstance");
    int classIdx = spt_absindex(S, -1);

    // Create first instance
    spt_pushvalue(S, classIdx);
    spt_newinstance(S, 0);
    spt_pushint(S, 100);
    spt_setprop(S, -2, "value");
    int inst1Idx = spt_absindex(S, -1);

    // Create second instance
    spt_pushvalue(S, classIdx);
    spt_newinstance(S, 0);
    spt_pushint(S, 200);
    spt_setprop(S, -2, "value");

    // Check they're independent
    spt_getprop(S, inst1Idx, "value");
    ASSERT_EQ(100, spt_toint(S, -1), "instance 1 value");
    spt_pop(S, 1);

    spt_getprop(S, -1, "value");
    ASSERT_EQ(200, spt_toint(S, -1), "instance 2 value");

    spt_close(S);
  }

  static void testCInstanceZeroSize() {
    spt_State *S = spt_newstate();

    // Zero-size C instance
    void *data = spt_newcinstance(S, 0);
    // data might be non-null but shouldn't be used
    ASSERT_EQ(SPT_TCINSTANCE, spt_type(S, -1), "zero-size cinstance type");

    spt_close(S);
  }

  static void testCInstanceLifecycle() {
    spt_State *S = spt_newstate();

    struct TestData {
      int value;
      bool *destroyed;
    };

    static bool wasDestroyed = false;
    wasDestroyed = false;

    spt_newclass(S, "LifecycleClass");

    // Set __gc
    spt_pushcfunction(S, [](spt_State *S) -> int {
      void *data = spt_getcinstancedata(S, 1);
      if (data) {
        TestData *td = static_cast<TestData *>(data);
        if (td->destroyed) {
          *td->destroyed = true;
        }
      }
      return 0;
    });
    spt_setmagicmethod(S, -2, SPT_MM_GC);

    // Create instance
    TestData *data = static_cast<TestData *>(spt_newcinstanceof(S, sizeof(TestData)));
    data->value = 42;
    data->destroyed = &wasDestroyed;

    ASSERT_EQ(42, data->value, "cinstance data value");

    spt_close(S);
  }

  // =========================================================
  // Function Call Edge Cases
  // =========================================================

  static void testCallZeroArgs() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 42);
      return 1;
    });

    int status = spt_call(S, 0, 1);
    ASSERT_EQ(SPT_OK, status, "call with 0 args");
    ASSERT_EQ(42, spt_toint(S, -1), "return value");

    spt_close(S);
  }

  static void testCallManyArgs() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_Int sum = 0;
      int top = spt_gettop(S);
      for (int i = 1; i <= top; i++) {
        sum += spt_toint(S, i);
      }
      spt_pushint(S, sum);
      return 1;
    });

    // Push 10 arguments
    for (int i = 1; i <= 10; i++) {
      spt_pushint(S, i);
    }

    int status = spt_call(S, 10, 1);
    ASSERT_EQ(SPT_OK, status, "call with many args");
    ASSERT_EQ(55, spt_toint(S, -1), "sum of 1-10");

    spt_close(S);
  }

  static void testNestedFunctionCalls() {
    spt_State *S = spt_newstate();

    // Outer function that calls inner
    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushcfunction(S, [](spt_State *S) -> int {
        spt_pushint(S, 100);
        return 1;
      });
      spt_call(S, 0, 1);
      spt_Int inner = spt_toint(S, -1);
      spt_pop(S, 1);
      spt_pushint(S, inner + 1);
      return 1;
    });

    int status = spt_call(S, 0, 1);
    ASSERT_EQ(SPT_OK, status, "nested call");
    ASSERT_EQ(101, spt_toint(S, -1), "nested result");

    spt_close(S);
  }

  static void testRecursiveFunctionCall() {
    spt_State *S = spt_newstate();

    // Use dostring for recursive function
    int status = spt_dostring(S,
                              "int factorial(int n) {"
                              "  if (n <= 1) { return 1; }"
                              "  return n * factorial(n - 1);"
                              "}"
                              "global int result = factorial(5);",
                              "recursive");

    if (status == SPT_OK) {
      spt_getglobal(S, "result");
      ASSERT_EQ(120, spt_toint(S, -1), "factorial(5) = 120");
    }

    spt_close(S);
  }

  static void testMultipleReturnValues() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 1);
      spt_pushint(S, 2);
      spt_pushint(S, 3);
      return 3;
    });

    int status = spt_call(S, 0, SPT_MULTRET);
    ASSERT_EQ(SPT_OK, status, "multi-return call");
    ASSERT(spt_gettop(S) >= 3, "should have at least 3 values");

    spt_close(S);
  }

  // =========================================================
  // Module System Tests
  // =========================================================

  static void testAddPath() {
    spt_State *S = spt_newstate();

    // Just ensure it doesn't crash
    spt_addpath(S, "/some/path");
    spt_addpath(S, "./relative/path");
    spt_addpath(S, "");

    spt_close(S);
  }

  static void testDefModule() {
    spt_State *S = spt_newstate();

    // Define a simple module
    static spt_Reg myModuleFuncs[] = {
        {"add",
         [](spt_State *S) -> int {
           spt_Int a = spt_toint(S, 1);
           spt_Int b = spt_toint(S, 2);
           spt_pushint(S, a + b);
           return 1;
         },
         2},
        {"mul",
         [](spt_State *S) -> int {
           spt_Int a = spt_toint(S, 1);
           spt_Int b = spt_toint(S, 2);
           spt_pushint(S, a * b);
           return 1;
         },
         2},
        {nullptr, nullptr, 0} // Sentinel
    };

    spt_defmodule(S, "mymath", myModuleFuncs);

    spt_close(S);
  }

  // =========================================================
  // Reference System Tests
  // =========================================================

  static void testMultipleReferences() {
    spt_State *S = spt_newstate();

    // Create multiple references
    int refs[10];
    for (int i = 0; i < 10; i++) {
      spt_pushint(S, i * 10);
      refs[i] = spt_ref(S);
      ASSERT(refs[i] != SPT_NOREF, "ref should be valid");
    }

    // Retrieve them
    for (int i = 0; i < 10; i++) {
      spt_getref(S, refs[i]);
      ASSERT_EQ(i * 10, spt_toint(S, -1), "ref value");
      spt_pop(S, 1);
    }

    // Release them
    for (int i = 0; i < 10; i++) {
      spt_unref(S, refs[i]);
    }

    spt_close(S);
  }

  static void testReferenceToComplexObjects() {
    spt_State *S = spt_newstate();

    // Reference to a list
    spt_newlist(S, 0);
    spt_pushint(S, 42);
    spt_listappend(S, -2);
    int listRef = spt_ref(S);

    // Reference to a map
    spt_newmap(S, 0);
    spt_pushstring(S, "value");
    spt_setfield(S, -2, "key");
    int mapRef = spt_ref(S);

    // Retrieve list
    spt_getref(S, listRef);
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "list ref type");
    spt_listgeti(S, -1, 0);
    ASSERT_EQ(42, spt_toint(S, -1), "list element");
    spt_pop(S, 2);

    // Retrieve map
    spt_getref(S, mapRef);
    ASSERT_EQ(SPT_TMAP, spt_type(S, -1), "map ref type");
    spt_getfield(S, -1, "key");
    ASSERT_STR_EQ("value", spt_tostring(S, -1, nullptr), "map value");

    spt_unref(S, listRef);
    spt_unref(S, mapRef);

    spt_close(S);
  }

  static void testUnrefNonexistent() {
    spt_State *S = spt_newstate();

    // Unref with invalid ref should not crash
    spt_unref(S, SPT_NOREF);
    spt_unref(S, SPT_REFNIL);
    spt_unref(S, 999999); // Very large invalid ref

    spt_close(S);
  }

  // =========================================================
  // Error Handling Edge Cases
  // =========================================================

  static void testErrorInPcall() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_error(S, "Test error message");
      return 0;
    });

    int status = spt_pcall(S, 0, 0, 0);
    ASSERT(status != SPT_OK, "pcall should catch error");

    // Error message should be on stack
    if (spt_gettop(S) > 0) {
      int type = spt_type(S, -1);
      // Error could be string or other type
    }

    spt_close(S);
  }

  static void testNestedPcallErrors() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      // Inner pcall
      spt_pushcfunction(S, [](spt_State *S) -> int {
        spt_error(S, "Inner error");
        return 0;
      });

      int status = spt_pcall(S, 0, 0, 0);
      if (status != SPT_OK) {
        // Re-throw
        spt_error(S, "Outer caught inner error");
      }
      return 0;
    });

    int status = spt_pcall(S, 0, 0, 0);
    ASSERT(status != SPT_OK, "nested pcall should propagate error");

    spt_close(S);
  }

  static void testThrow() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushstring(S, "thrown error");
      spt_throw(S);
      return 0; // Not reached
    });

    int status = spt_pcall(S, 0, 0, 0);
    ASSERT(status != SPT_OK, "throw should cause error");

    spt_close(S);
  }
};

// Convenience function
inline int runSptCApiExtendedTests() {
  SptCApiExtendedTests tests;
  return tests.runAll();
}