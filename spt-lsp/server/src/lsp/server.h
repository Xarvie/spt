/*
** server.h — LSP 服务器核心：生命周期状态 + 消息分派。
**
** 设计要点（为 TDD 而分离）：
**   - lsp_dispatch 是纯函数式核心：输入 (server, 已解析的消息) -> 输出响应 cJSON
**     （请求）或 NULL（通知/无响应）。不触碰 I/O，可在内存中直接断言。
**   - lsp_run 是生产环境主循环：从 in 读、分派、向 out 写，直到 exit。
**   - 生命周期遵循 LSP：initialize 前除 initialize 外的请求返回 not-initialized；
**     shutdown 后的请求返回 invalid-request；exit 通知置退出标志。
*/
#ifndef SPT_LSP_SERVER_H
#define SPT_LSP_SERVER_H

#include "cJSON.h"
#include "documents.h"
#include "workspace.h"

#include <stdbool.h>
#include <stdio.h>

typedef enum {
  LSP_UNINITIALIZED = 0, /* 尚未收到 initialize */
  LSP_INITIALIZED,       /* 已 initialize（initialized 通知后亦同此态） */
  LSP_SHUTDOWN           /* 已 shutdown，等待 exit */
} LspState;

/* 服务器主动发出消息（通知，如 publishDiagnostics）的出口。
** 实现接管 msg 的所有权（负责写出并 cJSON_Delete）。 */
typedef void (*LspEmitFn)(void *ctx, cJSON *msg);

typedef struct {
  LspState state;
  bool should_exit; /* 收到 exit 后置位 */
  int exit_code;
  DocStore docs;     /* 打开的文档 */
  Workspace ws;      /* 跨文件符号索引 */
  LspEmitFn emit;    /* 通知出口（可空：无出口时丢弃） */
  void *emit_ctx;
} LspServer;

void lsp_server_init(LspServer *s);
void lsp_server_free(LspServer *s);

/* 设置通知出口（测试用捕获器 / 生产用写 stdout）。 */
void lsp_server_set_emit(LspServer *s, LspEmitFn fn, void *ctx);

/* 主动发出一条消息（接管 msg 所有权）。 */
void lsp_emit(LspServer *s, cJSON *msg);

/* 分派一条已解析的消息。
**   - 请求（含 id）：返回应回复的 cJSON（成功或错误），调用方负责 framing/写出/删除。
**   - 通知（无 id）：返回 NULL（无响应）；副作用在内部完成。 */
cJSON *lsp_dispatch(LspServer *s, const cJSON *msg);

/* 生产主循环：从 in 读 LSP 消息、分派、把响应写入 out，直到 should_exit。
** 返回进程退出码。 */
int lsp_run(LspServer *s, FILE *in, FILE *out);

#endif /* SPT_LSP_SERVER_H */
