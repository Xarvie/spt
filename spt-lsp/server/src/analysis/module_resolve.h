/*
** module_resolve.h — SPT 模块路径解析（import "name" -> 文件路径）。
**
** 复制根 README §14.3 的运行时搜索语义：
**   1. 主脚本所在目录：script_dir/?.spt
**   2. 环境变量 SPT_PATH（分号分隔）：每段/?.spt
**   3. 当前工作目录：./?.spt
** 其中 ? 替换为模块名（不含扩展名）。
**
** LSP 场景下"主脚本所在目录"放宽为「当前文件所在目录」（编辑器无单一主脚本）。
** 不支持相对路径 ./xxx、绝对路径、点分路径展开（a.b.c 找的是 a.b.c.spt 而非 a/b/c.spt）。
**
** 返回 1 并写 out（绝对路径），未找到返回 0。
*/
#ifndef SPT_LSP_MODULE_RESOLVE_H
#define SPT_LSP_MODULE_RESOLVE_H

#include <stddef.h>

/* 在 from_path 所在文件中 import 名为 module_name 的模块，解析其 .spt 文件路径。
   out 需至少 4096 字节。返回 1=找到，0=未找到。 */
int resolve_module_path(const char *from_path, const char *module_name, char *out, size_t cap);

#endif /* SPT_LSP_MODULE_RESOLVE_H */
