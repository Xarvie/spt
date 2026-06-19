# SPT-LSP — 状态与规划（Roadmap）

> 配套文档：架构与已实现能力见 `README.md`；语言规范见根目录 `README.md` / `change.md`。
> 本文件是 LSP 服务器的"稳定前进"总纲：现状盘点 + 分阶段计划 + 纪律。

---

## 一、这是什么

SPT（Lua 5.5 方言）的**纯 C 语言服务器**，直接复用 `spt-lang/src/frontend` 的词法/语法/AST
作为**唯一解析真相**——不引入第二套文法，不依赖 ANTLR/C++，避免与真实编译器漂移。

JSON 用 vendored **cJSON 1.7.19**。仅链接前端 6 个文件（arena/ast/diag/lexer/parser
+ LSP 桥），**不链接 VM/codegen**。

核心分层：
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

可测性核心：`lsp_dispatch(server, msg) -> response|NULL` 是**纯函数**，单元测试在内存内驱动；
主动通知（如 `publishDiagnostics`）经可注入的 `emit` 出口发出。

---

## 二、已实现且经验证的能力

| 能力 | 方法 | 说明 |
|---|---|---|
| 诊断 | `publishDiagnostics` | didOpen/didChange 推送，didClose 清空；前端诊断→LSP 范围 |
| 悬浮 | `textDocument/hover` | 签名/类型 + 文档注释（markdown） |
| 跳转定义 | `textDocument/definition` | 局部/参数/文件级；成员→类成员 |
| 查找引用 | `textDocument/references` | 局部限定到函数体，文件级全文件 |
| 重命名 | `textDocument/rename` | WorkspaceEdit（**当前仅文件内**）；prepareRename 校验 |
| 文档符号 | `textDocument/documentSymbol` | 层级：函数/类+成员/变量/declare 模块 |
| 工作区符号 | `workspace/symbol` | 递归索引 *.spt，子串过滤；打开文档覆盖索引 |
| 补全 | `textDocument/completion` | 关键字 + 可见符号；`.`/`:` 后成员 |
| 签名帮助 | `textDocument/signatureHelp` | 活动参数随逗号推进 |
| 语义高亮 | `textDocument/semanticTokens/full` | 标识符分类（函数/类/属性/变量），补充 TextMate |
| 格式化 | `textDocument/formatting` | 保守：去行尾空白 + 末尾单换行 |
| 高亮同符号 | `textDocument/documentHighlight` | 复用 sem_references，kind=Text |
| 折叠 | `textDocument/foldingRange` | 纯语法：块/class_decl/declare_module 区间 |
| 选区层级 | `textDocument/selectionRange` | 标识符 token 范围 |
| 重命名预校验 | `textDocument/prepareRename` | 拒绝 declare 外部符号/无定义处 |

**`declare` 外部符号**已集成进语义层：文档符号、成员补全、semantic_tokens、signature help
均遍历 `NODE_DECLARE_MODULE` 成员（见 `semantic.c` 三处分支）。

同步：`textDocumentSync = Incremental`（didChange 支持 range 增量补丁，兼容 Full）。

**质量基线**：12 个 ctest 全绿（test_rpc/server/documents/diagnostics/features/
crash_semtok/incomplete/workspace/cross_import/type_infer/phase4）；test_workspace 已跨平台（Windows 用 GetTempPathA/
CreateDirectoryA，POSIX 用 mkdtemp）；ASan+UBSan（含泄漏）已通过。

---

## 三、质量门槛（每次改动必须全过，否则回退）

- `ctest --test-dir build -C Release --output-on-failure`：12 项单元测试
- `run_tests.sh`：端到端管道冒烟（gcc 快速回路，含编译+单测+冒烟）
- ASan+UBSan（含泄漏）：内存/未定义行为回归
- VS Code 客户端手测：开 `.spt` 文件，hover/定义/补全/诊断四项冒烟
- 无回归：现有 15 项能力不得退化

---

## 四、已知边界与系统性弱点

**干净降级（无正确性风险，仅功能缺失）**：
1. **重命名仅文件内**：跨文件同名符号不会被一起改。
2. **签名帮助不跨文件**：`sem_find_function` 只查当前文件的顶层/方法/declare 成员。
3. **类型推断为轻量级**：仅支持变量/参数的类型注解 → 类成员精准化；不做跨函数流敏感推导、
   不推导 `list<T>` 元素类型。推断失败时回退到全文件类成员兜底（零回归）。

**未实现的 LSP 能力**（无前置依赖，可独立加）：
`inlayHint`、`codeAction`。

**语言哲学约束**：SPT 的类型注解是「提示、运行期无效」（见根 README §13）。
因此 LSP **不做硬类型检查报错**（如 `int x = "str"` 不报诊断），只做结构性检查
（未定义名、arity 明显不符等）。类型信息仅用于**精准化跳转/补全/hover**，不用于报错。

---

## 五、分阶段规划（先降风险铺基建，再啃类型推断大特性）

> 核心判断：类型推断（阶段 2）之所以高风险，在于需要在「无类型检查器」的纯 C 语义层上
> 叠加一个轻量类型推导，且不能破坏「全文件类成员」这条已有兜底路径。稳定路径 =
> 先把跨文件解析与轻量基建做掉（降风险+可测），再分阶段啃类型推断。

### 阶段 0 — 基建补强（低风险高价值，可并行多项）✅ 已完成

**0a. 未保存文档覆盖工作区索引** ✅
- **做什么**：`workspace_index` 时，对每个根目录下的 `*.spt`，若 doc_store 中有打开副本，
  用打开副本的文本覆盖磁盘内容。didChange/didOpen 后标记索引脏，下次 `workspace/symbol`
  懒重建（或 didChange 后异步重建）。
- **价值**：编辑中 `workspace/symbol` 立即反映改动；为阶段 1 跨文件解析铺路（目标文件可能正打开）。
- **风险**：低。索引路径已有，只需加一层"打开文档优先"的文本源。

**0b. documentHighlight（高亮同符号）** ✅
- **做什么**：复用 `sem_references`，返回同指代的所有出现为 DocumentHighlight[]。
  读写区分（赋值左侧 = Write，其余 = Read）可后续细化，v1 全返 Read。
- **价值**：编辑器默认高亮光标符号，体验提升明显；几乎零新增代码。
- **风险**：极低。`sem_references` 已存在且经测试。

**0c. foldingRange / selectionRange（纯语法）** ✅
- **做什么**：foldingRange 遍历 AST 的块节点（函数体/class 体/if/for/while）给区间；
  selectionRange 给标识符的包含层级（表达式→语句→函数→类）。
- **价值**：编辑器折叠/双击选区，纯语法无需语义。
- **风险**：低。只读 AST，不碰语义层。

**0d. prepareRename 校验** ✅
- **做什么**：rename 前先回 `textDocument/prepareRename`，校验光标处确实是可重命名标识符
  （非关键字/非内置），返回其区间。客户端据此禁用非法重命名。
- **价值**：避免 rename 把关键字或 declare 外部符号（不可改）误改。
- **风险**：低。复用 `sem_resolve` 的 `found` 字段。

### 阶段 1 — 跨文件 import 解析（高价值，中风险）✅ 已完成

**价值**：从"单文件工具"迈向"项目级工具"的关键一步。SPT 的 import/export 是核心模块机制，
跨文件跳转/hover/补全是真实开发刚需。

**做什么**：
1. **路径解析** ✅：实现 `resolve_module_path(from_file, module_name)`，按运行时语义：
   `script_dir/?.spt ; $SPT_PATH ; ./?.spt`（见根 README §14.3）。返回绝对路径或 NULL。
2. **目标文件解析缓存** ✅：`Workspace` 增加按路径缓存 `SptLspUnit*`（容错解析结果）。
   缓存键=路径，失效策略：磁盘 mtime 变化或该路径有打开文档（用打开文档文本，随 didChange 失效）。
3. **导出符号表** ✅：对目标文件 unit，收集所有 `is_module_root && is_exported` 的声明
   （函数/类/变量），建 `{name -> def_node, file_path, byte_range}` 映射。
4. **接线三处 provider** ✅：
   - `definition`：点击 `import { X }` 的 `X` 或 `m.X`（命名空间导入）→ 跳目标文件定义处。
   - `hover`：`X` / `m.X` 显示目标文件定义的签名+文档。
   - `completion`：`m.` 后列出目标模块的导出符号（替代当前的全文件成员近似）。
5. **`declare from "x"` 联动** ✅：`declare` 块描述的外部模块，其成员已在语义层；本阶段确保
   `import { X } from "x"` 且同文件有 `declare from "x" { ... X ... }` 时，跳转优先到 declare
   声明处（因为目标可能是 C 绑定，无 .spt 源）。

**风险与缓解**：
- 路径解析与运行时不一致 → 严格对齐根 README §14.3 的三段搜索；加测试覆盖。
- 循环 import 死循环 → 解析缓存带"正在解析"标记，遇环返回 NULL（降级为不跳转，不崩溃）。
- 目标文件容错解析失败 → 降级为不跳转，不影响当前文件功能。

**go/no-go** ✅：新增 test_cross_import（解析路径+导出表+跳转目标+declare-from 联动），
全量 ctest 绿（9/9）。

### 阶段 2 — 轻量类型推断（根治成员近似，高风险高价值）✅ 已完成

**价值**：根治"全文件类成员"近似——`a.x` 只列 `a` 的类型的成员，不再混入无关类。

**风险**：高。需在纯 C 语义层引入类型推导，且不能破坏现有兜底。

**做什么**（渐进式，每步独立可回退）：
1. **类型环境** ✅：`SemRef` 扩展 `type_kind`（变量/参数/字段/返回值/类实例/模块命名空间/未知）。
   `sem_resolve` 对变量/参数/字段，顺带解析其类型注解（已有 `sem_type_string` 基础）。
2. **表达式类型推导（前驱表达式）** ✅：对 `EXPR.IDENT` 的 `EXPR`，推导其类型：
   - 变量引用 → 其声明类型注解
   - `ClassName(...)` → `ClassName` 实例
   - `obj.method()` → method 的返回类型注解
   - `import * as m` → 模块命名空间（阶段 1 的导出表）
   - `declare from "x"` 的模块成员 → declare 签名
   - 推导失败 → 回退到「全文件类成员」（现有兜底，零回归）
3. **成员过滤** ✅：`sem_all_members` 增加可选 `type_filter` 参数；推导成功时只列该类型成员，
   失败时仍列全文件成员。
4. **定义跳转精准化** ✅：`a.x` 的 `x` 优先跳 `a` 的类型的 `x` 成员定义；推导失败回退现状。

**不做**：
- 不做跨函数/跨控制流的流敏感推导（SPT 类型是提示，不值得）。
- 不报类型错误诊断（语言哲学约束，见 §四）。
- 不推导 `list<int>` 元素类型用于 `for x in lst` 的 `x.` 补全（v1 不做，待真实需求驱动）。

**go/no-go** ✅：新增 test_type_infer（变量显式类型注解/参数类型注解/成员补全过滤/推断失败回退），
全量 ctest 绿（10/10），VS Code 手测成员补全精准度通过。

### 阶段 3 — 深度语义特性（按真实负载驱动，非必做）

- **跨文件签名帮助** ✅：`sem_find_function` 扩展查目标文件导出函数（阶段 1 缓存复用）。
  `signature.c` 接受 `Workspace*`，本地未找到时经 `sem_import_binding_path` →
  `workspace_resolve_module` → `workspace_get_unit` 跨文件解析。
- **工作区重命名** ✅：`rename` 基于 `workspace/symbol` + 跨文件引用搜索，产出多文件 TextEdit[]。
  支持两种场景：(A) 重命名本地导出符号 → 扫描导入者；(B) 重命名导入符号 →
  解析目标模块在定义文件改名 + 扫描其他导入者。复用 `prepareRename` 校验。
- **inlayHint** ✅：参数名提示（调用处 `f(1, 2)` → `f(a=1, b=2)`）。
  `inlay_hints.c` 递归遍历 AST 找 `NODE_FUNCTION_CALL`，解析函数签名后为每个参数
  生成 `paramName=` 标签（kind=2 Parameter）。复用阶段 2 的类型推导。
- **结构性诊断** ✅（克制）：未定义名警告（`x` 无任何声明）、arity 明显不符
  （调用参数数 vs 声明形参数，仅当差值>1 且无 varargs 时）。**不报类型不匹配**。
  `diagnostics.c` 在无解析错误时追加 `check_undefined_names`（跳过成员访问/import/内建）
  与 `arity_walk_block`（递归 AST 找 `NODE_FUNCTION_CALL` 检查参数数）。

**go/no-go** ✅：新增 test_phase3（跨文件签名/工作区重命名/inlayHint/结构性诊断），
全量 ctest 绿（11/11），无回归。

### 阶段 4 — 体验打磨（低优先）

- **格式化增强** ✅：缩进规范化——读取 `FormattingOptions.tabSize/insertSpaces`，
  将每行前导空白按视觉列数统一为纯 tab 或纯 space，混合 tab/space 自动换算。
  保留行尾空白清理 + 末尾单换行。
- **codeAction** ✅：`textDocument/codeAction` 快速修复——为顶层未 `export` 的
  函数/类/变量声明提供 "Add 'export'" quickfix，生成插入 `export ` 前缀的 TextEdit。
- **snippet 增强** ✅：补全项支持 `insertTextFormat=2`（Snippet）。
  函数/方法符号自动生成参数占位符调用模板（`add(${1:a}, ${2:b})$0`）；
  关键字（class/declare/function/if/while/for/import/return/defer）生成结构化模板。
- **增量同步** ✅：`textDocumentSync=2`（Incremental）。`didChange` 支持 range-based
  增量补丁（`doc_store_change_range` 按 LSP range 定位字节区间后替换），
  同时兼容 Full 变更（无 range 字段时整篇替换）。

**go/no-go** ✅：新增 test_phase4（snippet 补全/格式化缩进/codeAction/增量同步），
全量 ctest 绿（12/12），无回归。

---

## 六、贯穿所有阶段的纪律

1. **单一解析真相**：任何语法/AST 变动必须复用 `spt-lang/src/frontend`，**绝不**在 LSP 内引入
   第二套文法或解析逻辑。容错解析走 `spt_lsp_bridge` 的 `spt_parse_tolerant`。
2. **纯函数核心**：新功能尽量落在 `lsp_dispatch` 的纯函数路径上，保证可单测；
   主动通知经 `emit` 出口，测试用捕获器断言。
3. **降级优于崩溃**：跨文件/类型推导失败时，降级为"不跳转/全文件成员"，绝不崩溃或报错。
   每条新路径都要有兜底分支并加测试。
4. **类型是提示不是法律**：类型信息只用于精准化跳转/补全/hover，**绝不**用于报诊断
   （SPT 类型注解运行期无效，见根 README §13）。结构性诊断只报"未定义名/arity 明显不符"。
5. **位置语义不漂移**：所有位置统一用字节偏移，经 Document 的行索引转 LSP (行, UTF-16 列)。
   新功能涉及位置必须走 `doc_offset_at` / `doc_pos_at`，不直接操作行列。
6. **UTF-16 边界**：`character` 是行内 UTF-16 码元计数；多字节/星补字符的转换已有覆盖，
   新增位置相关代码不得绕过 `documents.c` 的转换。
7. **每次改动一个独立单元**：改完立刻过完整门槛（12 ctest + ASan + 手测）再继续。
8. **能力声明诚实**：`make_initialize_result` 只声明已实际接线的能力（见 server.c 注释）。
   新功能先接线再开 capability，不预先声明未实现的。
9. **Windows/POSIX 兼容**：跨文件路径解析用平台无关 API；`test_workspace` 已跨平台
   （Windows 用 GetTempPathA/CreateDirectoryA，POSIX 用 mkdtemp）。新测试若用 POSIX 专属
   API 需提供 Windows 等价实现并在 CMakeLists.txt 无门控加入。
