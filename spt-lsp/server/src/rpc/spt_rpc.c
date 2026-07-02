/*
** spt_rpc.c — LSP 传输层实现。
*/
#include "spt_rpc.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <io.h>
#endif

/* ===========================================================================
** RpcReader
** ========================================================================= */
void rpc_reader_init(RpcReader *r) {
  r->buf = NULL;
  r->len = 0;
  r->cap = 0;
  r->msg = NULL;
  r->msg_len = 0;
  r->msg_cap = 0;
}

void rpc_reader_free(RpcReader *r) {
  free(r->buf);
  free(r->msg);
  rpc_reader_init(r);
}

void rpc_reader_feed(RpcReader *r, const char *data, size_t n) {
  if (n == 0)
    return;
  if (r->len + n > r->cap) {
    size_t nc = r->cap ? r->cap * 2 : 4096;
    while (nc < r->len + n)
      nc *= 2;
    char *nb = (char *)realloc(r->buf, nc);
    if (!nb)
      return; /* OOM：丢弃本次（生产环境极少触发） */
    r->buf = nb;
    r->cap = nc;
  }
  memcpy(r->buf + r->len, data, n);
  r->len += n;
}

/* 在 [from, r->len) 中查找 "\r\n\r\n"（头/体分隔），返回其起始偏移或 (size_t)-1。 */
static size_t find_header_end(const RpcReader *r, size_t from) {
  if (r->len < 4)
    return (size_t)-1;
  for (size_t i = from; i + 4 <= r->len; i++) {
    if (r->buf[i] == '\r' && r->buf[i + 1] == '\n' && r->buf[i + 2] == '\r' &&
        r->buf[i + 3] == '\n')
      return i;
  }
  return (size_t)-1;
}

/* 解析头块 [0, hdr_end) 内的 Content-Length。返回 1 成功并写 *out_len；否则 0。
** 头块由若干以 \r\n 结束的行组成；最后一行的 \r\n 恰在 hdr_end 处（不含在范围内）。 */
static int parse_content_length(const RpcReader *r, size_t hdr_end, size_t *out_len) {
  const char *p = r->buf;
  const char *key = "content-length:";
  size_t klen = strlen(key);
  int found = 0;
  size_t value = 0;

  size_t i = 0;
  while (i < hdr_end) {
    /* 定位本行结束：下一个范围内 \r\n 的 \r，或 hdr_end（末行）。 */
    size_t eol = i;
    while (eol + 1 < hdr_end && !(p[eol] == '\r' && p[eol + 1] == '\n'))
      eol++;
    size_t line_end = (eol + 1 < hdr_end && p[eol] == '\r' && p[eol + 1] == '\n') ? eol : hdr_end;
    size_t line_len = line_end - i;

    if (line_len >= klen) {
      int match = 1;
      for (size_t k = 0; k < klen; k++) {
        char c = p[i + k];
        if (c >= 'A' && c <= 'Z')
          c = (char)(c - 'A' + 'a');
        if (c != key[k]) {
          match = 0;
          break;
        }
      }
      if (match) {
        size_t j = i + klen;
        while (j < i + line_len && (p[j] == ' ' || p[j] == '\t'))
          j++;
        size_t v = 0;
        int any = 0;
        while (j < i + line_len && p[j] >= '0' && p[j] <= '9') {
          v = v * 10 + (size_t)(p[j] - '0');
          any = 1;
          j++;
        }
        if (any) {
          value = v;
          found = 1;
        }
      }
    }

    if (line_end == hdr_end)
      break;
    i = line_end + 2; /* 跳过 \r\n */
  }

  if (found)
    *out_len = value;
  return found;
}

int rpc_reader_next(RpcReader *r, const char **body, size_t *body_len) {
  size_t hdr_end = find_header_end(r, 0);
  if (hdr_end == (size_t)-1)
    return 0; /* 头未完整 */

  size_t content_len = 0;
  if (!parse_content_length(r, hdr_end, &content_len))
    return -1; /* 协议错误：无 Content-Length */

  size_t body_start = hdr_end + 4;
  if (r->len - body_start < content_len)
    return 0; /* 体未完整 */

  /* 复制体到稳定的 msg 缓冲（NUL 结尾） */
  if (content_len + 1 > r->msg_cap) {
    size_t nc = r->msg_cap ? r->msg_cap * 2 : 256;
    while (nc < content_len + 1)
      nc *= 2;
    char *nm = (char *)realloc(r->msg, nc);
    if (!nm)
      return -1;
    r->msg = nm;
    r->msg_cap = nc;
  }
  memcpy(r->msg, r->buf + body_start, content_len);
  r->msg[content_len] = '\0';
  r->msg_len = content_len;

  /* 从缓冲消费 [0, body_start + content_len) */
  size_t consumed = body_start + content_len;
  size_t remain = r->len - consumed;
  if (remain > 0)
    memmove(r->buf, r->buf + consumed, remain);
  r->len = remain;

  if (body)
    *body = r->msg;
  if (body_len)
    *body_len = r->msg_len;
  return 1;
}

/* ===========================================================================
** 写出
** ========================================================================= */
long rpc_write(FILE *out, const cJSON *v) {
  char *body = cJSON_PrintUnformatted((cJSON *)v);
  if (!body)
    return -1;
  size_t blen = strlen(body);
  char hdr[64];
  int hn = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", blen);
  if (hn < 0) {
    free(body);
    return -1;
  }
#ifdef _WIN32
  /* Windows: fwrite/fprintf 在管道上可能缓冲不刷新，用 _write 直接写 fd */
  int fd = _fileno(out);
  if (_write(fd, hdr, hn) != hn) {
    free(body);
    return -1;
  }
  if (_write(fd, body, (int)blen) != (int)blen) {
    free(body);
    return -1;
  }
#else
  size_t wn = fwrite(hdr, 1, (size_t)hn, out);
  wn += fwrite(body, 1, blen, out);
  fflush(out);
  free(body);
  if (wn != (size_t)hn + blen)
    return -1;
  return (long)((size_t)hn + blen);
#endif
  free(body);
  return (long)((size_t)hn + blen);
}

char *rpc_frame_to_string(const cJSON *v, size_t *out_len) {
  char *body = cJSON_PrintUnformatted((cJSON *)v);
  if (!body)
    return NULL;
  size_t blen = strlen(body);
  char hdr[64];
  int hn = snprintf(hdr, sizeof(hdr), "Content-Length: %zu\r\n\r\n", blen);
  size_t total = (size_t)hn + blen;
  char *out = (char *)malloc(total + 1);
  if (!out) {
    free(body);
    return NULL;
  }
  memcpy(out, hdr, (size_t)hn);
  memcpy(out + hn, body, blen);
  out[total] = '\0';
  free(body);
  if (out_len)
    *out_len = total;
  return out;
}

/* ===========================================================================
** JSON-RPC 2.0
** ========================================================================= */
cJSON *rpc_parse(const char *body, size_t len) { return cJSON_ParseWithLength(body, len); }

int rpc_is_request(const cJSON *msg) {
  return cJSON_GetObjectItemCaseSensitive((cJSON *)msg, "id") != NULL;
}

const char *rpc_method(const cJSON *msg) {
  cJSON *m = cJSON_GetObjectItemCaseSensitive((cJSON *)msg, "method");
  return (m && cJSON_IsString(m)) ? m->valuestring : NULL;
}

cJSON *rpc_params(const cJSON *msg) {
  return cJSON_GetObjectItemCaseSensitive((cJSON *)msg, "params");
}

cJSON *rpc_id(const cJSON *msg) { return cJSON_GetObjectItemCaseSensitive((cJSON *)msg, "id"); }

cJSON *rpc_make_response(const cJSON *id, cJSON *result) {
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
  cJSON_AddItemToObject(resp, "id", id ? cJSON_Duplicate((cJSON *)id, 1) : cJSON_CreateNull());
  cJSON_AddItemToObject(resp, "result", result ? result : cJSON_CreateNull());
  return resp;
}

cJSON *rpc_make_error(const cJSON *id, int code, const char *message) {
  cJSON *resp = cJSON_CreateObject();
  cJSON_AddStringToObject(resp, "jsonrpc", "2.0");
  cJSON_AddItemToObject(resp, "id", id ? cJSON_Duplicate((cJSON *)id, 1) : cJSON_CreateNull());
  cJSON *err = cJSON_CreateObject();
  cJSON_AddNumberToObject(err, "code", code);
  cJSON_AddStringToObject(err, "message", message ? message : "");
  cJSON_AddItemToObject(resp, "error", err);
  return resp;
}

cJSON *rpc_make_notification(const char *method, cJSON *params) {
  cJSON *n = cJSON_CreateObject();
  cJSON_AddStringToObject(n, "jsonrpc", "2.0");
  cJSON_AddStringToObject(n, "method", method);
  cJSON_AddItemToObject(n, "params", params ? params : cJSON_CreateNull());
  return n;
}
