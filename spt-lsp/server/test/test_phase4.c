/*
** test_phase4.c — Phase 4 体验打磨端到端测试。
**
** 覆盖：
**   4a. snippet 补全（函数调用参数占位 + 关键字模板）
**   4b. 格式化增强（缩进规范化：tab/space 混合 → 纯 space）
**   4c. codeAction（为未 export 的函数声明提供快速修复）
**   4d. 增量同步（didChange range-based 补丁）
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "diagnostics.h"
#include "lsp_features.h"
#include "module_resolve.h"
#include "semantic.h"
#include "server.h"
#include "spt_lsp_bridge.h"
#include "workspace.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static cJSON *make_notif(const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  return m;
}

/* 在 text 中找 substr 第 occ 次（1 起）出现的 LSP 位置。 */
static LspPos pos_of(const char *text, const char *substr, int occ) {
  LspPos p = {0, 0};
  int line = 0, col = 0, found = 0;
  for (size_t i = 0; text[i]; i++) {
    if (text[i] == '\n') {
      line++;
      col = 0;
      continue;
    }
    if (strncmp(text + i, substr, strlen(substr)) == 0) {
      found++;
      if (found == occ) {
        p.line = line;
        p.character = col;
        return p;
      }
    }
    col++;
  }
  return p;
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("=== TestPhase4: experience polish ===\n");

  LspServer srv;
  lsp_server_init(&srv);
  lsp_server_set_emit(&srv, sink_emit, NULL);

  /* initialize */
  {
    cJSON *params = cJSON_CreateObject();
    cJSON *wf = cJSON_CreateArray();
    cJSON *fo = cJSON_CreateObject();
    cJSON_AddStringToObject(fo, "uri", "file:///tmp");
    cJSON_AddItemToArray(wf, fo);
    cJSON_AddItemToObject(params, "workspaceFolders", wf);
    cJSON *resp = lsp_dispatch(&srv, make_req(1, "initialize", params));
    CHECK(resp != NULL, "initialize response");
    /* 验证 textDocumentSync = 2 (Incremental) */
    if (resp) {
      cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
      cJSON *caps = result ? cJSON_GetObjectItemCaseSensitive(result, "capabilities") : NULL;
      cJSON *sync = caps ? cJSON_GetObjectItemCaseSensitive(caps, "textDocumentSync") : NULL;
      CHECK(sync && cJSON_IsNumber(sync) && sync->valueint == 2,
            "textDocumentSync=2 (Incremental)");
      /* 验证 codeActionProvider 已声明 */
      cJSON *ca = caps ? cJSON_GetObjectItemCaseSensitive(caps, "codeActionProvider") : NULL;
      CHECK(ca && cJSON_IsTrue(ca), "codeActionProvider declared");
      cJSON_Delete(resp);
    }
  }

  /* ---- 4a. snippet 补全 ---- */
  printf("--- 4a: snippet completion ---\n");
  {
    const char *text = "int add(int a, int b) {\n"
                       "  return a + b;\n"
                       "}\n";
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", "file:///test_snip.spt");
    cJSON_AddStringToObject(td, "text", text);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp)
      cJSON_Delete(resp);

    /* 在文件末尾请求补全（应看到 add 函数和 class 关键字） */
    LspPos pos = {3, 0}; /* 最后一行之后 */
    cJSON *cparams = cJSON_CreateObject();
    cJSON *ctd = cJSON_CreateObject();
    cJSON_AddStringToObject(ctd, "uri", "file:///test_snip.spt");
    cJSON_AddItemToObject(cparams, "textDocument", ctd);
    cJSON_AddItemToObject(cparams, "position", lsp_pos_to_json(pos));
    cJSON *cresp = lsp_dispatch(&srv, make_req(2, "textDocument/completion", cparams));
    CHECK(cresp != NULL, "completion response");
    int found_func_snip = 0;
    int found_class_snip = 0;
    if (cresp) {
      cJSON *result = cJSON_GetObjectItemCaseSensitive(cresp, "result");
      if (result && cJSON_IsArray(result)) {
        int n = cJSON_GetArraySize(result);
        for (int i = 0; i < n; i++) {
          cJSON *item = cJSON_GetArrayItem(result, i);
          cJSON *label = cJSON_GetObjectItemCaseSensitive(item, "label");
          if (!label || !label->valuestring)
            continue;
          cJSON *itf = cJSON_GetObjectItemCaseSensitive(item, "insertTextFormat");
          cJSON *it = cJSON_GetObjectItemCaseSensitive(item, "insertText");
          if (strcmp(label->valuestring, "add") == 0) {
            /* 函数 add 应有 insertTextFormat=2 和 insertText 含 ${1:a} */
            if (itf && itf->valueint == 2 && it && it->valuestring &&
                strstr(it->valuestring, "${1:a}"))
              found_func_snip = 1;
          }
          if (strcmp(label->valuestring, "class") == 0) {
            /* class 关键字应有 snippet */
            if (itf && itf->valueint == 2 && it && it->valuestring &&
                strstr(it->valuestring, "${1:Name}"))
              found_class_snip = 1;
          }
        }
      }
      cJSON_Delete(cresp);
    }
    CHECK(found_func_snip, "function 'add' has snippet with ${1:a}");
    CHECK(found_class_snip, "keyword 'class' has snippet with ${1:Name}");
  }

  /* ---- 4b. 格式化增强（缩进规范化）---- */
  printf("--- 4b: format indentation normalization ---\n");
  {
    /* 混合 tab 和 space 缩进 */
    const char *text = "int f() {\n"
                       "\treturn 1;   \n"
                       "  \t  if (true) {\n"
                       "\t\treturn 2;\n"
                       "  }\n"
                       "}\n";
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", "file:///test_fmt.spt");
    cJSON_AddStringToObject(td, "text", text);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp)
      cJSON_Delete(resp);

    /* 请求格式化，options: tabSize=4, insertSpaces=true */
    cJSON *fparams = cJSON_CreateObject();
    cJSON *ftd = cJSON_CreateObject();
    cJSON_AddStringToObject(ftd, "uri", "file:///test_fmt.spt");
    cJSON_AddItemToObject(fparams, "textDocument", ftd);
    cJSON *opts = cJSON_CreateObject();
    cJSON_AddNumberToObject(opts, "tabSize", 4);
    cJSON_AddBoolToObject(opts, "insertSpaces", 1);
    cJSON_AddItemToObject(fparams, "options", opts);
    cJSON *fresp = lsp_dispatch(&srv, make_req(3, "textDocument/formatting", fparams));
    CHECK(fresp != NULL, "formatting response");
    int format_ok = 0;
    if (fresp) {
      cJSON *result = cJSON_GetObjectItemCaseSensitive(fresp, "result");
      if (result && cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0) {
        cJSON *ed = cJSON_GetArrayItem(result, 0);
        cJSON *nt = cJSON_GetObjectItemCaseSensitive(ed, "newText");
        if (nt && nt->valuestring) {
          /* 验证：tab 被转为 4 空格，行尾空白被去除 */
          int has_tab = (strchr(nt->valuestring, '\t') != NULL);
          int has_trailing = 0;
          /* 检查是否有行尾空白（' ' 或 '\t' 紧跟 '\n'） */
          for (const char *p = nt->valuestring; *p; p++) {
            if ((*p == ' ' || *p == '\t') && p[1] == '\n')
              has_trailing = 1;
          }
          /* 第二行应为 "    return 1;\n"（4 空格缩进） */
          int has_4space_return = (strstr(nt->valuestring, "    return 1;\n") != NULL);
          if (!has_tab && !has_trailing && has_4space_return)
            format_ok = 1;
        }
      }
      cJSON_Delete(fresp);
    }
    CHECK(format_ok, "indentation normalized (tabs->4spaces, no trailing whitespace)");
  }

  /* ---- 4c. codeAction ---- */
  printf("--- 4c: codeAction (add export) ---\n");
  {
    const char *text = "int add(int a, int b) {\n"
                       "  return a + b;\n"
                       "}\n";
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", "file:///test_ca.spt");
    cJSON_AddStringToObject(td, "text", text);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp)
      cJSON_Delete(resp);

    /* 请求 codeAction，range 覆盖第一行（add 函数声明） */
    cJSON *caparams = cJSON_CreateObject();
    cJSON *catd = cJSON_CreateObject();
    cJSON_AddStringToObject(catd, "uri", "file:///test_ca.spt");
    cJSON_AddItemToObject(caparams, "textDocument", catd);
    LspRange range = {{0, 0}, {0, 10}};
    cJSON_AddItemToObject(caparams, "range", lsp_range_to_json(range));
    cJSON *ctx = cJSON_CreateObject();
    cJSON_AddItemToObject(ctx, "diagnostics", cJSON_CreateArray());
    cJSON_AddItemToObject(caparams, "context", ctx);
    cJSON *caresp = lsp_dispatch(&srv, make_req(4, "textDocument/codeAction", caparams));
    CHECK(caresp != NULL, "codeAction response");
    int found_export_action = 0;
    if (caresp) {
      cJSON *result = cJSON_GetObjectItemCaseSensitive(caresp, "result");
      if (result && cJSON_IsArray(result)) {
        int n = cJSON_GetArraySize(result);
        for (int i = 0; i < n; i++) {
          cJSON *action = cJSON_GetArrayItem(result, i);
          cJSON *title = cJSON_GetObjectItemCaseSensitive(action, "title");
          if (title && title->valuestring && strstr(title->valuestring, "export")) {
            found_export_action = 1;
            /* 验证 edit.changes 包含 TextEdit */
            cJSON *edit = cJSON_GetObjectItemCaseSensitive(action, "edit");
            cJSON *changes = edit ? cJSON_GetObjectItemCaseSensitive(edit, "changes") : NULL;
            if (changes) {
              cJSON *edits = cJSON_GetObjectItemCaseSensitive(changes, "file:///test_ca.spt");
              if (edits && cJSON_IsArray(edits) && cJSON_GetArraySize(edits) > 0) {
                cJSON *ed = cJSON_GetArrayItem(edits, 0);
                cJSON *nt = cJSON_GetObjectItemCaseSensitive(ed, "newText");
                if (nt && nt->valuestring && strcmp(nt->valuestring, "export ") == 0)
                  found_export_action = 1;
                else
                  found_export_action = 0;
              }
            }
          }
        }
      }
      cJSON_Delete(caresp);
    }
    CHECK(found_export_action, "codeAction offers 'Add export' with correct TextEdit");
  }

  /* ---- 4d. 增量同步 ---- */
  printf("--- 4d: incremental sync (didChange range) ---\n");
  {
    const char *text = "int f() {\n"
                       "  return 1;\n"
                       "}\n";
    cJSON *params = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", "file:///test_inc.spt");
    cJSON_AddStringToObject(td, "text", text);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp)
      cJSON_Delete(resp);

    /* 发送增量变更：将 "return 1;" 中的 "1" 替换为 "42" */
    cJSON *dparams = cJSON_CreateObject();
    cJSON *dtd = cJSON_CreateObject();
    cJSON_AddStringToObject(dtd, "uri", "file:///test_inc.spt");
    cJSON_AddNumberToObject(dtd, "version", 2);
    cJSON_AddItemToObject(dparams, "textDocument", dtd);
    cJSON *changes = cJSON_CreateArray();
    cJSON *change = cJSON_CreateObject();
    /* range: line 1, character 9-10 (即 "1" 的位置) */
    LspRange crange = {{1, 9}, {1, 10}};
    cJSON_AddItemToObject(change, "range", lsp_range_to_json(crange));
    cJSON_AddStringToObject(change, "text", "42");
    cJSON_AddItemToArray(changes, change);
    cJSON_AddItemToObject(dparams, "contentChanges", changes);
    cJSON *dresp = lsp_dispatch(&srv, make_notif("textDocument/didChange", dparams));
    if (dresp)
      cJSON_Delete(dresp);

    /* 验证文档内容已更新 */
    Document *d = doc_store_get(&srv.docs, "file:///test_inc.spt");
    CHECK(d != NULL, "document found after didChange");
    int inc_ok = 0;
    if (d) {
      /* 应为 "int f() {\n  return 42;\n}\n" */
      const char *expected = "int f() {\n  return 42;\n}\n";
      if (d->text_len == strlen(expected) && memcmp(d->text, expected, d->text_len) == 0)
        inc_ok = 1;
    }
    CHECK(inc_ok, "incremental didChange applied correctly (1->42)");
  }

  /* ---- 清理 ---- */
  lsp_server_free(&srv);

  printf("=== TestPhase4: %s (%d failures) ===\n", failed ? "FAIL" : "PASS", failed);
  return failed ? 1 : 0;
}
