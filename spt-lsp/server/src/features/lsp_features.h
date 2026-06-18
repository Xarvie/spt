/*
** features.h — 各 LSP 功能的入口。
**
** 约定：每个功能接收 Document（+ 需要的参数），内部容错解析，返回应放入响应
** result 的 cJSON（调用方拥有）。返回 NULL 表示 result 为 null。
*/
#ifndef SPT_LSP_FEATURES_H
#define SPT_LSP_FEATURES_H

#include "cJSON.h"
#include "documents.h"
#include "protocol.h"

/* textDocument/documentSymbol -> DocumentSymbol[] */
cJSON *feature_document_symbols(const Document *d);

/* textDocument/hover -> {contents:MarkupContent, range} | null */
cJSON *feature_hover(const Document *d, LspPos pos);

/* textDocument/definition -> Location | null */
cJSON *feature_definition(const Document *d, LspPos pos, const char *uri);

/* textDocument/references -> Location[] */
cJSON *feature_references(const Document *d, LspPos pos, const char *uri, int include_decl);

/* textDocument/completion -> CompletionItem[] */
cJSON *feature_completion(const Document *d, LspPos pos);

/* textDocument/rename -> WorkspaceEdit | null */
cJSON *feature_rename(const Document *d, LspPos pos, const char *uri, const char *new_name);

/* textDocument/signatureHelp -> SignatureHelp | null */
cJSON *feature_signature_help(const Document *d, LspPos pos);

/* textDocument/semanticTokens/full -> {data:int[]} */
cJSON *feature_semantic_tokens_full(const Document *d);

/* 语义高亮图例（与 capabilities 中声明一致）。返回 token 类型名数组与修饰名数组。 */
extern const char *const SPT_TOKEN_TYPES[];
extern const int SPT_TOKEN_TYPES_COUNT;

/* textDocument/formatting -> TextEdit[] | null */
cJSON *feature_format(const Document *d, const cJSON *options);

#endif /* SPT_LSP_FEATURES_H */
