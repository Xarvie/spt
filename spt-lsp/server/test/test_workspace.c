/*
** test_workspace.c — 跨文件 workspace/symbol（在临时项目目录上）+ 打开文档覆盖索引。
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "server.h"
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

/* 跨平台创建唯一临时目录，返回路径（写入 out）。失败返回 NULL。 */
static char *make_temp_dir(char *out, size_t cap) {
#ifdef _WIN32
  char base[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, base))
    return NULL;
  snprintf(out, cap, "%ssptws_%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(out, NULL))
    return NULL;
  return out;
#else
  (void)cap;
  snprintf(out, cap, "/tmp/sptws_%d", (int)getpid());
  if (mkdir(out, 0777) != 0)
    return NULL;
  return out;
#endif
}

/* 跨平台递归删除目录（best-effort）。 */
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

static int has_named(cJSON *arr, const char *name) {
  int n = cJSON_GetArraySize(arr);
  for (int i = 0; i < n; i++) {
    cJSON *it = cJSON_GetArrayItem(arr, i);
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
    if (nm && nm->valuestring && strcmp(nm->valuestring, name) == 0)
      return 1;
  }
  return 0;
}

int main(void) {
  printf("=== TestWorkspace: workspace/symbol across files ===\n");

  /* uri<->path 往返（POSIX 与 Windows 各自的预期） */
  {
    char p[1024], u[1024];
#ifdef _WIN32
    spt_uri_to_path("file:///C%3A/a%20b/x.spt", p, sizeof p);
    CHECK(strcmp(p, "C:\\a b\\x.spt") == 0,
          "uri_to_path decodes %20/%3A, strips scheme, drive + backslash");
    spt_path_to_uri("C:\\a b\\x.spt", u, sizeof u);
    CHECK(strcmp(u, "file:///C%3A/a%20b/x.spt") == 0,
          "path_to_uri encodes space/colon, flips backslash");
#else
    spt_uri_to_path("file:///tmp/a%20b/x.spt", p, sizeof p);
    CHECK(strcmp(p, "/tmp/a b/x.spt") == 0, "uri_to_path decodes %20 and strips scheme");
    spt_path_to_uri("/tmp/a b/x.spt", u, sizeof u);
    CHECK(strcmp(u, "file:///tmp/a%20b/x.spt") == 0, "path_to_uri encodes space");
#endif
  }

  char tmpl[4096];
  char *dir = make_temp_dir(tmpl, sizeof tmpl);
  CHECK(dir != NULL, "make_temp_dir ok");
  if (!dir) {
    printf("=== abort ===\n");
    return 1;
  }

  write_file(dir, "a.spt",
             "int foo(int n) { return n; }\n"
             "class Bar {\n  int v;\n  int baz() { return 0; }\n}\n");
  write_file(dir, "b.spt", "int qux() { return 1; }\nglobal str title = \"hi\";\n");
  /* 子目录也应被扫描 */
  char sub[4096];
#ifdef _WIN32
  snprintf(sub, sizeof sub, "%s\\sub", dir);
  _mkdir(sub);
#else
  snprintf(sub, sizeof sub, "%s/sub", dir);
  mkdir(sub, 0777);
#endif
  write_file(sub, "c.spt", "int deep() { return 2; }\n");

  LspServer s;
  lsp_server_init(&s);
  lsp_server_set_emit(&s, sink_emit, NULL);

  /* initialize with rootUri */
  char rootUri[4200];
  spt_path_to_uri(dir, rootUri, sizeof rootUri);
  cJSON *im = cJSON_CreateObject();
  cJSON_AddStringToObject(im, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(im, "id", 1);
  cJSON_AddStringToObject(im, "method", "initialize");
  cJSON *ip = cJSON_CreateObject();
  cJSON_AddStringToObject(ip, "rootUri", rootUri);
  cJSON_AddItemToObject(im, "params", ip);
  cJSON *ir = lsp_dispatch(&s, im);
  cJSON *caps = ir ? cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetObjectItemCaseSensitive(ir, "result"), "capabilities")
                   : NULL;
  CHECK(caps && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "workspaceSymbolProvider")),
        "advertises workspaceSymbolProvider");
  cJSON_Delete(ir);
  cJSON_Delete(im);

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
  cJSON_Delete(qm);
  cJSON_Delete(qr);

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
  cJSON_Delete(fm);
  cJSON_Delete(fr);

  /* ---- 打开文档覆盖索引：didOpen 用改动后的文本，workspace/symbol 反映新符号 ---- */
  printf("Testing: open-document overlay on index...\n");
  {
    /* 打开 a.spt，但内容替换：foo -> foo_renamed，新增 overlay_only */
    char aUri[4400];
    spt_path_to_uri(dir, aUri, sizeof aUri);
    /* spt_path_to_uri 生成 a.spt 的 URI；但 a.spt 在 dir 下，需拼路径 */
    char apath[4200];
#ifdef _WIN32
    snprintf(apath, sizeof apath, "%s\\a.spt", dir);
#else
    snprintf(apath, sizeof apath, "%s/a.spt", dir);
#endif
    spt_path_to_uri(apath, aUri, sizeof aUri);

    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", aUri);
    cJSON_AddStringToObject(td, "languageId", "sptscript");
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text",
                            "int foo_renamed(int n) { return n; }\n"
                            "int overlay_only() { return 42; }\n");
    cJSON *op = cJSON_CreateObject();
    cJSON_AddItemToObject(op, "textDocument", td);
    cJSON *om = cJSON_CreateObject();
    cJSON_AddStringToObject(om, "jsonrpc", "2.0");
    cJSON_AddStringToObject(om, "method", "textDocument/didOpen");
    cJSON_AddItemToObject(om, "params", op);
    cJSON *orr = lsp_dispatch(&s, om);
    if (orr)
      cJSON_Delete(orr);
    cJSON_Delete(om);

    /* 查询：overlay_only 应在（仅存在于打开文档），foo 应不在（被覆盖移除） */
    cJSON *om2 = cJSON_CreateObject();
    cJSON_AddStringToObject(om2, "jsonrpc", "2.0");
    cJSON_AddNumberToObject(om2, "id", 4);
    cJSON_AddStringToObject(om2, "method", "workspace/symbol");
    cJSON *op2 = cJSON_CreateObject();
    cJSON_AddStringToObject(op2, "query", "");
    cJSON_AddItemToObject(om2, "params", op2);
    cJSON *orr2 = lsp_dispatch(&s, om2);
    cJSON *ores = orr2 ? cJSON_GetObjectItemCaseSensitive(orr2, "result") : NULL;
    CHECK(has_named(ores, "overlay_only"), "overlay: overlay_only indexed from open doc");
    CHECK(has_named(ores, "foo_renamed"), "overlay: foo_renamed indexed from open doc");
    CHECK(!has_named(ores, "foo"), "overlay: disk-only 'foo' replaced by open doc");
    /* b.spt 的符号不受影响 */
    CHECK(has_named(ores, "qux"), "overlay: other files unaffected (qux still indexed)");
    cJSON_Delete(om2);
    cJSON_Delete(orr2);
  }

  lsp_server_free(&s);

  /* cleanup */
  remove_dir_recursive(dir);

  if (failed == 0) {
    printf("=== TestWorkspace: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestWorkspace: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
