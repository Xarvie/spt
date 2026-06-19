/* format.c — textDocument/formatting
**
** Phase 4: 缩进规范化 + 行尾空白清理 + 末尾单换行。
**   - 读取 options.tabSize / options.insertSpaces（LSP FormattingOptions）。
**   - 将每行前导空白统一为纯 tab 或纯 space（按 insertSpaces）。
**   - 混合 tab/space 按视觉列数换算后重新生成。
**   - 不改动行内非前导空白（字符串/注释内容不受影响）。
*/
#include "lsp_features.h"

#include <stdlib.h>
#include <string.h>

cJSON *feature_format(const Document *d, const cJSON *options) {
  /* 解析 FormattingOptions */
  int tab_size = 4;
  int insert_spaces = 1;
  if (options) {
    cJSON *ts = cJSON_GetObjectItemCaseSensitive((cJSON *)options, "tabSize");
    if (ts && cJSON_IsNumber(ts) && ts->valueint > 0) tab_size = ts->valueint;
    cJSON *is = cJSON_GetObjectItemCaseSensitive((cJSON *)options, "insertSpaces");
    if (is && cJSON_IsBool(is)) insert_spaces = cJSON_IsTrue(is) ? 1 : 0;
  }

  /* 生成格式化后的文本 */
  size_t cap = d->text_len + d->text_len / 2 + 16;
  char *out = (char *)malloc(cap);
  size_t w = 0;

  size_t i = 0;
  while (i < d->text_len) {
    /* ---- 处理一行 ---- */
    size_t line_start = i;

    /* 1. 计算前导白空的视觉列数 */
    int col = 0;
    while (i < d->text_len) {
      char c = d->text[i];
      if (c == ' ') { col++; i++; }
      else if (c == '\t') { col += tab_size - (col % tab_size); i++; }
      else break;
    }
    /* 记录非空白起始 */
    size_t content_start = i;

    /* 2. 生成规范化后的前导白空 */
    if (insert_spaces) {
      for (int k = 0; k < col; k++) out[w++] = ' ';
    } else {
      int tabs = col / tab_size;
      int spaces = col % tab_size;
      for (int k = 0; k < tabs; k++) out[w++] = '\t';
      for (int k = 0; k < spaces; k++) out[w++] = ' ';
    }

    /* 3. 拷贝行内容到行尾（含 \n），去行尾空白 */
    size_t content_write_start = w;
    while (i < d->text_len && d->text[i] != '\n') {
      out[w++] = d->text[i++];
    }
    /* 去行尾空白 */
    while (w > content_write_start && (out[w - 1] == ' ' || out[w - 1] == '\t')) w--;

    /* 4. 拷贝换行符 */
    if (i < d->text_len && d->text[i] == '\n') {
      out[w++] = '\n';
      i++;
    }
    (void)line_start;
    (void)content_start;
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
    memcpy(nt, out, w);
    nt[w] = '\0';
    cJSON_AddStringToObject(ed, "newText", nt);
    free(nt);
    cJSON_AddItemToArray(res, ed);
  }
  free(out);
  return res;
}
