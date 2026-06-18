/*
** test_documents.c — 文档存储与位置换算（含多字节 UTF-16）单元测试。
*/
#include "documents.h"

#include <stdio.h>
#include <string.h>

static int failed = 0;
#define CHECK(cond, msg)                                                                            \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("  FAIL: %s\n", msg);                                                                 \
      failed++;                                                                                    \
    }                                                                                             \
  } while (0)

static void test_open_change_close(void) {
  printf("Testing: open/change/close + versions...\n");
  DocStore s;
  doc_store_init(&s);
  const char *t1 = "int x = 1;\n";
  Document *d = doc_store_open(&s, "file:///a.spt", t1, strlen(t1), 3);
  CHECK(d != NULL, "open returns doc");
  CHECK(d->version == 3, "version stored");
  CHECK(doc_store_get(&s, "file:///a.spt") == d, "get finds doc");
  CHECK(d->line_count == 2, "two lines (text + trailing newline)");

  const char *t2 = "int x = 1;\nint y = 2;\n";
  doc_store_change(&s, "file:///a.spt", t2, strlen(t2), 4);
  CHECK(d->version == 4, "version updated");
  CHECK(d->line_count == 3, "three lines after change");

  doc_store_close(&s, "file:///a.spt");
  CHECK(doc_store_get(&s, "file:///a.spt") == NULL, "closed doc gone");
  doc_store_free(&s);
}

static void test_crlf_normalization(void) {
  printf("Testing: CRLF normalized, line semantics preserved...\n");
  DocStore s;
  doc_store_init(&s);
  const char *t = "ab\r\ncd\r\n";
  Document *d = doc_store_open(&s, "u", t, strlen(t), 1);
  CHECK(d->text_len == 6, "CR removed (ab\\ncd\\n = 6 bytes)");
  CHECK(d->line_count == 3, "lines: 'ab','cd','' ");
  /* 第 1 行 'cd' 的第 0 字符偏移 = 'ab\n' = 3 */
  LspPos p = {1, 0};
  CHECK(doc_offset_at(d, p) == 3, "line1 char0 -> offset 3");
  doc_store_free(&s);
}

static void test_utf16_positions(void) {
  printf("Testing: UTF-16 character math (multibyte + astral)...\n");
  DocStore s;
  doc_store_init(&s);
  /* 行: "é🎉x"
     é = U+00E9 (UTF-8 2字节, UTF-16 1码元)
     🎉 = U+1F389 (UTF-8 4字节, UTF-16 2码元: surrogate pair)
     x = 1字节, 1码元
     字节布局: [C3 A9][F0 9F 8E 89][78]  共7字节
     UTF-16 列: é@0, 🎉@1(占2), x@3  => 'x' 在 character 3 */
  const char *t = "\xC3\xA9\xF0\x9F\x8E\x89x\n";
  Document *d = doc_store_open(&s, "u", t, strlen(t), 1);

  /* character 3 (x) -> byte offset 6 */
  LspPos px = {0, 3};
  CHECK(doc_offset_at(d, px) == 6, "char3 (x) -> byte 6");
  /* 反向：byte 6 -> character 3 */
  LspPos back = doc_pos_at(d, 6);
  CHECK(back.line == 0 && back.character == 3, "byte6 -> line0 char3");
  /* character 1 (🎉 起始) -> byte 2 */
  LspPos pe = {0, 1};
  CHECK(doc_offset_at(d, pe) == 2, "char1 (emoji) -> byte 2");
  /* byte 2 -> character 1 */
  LspPos b2 = doc_pos_at(d, 2);
  CHECK(b2.character == 1, "byte2 -> char1");

  doc_store_free(&s);
}

static void test_frontend_coords(void) {
  printf("Testing: frontend (1-based byte col) -> LSP position...\n");
  DocStore s;
  doc_store_init(&s);
  /* 行0: "  é = 1;"  (前导两空格 + é)
     前端列按字节计：é 起始在 byte col 3 (1-based)。
     字节: ' '(0) ' '(1) C3(2) A9(3) ... 所以 é 起始字节偏移 2 -> 前端 col 3。
     转 LSP：character 应为 2（两个空格各 1 码元）。 */
  const char *t = "  \xC3\xA9 = 1;\n";
  Document *d = doc_store_open(&s, "u", t, strlen(t), 1);
  LspPos p = doc_pos_from_frontend(d, 1, 3); /* line1, col3(bytes) */
  CHECK(p.line == 0 && p.character == 2, "frontend (1,3 bytes) -> LSP (0,2 utf16)");
  doc_store_free(&s);
}

int main(void) {
  printf("=== TestDocuments: store + UTF-16 positions ===\n");
  test_open_change_close();
  test_crlf_normalization();
  test_utf16_positions();
  test_frontend_coords();
  if (failed == 0) {
    printf("=== TestDocuments: ALL PASS ===\n");
    return 0;
  }
  printf("=== TestDocuments: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
