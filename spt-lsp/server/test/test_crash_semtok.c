/*
** test_crash_semtok.c — 复现 textDocument/semanticTokens/full 崩溃。
**
** 崩溃场景：VSCode 打开 test_phase1_1.spt，server 收到 semanticTokens/full 时崩溃。
** 根因：feature_semantic_tokens_full → gather() → ns_add() 里的 realloc 触发堆损坏。
**
** 复现步骤：initialize → didOpen(test_phase1_1.spt) → semanticTokens/full
** 预期：server 不崩溃，返回 semantic tokens data 数组。
** 实际：server 崩溃（exit code 0xC0000005），无响应。
**
** 录制来源：spt-lsp/server/lsp-record.jsonl（VSCode 真实操作录制）
*/
#include "server.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;
#define CHECK(cond, msg)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("  FAIL: %s\n", msg);                                                                 \
      failed++;                                                                                    \
    }                                                                                              \
  } while (0)

/* 丢弃服务器主动通知（诊断）。 */
static void sink_emit(void *ctx, cJSON *m) {
  (void)ctx;
  cJSON_Delete(m);
}

static int next_id = 100;

static void open_doc(LspServer *s, const char *uri, const char *text) {
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
  cJSON *r = lsp_dispatch(s, m);
  if (r)
    cJSON_Delete(r);
  cJSON_Delete(m);
}

static cJSON *call(LspServer *s, const char *method, cJSON *params, cJSON **resp_out) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", next_id++);
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  cJSON *resp = lsp_dispatch(s, m);
  cJSON_Delete(m);
  *resp_out = resp;
  return resp ? cJSON_GetObjectItemCaseSensitive(resp, "result") : NULL;
}

static cJSON *docp(const char *uri) {
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON *p = cJSON_CreateObject();
  cJSON_AddItemToObject(p, "textDocument", td);
  return p;
}

/* 录制文件原文：spt-lsp-backup/test/test_phase1_1.spt */
static const char *DOC_PHASE1_1 = "// Phase 1.1 变量声明与类型推断测试\n"
                                  "\n"
                                  "// 测试1: 基本类型声明\n"
                                  "// 预期: hover显示int类型，点击a可跳转到定义\n"
                                  "int a = 10;\n"
                                  "\n"
                                  "// 测试2: auto类型推断\n"
                                  "// 预期: hover显示推断类型int\n"
                                  "auto b = 10;\n"
                                  "\n"
                                  "// 测试3: auto推断字符串\n"
                                  "// 预期: hover显示推断类型str\n"
                                  "auto s = \"hello\";\n"
                                  "\n"
                                  "// 测试4: const声明\n"
                                  "// 预期: hover显示const int\n"
                                  "const int c = 100;\n"
                                  "\n"
                                  "// 测试5: 多变量声明\n"
                                  "// 预期: 每个变量独立跳转\n"
                                  "vars x, y, z;\n"
                                  "\n"
                                  "// 测试6: global声明\n"
                                  "// 预期: 正确识别全局变量\n"
                                  "global int g = 1;\n"
                                  "\n"
                                  "// 测试7: 浮点类型\n"
                                  "float f = 3.14;\n"
                                  "\n"
                                  "// 测试8: 字符串类型\n"
                                  "str name = \"test\";\n"
                                  "\n"
                                  "// 测试9: 布尔类型\n"
                                  "bool flag = true;\n"
                                  "\n"
                                  "// 测试10: 变量引用测试\n"
                                  "// 预期: 点击a可跳转到上面的定义\n"
                                  "void testFunc() {\n"
                                  "    a = 20;\n"
                                  "    b = a + 1;\n"
                                  "    s = \"world\";\n"
                                  "    c = 200;\n"
                                  "    g = 2;\n"
                                  "    x = 1;\n"
                                  "    y = 2;\n"
                                  "    z = 3;\n"
                                  "}\n";

int main(void) {
  printf("=== TestCrashSemtok: 复现 semanticTokens/full 崩溃 ===\n");
  printf("文件内容: test_phase1_1.spt（含 vars/global/多变量声明 + 函数）\n");
  printf("崩溃点: feature_semantic_tokens_full -> gather() -> ns_add() -> realloc\n\n");

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

  /* didOpen: 使用录制的 test_phase1_1.spt 原文 */
  const char *URI = "file:///test_phase1_1.spt";
  printf("步骤1: didOpen test_phase1_1.spt\n");
  open_doc(&s, URI, DOC_PHASE1_1);

  /* documentSymbol（录制中此步通过） */
  printf("步骤2: documentSymbol\n");
  cJSON *resp = NULL;
  cJSON *res = call(&s, "textDocument/documentSymbol", docp(URI), &resp);
  CHECK(res != NULL, "documentSymbol 返回结果");
  cJSON_Delete(resp);

  /* semanticTokens/full —— 崩溃点 */
  printf("步骤3: semanticTokens/full（崩溃点）\n");
  printf("  如果程序在这里崩溃（exit code 0xC0000005），说明 bug 已复现。\n");
  res = call(&s, "textDocument/semanticTokens/full", docp(URI), &resp);
  CHECK(res != NULL, "semanticTokens/full 返回结果（不崩溃）");
  if (res) {
    cJSON *data = cJSON_GetObjectItemCaseSensitive(res, "data");
    int len = data ? cJSON_GetArraySize(data) : 0;
    CHECK(len > 0 && (len % 5) == 0, "semantic tokens data 非空且为 5 的倍数");
    printf("  semantic tokens count: %d (groups: %d)\n", len, len / 5);
  }
  cJSON_Delete(resp);

  if (failed == 0)
    printf("\n结果: 通过（未崩溃）\n");
  else
    printf("\n结果: 失败（%d 项检查未通过）\n", failed);
  return failed ? 1 : 0;
}
