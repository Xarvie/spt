/*
** test_diagnostics.c — 端到端（内存内）诊断：didOpen/didChange/didClose -> publishDiagnostics。
*/
#include "server.h"

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

/* 捕获服务器主动发出的消息（接管所有权）。 */
typedef struct {
  cJSON **msgs;
  int count, cap;
} Capture;

static void cap_emit(void *ctx, cJSON *msg) {
  Capture *c = (Capture *)ctx;
  if (c->count >= c->cap) {
    c->cap = c->cap ? c->cap * 2 : 8;
    c->msgs = (cJSON **)realloc(c->msgs, sizeof(cJSON *) * (size_t)c->cap);
  }
  c->msgs[c->count++] = msg;
}

static void cap_clear(Capture *c) {
  for (int i = 0; i < c->count; i++)
    cJSON_Delete(c->msgs[i]);
  c->count = 0;
}

/* 取最近一条指定方法的通知。 */
static cJSON *cap_last(Capture *c, const char *method) {
  for (int i = c->count - 1; i >= 0; i--) {
    cJSON *m = cJSON_GetObjectItemCaseSensitive(c->msgs[i], "method");
    if (m && cJSON_IsString(m) && strcmp(m->valuestring, method) == 0)
      return c->msgs[i];
  }
  return NULL;
}

static int diag_count_of(cJSON *publish) {
  if (!publish)
    return -1;
  cJSON *p = cJSON_GetObjectItemCaseSensitive(publish, "params");
  cJSON *arr = p ? cJSON_GetObjectItemCaseSensitive(p, "diagnostics") : NULL;
  return arr ? cJSON_GetArraySize(arr) : -1;
}

static cJSON *notif(const char *method, cJSON *params) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "jsonrpc", "2.0");
  cJSON_AddStringToObject(o, "method", method);
  cJSON_AddItemToObject(o, "params", params);
  return o;
}

static cJSON *did_open(const char *uri, const char *text, int version) {
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON_AddStringToObject(td, "languageId", "sptscript");
  cJSON_AddNumberToObject(td, "version", version);
  cJSON_AddStringToObject(td, "text", text);
  cJSON *p = cJSON_CreateObject();
  cJSON_AddItemToObject(p, "textDocument", td);
  return notif("textDocument/didOpen", p);
}

static cJSON *did_change(const char *uri, const char *text, int version) {
  cJSON *td = cJSON_CreateObject();
  cJSON_AddStringToObject(td, "uri", uri);
  cJSON_AddNumberToObject(td, "version", version);
  cJSON *ch = cJSON_CreateObject();
  cJSON_AddStringToObject(ch, "text", text);
  cJSON *arr = cJSON_CreateArray();
  cJSON_AddItemToArray(arr, ch);
  cJSON *p = cJSON_CreateObject();
  cJSON_AddItemToObject(p, "textDocument", td);
  cJSON_AddItemToObject(p, "contentChanges", arr);
  return notif("textDocument/didChange", p);
}

int main(void) {
  printf("=== TestDiagnostics: didOpen/didChange/didClose ===\n");
  LspServer s;
  lsp_server_init(&s);
  Capture cap = {0};
  lsp_server_set_emit(&s, cap_emit, &cap);

  /* initialize（响应直接返回，不经 emit） */
  cJSON *init = cJSON_CreateObject();
  cJSON_AddStringToObject(init, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(init, "id", 1);
  cJSON_AddStringToObject(init, "method", "initialize");
  cJSON *ir = lsp_dispatch(&s, init);
  cJSON_Delete(ir);
  cJSON_Delete(init);

  const char *URI = "file:///t.spt";

  /* didOpen 一个语法错误的文件 -> 至少 1 条诊断 */
  printf("Testing: didOpen broken file -> diagnostics...\n");
  cJSON *o = did_open(URI, "int x = ;\n", 1);
  cJSON *r1 = lsp_dispatch(&s, o);
  CHECK(r1 == NULL, "didOpen is a notification (no response)");
  cJSON_Delete(o);
  cJSON *pub = cap_last(&cap, "textDocument/publishDiagnostics");
  CHECK(pub != NULL, "publishDiagnostics emitted");
  CHECK(diag_count_of(pub) >= 1, "at least one diagnostic for broken file");
  if (pub) {
    cJSON *p = cJSON_GetObjectItemCaseSensitive(pub, "params");
    cJSON *uri = cJSON_GetObjectItemCaseSensitive(p, "uri");
    CHECK(uri && strcmp(uri->valuestring, URI) == 0, "uri matches");
    cJSON *arr = cJSON_GetObjectItemCaseSensitive(p, "diagnostics");
    cJSON *d0 = cJSON_GetArrayItem(arr, 0);
    CHECK(cJSON_GetObjectItemCaseSensitive(d0, "range") != NULL, "diagnostic has range");
    CHECK(cJSON_GetObjectItemCaseSensitive(d0, "message") != NULL, "diagnostic has message");
  }

  /* didChange 修正 -> 0 诊断 */
  printf("Testing: didChange fix -> zero diagnostics...\n");
  cap_clear(&cap);
  cJSON *c = did_change(URI, "int x = 1;\n", 2);
  cJSON *r2 = lsp_dispatch(&s, c);
  CHECK(r2 == NULL, "didChange notification");
  cJSON_Delete(c);
  pub = cap_last(&cap, "textDocument/publishDiagnostics");
  CHECK(pub != NULL, "publishDiagnostics emitted on change");
  CHECK(diag_count_of(pub) == 0, "zero diagnostics after fix");

  /* didClose -> 清空诊断（空数组） */
  printf("Testing: didClose -> clears diagnostics...\n");
  cap_clear(&cap);
  cJSON *cl_td = cJSON_CreateObject();
  cJSON_AddStringToObject(cl_td, "uri", URI);
  cJSON *cl_p = cJSON_CreateObject();
  cJSON_AddItemToObject(cl_p, "textDocument", cl_td);
  cJSON *cl = notif("textDocument/didClose", cl_p);
  cJSON *r3 = lsp_dispatch(&s, cl);
  CHECK(r3 == NULL, "didClose notification");
  cJSON_Delete(cl);
  pub = cap_last(&cap, "textDocument/publishDiagnostics");
  CHECK(pub != NULL && diag_count_of(pub) == 0, "empty diagnostics on close");

  cap_clear(&cap);
  free(cap.msgs);
  lsp_server_free(&s);

  if (failed == 0) {
    printf("=== TestDiagnostics: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestDiagnostics: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
