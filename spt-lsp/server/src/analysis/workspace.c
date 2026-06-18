/*
** workspace.c — 跨文件符号索引实现。
*/
#include "workspace.h"

#include "documents.h"
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

void workspace_free(Workspace *ws) {
  for (int i = 0; i < ws->root_count; i++) free(ws->roots[i]);
  free(ws->roots);
  free_syms(ws);
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

  char uri[4096];
  spt_path_to_uri(path, uri, sizeof uri);

  DocStore tmp;
  doc_store_init(&tmp);
  Document *d = doc_store_open(&tmp, uri, buf, rd, 1);
  SptLspUnit *u = spt_lsp_parse(d->text, d->text_len);
  cJSON *syms = sem_document_symbols(u, d);
  flatten(ws, uri, syms, NULL);
  cJSON_Delete(syms);
  spt_lsp_unit_free(u);
  doc_store_free(&tmp);
  free(buf);
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
  if (!ws->indexed) workspace_index(ws);
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
