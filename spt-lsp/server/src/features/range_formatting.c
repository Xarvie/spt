/* range_formatting.c — textDocument/rangeFormatting
**
** Phase 6e: 只格式化 range 内的行，生成针对这些行的 TextEdit。
** 复用 feature_format 的缩进规范化逻辑，但限制行范围。
*/
#include "lsp_features.h"

#include <stdlib.h>
#include <string.h>

cJSON *feature_range_formatting(const Document *d, LspRange range, const cJSON *options) {
  int tab_size = 4;
  int insert_spaces = 1;
  if (options) {
    cJSON *ts = cJSON_GetObjectItemCaseSensitive((cJSON *)options, "tabSize");
    if (ts && cJSON_IsNumber(ts) && ts->valueint > 0)
      tab_size = ts->valueint;
    cJSON *is = cJSON_GetObjectItemCaseSensitive((cJSON *)options, "insertSpaces");
    if (is && cJSON_IsBool(is))
      insert_spaces = cJSON_IsTrue(is) ? 1 : 0;
  }

  /* 确定 range 的字节区间。 */
  size_t start_off = doc_offset_at(d, range.start);
  size_t end_off = doc_offset_at(d, range.end);

  /* 对齐到行首/行尾。 */
  while (start_off > 0 && d->text[start_off - 1] != '\n')
    start_off--;
  while (end_off < d->text_len && d->text[end_off] != '\n')
    end_off++;

  if (start_off >= end_off)
    return cJSON_CreateArray();

  /* 格式化 [start_off, end_off) 区间的文本。 */
  size_t seg_len = end_off - start_off;
  size_t cap = seg_len + seg_len / 2 + 16;
  char *out = (char *)malloc(cap);
  size_t w = 0;

  size_t i = start_off;
  while (i < end_off) {
    int col = 0;
    while (i < end_off) {
      char c = d->text[i];
      if (c == ' ') {
        col++;
        i++;
      } else if (c == '\t') {
        col += tab_size - (col % tab_size);
        i++;
      } else
        break;
    }

    if (col > 0) {
      int aligned = ((col + tab_size - 1) / tab_size) * tab_size;
      if (insert_spaces) {
        for (int k = 0; k < aligned; k++)
          out[w++] = ' ';
      } else {
        int tabs = aligned / tab_size;
        for (int k = 0; k < tabs; k++)
          out[w++] = '\t';
      }
    }

    size_t content_write_start = w;
    while (i < end_off && d->text[i] != '\n') {
      out[w++] = d->text[i++];
    }
    while (w > content_write_start && (out[w - 1] == ' ' || out[w - 1] == '\t'))
      w--;

    if (i < end_off && d->text[i] == '\n') {
      out[w++] = '\n';
      i++;
    }
  }

  cJSON *res;
  if (w == seg_len && memcmp(out, d->text + start_off, w) == 0) {
    res = cJSON_CreateArray();
  } else {
    res = cJSON_CreateArray();
    cJSON *ed = cJSON_CreateObject();
    cJSON_AddItemToObject(ed, "range", lsp_range_to_json(doc_range(d, start_off, end_off)));
    char *nt = (char *)malloc(w + 1);
    memcpy(nt, out, w);
    nt[w] = '\0';
    cJSON_AddStringToObject(ed, "newText", nt);
    free(nt);
    cJSON_AddItemToArray(res, ed);
  }
  free(out);
  return res;
}
