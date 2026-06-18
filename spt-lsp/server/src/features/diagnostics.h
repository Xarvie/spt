/*
** diagnostics.h — 由文档计算 textDocument/publishDiagnostics 的 params。
*/
#ifndef SPT_LSP_DIAGNOSTICS_H
#define SPT_LSP_DIAGNOSTICS_H

#include "cJSON.h"
#include "documents.h"

/* 解析文档（容错），把前端诊断转为 LSP Diagnostic，返回 publishDiagnostics 的 params：
**   { "uri": ..., "version": ..., "diagnostics": [ {range,severity,source,message}, ... ] }
** 调用方拥有返回值（cJSON_Delete）。 */
cJSON *diagnostics_compute(const Document *d);

#endif /* SPT_LSP_DIAGNOSTICS_H */
