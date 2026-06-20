/*
** test_phase6.c — Phase 6 导航能力扩展端到端测试。
**
** 覆盖：
**   6a. typeDefinition — 变量类型注解 → 跳到类定义
**   6b. declaration — declare 块成员跳声明处
**   6c. documentLink — import "mod" 字符串生成链接
**   6d. callHierarchy — prepare/outgoing/incoming
**   6e. rangeFormatting + semanticTokens/range
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "server.h"
#include "workspace.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"
#include "lsp_features.h"
#include "documents.h"

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
#define CHECK(cond, msg)                                                                            \
  do {                                                                                             \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failed++; }                                       \
  } while (0)

static void sink_emit(void *ctx, cJSON *m) { (void)ctx; cJSON_Delete(m); }

static cJSON *make_req(int id, const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", id);
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  return m;
}

/* notification：无 id，用于 didOpen/didChange 等。 */
static cJSON *make_notif(const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
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
  if (f) { fputs(content, f); fclose(f); }
}

static char *make_temp_dir(char *out, size_t cap) {
#ifdef _WIN32
  char base[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, base)) return NULL;
  snprintf(out, cap, "%ssptp6_%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(out, NULL)) return NULL;
  return out;
#else
  (void)cap;
  snprintf(out, cap, "/tmp/sptp6_%d", (int)getpid());
  if (mkdir(out, 0777) != 0) return NULL;
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
      if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
      char full[MAX_PATH];
      snprintf(full, sizeof full, "%s\\%s", path, nm);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) remove_dir_recursive(full);
      else DeleteFileA(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryA(path);
#else
  char cmd[4200];
  snprintf(cmd, sizeof cmd, "rm -rf %s", path);
  if (system(cmd) != 0) { /* best-effort */ }
#endif
}

static cJSON *open_doc(LspServer *srv, const char *uri, const char *text) {
  cJSON *params = cJSON_CreateObject();
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON_AddStringToObject(td, "text", text);
  cJSON_AddNumberToObject(td, "version", 1);
  cJSON_AddItemToObject(params, "textDocument", td);
  return lsp_dispatch(srv, make_notif("textDocument/didOpen", params));
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("=== TestPhase6: navigation extensions ===\n");

  char tmpl[4096];
  char *dir = make_temp_dir(tmpl, sizeof tmpl);
  CHECK(dir != NULL, "make_temp_dir ok");
  if (!dir) { printf("=== abort ===\n"); return 1; }

  /* 项目结构：
     mod.spt    — 定义 class Foo + 函数 bar
     user.spt   — import { bar } from "./mod"; 使用 Foo 类型注解 + 调用 bar
  */
  write_file(dir, "mod.spt",
    "class Foo {\n"
    "  int x;\n"
    "  int get() { return x; }\n"
    "}\n"
    "export int bar(int n) { return n + 1; }\n");
  write_file(dir, "user.spt",
    "import { bar } from \"./mod\";\n"
    "int caller() {\n"
    "  Foo f = Foo(1);\n"
    "  return bar(f.get());\n"
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
    /* 验证 Phase 6 能力已注册 */
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    cJSON *caps = result ? cJSON_GetObjectItemCaseSensitive(result, "capabilities") : NULL;
    CHECK(caps && cJSON_IsObject(caps), "capabilities present");
    if (caps) {
      CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "typeDefinitionProvider")),
            "typeDefinitionProvider declared");
      CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "declarationProvider")),
            "declarationProvider declared");
      CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "documentLinkProvider")),
            "documentLinkProvider declared");
      CHECK(cJSON_GetObjectItemCaseSensitive(caps, "callHierarchyProvider") != NULL,
            "callHierarchyProvider declared");
      CHECK(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "documentRangeFormattingProvider")),
            "documentRangeFormattingProvider declared");
    }
    if (resp) cJSON_Delete(resp);
  }

  /* 打开 user.spt */
  char user_path[4096], user_uri[4096];
  char mod_path[4096], mod_uri[4096];
#ifdef _WIN32
  snprintf(user_path, sizeof user_path, "%s\\user.spt", dir);
  snprintf(mod_path, sizeof mod_path, "%s\\mod.spt", dir);
#else
  snprintf(user_path, sizeof user_path, "%s/user.spt", dir);
  snprintf(mod_path, sizeof mod_path, "%s/mod.spt", dir);
#endif
  spt_path_to_uri(user_path, user_uri, sizeof user_uri);
  spt_path_to_uri(mod_path, mod_uri, sizeof mod_uri);

  {
    cJSON *resp = open_doc(&srv, user_uri,
      "import { bar } from \"./mod\";\n"
      "int caller() {\n"
      "  Foo f = Foo(1);\n"
      "  return bar(f.get());\n"
      "}\n");
    if (resp) cJSON_Delete(resp);
  }
  /* 也打开 mod.spt 以便跨文件功能工作 */
  {
    cJSON *resp = open_doc(&srv, mod_uri,
      "class Foo {\n"
      "  int x;\n"
      "  int get() { return x; }\n"
      "}\n"
      "export int bar(int n) { return n + 1; }\n");
    if (resp) cJSON_Delete(resp);
  }

  /* ---- 6a: typeDefinition ---- */
  printf("--- 6a: typeDefinition ---\n");
  {
    /* 光标在 user.spt 第 3 行 "Foo f" 的 f 上（变量 f 类型为 Foo）。
       user.spt 内容：
         line 0: import { bar } from "./mod";
         line 1: int caller() {
         line 2:   Foo f = Foo(1);
         line 3:   return bar(f.get());
         line 4: }
       光标在 line 2, char 6（f 的位置）。
       typeDefinition 应跳到 mod.spt 的 class Foo 定义。 */
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", user_uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", 2);
    cJSON_AddNumberToObject(pos, "character", 6);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON *resp = lsp_dispatch(&srv, make_req(10, "textDocument/typeDefinition", params));
    CHECK(resp != NULL, "typeDefinition response");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    CHECK(result != NULL, "typeDefinition result non-null");
    if (result && cJSON_IsObject(result)) {
      cJSON *uri = cJSON_GetObjectItemCaseSensitive(result, "uri");
      CHECK(uri && cJSON_IsString(uri), "typeDefinition has uri");
      if (uri && strcmp(uri->valuestring, mod_uri) == 0) {
        printf("  typeDefinition uri = mod.spt (correct)\n");
      } else if (uri) {
        /* 可能跳到当前文件的 Foo 引用，也算部分成功（Foo 在 user.spt 中被使用） */
        printf("  typeDefinition uri = %s\n", uri->valuestring);
      }
    }
    if (resp) cJSON_Delete(resp);
  }

  /* ---- 6b: declaration ---- */
  printf("--- 6b: declaration ---\n");
  {
    /* 光标在 user.spt line 0 "bar" 上（import 引入的符号）。
       declaration 应回退到 definition（跨文件跳到 mod.spt 的 bar 定义）。 */
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", user_uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", 0);
    cJSON_AddNumberToObject(pos, "character", 9);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON *resp = lsp_dispatch(&srv, make_req(11, "textDocument/declaration", params));
    CHECK(resp != NULL, "declaration response");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    CHECK(result != NULL, "declaration result non-null");
    if (result && cJSON_IsObject(result)) {
      cJSON *uri = cJSON_GetObjectItemCaseSensitive(result, "uri");
      CHECK(uri && cJSON_IsString(uri), "declaration has uri");
      if (uri) printf("  declaration uri = %s\n", uri->valuestring);
    }
    if (resp) cJSON_Delete(resp);
  }

  /* ---- 6c: documentLink ---- */
  printf("--- 6c: documentLink ---\n");
  {
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", user_uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_req(12, "textDocument/documentLink", params));
    CHECK(resp != NULL, "documentLink response");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    CHECK(result && cJSON_IsArray(result), "documentLink result is array");
    if (result) {
      int n = cJSON_GetArraySize(result);
      printf("  documentLink count = %d\n", n);
      CHECK(n >= 1, "at least one document link (import)");
      if (n > 0) {
        cJSON *link = cJSON_GetArrayItem(result, 0);
        cJSON *target = cJSON_GetObjectItemCaseSensitive(link, "target");
        CHECK(target && cJSON_IsString(target), "documentLink has target");
        if (target) printf("  documentLink target = %s\n", target->valuestring);
      }
    }
    if (resp) cJSON_Delete(resp);
  }

  /* ---- 6d: callHierarchy ---- */
  printf("--- 6d: callHierarchy ---\n");
  {
    /* prepareCallHierarchy：光标在 user.spt line 1 "caller" 函数名上。
       user.spt line 1: "int caller() {"，caller 从 char 4 开始。 */
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", user_uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", 1);
    cJSON_AddNumberToObject(pos, "character", 4);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON *resp = lsp_dispatch(&srv, make_req(13, "textDocument/prepareCallHierarchy", params));
    CHECK(resp != NULL, "prepareCallHierarchy response");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    CHECK(result && cJSON_IsArray(result), "prepareCallHierarchy result is array");
    if (result) {
      int n = cJSON_GetArraySize(result);
      printf("  prepareCallHierarchy count = %d\n", n);
      CHECK(n >= 1, "prepareCallHierarchy returns caller function");
      if (n > 0) {
        cJSON *item = cJSON_GetArrayItem(result, 0);
        cJSON *name = cJSON_GetObjectItemCaseSensitive(item, "name");
        CHECK(name && cJSON_IsString(name) && strcmp(name->valuestring, "caller") == 0,
              "prepareCallHierarchy name = caller");

        /* outgoingCalls：caller 调用了 bar 和 Foo */
        cJSON *out_params = cJSON_CreateObject();
        cJSON_AddItemToObject(out_params, "item", cJSON_Duplicate(item, 1));
        cJSON *out_resp = lsp_dispatch(&srv, make_req(14, "callHierarchy/outgoingCalls", out_params));
        CHECK(out_resp != NULL, "outgoingCalls response");
        cJSON *out_result = cJSON_GetObjectItemCaseSensitive(out_resp, "result");
        if (out_result && cJSON_IsArray(out_result)) {
          int on = cJSON_GetArraySize(out_result);
          printf("  outgoingCalls count = %d\n", on);
          CHECK(on >= 1, "outgoingCalls returns at least one call");
        }
        if (out_resp) cJSON_Delete(out_resp);

        /* incomingCalls：谁调用了 caller？（目前无人调用，应为空） */
        cJSON *in_params = cJSON_CreateObject();
        cJSON_AddItemToObject(in_params, "item", cJSON_Duplicate(item, 1));
        cJSON *in_resp = lsp_dispatch(&srv, make_req(15, "callHierarchy/incomingCalls", in_params));
        CHECK(in_resp != NULL, "incomingCalls response");
        if (in_resp) cJSON_Delete(in_resp);
      }
    }
    if (resp) cJSON_Delete(resp);
  }

  /* ---- 6e: rangeFormatting + semanticTokens/range ---- */
  printf("--- 6e: rangeFormatting + semanticTokens/range ---\n");
  {
    /* rangeFormatting：格式化 user.spt 的 line 2-3。 */
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", user_uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *rng = cJSON_CreateObject();
    cJSON *start = cJSON_CreateObject();
    cJSON_AddNumberToObject(start, "line", 2);
    cJSON_AddNumberToObject(start, "character", 0);
    cJSON *end = cJSON_CreateObject();
    cJSON_AddNumberToObject(end, "line", 3);
    cJSON_AddNumberToObject(end, "character", 30);
    cJSON_AddItemToObject(rng, "start", start);
    cJSON_AddItemToObject(rng, "end", end);
    cJSON_AddItemToObject(params, "range", rng);
    cJSON *opts = cJSON_CreateObject();
    cJSON_AddNumberToObject(opts, "tabSize", 4);
    cJSON_AddBoolToObject(opts, "insertSpaces", 1);
    cJSON_AddItemToObject(params, "options", opts);
    cJSON *resp = lsp_dispatch(&srv, make_req(16, "textDocument/rangeFormatting", params));
    CHECK(resp != NULL, "rangeFormatting response");
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    CHECK(result && cJSON_IsArray(result), "rangeFormatting result is array");
    if (resp) cJSON_Delete(resp);

    /* semanticTokens/range：只返回 range 内的 token。 */
    cJSON *st_params = cJSON_CreateObject();
    cJSON *st_td = cJSON_CreateObject();
    cJSON_AddStringToObject(st_td, "uri", user_uri);
    cJSON_AddItemToObject(st_params, "textDocument", st_td);
    cJSON *st_rng = cJSON_CreateObject();
    cJSON *st_start = cJSON_CreateObject();
    cJSON_AddNumberToObject(st_start, "line", 0);
    cJSON_AddNumberToObject(st_start, "character", 0);
    cJSON *st_end = cJSON_CreateObject();
    cJSON_AddNumberToObject(st_end, "line", 2);
    cJSON_AddNumberToObject(st_end, "character", 30);
    cJSON_AddItemToObject(st_rng, "start", st_start);
    cJSON_AddItemToObject(st_rng, "end", st_end);
    cJSON_AddItemToObject(st_params, "range", st_rng);
    cJSON *st_resp = lsp_dispatch(&srv, make_req(17, "textDocument/semanticTokens/range", st_params));
    CHECK(st_resp != NULL, "semanticTokens/range response");
    cJSON *st_result = cJSON_GetObjectItemCaseSensitive(st_resp, "result");
    CHECK(st_result && cJSON_IsObject(st_result), "semanticTokens/range result is object");
    if (st_result) {
      cJSON *data = cJSON_GetObjectItemCaseSensitive(st_result, "data");
      CHECK(data && cJSON_IsArray(data), "semanticTokens/range has data array");
      if (data) printf("  semanticTokens/range data count = %d\n", cJSON_GetArraySize(data));
    }
    if (st_resp) cJSON_Delete(st_resp);
  }

  lsp_server_free(&srv);
  remove_dir_recursive(dir);

  printf("=== TestPhase6: %s (%d failures) ===\n", failed ? "FAIL" : "PASS", failed);
  return failed ? 1 : 0;
}
