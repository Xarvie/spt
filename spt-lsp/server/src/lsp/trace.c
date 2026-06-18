/*
** trace.c — 环境变量配置的调试日志 / 录制文件。
*/
#include "trace.h"

#include <stdlib.h>

static FILE *open_env(const char *var) {
  const char *p = getenv(var);
  if (!p || !*p)
    return NULL;
  return fopen(p, "a");
}

FILE *spt_open_log(void) { return open_env("SPT_LSP_LOG"); }
FILE *spt_open_record(void) { return open_env("SPT_LSP_RECORD"); }
