#pragma once

#include "spt.h"
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

class SptCApiDirectTests {
public:
  struct TestResult {
    std::string name;
    bool passed;
    std::string message;
  };

  int runAll() {
    std::cout << "Running SPT C API Direct Tests..." << std::endl;
    std::cout << "==================================================" << std::endl;

    addTest("spt_newstate/spt_close", testStateManagement);
    addTest("spt_newstateex", testStateManagementEx);
    addTest("spt_version", testVersion);
    addTest("spt_userdata", testUserData);

    addTest("spt_pushnil", testPushNil);
    addTest("spt_pushbool", testPushBool);
    addTest("spt_pushint", testPushInt);
    addTest("spt_pushfloat", testPushFloat);
    addTest("spt_pushstring", testPushString);
    addTest("spt_pushlstring", testPushLString);
    addTest("spt_pushfstring", testPushFString);
    addTest("spt_pushlightuserdata", testPushLightUserData);

    addTest("spt_gettop/spt_settop", testGetSetTop);
    addTest("spt_pop", testPop);
    addTest("spt_pushvalue", testPushValue);
    addTest("spt_copy", testCopy);
    addTest("spt_insert", testInsert);
    addTest("spt_remove", testRemove);
    addTest("spt_replace", testReplace);
    addTest("spt_rotate", testRotate);
    addTest("spt_absindex", testAbsIndex);
    addTest("spt_checkstack", testCheckStack);

    addTest("spt_type", testType);
    addTest("spt_typename", testTypename);
    addTest("spt_is* functions", testIsTypeFunctions);

    addTest("spt_toboolean", testToBoolean);
    addTest("spt_toint/spt_tointx", testToInt);
    addTest("spt_tofloat/spt_tofloatx", testToFloat);
    addTest("spt_tostring", testToString);
    addTest("spt_tolightuserdata", testToLightUserData);

    addTest("spt_equal", testEqual);
    addTest("spt_rawequal", testRawEqual);
    addTest("spt_compare", testCompare);

    addTest("spt_newlist", testNewList);
    addTest("spt_listlen", testListLen);
    addTest("spt_listappend", testListAppend);
    addTest("spt_listgeti/spt_listseti", testListGetSet);
    addTest("spt_listinsert", testListInsert);
    addTest("spt_listremove", testListRemove);
    addTest("spt_listclear", testListClear);

    addTest("spt_newmap", testNewMap);
    addTest("spt_maplen", testMapLen);
    addTest("spt_getmap/spt_setmap", testMapGetSet);
    addTest("spt_getfield/spt_setfield", testFieldGetSet);
    addTest("spt_haskey", testHasKey);
    addTest("spt_mapremove", testMapRemove);
    addTest("spt_mapclear", testMapClear);
    addTest("spt_mapkeys/spt_mapvalues", testMapKeysValues);
    addTest("spt_mapnext", testMapNext);

    addTest("spt_rawget/spt_rawset", testRawGetSet);

    addTest("spt_newclass", testNewClass);
    addTest("spt_bindmethod", testBindMethod);
    addTest("spt_bindstatic", testBindStatic);
    addTest("spt_newinstance [BUG: __init not called]", testNewInstance);
    addTest("spt_getprop/spt_setprop", testPropGetSet);
    addTest("spt_hasprop", testHasProp);
    addTest("spt_getclass", testGetClass);
    addTest("spt_classname", testClassName);
    addTest("spt_isinstance", testIsInstance);

    addTest("spt_newcinstance", testNewCInstance);
    addTest("spt_newcinstanceof", testNewCInstanceOf);
    addTest("spt_getcinstancedata", testGetCInstanceData);

    addTest("spt_pushcclosure", testPushCClosure);
    addTest("spt_pushcfunction", testPushCFunction);
    addTest("spt_getupvalue/spt_setupvalue", testUpvalues);

    addTest("spt_compile", testCompile);
    addTest("spt_loadfile [BUG: always fails]", testLoadFile);
    addTest("spt_call", testCall);
    addTest("spt_pcall", testPCall);

    addTest("spt_getglobal/spt_setglobal", testGlobals);
    addTest("spt_hasglobal", testHasGlobal);
    addTest("spt_ref/spt_unref/spt_getref", testReferences);

    addTest("spt_error", testError);

    addTest("spt_gc", testGC);

    addTest("spt_len", testLen);
    addTest("spt_concat", testConcat);
    addTest("spt_checkint/spt_checkfloat/spt_checkstring", testCheckFunctions);
    addTest("spt_optint/spt_optfloat/spt_optstring", testOptFunctions);

    addTest("spt_listiter/spt_listnext", testListIteration);

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

  static void testStateManagement() {
    spt_State *S = spt_newstate();
    ASSERT(S != nullptr, "spt_newstate returned NULL");

    spt_State *main = spt_getmain(S);
    ASSERT(main == S, "spt_getmain should return the same state for main state");

    spt_State *current = spt_getcurrent(S);
    ASSERT(current != nullptr, "spt_getcurrent returned NULL");

    spt_close(S);
  }

  static void testStateManagementEx() {
    spt_State *S = spt_newstateex(1024 * 256, 1024 * 1024 * 64, true);
    ASSERT(S != nullptr, "spt_newstateex returned NULL");
    spt_close(S);
  }

  static void testVersion() {
    const char *version = spt_version();
    ASSERT(version != nullptr, "spt_version returned NULL");
    ASSERT(strlen(version) > 0, "spt_version returned empty string");

    int versionNum = spt_versionnum();
    ASSERT(versionNum == SPT_VERSION_NUM, "spt_versionnum mismatch");
  }

  static void testUserData() {
    spt_State *S = spt_newstate();
    ASSERT(S != nullptr, "spt_newstate returned NULL");

    int data = 12345;
    spt_setuserdata(S, &data);

    void *retrieved = spt_getuserdata(S);
    ASSERT(retrieved == &data, "spt_getuserdata returned wrong pointer");
    ASSERT(*static_cast<int *>(retrieved) == 12345, "User data value mismatch");

    spt_close(S);
  }

  static void testPushNil() {
    spt_State *S = spt_newstate();

    spt_pushnil(S);
    ASSERT_EQ(1, spt_gettop(S), "Stack top should be 1");
    ASSERT_EQ(SPT_TNIL, spt_type(S, -1), "Top should be nil");

    spt_close(S);
  }

  static void testPushBool() {
    spt_State *S = spt_newstate();

    spt_pushbool(S, 1);
    ASSERT_EQ(SPT_TBOOL, spt_type(S, -1), "Top should be bool");
    ASSERT_EQ(1, spt_tobool(S, -1), "Bool value should be true");

    spt_pushbool(S, 0);
    ASSERT_EQ(0, spt_tobool(S, -1), "Bool value should be false");

    spt_close(S);
  }

  static void testPushInt() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    ASSERT_EQ(SPT_TINT, spt_type(S, -1), "Top should be int");
    ASSERT_EQ(42, spt_toint(S, -1), "Int value should be 42");

    spt_pushint(S, -100);
    ASSERT_EQ(-100, spt_toint(S, -1), "Int value should be -100");

    spt_pushint(S, 9223372036854775807LL);
    ASSERT_EQ(9223372036854775807LL, spt_toint(S, -1), "Large int test");

    spt_close(S);
  }

  static void testPushFloat() {
    spt_State *S = spt_newstate();

    spt_pushfloat(S, 3.14159);
    ASSERT_EQ(SPT_TFLOAT, spt_type(S, -1), "Top should be float");
    ASSERT_FLOAT_EQ(3.14159, spt_tofloat(S, -1), "Float value mismatch");

    spt_pushfloat(S, -0.001);
    ASSERT_FLOAT_EQ(-0.001, spt_tofloat(S, -1), "Negative float mismatch");

    spt_close(S);
  }

  static void testPushString() {
    spt_State *S = spt_newstate();

    spt_pushstring(S, "Hello World");
    ASSERT_EQ(SPT_TSTRING, spt_type(S, -1), "Top should be string");

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT_STR_EQ("Hello World", str, "String value mismatch");
    ASSERT_EQ(11, (int64_t)len, "String length mismatch");

    spt_pushstring(S, "");
    str = spt_tostring(S, -1, &len);
    ASSERT_STR_EQ("", str, "Empty string test");
    ASSERT_EQ(0, (int64_t)len, "Empty string length");

    spt_pushstring(S, nullptr);
    ASSERT_EQ(SPT_TNIL, spt_type(S, -1), "NULL string should push nil");

    spt_close(S);
  }

  static void testPushLString() {
    spt_State *S = spt_newstate();

    const char data[] = "Hello\0World";
    spt_pushlstring(S, data, 11);

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT_EQ(11, (int64_t)len, "lstring length should include null byte");

    spt_close(S);
  }

  static void testPushFString() {
    spt_State *S = spt_newstate();

    const char *result = spt_pushfstring(S, "Value: %d, Name: %s", 42, "test");
    ASSERT(result != nullptr, "spt_pushfstring returned NULL");

    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT(strstr(str, "42") != nullptr, "Formatted string should contain 42");
    ASSERT(strstr(str, "test") != nullptr, "Formatted string should contain test");

    spt_close(S);
  }

  static void testPushLightUserData() {
    spt_State *S = spt_newstate();

    int data = 999;
    spt_pushlightuserdata(S, &data);
    ASSERT_EQ(SPT_TLIGHTUSERDATA, spt_type(S, -1), "Top should be lightuserdata");

    void *ptr = spt_tolightuserdata(S, -1);
    ASSERT(ptr == &data, "Light userdata pointer mismatch");

    spt_close(S);
  }

  static void testGetSetTop() {
    spt_State *S = spt_newstate();

    ASSERT_EQ(0, spt_gettop(S), "Initial stack should be empty");

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);
    ASSERT_EQ(3, spt_gettop(S), "Stack top should be 3");

    spt_settop(S, 2);
    ASSERT_EQ(2, spt_gettop(S), "Stack top should be 2 after settop");

    spt_settop(S, 5);
    ASSERT_EQ(5, spt_gettop(S), "Stack top should be 5 after expansion");
    ASSERT_EQ(SPT_TNIL, spt_type(S, 3), "New slots should be nil");
    ASSERT_EQ(SPT_TNIL, spt_type(S, 4), "New slots should be nil");
    ASSERT_EQ(SPT_TNIL, spt_type(S, 5), "New slots should be nil");

    spt_settop(S, -3);
    ASSERT_EQ(3, spt_gettop(S), "Negative settop test");

    spt_close(S);
  }

  static void testPop() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);

    spt_pop(S, 1);
    ASSERT_EQ(2, spt_gettop(S), "Pop 1 should leave 2 elements");

    spt_pop(S, 2);
    ASSERT_EQ(0, spt_gettop(S), "Pop 2 should leave 0 elements");

    spt_close(S);
  }

  static void testPushValue() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    spt_pushstring(S, "hello");
    spt_pushvalue(S, 1);

    ASSERT_EQ(3, spt_gettop(S), "Stack should have 3 elements");
    ASSERT_EQ(42, spt_toint(S, -1), "Copied value should be 42");

    spt_close(S);
  }

  static void testCopy() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 10);
    spt_pushint(S, 20);
    spt_pushint(S, 30);

    spt_copy(S, 1, 3);

    ASSERT_EQ(10, spt_toint(S, 3), "Position 3 should now be 10");

    spt_close(S);
  }

  static void testInsert() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);

    spt_insert(S, 1);

    ASSERT_EQ(3, spt_toint(S, 1), "Position 1 should be 3");
    ASSERT_EQ(1, spt_toint(S, 2), "Position 2 should be 1");
    ASSERT_EQ(2, spt_toint(S, 3), "Position 3 should be 2");

    spt_close(S);
  }

  static void testRemove() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);

    spt_remove(S, 2);

    ASSERT_EQ(2, spt_gettop(S), "Stack should have 2 elements");
    ASSERT_EQ(1, spt_toint(S, 1), "Position 1 should be 1");
    ASSERT_EQ(3, spt_toint(S, 2), "Position 2 should be 3");

    spt_close(S);
  }

  static void testReplace() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 99);

    spt_replace(S, 1);

    ASSERT_EQ(2, spt_gettop(S), "Stack should have 2 elements");
    ASSERT_EQ(99, spt_toint(S, 1), "Position 1 should be 99");
    ASSERT_EQ(2, spt_toint(S, 2), "Position 2 should be 2");

    spt_close(S);
  }

  static void testRotate() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);
    spt_pushint(S, 4);

    spt_rotate(S, 2, 1);

    ASSERT_EQ(1, spt_toint(S, 1), "Position 1");
    ASSERT_EQ(4, spt_toint(S, 2), "Position 2 after rotate");
    ASSERT_EQ(2, spt_toint(S, 3), "Position 3 after rotate");
    ASSERT_EQ(3, spt_toint(S, 4), "Position 4 after rotate");

    spt_close(S);
  }

  static void testAbsIndex() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 1);
    spt_pushint(S, 2);
    spt_pushint(S, 3);

    ASSERT_EQ(3, spt_absindex(S, -1), "Abs index of -1");
    ASSERT_EQ(2, spt_absindex(S, -2), "Abs index of -2");
    ASSERT_EQ(1, spt_absindex(S, -3), "Abs index of -3");
    ASSERT_EQ(1, spt_absindex(S, 1), "Abs index of 1");

    spt_close(S);
  }

  static void testCheckStack() {
    spt_State *S = spt_newstate();

    int result = spt_checkstack(S, 100);
    ASSERT(result != 0, "spt_checkstack should succeed for reasonable size");

    spt_close(S);
  }

  static void testType() {
    spt_State *S = spt_newstate();

    spt_pushnil(S);
    ASSERT_EQ(SPT_TNIL, spt_type(S, -1), "nil type");

    spt_pushbool(S, 1);
    ASSERT_EQ(SPT_TBOOL, spt_type(S, -1), "bool type");

    spt_pushint(S, 42);
    ASSERT_EQ(SPT_TINT, spt_type(S, -1), "int type");

    spt_pushfloat(S, 3.14);
    ASSERT_EQ(SPT_TFLOAT, spt_type(S, -1), "float type");

    spt_pushstring(S, "test");
    ASSERT_EQ(SPT_TSTRING, spt_type(S, -1), "string type");

    spt_newlist(S, 0);
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "list type");

    spt_newmap(S, 0);
    ASSERT_EQ(SPT_TMAP, spt_type(S, -1), "map type");

    ASSERT_EQ(SPT_TNONE, spt_type(S, 100), "invalid index should return TNONE");

    spt_close(S);
  }

  static void testTypename() {
    spt_State *S = spt_newstate();

    ASSERT_STR_EQ("nil", spt_typename(S, SPT_TNIL), "nil typename");
    ASSERT_STR_EQ("bool", spt_typename(S, SPT_TBOOL), "bool typename");
    ASSERT_STR_EQ("int", spt_typename(S, SPT_TINT), "int typename");
    ASSERT_STR_EQ("float", spt_typename(S, SPT_TFLOAT), "float typename");
    ASSERT_STR_EQ("string", spt_typename(S, SPT_TSTRING), "string typename");
    ASSERT_STR_EQ("list", spt_typename(S, SPT_TLIST), "list typename");
    ASSERT_STR_EQ("map", spt_typename(S, SPT_TMAP), "map typename");
    ASSERT_STR_EQ("function", spt_typename(S, SPT_TCLOSURE), "closure typename");
    ASSERT_STR_EQ("class", spt_typename(S, SPT_TCLASS), "class typename");

    spt_close(S);
  }

  static void testIsTypeFunctions() {
    spt_State *S = spt_newstate();

    spt_pushnil(S);
    ASSERT(spt_isnil(S, -1), "isnil");
    ASSERT(!spt_isbool(S, -1), "isnil not bool");

    spt_pushbool(S, 1);
    ASSERT(spt_isbool(S, -1), "isbool");
    ASSERT(!spt_isint(S, -1), "isbool not int");

    spt_pushint(S, 42);
    ASSERT(spt_isint(S, -1), "isint");
    ASSERT(spt_isnumber(S, -1), "int is number");
    ASSERT(!spt_isfloat(S, -1), "isint not float");

    spt_pushfloat(S, 3.14);
    ASSERT(spt_isfloat(S, -1), "isfloat");
    ASSERT(spt_isnumber(S, -1), "float is number");
    ASSERT(!spt_isint(S, -1), "isfloat not int");

    spt_pushstring(S, "test");
    ASSERT(spt_isstring(S, -1), "isstring");

    spt_newlist(S, 0);
    ASSERT(spt_islist(S, -1), "islist");

    spt_newmap(S, 0);
    ASSERT(spt_ismap(S, -1), "ismap");

    int data = 0;
    spt_pushlightuserdata(S, &data);
    ASSERT(spt_islightuserdata(S, -1), "islightuserdata");

    spt_close(S);
  }

  static void testToBoolean() {
    spt_State *S = spt_newstate();

    spt_pushnil(S);
    ASSERT_EQ(0, spt_toboolean(S, -1), "nil is falsy");

    spt_pushbool(S, 0);
    ASSERT_EQ(0, spt_toboolean(S, -1), "false is falsy");

    spt_pushbool(S, 1);
    ASSERT_EQ(1, spt_toboolean(S, -1), "true is truthy");

    spt_pushint(S, 0);

    spt_pushint(S, 1);
    ASSERT_EQ(1, spt_toboolean(S, -1), "non-zero int is truthy");

    spt_pushstring(S, "hello");
    ASSERT_EQ(1, spt_toboolean(S, -1), "non-empty string is truthy");

    spt_close(S);
  }

  static void testToInt() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    int isnum;
    spt_Int val = spt_tointx(S, -1, &isnum);
    ASSERT_EQ(42, val, "tointx value");
    ASSERT_EQ(1, isnum, "tointx should set isnum to 1");

    spt_pushfloat(S, 3.7);
    val = spt_tointx(S, -1, &isnum);
    ASSERT_EQ(3, val, "float to int should truncate");
    ASSERT_EQ(1, isnum, "float is convertible to int");

    spt_pushstring(S, "not a number");
    val = spt_tointx(S, -1, &isnum);
    ASSERT_EQ(0, isnum, "string should not be convertible");

    spt_close(S);
  }

  static void testToFloat() {
    spt_State *S = spt_newstate();

    spt_pushfloat(S, 3.14);
    int isnum;
    spt_Float val = spt_tofloatx(S, -1, &isnum);
    ASSERT_FLOAT_EQ(3.14, val, "tofloatx value");
    ASSERT_EQ(1, isnum, "tofloatx should set isnum to 1");

    spt_pushint(S, 42);
    val = spt_tofloatx(S, -1, &isnum);
    ASSERT_FLOAT_EQ(42.0, val, "int to float");
    ASSERT_EQ(1, isnum, "int is convertible to float");

    spt_close(S);
  }

  static void testToString() {
    spt_State *S = spt_newstate();

    spt_pushstring(S, "hello");
    size_t len;
    const char *str = spt_tostring(S, -1, &len);
    ASSERT_STR_EQ("hello", str, "tostring value");
    ASSERT_EQ(5, (int64_t)len, "tostring length");

    spt_pushint(S, 42);
    str = spt_tostring(S, -1, nullptr);
    ASSERT(str == nullptr, "tostring on int should return NULL");

    spt_close(S);
  }

  static void testToLightUserData() {
    spt_State *S = spt_newstate();

    int data = 123;
    spt_pushlightuserdata(S, &data);
    void *ptr = spt_tolightuserdata(S, -1);
    ASSERT(ptr == &data, "tolightuserdata pointer");

    spt_pushint(S, 42);
    ptr = spt_tolightuserdata(S, -1);
    ASSERT(ptr == nullptr, "tolightuserdata on int should return NULL");

    spt_close(S);
  }

  static void testEqual() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    spt_pushint(S, 42);
    ASSERT(spt_equal(S, -1, -2), "equal ints");

    spt_pushint(S, 43);
    ASSERT(!spt_equal(S, -1, -2), "unequal ints");

    spt_pushstring(S, "hello");
    spt_pushstring(S, "hello");
    ASSERT(spt_equal(S, -1, -2), "equal strings");

    spt_close(S);
  }

  static void testRawEqual() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    spt_pushint(S, 42);
    ASSERT(spt_rawequal(S, -1, -2), "rawequal ints");

    spt_close(S);
  }

  static void testCompare() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 10);
    spt_pushint(S, 20);

    int cmp = spt_compare(S, 1, 2);
    ASSERT(cmp < 0, "10 < 20");

    cmp = spt_compare(S, 2, 1);
    ASSERT(cmp > 0, "20 > 10");

    spt_pushint(S, 20);
    cmp = spt_compare(S, 2, 3);
    ASSERT(cmp == 0, "20 == 20");

    spt_close(S);
  }

  static void testNewList() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "newlist type");
    ASSERT_EQ(0, spt_listlen(S, -1), "newlist should be empty");

    spt_close(S);
  }

  static void testListLen() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    ASSERT_EQ(0, spt_listlen(S, -1), "empty list len");

    spt_pushint(S, 1);
    spt_listappend(S, -2);
    ASSERT_EQ(1, spt_listlen(S, -1), "list len after append");

    spt_close(S);
  }

  static void testListAppend() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 10);
    spt_listappend(S, -2);
    spt_pushint(S, 20);
    spt_listappend(S, -2);
    spt_pushint(S, 30);
    spt_listappend(S, -2);

    ASSERT_EQ(3, spt_listlen(S, -1), "list len after 3 appends");

    spt_listgeti(S, -1, 0);
    ASSERT_EQ(10, spt_toint(S, -1), "list[0]");
    spt_pop(S, 1);

    spt_listgeti(S, -1, 2);
    ASSERT_EQ(30, spt_toint(S, -1), "list[2]");

    spt_close(S);
  }

  static void testListGetSet() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 100);
    spt_listappend(S, -2);
    spt_pushint(S, 200);
    spt_listappend(S, -2);

    spt_listgeti(S, -1, 0);
    ASSERT_EQ(100, spt_toint(S, -1), "get list[0]");
    spt_pop(S, 1);

    spt_pushint(S, 999);
    spt_listseti(S, -2, 0);

    spt_listgeti(S, -1, 0);
    ASSERT_EQ(999, spt_toint(S, -1), "list[0] after set");

    spt_close(S);
  }

  static void testListInsert() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 1);
    spt_listappend(S, -2);
    spt_pushint(S, 3);
    spt_listappend(S, -2);

    spt_pushint(S, 2);
    spt_listinsert(S, -2, 1);

    ASSERT_EQ(3, spt_listlen(S, -1), "list len after insert");

    spt_listgeti(S, -1, 1);
    ASSERT_EQ(2, spt_toint(S, -1), "inserted element");

    spt_close(S);
  }

  static void testListRemove() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 1);
    spt_listappend(S, -2);
    spt_pushint(S, 2);
    spt_listappend(S, -2);
    spt_pushint(S, 3);
    spt_listappend(S, -2);

    spt_listremove(S, -1, 1);
    ASSERT_EQ(2, spt_toint(S, -1), "removed element should be 2");
    spt_pop(S, 1);

    ASSERT_EQ(2, spt_listlen(S, -1), "list len after remove");

    spt_close(S);
  }

  static void testListClear() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 1);
    spt_listappend(S, -2);
    spt_pushint(S, 2);
    spt_listappend(S, -2);

    spt_listclear(S, -1);
    ASSERT_EQ(0, spt_listlen(S, -1), "list len after clear");

    spt_close(S);
  }

  static void testNewMap() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    ASSERT_EQ(SPT_TMAP, spt_type(S, -1), "newmap type");
    ASSERT_EQ(0, spt_maplen(S, -1), "newmap should be empty");

    spt_close(S);
  }

  static void testMapLen() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);

    spt_pushstring(S, "key");
    spt_pushint(S, 100);
    spt_setmap(S, -3);

    ASSERT_EQ(1, spt_maplen(S, -1), "map len after set");

    spt_close(S);
  }

  static void testMapGetSet() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);

    spt_pushstring(S, "name");
    spt_pushstring(S, "Alice");
    spt_setmap(S, -3);

    spt_pushstring(S, "age");
    spt_pushint(S, 30);
    spt_setmap(S, -3);

    ASSERT_EQ(2, spt_maplen(S, -1), "map should have 2 entries");

    spt_pushstring(S, "name");
    spt_getmap(S, -2);
    ASSERT_EQ(SPT_TSTRING, spt_type(S, -1), "name should be string");
    ASSERT_STR_EQ("Alice", spt_tostring(S, -1, nullptr), "name value");
    spt_pop(S, 1);

    spt_pushstring(S, "age");
    spt_getmap(S, -2);
    ASSERT_EQ(30, spt_toint(S, -1), "age value");

    spt_close(S);
  }

  static void testFieldGetSet() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);

    spt_pushint(S, 42);
    spt_setfield(S, -2, "value");

    spt_getfield(S, -1, "value");
    ASSERT_EQ(42, spt_toint(S, -1), "field value");

    spt_getfield(S, -2, "nonexistent");
    ASSERT_EQ(SPT_TNIL, spt_type(S, -1), "nonexistent field should be nil");

    spt_close(S);
  }

  static void testHasKey() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    spt_pushint(S, 100);
    spt_setfield(S, -2, "exists");

    spt_pushstring(S, "exists");
    ASSERT(spt_haskey(S, -2), "should have key 'exists'");

    spt_pushstring(S, "missing");
    ASSERT(!spt_haskey(S, -2), "should not have key 'missing'");

    spt_close(S);
  }

  static void testMapRemove() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    spt_pushint(S, 100);
    spt_setfield(S, -2, "key");

    spt_pushstring(S, "key");
    spt_mapremove(S, -2);
    ASSERT_EQ(100, spt_toint(S, -1), "removed value");
    spt_pop(S, 1);

    ASSERT_EQ(0, spt_maplen(S, -1), "map should be empty after remove");

    spt_close(S);
  }

  static void testMapClear() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    spt_pushint(S, 1);
    spt_setfield(S, -2, "a");
    spt_pushint(S, 2);
    spt_setfield(S, -2, "b");

    spt_mapclear(S, -1);
    ASSERT_EQ(0, spt_maplen(S, -1), "map should be empty after clear");

    spt_close(S);
  }

  static void testMapKeysValues() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    spt_pushint(S, 10);
    spt_setfield(S, -2, "a");
    spt_pushint(S, 20);
    spt_setfield(S, -2, "b");

    spt_mapkeys(S, -1);
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "mapkeys should return list");
    ASSERT_EQ(2, spt_listlen(S, -1), "should have 2 keys");
    spt_pop(S, 1);

    spt_mapvalues(S, -1);
    ASSERT_EQ(SPT_TLIST, spt_type(S, -1), "mapvalues should return list");
    ASSERT_EQ(2, spt_listlen(S, -1), "should have 2 values");

    spt_close(S);
  }

  static void testMapNext() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);
    spt_pushint(S, 10);
    spt_setfield(S, -2, "a");
    spt_pushint(S, 20);
    spt_setfield(S, -2, "b");

    int count = 0;
    spt_pushnil(S);
    while (spt_mapnext(S, -2)) {
      count++;
      spt_pop(S, 1);
    }

    ASSERT_EQ(2, count, "should iterate 2 entries");

    spt_close(S);
  }

  static void testRawGetSet() {
    spt_State *S = spt_newstate();

    spt_newmap(S, 0);

    spt_pushstring(S, "key");
    spt_pushint(S, 999);
    spt_rawset(S, -3);

    spt_pushstring(S, "key");
    spt_rawget(S, -2);
    ASSERT_EQ(999, spt_toint(S, -1), "rawget value");

    spt_close(S);
  }

  static void testNewClass() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "MyClass");
    ASSERT_EQ(SPT_TCLASS, spt_type(S, -1), "newclass type");

    const char *name = spt_classname(S, -1);
    ASSERT_STR_EQ("MyClass", name, "class name");

    spt_close(S);
  }

  static void testBindMethod() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "TestClass");

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 42);
      return 1;
    });
    spt_bindmethod(S, -2, "getValue");

    spt_close(S);
  }

  static void testBindStatic() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "TestClass");
    spt_pushint(S, 100);
    spt_bindstatic(S, -2, "CONSTANT");

    spt_close(S);
  }

  static void testNewInstance() {

    spt_State *S = spt_newstate();

    spt_newclass(S, "Person");

    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    ASSERT_EQ(SPT_TOBJECT, spt_type(S, -1), "instance type");

    spt_close(S);
  }

  static void testPropGetSet() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "TestClass");
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    spt_pushint(S, 123);
    spt_setprop(S, -2, "value");

    spt_getprop(S, -1, "value");
    ASSERT_EQ(123, spt_toint(S, -1), "property value");

    spt_close(S);
  }

  static void testHasProp() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "TestClass");
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    spt_pushint(S, 42);
    spt_setprop(S, -2, "exists");

    ASSERT(spt_hasprop(S, -1, "exists"), "should have property 'exists'");
    ASSERT(!spt_hasprop(S, -1, "missing"), "should not have property 'missing'");

    spt_close(S);
  }

  static void testGetClass() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "MyClass");
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    spt_getclass(S, -1);
    ASSERT_EQ(SPT_TCLASS, spt_type(S, -1), "getclass should return class");
    ASSERT_STR_EQ("MyClass", spt_classname(S, -1), "class name from instance");

    spt_close(S);
  }

  static void testClassName() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "TestClassName");
    const char *name = spt_classname(S, -1);
    ASSERT_STR_EQ("TestClassName", name, "classname");

    spt_close(S);
  }

  static void testIsInstance() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "Animal");
    spt_pushvalue(S, -1);
    spt_newinstance(S, 0);

    ASSERT(spt_isinstance(S, -1, 1), "should be instance of Animal");

    spt_newclass(S, "Plant");
    ASSERT(!spt_isinstance(S, -2, -1), "should not be instance of Plant");

    spt_close(S);
  }

  static void testNewCInstance() {
    spt_State *S = spt_newstate();

    struct MyData {
      int x;
      int y;
    };

    MyData *data = static_cast<MyData *>(spt_newcinstance(S, sizeof(MyData)));
    ASSERT(data != nullptr, "spt_newcinstance should return data pointer");

    data->x = 10;
    data->y = 20;

    ASSERT_EQ(SPT_TCINSTANCE, spt_type(S, -1), "cinstance type");

    MyData *retrieved = static_cast<MyData *>(spt_getcinstancedata(S, -1));
    ASSERT(retrieved == data, "getcinstancedata should return same pointer");
    ASSERT_EQ(10, retrieved->x, "data.x");
    ASSERT_EQ(20, retrieved->y, "data.y");

    spt_close(S);
  }

  static void testNewCInstanceOf() {
    spt_State *S = spt_newstate();

    spt_newclass(S, "Vector");

    struct Vec2 {
      float x, y;
    };

    Vec2 *vec = static_cast<Vec2 *>(spt_newcinstanceof(S, sizeof(Vec2)));
    ASSERT(vec != nullptr, "newcinstanceof should return data");

    vec->x = 1.0f;
    vec->y = 2.0f;

    ASSERT_EQ(SPT_TCINSTANCE, spt_type(S, -1), "type should be cinstance");

    spt_close(S);
  }

  static void testGetCInstanceData() {
    spt_State *S = spt_newstate();

    int *data = static_cast<int *>(spt_newcinstance(S, sizeof(int)));
    *data = 12345;

    int *retrieved = static_cast<int *>(spt_getcinstancedata(S, -1));
    ASSERT_EQ(12345, *retrieved, "cinstance data value");

    spt_pushint(S, 42);
    void *ptr = spt_getcinstancedata(S, -1);
    ASSERT(ptr == nullptr, "getcinstancedata on int should return NULL");

    spt_close(S);
  }

  static void testPushCClosure() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 100);
    spt_pushcclosure(
        S,
        [](spt_State *S) -> int {
          spt_pushstring(S, "closure called");
          return 1;
        },
        1);

    ASSERT_EQ(SPT_TCLOSURE, spt_type(S, -1), "cclosure type");
    ASSERT(spt_iscfunction(S, -1), "should be cfunction");
    ASSERT(spt_isfunction(S, -1), "should be function");

    spt_close(S);
  }

  static void testPushCFunction() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 999);
      return 1;
    });

    ASSERT(spt_isfunction(S, -1), "should be function");
    ASSERT(spt_iscfunction(S, -1), "should be cfunction");

    spt_close(S);
  }

  static void testUpvalues() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    spt_pushstring(S, "hello");
    spt_pushcclosure(
        S,
        [](spt_State *S) -> int {
          spt_pushstring(S, "test");
          return 1;
        },
        2);

    int count = spt_getupvaluecount(S, -1);

    ASSERT(count >= 2, "should have at least 2 upvalues");

    spt_close(S);
  }

  static void testCompile() {
    spt_Ast *ast = spt_parse("var x = 42;", "test");
    ASSERT(ast != nullptr, "parse should succeed");

    spt_Compiler *compiler = spt_newcompiler("test", "test");
    spt_Chunk *chunk = spt_compile(compiler, ast);
    ASSERT(chunk != nullptr, "compile should succeed");
    ASSERT(!spt_compilerhaserror(compiler), "compiler should have no errors");

    spt_freechunk(chunk);
    spt_freecompiler(compiler);
    spt_freeast(ast);
  }

  static void testLoadFile() {

    spt_State *S = spt_newstate();

    spt_Chunk *chunk = spt_loadfile(S, "any_file.spt");

    ASSERT(chunk == nullptr, "loadfile is broken - always returns NULL (known bug)");

    spt_close(S);
  }

  static void testCall() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_Int a = spt_toint(S, 1);
      spt_Int b = spt_toint(S, 2);
      spt_pushint(S, a + b);
      return 1;
    });

    spt_pushint(S, 10);
    spt_pushint(S, 20);

    int result = spt_call(S, 2, 1);
    ASSERT_EQ(SPT_OK, result, "call should succeed");

    ASSERT_EQ(30, spt_toint(S, -1), "return value should be 30");

    spt_close(S);
  }

  static void testPCall() {
    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_pushint(S, 42);
      return 1;
    });

    int result = spt_pcall(S, 0, 1, 0);
    ASSERT_EQ(SPT_OK, result, "pcall should succeed");
    ASSERT_EQ(42, spt_toint(S, -1), "return value");

    spt_close(S);
  }

  static void testGlobals() {
    spt_State *S = spt_newstate();

    spt_dostring(S, "global int testGlobal = 0;", "init");

    spt_pushint(S, 12345);
    spt_setglobal(S, "testGlobal");

    spt_getglobal(S, "testGlobal");
    ASSERT_EQ(12345, spt_toint(S, -1), "global value");

    spt_close(S);
  }

  static void testHasGlobal() {
    spt_State *S = spt_newstate();

    spt_dostring(S, "global int exists = 1;", "init");

    ASSERT(spt_hasglobal(S, "exists"), "should have global 'exists'");
    ASSERT(!spt_hasglobal(S, "missing"), "should not have global 'missing'");

    spt_close(S);
  }

  static void testReferences() {
    spt_State *S = spt_newstate();

    spt_pushstring(S, "referenced value");
    int ref = spt_ref(S);
    ASSERT(ref != SPT_NOREF, "ref should be valid");
    ASSERT(ref != SPT_REFNIL, "ref should not be nil ref");

    spt_getref(S, ref);
    ASSERT_EQ(SPT_TSTRING, spt_type(S, -1), "ref should be string");
    ASSERT_STR_EQ("referenced value", spt_tostring(S, -1, nullptr), "ref value");

    spt_unref(S, ref);

    spt_close(S);
  }

  static void testError() {

    spt_State *S = spt_newstate();

    spt_pushcfunction(S, [](spt_State *S) -> int {
      spt_error(S, "Test error: %d", 42);
      return 0;
    });

    int result = spt_pcall(S, 0, 0, 0);
    ASSERT(result != SPT_OK, "pcall should catch error");

    spt_close(S);
  }

  static void testGC() {
    spt_State *S = spt_newstate();

    int memKB = spt_gc(S, SPT_GCCOUNT, 0);
    ASSERT(memKB >= 0, "memory count should be non-negative");

    spt_gc(S, SPT_GCCOLLECT, 0);

    int running = spt_gc(S, SPT_GCISRUNNING, 0);

    spt_close(S);
  }

  static void testLen() {
    spt_State *S = spt_newstate();

    spt_pushstring(S, "hello");
    ASSERT_EQ(5, (int64_t)spt_len(S, -1), "string len");

    spt_newlist(S, 0);
    spt_pushint(S, 1);
    spt_listappend(S, -2);
    spt_pushint(S, 2);
    spt_listappend(S, -2);
    ASSERT_EQ(2, (int64_t)spt_len(S, -1), "list len");

    spt_newmap(S, 0);
    spt_pushint(S, 1);
    spt_setfield(S, -2, "a");
    ASSERT_EQ(1, (int64_t)spt_len(S, -1), "map len");

    spt_close(S);
  }

  static void testConcat() {
    spt_State *S = spt_newstate();

    spt_pushstring(S, "Hello");
    spt_pushstring(S, " ");
    spt_pushstring(S, "World");

    spt_concat(S, 3);

    ASSERT_EQ(1, spt_gettop(S), "concat should leave 1 element");
    ASSERT_STR_EQ("Hello World", spt_tostring(S, -1, nullptr), "concat result");

    spt_close(S);
  }

  static void testCheckFunctions() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    spt_pushfloat(S, 3.14);
    spt_pushstring(S, "test");

    ASSERT_EQ(42, spt_checkint(S, 1), "checkint");
    ASSERT_FLOAT_EQ(3.14, spt_checkfloat(S, 2), "checkfloat");

    size_t len;
    const char *str = spt_checkstring(S, 3, &len);
    ASSERT_STR_EQ("test", str, "checkstring");
    ASSERT_EQ(4, (int64_t)len, "checkstring len");

    spt_close(S);
  }

  static void testOptFunctions() {
    spt_State *S = spt_newstate();

    spt_pushint(S, 42);
    spt_pushnil(S);

    ASSERT_EQ(42, spt_optint(S, 1, 0), "optint with value");
    ASSERT_EQ(999, spt_optint(S, 2, 999), "optint with default");
    ASSERT_EQ(100, spt_optint(S, 3, 100), "optint out of range");

    spt_close(S);
  }

  static void testListIteration() {
    spt_State *S = spt_newstate();

    spt_newlist(S, 0);
    spt_pushint(S, 10);
    spt_listappend(S, -2);
    spt_pushint(S, 20);
    spt_listappend(S, -2);
    spt_pushint(S, 30);
    spt_listappend(S, -2);

    int iter = spt_listiter(S, -1);
    ASSERT(iter >= 0, "listiter should return valid state");

    int64_t sum = 0;
    while (spt_listnext(S, -1, &iter)) {
      sum += spt_toint(S, -1);
      spt_pop(S, 1);
    }

    ASSERT_EQ(60, sum, "sum of list elements");

    spt_close(S);
  }
};

inline int runSptCApiDirectTests() {
  SptCApiDirectTests tests;
  return tests.runAll();
}