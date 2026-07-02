/*
** test_features.c — 经 lsp_dispatch 在内存内验证各语言功能。
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

/* 发请求，返回 result（仍属于 resp，调用方删除 *resp_out）。 */
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

static cJSON *find_by_name(cJSON *arr, const char *name) {
  if (!arr)
    return NULL;
  int n = cJSON_GetArraySize(arr);
  for (int i = 0; i < n; i++) {
    cJSON *it = cJSON_GetArrayItem(arr, i);
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
    if (nm && nm->valuestring && strcmp(nm->valuestring, name) == 0)
      return it;
  }
  return NULL;
}

static int array_has_label(cJSON *arr, const char *label) {
  if (!arr)
    return 0;
  int n = cJSON_GetArraySize(arr);
  for (int i = 0; i < n; i++) {
    cJSON *it = cJSON_GetArrayItem(arr, i);
    cJSON *l = cJSON_GetObjectItemCaseSensitive(it, "label");
    if (l && l->valuestring && strcmp(l->valuestring, label) == 0)
      return 1;
  }
  return 0;
}

static const char *DOC = "/// Adds two integers.\n"  /* 0 */
                         "int add(int a, int b) {\n" /* 1 */
                         "  int sum = a + b;\n"      /* 2 */
                         "  return sum;\n"           /* 3 */
                         "}\n"                       /* 4 */
                         "\n"                        /* 5 */
                         "class Point {\n"           /* 6 */
                         "  int x;\n"                /* 7 */
                         "  int getX() {\n"          /* 8 */
                         "    return x;\n"           /* 9 */
                         "  }\n"                     /* 10 */
                         "}\n"                       /* 11 */
                         "\n"                        /* 12 */
                         "int main() {\n"            /* 13 */
                         "  int r = add(1, 2);\n"    /* 14 */
                         "  return r;\n"             /* 15 */
                         "}\n";                      /* 16 */

int main(void) {
  printf("=== TestFeatures: "
         "documentSymbol/hover/definition/references/completion/signature/rename/semanticTokens/"
         "format + documentHighlight/foldingRange/selectionRange/prepareRename ===\n");
  LspServer s;
  lsp_server_init(&s);
  lsp_server_set_emit(&s, sink_emit, NULL);

  /* initialize */
  cJSON *im = cJSON_CreateObject();
  cJSON_AddStringToObject(im, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(im, "id", 1);
  cJSON_AddStringToObject(im, "method", "initialize");
  cJSON *ir = lsp_dispatch(&s, im);
  /* 顺带检查能力广告 */
  cJSON *caps = ir ? cJSON_GetObjectItemCaseSensitive(
                         cJSON_GetObjectItemCaseSensitive(ir, "result"), "capabilities")
                   : NULL;
  CHECK(caps && cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(caps, "hoverProvider")),
        "advertises hoverProvider");
  CHECK(caps && cJSON_GetObjectItemCaseSensitive(caps, "completionProvider"),
        "advertises completionProvider");
  CHECK(caps && cJSON_GetObjectItemCaseSensitive(caps, "semanticTokensProvider"),
        "advertises semanticTokensProvider");
  cJSON_Delete(ir);
  cJSON_Delete(im);

  const char *URI = "file:///main.spt";
  open_doc(&s, URI, DOC);

  cJSON *resp = NULL, *res = NULL;

  /* ---- documentSymbol ---- */
  printf("Testing: documentSymbol hierarchy...\n");
  res = call(&s, "textDocument/documentSymbol", posp(URI, 0, 0), &resp);
  CHECK(find_by_name(res, "add") != NULL, "symbol add present");
  CHECK(find_by_name(res, "main") != NULL, "symbol main present");
  cJSON *pt = find_by_name(res, "Point");
  CHECK(pt != NULL, "symbol Point present");
  if (pt) {
    cJSON *ch = cJSON_GetObjectItemCaseSensitive(pt, "children");
    CHECK(find_by_name(ch, "x") != NULL, "Point.x child present");
    CHECK(find_by_name(ch, "getX") != NULL, "Point.getX child present");
  }
  cJSON_Delete(resp);

  /* ---- hover on add (call site, line14 char11) ---- */
  printf("Testing: hover...\n");
  res = call(&s, "textDocument/hover", posp(URI, 14, 11), &resp);
  CHECK(res && !cJSON_IsNull(res), "hover returns content");
  if (res) {
    cJSON *c = cJSON_GetObjectItemCaseSensitive(res, "contents");
    cJSON *v = c ? cJSON_GetObjectItemCaseSensitive(c, "value") : NULL;
    CHECK(v && strstr(v->valuestring, "add("), "hover shows signature add(");
    if (v && strstr(v->valuestring, "Adds"))
      printf("    (doc-comment surfaced in hover)\n");
  }
  cJSON_Delete(resp);

  /* ---- definition of add ---- */
  printf("Testing: definition...\n");
  res = call(&s, "textDocument/definition", posp(URI, 14, 11), &resp);
  CHECK(res && !cJSON_IsNull(res), "definition returns location");
  if (res) {
    cJSON *rng = cJSON_GetObjectItemCaseSensitive(res, "range");
    cJSON *st = rng ? cJSON_GetObjectItemCaseSensitive(rng, "start") : NULL;
    cJSON *ln = st ? cJSON_GetObjectItemCaseSensitive(st, "line") : NULL;
    cJSON *cc = st ? cJSON_GetObjectItemCaseSensitive(st, "character") : NULL;
    CHECK(ln && ln->valueint == 1, "definition points to line 1");
    CHECK(cc && cc->valueint == 4, "definition points to char 4 (name 'add')");
  }
  cJSON_Delete(resp);

  /* ---- references on add (incl decl) -> >=2 ---- */
  printf("Testing: references...\n");
  {
    cJSON *p = posp(URI, 14, 11);
    cJSON *ctx = cJSON_CreateObject();
    cJSON_AddBoolToObject(ctx, "includeDeclaration", 1);
    cJSON_AddItemToObject(p, "context", ctx);
    res = call(&s, "textDocument/references", p, &resp);
    CHECK(res && cJSON_GetArraySize(res) >= 2, "references >= 2 for add (decl + call)");
    cJSON_Delete(resp);
  }

  /* ---- references on local sum -> 2, scoped ---- */
  res = call(&s, "textDocument/references", posp(URI, 3, 9), &resp); /* default ctx -> include */
  CHECK(res && cJSON_GetArraySize(res) == 2, "references == 2 for local sum (decl+use)");
  cJSON_Delete(resp);

  /* ---- completion (plain) ---- */
  printf("Testing: completion (plain + member)...\n");
  res = call(&s, "textDocument/completion", posp(URI, 15, 2), &resp);
  CHECK(array_has_label(res, "add"), "completion offers symbol add");
  CHECK(array_has_label(res, "main"), "completion offers symbol main");
  CHECK(array_has_label(res, "while"), "completion offers keyword while");
  cJSON_Delete(resp);

  /* ---- completion (member) ---- */
  {
    const char *U2 = "file:///m.spt";
    const char *T2 = "class C {\n  int val;\n  int fn() { return 0; }\n}\nany o;\no.\n";
    open_doc(&s, U2, T2);
    res = call(&s, "textDocument/completion", posp(U2, 5, 2), &resp); /* after 'o.' */
    CHECK(array_has_label(res, "val"), "member completion offers val");
    CHECK(array_has_label(res, "fn"), "member completion offers fn");
    cJSON_Delete(resp);
  }

  /* ---- signature help inside add(1, |2) ---- */
  printf("Testing: signatureHelp...\n");
  res = call(&s, "textDocument/signatureHelp", posp(URI, 14, 17), &resp);
  CHECK(res && !cJSON_IsNull(res), "signatureHelp returns");
  if (res) {
    cJSON *sigs = cJSON_GetObjectItemCaseSensitive(res, "signatures");
    cJSON *s0 = sigs ? cJSON_GetArrayItem(sigs, 0) : NULL;
    cJSON *label = s0 ? cJSON_GetObjectItemCaseSensitive(s0, "label") : NULL;
    cJSON *prm = s0 ? cJSON_GetObjectItemCaseSensitive(s0, "parameters") : NULL;
    cJSON *active = cJSON_GetObjectItemCaseSensitive(res, "activeParameter");
    CHECK(label && strstr(label->valuestring, "add("), "signature label has add(");
    CHECK(prm && cJSON_GetArraySize(prm) == 2, "signature has 2 parameters");
    CHECK(active && active->valueint == 1, "activeParameter == 1 (second arg)");
  }
  cJSON_Delete(resp);

  /* ---- rename add -> plus ---- */
  printf("Testing: rename...\n");
  {
    cJSON *p = posp(URI, 14, 11);
    cJSON_AddStringToObject(p, "newName", "plus");
    res = call(&s, "textDocument/rename", p, &resp);
    CHECK(res && !cJSON_IsNull(res), "rename returns WorkspaceEdit");
    if (res) {
      cJSON *ch = cJSON_GetObjectItemCaseSensitive(res, "changes");
      cJSON *edits = ch ? cJSON_GetObjectItemCaseSensitive(ch, URI) : NULL;
      CHECK(edits && cJSON_GetArraySize(edits) >= 2, "rename edits >= 2");
      if (edits) {
        cJSON *e0 = cJSON_GetArrayItem(edits, 0);
        cJSON *nt = cJSON_GetObjectItemCaseSensitive(e0, "newText");
        CHECK(nt && strcmp(nt->valuestring, "plus") == 0, "rename newText == plus");
      }
    }
    cJSON_Delete(resp);
  }

  /* ---- semantic tokens ---- */
  printf("Testing: semanticTokens/full...\n");
  res = call(&s, "textDocument/semanticTokens/full", posp(URI, 0, 0), &resp);
  if (res) {
    cJSON *data = cJSON_GetObjectItemCaseSensitive(res, "data");
    int len = data ? cJSON_GetArraySize(data) : 0;
    CHECK(len > 0 && (len % 5) == 0, "semantic tokens data non-empty, multiple of 5");
  } else
    CHECK(0, "semanticTokens returns object");
  cJSON_Delete(resp);

  /* ---- formatting ---- */
  printf("Testing: formatting...\n");
  {
    const char *U3 = "file:///f.spt";
    open_doc(&s, U3, "int x = 1;   \n"); /* trailing spaces */
    res = call(&s, "textDocument/formatting", posp(U3, 0, 0), &resp);
    CHECK(res && cJSON_GetArraySize(res) == 1, "format dirty doc -> 1 edit");
    if (res && cJSON_GetArraySize(res) == 1) {
      cJSON *e = cJSON_GetArrayItem(res, 0);
      cJSON *nt = cJSON_GetObjectItemCaseSensitive(e, "newText");
      CHECK(nt && strcmp(nt->valuestring, "int x = 1;\n") == 0, "format strips trailing ws");
    }
    cJSON_Delete(resp);

    const char *U4 = "file:///clean.spt";
    open_doc(&s, U4, "int x = 1;\n");
    res = call(&s, "textDocument/formatting", posp(U4, 0, 0), &resp);
    CHECK(res && cJSON_GetArraySize(res) == 0, "format clean doc -> 0 edits");
    cJSON_Delete(resp);
  }

  /* ---- documentHighlight on add (call site) -> >=2 (decl+use) ---- */
  printf("Testing: documentHighlight...\n");
  res = call(&s, "textDocument/documentHighlight", posp(URI, 14, 11), &resp);
  CHECK(res && cJSON_GetArraySize(res) >= 2, "documentHighlight >= 2 for add");
  if (res && cJSON_GetArraySize(res) >= 1) {
    cJSON *h0 = cJSON_GetArrayItem(res, 0);
    cJSON *kind = cJSON_GetObjectItemCaseSensitive(h0, "kind");
    CHECK(kind && kind->valueint == 1, "documentHighlight kind == Text(1)");
  }
  cJSON_Delete(resp);

  /* ---- foldingRange -> function bodies + class body ---- */
  printf("Testing: foldingRange...\n");
  res = call(&s, "textDocument/foldingRange", posp(URI, 0, 0), &resp);
  CHECK(res && cJSON_GetArraySize(res) >= 3, "foldingRange >= 3 (add/getX/main bodies)");
  cJSON_Delete(resp);

  /* ---- selectionRange on add -> range covering identifier ---- */
  printf("Testing: selectionRange...\n");
  res = call(&s, "textDocument/selectionRange", posp(URI, 14, 11), &resp);
  CHECK(res && !cJSON_IsNull(res), "selectionRange returns object");
  if (res) {
    cJSON *rng = cJSON_GetObjectItemCaseSensitive(res, "range");
    cJSON *st = rng ? cJSON_GetObjectItemCaseSensitive(rng, "start") : NULL;
    cJSON *ln = st ? cJSON_GetObjectItemCaseSensitive(st, "line") : NULL;
    CHECK(ln && ln->valueint == 14, "selectionRange start line == 14");
  }
  cJSON_Delete(resp);

  /* ---- prepareRename on add -> {range, placeholder:"add"} ---- */
  printf("Testing: prepareRename...\n");
  res = call(&s, "textDocument/prepareRename", posp(URI, 14, 11), &resp);
  CHECK(res && !cJSON_IsNull(res), "prepareRename returns object for add");
  if (res) {
    cJSON *ph = cJSON_GetObjectItemCaseSensitive(res, "placeholder");
    CHECK(ph && strcmp(ph->valuestring, "add") == 0, "prepareRename placeholder == add");
  }
  cJSON_Delete(resp);

  /* ---- prepareRename on declare ambient symbol -> null (rejected) ---- */
  {
    const char *U5 = "file:///decl.spt";
    const char *T5 = "declare int ExtFn(int x);\nint caller() { return ExtFn(1); }\n";
    open_doc(&s, U5, T5);
    /* ExtFn 定义在 declare 行（line0），use 在 line1 char18 */
    res = call(&s, "textDocument/prepareRename", posp(U5, 1, 18), &resp);
    CHECK(res == NULL || cJSON_IsNull(res), "prepareRename rejects declare ambient symbol");
    cJSON_Delete(resp);
    /* 普通函数 caller 可重命名 */
    res = call(&s, "textDocument/prepareRename", posp(U5, 1, 4), &resp);
    CHECK(res && !cJSON_IsNull(res), "prepareRename allows normal function caller");
    cJSON_Delete(resp);
  }

  lsp_server_free(&s);
  if (failed == 0) {
    printf("=== TestFeatures: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestFeatures: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
