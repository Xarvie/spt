/*
** test_incomplete.c — 残缺代码健壮性测试。
**
** 遍历 test/incomplete/ 目录下的 .spt 文件，模拟用户编辑中的残缺代码。
** 对每个文件执行 didOpen → documentSymbol → semanticTokens/full → hover，
** 验证 server 不崩溃且返回合理结果（允许诊断错误，但不允许段错误）。
**
** 测试覆盖的残缺场景：
**   - 变量声明缺右值 / 缺到行尾
**   - return 缺表达式 / 截断到行尾
**   - 函数调用缺参数
**   - if 条件缺右半 / 截断到行尾
**   - if 块内语句缺右值
**   - if 无块缺条件 / 缺语句
**   - for 缺条件
**   - while 缺条件
**   - class 成员缺名
**   - class 方法 return 缺表达式
*/
#include "server.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#else
#include <dirent.h>
#endif

static int failed = 0;
static int passed = 0;

#define CHECK(cond, msg)                                                                            \
  do {                                                                                             \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failed++; return; }                               \
  } while (0)

static void sink_emit(void *ctx, cJSON *m) { (void)ctx; cJSON_Delete(m); }

static int next_id = 200;

static cJSON *call(LspServer *s, const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", next_id++);
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  cJSON *resp = lsp_dispatch(s, m);
  cJSON_Delete(m);
  return resp;
}

static cJSON *docp(const char *uri) {
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON *p = cJSON_CreateObject();
  cJSON_AddItemToObject(p, "textDocument", td);
  return p;
}

static cJSON *posp(const char *uri, int line, int ch) {
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON *pos = cJSON_CreateObject();
  cJSON_AddNumberToObject(pos, "line", line);
  cJSON_AddNumberToObject(pos, "character", ch);
  cJSON *p = cJSON_CreateObject();
  cJSON_AddItemToObject(p, "textDocument", td);
  cJSON_AddItemToObject(p, "position", pos);
  return p;
}

static char *read_file(const char *path, long *out_len) {
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)len + 1);
  if (!buf) { fclose(f); return NULL; }
  size_t rd = fread(buf, 1, (size_t)len, f);
  fclose(f);
  buf[rd] = '\0';
  if (out_len) *out_len = (long)rd;
  return buf;
}

static void test_one_file(const char *path, const char *name) {
  printf("  [%s] ", name);

  long len;
  char *text = read_file(path, &len);
  if (!text) {
    printf("FAIL: cannot read\n");
    failed++;
    return;
  }

  LspServer s;
  lsp_server_init(&s);
  lsp_server_set_emit(&s, sink_emit, NULL);

  /* initialize */
  cJSON *im = cJSON_CreateObject();
  cJSON_AddStringToObject(im, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(im, "id", 1);
  cJSON_AddStringToObject(im, "method", "initialize");
  cJSON *ir = lsp_dispatch(&s, im);
  cJSON_Delete(ir);
  cJSON_Delete(im);

  /* didOpen */
  char uri[512];
  snprintf(uri, sizeof(uri), "file:///incomplete/%s", name);
  {
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", uri);
    cJSON_AddStringToObject(td, "languageId", "sptscript");
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text", text);
    cJSON *p = cJSON_CreateObject();
    cJSON_AddItemToObject(p, "textDocument", td);
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddStringToObject(m, "method", "textDocument/didOpen");
    cJSON_AddItemToObject(m, "params", p);
    cJSON *r = lsp_dispatch(&s, m);
    if (r) cJSON_Delete(r);
    cJSON_Delete(m);
  }

  /* documentSymbol — 允许返回空或错误，但不能崩溃 */
  {
    cJSON *resp = call(&s, "textDocument/documentSymbol", docp(uri));
    if (!resp) {
      printf("FAIL: documentSymbol crashed\n");
      failed++;
      free(text);
      lsp_server_free(&s);
      return;
    }
    cJSON_Delete(resp);
  }

  /* semanticTokens/full — 之前的崩溃点 */
  {
    cJSON *resp = call(&s, "textDocument/semanticTokens/full", docp(uri));
    if (!resp) {
      printf("FAIL: semanticTokens/full crashed\n");
      failed++;
      free(text);
      lsp_server_free(&s);
      return;
    }
    cJSON_Delete(resp);
  }

  /* hover on line 1 char 1 — 测试残缺代码上的 hover */
  {
    cJSON *resp = call(&s, "textDocument/hover", posp(uri, 0, 1));
    if (!resp) {
      printf("FAIL: hover crashed\n");
      failed++;
      free(text);
      lsp_server_free(&s);
      return;
    }
    cJSON_Delete(resp);
  }

  /* hover on line 11 char 5 (函数体内) */
  {
    cJSON *resp = call(&s, "textDocument/hover", posp(uri, 10, 5));
    if (!resp) {
      printf("FAIL: hover(in-func) crashed\n");
      failed++;
      free(text);
      lsp_server_free(&s);
      return;
    }
    cJSON_Delete(resp);
  }

  printf("OK\n");
  passed++;
  free(text);
  lsp_server_free(&s);
}

#ifdef _WIN32
static void run_dir(const char *dir) {
  char pattern[MAX_PATH];
  snprintf(pattern, sizeof(pattern), "%s\\*.spt", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    printf("  (no .spt files in %s)\n", dir);
    return;
  }
  do {
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s\\%s", dir, fd.cFileName);
    test_one_file(path, fd.cFileName);
  } while (FindNextFileA(h, &fd));
  FindClose(h);
}
#else
static void run_dir(const char *dir) {
  DIR *d = opendir(dir);
  if (!d) {
    printf("  (cannot open %s)\n", dir);
    return;
  }
  struct dirent *e;
  while ((e = readdir(d))) {
    size_t len = strlen(e->d_name);
    if (len < 5 || strcmp(e->d_name + len - 4, ".spt") != 0) continue;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
    test_one_file(path, e->d_name);
  }
  closedir(d);
}
#endif

int main(void) {
  printf("=== TestIncomplete: 残缺代码健壮性测试 ===\n");
  printf("模拟用户编辑中的残缺代码，验证 LSP 不崩溃。\n\n");

  /* 用相对路径：测试从 server/ 目录运行 */
  const char *dir = "test/incomplete";

  /* 尝试几个可能的路径 */
  const char *dirs[] = {
    "test/incomplete",
    "../test/incomplete",
    "../../test/incomplete",
    NULL
  };

  int found = 0;
  for (int i = 0; dirs[i]; i++) {
#ifdef _WIN32
    DWORD attr = GetFileAttributesA(dirs[i]);
    if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
#else
    DIR *test = opendir(dirs[i]);
    if (test) { closedir(test);
#endif
      dir = dirs[i];
      found = 1;
      break;
    }
  }

  if (!found) {
    printf("FAIL: cannot find test/incomplete directory\n");
    return 1;
  }

  printf("扫描目录: %s\n\n", dir);
  run_dir(dir);

  printf("\n=== 结果: %d passed, %d failed ===\n", passed, failed);
  return failed ? 1 : 0;
}
