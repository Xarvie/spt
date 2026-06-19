/**
 * 测试新增的 C API
 * 验证 lua.h 中新增的扩展数组操作 API 和表模式查询 API
 */

#include "lua.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) printf("Testing: %s... ", name)
#define PASS() printf("PASS\n")
#define FAIL(msg)                                                                                  \
  printf("FAIL: %s\n", msg);                                                                       \
  failed++

static int failed = 0;

/* Minimal lua_Alloc for testing */
static void *test_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  } else {
    return realloc(ptr, nsize);
  }
}

int main() {
  lua_State *L = lua_newstate(test_alloc, NULL, 0);

  printf("=== Testing New C API ===\n\n");

  // =======================================================
  // Test lua_createarray
  // =======================================================
  TEST("lua_createarray");
  lua_createarray(L, 10);
  if (lua_type(L, -1) != LUA_TARRAY) {
    FAIL("should create array type");
  } else {
    lua_Integer len = lua_arraylen(L, -1);
    if (len != 0) {
      FAIL("initial length should be 0");
    } else {
      PASS();
    }
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_arraycapacity
  // =======================================================
  TEST("lua_arraycapacity");
  lua_createarray(L, 100);
  lua_Integer cap = lua_arraycapacity(L, -1);
  if (cap != 100) {
    FAIL("capacity should be 100");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_arrayisempty
  // =======================================================
  TEST("lua_arrayisempty");
  lua_createarray(L, 0);
  if (lua_arrayisempty(L, -1) != 1) {
    FAIL("empty array should return 1");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  lua_createarray(L, 10);
  lua_pushinteger(L, 42);
  lua_seti(L, -2, 0);
  if (lua_arrayisempty(L, -1) != 0) {
    FAIL("non-empty array should return 0");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_arrayreserve
  // =======================================================
  TEST("lua_arrayreserve");
  lua_createarray(L, 10);
  lua_arrayreserve(L, -1, 100);
  lua_Integer newCap = lua_arraycapacity(L, -1);
  if (newCap < 100) {
    FAIL("capacity should be at least 100");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_getarrayrange
  // =======================================================
  TEST("lua_getarrayrange");
  lua_createarray(L, 10);
  lua_pushinteger(L, 10);
  lua_seti(L, -2, 0);
  lua_pushinteger(L, 20);
  lua_seti(L, -2, 1);
  lua_pushinteger(L, 30);
  lua_seti(L, -2, 2);
  lua_arraysetlen(L, -1, 3);
  
  lua_getarrayrange(L, -1, 0, 3);
  if (lua_gettop(L) != 4) { /* array + 3 values */
    FAIL("should push 3 values");
  } else {
    lua_Integer v1 = lua_tointeger(L, -3);
    lua_Integer v2 = lua_tointeger(L, -2);
    lua_Integer v3 = lua_tointeger(L, -1);
    if (v1 != 10 || v2 != 20 || v3 != 30) {
      FAIL("values should be 10, 20, 30");
    } else {
      PASS();
    }
  }
  lua_pop(L, 4);

  // =======================================================
  // Test lua_setarrayrange
  // =======================================================
  TEST("lua_setarrayrange");
  lua_createarray(L, 10);
  lua_pushinteger(L, 100);
  lua_pushinteger(L, 200);
  lua_pushinteger(L, 300);
  lua_setarrayrange(L, -4, 0, 3); /* array at -4, set 3 values starting at 0 */
  
  lua_Integer len = lua_arraylen(L, -1);
  if (len != 3) {
    FAIL("length should be 3");
  } else {
    lua_geti(L, -1, 0);
    lua_Integer v1 = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_geti(L, -1, 1);
    lua_Integer v2 = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_geti(L, -1, 2);
    lua_Integer v3 = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (v1 != 100 || v2 != 200 || v3 != 300) {
      FAIL("values should be 100, 200, 300");
    } else {
      PASS();
    }
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_movearray
  // =======================================================
  TEST("lua_movearray");
  lua_createarray(L, 10);
  lua_pushinteger(L, 1);
  lua_pushinteger(L, 2);
  lua_pushinteger(L, 3);
  lua_setarrayrange(L, -4, 0, 3);
  
  lua_createarray(L, 10);
  lua_movearray(L, -2, -1, 0, 0, 3); /* move from first array to second */
  
  lua_Integer len2 = lua_arraylen(L, -1);
  if (len2 != 3) {
    FAIL("destination length should be 3");
  } else {
    lua_geti(L, -1, 0);
    lua_Integer v1 = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_geti(L, -1, 1);
    lua_Integer v2 = lua_tointeger(L, -1);
    lua_pop(L, 1);
    lua_geti(L, -1, 2);
    lua_Integer v3 = lua_tointeger(L, -1);
    lua_pop(L, 1);
    if (v1 != 1 || v2 != 2 || v3 != 3) {
      FAIL("values should be 1, 2, 3");
    } else {
      PASS();
    }
  }
  lua_pop(L, 2);

  // =======================================================
  // Test lua_nextarray
  // =======================================================
  TEST("lua_nextarray");
  lua_createarray(L, 10);
  lua_pushinteger(L, 10);
  lua_seti(L, -2, 0);
  lua_pushinteger(L, 20);
  lua_seti(L, -2, 1);
  lua_pushinteger(L, 30);
  lua_seti(L, -2, 2);
  lua_arraysetlen(L, -1, 3);
  
  lua_Integer cursor = -1;
  int count = 0;
  lua_Integer sum = 0;
  while (lua_nextarray(L, -1, &cursor)) {
    lua_Integer key = lua_tointeger(L, -2);
    lua_Integer val = lua_tointeger(L, -1);
    sum += val;
    count++;
    lua_pop(L, 2); /* pop key and value */
  }
  
  if (count != 3 || sum != 60) {
    FAIL("should iterate 3 elements with sum 60");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_gettablemode
  // =======================================================
  TEST("lua_gettablemode");

  // Array mode
  lua_createarray(L, 0);
  int mode = lua_gettablemode(L, -1);
  if (mode != 1) { // TABLE_ARRAY
    FAIL("array mode should be 1");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // Map mode
  lua_createtable(L, 0, 5);
  mode = lua_gettablemode(L, -1);
  if (mode != 2) { // TABLE_MAP
    FAIL("map mode should be 2");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_ismap
  // =======================================================
  TEST("lua_ismap");

  lua_createarray(L, 0);
  if (lua_ismap(L, -1) != 0) {
    FAIL("array should not be map");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  lua_createtable(L, 0, 5);
  if (lua_ismap(L, -1) != 1) {
    FAIL("table should be map");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_rawlen for array
  // =======================================================
  TEST("lua_rawlen for array");
  lua_createarray(L, 10);
  lua_pushinteger(L, 1);
  lua_seti(L, -2, 0);
  lua_pushinteger(L, 2);
  lua_seti(L, -2, 1);
  lua_pushinteger(L, 3);
  lua_seti(L, -2, 2);
  lua_arraysetlen(L, -1, 3);
  
  lua_Unsigned rawlen_val = lua_rawlen(L, -1);
  if (rawlen_val != 3) {
    FAIL("rawlen should be 3");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Integration test: Create array, manipulate, verify
  // =======================================================
  TEST("Integration test");
  lua_createarray(L, 10);

  // Set some values
  lua_pushinteger(L, 10);
  lua_seti(L, -2, 0);
  lua_pushinteger(L, 20);
  lua_seti(L, -2, 1);
  lua_pushinteger(L, 30);
  lua_seti(L, -2, 2);

  // Verify length
  lua_Integer len_int = lua_arraylen(L, -1);
  if (len_int != 0) {
    // Note: seti doesn't update loglen in current implementation
    // This is expected behavior - loglen is only updated via arraysetlen or push
    lua_arraysetlen(L, -1, 3);
  }

  lua_Integer len3 = lua_arraylen(L, -1);
  if (len3 != 3) {
    FAIL("length should be 3 after setlen");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test overlapping movearray
  // =======================================================
  TEST("lua_movearray overlapping");
  lua_createarray(L, 20);
  lua_pushinteger(L, 1);
  lua_pushinteger(L, 2);
  lua_pushinteger(L, 3);
  lua_pushinteger(L, 4);
  lua_pushinteger(L, 5);
  lua_setarrayrange(L, -6, 0, 5);
  
  /* Move elements from 0-4 to 2-6 (overlapping) */
  lua_movearray(L, -1, -1, 0, 2, 5);
  
  lua_geti(L, -1, 2);
  lua_Integer v1 = lua_tointeger(L, -1);
  lua_pop(L, 1);
  lua_geti(L, -1, 3);
  lua_Integer v2 = lua_tointeger(L, -1);
  lua_pop(L, 1);
  lua_geti(L, -1, 4);
  lua_Integer v3 = lua_tointeger(L, -1);
  lua_pop(L, 1);
  lua_geti(L, -1, 5);
  lua_Integer v4 = lua_tointeger(L, -1);
  lua_pop(L, 1);
  lua_geti(L, -1, 6);
  lua_Integer v5 = lua_tointeger(L, -1);
  lua_pop(L, 1);
  
  if (v1 != 1 || v2 != 2 || v3 != 3 || v4 != 4 || v5 != 5) {
    FAIL("overlapping move should preserve values");
  } else {
    PASS();
  }
  lua_pop(L, 1);

  // =======================================================
  // Test lua_getarrayrange with empty slots
  // =======================================================
  TEST("lua_getarrayrange with empty slots");
  lua_createarray(L, 10);
  lua_arraysetlen(L, -1, 3); /* Set logical length first */
  lua_pushinteger(L, 100);
  lua_seti(L, -2, 0);
  /* Skip index 1 (leave it empty/nil) */
  lua_pushinteger(L, 300);
  lua_seti(L, -2, 2);
  
  lua_getarrayrange(L, -1, 0, 3);
  if (lua_gettop(L) != 4) {
    FAIL("should push 3 values");
  } else {
    lua_Integer v1 = lua_tointeger(L, -3);
    int t2 = lua_type(L, -2);
    lua_Integer v3 = lua_tointeger(L, -1);
    printf("[v1=%lld, t2=%d, v3=%lld] ", (long long)v1, t2, (long long)v3);
    if (v1 != 100 || t2 != LUA_TNIL || v3 != 300) {
      FAIL("empty slot should return nil");
    } else {
      PASS();
    }
  }
  lua_pop(L, 4);

  lua_close(L);

  printf("\n=== Test Summary ===\n");
  if (failed == 0) {
    printf("All tests PASSED!\n");
    return 0;
  } else {
    printf("%d test(s) FAILED!\n", failed);
    return 1;
  }
}