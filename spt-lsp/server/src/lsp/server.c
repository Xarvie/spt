/*
** server.c — LSP 服务器核心实现。
*/
#if !defined(_WIN32) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1 /* 暴露 POSIX fileno/read（glibc 在 -std=c11 严格模式下默认隐藏） */
#endif
#include "server.h"

#include "diagnostics.h"
#include "lsp_features.h"
#include "spt_rpc.h"
#include "trace.h"

#include <string.h>
#include <errno.h>
#ifdef _WIN32
#include <io.h>
#define spt_fileno _fileno
#define spt_read _read
#else
#include <unistd.h>
#define spt_fileno fileno
#define spt_read read
#endif

#define SPT_LSP_NAME "spt-lsp"
#define SPT_LSP_VERSION "0.1.0"

void lsp_server_init(LspServer *s) {
  s->state = LSP_UNINITIALIZED;
  s->should_exit = false;
  s->exit_code = 0;
  doc_store_init(&s->docs);
  workspace_init(&s->ws);
  s->emit = NULL;
  s->emit_ctx = NULL;
}

void lsp_server_free(LspServer *s) {
  doc_store_free(&s->docs);
  workspace_free(&s->ws);
}

void lsp_server_set_emit(LspServer *s, LspEmitFn fn, void *ctx) {
  s->emit = fn;
  s->emit_ctx = ctx;
}

void lsp_emit(LspServer *s, cJSON *msg) {
  if (s->emit)
    s->emit(s->emit_ctx, msg); /* 出口接管所有权 */
  else
    cJSON_Delete(msg); /* 无出口：丢弃，避免泄漏 */
}

/* 针对某文档计算并推送诊断。 */
static void publish_diagnostics(LspServer *s, const char *uri) {
  Document *d = doc_store_get(&s->docs, uri);
  if (!d)
    return;
  cJSON *params = diagnostics_compute(d);
  lsp_emit(s, rpc_make_notification("textDocument/publishDiagnostics", params));
}

/* 构造 initialize 的结果：serverInfo + capabilities。
** 目前仅声明已实际接线的能力（诚实原则）：
**   - textDocumentSync = 1（Full：变更时整篇重发；后续切增量再改 2）。
** 随着 feature 落地（hover/definition/completion/...）逐步在此追加 capability。 */
static cJSON *make_initialize_result(void) {
  cJSON *result = cJSON_CreateObject();

  cJSON *caps = cJSON_CreateObject();
  cJSON_AddNumberToObject(caps, "textDocumentSync", 1); /* TextDocumentSyncKind.Full */
  cJSON_AddBoolToObject(caps, "documentSymbolProvider", 1);
  cJSON_AddBoolToObject(caps, "hoverProvider", 1);
  cJSON_AddBoolToObject(caps, "definitionProvider", 1);
  cJSON_AddBoolToObject(caps, "referencesProvider", 1);
  cJSON_AddBoolToObject(caps, "workspaceSymbolProvider", 1);
  cJSON_AddBoolToObject(caps, "renameProvider", 1);
  cJSON_AddBoolToObject(caps, "documentFormattingProvider", 1);
  /* completion */
  cJSON *comp = cJSON_CreateObject();
  cJSON *trig = cJSON_CreateArray();
  cJSON_AddItemToArray(trig, cJSON_CreateString("."));
  cJSON_AddItemToObject(comp, "triggerCharacters", trig);
  cJSON_AddItemToObject(caps, "completionProvider", comp);
  /* signature help */
  cJSON *sig = cJSON_CreateObject();
  cJSON *strig = cJSON_CreateArray();
  cJSON_AddItemToArray(strig, cJSON_CreateString("("));
  cJSON_AddItemToArray(strig, cJSON_CreateString(","));
  cJSON_AddItemToObject(sig, "triggerCharacters", strig);
  cJSON_AddItemToObject(caps, "signatureHelpProvider", sig);
  /* semantic tokens (full) */
  cJSON *sem = cJSON_CreateObject();
  cJSON *legend = cJSON_CreateObject();
  cJSON *types = cJSON_CreateArray();
  for (int i = 0; i < SPT_TOKEN_TYPES_COUNT; i++)
    cJSON_AddItemToArray(types, cJSON_CreateString(SPT_TOKEN_TYPES[i]));
  cJSON_AddItemToObject(legend, "tokenTypes", types);
  cJSON_AddItemToObject(legend, "tokenModifiers", cJSON_CreateArray());
  cJSON_AddItemToObject(sem, "legend", legend);
  cJSON_AddBoolToObject(sem, "full", 1);
  cJSON_AddItemToObject(caps, "semanticTokensProvider", sem);

  cJSON_AddItemToObject(result, "capabilities", caps);

  cJSON *info = cJSON_CreateObject();
  cJSON_AddStringToObject(info, "name", SPT_LSP_NAME);
  cJSON_AddStringToObject(info, "version", SPT_LSP_VERSION);
  cJSON_AddItemToObject(result, "serverInfo", info);

  return result;
}

/* ---- 请求参数辅助 ---- */
static Document *get_doc(LspServer *s, const cJSON *params) {
  cJSON *td = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "textDocument");
  cJSON *uri = td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
  if (!uri || !cJSON_IsString(uri)) return NULL;
  return doc_store_get(&s->docs, uri->valuestring);
}
static const char *get_uri(const cJSON *params) {
  cJSON *td = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "textDocument");
  cJSON *uri = td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
  return (uri && cJSON_IsString(uri)) ? uri->valuestring : NULL;
}
static LspPos get_pos(const cJSON *params) {
  return lsp_pos_from_json(cJSON_GetObjectItemCaseSensitive((cJSON *)params, "position"));
}

/* 请求处理：返回响应（成功或错误）。 */
static cJSON *handle_request(LspServer *s, const char *method, const cJSON *id,
                             const cJSON *params) {
  /* initialize 只能在 UNINITIALIZED 态处理一次。 */
  if (strcmp(method, "initialize") == 0) {
    if (s->state != LSP_UNINITIALIZED)
      return rpc_make_error(id, RPC_INVALID_REQUEST, "server already initialized");
    s->state = LSP_INITIALIZED;
    /* 记录工作区根：优先 workspaceFolders[].uri，回退 rootUri / rootPath。 */
    cJSON *wf = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "workspaceFolders");
    if (wf && cJSON_IsArray(wf)) {
      int n = cJSON_GetArraySize(wf);
      for (int i = 0; i < n; i++) {
        cJSON *fo = cJSON_GetArrayItem(wf, i);
        cJSON *u = cJSON_GetObjectItemCaseSensitive(fo, "uri");
        if (u && cJSON_IsString(u)) workspace_add_root_uri(&s->ws, u->valuestring);
      }
    } else {
      cJSON *ru = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "rootUri");
      cJSON *rp = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "rootPath");
      if (ru && cJSON_IsString(ru)) workspace_add_root_uri(&s->ws, ru->valuestring);
      else if (rp && cJSON_IsString(rp)) workspace_add_root_path(&s->ws, rp->valuestring);
    }
    return rpc_make_response(id, make_initialize_result());
  }

  /* shutdown 后任何请求都无效。 */
  if (s->state == LSP_SHUTDOWN)
    return rpc_make_error(id, RPC_INVALID_REQUEST, "server is shutting down");

  /* initialize 之前，除 initialize 外的请求一律 not-initialized。 */
  if (s->state == LSP_UNINITIALIZED)
    return rpc_make_error(id, RPC_SERVER_NOT_INITIALIZED, "server not initialized");

  /* 已初始化：处理具体方法。 */
  if (strcmp(method, "shutdown") == 0) {
    s->state = LSP_SHUTDOWN;
    return rpc_make_response(id, cJSON_CreateNull());
  }

  /* ---- 语言功能 ---- */
  if (strcmp(method, "textDocument/documentSymbol") == 0) {
    Document *d = get_doc(s, params);
    return rpc_make_response(id, d ? feature_document_symbols(d) : cJSON_CreateArray());
  }
  if (strcmp(method, "textDocument/hover") == 0) {
    Document *d = get_doc(s, params);
    return rpc_make_response(id, d ? feature_hover(d, get_pos(params)) : NULL);
  }
  if (strcmp(method, "textDocument/definition") == 0) {
    Document *d = get_doc(s, params);
    return rpc_make_response(id, d ? feature_definition(d, get_pos(params), get_uri(params)) : NULL);
  }
  if (strcmp(method, "textDocument/references") == 0) {
    Document *d = get_doc(s, params);
    int incl = 1;
    cJSON *ctx = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "context");
    cJSON *idf = ctx ? cJSON_GetObjectItemCaseSensitive(ctx, "includeDeclaration") : NULL;
    if (idf && cJSON_IsBool(idf)) incl = cJSON_IsTrue(idf);
    return rpc_make_response(id, d ? feature_references(d, get_pos(params), get_uri(params), incl)
                                   : cJSON_CreateArray());
  }
  if (strcmp(method, "textDocument/completion") == 0) {
    Document *d = get_doc(s, params);
    return rpc_make_response(id, d ? feature_completion(d, get_pos(params)) : cJSON_CreateArray());
  }
  if (strcmp(method, "textDocument/signatureHelp") == 0) {
    Document *d = get_doc(s, params);
    return rpc_make_response(id, d ? feature_signature_help(d, get_pos(params)) : NULL);
  }
  if (strcmp(method, "textDocument/rename") == 0) {
    Document *d = get_doc(s, params);
    cJSON *nn = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "newName");
    const char *new_name = (nn && cJSON_IsString(nn)) ? nn->valuestring : NULL;
    return rpc_make_response(id, (d && new_name) ? feature_rename(d, get_pos(params),
                                                                  get_uri(params), new_name)
                                                 : NULL);
  }
  if (strcmp(method, "textDocument/semanticTokens/full") == 0) {
    Document *d = get_doc(s, params);
    return rpc_make_response(id, d ? feature_semantic_tokens_full(d) : NULL);
  }
  if (strcmp(method, "textDocument/formatting") == 0) {
    Document *d = get_doc(s, params);
    cJSON *opts = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "options");
    return rpc_make_response(id, d ? feature_format(d, opts) : NULL);
  }

  if (strcmp(method, "workspace/symbol") == 0) {
    cJSON *q = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "query");
    const char *query = (q && cJSON_IsString(q)) ? q->valuestring : "";
    return rpc_make_response(id, workspace_symbols(&s->ws, query));
  }

  /* 未知/未实现的方法。 */
  return rpc_make_error(id, RPC_METHOD_NOT_FOUND, method);
}

/* 通知处理：无响应。 */
static void handle_notification(LspServer *s, const char *method, const cJSON *params) {
  if (strcmp(method, "exit") == 0) {
    s->exit_code = (s->state == LSP_SHUTDOWN) ? 0 : 1;
    s->should_exit = true;
    return;
  }
  if (strcmp(method, "initialized") == 0)
    return;

  /* 文档同步（Full）：didOpen/didChange 携带整篇文本。 */
  if (strcmp(method, "textDocument/didOpen") == 0) {
    cJSON *td = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "textDocument");
    cJSON *uri = td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
    cJSON *text = td ? cJSON_GetObjectItemCaseSensitive(td, "text") : NULL;
    cJSON *ver = td ? cJSON_GetObjectItemCaseSensitive(td, "version") : NULL;
    if (uri && cJSON_IsString(uri) && text && cJSON_IsString(text)) {
      doc_store_open(&s->docs, uri->valuestring, text->valuestring,
                     strlen(text->valuestring), ver && cJSON_IsNumber(ver) ? ver->valueint : 0);
      publish_diagnostics(s, uri->valuestring);
    }
    return;
  }
  if (strcmp(method, "textDocument/didChange") == 0) {
    cJSON *td = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "textDocument");
    cJSON *uri = td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
    cJSON *ver = td ? cJSON_GetObjectItemCaseSensitive(td, "version") : NULL;
    cJSON *changes = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "contentChanges");
    /* Full 同步：取最后一个变更的 text 为整篇内容。 */
    cJSON *last_text = NULL;
    if (changes && cJSON_IsArray(changes)) {
      int n = cJSON_GetArraySize(changes);
      for (int i = 0; i < n; i++) {
        cJSON *ch = cJSON_GetArrayItem(changes, i);
        cJSON *t = cJSON_GetObjectItemCaseSensitive(ch, "text");
        if (t && cJSON_IsString(t))
          last_text = t;
      }
    }
    if (uri && cJSON_IsString(uri) && last_text) {
      doc_store_change(&s->docs, uri->valuestring, last_text->valuestring,
                       strlen(last_text->valuestring),
                       ver && cJSON_IsNumber(ver) ? ver->valueint : 0);
      publish_diagnostics(s, uri->valuestring);
    }
    return;
  }
  if (strcmp(method, "textDocument/didClose") == 0) {
    cJSON *td = cJSON_GetObjectItemCaseSensitive((cJSON *)params, "textDocument");
    cJSON *uri = td ? cJSON_GetObjectItemCaseSensitive(td, "uri") : NULL;
    if (uri && cJSON_IsString(uri)) {
      doc_store_close(&s->docs, uri->valuestring);
      /* 关闭时清空该文件的诊断（推送空数组）。 */
      cJSON *p = cJSON_CreateObject();
      cJSON_AddStringToObject(p, "uri", uri->valuestring);
      cJSON_AddItemToObject(p, "diagnostics", cJSON_CreateArray());
      lsp_emit(s, rpc_make_notification("textDocument/publishDiagnostics", p));
    }
    return;
  }

  /* 其它通知：暂忽略。 */
}

cJSON *lsp_dispatch(LspServer *s, const cJSON *msg) {
  if (!msg || !cJSON_IsObject(msg))
    return rpc_make_error(NULL, RPC_INVALID_REQUEST, "message is not a JSON object");

  const char *method = rpc_method(msg);
  const cJSON *id = rpc_id(msg);

  FILE *dbg = spt_open_log();
  if (dbg) { fprintf(dbg, "dispatch: method=%s has_id=%d\n", method ? method : "(null)", id ? 1 : 0); fflush(dbg); fclose(dbg); }

  if (!method) {
    /* 没有 method：若像请求则报错，否则忽略。 */
    if (id)
      return rpc_make_error(id, RPC_INVALID_REQUEST, "missing method");
    return NULL;
  }

  if (rpc_is_request(msg)) {
    cJSON *resp = handle_request(s, method, id, rpc_params(msg));
    dbg = spt_open_log();
    if (dbg) { fprintf(dbg, "dispatch: handle_request done resp=%s\n", resp ? "yes" : "no"); fflush(dbg); fclose(dbg); }
    return resp;
  }

  handle_notification(s, method, rpc_params(msg));
  dbg = spt_open_log();
  if (dbg) { fprintf(dbg, "dispatch: handle_notification done\n"); fflush(dbg); fclose(dbg); }
  return NULL;
}

/* 生产出口：把服务器主动消息写到 out 并释放。 */
static void stdio_emit(void *ctx, cJSON *msg) {
  FILE *out = (FILE *)ctx;
  rpc_write(out, msg);
  cJSON_Delete(msg);
}

int lsp_run(LspServer *s, FILE *in, FILE *out) {
  FILE *dbg = spt_open_log();
  RpcReader r;
  rpc_reader_init(&r);
  char chunk[8192];

  lsp_server_set_emit(s, stdio_emit, out);
  if (dbg) { fprintf(dbg, "lsp_run: emit set, entering loop\n"); fflush(dbg); }

  while (!s->should_exit) {
    const char *body;
    size_t blen;
    int rc = rpc_reader_next(&r, &body, &blen);
    if (rc == 1) {
      if (dbg) { fprintf(dbg, "lsp_run: got msg, blen=%zu\n", blen); fflush(dbg); }
      /* 录制接收到的原始消息 */
      FILE *rec = spt_open_record();
      if (rec) { fwrite(body, 1, blen, rec); fputc('\n', rec); fflush(rec); fclose(rec); }
      cJSON *msg = rpc_parse(body, blen);
      if (!msg) {
        if (dbg) { fprintf(dbg, "lsp_run: parse failed\n"); fflush(dbg); }
        cJSON *err = rpc_make_error(NULL, RPC_PARSE_ERROR, "invalid JSON");
        rpc_write(out, err);
        cJSON_Delete(err);
        continue;
      }
      cJSON *resp = lsp_dispatch(s, msg);
      if (resp) {
        if (dbg) { fprintf(dbg, "lsp_run: dispatch done, writing resp\n"); fflush(dbg); }
        rpc_write(out, resp);
        cJSON_Delete(resp);
      } else {
        if (dbg) { fprintf(dbg, "lsp_run: dispatch returned null (notification)\n"); fflush(dbg); }
      }
      cJSON_Delete(msg);
      continue;
    }
    if (rc == -1) {
      if (dbg) { fprintf(dbg, "lsp_run: rpc_reader_next error\n"); fflush(dbg); }
      break;
    }
    if (dbg) { fprintf(dbg, "lsp_run: need more data, reading\n"); fflush(dbg); }
    int fd = spt_fileno(in);
    int n = (int)spt_read(fd, chunk, sizeof(chunk));
    if (dbg) { fprintf(dbg, "lsp_run: read n=%d errno=%d\n", n, errno); fflush(dbg); }
    if (n <= 0)
      break; /* EOF 或错误：客户端断开 */
    rpc_reader_feed(&r, chunk, (size_t)n);
  }

  if (dbg) { fprintf(dbg, "lsp_run: loop exited, should_exit=%d\n", s->should_exit); fflush(dbg); }
  rpc_reader_free(&r);
  if (dbg) fclose(dbg);
  return s->exit_code;
}
