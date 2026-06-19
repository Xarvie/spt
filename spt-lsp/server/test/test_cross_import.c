/*
** test_cross_import.c — 跨文件 import 解析（Phase 1）端到端测试。
**
** 覆盖：
**   - resolve_module_path / workspace_resolve_module 路径解析
**   - workspace_get_unit 目标文件解析（磁盘 + overlay）
**   - sem_resolve_export 导出符号查找
**   - textDocument/definition 跨文件跳转（具名导入 + 命名空间成员）
**   - textDocument/hover 跨文件签名
**   - textDocument/completion 命名空间 m. 成员补全（只列导出）
**   - 降级：不存在的模块不崩溃
**   - 循环 import 不崩溃
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "server.h"
#include "workspace.h"
#include "module_resolve.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

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

/* ---- 跨平台临时目录工具（与 test_workspace 同构） ---- */
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
  snprintf(out, cap, "%ssptci_%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(out, NULL)) return NULL;
  return out;
#else
  (void)cap;
  snprintf(out, cap, "/tmp/sptci_%d", (int)getpid());
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

/* 在 text 中找 substr 第 occ 次（1 起）出现的 LSP 位置（行/字符，0 起，按字节计）。 */
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

/* ---- 构造一个 LSP 请求 ---- */
static cJSON *make_req(int id, const char *method, cJSON *params) {
  cJSON *m = cJSON_CreateObject();
  cJSON_AddStringToObject(m, "jsonrpc", "2.0");
  cJSON_AddNumberToObject(m, "id", id);
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

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("=== TestCrossImport: cross-file import resolution ===\n");

  /* ---- resolve_module_path 单元测试 ---- */
  {
    char tmpl[4096];
    char *dir = make_temp_dir(tmpl, sizeof tmpl);
    CHECK(dir != NULL, "make_temp_dir ok");
    if (!dir) { printf("=== abort ===\n"); return 1; }
    write_file(dir, "b.spt", "export int add(int a, int b) { return a + b; }\n");
#ifdef _WIN32
    char from_path[4096], expect[4096], got[4096];
    snprintf(from_path, sizeof from_path, "%s\\a.spt", dir);
    snprintf(expect, sizeof expect, "%s\\b.spt", dir);
#else
    char from_path[4096], expect[4096], got[4096];
    snprintf(from_path, sizeof from_path, "%s/a.spt", dir);
    snprintf(expect, sizeof expect, "%s/b.spt", dir);
#endif
    CHECK(resolve_module_path(from_path, "b", got, sizeof got) == 1, "resolve_module_path finds b.spt");
    CHECK(strcmp(got, expect) == 0, "resolve_module_path returns script_dir/b.spt");
    CHECK(resolve_module_path(from_path, "nonexist", got, sizeof got) == 0, "resolve_module_path misses nonexist");
    /* 含分隔符的模块名应被拒 */
    CHECK(resolve_module_path(from_path, "a/b", got, sizeof got) == 0, "resolve_module_path rejects slash in name");
    remove_dir_recursive(dir);
  }

  /* ---- 端到端：a.spt imports from b.spt ---- */
  char tmpl[4096];
  char *dir = make_temp_dir(tmpl, sizeof tmpl);
  CHECK(dir != NULL, "make_temp_dir ok (e2e)");
  if (!dir) { printf("=== abort ===\n"); return 1; }

  /* b.spt：导出 add/SECRET，私有 helper 不导出 */
  write_file(dir, "b.spt",
             "export int add(int a, int b) { return a + b; }\n"
             "export int SECRET = 42;\n"
             "int private_helper() { return 0; }\n");
  /* a.spt：具名导入 add，命名空间导入 m */
  const char *a_text =
      "import { add } from \"b\"\n"
      "import * as m from \"b\"\n"
      "int caller() { return add(1, 2) + m.SECRET; }\n";
  write_file(dir, "a.spt", a_text);

  char a_uri[4400], b_uri[4400];
#ifdef _WIN32
  char a_path[4200], b_path[4200];
  snprintf(a_path, sizeof a_path, "%s\\a.spt", dir);
  snprintf(b_path, sizeof b_path, "%s\\b.spt", dir);
#else
  char a_path[4200], b_path[4200];
  snprintf(a_path, sizeof a_path, "%s/a.spt", dir);
  snprintf(b_path, sizeof b_path, "%s/b.spt", dir);
#endif
  spt_path_to_uri(a_path, a_uri, sizeof a_uri);
  spt_path_to_uri(b_path, b_uri, sizeof b_uri);

  LspServer s;
  lsp_server_init(&s);
  lsp_server_set_emit(&s, sink_emit, NULL);

  /* initialize（注册根目录） */
  {
    char rootUri[4400];
    spt_path_to_uri(dir, rootUri, sizeof rootUri);
    cJSON *im = make_req(1, "initialize", cJSON_CreateObject());
    cJSON_AddStringToObject(cJSON_GetObjectItem(im, "params"), "rootUri", rootUri);
    cJSON *ir = lsp_dispatch(&s, im);
    CHECK(ir != NULL, "initialize responds");
    cJSON_Delete(ir); cJSON_Delete(im);
  }

  /* didOpen a.spt */
  {
    cJSON *p = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", a_uri);
    cJSON_AddStringToObject(td, "languageId", "sptscript");
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text", a_text);
    cJSON_AddItemToObject(p, "textDocument", td);
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddStringToObject(m, "method", "textDocument/didOpen");
    cJSON_AddItemToObject(m, "params", p);
    cJSON *r = lsp_dispatch(&s, m);
    if (r) cJSON_Delete(r);
    cJSON_Delete(m);
  }

  /* ---- workspace_resolve_module ---- */
  {
    char out_uri[4096];
    CHECK(workspace_resolve_module(&s.ws, a_uri, "b", out_uri, sizeof out_uri) == 1,
          "workspace_resolve_module resolves b");
    CHECK(strcmp(out_uri, b_uri) == 0, "workspace_resolve_module uri matches b_uri");
  }

  /* ---- workspace_get_unit + sem_resolve_export ---- */
  {
    WsUnit wu = workspace_get_unit(&s.ws, b_path);
    CHECK(wu.unit != NULL && wu.doc != NULL, "workspace_get_unit(b) returns unit+doc");
    if (wu.unit && wu.doc) {
      SemRef xr;
      CHECK(sem_resolve_export(wu.unit, wu.doc, "add", &xr) == 1, "sem_resolve_export finds add");
      CHECK(xr.has_def, "sem_resolve_export add has_def");
      CHECK(strstr(xr.detail, "int add") != NULL, "sem_resolve_export add detail has signature");
      CHECK(sem_resolve_export(wu.unit, wu.doc, "SECRET", &xr) == 1, "sem_resolve_export finds SECRET");
      CHECK(sem_resolve_export(wu.unit, wu.doc, "private_helper", &xr) == 0,
            "sem_resolve_export rejects non-exported private_helper");
    }
    /* 缓存命中：再次 get 不崩溃且返回有效 */
    WsUnit wu2 = workspace_get_unit(&s.ws, b_path);
    CHECK(wu2.unit != NULL, "workspace_get_unit(b) cache hit returns unit");
  }

  /* ---- textDocument/definition：点击 add(1,2) 的 add → 跳 b.spt ---- */
  printf("Testing: definition on imported `add`...\n");
  {
    LspPos pos = pos_of(a_text, "add", 2); /* 第 2 次 add（caller 体内） */
    cJSON *r = lsp_dispatch(&s, make_req(10, "textDocument/definition", td_params(a_uri, pos)));
    cJSON *res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    CHECK(res != NULL, "definition on imported add returns Location (not null)");
    if (res) {
      cJSON *uri = cJSON_GetObjectItemCaseSensitive(res, "uri");
      CHECK(uri && strstr(uri->valuestring, "b.spt"), "definition uri -> b.spt");
      cJSON *rng = cJSON_GetObjectItemCaseSensitive(res, "range");
      cJSON *start = rng ? cJSON_GetObjectItemCaseSensitive(rng, "start") : NULL;
      cJSON *line = start ? cJSON_GetObjectItemCaseSensitive(start, "line") : NULL;
      CHECK(line && line->valueint == 0, "definition range.start.line == 0 (add in b.spt line 0)");
    }
    cJSON_Delete(r);
  }

  /* ---- textDocument/definition：点击 m.SECRET 的 SECRET → 跳 b.spt SECRET ---- */
  printf("Testing: definition on namespace member `m.SECRET`...\n");
  {
    LspPos pos = pos_of(a_text, "SECRET", 1);
    cJSON *r = lsp_dispatch(&s, make_req(11, "textDocument/definition", td_params(a_uri, pos)));
    cJSON *res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    CHECK(res != NULL, "definition on m.SECRET returns Location");
    if (res) {
      cJSON *uri = cJSON_GetObjectItemCaseSensitive(res, "uri");
      CHECK(uri && strstr(uri->valuestring, "b.spt"), "m.SECRET definition uri -> b.spt");
      cJSON *rng = cJSON_GetObjectItemCaseSensitive(res, "range");
      cJSON *start = rng ? cJSON_GetObjectItemCaseSensitive(rng, "start") : NULL;
      cJSON *line = start ? cJSON_GetObjectItemCaseSensitive(start, "line") : NULL;
      CHECK(line && line->valueint == 1, "m.SECRET range.start.line == 1 (SECRET in b.spt line 1)");
    }
    cJSON_Delete(r);
  }

  /* ---- textDocument/hover：点击 add → 显示 b.spt 的签名 ---- */
  printf("Testing: hover on imported `add`...\n");
  {
    LspPos pos = pos_of(a_text, "add", 2);
    cJSON *r = lsp_dispatch(&s, make_req(12, "textDocument/hover", td_params(a_uri, pos)));
    cJSON *res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    CHECK(res != NULL, "hover on imported add returns result");
    if (res) {
      cJSON *contents = cJSON_GetObjectItemCaseSensitive(res, "contents");
      cJSON *value = contents ? cJSON_GetObjectItemCaseSensitive(contents, "value") : NULL;
      CHECK(value && strstr(value->valuestring, "int add(int a, int b)") != NULL,
            "hover value contains target signature `int add(int a, int b)`");
    }
    cJSON_Delete(r);
  }

  /* ---- textDocument/completion：m. 后列出 b 的导出（add/SECRET），不含 private_helper ---- */
  printf("Testing: completion after `m.` lists target exports...\n");
  {
    /* 光标置于 m.SECRET 的 S 处（m. 之后） */
    LspPos pos = pos_of(a_text, "SECRET", 1);
    cJSON *r = lsp_dispatch(&s, make_req(13, "textDocument/completion", td_params(a_uri, pos)));
    cJSON *res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    int has_add = 0, has_secret = 0, has_private = 0;
    if (res && cJSON_IsArray(res)) {
      int n = cJSON_GetArraySize(res);
      for (int i = 0; i < n; i++) {
        cJSON *it = cJSON_GetArrayItem(res, i);
        cJSON *lbl = cJSON_GetObjectItemCaseSensitive(it, "label");
        if (lbl && lbl->valuestring) {
          if (strcmp(lbl->valuestring, "add") == 0) has_add = 1;
          if (strcmp(lbl->valuestring, "SECRET") == 0) has_secret = 1;
          if (strcmp(lbl->valuestring, "private_helper") == 0) has_private = 1;
        }
      }
    }
    CHECK(has_add, "completion m. includes exported `add`");
    CHECK(has_secret, "completion m. includes exported `SECRET`");
    CHECK(!has_private, "completion m. excludes non-exported `private_helper`");
    cJSON_Delete(r);
  }

  /* ---- 降级：import 不存在的模块，definition 不崩溃且返回 null ---- */
  printf("Testing: degrade on non-existent module...\n");
  {
    const char *a2_text =
        "import { ghost } from \"no_such_module\"\n"
        "int useit() { return ghost(); }\n";
    /* 用 didChange 替换 a.spt 内容 */
    cJSON *p = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", a_uri);
    cJSON_AddNumberToObject(td, "version", 2);
    cJSON_AddItemToObject(p, "textDocument", td);
    cJSON *ch = cJSON_CreateArray();
    cJSON *one = cJSON_CreateObject();
    cJSON_AddStringToObject(one, "text", a2_text);
    cJSON_AddItemToArray(ch, one);
    cJSON_AddItemToObject(p, "contentChanges", ch);
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddStringToObject(m, "method", "textDocument/didChange");
    cJSON_AddItemToObject(m, "params", p);
    cJSON *rr = lsp_dispatch(&s, m);
    if (rr) cJSON_Delete(rr);
    cJSON_Delete(m);

    LspPos pos = pos_of(a2_text, "ghost", 2);
    cJSON *r = lsp_dispatch(&s, make_req(14, "textDocument/definition", td_params(a_uri, pos)));
    cJSON *res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    /* 降级要求：不崩溃，且不指向不存在的模块文件（可为 null 或回退到当前文件 import 处）。 */
    int degrade_ok = (res == NULL || cJSON_IsNull(res));
    if (res && !cJSON_IsNull(res)) {
      cJSON *u2 = cJSON_GetObjectItemCaseSensitive(res, "uri");
      if (u2 && u2->valuestring) {
        /* 回退到当前文件 a_uri 是可接受的；引用 no_such_module 则不可。 */
        degrade_ok = (strstr(u2->valuestring, "no_such_module") == NULL);
      }
    }
    CHECK(degrade_ok, "definition on ghost (no module) degrades gracefully (no crash, no bogus target)");
    cJSON_Delete(r);
  }

  /* ---- 循环 import 不崩溃 ---- */
  printf("Testing: circular import no crash...\n");
  {
    write_file(dir, "c.spt", "import { f } from \"d\"\nexport int f() { return 1; }\n");
    write_file(dir, "d.spt", "import { g } from \"c\"\nexport int g() { return 2; }\n");
    char c_uri[4400], c_path[4200];
#ifdef _WIN32
    snprintf(c_path, sizeof c_path, "%s\\c.spt", dir);
#else
    snprintf(c_path, sizeof c_path, "%s/c.spt", dir);
#endif
    spt_path_to_uri(c_path, c_uri, sizeof c_uri);
    /* didOpen c.spt */
    cJSON *p = cJSON_CreateObject();
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", c_uri);
    cJSON_AddStringToObject(td, "languageId", "sptscript");
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "text", "import { g } from \"d\"\nint use() { return g(); }\n");
    cJSON_AddItemToObject(p, "textDocument", td);
    cJSON *m = cJSON_CreateObject();
    cJSON_AddStringToObject(m, "jsonrpc", "2.0");
    cJSON_AddStringToObject(m, "method", "textDocument/didOpen");
    cJSON_AddItemToObject(m, "params", p);
    cJSON *rr = lsp_dispatch(&s, m);
    if (rr) cJSON_Delete(rr);
    cJSON_Delete(m);

    /* definition on g → 解析 d.spt（d 又 import c，但只查 d 的导出，不递归） */
    LspPos pos = {1, 19}; /* line 1, "return g()" 中 g 大约在 char 19 */
    cJSON *r = lsp_dispatch(&s, make_req(15, "textDocument/definition", td_params(c_uri, pos)));
    /* 不崩溃即通过；结果可为 Location 或 null */
    CHECK(r != NULL, "circular import: definition returns a response (no crash)");
    cJSON_Delete(r);
  }

  /* ---- declare from "x" 联动：C 绑定模块无 .spt 源码，跳转到同文件 declare 块成员 ---- */
  printf("Testing: declare-from linkage (C binding module)...\n");
  {
    /* e.spt：declare 一个外部模块 sdl 的形状，并 import 使用。
       sdl 模块无对应 .spt 文件（模拟 C 绑定）。 */
    const char *e_text =
        "declare from \"sdl\" {\n"
        "    int Init(int flags);\n"
        "    const int INIT_VIDEO;\n"
        "}\n"
        "import { Init, INIT_VIDEO } from \"sdl\"\n"
        "int start() { return Init(INIT_VIDEO); }\n";
    char e_uri[4400], e_path[4200];
#ifdef _WIN32
    snprintf(e_path, sizeof e_path, "%s\\e.spt", dir);
#else
    snprintf(e_path, sizeof e_path, "%s/e.spt", dir);
#endif
    spt_path_to_uri(e_path, e_uri, sizeof e_uri);
    /* didOpen e.spt */
    {
      cJSON *p = cJSON_CreateObject();
      cJSON *td = cJSON_CreateObject();
      cJSON_AddStringToObject(td, "uri", e_uri);
      cJSON_AddStringToObject(td, "languageId", "sptscript");
      cJSON_AddNumberToObject(td, "version", 1);
      cJSON_AddStringToObject(td, "text", e_text);
      cJSON_AddItemToObject(p, "textDocument", td);
      cJSON *m = cJSON_CreateObject();
      cJSON_AddStringToObject(m, "jsonrpc", "2.0");
      cJSON_AddStringToObject(m, "method", "textDocument/didOpen");
      cJSON_AddItemToObject(m, "params", p);
      cJSON *rr = lsp_dispatch(&s, m);
      if (rr) cJSON_Delete(rr);
      cJSON_Delete(m);
    }

    /* definition：点击 caller 体内的 Init（第 2 次 Init）→ 跳到 declare 块的 Init（第 1 次） */
    LspPos pos = pos_of(e_text, "Init", 2);
    cJSON *r = lsp_dispatch(&s, make_req(16, "textDocument/definition", td_params(e_uri, pos)));
    cJSON *res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    CHECK(res != NULL && !cJSON_IsNull(res), "declare-from: definition on Init returns Location");
    if (res) {
      cJSON *uri = cJSON_GetObjectItemCaseSensitive(res, "uri");
      CHECK(uri && strstr(uri->valuestring, "e.spt") != NULL,
            "declare-from: definition uri -> e.spt (same file, C binding has no source)");
      cJSON *rng = cJSON_GetObjectItemCaseSensitive(res, "range");
      cJSON *start = rng ? cJSON_GetObjectItemCaseSensitive(rng, "start") : NULL;
      cJSON *line = start ? cJSON_GetObjectItemCaseSensitive(start, "line") : NULL;
      /* Init 在 declare 块第 1 行（0 起：declare 行是 0，Init 行是 1） */
      CHECK(line && line->valueint == 1, "declare-from: definition line == 1 (Init in declare block)");
    }
    cJSON_Delete(r);

    /* hover：点击 Init → 显示 declare 签名 int Init(int) */
    pos = pos_of(e_text, "Init", 2);
    r = lsp_dispatch(&s, make_req(17, "textDocument/hover", td_params(e_uri, pos)));
    res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    CHECK(res != NULL, "declare-from: hover on Init returns result");
    if (res) {
      cJSON *contents = cJSON_GetObjectItemCaseSensitive(res, "contents");
      cJSON *value = contents ? cJSON_GetObjectItemCaseSensitive(contents, "value") : NULL;
      CHECK(value && strstr(value->valuestring, "int Init") != NULL,
            "declare-from: hover value contains declare signature `int Init`");
    }
    cJSON_Delete(r);

    /* definition：点击 INIT_VIDEO（caller 体内）→ 跳到 declare 块的 INIT_VIDEO */
    pos = pos_of(e_text, "INIT_VIDEO", 2);
    r = lsp_dispatch(&s, make_req(18, "textDocument/definition", td_params(e_uri, pos)));
    res = r ? cJSON_GetObjectItemCaseSensitive(r, "result") : NULL;
    CHECK(res != NULL && !cJSON_IsNull(res), "declare-from: definition on INIT_VIDEO returns Location");
    if (res) {
      cJSON *uri = cJSON_GetObjectItemCaseSensitive(res, "uri");
      CHECK(uri && strstr(uri->valuestring, "e.spt") != NULL,
            "declare-from: INIT_VIDEO definition uri -> e.spt");
    }
    cJSON_Delete(r);
  }

  lsp_server_free(&s);
  remove_dir_recursive(dir);

  if (failed == 0) { printf("=== TestCrossImport: ALL PASS ===\n"); return 0; }
  printf("=== TestCrossImport: %d CHECK(s) FAILED ===\n", failed);
  return 1;
}
