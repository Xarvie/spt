/* test_format.c — 从 IDE 日志录制的 formatting 请求回放
**
** 录制来源：lsp-debug.log id=26
** 请求：textDocument/formatting, format_demo.spt, tabSize=4, insertSpaces=false
** 验证：4 spaces (col=4) → 1 tab; 2 spaces (col=2) → 2 spaces; 行尾空白去除
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "server.h"
#include "workspace.h"
#include "lsp_features.h"
#include "documents.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

static int failed = 0;
#define CHECK(cond, msg) do { \
  if (!(cond)) { printf("  FAIL: %s\n", msg); failed = 1; } \
  else { printf("  OK: %s\n", msg); } \
} while(0)

static void sink_emit(void *ctx, cJSON *m) { (void)ctx; cJSON_Delete(m); }

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

/* 录制自日志：format_demo.spt 的完整内容 */
static const char *FORMAT_DEMO_TEXT =
  "/* format_demo.spt — 格式化测试（P4b）\n"
  "**\n"
  "** 测试要点：\n"
  "**   P4b: 打开此文件后执行 Format Document (Shift+Alt+F)\n"
  "**   验证：tab 被转为 4 空格，行尾空白被去除，缩进层级正确\n"
  "**\n"
  "** 格式化前：包含 tab/space 混合缩进 + 行尾空白 + 不规范缩进\n"
  "*/\n"
  "\n"
  "int f() {\n"
  "\treturn 1;\n"
  "  int x = 2;\n"
  "\tif (true) {\n"
  "\t\treturn 2;\n"
  "  }\n"
  "  for (int i = 0, 10) {\n"
  "    print(i);\n"
  "  }\n"
  "}\n"
  "\n"
  "class Demo {\n"
  "\tint x;\n"
  "\tint y;\n"
  "\tvoid __init(int x, int y) {\n"
  "\t\tthis.x = x;\n"
  "    this.y = y;\n"
  "\t}\n"
  "\tint getX() {\n"
  "\t\treturn this.x;\n"
  "\t}\n"
  "}\n"
  "\n"
  "int g(int a,\n"
  "  int b) {\n"
  "    return a + b;\n"
  "  }\n";

int main(void) {
  printf("=== TestFormat: formatting replay from IDE log ===\n");

  /* 创建临时工作区 */
  char dir[4096];
#ifdef _WIN32
  GetTempPathA(sizeof dir, dir);
  strcat_s(dir, sizeof dir, "spt_fmt_test");
#else
  strcpy(dir, "/tmp/spt_fmt_test");
#endif

  /* 清理旧的临时目录 */
#ifdef _WIN32
  {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\" 2>nul", dir);
    system(cmd);
  }
  CreateDirectoryA(dir, NULL);
#else
  rmdir(dir);
  mkdir(dir, 0755);
#endif

  write_file(dir, "format_demo.spt", FORMAT_DEMO_TEXT);

  /* 初始化 LSP server */
  LspServer srv;
  lsp_server_init(&srv);
  lsp_server_set_emit(&srv, sink_emit, NULL);

  char rootUri[4200];
  spt_path_to_uri(dir, rootUri, sizeof rootUri);

  /* initialize */
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "rootUri", rootUri);
    cJSON *resp = lsp_dispatch(&srv, make_req(1, "initialize", params));
    if (resp) cJSON_Delete(resp);
  }

  /* 触发 workspace 索引 */
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "query", "");
    cJSON *resp = lsp_dispatch(&srv, make_req(2, "workspace/symbol", params));
    if (resp) cJSON_Delete(resp);
  }

  char file_uri[4096];
  {
    char file_path[4096];
#ifdef _WIN32
    snprintf(file_path, sizeof file_path, "%s\\format_demo.spt", dir);
#else
    snprintf(file_path, sizeof file_path, "%s/format_demo.spt", dir);
#endif
    spt_path_to_uri(file_path, file_uri, sizeof file_uri);
  }

  /* didOpen format_demo.spt */
  {
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", file_uri);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "languageId", "sptscript");
    cJSON_AddStringToObject(td, "text", FORMAT_DEMO_TEXT);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp) cJSON_Delete(resp);
  }

  /* 录制自日志 id=26: textDocument/formatting, insertSpaces=false */
  printf("\n--- formatting: tabSize=4, insertSpaces=false ---\n");
  {
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", file_uri);
    cJSON *opts = cJSON_CreateObject();
    cJSON_AddNumberToObject(opts, "tabSize", 4);
    cJSON_AddBoolToObject(opts, "insertSpaces", 0);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "options", opts);

    cJSON *resp = lsp_dispatch(&srv, make_req(26, "textDocument/formatting", params));
    if (!resp) {
      CHECK(0, "formatting returned null response");
    } else {
      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (!result || !cJSON_IsArray(result) || cJSON_GetArraySize(result) == 0) {
        CHECK(0, "formatting returned empty result");
      } else {
        cJSON *ed = cJSON_GetArrayItem(result, 0);
        cJSON *nt = cJSON_GetObjectItem(ed, "newText");
        if (!nt || !cJSON_IsString(nt)) {
          CHECK(0, "formatting result has no newText");
        } else {
          const char *text = nt->valuestring;
          printf("  newText length: %zu\n", strlen(text));

          /* 逐行打印缩进分析（前 25 行）*/
          const char *p = text;
          int line = 0;
          while (*p && line < 25) {
            int tabs = 0, spaces = 0;
            while (*p == '\t') { tabs++; p++; }
            while (*p == ' ') { spaces++; p++; }
            const char *content = p;
            while (*p && *p != '\n') p++;
            if (*p == '\n') p++;
            int col = tabs * 4 + spaces;
            printf("  L%-2d: tabs=%d spaces=%d col=%d  %.*s\n",
                   line, tabs, spaces, col,
                   (int)(p - content > 40 ? 40 : p - content), content);
            line++;
          }

          /* 验证关键行 */

          /* "    print(i);" (4 spaces, col=4) → "\tprint(i);" (1 tab) */
          const char *pos = strstr(text, "print(i)");
          if (pos) {
            CHECK(pos > text && pos[-1] == '\t',
                  "4 spaces before print(i) converted to 1 tab");
          } else {
            CHECK(0, "print(i) not found");
          }

          /* "    this.y = y;" (4 spaces, col=4) → "\tthis.y = y;" (1 tab) */
          pos = strstr(text, "this.y = y");
          if (pos) {
            CHECK(pos > text && pos[-1] == '\t',
                  "4 spaces before this.y converted to 1 tab");
          } else {
            CHECK(0, "this.y = y not found");
          }

          /* "    return a + b;" (4 spaces, col=4) → "\treturn a + b;" (1 tab) */
          pos = strstr(text, "return a + b");
          if (pos) {
            CHECK(pos > text && pos[-1] == '\t',
                  "4 spaces before return a+b converted to 1 tab");
          } else {
            CHECK(0, "return a + b not found");
          }

          /* "  int x = 2;" (2 spaces, col=2) → "\tint x = 2;" (1 tab, 向上对齐到 col=4) */
          pos = strstr(text, "int x = 2");
          if (pos) {
            CHECK(pos > text && pos[-1] == '\t',
                  "2 spaces before int x=2 aligned up to 1 tab");
          }

          /* 行尾空白去除 */
          int trailing_ws = 0;
          const char *q = text;
          while (*q) {
            const char *eol = q;
            while (*eol && *eol != '\n') eol++;
            if (eol > q && (eol[-1] == ' ' || eol[-1] == '\t')) {
              trailing_ws = 1;
              break;
            }
            if (*eol == '\n') q = eol + 1;
            else break;
          }
          CHECK(!trailing_ws, "no trailing whitespace in formatted text");
        }
      }
      cJSON_Delete(resp);
    }
  }

  /* 测试 insertSpaces=true: tab 应转为 4 spaces */
  printf("\n--- formatting: tabSize=4, insertSpaces=true ---\n");
  {
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", file_uri);
    cJSON *opts = cJSON_CreateObject();
    cJSON_AddNumberToObject(opts, "tabSize", 4);
    cJSON_AddBoolToObject(opts, "insertSpaces", 1);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "options", opts);

    cJSON *resp = lsp_dispatch(&srv, make_req(27, "textDocument/formatting", params));
    if (resp) {
      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0) {
        cJSON *ed = cJSON_GetArrayItem(result, 0);
        cJSON *nt = cJSON_GetObjectItem(ed, "newText");
        if (nt && cJSON_IsString(nt)) {
          const char *text = nt->valuestring;

          /* tab 应转为 4 spaces: "return 1" 前应有 4 spaces */
          const char *pos = strstr(text, "return 1");
          if (pos) {
            CHECK(pos >= text + 4 && pos[-1] == ' ' && pos[-2] == ' ' &&
                       pos[-3] == ' ' && pos[-4] == ' ',
                  "tab before return 1 converted to 4 spaces");
          }

          /* 不应有 tab 字符 */
          int has_tab = 0;
          for (size_t i = 0; text[i]; i++) {
            if (text[i] == '\t') { has_tab = 1; break; }
          }
          CHECK(!has_tab, "no tab characters when insertSpaces=true");
        }
      }
      cJSON_Delete(resp);
    }
  }

  lsp_server_free(&srv);

  /* 清理 */
#ifdef _WIN32
  {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "rmdir /s /q \"%s\"", dir);
    system(cmd);
  }
#else
  {
    char cmd[4096];
    snprintf(cmd, sizeof cmd, "rm -rf \"%s\"", dir);
    system(cmd);
  }
#endif

  printf("\n=== %s ===\n", failed ? "FAIL" : "PASS");
  return failed ? 1 : 0;
}
