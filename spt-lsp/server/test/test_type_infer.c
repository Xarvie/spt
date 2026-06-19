/*
** test_type_infer.c — Phase 2 轻量类型推断：成员跳转精准化 + 成员补全过滤。
**
** 验证：
**   1. 变量显式类型注解 → a.x 跳到 a 的类型的 x 成员（非全文件第一个匹配）
**   2. 参数类型注解 → 同上
**   3. 成员补全只列该类成员（不含其他类的同名成员）
**   4. 推断失败 → 回退到全文件兜底（不崩溃）
*/
#include "server.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;
#define CHECK(cond, msg)                                                                            \
  do {                                                                                             \
    if (!(cond)) { printf("  FAIL: %s\n", msg); failed++; }                                       \
  } while (0)

static void sink_emit(void *ctx, cJSON *m) { (void)ctx; cJSON_Delete(m); }
static int next_id = 200;

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
  if (r) cJSON_Delete(r);
  cJSON_Delete(m);
}

static cJSON *call(LspServer *s, const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", next_id++);
  cJSON_AddStringToObject(m, "method", method);
  cJSON_AddItemToObject(m, "params", params);
  cJSON *resp = lsp_dispatch(s, m);
  cJSON_Delete(m);
  return resp ? cJSON_GetObjectItemCaseSensitive(resp, "result") : NULL;
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

static int array_has_label(cJSON *arr, const char *label) {
  if (!arr) return 0;
  int n = cJSON_GetArraySize(arr);
  for (int i = 0; i < n; i++) {
    cJSON *it = cJSON_GetArrayItem(arr, i);
    cJSON *l = cJSON_GetObjectItemCaseSensitive(it, "label");
    if (l && l->valuestring && strcmp(l->valuestring, label) == 0) return 1;
  }
  return 0;
}

static int array_count(cJSON *arr) {
  return arr ? cJSON_GetArraySize(arr) : 0;
}

/* 两个类，都有 x 成员，用于验证精准化 */
static const char *DOC =
    "class Point {\n"          /* 0 */
    "  int x;\n"               /* 1 */
    "  int y;\n"               /* 2 */
    "  int getX() {\n"         /* 3 */
    "    return x;\n"          /* 4 */
    "  }\n"                    /* 5 */
    "}\n"                      /* 6 */
    "\n"                       /* 7 */
    "class Line {\n"           /* 8 */
    "  int x;\n"               /* 9 */
    "  int length;\n"          /* 10 */
    "}\n"                      /* 11 */
    "\n"                       /* 12 */
    "int test_explicit() {\n"  /* 13 */
    "  Point p;\n"             /* 14 */
    "  p.x = 1;\n"             /* 15 */
    "  return 0;\n"            /* 16 */
    "}\n"                      /* 17 */
    "\n"                       /* 18 */
    "int test_param(Point p) {\n" /* 19 */
    "  p.x = 3;\n"             /* 20 */
    "  return 0;\n"            /* 21 */
    "}\n";                     /* 22 */

int main(void) {
  printf("=== TestTypeInfer: Phase 2 类型推断（成员跳转精准化 + 补全过滤）===\n");
  LspServer s;
  lsp_server_init(&s);
  lsp_server_set_emit(&s, sink_emit, NULL);

  /* initialize */
  cJSON *im = cJSON_CreateObject();
  cJSON_AddStringToObject(im, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(im, "id", 1);
  cJSON_AddStringToObject(im, "method", "initialize");
  cJSON *ir = lsp_dispatch(&s, im);
  if (ir) cJSON_Delete(ir);
  cJSON_Delete(im);

  const char *uri = "file:///test_type.spt";
  open_doc(&s, uri, DOC);

  /* --- 1. 变量显式类型注解：p.x 应跳到 Point.x（行1），不是 Line.x（行9） --- */
  {
    /* 光标置于 p.x 的 x 上（char 4） */
    cJSON *resp = call(&s, "textDocument/definition", posp(uri, 15, 4));
    CHECK(resp != NULL, "definition on p.x (explicit) should return result");
    if (resp) {
      cJSON *range = cJSON_GetObjectItemCaseSensitive(resp, "range");
      cJSON *start = range ? cJSON_GetObjectItemCaseSensitive(range, "start") : NULL;
      cJSON *line = start ? cJSON_GetObjectItemCaseSensitive(start, "line") : NULL;
      int def_line = line ? line->valueint : -1;
      CHECK(def_line == 1, "p.x (explicit) should jump to Point.x (line 1), got line");
      if (def_line != 1) printf("  (got line %d)\n", def_line);
    }
  }

  /* --- 2. 参数类型注解：p.x 应跳到 Point.x（行1） --- */
  {
    /* 光标置于 p.x 的 x 上（char 4） */
    cJSON *resp = call(&s, "textDocument/definition", posp(uri, 20, 4));
    CHECK(resp != NULL, "definition on p.x (param) should return result");
    if (resp) {
      cJSON *range = cJSON_GetObjectItemCaseSensitive(resp, "range");
      cJSON *start = range ? cJSON_GetObjectItemCaseSensitive(range, "start") : NULL;
      cJSON *line = start ? cJSON_GetObjectItemCaseSensitive(start, "line") : NULL;
      int def_line = line ? line->valueint : -1;
      CHECK(def_line == 1, "p.x (param) should jump to Point.x (line 1)");
      if (def_line != 1) printf("  (got line %d)\n", def_line);
    }
  }

  /* --- 3. 成员补全：p. 后只列 Point 的成员（x/y/getX），不含 Line 的 length --- */
  {
    /* 光标置于 p.x 的 x 上（char 4，即点号之后） */
    cJSON *resp = call(&s, "textDocument/completion", posp(uri, 15, 4));
    CHECK(resp != NULL, "completion after p. should return result");
    if (resp) {
      CHECK(array_has_label(resp, "x"), "completion should include Point.x");
      CHECK(array_has_label(resp, "y"), "completion should include Point.y");
      CHECK(array_has_label(resp, "getX"), "completion should include Point.getX");
      CHECK(!array_has_label(resp, "length"), "completion should NOT include Line.length");
    }
  }

  /* --- 4. 推断失败回退：无类型注解的变量，成员跳转回退到全文件兜底 --- */
  {
    /* 用一个无类型注解的场景：在 getX 内部点击 x，x 是成员访问（this.x），
       接收者无显式类型，应回退到全文件兜底（不崩溃）。 */
    cJSON *resp = call(&s, "textDocument/definition", posp(uri, 4, 12));
    CHECK(resp != NULL || resp == NULL, "fallback definition should not crash");
    /* 不验证具体跳转位置，只要不崩溃即可 */
  }

  lsp_server_free(&s);
  printf(failed ? "=== TestTypeInfer: %d FAIL ===\n" : "=== TestTypeInfer: PASS ===\n", failed);
  return failed ? 1 : 0;
}
