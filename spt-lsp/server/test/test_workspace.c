/*
** test_workspace.c — 跨文件 workspace/symbol（在临时项目目录上）。
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "server.h"
#include "workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int failed = 0;
#define CHECK(cond, msg)                                                                            \
  do {                                                                                             \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failed++; }                                       \
  } while (0)

static void sink_emit(void *ctx, cJSON *m) { (void)ctx; cJSON_Delete(m); }

static void write_file(const char *dir, const char *name, const char *content) {
  char path[4096];
  snprintf(path, sizeof path, "%s/%s", dir, name);
  FILE *f = fopen(path, "wb");
  if (f) { fputs(content, f); fclose(f); }
}

static int has_named(cJSON *arr, const char *name) {
  int n = cJSON_GetArraySize(arr);
  for (int i = 0; i < n; i++) {
    cJSON *it = cJSON_GetArrayItem(arr, i);
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
    if (nm && nm->valuestring && strcmp(nm->valuestring, name) == 0) return 1;
  }
  return 0;
}

int main(void) {
  printf("=== TestWorkspace: workspace/symbol across files ===\n");

  /* uri<->path 往返 */
  {
    char p[1024], u[1024];
    spt_uri_to_path("file:///tmp/a%20b/x.spt", p, sizeof p);
    CHECK(strcmp(p, "/tmp/a b/x.spt") == 0, "uri_to_path decodes %20 and strips scheme");
    spt_path_to_uri("/tmp/a b/x.spt", u, sizeof u);
    CHECK(strcmp(u, "file:///tmp/a%20b/x.spt") == 0, "path_to_uri encodes space");
  }

  char tmpl[] = "/tmp/sptwsXXXXXX";
  char *dir = mkdtemp(tmpl);
  CHECK(dir != NULL, "mkdtemp ok");
  if (!dir) { printf("=== abort ===\n"); return 1; }

  write_file(dir, "a.spt",
             "int foo(int n) { return n; }\n"
             "class Bar {\n  int v;\n  int baz() { return 0; }\n}\n");
  write_file(dir, "b.spt", "int qux() { return 1; }\nglobal str title = \"hi\";\n");
  /* 子目录也应被扫描 */
  char sub[4096];
  snprintf(sub, sizeof sub, "%s/sub", dir);
#ifdef _WIN32
  _mkdir(sub);
#else
  mkdir(sub, 0777);
#endif
  write_file(sub, "c.spt", "int deep() { return 2; }\n");

  LspServer s;
  lsp_server_init(&s);
  lsp_server_set_emit(&s, sink_emit, NULL);

  /* initialize with rootUri */
  char rootUri[4200];
  snprintf(rootUri, sizeof rootUri, "file://%s", dir);
  cJSON *im = cJSON_CreateObject();
  cJSON_AddStringToObject(im, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(im, "id", 1);
  cJSON_AddStringToObject(im, "method", "initialize");
  cJSON *ip = cJSON_CreateObject();
  cJSON_AddStringToObject(ip, "rootUri", rootUri);
  cJSON_AddItemToObject(im, "params", ip);
  cJSON *ir = lsp_dispatch(&s, im);
  cJSON *caps = ir ? cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(ir, "result"), "capabilities") : NULL;
  CHECK(caps && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "workspaceSymbolProvider")),
        "advertises workspaceSymbolProvider");
  cJSON_Delete(ir); cJSON_Delete(im);

  /* workspace/symbol query "" -> all */
  cJSON *qm = cJSON_CreateObject();
  cJSON_AddStringToObject(qm, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(qm, "id", 2);
  cJSON_AddStringToObject(qm, "method", "workspace/symbol");
  cJSON *qp = cJSON_CreateObject();
  cJSON_AddStringToObject(qp, "query", "");
  cJSON_AddItemToObject(qm, "params", qp);
  cJSON *qr = lsp_dispatch(&s, qm);
  cJSON *res = qr ? cJSON_GetObjectItemCaseSensitive(qr, "result") : NULL;
  printf("Testing: full index...\n");
  CHECK(has_named(res, "foo"), "indexed foo (a.spt)");
  CHECK(has_named(res, "Bar"), "indexed Bar (a.spt)");
  CHECK(has_named(res, "baz"), "indexed baz (Bar method)");
  CHECK(has_named(res, "qux"), "indexed qux (b.spt)");
  CHECK(has_named(res, "title"), "indexed title (b.spt global)");
  CHECK(has_named(res, "deep"), "indexed deep (sub/c.spt) — recursive");
  /* location.uri sanity for foo */
  if (res) {
    int n = cJSON_GetArraySize(res);
    for (int i = 0; i < n; i++) {
      cJSON *it = cJSON_GetArrayItem(res, i);
      cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
      if (nm && strcmp(nm->valuestring, "foo") == 0) {
        cJSON *loc = cJSON_GetObjectItemCaseSensitive(it, "location");
        cJSON *uri = loc ? cJSON_GetObjectItemCaseSensitive(loc, "uri") : NULL;
        CHECK(uri && strstr(uri->valuestring, "a.spt"), "foo location uri -> a.spt");
        CHECK(loc && cJSON_GetObjectItemCaseSensitive(loc, "range"), "foo has range");
      }
      if (nm && strcmp(nm->valuestring, "baz") == 0) {
        cJSON *cn = cJSON_GetObjectItemCaseSensitive(it, "containerName");
        CHECK(cn && strcmp(cn->valuestring, "Bar") == 0, "baz containerName == Bar");
      }
    }
  }
  cJSON_Delete(qm); cJSON_Delete(qr);

  /* query "ba" -> Bar, baz only (case-insensitive substring) */
  printf("Testing: filtered query 'ba'...\n");
  cJSON *fm = cJSON_CreateObject();
  cJSON_AddStringToObject(fm, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(fm, "id", 3);
  cJSON_AddStringToObject(fm, "method", "workspace/symbol");
  cJSON *fp = cJSON_CreateObject();
  cJSON_AddStringToObject(fp, "query", "ba");
  cJSON_AddItemToObject(fm, "params", fp);
  cJSON *fr = lsp_dispatch(&s, fm);
  cJSON *fres = fr ? cJSON_GetObjectItemCaseSensitive(fr, "result") : NULL;
  CHECK(has_named(fres, "Bar"), "query 'ba' matches Bar");
  CHECK(has_named(fres, "baz"), "query 'ba' matches baz");
  CHECK(!has_named(fres, "foo"), "query 'ba' excludes foo");
  CHECK(!has_named(fres, "qux"), "query 'ba' excludes qux");
  cJSON_Delete(fm); cJSON_Delete(fr);

  lsp_server_free(&s);

  /* cleanup */
  char cmd[4300];
  snprintf(cmd, sizeof cmd, "rm -rf %s", dir);
  if (system(cmd) != 0) { /* best-effort */ }

  if (failed == 0) { printf("=== TestWorkspace: ALL PASS ===\n"); return 0; }
  printf("=== TestWorkspace: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
