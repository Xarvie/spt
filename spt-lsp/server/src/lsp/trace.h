/*
** trace.h — 可选的调试日志 / 协议录制（路径由环境变量配置，默认关闭）。
**
**   SPT_LSP_LOG    设为文件路径 -> 写调试日志（追加）；未设则关闭。
**   SPT_LSP_RECORD 设为文件路径 -> 把收到的每条消息体逐行录制为 jsonl（追加）；未设则关闭。
**
** 关闭时各函数返回 NULL，调用点用 if(dbg)/if(rec) 守卫即为无操作，无文件、无开销、可移植。
** （取代此前写死的 c:\Users\... 绝对路径。）
*/
#ifndef SPT_LSP_TRACE_H
#define SPT_LSP_TRACE_H

#include <stdio.h>

FILE *spt_open_log(void);    /* $SPT_LSP_LOG（追加）或 NULL */
FILE *spt_open_record(void); /* $SPT_LSP_RECORD（追加）或 NULL */

#endif /* SPT_LSP_TRACE_H */
