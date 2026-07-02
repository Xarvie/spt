/*
** test_phase5.c — Phase 5 性能基建端到端测试。
**
** 覆盖：
**   5a. 哈希符号索引（sem_find_function / find_class_by_name 查哈希命中）
**   5b. 引用倒排索引（workspace_find_occurrences 跨文件查表）
**   5c. 模块依赖图（workspace_find_importers 反向查询）
**   5d. 增量失效（workspace_mark_doc_dirty 单文档重建，不漏不重）
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "documents.h"
#include "lsp_features.h"
#include "semantic.h"
#include "server.h"
#include "spt_lsp_bridge.h"
#include "workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failed = 0;
#define CHECK(cond, msg)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("  FAIL: %s\n", msg);                                                                 \
      failed++;                                                                                    \
    }                                                                                              \
  } while (0)

static void sink_emit(void *ctx, cJSON *m) {
  (void)ctx;
  cJSON_Delete(m);
}

static cJSON *make_req(int id, const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", id);
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  return m;
}

static void write_file(const char *dir, const char *name, const char *content) {
  char path[4096];
#ifdef _WIN32
  snprintf(path, sizeof path, "%s\\%s", dir, name);
#else
  snprintf(path, sizeof path, "%s/%s", dir, name);
#endif
  FILE *f = fopen(path, "wb");
  if (f) {
    fputs(content, f);
    fclose(f);
  }
}

static char *make_temp_dir(char *out, size_t cap) {
#ifdef _WIN32
  char base[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, base))
    return NULL;
  snprintf(out, cap, "%ssptp5_%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(out, NULL))
    return NULL;
  return out;
#else
  (void)cap;
  snprintf(out, cap, "/tmp/sptp5_%d", (int)getpid());
  if (mkdir(out, 0777) != 0)
    return NULL;
  return out;
#endif
}

static void remove_dir_recursive(const char *path) {
#ifdef _WIN32
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof pattern, "%s\\*", path);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h != INVALID_HANDLE_VALUE) {
    do {
      const char *nm = fd.cFileName;
      if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
        continue;
      char full[MAX_PATH];
      snprintf(full, sizeof full, "%s\\%s", path, nm);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        remove_dir_recursive(full);
      else
        DeleteFileA(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryA(path);
#else
  char cmd[4200];
  snprintf(cmd, sizeof cmd, "rm -rf %s", path);
  if (system(cmd) != 0) { /* best-effort */
  }
#endif
}

/* ---- 5b/5c 回调收集器 ---- */
typedef struct {
  char uris[16][4096];
  int count;
} OccCollector;

static void occ_cb(void *ctx, const char *uri, size_t off, int len) {
  (void)off;
  (void)len;
  OccCollector *c = (OccCollector *)ctx;
  if (c->count < 16) {
    strncpy(c->uris[c->count], uri, 4095);
    c->uris[c->count][4095] = '\0';
    c->count++;
  }
}

static void imp_cb(void *ctx, const char *importer_uri) {
  OccCollector *c = (OccCollector *)ctx;
  if (c->count < 16) {
    strncpy(c->uris[c->count], importer_uri, 4095);
    c->uris[c->count][4095] = '\0';
    c->count++;
  }
}

static int occ_has_uri(OccCollector *c, const char *uri) {
  for (int i = 0; i < c->count; i++)
    if (strcmp(c->uris[i], uri) == 0)
      return 1;
  return 0;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("=== TestPhase5: performance infrastructure ===\n");

  char tmpl[4096];
  char *dir = make_temp_dir(tmpl, sizeof tmpl);
  CHECK(dir != NULL, "make_temp_dir ok");
  if (!dir) {
    printf("=== abort ===\n");
    return 1;
  }

  /* 项目结构：
     mod.spt    — 定义 foo / class Bar
     user.spt   — import { foo } from "./mod"; 调用 foo
     user2.spt  — import * as M from "./mod"; 调用 M.foo
  */
  write_file(dir, "mod.spt",
             "int foo(int n) { return n + 1; }\n"
             "class Bar {\n"
             "  int x;\n"
             "  int get() { return x; }\n"
             "}\n");
  write_file(dir, "user.spt",
             "import { foo } from \"./mod\";\n"
             "int main() {\n"
             "  return foo(42);\n"
             "}\n");
  write_file(dir, "user2.spt",
             "import * as M from \"./mod\";\n"
             "int run() {\n"
             "  return M.foo(1);\n"
             "}\n");

  LspServer srv;
  lsp_server_init(&srv);
  lsp_server_set_emit(&srv, sink_emit, NULL);

  /* initialize with rootUri */
  char rootUri[4200];
  spt_path_to_uri(dir, rootUri, sizeof rootUri);
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "rootUri", rootUri);
    cJSON *resp = lsp_dispatch(&srv, make_req(1, "initialize", params));
    CHECK(resp != NULL, "initialize response");
    if (resp)
      cJSON_Delete(resp);
  }

  /* 触发 workspace 索引构建（workspace/symbol 空查询 = 全量） */
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "query", "");
    cJSON *resp = lsp_dispatch(&srv, make_req(2, "workspace/symbol", params));
    CHECK(resp != NULL, "workspace/symbol triggers index build");
    if (resp)
      cJSON_Delete(resp);
  }

  Workspace *ws = &srv.ws;

  /* ---- 5b: 引用倒排索引 ---- */
  printf("--- 5b: ref inverted index (workspace_find_occurrences) ---\n");
  {
    /* foo 出现在 mod.spt（定义）+ user.spt（调用）+ user2.spt（M.foo）= 3 个文件 */
    OccCollector occ = {0};
    int n = workspace_find_occurrences(ws, "foo", occ_cb, &occ);
    printf("  find_occurrences(\"foo\") = %d\n", n);
    CHECK(n > 0, "foo occurrences found via index");

    char mod_uri[4096], user_uri[4096], user2_uri[4096];
    char mod_path[4096], user_path[4096], user2_path[4096];
#ifdef _WIN32
    snprintf(mod_path, sizeof mod_path, "%s\\mod.spt", dir);
    snprintf(user_path, sizeof user_path, "%s\\user.spt", dir);
    snprintf(user2_path, sizeof user2_path, "%s\\user2.spt", dir);
#else
    snprintf(mod_path, sizeof mod_path, "%s/mod.spt", dir);
    snprintf(user_path, sizeof user_path, "%s/user.spt", dir);
    snprintf(user2_path, sizeof user2_path, "%s/user2.spt", dir);
#endif
    spt_path_to_uri(mod_path, mod_uri, sizeof mod_uri);
    spt_path_to_uri(user_path, user_uri, sizeof user_uri);
    spt_path_to_uri(user2_path, user2_uri, sizeof user2_uri);

    CHECK(occ_has_uri(&occ, mod_uri), "foo occurrence in mod.spt");
    CHECK(occ_has_uri(&occ, user_uri), "foo occurrence in user.spt");
    CHECK(occ_has_uri(&occ, user2_uri), "foo occurrence in user2.spt");

    /* 不存在的符号应返回 0 */
    OccCollector occ2 = {0};
    int n2 = workspace_find_occurrences(ws, "nonexistent_xyz", occ_cb, &occ2);
    CHECK(n2 == 0, "nonexistent symbol returns 0");
  }

  /* ---- 5c: 模块依赖图 ---- */
  printf("--- 5c: dep graph (workspace_find_importers) ---\n");
  {
    /* "./mod" 被 user.spt 和 user2.spt 导入 */
    OccCollector imp = {0};
    int n = workspace_find_importers(ws, "./mod", imp_cb, &imp);
    printf("  find_importers(\"./mod\") = %d\n", n);
    CHECK(n == 2, "two importers of ./mod");

    char user_uri[4096], user2_uri[4096];
    char user_path[4096], user2_path[4096];
#ifdef _WIN32
    snprintf(user_path, sizeof user_path, "%s\\user.spt", dir);
    snprintf(user2_path, sizeof user2_path, "%s\\user2.spt", dir);
#else
    snprintf(user_path, sizeof user_path, "%s/user.spt", dir);
    snprintf(user2_path, sizeof user2_path, "%s/user2.spt", dir);
#endif
    spt_path_to_uri(user_path, user_uri, sizeof user_uri);
    spt_path_to_uri(user2_path, user2_uri, sizeof user2_uri);

    CHECK(occ_has_uri(&imp, user_uri), "user.spt imports ./mod");
    CHECK(occ_has_uri(&imp, user2_uri), "user2.spt imports ./mod");

    /* 不存在的模块应返回 0 */
    OccCollector imp2 = {0};
    int n2 = workspace_find_importers(ws, "./nonexistent", imp_cb, &imp2);
    CHECK(n2 == 0, "nonexistent module returns 0 importers");
  }

  /* ---- 5d: 增量失效 ---- */
  printf("--- 5d: incremental invalidation (workspace_mark_doc_dirty) ---\n");
  {
    /* 修改 user.spt：完全移除 foo 的 import 和调用，换成独立函数 */
    char user_uri[4096], user_path[4096];
#ifdef _WIN32
    snprintf(user_path, sizeof user_path, "%s\\user.spt", dir);
#else
    snprintf(user_path, sizeof user_path, "%s/user.spt", dir);
#endif
    spt_path_to_uri(user_path, user_uri, sizeof user_uri);

    /* 修改前：foo 在 user.spt 有出现（import + 调用） */
    OccCollector before = {0};
    workspace_find_occurrences(ws, "foo", occ_cb, &before);
    CHECK(occ_has_uri(&before, user_uri), "foo in user.spt before change");

    /* 重写 user.spt：完全移除 foo，换成 independent() */
    write_file(dir, "user.spt",
               "int independent() {\n"
               "  return 0;\n"
               "}\n");

    /* 增量失效：只重建 user.spt */
    workspace_mark_doc_dirty(ws, user_uri);

    /* 修改后：foo 不再出现在 user.spt（import 和调用都删了） */
    OccCollector after = {0};
    workspace_find_occurrences(ws, "foo", occ_cb, &after);
    CHECK(!occ_has_uri(&after, user_uri), "foo gone from user.spt after incremental update");

    char mod_uri[4096], user2_uri[4096];
    char mod_path[4096], user2_path[4096];
#ifdef _WIN32
    snprintf(mod_path, sizeof mod_path, "%s\\mod.spt", dir);
    snprintf(user2_path, sizeof user2_path, "%s\\user2.spt", dir);
#else
    snprintf(mod_path, sizeof mod_path, "%s/mod.spt", dir);
    snprintf(user2_path, sizeof user2_path, "%s/user2.spt", dir);
#endif
    spt_path_to_uri(mod_path, mod_uri, sizeof mod_uri);
    spt_path_to_uri(user2_path, user2_uri, sizeof user2_uri);
    CHECK(occ_has_uri(&after, mod_uri), "foo still in mod.spt (not invalidated)");
    CHECK(occ_has_uri(&after, user2_uri), "foo still in user2.spt (not invalidated)");

    /* 新增的 independent() 应出现在索引中 */
    OccCollector indep = {0};
    int indep_n = workspace_find_occurrences(ws, "independent", occ_cb, &indep);
    CHECK(indep_n > 0, "independent found after incremental update");
    CHECK(occ_has_uri(&indep, user_uri), "independent in user.spt after update");

    /* 依赖图：user.spt 不再导入 ./mod，只剩 user2.spt */
    OccCollector imp_after = {0};
    workspace_find_importers(ws, "./mod", imp_cb, &imp_after);
    CHECK(!occ_has_uri(&imp_after, user_uri), "user.spt no longer imports ./mod");
    CHECK(occ_has_uri(&imp_after, user2_uri), "user2.spt still imports ./mod");
    CHECK(imp_after.count == 1, "importer count decreased to 1");
  }

  /* ---- 5d 续: 增量失效 — 添加新符号 ---- */
  printf("--- 5d: incremental add new symbol ---\n");
  {
    char mod_uri[4096], mod_path[4096];
#ifdef _WIN32
    snprintf(mod_path, sizeof mod_path, "%s\\mod.spt", dir);
#else
    snprintf(mod_path, sizeof mod_path, "%s/mod.spt", dir);
#endif
    spt_path_to_uri(mod_path, mod_uri, sizeof mod_uri);

    /* 修改前：newfunc 不存在 */
    OccCollector before = {0};
    int n_before = workspace_find_occurrences(ws, "newfunc", occ_cb, &before);
    CHECK(n_before == 0, "newfunc not in index before add");

    /* 重写 mod.spt：添加 newfunc */
    write_file(dir, "mod.spt",
               "int foo(int n) { return n + 1; }\n"
               "int newfunc() { return 0; }\n"
               "class Bar {\n"
               "  int x;\n"
               "  int get() { return x; }\n"
               "}\n");

    workspace_mark_doc_dirty(ws, mod_uri);

    /* 修改后：newfunc 应在 mod.spt 出现 */
    OccCollector after = {0};
    int n_after = workspace_find_occurrences(ws, "newfunc", occ_cb, &after);
    CHECK(n_after > 0, "newfunc found after incremental update");
    CHECK(occ_has_uri(&after, mod_uri), "newfunc in mod.spt after update");

    /* foo 仍在 mod.spt（没被删） */
    OccCollector foo_after = {0};
    workspace_find_occurrences(ws, "foo", occ_cb, &foo_after);
    CHECK(occ_has_uri(&foo_after, mod_uri), "foo still in mod.spt after update");
  }

  /* ---- 5a: 哈希符号索引（通过 sem_find_function 间接验证） ---- */
  printf("--- 5a: hash symbol index (sem_find_function) ---\n");
  {
    /* 解析一个简单文件，验证 sem_find_function 能找到定义 */
    const char *text = "int alpha(int x) { return x; }\n"
                       "int beta() { return alpha(1); }\n";
    Document d;
    doc_store_init(&srv.docs);
    Document *od = doc_store_open(&srv.docs, "file:///test_hash.spt", text, strlen(text), 1);
    CHECK(od != NULL, "open test doc for hash index");
    if (od) {
      SptLspUnit *u = spt_lsp_parse(text, strlen(text));
      CHECK(u != NULL, "parse test doc");
      if (u) {
        /* sem_find_function 应找到 alpha（通过哈希索引或线性扫描） */
        const AstNode *f = sem_find_function(u, "alpha");
        CHECK(f != NULL, "sem_find_function(\"alpha\") found");
        f = sem_find_function(u, "beta");
        CHECK(f != NULL, "sem_find_function(\"beta\") found");
        f = sem_find_function(u, "nonexistent");
        CHECK(f == NULL, "sem_find_function(\"nonexistent\") returns NULL");
        spt_lsp_unit_free(u);
      }
    }
    doc_store_close(&srv.docs, "file:///test_hash.spt");
  }

  lsp_server_free(&srv);
  remove_dir_recursive(dir);

  printf("=== %s ===\n", failed ? "FAIL" : "PASS");
  return failed ? 1 : 0;
}
