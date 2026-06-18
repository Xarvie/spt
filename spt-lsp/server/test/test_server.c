/*
** test_server.c — LSP 生命周期 + 分派单元测试（纯内存，不经 I/O）。
*/
#include "server.h"
#include "spt_rpc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failed = 0;
#define CHECK(cond, msg)                                                                            \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("  FAIL: %s\n", msg);                                                                 \
      failed++;                                                                                    \
    }                                                                                             \
  } while (0)

static cJSON *request(int id, const char *method) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(o, "id", id);
  cJSON_AddStringToObject(o, "method", method);
  return o;
}
static cJSON *notification(const char *method) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "jsonrpc", "2.0");
  cJSON_AddStringToObject(o, "method", method);
  return o;
}
/* 取响应的 error.code，无 error 返回 0。 */
static int err_code(const cJSON *resp) {
  cJSON *e = cJSON_GetObjectItemCaseSensitive((cJSON *)resp, "error");
  if (!e)
    return 0;
  cJSON *c = cJSON_GetObjectItemCaseSensitive(e, "code");
  return c ? c->valueint : 0;
}
static int has_result(const cJSON *resp) {
  return cJSON_GetObjectItemCaseSensitive((cJSON *)resp, "result") != NULL;
}

/* ---- 1. 未初始化时，非 initialize 请求 -> not initialized ---- */
static void test_not_initialized(void) {
  printf("Testing: request before initialize -> not initialized...\n");
  LspServer s;
  lsp_server_init(&s);
  cJSON *req = request(1, "textDocument/hover");
  cJSON *resp = lsp_dispatch(&s, req);
  CHECK(resp != NULL, "request gets a response");
  CHECK(err_code(resp) == RPC_SERVER_NOT_INITIALIZED, "code is server-not-initialized");
  cJSON_Delete(resp);
  cJSON_Delete(req);
}

/* ---- 2. initialize -> 返回 capabilities + serverInfo ---- */
static void test_initialize(void) {
  printf("Testing: initialize handshake...\n");
  LspServer s;
  lsp_server_init(&s);
  cJSON *req = request(1, "initialize");
  cJSON *resp = lsp_dispatch(&s, req);
  CHECK(resp != NULL && has_result(resp), "initialize returns result");
  CHECK(s.state == LSP_INITIALIZED, "state -> initialized");
  if (resp && has_result(resp)) {
    cJSON *result = cJSON_GetObjectItemCaseSensitive(resp, "result");
    cJSON *caps = cJSON_GetObjectItemCaseSensitive(result, "capabilities");
    CHECK(caps != NULL, "has capabilities");
    cJSON *sync = caps ? cJSON_GetObjectItemCaseSensitive(caps, "textDocumentSync") : NULL;
    CHECK(sync && cJSON_IsNumber(sync), "advertises textDocumentSync");
    cJSON *info = cJSON_GetObjectItemCaseSensitive(result, "serverInfo");
    CHECK(info != NULL, "has serverInfo");
  }
  cJSON_Delete(resp);
  cJSON_Delete(req);

  /* 二次 initialize -> invalid request */
  cJSON *req2 = request(2, "initialize");
  cJSON *resp2 = lsp_dispatch(&s, req2);
  CHECK(err_code(resp2) == RPC_INVALID_REQUEST, "double initialize -> invalid request");
  cJSON_Delete(resp2);
  cJSON_Delete(req2);
}

/* ---- 3. 已初始化后，未知方法 -> method not found ---- */
static void test_unknown_method(void) {
  printf("Testing: unknown method after init -> method not found...\n");
  LspServer s;
  lsp_server_init(&s);
  cJSON *init = request(1, "initialize");
  cJSON *ir = lsp_dispatch(&s, init);
  cJSON_Delete(ir);
  cJSON_Delete(init);

  cJSON *req = request(2, "frobnicate/foo");
  cJSON *resp = lsp_dispatch(&s, req);
  CHECK(err_code(resp) == RPC_METHOD_NOT_FOUND, "unknown -> -32601");
  cJSON_Delete(resp);
  cJSON_Delete(req);
}

/* ---- 4. shutdown -> result null, 状态切换；之后请求 -> invalid ---- */
static void test_shutdown(void) {
  printf("Testing: shutdown lifecycle...\n");
  LspServer s;
  lsp_server_init(&s);
  cJSON *init = request(1, "initialize");
  cJSON *ir = lsp_dispatch(&s, init);
  cJSON_Delete(ir);
  cJSON_Delete(init);

  cJSON *sd = request(2, "shutdown");
  cJSON *resp = lsp_dispatch(&s, sd);
  CHECK(resp != NULL && has_result(resp) && err_code(resp) == 0, "shutdown returns result(null)");
  CHECK(s.state == LSP_SHUTDOWN, "state -> shutdown");
  cJSON_Delete(resp);
  cJSON_Delete(sd);

  cJSON *after = request(3, "textDocument/hover");
  cJSON *ar = lsp_dispatch(&s, after);
  CHECK(err_code(ar) == RPC_INVALID_REQUEST, "request after shutdown -> invalid");
  cJSON_Delete(ar);
  cJSON_Delete(after);
}

/* ---- 5. 通知不产生响应；exit 置退出标志与退出码 ---- */
static void test_notifications(void) {
  printf("Testing: notifications + exit...\n");
  LspServer s;
  lsp_server_init(&s);
  cJSON *init = request(1, "initialize");
  cJSON *ir = lsp_dispatch(&s, init);
  cJSON_Delete(ir);
  cJSON_Delete(init);

  cJSON *inited = notification("initialized");
  cJSON *r1 = lsp_dispatch(&s, inited);
  CHECK(r1 == NULL, "initialized notification -> no response");
  cJSON_Delete(inited);

  /* shutdown 后 exit -> 退出码 0 */
  cJSON *sd = request(2, "shutdown");
  cJSON *sr = lsp_dispatch(&s, sd);
  cJSON_Delete(sr);
  cJSON_Delete(sd);

  cJSON *ex = notification("exit");
  cJSON *r2 = lsp_dispatch(&s, ex);
  CHECK(r2 == NULL, "exit notification -> no response");
  CHECK(s.should_exit == true, "should_exit set");
  CHECK(s.exit_code == 0, "exit code 0 after shutdown");
  cJSON_Delete(ex);
}

/* ---- 6. 未 shutdown 直接 exit -> 退出码 1 ---- */
static void test_exit_without_shutdown(void) {
  printf("Testing: exit without shutdown -> code 1...\n");
  LspServer s;
  lsp_server_init(&s);
  cJSON *ex = notification("exit");
  cJSON *r = lsp_dispatch(&s, ex);
  CHECK(r == NULL, "no response");
  CHECK(s.should_exit && s.exit_code == 1, "exit code 1 when not shut down");
  cJSON_Delete(ex);
}

int main(void) {
  printf("=== TestServer: lifecycle + dispatch ===\n");
  test_not_initialized();
  test_initialize();
  test_unknown_method();
  test_shutdown();
  test_notifications();
  test_exit_without_shutdown();
  if (failed == 0) {
    printf("=== TestServer: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestServer: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
