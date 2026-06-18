/*
** test_rpc.c — spt_rpc 单元测试：Content-Length 分帧 + JSON-RPC 构造/解析。
*/
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

/* 构造一个测试消息：{"jsonrpc":"2.0","id":<id>,"method":<m>} */
static cJSON *mk_req(int id, const char *method) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddStringToObject(o, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(o, "id", id);
  cJSON_AddStringToObject(o, "method", method);
  return o;
}

/* ---- 1. 分帧往返 ---- */
static void test_frame_roundtrip(void) {
  printf("Testing: frame round-trip...\n");
  cJSON *msg = mk_req(7, "initialize");
  size_t flen = 0;
  char *framed = rpc_frame_to_string(msg, &flen);
  CHECK(framed != NULL, "frame produced");
  CHECK(strstr(framed, "Content-Length: ") == framed, "starts with Content-Length header");
  CHECK(strstr(framed, "\r\n\r\n") != NULL, "has header/body separator");

  RpcReader r;
  rpc_reader_init(&r);
  rpc_reader_feed(&r, framed, flen);
  const char *body = NULL;
  size_t blen = 0;
  int rc = rpc_reader_next(&r, &body, &blen);
  CHECK(rc == 1, "one complete message extracted");
  if (rc == 1) {
    cJSON *got = rpc_parse(body, blen);
    CHECK(got != NULL, "body parses");
    CHECK(rpc_is_request(got), "is request");
    CHECK(strcmp(rpc_method(got), "initialize") == 0, "method matches");
    cJSON *id = rpc_id(got);
    CHECK(id && cJSON_IsNumber(id) && id->valueint == 7, "id matches");
    cJSON_Delete(got);
  }
  /* 没有更多消息 */
  CHECK(rpc_reader_next(&r, &body, &blen) == 0, "no more messages");
  rpc_reader_free(&r);
  free(framed);
  cJSON_Delete(msg);
}

/* ---- 2. 一次 feed 两条消息 ---- */
static void test_two_messages(void) {
  printf("Testing: two messages in one feed...\n");
  cJSON *m1 = mk_req(1, "a");
  cJSON *m2 = mk_req(2, "b");
  size_t l1 = 0, l2 = 0;
  char *f1 = rpc_frame_to_string(m1, &l1);
  char *f2 = rpc_frame_to_string(m2, &l2);
  char *both = (char *)malloc(l1 + l2);
  memcpy(both, f1, l1);
  memcpy(both + l1, f2, l2);

  RpcReader r;
  rpc_reader_init(&r);
  rpc_reader_feed(&r, both, l1 + l2);
  const char *body;
  size_t blen;
  int rc1 = rpc_reader_next(&r, &body, &blen);
  CHECK(rc1 == 1, "first extracted");
  cJSON *g1 = rc1 == 1 ? rpc_parse(body, blen) : NULL;
  CHECK(g1 && strcmp(rpc_method(g1), "a") == 0, "first is 'a'");
  int rc2 = rpc_reader_next(&r, &body, &blen);
  CHECK(rc2 == 1, "second extracted");
  cJSON *g2 = rc2 == 1 ? rpc_parse(body, blen) : NULL;
  CHECK(g2 && strcmp(rpc_method(g2), "b") == 0, "second is 'b'");
  CHECK(rpc_reader_next(&r, &body, &blen) == 0, "no third");
  cJSON_Delete(g1);
  cJSON_Delete(g2);
  rpc_reader_free(&r);
  free(f1);
  free(f2);
  free(both);
  cJSON_Delete(m1);
  cJSON_Delete(m2);
}

/* ---- 3. 逐字节 feed（流式分帧） ---- */
static void test_byte_by_byte(void) {
  printf("Testing: byte-by-byte streaming...\n");
  cJSON *m = mk_req(42, "textDocument/didOpen");
  size_t flen = 0;
  char *framed = rpc_frame_to_string(m, &flen);

  RpcReader r;
  rpc_reader_init(&r);
  const char *body;
  size_t blen;
  int got = 0;
  for (size_t i = 0; i < flen; i++) {
    rpc_reader_feed(&r, framed + i, 1);
    int rc = rpc_reader_next(&r, &body, &blen);
    if (rc == 1) {
      got = 1;
      cJSON *g = rpc_parse(body, blen);
      CHECK(g && strcmp(rpc_method(g), "textDocument/didOpen") == 0, "method after full feed");
      cJSON_Delete(g);
    } else {
      CHECK(rc == 0, "partial returns 0 (not error) before complete");
    }
  }
  CHECK(got == 1, "message eventually extracted");
  rpc_reader_free(&r);
  free(framed);
  cJSON_Delete(m);
}

/* ---- 4. 协议错误：无 Content-Length ---- */
static void test_missing_content_length(void) {
  printf("Testing: missing Content-Length -> protocol error...\n");
  const char *bad = "X-Foo: bar\r\n\r\n{}";
  RpcReader r;
  rpc_reader_init(&r);
  rpc_reader_feed(&r, bad, strlen(bad));
  const char *body;
  size_t blen;
  int rc = rpc_reader_next(&r, &body, &blen);
  CHECK(rc == -1, "returns -1 when no Content-Length");
  rpc_reader_free(&r);
}

/* ---- 5. 额外头被忽略 + 大小写不敏感 ---- */
static void test_extra_headers(void) {
  printf("Testing: extra headers ignored, case-insensitive...\n");
  const char *body_json = "{\"jsonrpc\":\"2.0\",\"method\":\"ping\"}";
  char msg[256];
  int n = snprintf(msg, sizeof(msg),
                   "content-LENGTH: %zu\r\nContent-Type: application/vscode-jsonrpc\r\n\r\n%s",
                   strlen(body_json), body_json);
  RpcReader r;
  rpc_reader_init(&r);
  rpc_reader_feed(&r, msg, (size_t)n);
  const char *body;
  size_t blen;
  int rc = rpc_reader_next(&r, &body, &blen);
  CHECK(rc == 1, "extracts despite extra header + lowercase key");
  if (rc == 1) {
    cJSON *g = rpc_parse(body, blen);
    CHECK(g && !rpc_is_request(g), "notification (no id)");
    CHECK(g && strcmp(rpc_method(g), "ping") == 0, "method ping");
    cJSON_Delete(g);
  }
  rpc_reader_free(&r);
}

/* ---- 6. JSON-RPC 响应 / 错误 / 通知 构造 ---- */
static void test_message_construction(void) {
  printf("Testing: response/error/notification construction...\n");
  cJSON *id = cJSON_CreateNumber(9);

  cJSON *result = cJSON_CreateObject();
  cJSON_AddBoolToObject(result, "ok", 1);
  cJSON *resp = rpc_make_response(id, result);
  char *s = cJSON_PrintUnformatted(resp);
  CHECK(strstr(s, "\"jsonrpc\":\"2.0\"") != NULL, "resp has jsonrpc");
  CHECK(strstr(s, "\"id\":9") != NULL, "resp has id");
  CHECK(strstr(s, "\"result\"") != NULL, "resp has result");
  CHECK(strstr(s, "\"error\"") == NULL, "resp has no error");
  free(s);
  cJSON_Delete(resp);

  cJSON *err = rpc_make_error(id, RPC_METHOD_NOT_FOUND, "no such method");
  s = cJSON_PrintUnformatted(err);
  CHECK(strstr(s, "\"error\"") != NULL, "err has error");
  CHECK(strstr(s, "-32601") != NULL, "err code present");
  CHECK(strstr(s, "no such method") != NULL, "err message present");
  free(s);
  cJSON_Delete(err);

  cJSON *params = cJSON_CreateObject();
  cJSON_AddStringToObject(params, "uri", "file:///x.spt");
  cJSON *note = rpc_make_notification("textDocument/publishDiagnostics", params);
  s = cJSON_PrintUnformatted(note);
  CHECK(strstr(s, "publishDiagnostics") != NULL, "note method");
  CHECK(strstr(s, "\"id\"") == NULL, "note has no id");
  free(s);
  cJSON_Delete(note);

  cJSON_Delete(id);
}

int main(void) {
  printf("=== TestRpc: framing + JSON-RPC ===\n");
  test_frame_roundtrip();
  test_two_messages();
  test_byte_by_byte();
  test_missing_content_length();
  test_extra_headers();
  test_message_construction();
  if (failed == 0) {
    printf("=== TestRpc: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestRpc: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
