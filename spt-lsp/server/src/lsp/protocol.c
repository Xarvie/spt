/*
** protocol.c — LSP 公共类型的 JSON 互转。
*/
#include "protocol.h"

cJSON *lsp_pos_to_json(LspPos p) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddNumberToObject(o, "line", p.line);
  cJSON_AddNumberToObject(o, "character", p.character);
  return o;
}

cJSON *lsp_range_to_json(LspRange r) {
  cJSON *o = cJSON_CreateObject();
  cJSON_AddItemToObject(o, "start", lsp_pos_to_json(r.start));
  cJSON_AddItemToObject(o, "end", lsp_pos_to_json(r.end));
  return o;
}

LspPos lsp_pos_from_json(const cJSON *o) {
  LspPos p = {0, 0};
  if (!o)
    return p;
  cJSON *l = cJSON_GetObjectItemCaseSensitive((cJSON *)o, "line");
  cJSON *c = cJSON_GetObjectItemCaseSensitive((cJSON *)o, "character");
  if (l && cJSON_IsNumber(l))
    p.line = l->valueint;
  if (c && cJSON_IsNumber(c))
    p.character = c->valueint;
  return p;
}

LspRange lsp_range_from_json(const cJSON *o) {
  LspRange r = {{0, 0}, {0, 0}};
  if (!o)
    return r;
  r.start = lsp_pos_from_json(cJSON_GetObjectItemCaseSensitive((cJSON *)o, "start"));
  r.end = lsp_pos_from_json(cJSON_GetObjectItemCaseSensitive((cJSON *)o, "end"));
  return r;
}
