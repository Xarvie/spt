/*
** spt_rpc.h — LSP 传输层：Content-Length 分帧 + JSON-RPC 2.0 辅助。
**
** 设计：
**   - 分帧与 I/O 解耦，便于 TDD：RpcReader 在内存缓冲上增量解析消息
**     （生产环境把 stdin 读到的字节 feed 进来，测试里直接 feed 固定字节）。
**   - JSON 用 vendored cJSON；本层只负责「读出一条消息体 / 写出一条带头消息」
**     与「构造 JSON-RPC 响应/错误/通知」。
**
** LSP 基础协议：每条消息形如
**     Content-Length: <N>\r\n
**     \r\n
**     <N 字节 JSON 体>
** （可有其它头如 Content-Type，解析时忽略未知头。）
*/
#ifndef SPT_RPC_H
#define SPT_RPC_H

#include "cJSON.h"

#include <stddef.h>
#include <stdio.h>

/* JSON-RPC 标准错误码（LSP 复用）。 */
enum {
  RPC_PARSE_ERROR = -32700,
  RPC_INVALID_REQUEST = -32600,
  RPC_METHOD_NOT_FOUND = -32601,
  RPC_INVALID_PARAMS = -32602,
  RPC_INTERNAL_ERROR = -32603,
  /* LSP 专用 */
  RPC_SERVER_NOT_INITIALIZED = -32002,
  RPC_REQUEST_FAILED = -32803
};

/* ---------------------------------------------------------------------------
** 增量消息读取器（在内存缓冲上分帧）
** ------------------------------------------------------------------------- */
typedef struct {
  char *buf;  /* 已接收、尚未消费的原始字节 */
  size_t len; /* buf 有效长度 */
  size_t cap; /* buf 容量 */
  char *msg;  /* 最近一条消息体（NUL 结尾，稳定到下一次 next/feed） */
  size_t msg_len;
  size_t msg_cap;
} RpcReader;

void rpc_reader_init(RpcReader *r);
void rpc_reader_free(RpcReader *r);

/* 追加收到的字节。 */
void rpc_reader_feed(RpcReader *r, const char *data, size_t n);

/* 尝试取出一条完整消息体。
**   返回  1：成功，*body 指向 r->msg（NUL 结尾），*body_len 为字节数（已从缓冲消费）。
**   返回  0：数据不足，需继续 feed。
**   返回 -1：协议错误（缺少 Content-Length、长度非法等）。 */
int rpc_reader_next(RpcReader *r, const char **body, size_t *body_len);

/* ---------------------------------------------------------------------------
** 写出
** ------------------------------------------------------------------------- */
/* 把 JSON 值序列化并加 Content-Length 头写入 out（生产用 stdout）。
** 返回写出的总字节数（含头）；失败返回 -1。不释放 v。 */
long rpc_write(FILE *out, const cJSON *v);

/* 测试友好：把带头的整条消息写入 malloc 缓冲（调用方 free）。*out_len 可空。 */
char *rpc_frame_to_string(const cJSON *v, size_t *out_len);

/* ---------------------------------------------------------------------------
** JSON-RPC 2.0 消息构造 / 解析
** ------------------------------------------------------------------------- */
/* 解析消息体为 cJSON（调用方 cJSON_Delete）。失败返回 NULL。 */
cJSON *rpc_parse(const char *body, size_t len);

/* 是否为请求（含 id）。通知无 id。 */
int rpc_is_request(const cJSON *msg);

/* 取 method（缺失返回 NULL）、params（缺失返回 NULL，不复制）、id（不复制，可能为 NULL）。 */
const char *rpc_method(const cJSON *msg);
cJSON *rpc_params(const cJSON *msg);
cJSON *rpc_id(const cJSON *msg);

/* 构造成功响应：{"jsonrpc":"2.0","id":<id 复制>,"result":<result 接管>}。
** id 可为 NULL（则 id=null）。result 可为 NULL（则 result=null）。 */
cJSON *rpc_make_response(const cJSON *id, cJSON *result);

/* 构造错误响应：{"jsonrpc":"2.0","id":<id 复制>,"error":{code,message}}。 */
cJSON *rpc_make_error(const cJSON *id, int code, const char *message);

/* 构造通知：{"jsonrpc":"2.0","method":...,"params":<params 接管>}。 */
cJSON *rpc_make_notification(const char *method, cJSON *params);

#endif /* SPT_RPC_H */
