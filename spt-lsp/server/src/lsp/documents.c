/*
** documents.c — 文档存储与位置换算实现。
*/
#include "documents.h"

#include <stdlib.h>
#include <string.h>

/* 可移植 strdup（strdup 在严格 c11 下不声明，且 MSVC 名为 _strdup）。 */
static char *dup_str(const char *s) {
  size_t n = strlen(s);
  char *p = (char *)malloc(n + 1);
  if (p)
    memcpy(p, s, n + 1);
  return p;
}

/* ---- UTF-8 解码：返回码点，*adv 为消耗字节数（≥1）。非法序列按 1 字节推进。 ---- */
static unsigned utf8_next(const char *s, size_t n, size_t i, int *adv) {
  unsigned char c = (unsigned char)s[i];
  if (c < 0x80) {
    *adv = 1;
    return c;
  }
  int len;
  unsigned cp;
  if ((c & 0xE0) == 0xC0) {
    len = 2;
    cp = c & 0x1F;
  } else if ((c & 0xF0) == 0xE0) {
    len = 3;
    cp = c & 0x0F;
  } else if ((c & 0xF8) == 0xF0) {
    len = 4;
    cp = c & 0x07;
  } else {
    *adv = 1;
    return 0xFFFD;
  }
  if (i + (size_t)len > n) {
    *adv = 1;
    return 0xFFFD;
  }
  for (int k = 1; k < len; k++) {
    unsigned char cc = (unsigned char)s[i + (size_t)k];
    if ((cc & 0xC0) != 0x80) {
      *adv = 1;
      return 0xFFFD;
    }
    cp = (cp << 6) | (cc & 0x3F);
  }
  *adv = len;
  return cp;
}

/* 码点的 UTF-16 码元数。 */
static int utf16_units(unsigned cp) { return cp > 0xFFFF ? 2 : 1; }

/* ---- 换行规范化（CRLF/CR -> LF），原地缩短，返回新长度 ---- */
static size_t normalize_newlines(char *s, size_t len) {
  size_t w = 0;
  for (size_t r = 0; r < len; r++) {
    char c = s[r];
    if (c == '\r') {
      s[w++] = '\n';
      if (r + 1 < len && s[r + 1] == '\n')
        r++;
    } else {
      s[w++] = c;
    }
  }
  s[w] = '\0';
  return w;
}

/* ---- 重建行索引 ---- */
static void build_line_index(Document *d) {
  free(d->line_starts);
  int cap = 16, n = 0;
  size_t *starts = (size_t *)malloc(sizeof(size_t) * (size_t)cap);
  starts[n++] = 0; /* 第 0 行从 0 开始 */
  for (size_t i = 0; i < d->text_len; i++) {
    if (d->text[i] == '\n') {
      if (n >= cap) {
        cap *= 2;
        starts = (size_t *)realloc(starts, sizeof(size_t) * (size_t)cap);
      }
      starts[n++] = i + 1;
    }
  }
  d->line_starts = starts;
  d->line_count = n;
}

static void doc_set_text(Document *d, const char *text, size_t text_len) {
  free(d->text);
  char *t = (char *)malloc(text_len + 1);
  memcpy(t, text ? text : "", text_len);
  t[text_len] = '\0';
  d->text_len = normalize_newlines(t, text_len);
  d->text = t;
  build_line_index(d);
}

/* ---- 存储 ---- */
void doc_store_init(DocStore *s) {
  s->docs = NULL;
  s->count = 0;
  s->cap = 0;
}

void doc_store_free(DocStore *s) {
  for (int i = 0; i < s->count; i++) {
    free(s->docs[i]->uri);
    free(s->docs[i]->text);
    free(s->docs[i]->line_starts);
    free(s->docs[i]);
  }
  free(s->docs);
  doc_store_init(s);
}

Document *doc_store_get(DocStore *s, const char *uri) {
  for (int i = 0; i < s->count; i++)
    if (strcmp(s->docs[i]->uri, uri) == 0)
      return s->docs[i];
  return NULL;
}

Document *doc_store_open(DocStore *s, const char *uri, const char *text, size_t text_len,
                         int version) {
  Document *d = doc_store_get(s, uri);
  if (!d) {
    d = (Document *)calloc(1, sizeof(Document));
    d->uri = dup_str(uri);
    if (s->count >= s->cap) {
      s->cap = s->cap ? s->cap * 2 : 8;
      s->docs = (Document **)realloc(s->docs, sizeof(Document *) * (size_t)s->cap);
    }
    s->docs[s->count++] = d;
  }
  d->version = version;
  doc_set_text(d, text, text_len);
  return d;
}

Document *doc_store_change(DocStore *s, const char *uri, const char *text, size_t text_len,
                           int version) {
  Document *d = doc_store_get(s, uri);
  if (!d)
    return doc_store_open(s, uri, text, text_len, version);
  d->version = version;
  doc_set_text(d, text, text_len);
  return d;
}

Document *doc_store_change_range(DocStore *s, const char *uri, size_t start_off, size_t end_off,
                                 const char *replacement, size_t repl_len, int version) {
  Document *d = doc_store_get(s, uri);
  if (!d)
    return doc_store_open(s, uri, replacement, repl_len, version);
  /* 钳制范围。 */
  if (start_off > d->text_len) start_off = d->text_len;
  if (end_off > d->text_len) end_off = d->text_len;
  if (start_off > end_off) { size_t t = start_off; start_off = end_off; end_off = t; }

  /* 规范化 replacement 的换行（CRLF/CR -> LF）。 */
  char *norm_repl = (char *)malloc(repl_len + 1);
  memcpy(norm_repl, replacement ? replacement : "", repl_len);
  norm_repl[repl_len] = '\0';
  size_t norm_len = normalize_newlines(norm_repl, repl_len);

  /* 拼接新文本：前缀 + replacement + 后缀。 */
  size_t new_len = start_off + norm_len + (d->text_len - end_off);
  char *new_text = (char *)malloc(new_len + 1);
  memcpy(new_text, d->text, start_off);
  memcpy(new_text + start_off, norm_repl, norm_len);
  memcpy(new_text + start_off + norm_len, d->text + end_off, d->text_len - end_off);
  new_text[new_len] = '\0';
  free(norm_repl);

  d->version = version;
  free(d->text);
  d->text = new_text;
  d->text_len = new_len;
  build_line_index(d);
  return d;
}

void doc_store_close(DocStore *s, const char *uri) {
  for (int i = 0; i < s->count; i++) {
    if (strcmp(s->docs[i]->uri, uri) == 0) {
      free(s->docs[i]->uri);
      free(s->docs[i]->text);
      free(s->docs[i]->line_starts);
      free(s->docs[i]);
      s->docs[i] = s->docs[--s->count];
      return;
    }
  }
}

/* ---- 换算 ---- */
size_t doc_offset_at(const Document *d, LspPos p) {
  int line = p.line;
  if (line < 0)
    line = 0;
  if (line >= d->line_count)
    return d->text_len; /* 超出末行 -> 文末 */
  size_t ls = d->line_starts[line];
  size_t le = (line + 1 < d->line_count) ? d->line_starts[line + 1] : d->text_len;
  /* 行内：把 character（UTF-16 码元）转为字节偏移 */
  int want = p.character;
  if (want <= 0)
    return ls;
  int units = 0;
  size_t i = ls;
  while (i < le) {
    if (d->text[i] == '\n')
      break; /* 不越过行尾 */
    int adv;
    unsigned cp = utf8_next(d->text, d->text_len, i, &adv);
    int u = utf16_units(cp);
    if (units + u > want)
      break;
    units += u;
    i += (size_t)adv;
    if (units >= want)
      break;
  }
  return i;
}

LspPos doc_pos_at(const Document *d, size_t off) {
  LspPos p = {0, 0};
  if (off > d->text_len)
    off = d->text_len;
  /* 二分定位行：最大的 line 使 line_starts[line] <= off */
  int lo = 0, hi = d->line_count - 1, line = 0;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    if (d->line_starts[mid] <= off) {
      line = mid;
      lo = mid + 1;
    } else {
      hi = mid - 1;
    }
  }
  p.line = line;
  /* 行内 UTF-16 码元计数到 off */
  size_t i = d->line_starts[line];
  int units = 0;
  while (i < off) {
    int adv;
    unsigned cp = utf8_next(d->text, d->text_len, i, &adv);
    units += utf16_units(cp);
    i += (size_t)adv;
  }
  p.character = units;
  return p;
}

LspRange doc_range(const Document *d, size_t start, size_t end) {
  LspRange r;
  r.start = doc_pos_at(d, start);
  r.end = doc_pos_at(d, end);
  return r;
}

LspPos doc_pos_from_frontend(const Document *d, int line1, int col1_bytes) {
  int line0 = line1 - 1;
  if (line0 < 0)
    line0 = 0;
  if (line0 >= d->line_count) {
    LspPos p = {d->line_count > 0 ? d->line_count - 1 : 0, 0};
    return p;
  }
  size_t byte_off = d->line_starts[line0] + (size_t)(col1_bytes > 0 ? col1_bytes - 1 : 0);
  if (byte_off > d->text_len)
    byte_off = d->text_len;
  return doc_pos_at(d, byte_off);
}
