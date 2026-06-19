/*
** workspace.c — 跨文件符号索引实现。
*/
#include "workspace.h"

#include "documents.h"
#include "module_resolve.h"
#include "semantic.h"
#include "spt_lsp_bridge.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#endif

/* ---- URI <-> path ---- */
static int hexval(int c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

void spt_uri_to_path(const char *uri, char *out, size_t cap) {
  const char *p = uri;
  if (strncmp(p, "file://", 7) == 0) p += 7;
  /* file:///path -> /path ；Windows: file:///C:/x -> /C:/x，去掉前导 '/' */
  size_t w = 0;
  for (size_t i = 0; p[i] && w + 1 < cap; i++) {
    if (p[i] == '%' && hexval(p[i + 1]) >= 0 && hexval(p[i + 2]) >= 0) {
      out[w++] = (char)(hexval(p[i + 1]) * 16 + hexval(p[i + 2]));
      i += 2;
    } else {
      out[w++] = p[i];
    }
  }
  out[w] = '\0';
#ifdef _WIN32
  if (out[0] == '/' && out[1] && out[2] == ':') memmove(out, out + 1, strlen(out));
  for (char *q = out; *q; q++) if (*q == '/') *q = '\\';
#endif
}

void spt_path_to_uri(const char *path, char *out, size_t cap) {
  size_t w = 0;
  const char *pfx = "file://";
  for (const char *q = pfx; *q && w + 1 < cap; q++) out[w++] = *q;
#ifdef _WIN32
  if (w + 1 < cap) out[w++] = '/'; /* file:///C:/... */
#endif
  for (size_t i = 0; path[i] && w + 4 < cap; i++) {
    unsigned char c = (unsigned char)path[i];
#ifdef _WIN32
    if (c == '\\') c = '/';
#endif
    if (c == ' ') { out[w++] = '%'; out[w++] = '2'; out[w++] = '0'; }
    else out[w++] = (char)c;
  }
  out[w] = '\0';
}

/* ---- 根目录 ---- */
void workspace_init(Workspace *ws) { memset(ws, 0, sizeof *ws); }

static void free_syms(Workspace *ws) {
  for (int i = 0; i < ws->sym_count; i++) {
    free(ws->syms[i].name);
    free(ws->syms[i].uri);
    free(ws->syms[i].container);
  }
  free(ws->syms);
  ws->syms = NULL;
  ws->sym_count = ws->sym_cap = 0;
}

/* 释放单个独立 Document（非 DocStore 管理）。 */
static void doc_free_one(Document *d) {
  if (!d) return;
  free(d->uri);
  free(d->text);
  free(d->line_starts);
  free(d);
}

/* 释放目标文件解析缓存。 */
static void free_units(Workspace *ws) {
  for (int i = 0; i < ws->unit_count; i++) {
    free(ws->units[i].path);
    spt_lsp_unit_free(ws->units[i].unit);
    doc_free_one(ws->units[i].temp_doc);
  }
  free(ws->units);
  ws->units = NULL;
  ws->unit_count = ws->unit_cap = 0;
}

void workspace_free(Workspace *ws) {
  for (int i = 0; i < ws->root_count; i++) free(ws->roots[i]);
  free(ws->roots);
  free_syms(ws);
  free_units(ws);
  memset(ws, 0, sizeof *ws);
}

static char *dupz(const char *s) {
  size_t n = strlen(s);
  char *p = (char *)malloc(n + 1);
  if (p) memcpy(p, s, n + 1);
  return p;
}

void workspace_add_root_path(Workspace *ws, const char *path) {
  ws->roots = (char **)realloc(ws->roots, sizeof(char *) * (size_t)(ws->root_count + 1));
  ws->roots[ws->root_count++] = dupz(path);
}

void workspace_add_root_uri(Workspace *ws, const char *root_uri) {
  if (!root_uri) return;
  if (strncmp(root_uri, "file://", 7) != 0) return;
  char path[4096];
  spt_uri_to_path(root_uri, path, sizeof path);
  workspace_add_root_path(ws, path);
}

void workspace_set_overlay(Workspace *ws, const DocStore *overlay) { ws->overlay = overlay; }

void workspace_mark_dirty(Workspace *ws) {
  if (ws->indexed) ws->dirty = 1;
  /* 打开文档变更 -> 目标文件缓存可能过期（overlay 文本变了；磁盘文件一般不变但
     保守起见整体失效，重建成本低且 v1 跨文件解析由用户点击触发，频次低）。 */
  free_units(ws);
}

/* ---- 符号收集 ---- */
static void sym_push(Workspace *ws, const char *name, int kind, const char *uri, LspRange r,
                     const char *container) {
  if (!name) return;
  if (ws->sym_count >= ws->sym_cap) {
    ws->sym_cap = ws->sym_cap ? ws->sym_cap * 2 : 64;
    ws->syms = (WsSymbol *)realloc(ws->syms, sizeof(WsSymbol) * (size_t)ws->sym_cap);
  }
  WsSymbol *s = &ws->syms[ws->sym_count++];
  s->name = dupz(name);
  s->kind = kind;
  s->uri = dupz(uri);
  s->range = r;
  s->container = container ? dupz(container) : NULL;
}

static LspRange range_from_json(cJSON *rng) {
  LspRange r = {{0, 0}, {0, 0}};
  if (!rng) return r;
  r = lsp_range_from_json(rng);
  return r;
}

/* 摊平 sem_document_symbols 的层级结果。 */
static void flatten(Workspace *ws, const char *uri, cJSON *arr, const char *container) {
  if (!arr) return;
  int n = cJSON_GetArraySize(arr);
  for (int i = 0; i < n; i++) {
    cJSON *it = cJSON_GetArrayItem(arr, i);
    cJSON *nm = cJSON_GetObjectItemCaseSensitive(it, "name");
    cJSON *kd = cJSON_GetObjectItemCaseSensitive(it, "kind");
    cJSON *sel = cJSON_GetObjectItemCaseSensitive(it, "selectionRange");
    if (nm && nm->valuestring) {
      LspRange r = range_from_json(sel);
      sym_push(ws, nm->valuestring, kd ? kd->valueint : 0, uri, r, container);
    }
    cJSON *ch = cJSON_GetObjectItemCaseSensitive(it, "children");
    if (ch) flatten(ws, uri, ch, nm ? nm->valuestring : NULL);
  }
}

static void index_file(Workspace *ws, const char *path) {
  char uri[4096];
  spt_path_to_uri(path, uri, sizeof uri);

  /* 覆盖层：若该文件正打开（未保存改动），用打开文档的文本而非磁盘内容。 */
  const Document *od = ws->overlay ? doc_store_get((DocStore *)ws->overlay, uri) : NULL;
  SptLspUnit *u = NULL;
  DocStore tmp;
  int tmp_inited = 0;
  const Document *d = NULL;
  if (od) {
    u = spt_lsp_parse(od->text, od->text_len);
    d = od;
  } else {
    FILE *f = fopen(path, "rb");
    if (!f) return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return; }
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc((size_t)sz + 1);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    doc_store_init(&tmp);
    tmp_inited = 1;
    Document *td = doc_store_open(&tmp, uri, buf, rd, 1);
    if (td) {
      u = spt_lsp_parse(td->text, td->text_len);
      d = td;
    }
    free(buf);
  }

  if (u && d) {
    cJSON *syms = sem_document_symbols(u, d);
    flatten(ws, uri, syms, NULL);
    cJSON_Delete(syms);
  }
  spt_lsp_unit_free(u);
  if (tmp_inited) doc_store_free(&tmp);
}

static int has_spt_ext(const char *name) {
  size_t n = strlen(name);
  return n >= 4 && strcmp(name + n - 4, ".spt") == 0;
}

static void walk_dir(Workspace *ws, const char *dir, int depth) {
  if (depth > 24) return;
#ifdef _WIN32
  char pattern[4096];
  snprintf(pattern, sizeof pattern, "%s\\*", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE) return;
  do {
    const char *nm = fd.cFileName;
    if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
    char full[4096];
    snprintf(full, sizeof full, "%s\\%s", dir, nm);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (nm[0] != '.') walk_dir(ws, full, depth + 1);
    } else if (has_spt_ext(nm)) {
      index_file(ws, full);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *dp = opendir(dir);
  if (!dp) return;
  struct dirent *de;
  while ((de = readdir(dp)) != NULL) {
    const char *nm = de->d_name;
    if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
    if (nm[0] == '.') continue; /* 跳过隐藏目录/文件（.git 等） */
    char full[4096];
    snprintf(full, sizeof full, "%s/%s", dir, nm);
    struct stat st;
    if (stat(full, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) {
      if (strcmp(nm, "node_modules") != 0) walk_dir(ws, full, depth + 1);
    } else if (S_ISREG(st.st_mode) && has_spt_ext(nm)) {
      index_file(ws, full);
    }
  }
  closedir(dp);
#endif
}

void workspace_index(Workspace *ws) {
  free_syms(ws);
  for (int i = 0; i < ws->root_count; i++) walk_dir(ws, ws->roots[i], 0);
  ws->indexed = 1;
  ws->dirty = 0;
}

/* ---- 查询 ---- */
static int ci_contains(const char *hay, const char *needle) {
  if (!needle || !needle[0]) return 1;
  size_t hn = strlen(hay), nn = strlen(needle);
  if (nn > hn) return 0;
  for (size_t i = 0; i + nn <= hn; i++) {
    size_t k = 0;
    while (k < nn && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k])) k++;
    if (k == nn) return 1;
  }
  return 0;
}

cJSON *workspace_symbols(Workspace *ws, const char *query) {
  if (!ws->indexed || ws->dirty) workspace_index(ws);
  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < ws->sym_count; i++) {
    WsSymbol *s = &ws->syms[i];
    if (!ci_contains(s->name, query)) continue;
    cJSON *si = cJSON_CreateObject();
    cJSON_AddStringToObject(si, "name", s->name);
    cJSON_AddNumberToObject(si, "kind", s->kind);
    cJSON *loc = cJSON_CreateObject();
    cJSON_AddStringToObject(loc, "uri", s->uri);
    cJSON_AddItemToObject(loc, "range", lsp_range_to_json(s->range));
    cJSON_AddItemToObject(si, "location", loc);
    if (s->container) cJSON_AddStringToObject(si, "containerName", s->container);
    cJSON_AddItemToArray(arr, si);
  }
  return arr;
}

/* ---- 跨文件 import 解析 ---- */

int workspace_resolve_module(Workspace *ws, const char *from_uri, const char *module_name,
                             char *out_uri, size_t cap) {
  (void)ws;
  if (!from_uri || !module_name) return 0;
  char from_path[4096];
  spt_uri_to_path(from_uri, from_path, sizeof from_path);
  char tgt_path[4096];
  if (!resolve_module_path(from_path, module_name, tgt_path, sizeof tgt_path)) return 0;
  spt_path_to_uri(tgt_path, out_uri, cap);
  return 1;
}

/* 读磁盘文件全文到 malloc 缓冲（NUL 结尾），返回长度，失败 -1。 */
static long read_file_all(const char *path, char **out_buf) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz < 0) { fclose(f); return -1; }
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return -1; }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = '\0';
  *out_buf = buf;
  return (long)rd;
}

WsUnit workspace_get_unit(Workspace *ws, const char *path) {
  WsUnit r = { NULL, NULL };
  if (!path) return r;

  /* 该路径是否在 overlay 中打开？决定文本来源与 doc 归属。 */
  char uri[4096];
  spt_path_to_uri(path, uri, sizeof uri);
  Document *od = ws->overlay ? doc_store_get((DocStore *)ws->overlay, uri) : NULL;

  /* 查缓存（overlay 与 disk 共用 path 键）。 */
  for (int i = 0; i < ws->unit_count; i++) {
    if (strcmp(ws->units[i].path, path) == 0) {
      if (ws->units[i].parsing) return r; /* 防环 */
      r.unit = ws->units[i].unit;
      r.doc = od ? od : ws->units[i].temp_doc;
      return r;
    }
  }

  /* 未缓存：取文本（overlay 优先，否则磁盘）。 */
  const char *text = NULL;
  size_t text_len = 0;
  char *buf = NULL;
  Document *owned = NULL;
  if (od) {
    text = od->text;
    text_len = od->text_len;
  } else {
    long sz = read_file_all(path, &buf);
    if (sz < 0) return r;
    /* 构造独立 Document（LF 规范化 + 行索引）。 */
    DocStore tmp;
    doc_store_init(&tmp);
    Document *td = doc_store_open(&tmp, uri, buf, (size_t)sz, 0);
    free(buf);
    if (!td) return r;
    owned = td;
    free(tmp.docs); /* 释放 Document* 数组壳，保留 td 本体 */
    doc_store_init(&tmp);
    text = owned->text;
    text_len = owned->text_len;
  }

  /* 扩容缓存数组。 */
  if (ws->unit_count >= ws->unit_cap) {
    ws->unit_cap = ws->unit_cap ? ws->unit_cap * 2 : 8;
    ws->units = (void *)realloc(ws->units, sizeof(ws->units[0]) * (size_t)ws->unit_cap);
  }
  int idx = ws->unit_count++;
  ws->units[idx].path = dupz(path);
  ws->units[idx].parsing = 1;
  ws->units[idx].temp_doc = owned; /* disk 文件才拥有；overlay 时为 NULL */
  ws->units[idx].unit = spt_lsp_parse(text, text_len);
  ws->units[idx].parsing = 0;

  r.unit = ws->units[idx].unit;
  r.doc = od ? od : owned;
  return r;
}
