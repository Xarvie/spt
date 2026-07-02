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

/* ===========================================================================
** Phase 5b/5c: 引用倒排索引 + 模块依赖图（前置声明，实现在文件末尾）
** ========================================================================= */
typedef struct {
  char *uri;
  size_t offset;
  int length;
} RefOcc;

typedef struct {
  char *name;
  RefOcc *occs;
  int occ_count, occ_cap;
} RefBucket;

typedef struct {
  RefBucket *slots;
  int capacity, count;
} RefIndex;

typedef struct {
  char *module_path;
  char **importers;
  int imp_count, imp_cap;
} DepEntry;

typedef struct {
  DepEntry *slots;
  int capacity, count;
} DepGraph;

static void ref_index_init(RefIndex *ri, int cap);
static void ref_index_free(RefIndex *ri);
static void ref_index_remove_uri(RefIndex *ri, const char *uri);
static void dep_graph_init(DepGraph *dg, int cap);
static void dep_graph_free(DepGraph *dg);
static void dep_graph_remove_uri(DepGraph *dg, const char *uri);
static void index_ref_tokens(Workspace *ws, const SptLspUnit *u, const char *uri,
                             const Document *d);
static void index_imports(Workspace *ws, const SptLspUnit *u, const char *uri);
static void index_file(Workspace *ws, const char *path);

/* ---- URI <-> path ---- */
static int hexval(int c) {
  if (c >= '0' && c <= '9')
    return c - '0';
  if (c >= 'a' && c <= 'f')
    return c - 'a' + 10;
  if (c >= 'A' && c <= 'F')
    return c - 'A' + 10;
  return -1;
}

void spt_uri_to_path(const char *uri, char *out, size_t cap) {
  const char *p = uri;
  if (strncmp(p, "file://", 7) == 0)
    p += 7;
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
  if (out[0] == '/' && out[1] && out[2] == ':')
    memmove(out, out + 1, strlen(out));
  for (char *q = out; *q; q++)
    if (*q == '/')
      *q = '\\';
#endif
}

void spt_path_to_uri(const char *path, char *out, size_t cap) {
  size_t w = 0;
  const char *pfx = "file://";
  for (const char *q = pfx; *q && w + 1 < cap; q++)
    out[w++] = *q;
#ifdef _WIN32
  if (w + 1 < cap)
    out[w++] = '/'; /* file:///C:/... */
#endif
  for (size_t i = 0; path[i] && w + 4 < cap; i++) {
    unsigned char c = (unsigned char)path[i];
#ifdef _WIN32
    if (c == '\\')
      c = '/';
#endif
    if (c == ' ') {
      out[w++] = '%';
      out[w++] = '2';
      out[w++] = '0';
    } else if (c == ':') {
      out[w++] = '%';
      out[w++] = '3';
      out[w++] = 'A';
    } else
      out[w++] = (char)c;
  }
  out[w] = '\0';
}

/* ---- 根目录 ---- */
void workspace_init(Workspace *ws) {
  memset(ws, 0, sizeof *ws);
  ws->ref_idx = calloc(1, sizeof(RefIndex));
  ws->dep_graph = calloc(1, sizeof(DepGraph));
  ref_index_init((RefIndex *)ws->ref_idx, 256);
  dep_graph_init((DepGraph *)ws->dep_graph, 64);
}

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

/* Phase 5d: 移除指定 URI 的所有符号条目（原地紧凑）。 */
static void syms_remove_uri(Workspace *ws, const char *uri) {
  if (!uri)
    return;
  int w = 0;
  for (int i = 0; i < ws->sym_count; i++) {
    if (ws->syms[i].uri && strcmp(ws->syms[i].uri, uri) == 0) {
      free(ws->syms[i].name);
      free(ws->syms[i].uri);
      free(ws->syms[i].container);
    } else {
      if (w != i)
        ws->syms[w] = ws->syms[i];
      w++;
    }
  }
  ws->sym_count = w;
}

/* 释放单个独立 Document（非 DocStore 管理）。 */
static void doc_free_one(Document *d) {
  if (!d)
    return;
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

/* Phase 5d: 释放指定 path 的缓存 unit（按路径精确匹配）。返回是否命中。 */
static int free_unit_by_path(Workspace *ws, const char *path) {
  if (!path)
    return 0;
  for (int i = 0; i < ws->unit_count; i++) {
    if (ws->units[i].path && strcmp(ws->units[i].path, path) == 0) {
      free(ws->units[i].path);
      spt_lsp_unit_free(ws->units[i].unit);
      doc_free_one(ws->units[i].temp_doc);
      /* 用末尾元素填补空洞。 */
      ws->units[i] = ws->units[ws->unit_count - 1];
      ws->unit_count--;
      return 1;
    }
  }
  return 0;
}

void workspace_free(Workspace *ws) {
  for (int i = 0; i < ws->root_count; i++)
    free(ws->roots[i]);
  free(ws->roots);
  free_syms(ws);
  free_units(ws);
  if (ws->ref_idx) {
    ref_index_free((RefIndex *)ws->ref_idx);
    free(ws->ref_idx);
  }
  if (ws->dep_graph) {
    dep_graph_free((DepGraph *)ws->dep_graph);
    free(ws->dep_graph);
  }
  memset(ws, 0, sizeof *ws);
}

static char *dupz(const char *s) {
  size_t n = strlen(s);
  char *p = (char *)malloc(n + 1);
  if (p)
    memcpy(p, s, n + 1);
  return p;
}

void workspace_add_root_path(Workspace *ws, const char *path) {
  ws->roots = (char **)realloc(ws->roots, sizeof(char *) * (size_t)(ws->root_count + 1));
  ws->roots[ws->root_count++] = dupz(path);
}

void workspace_add_root_uri(Workspace *ws, const char *root_uri) {
  if (!root_uri)
    return;
  if (strncmp(root_uri, "file://", 7) != 0)
    return;
  char path[4096];
  spt_uri_to_path(root_uri, path, sizeof path);
  workspace_add_root_path(ws, path);
}

void workspace_set_overlay(Workspace *ws, const DocStore *overlay) { ws->overlay = overlay; }

void workspace_mark_dirty(Workspace *ws) {
  if (ws->indexed)
    ws->dirty = 1;
  /* 打开文档变更 -> 目标文件缓存可能过期（overlay 文本变了；磁盘文件一般不变但
     保守起见整体失效，重建成本低且 v1 跨文件解析由用户点击触发，频次低）。 */
  free_units(ws);
}

/* Phase 5d: 按文档粒度失效——只清除该 URI 对应的缓存 unit / 倒排条目 / 依赖图条目 /
 *           符号条目，然后单文件重建。其余文档复用缓存，避免大工作区全量重建。
 *           降级：若索引尚未建立或已脏（无法增量），回退到 workspace_mark_dirty。 */
void workspace_mark_doc_dirty(Workspace *ws, const char *uri) {
  if (!ws || !uri)
    return;
  /* 索引未建立或已脏 → 无法增量，回退整体失效。 */
  if (!ws->indexed || ws->dirty || !ws->ref_idx || !ws->dep_graph) {
    workspace_mark_dirty(ws);
    return;
  }
  /* 1. 释放该 URI 对应的缓存 unit（若磁盘文件解析过）。 */
  char path[4096];
  spt_uri_to_path(uri, path, sizeof path);
  free_unit_by_path(ws, path);
  /* 2. 从三个索引中移除该 URI 的条目。 */
  ref_index_remove_uri((RefIndex *)ws->ref_idx, uri);
  dep_graph_remove_uri((DepGraph *)ws->dep_graph, uri);
  syms_remove_uri(ws, uri);
  /* 3. 单文件重建（若文件存在且在根目录下，index_file 会重新解析并加入索引）。
     若文件已删除（didClose 后磁盘无对应），index_file 静默跳过——条目已被清除。 */
  index_file(ws, path);
}

/* ---- 符号收集 ---- */
static void sym_push(Workspace *ws, const char *name, int kind, const char *uri, LspRange r,
                     const char *container) {
  if (!name)
    return;
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
  if (!rng)
    return r;
  r = lsp_range_from_json(rng);
  return r;
}

/* 摊平 sem_document_symbols 的层级结果。 */
static void flatten(Workspace *ws, const char *uri, cJSON *arr, const char *container) {
  if (!arr)
    return;
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
    if (ch)
      flatten(ws, uri, ch, nm ? nm->valuestring : NULL);
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
    if (!f)
      return;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz < 0) {
      fclose(f);
      return;
    }
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
    /* Phase 5b/5c: 构建引用倒排索引 + 模块依赖图。 */
    index_ref_tokens(ws, u, uri, d);
    index_imports(ws, u, uri);
  }
  spt_lsp_unit_free(u);
  if (tmp_inited)
    doc_store_free(&tmp);
}

static int has_spt_ext(const char *name) {
  size_t n = strlen(name);
  return n >= 4 && strcmp(name + n - 4, ".spt") == 0;
}

static void walk_dir(Workspace *ws, const char *dir, int depth) {
  if (depth > 24)
    return;
#ifdef _WIN32
  char pattern[4096];
  snprintf(pattern, sizeof pattern, "%s\\*", dir);
  WIN32_FIND_DATAA fd;
  HANDLE h = FindFirstFileA(pattern, &fd);
  if (h == INVALID_HANDLE_VALUE)
    return;
  do {
    const char *nm = fd.cFileName;
    if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
      continue;
    char full[4096];
    snprintf(full, sizeof full, "%s\\%s", dir, nm);
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      if (nm[0] != '.')
        walk_dir(ws, full, depth + 1);
    } else if (has_spt_ext(nm)) {
      index_file(ws, full);
    }
  } while (FindNextFileA(h, &fd));
  FindClose(h);
#else
  DIR *dp = opendir(dir);
  if (!dp)
    return;
  struct dirent *de;
  while ((de = readdir(dp)) != NULL) {
    const char *nm = de->d_name;
    if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0)
      continue;
    if (nm[0] == '.')
      continue; /* 跳过隐藏目录/文件（.git 等） */
    char full[4096];
    snprintf(full, sizeof full, "%s/%s", dir, nm);
    struct stat st;
    if (stat(full, &st) != 0)
      continue;
    if (S_ISDIR(st.st_mode)) {
      if (strcmp(nm, "node_modules") != 0)
        walk_dir(ws, full, depth + 1);
    } else if (S_ISREG(st.st_mode) && has_spt_ext(nm)) {
      index_file(ws, full);
    }
  }
  closedir(dp);
#endif
}

void workspace_index(Workspace *ws) {
  free_syms(ws);
  /* Phase 5b/5c: 清空索引后重建。 */
  if (ws->ref_idx) {
    ref_index_free((RefIndex *)ws->ref_idx);
    ref_index_init((RefIndex *)ws->ref_idx, 256);
  }
  if (ws->dep_graph) {
    dep_graph_free((DepGraph *)ws->dep_graph);
    dep_graph_init((DepGraph *)ws->dep_graph, 64);
  }
  for (int i = 0; i < ws->root_count; i++)
    walk_dir(ws, ws->roots[i], 0);
  ws->indexed = 1;
  ws->dirty = 0;
}

/* ---- 查询 ---- */
static int ci_contains(const char *hay, const char *needle) {
  if (!needle || !needle[0])
    return 1;
  size_t hn = strlen(hay), nn = strlen(needle);
  if (nn > hn)
    return 0;
  for (size_t i = 0; i + nn <= hn; i++) {
    size_t k = 0;
    while (k < nn && tolower((unsigned char)hay[i + k]) == tolower((unsigned char)needle[k]))
      k++;
    if (k == nn)
      return 1;
  }
  return 0;
}

cJSON *workspace_symbols(Workspace *ws, const char *query) {
  if (!ws->indexed || ws->dirty)
    workspace_index(ws);
  cJSON *arr = cJSON_CreateArray();
  for (int i = 0; i < ws->sym_count; i++) {
    WsSymbol *s = &ws->syms[i];
    if (!ci_contains(s->name, query))
      continue;
    cJSON *si = cJSON_CreateObject();
    cJSON_AddStringToObject(si, "name", s->name);
    cJSON_AddNumberToObject(si, "kind", s->kind);
    cJSON *loc = cJSON_CreateObject();
    cJSON_AddStringToObject(loc, "uri", s->uri);
    cJSON_AddItemToObject(loc, "range", lsp_range_to_json(s->range));
    cJSON_AddItemToObject(si, "location", loc);
    if (s->container)
      cJSON_AddStringToObject(si, "containerName", s->container);
    cJSON_AddItemToArray(arr, si);
  }
  return arr;
}

/* ---- 跨文件 import 解析 ---- */

int workspace_resolve_module(Workspace *ws, const char *from_uri, const char *module_name,
                             char *out_uri, size_t cap) {
  (void)ws;
  if (!from_uri || !module_name)
    return 0;
  char from_path[4096];
  spt_uri_to_path(from_uri, from_path, sizeof from_path);
  char tgt_path[4096];
  if (!resolve_module_path(from_path, module_name, tgt_path, sizeof tgt_path))
    return 0;
  spt_path_to_uri(tgt_path, out_uri, cap);
  return 1;
}

/* 读磁盘文件全文到 malloc 缓冲（NUL 结尾），返回长度，失败 -1。 */
static long read_file_all(const char *path, char **out_buf) {
  FILE *f = fopen(path, "rb");
  if (!f)
    return -1;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return -1;
  }
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc((size_t)sz + 1);
  if (!buf) {
    fclose(f);
    return -1;
  }
  size_t rd = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  buf[rd] = '\0';
  *out_buf = buf;
  return (long)rd;
}

WsUnit workspace_get_unit(Workspace *ws, const char *path) {
  WsUnit r = {NULL, NULL};
  if (!path)
    return r;

  /* 该路径是否在 overlay 中打开？决定文本来源与 doc 归属。 */
  char uri[4096];
  spt_path_to_uri(path, uri, sizeof uri);
  Document *od = ws->overlay ? doc_store_get((DocStore *)ws->overlay, uri) : NULL;

  /* 查缓存（overlay 与 disk 共用 path 键）。 */
  for (int i = 0; i < ws->unit_count; i++) {
    if (strcmp(ws->units[i].path, path) == 0) {
      if (ws->units[i].parsing)
        return r; /* 防环 */
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
    if (sz < 0)
      return r;
    /* 构造独立 Document（LF 规范化 + 行索引）。 */
    DocStore tmp;
    doc_store_init(&tmp);
    Document *td = doc_store_open(&tmp, uri, buf, (size_t)sz, 0);
    free(buf);
    if (!td)
      return r;
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

/* ===========================================================================
** Phase 5b: 引用倒排索引（{name → [(uri, offset, length)]}）
** Phase 5c: 模块依赖图（{module_path → [importer_uri]}）
** ========================================================================= */

static unsigned ws_hash_str(const char *s) {
  unsigned h = 5381;
  for (; *s; s++)
    h = h * 33 + (unsigned char)*s;
  return h;
}

/* ---- 5b: RefIndex ---- */

static void ref_index_init(RefIndex *ri, int cap) {
  ri->capacity = cap;
  ri->count = 0;
  ri->slots = (RefBucket *)calloc((size_t)cap, sizeof(RefBucket));
}

static void ref_bucket_free(RefBucket *b) {
  free(b->name);
  for (int i = 0; i < b->occ_count; i++)
    free(b->occs[i].uri);
  free(b->occs);
  b->name = NULL;
  b->occs = NULL;
  b->occ_count = b->occ_cap = 0;
}

static void ref_index_free(RefIndex *ri) {
  if (!ri->slots)
    return;
  for (int i = 0; i < ri->capacity; i++)
    if (ri->slots[i].name)
      ref_bucket_free(&ri->slots[i]);
  free(ri->slots);
  ri->slots = NULL;
  ri->capacity = ri->count = 0;
}

static RefBucket *ref_index_get_or_create(RefIndex *ri, const char *name) {
  if (ri->count * 10 >= ri->capacity * 7) {
    /* 扩容重插（跳过墓碑）。 */
    RefBucket *old = ri->slots;
    int oldcap = ri->capacity;
    ref_index_init(ri, ri->capacity * 2);
    for (int i = 0; i < oldcap; i++) {
      if (old[i].name && old[i].name[0] != '\0') {
        unsigned k = ws_hash_str(old[i].name) & (unsigned)(ri->capacity - 1);
        while (ri->slots[k].name)
          k = (k + 1) & (ri->capacity - 1);
        ri->slots[k] = old[i];
        ri->count++;
      } else if (old[i].name) {
        /* 墓碑：释放，不重插。 */
        free(old[i].name);
        free(old[i].occs);
      }
    }
    free(old);
  }
  unsigned k = ws_hash_str(name) & (unsigned)(ri->capacity - 1);
  int first_tomb = -1;
  while (ri->slots[k].name) {
    if (ri->slots[k].name[0] == '\0') {
      /* 墓碑：记录首个墓碑位置，继续探测。 */
      if (first_tomb < 0)
        first_tomb = (int)k;
    } else if (strcmp(ri->slots[k].name, name) == 0) {
      return &ri->slots[k];
    }
    k = (k + 1) & (ri->capacity - 1);
  }
  /* 未命中：优先复用墓碑，否则用 NULL 槽。 */
  int insert_k = (first_tomb >= 0) ? first_tomb : (int)k;
  if (ri->slots[insert_k].name)
    free(ri->slots[insert_k].name); /* 释放墓碑 */
  ri->slots[insert_k].name = dupz(name);
  ri->count++;
  return &ri->slots[insert_k];
}

static void ref_index_add(RefIndex *ri, const char *name, const char *uri, size_t offset,
                          int length) {
  if (!name || !uri)
    return;
  RefBucket *b = ref_index_get_or_create(ri, name);
  if (b->occ_count >= b->occ_cap) {
    b->occ_cap = b->occ_cap ? b->occ_cap * 2 : 8;
    b->occs = (RefOcc *)realloc(b->occs, sizeof(RefOcc) * (size_t)b->occ_cap);
  }
  b->occs[b->occ_count].uri = dupz(uri);
  b->occs[b->occ_count].offset = offset;
  b->occs[b->occ_count].length = length;
  b->occ_count++;
}

static const RefBucket *ref_index_lookup(const RefIndex *ri, const char *name) {
  if (!ri->slots || !name)
    return NULL;
  unsigned k = ws_hash_str(name) & (unsigned)(ri->capacity - 1);
  while (ri->slots[k].name) {
    if (strcmp(ri->slots[k].name, name) == 0)
      return &ri->slots[k];
    k = (k + 1) & (ri->capacity - 1);
  }
  return NULL;
}

/* Phase 5d: 移除指定 URI 的所有引用出现。空桶标记为墓碑（name=""），
   保持开放寻址探测链不断裂。get_or_create / lookup 均跳过墓碑。 */
static void ref_index_remove_uri(RefIndex *ri, const char *uri) {
  if (!ri->slots || !uri)
    return;
  for (int i = 0; i < ri->capacity; i++) {
    RefBucket *b = &ri->slots[i];
    if (!b->name || b->name[0] == '\0')
      continue; /* 空槽或墓碑 */
    /* 过滤掉匹配 URI 的出现，保留其余。 */
    int w = 0;
    for (int j = 0; j < b->occ_count; j++) {
      if (strcmp(b->occs[j].uri, uri) == 0) {
        free(b->occs[j].uri);
      } else {
        if (w != j)
          b->occs[w] = b->occs[j];
        w++;
      }
    }
    b->occ_count = w;
    /* 若桶空了，转为墓碑（name=""），探测链不断裂。 */
    if (w == 0) {
      free(b->name);
      free(b->occs);
      b->name = dupz(""); /* 墓碑标记 */
      b->occs = NULL;
      b->occ_count = b->occ_cap = 0;
      ri->count--;
    }
  }
}

/* ---- 5c: DepGraph ---- */

static void dep_graph_init(DepGraph *dg, int cap) {
  dg->capacity = cap;
  dg->count = 0;
  dg->slots = (DepEntry *)calloc((size_t)cap, sizeof(DepEntry));
}

static void dep_entry_free(DepEntry *e) {
  free(e->module_path);
  for (int i = 0; i < e->imp_count; i++)
    free(e->importers[i]);
  free(e->importers);
  e->module_path = NULL;
  e->importers = NULL;
  e->imp_count = e->imp_cap = 0;
}

static void dep_graph_free(DepGraph *dg) {
  if (!dg->slots)
    return;
  for (int i = 0; i < dg->capacity; i++)
    if (dg->slots[i].module_path)
      dep_entry_free(&dg->slots[i]);
  free(dg->slots);
  dg->slots = NULL;
  dg->capacity = dg->count = 0;
}

static DepEntry *dep_graph_get_or_create(DepGraph *dg, const char *mod) {
  if (dg->count * 10 >= dg->capacity * 7) {
    /* 扩容重插（跳过墓碑）。 */
    DepEntry *old = dg->slots;
    int oldcap = dg->capacity;
    dep_graph_init(dg, dg->capacity * 2);
    for (int i = 0; i < oldcap; i++) {
      if (old[i].module_path && old[i].module_path[0] != '\0') {
        unsigned k = ws_hash_str(old[i].module_path) & (unsigned)(dg->capacity - 1);
        while (dg->slots[k].module_path)
          k = (k + 1) & (dg->capacity - 1);
        dg->slots[k] = old[i];
        dg->count++;
      } else if (old[i].module_path) {
        /* 墓碑：释放，不重插。 */
        dep_entry_free(&old[i]);
      }
    }
    free(old);
  }
  unsigned k = ws_hash_str(mod) & (unsigned)(dg->capacity - 1);
  int first_tomb = -1;
  while (dg->slots[k].module_path) {
    if (dg->slots[k].module_path[0] == '\0') {
      if (first_tomb < 0)
        first_tomb = (int)k;
    } else if (strcmp(dg->slots[k].module_path, mod) == 0) {
      return &dg->slots[k];
    }
    k = (k + 1) & (dg->capacity - 1);
  }
  int insert_k = (first_tomb >= 0) ? first_tomb : (int)k;
  if (dg->slots[insert_k].module_path)
    dep_entry_free(&dg->slots[insert_k]); /* 释放墓碑 */
  dg->slots[insert_k].module_path = dupz(mod);
  dg->count++;
  return &dg->slots[insert_k];
}

static void dep_graph_add(DepGraph *dg, const char *mod, const char *importer_uri) {
  if (!mod || !importer_uri)
    return;
  DepEntry *e = dep_graph_get_or_create(dg, mod);
  /* 去重：同一文件对同一模块的多次 import 只记一次。 */
  for (int i = 0; i < e->imp_count; i++)
    if (strcmp(e->importers[i], importer_uri) == 0)
      return;
  if (e->imp_count >= e->imp_cap) {
    e->imp_cap = e->imp_cap ? e->imp_cap * 2 : 4;
    e->importers = (char **)realloc(e->importers, sizeof(char *) * (size_t)e->imp_cap);
  }
  e->importers[e->imp_count++] = dupz(importer_uri);
}

static const DepEntry *dep_graph_lookup(const DepGraph *dg, const char *mod) {
  if (!dg->slots || !mod)
    return NULL;
  unsigned k = ws_hash_str(mod) & (unsigned)(dg->capacity - 1);
  while (dg->slots[k].module_path) {
    if (dg->slots[k].module_path[0] != '\0' && strcmp(dg->slots[k].module_path, mod) == 0)
      return &dg->slots[k];
    k = (k + 1) & (dg->capacity - 1);
  }
  return NULL;
}

/* Phase 5d: 移除指定 URI 作为导入者的所有条目。空条目转墓碑。 */
static void dep_graph_remove_uri(DepGraph *dg, const char *uri) {
  if (!dg->slots || !uri)
    return;
  for (int i = 0; i < dg->capacity; i++) {
    DepEntry *e = &dg->slots[i];
    if (!e->module_path || e->module_path[0] == '\0')
      continue; /* 空槽或墓碑 */
    /* 过滤掉匹配 URI 的导入者。 */
    int w = 0;
    for (int j = 0; j < e->imp_count; j++) {
      if (strcmp(e->importers[j], uri) == 0) {
        free(e->importers[j]);
      } else {
        if (w != j)
          e->importers[w] = e->importers[j];
        w++;
      }
    }
    e->imp_count = w;
    /* 若导入者全空，转墓碑。 */
    if (w == 0) {
      dep_entry_free(e);
      e->module_path = dupz(""); /* 墓碑 */
      e->importers = NULL;
      e->imp_count = e->imp_cap = 0;
      dg->count--;
    }
  }
}

/* ---- 索引构建：在 index_file 中调用 ---- */

/* 扫描 unit 的标识符 token，加入引用倒排索引。 */
static void index_ref_tokens(Workspace *ws, const SptLspUnit *u, const char *uri,
                             const Document *d) {
  if (!u || !d)
    return;
  RefIndex *ri = (RefIndex *)ws->ref_idx;
  if (!ri)
    return;
  for (int ti = 0; ti < u->token_count; ti++) {
    const SptToken *t = &u->tokens[ti];
    if (t->kind != TOK_IDENTIFIER || t->length <= 0)
      continue;
    /* token 的 line/column 是 1 起；转为字节偏移。 */
    int li = t->line - 1;
    if (li < 0 || li >= d->line_count)
      continue;
    size_t off = d->line_starts[li] + (size_t)(t->column > 0 ? t->column - 1 : 0);
    /* 用 lexeme 做 name（NUL 结尾由 lexer 保证）。 */
    char nm[256];
    int nl = t->length;
    if (nl >= (int)sizeof nm)
      nl = (int)sizeof nm - 1;
    memcpy(nm, t->lexeme, (size_t)nl);
    nm[nl] = '\0';
    ref_index_add(ri, nm, uri, off, t->length);
  }
}

/* 扫描 unit 的顶层 import 语句，加入模块依赖图。 */
static void index_imports(Workspace *ws, const SptLspUnit *u, const char *uri) {
  if (!u || !u->root || u->root->type != NODE_BLOCK)
    return;
  DepGraph *dg = (DepGraph *)ws->dep_graph;
  if (!dg)
    return;
  const AstList *st = &u->root->u.block.statements;
  for (int i = 0; i < st->count; i++) {
    AstNode *s = st->items[i];
    const char *mod = NULL;
    if (s->type == NODE_IMPORT_NAMESPACE)
      mod = s->u.import_ns.module_path;
    else if (s->type == NODE_IMPORT_NAMED)
      mod = s->u.import_named.module_path;
    if (mod)
      dep_graph_add(dg, mod, uri);
  }
}

/* ---- 公开查询函数 ---- */

int workspace_find_occurrences(Workspace *ws, const char *name, RefOccCb cb, void *ctx) {
  if (!ws || !ws->indexed || ws->dirty || !ws->ref_idx)
    return 0;
  const RefIndex *ri = (const RefIndex *)ws->ref_idx;
  const RefBucket *b = ref_index_lookup(ri, name);
  if (!b)
    return 0;
  for (int i = 0; i < b->occ_count; i++)
    cb(ctx, b->occs[i].uri, b->occs[i].offset, b->occs[i].length);
  return b->occ_count;
}

int workspace_find_importers(Workspace *ws, const char *module_path, ImporterCb cb, void *ctx) {
  if (!ws || !ws->indexed || ws->dirty || !ws->dep_graph)
    return 0;
  const DepGraph *dg = (const DepGraph *)ws->dep_graph;
  const DepEntry *e = dep_graph_lookup(dg, module_path);
  if (!e)
    return 0;
  for (int i = 0; i < e->imp_count; i++)
    cb(ctx, e->importers[i]);
  return e->imp_count;
}
