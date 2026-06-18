/*
** main.c — spt-lsp 服务器入口。
** 通过 stdin/stdout 以 LSP 基础协议（Content-Length 分帧）与编辑器通信。
*/
#include "server.h"
#include "trace.h"

#include <stdio.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

int main(void) {
  FILE *dbg = spt_open_log();
  if (dbg) { fprintf(dbg, "main start\n"); fflush(dbg); }
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
  if (dbg) { fprintf(dbg, "setmode done\n"); fflush(dbg); }
#endif

  LspServer s;
  lsp_server_init(&s);
  if (dbg) { fprintf(dbg, "init done\n"); fflush(dbg); }
  int rc = lsp_run(&s, stdin, stdout);
  if (dbg) { fprintf(dbg, "run done rc=%d\n", rc); fflush(dbg); }
  lsp_server_free(&s);
  if (dbg) fclose(dbg);
  return rc;
}
