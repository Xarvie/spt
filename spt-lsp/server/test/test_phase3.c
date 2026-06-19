/*
** test_phase3.c — Phase 3 深度语义特性端到端测试。
**
** 覆盖：
**   3a. 跨文件签名帮助（具名导入函数的签名）
**   3b. 工作区重命名（导出符号跨文件 TextEdit）
**   3c. inlayHint（参数名提示）
**   3d. 结构性诊断（未定义名 + arity 不符）
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "server.h"
#include "workspace.h"
#include "module_resolve.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"
#include "lsp_features.h"
#include "diagnostics.h"

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

/* ---- 跨平台临时目录工具 ---- */
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
  snprintf(out, cap, "%ssptp3_%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(out, NULL)) return NULL;
  return out;
#else
  (void)cap;
  snprintf(out, cap, "/tmp/sptp3_%d", (int)getpid());
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

/* 在 text 中找 substr 第 occ 次（1 起）出现的 LSP 位置。 */
static LspPos pos_of(const char *text, const char *substr, int occ) {
  LspPos p = {0, 0};
  int line = 0, col = 0, found = 0;
  for (size_t i = 0; text[i]; i++) {
    if (text[i] == '\n') { line++; col = 0; continue; }
    if (strncmp(text + i, substr, strlen(substr)) == 0) {
      found++;
      if (found == occ) { p.line = line; p.character = col; return p; }
    }
    col++;
  }
  return p;
}

static cJSON *make_req(int id, const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", id);
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  return m;
}

static cJSON *make_notif(const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  return m;
}

static cJSON *td_params(const char *uri, LspPos pos) {
  cJSON *p = cJSON_CreateObject();
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON_AddItemToObject(p, "textDocument", td);
  cJSON_AddItemToObject(p, "position", lsp_pos_to_json(pos));
  return p;
}

static void path_to_uri(const char *path, char *out, size_t cap) {
  spt_path_to_uri(path, out, cap);
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("=== TestPhase3: deep semantic features ===\n");

  char tmpl[4096];
  char *dir = make_temp_dir(tmpl, sizeof tmpl);
  CHECK(dir != NULL, "make_temp_dir ok");
  if (!dir) { printf("=== abort ===\n"); return 1; }

  /* ---- 创建测试文件 ---- */
  /* b.spt: 导出函数 add */
  write_file(dir, "b.spt",
    "export int add(int a, int b) {\n"
    "  return a + b;\n"
    "}\n"
  );
  /* a.spt: 导入 add 并调用 */
  const char *a_text =
    "import { add } from \"b\";\n"
    "int caller() {\n"
    "  return add(1, 2);\n"
    "}\n";
  write_file(dir, "a.spt", a_text);
  /* diag.spt: 未定义名 + arity 不符 */
  write_file(dir, "diag.spt",
    "int f(int x) {\n"
    "  return x;\n"
    "}\n"
    "int g() {\n"
    "  return f(1, 2, 3, 4);\n"
    "}\n"
  );

  char a_path[4096], b_path[4096], diag_path[4096], a_uri[4096], b_uri[4096], diag_uri[4096];
#ifdef _WIN32
  snprintf(a_path, sizeof a_path, "%s\\a.spt", dir);
  snprintf(b_path, sizeof b_path, "%s\\b.spt", dir);
  snprintf(diag_path, sizeof diag_path, "%s\\diag.spt", dir);
#else
  snprintf(a_path, sizeof a_path, "%s/a.spt", dir);
  snprintf(b_path, sizeof b_path, "%s/b.spt", dir);
  snprintf(diag_path, sizeof diag_path, "%s/diag.spt", dir);
#endif
  path_to_uri(a_path, a_uri, sizeof a_uri);
  path_to_uri(b_path, b_uri, sizeof b_uri);
  path_to_uri(diag_path, diag_uri, sizeof diag_uri);

  /* ---- 初始化 LSP 服务器 ---- */
  LspServer srv;
  lsp_server_init(&srv);
  lsp_server_set_emit(&srv, sink_emit, NULL);

  /* initialize */
  {
    cJSON *params = cJSON_CreateObject();
    cJSON *wf = cJSON_CreateArray();
    char root_uri[4096];
#ifdef _WIN32
    char root_path[4096];
    snprintf(root_path, sizeof root_path, "%s", dir);
    path_to_uri(root_path, root_uri, sizeof root_uri);
#else
    snprintf(root_uri, sizeof root_uri, "file://%s", dir);
#endif
    cJSON *fo = cJSON_CreateObject();
    cJSON_AddStringToObject(fo, "uri", root_uri);
    cJSON_AddItemToArray(wf, fo);
    cJSON_AddItemToObject(params, "workspaceFolders", wf);
    cJSON *resp = lsp_dispatch(&srv, make_req(1, "initialize", params));
    CHECK(resp != NULL, "initialize response");
    cJSON_Delete(resp);
  }

  /* didOpen a.spt */
  {
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", a_uri);
    cJSON_AddStringToObject(td, "text", a_text);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp) cJSON_Delete(resp);
  }

  /* ---- 3a. 跨文件签名帮助 ---- */
  printf("--- 3a: cross-file signature help ---\n");
  {
    /* 光标在 add( 的 '(' 之后 */
    LspPos pos = pos_of(a_text, "add(", 1);
    pos.character += 4; /* 指向 ( 之后 */
    cJSON *resp = lsp_dispatch(&srv, make_req(2, "textDocument/signatureHelp",
                                               td_params(a_uri, pos)));
    CHECK(resp != NULL, "signatureHelp response");
    if (resp) {
      cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
      if (result && !cJSON_IsNull(result)) {
        cJSON *sigs = cJSON_GetObjectItemCaseSensitive(result, "signatures");
        CHECK(sigs && cJSON_GetArraySize(sigs) > 0, "signature found");
        if (sigs && cJSON_GetArraySize(sigs) > 0) {
          cJSON *sig = cJSON_GetArrayItem(sigs, 0);
          cJSON *label = cJSON_GetObjectItemCaseSensitive(sig, "label");
          CHECK(label && label->valuestring && strstr(label->valuestring, "add"),
                "signature label contains 'add'");
          CHECK(label && label->valuestring && strstr(label->valuestring, "int a"),
                "signature label contains param 'int a'");
        }
      } else {
        CHECK(0, "signature result not null");
      }
      cJSON_Delete(resp);
    }
  }

  /* ---- 3b. 工作区重命名 ---- */
  printf("--- 3b: workspace rename ---\n");
  {
    /* 光标在 add 调用处 */
    LspPos pos = pos_of(a_text, "add", 2); /* 第 2 次 "add"（调用处） */
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", a_uri);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "position", lsp_pos_to_json(pos));
    cJSON_AddStringToObject(params, "newName", "sum");
    cJSON *resp = lsp_dispatch(&srv, make_req(3, "textDocument/rename", params));
    CHECK(resp != NULL, "rename response");
    if (resp) {
      cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
      if (result && !cJSON_IsNull(result)) {
        cJSON *changes = cJSON_GetObjectItemCaseSensitive(result, "changes");
        CHECK(changes != NULL, "rename has changes");
        if (changes) {
          /* 应包含 a.spt 的编辑 */
          cJSON *a_edits = cJSON_GetObjectItemCaseSensitive(changes, a_uri);
          CHECK(a_edits && cJSON_GetArraySize(a_edits) > 0, "a.spt has edits");
          /* 应包含 b.spt 的编辑（跨文件） */
          cJSON *b_edits = cJSON_GetObjectItemCaseSensitive(changes, b_uri);
          CHECK(b_edits && cJSON_GetArraySize(b_edits) > 0, "b.spt has cross-file edits");
        }
      } else {
        CHECK(0, "rename result not null");
      }
      cJSON_Delete(resp);
    }
  }

  /* ---- 3c. inlayHint ---- */
  printf("--- 3c: inlay hints ---\n");
  {
    /* 直接调用 feature_inlay_hints */
    Document *d = doc_store_get(&srv.docs, a_uri);
    CHECK(d != NULL, "a.spt document found");
    if (d) {
      LspRange range = {{0, 0}, {100, 0}};
      cJSON *hints = feature_inlay_hints(d, range, &srv.ws);
      CHECK(hints != NULL, "inlay hints response");
      if (hints) {
        int count = cJSON_GetArraySize(hints);
        CHECK(count > 0, "inlay hints found");
        if (count > 0) {
          cJSON *hint = cJSON_GetArrayItem(hints, 0);
          cJSON *label = cJSON_GetObjectItemCaseSensitive(hint, "label");
          CHECK(label && label->valuestring && strstr(label->valuestring, "a="),
                "inlay hint label contains 'a='");
          cJSON *kind = cJSON_GetObjectItemCaseSensitive(hint, "kind");
          CHECK(kind && kind->valueint == 2, "inlay hint kind is Parameter (2)");
        }
        cJSON_Delete(hints);
      }
    }
  }

  /* ---- 3d. 结构性诊断 ---- */
  printf("--- 3d: structural diagnostics ---\n");
  {
    /* didOpen diag.spt */
    const char *diag_text =
      "int f(int x) {\n"
      "  return x;\n"
      "}\n"
      "int g() {\n"
      "  return f(1, 2, 3, 4);\n"
      "}\n";
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", diag_uri);
    cJSON_AddStringToObject(td, "text", diag_text);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp) cJSON_Delete(resp);

    /* 直接调用 diagnostics_compute */
    Document *d = doc_store_get(&srv.docs, diag_uri);
    CHECK(d != NULL, "diag.spt document found");
    if (d) {
      cJSON *result = diagnostics_compute(d);
      CHECK(result != NULL, "diagnostics result");
      if (result) {
        cJSON *diags = cJSON_GetObjectItemCaseSensitive(result, "diagnostics");
        CHECK(diags != NULL, "diagnostics array");
        if (diags) {
          int count = cJSON_GetArraySize(diags);
          CHECK(count > 0, "diagnostics found");
          /* 检查是否有 arity 不符警告 */
          int found_arity = 0;
          for (int i = 0; i < count; i++) {
            cJSON *diag = cJSON_GetArrayItem(diags, i);
            cJSON *msg = cJSON_GetObjectItemCaseSensitive(diag, "message");
            if (msg && msg->valuestring && strstr(msg->valuestring, "参数数量不符"))
              found_arity = 1;
          }
          CHECK(found_arity, "arity mismatch warning found");
        }
        cJSON_Delete(result);
      }
    }
  }

  /* ---- 清理 ---- */
  lsp_server_free(&srv);
  remove_dir_recursive(dir);

  printf("=== TestPhase3: %s (%d failures) ===\n", failed ? "FAIL" : "PASS", failed);
  return failed ? 1 : 0;
}
