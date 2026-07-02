/*
** lsp_features.h — 各 LSP 功能的入口。
**
** 约定：每个功能接收 Document（+ 需要的参数），内部容错解析，返回应放入响应
** result 的 cJSON（调用方拥有）。返回 NULL 表示 result 为 null。
*/
#ifndef SPT_LSP_FEATURES_H
#define SPT_LSP_FEATURES_H

#include "cJSON.h"
#include "documents.h"
#include "protocol.h"
#include "workspace.h"

/* textDocument/documentSymbol -> DocumentSymbol[] */
cJSON *feature_document_symbols(const Document *d);

/* textDocument/hover -> {contents:MarkupContent, range} | null
   ws 可空；非空时支持跨文件 import 目标 hover。 */
cJSON *feature_hover(const Document *d, LspPos pos, Workspace *ws);

/* textDocument/definition -> Location | null
   ws 可空；非空时支持跨文件 import 跳转。 */
cJSON *feature_definition(const Document *d, LspPos pos, const char *uri, Workspace *ws);

/* textDocument/references -> Location[] */
cJSON *feature_references(const Document *d, LspPos pos, const char *uri, int include_decl,
                          Workspace *ws);

/* textDocument/completion -> CompletionItem[]
   ws 可空；非空时支持命名空间导入 m. 的成员补全来自目标文件导出。 */
cJSON *feature_completion(const Document *d, LspPos pos, Workspace *ws);

/* textDocument/rename -> WorkspaceEdit | null
   ws 可空；非空时支持跨文件重命名导出符号。 */
cJSON *feature_rename(const Document *d, LspPos pos, const char *uri, const char *new_name,
                      Workspace *ws);

/* textDocument/prepareRename -> {range, placeholder} | null */
cJSON *feature_prepare_rename(const Document *d, LspPos pos);

/* textDocument/signatureHelp -> SignatureHelp | null
   ws 可空；非空时支持跨文件具名导入函数的签名帮助。 */
cJSON *feature_signature_help(const Document *d, LspPos pos, Workspace *ws);

/* textDocument/semanticTokens/full -> {data:int[]} */
cJSON *feature_semantic_tokens_full(const Document *d);

/* 语义高亮图例（与 capabilities 中声明一致）。返回 token 类型名数组与修饰名数组。 */
extern const char *const SPT_TOKEN_TYPES[];
extern const int SPT_TOKEN_TYPES_COUNT;

/* textDocument/formatting -> TextEdit[] | null */
cJSON *feature_format(const Document *d, const cJSON *options);

/* textDocument/documentHighlight -> DocumentHighlight[] */
cJSON *feature_document_highlight(const Document *d, LspPos pos);

/* textDocument/foldingRange -> FoldingRange[] */
cJSON *feature_folding_range(const Document *d);

/* textDocument/selectionRange -> SelectionRange | null */
cJSON *feature_selection_range(const Document *d, LspPos pos);

/* textDocument/inlayHints -> InlayHint[]
   ws 可空；非空时支持跨文件函数参数名提示。 */
cJSON *feature_inlay_hints(const Document *d, LspRange range, Workspace *ws);

/* textDocument/codeAction -> CodeAction[]
   Phase 4: 为顶层声明补全缺失的 export 前缀。 */
cJSON *feature_code_action(const Document *d, LspRange range);

/* ---- Phase 6: 导航能力扩展 ---- */

/* textDocument/typeDefinition -> Location | null */
cJSON *feature_type_definition(const Document *d, LspPos pos);

/* textDocument/declaration -> Location | null
   declare 块成员跳声明处；普通符号回退 definition。 */
cJSON *feature_declaration(const Document *d, LspPos pos, const char *uri, Workspace *ws);

/* textDocument/documentLink -> DocumentLink[] */
cJSON *feature_document_link(const Document *d, const char *uri, Workspace *ws);

/* textDocument/prepareCallHierarchy -> CallHierarchyItem[] */
cJSON *feature_prepare_call_hierarchy(const Document *d, LspPos pos, const char *uri);

/* callHierarchy/incomingCalls -> CallHierarchyIncomingCall[] */
cJSON *feature_call_hierarchy_incoming(Workspace *ws, const char *fn_name);

/* callHierarchy/outgoingCalls -> CallHierarchyOutgoingCall[] */
cJSON *feature_call_hierarchy_outgoing(const Document *d, const char *fn_name, Workspace *ws);

/* textDocument/rangeFormatting -> TextEdit[] */
cJSON *feature_range_formatting(const Document *d, LspRange range, const cJSON *options);

/* textDocument/semanticTokens/range -> {data:int[]} */
cJSON *feature_semantic_tokens_range(const Document *d, LspRange range);

#endif /* SPT_LSP_FEATURES_H */
