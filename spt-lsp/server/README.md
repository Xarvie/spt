# spt-lsp/server — SPT 语言服务器（纯 C）

SPT（Lua 5.5 方言）的语言服务器，**纯 C** 实现，直接复用 `spt-lang/src/frontend` 的词法/语法/AST
作为**唯一解析真相**——不引入第二套文法，不依赖 ANTLR/C++，避免与真实编译器漂移。

JSON 用 vendored **cJSON 1.7.19**（`third_party/cjson`，MIT）。仅链接前端 6 个文件
（arena/ast/diag/lexer/parser + LSP 桥），**不链接 VM/codegen**。

## 构建

独立（推荐，无需整套 VM）：

```sh
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
# 产物： build/bin/sptscript-lsp
```

无 CMake 时（开发/CI 快速回路，gcc）：

```sh
./run_tests.sh      # 编译 + 单元测试 + 端到端管道冒烟；打印 ALL GREEN
```

## 架构

```
third_party/cjson/   vendored cJSON（JSON 解析/序列化）
../../spt-lang/src/frontend/  复用前端：spt_lexer / spt_parser / spt_ast / spt_arena / spt_diag
                     + spt_lsp_bridge（容错解析桥：spt_parse_tolerant -> AST+诊断+token）
src/rpc/             Content-Length 分帧 + JSON-RPC 2.0
src/lsp/             server（生命周期/分派/能力）、documents（Full 同步 + UTF-16↔字节）、
                     protocol（LSP 公共类型 + JSON 互转）
src/analysis/        semantic（符号/作用域/类型/引用）、workspace（跨文件符号索引）
src/features/        各 provider（每个文件一项功能）
src/main.c           stdio 主循环入口（Windows 下切二进制 stdio）
```

可测性：`lsp_dispatch(server, msg) -> response|NULL` 是**纯函数核心**，单元测试在内存内驱动；
`lsp_run` 仅做 stdio 循环。服务器主动通知（如 `publishDiagnostics`）经可注入的 `emit` 出口
发出，测试用捕获器断言，生产用 `stdio_emit` 写 stdout。

## 已实现能力

| 能力 | 方法 | 说明 |
|---|---|---|
| 诊断 | `publishDiagnostics` | didOpen/didChange 推送，didClose 清空；前端诊断→LSP 范围 |
| 悬浮 | `textDocument/hover` | 签名/类型 + 文档注释（markdown） |
| 跳转定义 | `textDocument/definition` | 局部/参数/文件级；成员→类成员 |
| 查找引用 | `textDocument/references` | 局部限定到函数体，文件级全文件 |
| 重命名 | `textDocument/rename` | WorkspaceEdit（含定义） |
| 文档符号 | `textDocument/documentSymbol` | 层级：函数/类+成员/变量/declare 模块 |
| 工作区符号 | `workspace/symbol` | 递归索引 *.spt，子串过滤 |
| 补全 | `textDocument/completion` | 关键字 + 可见符号；`.`/`:` 后成员 |
| 签名帮助 | `textDocument/signatureHelp` | 活动参数随逗号推进 |
| 语义高亮 | `textDocument/semanticTokens/full` | 标识符分类（函数/类/属性/变量），补充 TextMate |
| 格式化 | `textDocument/formatting` | 保守：去行尾空白 + 末尾单换行 |

同步：`textDocumentSync = Full`（每次变更携带整篇文本）。

## 位置语义

文档存入时 CRLF/CR→LF 规范化（不改变 LSP 行/字符语义）。`character` 为行内 **UTF-16 码元**
计数；前端诊断/AST 的 (行, **字节**列) 经行索引转 LSP (行, UTF-16 列)。多字节/星补字符均覆盖。

## 测试

`test/`：`test_rpc` `test_server` `test_documents` `test_diagnostics` `test_features`
`test_workspace`，全部 TDD 先行；`run_tests.sh` 另含端到端管道冒烟。已通过 ASan+UBSan（含泄漏）。

## 客户端

VSCode 客户端在 `../client`。默认查找 `../server/build/bin/sptscript-lsp`，可用设置
`sptscript.lsp.serverPath` 覆盖。

## 设计取舍 / 后续

- 成员补全与成员跳转目前以「全文件类成员」近似（尚无类型推断）。
- 工作区索引读磁盘文件内容；让未保存的打开文档覆盖索引是后续项。
- 「跳到 import 的跨文件源符号」可在 workspace 索引之上叠加。
