/**
 * TestIter.c — 验证 iter() 内置函数 + ipairs 修复
 *
 * 覆盖:
 *   - iter(2, list) 在 for-each 中 2 变量迭代 (key + value)
 *   - iter(2, map)  在 for-each 中 2 变量迭代 (key + value)
 *   - iter 错误处理: nvars 越界、不支持的类型
 *   - iter 空集合: 空列表、空 map
 *   - ipairs 修复: 返回 4 值 (SPT 协议)、list 越界不崩溃
 *
 * 注意: Phase 1 仅测试显式 iter() 调用; parser 自动包语法糖在 Phase 2 实现。
 */

#include "lauxlib.h"
#include "lua.h"
#include "lualib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST(name) printf("Testing: %s... ", name)
#define PASS() printf("PASS\n")
#define FAIL(msg)                                                                                  \
  do {                                                                                             \
    printf("FAIL: %s\n", msg);                                                                     \
    failed++;                                                                                      \
  } while (0)

static int failed = 0;

static void *test_alloc(void *ud, void *ptr, size_t osize, size_t nsize) {
  (void)ud;
  (void)osize;
  if (nsize == 0) {
    free(ptr);
    return NULL;
  }
  return realloc(ptr, nsize);
}

/* 运行 SPT 代码; 返回 0=成功(LUA_OK), 非0=错误 */
static int run_spt(lua_State *L, const char *code) {
  int status = luaL_dostring(L, code);
  if (status != LUA_OK) {
    /* 错误消息在栈顶, 弹出避免累积 */
    lua_pop(L, 1);
  }
  return status;
}

int main(void) {
  lua_State *L = lua_newstate(test_alloc, NULL, 0);
  luaL_openlibs(L);

  printf("=== Testing iter() builtin + ipairs fix ===\n\n");

  /* ---- 1. iter(2, list) 2 变量 ---- */
  TEST("iter_list_2var");
  {
    const char *code = "list<int> l = [10, 20, 30];\n"
                       "int sk = 0; int sv = 0;\n"
                       "for (auto k, v : iter(2, l)) { sk += k; sv += v; }\n"
                       "assert(sk == 3, \"keys 0+1+2\");\n"
                       "assert(sv == 60, \"vals 10+20+30\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("iter list 2var failed");
    else
      PASS();
  }

  /* ---- 2. iter(2, list) 返回 4 值 ---- */
  TEST("iter_list_returns_4");
  {
    /* iter 必须返回 (func, state, closing_var, control) 4 值;
       for-each 期望 4 个表达式 (compile_exprlist_n + adjust 4)。
       若只返回 3 值, closing_var 会是 nil 导致 tbc 错误。 */
    const char *code = "list<int> l = [1, 2];\n"
                       "int count = 0;\n"
                       "for (auto k, v : iter(2, l)) { count += 1; }\n"
                       "assert(count == 2, \"iter returns 4 values for SPT protocol\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("iter list 4-value protocol failed");
    else
      PASS();
  }

  /* ---- 3. iter(2, map) 2 变量 ---- */
  TEST("iter_map_2var");
  {
    const char *code = "map<int, int> m = {};\n"
                       "m[1] = 100; m[2] = 200; m[3] = 300;\n"
                       "int sk = 0; int sv = 0;\n"
                       "for (auto k, v : iter(2, m)) { sk += k; sv += v; }\n"
                       "assert(sk == 6, \"keys 1+2+3\");\n"
                       "assert(sv == 600, \"vals 100+200+300\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("iter map 2var failed");
    else
      PASS();
  }

  /* ---- 4. iter 错误: nvars=0 ---- */
  TEST("iter_bad_nvars_0");
  {
    const char *code = "list<int> l = [1];\n"
                       "for (auto k, v : iter(0, l)) { }\n";
    if (run_spt(L, code) == LUA_OK)
      FAIL("iter(0, l) should error");
    else
      PASS();
  }

  /* ---- 5. iter 错误: nvars=3 ---- */
  TEST("iter_bad_nvars_3");
  {
    const char *code = "list<int> l = [1];\n"
                       "for (auto k, v : iter(3, l)) { }\n";
    if (run_spt(L, code) == LUA_OK)
      FAIL("iter(3, l) should error");
    else
      PASS();
  }

  /* ---- 6. iter 错误: 不支持的类型 (int) ---- */
  TEST("iter_bad_type_int");
  {
    const char *code = "for (auto k, v : iter(2, 42)) { }\n";
    if (run_spt(L, code) == LUA_OK)
      FAIL("iter(2, 42) should error");
    else
      PASS();
  }

  /* ---- 7. iter 错误: 不支持的类型 (string) ---- */
  TEST("iter_bad_type_str");
  {
    const char *code = "for (auto k, v : iter(2, \"hello\")) { }\n";
    if (run_spt(L, code) == LUA_OK)
      FAIL("iter(2, string) should error");
    else
      PASS();
  }

  /* ---- 8. iter 空列表 ---- */
  TEST("iter_empty_list");
  {
    const char *code = "list<int> l = [];\n"
                       "int count = 0;\n"
                       "for (auto k, v : iter(2, l)) { count += 1; }\n"
                       "assert(count == 0, \"empty list 0 iterations\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("iter empty list failed");
    else
      PASS();
  }

  /* ---- 9. iter 空 map ---- */
  TEST("iter_empty_map");
  {
    const char *code = "map<int, int> m = {};\n"
                       "int count = 0;\n"
                       "for (auto k, v : iter(2, m)) { count += 1; }\n"
                       "assert(count == 0, \"empty map 0 iterations\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("iter empty map failed");
    else
      PASS();
  }

  /* ---- 10. ipairs 返回 4 值 (SPT 协议) ---- */
  TEST("ipairs_4values");
  {
    /* ipairs 必须返回 (func, state, closing_var, control) 4 值。
       若只返回 3 值, closing_var 为 nil 导致 for-each 崩溃。
       list 的 ipairs 之前会因 lua_geti 越界报错, 现已修复。 */
    const char *code = "list<int> l = [10, 20, 30];\n"
                       "int count = 0; int sv = 0;\n"
                       "for (auto i, v : ipairs(l)) { count += 1; sv += v; }\n"
                       "assert(count == 3, \"ipairs list 3 iterations\");\n"
                       "assert(sv == 60, \"ipairs list vals 10+20+30\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("ipairs 4-value protocol over list failed");
    else
      PASS();
  }

  /* ---- 11. ipairs 对 map (hash table) ---- */
  TEST("ipairs_map");
  {
    /* ipairs 对 map: lua_geti 对 map 不越界报错 (返回 nil)。
       map 的 array 部分为空, ipairs 应立即停止。 */
    const char *code = "map<int, int> m = {};\n"
                       "m[5] = 50; m[10] = 100;\n"
                       "int count = 0;\n"
                       "for (auto i, v : ipairs(m)) { count += 1; }\n"
                       "assert(count == 0, \"ipairs map stops at first nil\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("ipairs over map failed");
    else
      PASS();
  }

  /* ---- 12. iter 单元素列表 ---- */
  TEST("iter_single_element");
  {
    const char *code = "list<int> l = [42];\n"
                       "int sk = 0; int sv = 0;\n"
                       "for (auto k, v : iter(2, l)) { sk = k; sv = v; }\n"
                       "assert(sk == 0, \"single element key 0\");\n"
                       "assert(sv == 42, \"single element value 42\");\n";
    if (run_spt(L, code) != LUA_OK)
      FAIL("iter single element failed");
    else
      PASS();
  }

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
