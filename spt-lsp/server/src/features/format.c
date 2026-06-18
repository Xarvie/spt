/* format.c — textDocument/formatting（保守：去行尾空白 + 末尾单换行） */
#include "lsp_features.h"

#include <stdlib.h>
#include <string.h>

cJSON *feature_format(const Document *d, const cJSON *options) {
  (void)options;
  /* 生成清理后的文本 */
  size_t cap = d->text_len + 2;
  char *out = (char *)malloc(cap);
  size_t w = 0, line_start = w;
  for (size_t i = 0; i < d->text_len; i++) {
    char c = d->text[i];
    out[w++] = c;
    if (c == '\n') {
      /* 回退行尾空白 */
      size_t j = w - 1; /* 指向 '\n' */
      size_t k = j;
      while (k > line_start && (out[k - 1] == ' ' || out[k - 1] == '\t')) k--;
      if (k < j) { out[k] = '\n'; w = k + 1; }
      line_start = w;
    }
  }
  /* 末行行尾空白 */
  {
    size_t k = w;
    while (k > line_start && (out[k - 1] == ' ' || out[k - 1] == '\t')) k--;
    w = k;
  }
  /* 末尾单换行 */
  while (w > 0 && out[w - 1] == '\n') w--;
  out[w++] = '\n';

  cJSON *res;
  if (w == d->text_len && memcmp(out, d->text, w) == 0) {
    res = cJSON_CreateArray(); /* 无变化 */
  } else {
    res = cJSON_CreateArray();
    cJSON *ed = cJSON_CreateObject();
    LspRange full = doc_range(d, 0, d->text_len);
    cJSON_AddItemToObject(ed, "range", lsp_range_to_json(full));
    char *nt = (char *)malloc(w + 1);
    memcpy(nt, out, w); nt[w] = '\0';
    cJSON_AddStringToObject(ed, "newText", nt);
    free(nt);
    cJSON_AddItemToArray(res, ed);
  }
  free(out);
  return res;
}
