/*
** test_manual_def.c — 手工测试 definition 跳转调试。
**
** 复现用户场景：在 services.spt 中点击 calculate 调用处，
** 验证 definition 是否跨文件跳转到 utils.spt 的定义。
**
** 打印每一步的请求和响应 JSON，便于定位问题。
*/
#define _DEFAULT_SOURCE 1
#define _XOPEN_SOURCE 700

#include "documents.h"
#include "lsp_features.h"
#include "semantic.h"
#include "server.h"
#include "spt_lsp_bridge.h"
#include "workspace.h"

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
#define CHECK(cond, msg)                                                                           \
  do {                                                                                             \
    if (!(cond)) {                                                                                 \
      printf("  FAIL: %s\n", msg);                                                                 \
      failed++;                                                                                    \
    } else {                                                                                       \
      printf("  OK:   %s\n", msg);                                                                 \
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

/* notification：无 id，用于 didOpen/didChange 等。 */
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
  if (f) {
    fputs(content, f);
    fclose(f);
  }
}

static char *make_temp_dir(char *out, size_t cap) {
#ifdef _WIN32
  char base[MAX_PATH];
  if (!GetTempPathA(MAX_PATH, base))
    return NULL;
  snprintf(out, cap, "%ssptdef_%lu", base, (unsigned long)GetCurrentProcessId());
  if (!CreateDirectoryA(out, NULL))
    return NULL;
  return out;
#else
  snprintf(out, cap, "/tmp/sptdef_%d", (int)getpid());
  if (mkdir(out, 0777) != 0)
    return NULL;
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
      if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
        continue;
      char full[MAX_PATH];
      snprintf(full, sizeof full, "%s\\%s", path, nm);
      if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
        remove_dir_recursive(full);
      else
        DeleteFileA(full);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
  }
  RemoveDirectoryA(path);
#else
  char cmd[4200];
  snprintf(cmd, sizeof cmd, "rm -rf %s", path);
  system(cmd);
#endif
}

/* 在文本中找子串第 occ 次出现的字节偏移（0 起）。 */
static size_t byte_of(const char *text, const char *sub, int occ) {
  const char *p = text;
  for (int i = 0; i < occ; i++) {
    p = strstr(p, sub);
    if (!p)
      return (size_t)-1;
    if (i < occ - 1)
      p += strlen(sub);
  }
  return (size_t)(p - text);
}

int main(void) {
  setvbuf(stdout, NULL, _IONBF, 0);
  printf("=== TestManualDef: definition jump debug ===\n");

  char tmpl[4096];
  char *dir = make_temp_dir(tmpl, sizeof tmpl);
  CHECK(dir != NULL, "make_temp_dir");
  if (!dir) {
    printf("=== abort ===\n");
    return 1;
  }

  /* 写入测试文件（与 manual_test_p3_p5 相同结构） */
  write_file(dir, "utils.spt",
             "export int calculate(int a, int b) {\n"
             "  return a + b;\n"
             "}\n"
             "export int formatTime(int seconds) {\n"
             "  return seconds;\n"
             "}\n"
             "export str greet(str name) {\n"
             "  return name;\n"
             "}\n");

  write_file(dir, "models.spt",
             "export class User {\n"
             "  int id;\n"
             "  str name;\n"
             "  void __init(int id, str name) {\n"
             "    this.id = id;\n"
             "    this.name = name;\n"
             "  }\n"
             "  str getName() { return this.name; }\n"
             "}\n");

  write_file(dir, "services.spt",
             "import { calculate } from \"./utils\";\n"
             "import { User } from \"./models\";\n"
             "import * as M from \"./utils\";\n"
             "\n"
             "export int computeScore(int base, int bonus) {\n"
             "  return calculate(base, bonus);\n"
             "}\n"
             "\n"
             "export str getGreeting(str name) {\n"
             "  return M.greet(name);\n"
             "}\n");

  write_file(dir, "app.spt",
             "import { computeScore } from \"./services\";\n"
             "import { User } from \"./models\";\n"
             "\n"
             "int main() {\n"
             "  int result = computeScore(10, 20);\n"
             "  User admin = User(1, \"admin\");\n"
             "  return result;\n"
             "}\n");

  LspServer srv;
  lsp_server_init(&srv);
  lsp_server_set_emit(&srv, sink_emit, NULL);

  /* initialize */
  char rootUri[4200];
  spt_path_to_uri(dir, rootUri, sizeof rootUri);
  printf("\n--- initialize (rootUri=%s) ---\n", rootUri);
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "rootUri", rootUri);
    cJSON *resp = lsp_dispatch(&srv, make_req(1, "initialize", params));
    if (resp) {
      char *s = cJSON_Print(resp);
      printf("  resp: %s\n", s);
      free(s);
      cJSON_Delete(resp);
    }
  }

  /* 触发 workspace 索引 */
  printf("\n--- workspace/symbol (trigger index) ---\n");
  {
    cJSON *params = cJSON_CreateObject();
    cJSON_AddStringToObject(params, "query", "");
    cJSON *resp = lsp_dispatch(&srv, make_req(2, "workspace/symbol", params));
    if (resp) {
      cJSON *result = cJSON_GetObjectItem(resp, "result");
      printf("  symbol count: %d\n",
             result && cJSON_IsArray(result) ? cJSON_GetArraySize(result) : -1);
      cJSON_Delete(resp);
    }
  }

  /* 构建 services.spt 的 URI */
  char svc_path[4096], svc_uri[4096];
#ifdef _WIN32
  snprintf(svc_path, sizeof svc_path, "%s\\services.spt", dir);
#else
  snprintf(svc_path, sizeof svc_path, "%s/services.spt", dir);
#endif
  spt_path_to_uri(svc_path, svc_uri, sizeof svc_uri);

  /* didOpen services.spt */
  printf("\n--- didOpen services.spt ---\n");
  {
    const char *text = "import { calculate } from \"./utils\";\n"
                       "import { User } from \"./models\";\n"
                       "import * as M from \"./utils\";\n"
                       "\n"
                       "export int computeScore(int base, int bonus) {\n"
                       "  return calculate(base, bonus);\n"
                       "}\n"
                       "\n"
                       "export str getGreeting(str name) {\n"
                       "  return M.greet(name);\n"
                       "}\n";

    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", svc_uri);
    cJSON_AddNumberToObject(td, "version", 1);
    cJSON_AddStringToObject(td, "languageId", "spt");
    cJSON_AddStringToObject(td, "text", text);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON *resp = lsp_dispatch(&srv, make_notif("textDocument/didOpen", params));
    if (resp)
      cJSON_Delete(resp);
  }

  /* 测试 1: definition of `calculate` at call site (line 6, col ~12)
     services.spt 第 6 行: "  return calculate(base, bonus);"
     calculate 出现在第 10 个字符位置（0 起行 5, 字符 9） */
  printf("\n--- definition: calculate at call site ---\n");
  {
    /* calculate 在 services.spt 中第 2 次出现（第 1 次在 import） */
    const char *svc_text = "import { calculate } from \"./utils\";\n"
                           "import { User } from \"./models\";\n"
                           "import * as M from \"./utils\";\n"
                           "\n"
                           "export int computeScore(int base, int bonus) {\n"
                           "  return calculate(base, bonus);\n"
                           "}\n";
    size_t off = byte_of(svc_text, "calculate", 2);
    printf("  byte offset of 2nd 'calculate': %zu\n", off);

    /* 转换为 LSP position */
    int line = 0, col = 0;
    for (size_t i = 0; i < off; i++) {
      if (svc_text[i] == '\n') {
        line++;
        col = 0;
      } else
        col++;
    }
    printf("  position: line=%d, character=%d\n", line, col);

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", col);
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", svc_uri);
    cJSON_AddItemToObject(td, "position", pos);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON *resp = lsp_dispatch(&srv, make_req(4, "textDocument/definition", params));
    if (resp) {
      char *s = cJSON_Print(resp);
      printf("  resp: %s\n", s);
      free(s);

      /* 验证：应跳到 utils.spt */
      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsObject(result)) {
        const char *resp_uri = cJSON_GetObjectItem(result, "uri")
                                   ? cJSON_GetObjectItem(result, "uri")->valuestring
                                   : NULL;
        printf("  result uri: %s\n", resp_uri ? resp_uri : "(null)");
        CHECK(resp_uri && strstr(resp_uri, "utils.spt"), "definition jumps to utils.spt");
      } else {
        CHECK(0, "definition returned non-object result");
      }
      cJSON_Delete(resp);
    } else {
      CHECK(0, "definition returned null response");
    }
  }

  /* 测试 2: definition of `M.greet` (namespace member) */
  printf("\n--- definition: M.greet (namespace member) ---\n");
  {
    const char *svc_text = "import { calculate } from \"./utils\";\n"
                           "import { User } from \"./models\";\n"
                           "import * as M from \"./utils\";\n"
                           "\n"
                           "export int computeScore(int base, int bonus) {\n"
                           "  return calculate(base, bonus);\n"
                           "}\n"
                           "\n"
                           "export str getGreeting(str name) {\n"
                           "  return M.greet(name);\n"
                           "}\n";
    size_t off = byte_of(svc_text, "greet", 1);
    printf("  byte offset of 'greet': %zu\n", off);

    int line = 0, col = 0;
    for (size_t i = 0; i < off; i++) {
      if (svc_text[i] == '\n') {
        line++;
        col = 0;
      } else
        col++;
    }
    printf("  position: line=%d, character=%d\n", line, col);

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", col);
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", svc_uri);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON *resp = lsp_dispatch(&srv, make_req(5, "textDocument/definition", params));
    if (resp) {
      char *s = cJSON_Print(resp);
      printf("  resp: %s\n", s);
      free(s);

      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsObject(result)) {
        const char *resp_uri = cJSON_GetObjectItem(result, "uri")
                                   ? cJSON_GetObjectItem(result, "uri")->valuestring
                                   : NULL;
        CHECK(resp_uri && strstr(resp_uri, "utils.spt"), "M.greet jumps to utils.spt");
      } else {
        CHECK(0, "M.greet definition returned non-object result");
      }
      cJSON_Delete(resp);
    }
  }

  /* 测试 3: 直接测试 resolve_module_path */
  printf("\n--- direct test: resolve_module_path ---\n");
  {
    char out[4096];
    int ok = resolve_module_path(svc_path, "./utils", out, sizeof out);
    printf("  resolve_module_path(\"%s\", \"./utils\") = %d, out=\"%s\"\n", svc_path, ok,
           ok ? out : "(null)");
    CHECK(ok, "resolve_module_path handles ./utils");
    if (ok) {
      /* 验证文件存在 */
#ifdef _WIN32
      DWORD a = GetFileAttributesA(out);
      int exists = a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
#else
      struct stat st;
      int exists = stat(out, &st) == 0 && S_ISREG(st.st_mode);
#endif
      printf("  file exists: %d\n", exists);
      CHECK(exists, "resolved path exists on disk");
    }
  }

  /* 测试 4: 直接测试 workspace_resolve_module */
  printf("\n--- direct test: workspace_resolve_module ---\n");
  {
    char tgt_uri[4096];
    int ok = workspace_resolve_module(&srv.ws, svc_uri, "./utils", tgt_uri, sizeof tgt_uri);
    printf("  workspace_resolve_module(\"%s\", \"./utils\") = %d, tgt=\"%s\"\n", svc_uri, ok,
           ok ? tgt_uri : "(null)");
    CHECK(ok, "workspace_resolve_module resolves ./utils");

    /* 测试 4b: 直接测试 workspace_get_unit + sem_resolve_export */
    if (ok) {
      char tgt_path[4096];
      spt_uri_to_path(tgt_uri, tgt_path, sizeof tgt_path);
      printf("\n--- direct test: workspace_get_unit + sem_resolve_export ---\n");
      printf("  tgt_path: \"%s\"\n", tgt_path);
      WsUnit wu = workspace_get_unit(&srv.ws, tgt_path);
      printf("  wu.unit: %p, wu.doc: %p\n", (void *)wu.unit, (void *)wu.doc);
      CHECK(wu.unit != NULL, "workspace_get_unit returns unit for utils.spt");
      CHECK(wu.doc != NULL, "workspace_get_unit returns doc for utils.spt");
      if (wu.unit && wu.doc) {
        SemRef xr;
        int found = sem_resolve_export(wu.unit, wu.doc, "calculate", &xr);
        printf("  sem_resolve_export(\"calculate\"): %d, has_def=%d\n", found,
               found ? xr.has_def : 0);
        CHECK(found && xr.has_def, "sem_resolve_export finds calculate in utils.spt");
        if (found && xr.has_def) {
          printf("  def_start=%zu, def_end=%zu\n", xr.def_start, xr.def_end);
        }
      }
    }
  }

  /* 测试 5: 直接调用 feature_definition（绕过 dispatch） */
  printf("\n--- direct test: feature_definition ---\n");
  {
    Document *d = doc_store_get(&srv.docs, svc_uri);
    printf("  doc_store_get: %p\n", (void *)d);
    CHECK(d != NULL, "doc_store_get returns services.spt doc");
    if (d) {
      const char *svc_text = "import { calculate } from \"./utils\";\n"
                             "import { User } from \"./models\";\n"
                             "import * as M from \"./utils\";\n"
                             "\n"
                             "export int computeScore(int base, int bonus) {\n"
                             "  return calculate(base, bonus);\n"
                             "}\n";
      size_t off = byte_of(svc_text, "calculate", 2);
      int line = 0, col = 0;
      for (size_t i = 0; i < off; i++) {
        if (svc_text[i] == '\n') {
          line++;
          col = 0;
        } else
          col++;
      }
      LspPos pos = {line, col};
      cJSON *def = feature_definition(d, pos, svc_uri, &srv.ws);
      if (def) {
        char *s = cJSON_Print(def);
        printf("  feature_definition result: %s\n", s);
        free(s);
        cJSON_Delete(def);
      } else {
        printf("  feature_definition returned NULL\n");
        CHECK(0, "feature_definition should find calculate");
      }
    }
  }

  /* 测试 6: 直接测试 sem_resolve_import_target */
  printf("\n--- direct test: sem_resolve_import_target ---\n");
  {
    const char *svc_text = "import { calculate } from \"./utils\";\n"
                           "import { User } from \"./models\";\n"
                           "import * as M from \"./utils\";\n"
                           "\n"
                           "export int computeScore(int base, int bonus) {\n"
                           "  return calculate(base, bonus);\n"
                           "}\n";
    SptLspUnit *u = spt_lsp_parse(svc_text, strlen(svc_text));
    CHECK(u != NULL, "parse services.spt");
    if (u) {
      /* 复用 didOpen 已打开的文档，不要 doc_store_init 重置 */
      Document *od = doc_store_get(&srv.docs, svc_uri);
      if (!od) {
        od = doc_store_open(&srv.docs, svc_uri, svc_text, strlen(svc_text), 1);
      }
      if (od) {
        size_t off = byte_of(svc_text, "calculate", 2);
        SemImportTarget t;
        int ok = sem_resolve_import_target(u, od, off, &t);
        printf("  sem_resolve_import_target at call site: %d\n", ok);
        if (ok) {
          printf("  module_path: \"%s\"\n", t.module_path);
          printf("  symbol_name: \"%s\"\n", t.symbol_name);
          CHECK(strcmp(t.module_path, "./utils") == 0, "module_path is ./utils");
          CHECK(strcmp(t.symbol_name, "calculate") == 0, "symbol_name is calculate");
        } else {
          CHECK(0, "sem_resolve_import_target should find calculate as import binding");
        }
      }
      spt_lsp_unit_free(u);
    }
  }

  /* 测试 7: rename — 情况 B: 在 services.spt 中 rename calculate（导入符号） */
  printf("\n--- rename: calculate in services.spt (imported symbol) ---\n");
  {
    const char *svc_text = "import { calculate } from \"./utils\";\n"
                           "import { User } from \"./models\";\n"
                           "import * as M from \"./utils\";\n"
                           "\n"
                           "export int computeScore(int base, int bonus) {\n"
                           "  return calculate(base, bonus);\n"
                           "}\n";
    size_t off = byte_of(svc_text, "calculate", 2);
    int line = 0, col = 0;
    for (size_t i = 0; i < off; i++) {
      if (svc_text[i] == '\n') {
        line++;
        col = 0;
      } else
        col++;
    }
    printf("  position: line=%d, character=%d\n", line, col);

    /* 调试：直接调用 sem_resolve */
    {
      Document *d = doc_store_get(&srv.docs, svc_uri);
      if (d) {
        SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
        if (u) {
          SemRef r = sem_resolve(u, d, off);
          printf("  sem_resolve: found=%d, has_def=%d, name=\"%s\", is_member=%d, is_ambient=%d\n",
                 r.found, r.has_def, r.name ? r.name : "(null)", r.is_member, r.is_ambient);
          char mod_path[256];
          int imported = sem_import_binding_path(u, "calculate", mod_path, sizeof mod_path);
          printf("  sem_import_binding_path(\"calculate\"): %d, mod=\"%s\"\n", imported,
                 imported ? mod_path : "(null)");
          spt_lsp_unit_free(u);
        }
      }
    }

    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", line);
    cJSON_AddNumberToObject(pos, "character", col);
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", svc_uri);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON_AddStringToObject(params, "newName", "calc");
    cJSON *resp = lsp_dispatch(&srv, make_req(6, "textDocument/rename", params));
    if (resp) {
      char *s = cJSON_Print(resp);
      printf("  resp: %s\n", s);
      free(s);

      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsObject(result)) {
        cJSON *changes = cJSON_GetObjectItem(result, "changes");
        if (changes && cJSON_IsObject(changes)) {
          int n_files = 0;
          int has_utils = 0, has_svc = 0;
          cJSON *child = changes->child;
          while (child) {
            n_files++;
            if (strstr(child->string, "utils.spt"))
              has_utils = 1;
            if (strstr(child->string, "services.spt"))
              has_svc = 1;
            int n_edits = cJSON_IsArray(child) ? cJSON_GetArraySize(child) : 0;
            printf("  file: %s -> %d edits\n", child->string, n_edits);
            child = child->next;
          }
          printf("  total files: %d, has_utils=%d, has_svc=%d\n", n_files, has_utils, has_svc);
          CHECK(n_files >= 2, "rename touches at least 2 files");
          CHECK(has_utils, "rename touches utils.spt");
          CHECK(has_svc, "rename touches services.spt");
        } else {
          CHECK(0, "rename result has no changes object");
        }
      } else {
        CHECK(0, "rename returned null result");
      }
      cJSON_Delete(resp);
    }
  }

  /* 测试 8: rename — 情况 A: 在 utils.spt 中 rename calculate（本地定义） */
  printf("\n--- rename: calculate in utils.spt (local def) ---\n");
  {
    /* 构建 utils.spt 的 URI */
    char utils_path[4096], utils_uri[4096];
#ifdef _WIN32
    snprintf(utils_path, sizeof utils_path, "%s\\utils.spt", dir);
#else
    snprintf(utils_path, sizeof utils_path, "%s/utils.spt", dir);
#endif
    spt_path_to_uri(utils_path, utils_uri, sizeof utils_uri);

    /* didOpen utils.spt */
    {
      const char *text = "export int calculate(int a, int b) {\n"
                         "  return a + b;\n"
                         "}\n"
                         "export int formatTime(int seconds) {\n"
                         "  return seconds;\n"
                         "}\n"
                         "export str greet(str name) {\n"
                         "  return name;\n"
                         "}\n";
      cJSON *td2 = cJSON_CreateObject();
      cJSON_AddStringToObject(td2, "uri", utils_uri);
      cJSON_AddNumberToObject(td2, "version", 1);
      cJSON_AddStringToObject(td2, "languageId", "spt");
      cJSON_AddStringToObject(td2, "text", text);
      cJSON *p2 = cJSON_CreateObject();
      cJSON_AddItemToObject(p2, "textDocument", td2);
      cJSON *r2 = lsp_dispatch(&srv, make_notif("textDocument/didOpen", p2));
      if (r2)
        cJSON_Delete(r2);
    }

    /* calculate 定义在第 0 行第 11 字符 */
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", 0);
    cJSON_AddNumberToObject(pos, "character", 11);
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", utils_uri);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON_AddStringToObject(params, "newName", "calc");
    cJSON *resp = lsp_dispatch(&srv, make_req(7, "textDocument/rename", params));
    if (resp) {
      char *s = cJSON_Print(resp);
      printf("  resp: %s\n", s);
      free(s);

      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsObject(result)) {
        cJSON *changes = cJSON_GetObjectItem(result, "changes");
        if (changes && cJSON_IsObject(changes)) {
          int n_files = 0;
          int has_utils = 0, has_svc = 0;
          cJSON *child = changes->child;
          while (child) {
            n_files++;
            if (strstr(child->string, "utils.spt"))
              has_utils = 1;
            if (strstr(child->string, "services.spt"))
              has_svc = 1;
            int n_edits = cJSON_IsArray(child) ? cJSON_GetArraySize(child) : 0;
            printf("  file: %s -> %d edits\n", child->string, n_edits);
            child = child->next;
          }
          printf("  total files: %d, has_utils=%d, has_svc=%d\n", n_files, has_utils, has_svc);
          CHECK(n_files >= 2, "rename touches at least 2 files");
          CHECK(has_utils, "rename touches utils.spt");
          CHECK(has_svc, "rename touches services.spt");
        } else {
          CHECK(0, "rename result has no changes object");
        }
      } else {
        CHECK(0, "rename returned null result");
      }
      cJSON_Delete(resp);
    }
  }

  /* 测试 9: references — 在 utils.spt 中查找 calculate 的跨文件引用 */
  printf("\n--- references: calculate in utils.spt (cross-file) ---\n");
  {
    char utils_path[4096], utils_uri[4096];
#ifdef _WIN32
    snprintf(utils_path, sizeof utils_path, "%s\\utils.spt", dir);
#else
    snprintf(utils_path, sizeof utils_path, "%s/utils.spt", dir);
#endif
    spt_path_to_uri(utils_path, utils_uri, sizeof utils_uri);

    /* calculate 定义在第 0 行第 11 字符 */
    cJSON *pos = cJSON_CreateObject();
    cJSON_AddNumberToObject(pos, "line", 0);
    cJSON_AddNumberToObject(pos, "character", 11);
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", utils_uri);
    cJSON *ctx = cJSON_CreateObject();
    cJSON_AddBoolToObject(ctx, "includeDeclaration", 1);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "position", pos);
    cJSON_AddItemToObject(params, "context", ctx);
    cJSON *resp = lsp_dispatch(&srv, make_req(8, "textDocument/references", params));
    if (resp) {
      char *s = cJSON_Print(resp);
      printf("  resp: %s\n", s);
      free(s);

      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsArray(result)) {
        int n = cJSON_GetArraySize(result);
        int has_utils = 0, has_svc = 0;
        for (int i = 0; i < n; i++) {
          cJSON *loc = cJSON_GetArrayItem(result, i);
          cJSON *u = cJSON_GetObjectItem(loc, "uri");
          if (u && strstr(u->valuestring, "utils.spt"))
            has_utils = 1;
          if (u && strstr(u->valuestring, "services.spt"))
            has_svc = 1;
        }
        printf("  total refs: %d, has_utils=%d, has_svc=%d\n", n, has_utils, has_svc);
        CHECK(has_utils, "references includes utils.spt");
        CHECK(has_svc, "references includes services.spt (cross-file)");
      } else {
        CHECK(0, "references returned non-array result");
      }
      cJSON_Delete(resp);
    }
  }

  /* 测试 10: formatting — tab/space 混合缩进规范化 */
  printf("\n--- formatting: mixed tab/space ---\n");
  {
    const char *fmt_text = "int f() {\n"
                           "\treturn 1;\n"
                           "  int x = 2;\n"
                           "\tif (true) {\n"
                           "\t\treturn 2;\n"
                           "  }\n"
                           "  for (int i = 0, 10) {\n"
                           "    print(i);\n"
                           "  }\n"
                           "}\n";
    char fmt_uri[4096];
    char fmt_path[4096];
#ifdef _WIN32
    snprintf(fmt_path, sizeof fmt_path, "%s\\fmt_test.spt", dir);
#else
    snprintf(fmt_path, sizeof fmt_path, "%s/fmt_test.spt", dir);
#endif
    spt_path_to_uri(fmt_path, fmt_uri, sizeof fmt_uri);

    /* didOpen */
    {
      cJSON *td2 = cJSON_CreateObject();
      cJSON_AddStringToObject(td2, "uri", fmt_uri);
      cJSON_AddNumberToObject(td2, "version", 1);
      cJSON_AddStringToObject(td2, "languageId", "spt");
      cJSON_AddStringToObject(td2, "text", fmt_text);
      cJSON *p2 = cJSON_CreateObject();
      cJSON_AddItemToObject(p2, "textDocument", td2);
      cJSON *r2 = lsp_dispatch(&srv, make_notif("textDocument/didOpen", p2));
      if (r2)
        cJSON_Delete(r2);
    }

    /* formatting with insertSpaces=false, tabSize=4 */
    cJSON *td = cJSON_CreateObject();
    cJSON_AddStringToObject(td, "uri", fmt_uri);
    cJSON *opts = cJSON_CreateObject();
    cJSON_AddNumberToObject(opts, "tabSize", 4);
    cJSON_AddBoolToObject(opts, "insertSpaces", 0);
    cJSON *params = cJSON_CreateObject();
    cJSON_AddItemToObject(params, "textDocument", td);
    cJSON_AddItemToObject(params, "options", opts);
    cJSON *resp = lsp_dispatch(&srv, make_req(9, "textDocument/formatting", params));
    if (resp) {
      cJSON *result = cJSON_GetObjectItem(resp, "result");
      if (result && cJSON_IsArray(result) && cJSON_GetArraySize(result) > 0) {
        cJSON *ed = cJSON_GetArrayItem(result, 0);
        cJSON *nt = cJSON_GetObjectItem(ed, "newText");
        if (nt && cJSON_IsString(nt)) {
          printf("  newText (first 200 chars):\n  ");
          for (int i = 0; nt->valuestring[i] && i < 200; i++) {
            if (nt->valuestring[i] == '\t')
              printf("\\t");
            else if (nt->valuestring[i] == '\n')
              printf("\\n\n  ");
            else
              putchar(nt->valuestring[i]);
          }
          printf("\n");

          /* 验证：4 spaces (col=4) 应转为 1 tab */
          /* "    print(i);" 应变为 "\tprint(i);" */
          const char *p = strstr(nt->valuestring, "print(i)");
          if (p) {
            /* 检查 print(i) 前面的字符 */
            int has_tab_before = (p > nt->valuestring && p[-1] == '\t');
            int has_spaces_before = (p >= nt->valuestring + 4 && p[-1] == ' ' && p[-2] == ' ' &&
                                     p[-3] == ' ' && p[-4] == ' ');
            printf("  print(i) preceded by: %s\n",
                   has_tab_before ? "TAB" : (has_spaces_before ? "4 SPACES" : "other"));
            CHECK(has_tab_before, "4 spaces converted to 1 tab");
          }
        }
      } else {
        CHECK(0, "formatting returned empty result");
      }
      cJSON_Delete(resp);
    }
  }

  lsp_server_free(&srv);
  remove_dir_recursive(dir);

  printf("\n=== %s ===\n", failed ? "FAIL" : "PASS");
  return failed ? 1 : 0;
}
