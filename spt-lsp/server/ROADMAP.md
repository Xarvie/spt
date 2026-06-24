# SPT-LSP — 状态与规划（Roadmap）

> 配套文档：架构与已实现能力见 `README.md`；语言规范见根目录 `README.md` / `change.md`。
> 本文件是 LSP 服务器的"稳定前进"总纲：现状盘点 + 分阶段计划 + 纪律。

---

## 一、这是什么

SPT（Lua 5.5 方言）的**纯 C 语言服务器**，直接复用 `spt-lang/src` 的词法/语法/AST
作为**唯一解析真相**——不引入第二套文法，不依赖 ANTLR/C++，避免与真实编译器漂移。

JSON 用 vendored **cJSON 1.7.19**。仅链接前端 6 个文件（arena/ast/diag/lexer/parser
+ LSP 桥），**不链接 VM/codegen**。

核心分层：
```
third_party/cjson/   vendored cJSON（JSON 解析/序列化）
../../spt-lang/src/  复用前端：spt_lexer / spt_parser / spt_ast / spt_arena / spt_diag
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
| 诊断 | `publishDiagnostics` | didOpen/didChange 推送，didClose 清空；前端诊断 + 结构性警告（未定义名/arity） |
| 悬浮 | `textDocument/hover` | 签名/类型 + 文档注释（markdown）；跨文件 import 符号 |
| 跳转定义 | `textDocument/definition` | 局部/参数/文件级；成员→类成员；跨文件 import 跳目标定义 |
| 查找引用 | `textDocument/references` | 局部限定到函数体，文件级全文件 |
| 重命名 | `textDocument/rename` | WorkspaceEdit 跨文件（扫描导入者）；prepareRename 校验 |
| 文档符号 | `textDocument/documentSymbol` | 层级：函数/类+成员/变量/declare 模块 |
| 工作区符号 | `workspace/symbol` | 递归索引 *.spt，子串过滤；打开文档覆盖索引 |
| 补全 | `textDocument/completion` | 关键字 + 可见符号；`.`/`:` 后成员；snippet 模板（函数参数占位符/关键字结构化） |
| 签名帮助 | `textDocument/signatureHelp` | 活动参数随逗号推进；跨文件具名导入函数 |
| 语义高亮 | `textDocument/semanticTokens/full` | 标识符分类（函数/类/属性/变量），补充 TextMate |
| 格式化 | `textDocument/formatting` | 缩进规范化（tab/space 转换）+ 行尾空白 + 末尾单换行 |
| 高亮同符号 | `textDocument/documentHighlight` | 复用 sem_references，kind=Text |
| 折叠 | `textDocument/foldingRange` | 纯语法：块/class_decl/declare_module 区间 |
| 选区层级 | `textDocument/selectionRange` | 标识符 token 范围 |
| 重命名预校验 | `textDocument/prepareRename` | 拒绝 declare 外部符号/无定义处 |
| Inlay 提示 | `textDocument/inlayHint` | 调用处参数名提示（`f(a=1, b=2)`） |
| 快速修复 | `textDocument/codeAction` | "Add 'export'" quickfix（顶层未导出声明） |

**`declare` 外部符号**已集成进语义层：文档符号、成员补全、semantic_tokens、signature help
均遍历 `NODE_DECLARE_MODULE` 成员（见 `semantic.c` 三处分支）。

同步：`textDocumentSync = Incremental`（didChange 支持 range 增量补丁，兼容 Full）。

**质量基线**：16 个 ctest 全绿（test_rpc/server/documents/diagnostics/features/
crash_semtok/incomplete/workspace/cross_import/type_infer/phase3/phase4/phase5/phase6/
manual_def/format）；test_workspace 已跨平台（Windows 用 GetTempPathA/
CreateDirectoryA，POSIX 用 mkdtemp）；ASan+UBSan（含泄漏）已通过。

---

## 三、质量门槛（每次改动必须全过，否则回退）

- `ctest --test-dir build -C Release --output-on-failure`：16 项单元测试
- `run_tests.sh`：端到端管道冒烟（gcc 快速回路，含编译+单测+冒烟）
- ASan+UBSan（含泄漏）：内存/未定义行为回归
- VS Code 客户端手测：开 `.spt` 文件，hover/定义/补全/诊断四项冒烟
- 无回归：现有 17 项能力不得退化

---

## 四、已知边界与系统性弱点

**干净降级（无正确性风险，仅功能缺失）**：
1. **类型推断仍为浅层**：仅支持变量/参数的类型注解 → 类成员精准化，以及 `ClassName(...)` 构造调用推导。
   不做跨函数流敏感推导；不推导 `list<T>`/`map<K,V>` 元素类型（`for x in lst` 的 `x.` 补全回退全文件成员）；
   不做方法链二跳推导（`obj.method().` 的返回类型）。推导失败时回退到全文件类成员兜底（零回归）。
2. **结构性诊断克制**：仅报未定义名警告 + arity 明显不符；**不报类型不匹配**（语言哲学约束）。
3. **无持久化索引**：`semantic.c`/`workspace.c` 全程线性扫描——符号查找、引用搜索、跨文件重命名均无哈希/倒排索引。
   didChange 后 `workspace_mark_dirty` 整体失效所有缓存，大工作区首查会重建全量索引。
4. **无模块依赖图**：无法快速回答"谁导入了我"，rename/callHierarchy 需现算。

**语言哲学约束**：SPT 的类型注解是「提示、运行期无效」（见 `spt-lang/README.md` §13）。
因此 LSP **不做硬类型检查报错**（如 `int x = "str"` 不报诊断），只做结构性检查
（未定义名、arity 明显不符等）。类型信息仅用于**精准化跳转/补全/hover**，不用于报错。

---

## 五、分阶段规划（阶段 0-5 已完成；阶段 6 导航扩展 → 阶段 7 语言特性）

> 核心判断：阶段 0-4 已铺完功能层（15 项 LSP 能力 + 跨文件 + 轻量类型推断 + 体验打磨）。
> 当前瓶颈从"功能缺失"转为"性能与索引基建缺失"——`semantic.c`/`workspace.c` 全程线性扫描，
> 无持久化索引。阶段 5 先建索引（高杠杆），阶段 6 在索引上开新导航能力，阶段 7 按真实需求补语言特性。

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

### 阶段 5 — 性能基建（高杠杆，中风险）✅

> 核心判断：`semantic.c`/`workspace.c` 全程线性扫描 + 无持久化索引，是 callHierarchy、
> typeDefinition、跨文件 references、大工作区 rename 的共同瓶颈。先建索引再开新能力，
> 避免新功能叠在 O(n) 扫描上雪崩。每步独立可回退，降级路径 = "索引未命中则回退线性扫描"。

**5a. 轻量哈希表 + 符号名索引** ✅
- **做什么**：在 `analysis/` 引入开放寻址哈希（djb2 散列），建 `{name → Def*}` 索引。
  `find_def_by_name`/`find_class_by_name`/`find_member_anywhere` 优先查哈希，未命中回退现有线性扫描。
- **价值**：大文件符号查找从 O(n) → O(1)；为 typeDefinition/callHierarchy 提供快速符号定位。
- **风险**：中。需保证索引与 AST 生命周期一致（文档变更时重建）；哈希冲突用开放寻址兜底。
- **降级**：哈希未命中或构建失败 → 回退线性扫描，零回归。
- **实现**：`sem_index.h`/`sem_index.c`（djb2 开放寻址，load factor 0.7 扩容）；
  `semantic.c` 单条目缓存（`g_idx_unit`/`g_idx_source` 防过期）；`sem_find_function`/`find_class_by_name`/`find_def_by_name` 均先查哈希。

**5b. 引用倒排索引** ✅
- **做什么**：`Workspace` 维护 `{symbol_name → [(uri, offset, length)]}` 倒排表，索引所有 `.spt` 的标识符 token。
  `references`/`rename`/`documentHighlight` 直接查表，不再全量重扫 token。
- **价值**：跨文件 references/rename 从"遍历全部文件全部 token"→"查表取目标符号的所有出现"。
- **风险**：中。倒排表需随 didChange 增量更新（只重建变更文档对应的条目）。
- **降级**：倒排表缺失/脏 → 回退 `sem_references` 全量扫描。
- **实现**：`workspace.c` 内 `RefIndex`（djb2 哈希 + 墓碑删除）；`workspace_find_occurrences` 公开查询；
  `rename.c` 情况 A 用倒排索引缩小候选文件集，索引未命中回退 `ws->syms` 全量扫描。

**5c. 模块依赖图（反向 import 索引）** ✅
- **做什么**：`Workspace` 维护 `{module_path → [importer_uri]}` 反向索引。
  `workspace_index` 时扫描每个文件的 `import ... from "mod"` 建图。
  rename 导出符号时直接查"谁导入了我"，无需遍历全部 `ws->syms`。
- **价值**：rename 场景 A（重命名本地导出符号 → 扫描导入者）从 O(全部文件) → O(导入者数)。
- **风险**：低。只读 AST 的 import 节点建图，不碰语义层。
- **降级**：依赖图缺失 → 回退现有"遍历 ws->syms 逐文件判断是否导入目标模块"。
- **实现**：`workspace.c` 内 `DepGraph`（djb2 哈希 + 墓碑删除）；`workspace_find_importers` 公开查询；
  `rename.c` 情况 B 用依赖图查 `def_mod_path` 的导入者，索引未命中回退全量扫描。

**5d. 增量失效（按文档粒度）** ✅
- **做什么**：`workspace_mark_dirty` 改为按文档粒度失效——只标记变更文档对应的 unit/倒排条目为脏，
  不再 `free_units` 整体清空。下次查询只重建脏文档，其余复用缓存。
- **价值**：大工作区 didChange 后首查从"全量重建"→"单文档重建"，消除卡顿。
- **风险**：中。需正确处理"文档 A 变更导致依赖 A 的文档 B 的倒排条目也失效"（经 5c 依赖图定位）。
- **降级**：增量失效判断失败 → 回退整体失效（现有行为，慢但正确）。
- **实现**：`workspace_mark_doc_dirty(ws, uri)` — 按 URI 精确移除 `ref_idx`/`dep_graph`/`syms` 条目
 （开放寻址墓碑标记，探测链不断裂），释放对应 `unit` 缓存，单文件 `index_file` 重建；
  `server.c` didOpen/didChange/didClose 改用 `workspace_mark_doc_dirty`；索引未建/已脏时回退 `workspace_mark_dirty`。

**go/no-go**：✅ test_phase5 通过（哈希查找正确性/倒排索引命中/依赖图反向查询/增量失效不漏），
全量 13/13 ctest 绿。

### 阶段 6 — 导航能力扩展（中价值，低风险，依赖阶段 5）

> 阶段 5 的索引基建就位后，以下能力均可低风险接入。每项独立可回退。

**6a. typeDefinition**（`textDocument/typeDefinition`）
- 复用 `infer_class_from_def`，跳转到变量/参数类型注解对应的 `class_decl`。
- 推导失败（无类型注解/内建类型）→ 返回空，客户端回退到 definition。

**6b. declaration**（`textDocument/declaration`）
- `declare from "x" { ... }` 的成员：定义即声明，跳到 declare 块内的声明处（复用 `sem_resolve_declare_member`）。
- 普通符号：回退到 definition（SPT 无头文件/实现分离概念）。

**6c. documentLink**（`textDocument/documentLink`）
- `import { X } from "mod"` / `import * as m from "mod"` 的 `"mod"` 字符串字面量 → 可点击链接到目标文件。
- 复用 `resolve_module_path` 解析目标路径，生成 `DocumentLink { range, target }`。

**6d. callHierarchy**（`textDocument/prepareCallHierarchy` + `incomingCalls`/`outgoingCalls`）
- outgoing：遍历函数体内的 `NODE_FUNCTION_CALL`，列出被调用函数。
- incoming：经阶段 5b 倒排索引查"谁调用了这个函数"。
- 依赖阶段 5b（倒排索引）才能高效；无索引时降级为全量扫描（慢但可用）。

**6e. range 变体补齐**
- `textDocument/rangeFormatting`：复用 `feature_format`，限定 range 内的行。
- `semanticTokens/range`：复用 `feature_semantic_tokens`，限定 range。
- 大文件编辑时只请求可见区域，降低延迟。

**go/no-go**：✅ test_phase6 通过（typeDefinition 跳类型/declaration 跳 declare/documentLink 链接/
callHierarchy 出入边/rangeFormatting 局部格式化），全量 16/16 ctest 绿，无回归。

### 阶段 7 — 语言特性深度支持（按真实负载驱动，非必做）

> 阶段 2 明确"v1 不做容器元素类型推导，待真实需求驱动"。本阶段补齐 SPT 语言特性的 LSP 支持。
> 各项独立，按需求优先级挑选实现，不做大而全。

**7a. 容器元素类型推导**（`list<T>`/`map<K,V>`）
- 变量声明 `list<int> lst` → 推导元素类型为 `int` 类（若 `int` 是 class）或基础类型。
- `for x in lst` 的 `x` 类型 = list 元素类型 → `x.` 补全精准化。
- `lst[0].` 补全也走元素类型。
- 依赖阶段 2 的类型环境扩展；推导失败回退全文件成员。

**7b. 元方法成员补全**（metatable 元表链）
- `obj.` 补全时，若 `obj` 的类有 `__index` 元方法指向另一类/表，列出被索引类的成员。
- hover 显示元表关系（`__index`/`__newindex`/`__call`/运算符元方法）。
- SPT 的 class 原生支持运算符重载，运算符重载方法在 documentSymbol 标注。

**7c. 协程/pcall 内建签名**
- `coroutine.create/resume/yield/wrap/status` 的签名帮助 + hover（内建函数表）。
- `pcall`/`xpcall` 的签名帮助 + 多返回值结构提示。
- 内建函数表硬编码在 `semantic.c`（已有内建名白名单，扩展为带签名的表）。

**7d. global 语义区分**
- `semantic_tokens` 将 `global` 声明的变量标记为 `modification=global`（vs 默认 local）。
- `documentSymbol` 标注全局变量（kind 或 detail 加 `[global]`）。
- 补全列表中 global 符号排序优先级区分。

**7e. 迭代器变量类型推导**
- `for x in ipairs(lst)` → `x` 类型 = list 元素类型（复用 7a）。
- `for k, v in pairs(map)` → `k`/`v` 类型 = map 的 K/V。
- 自定义迭代器：解析迭代器函数的返回类型注解。

**go/no-go**：按实际挑选的子集新增 test_phase7，全量 ctest 绿，无回归。
不做大而全——每项需有真实 SPT 代码场景驱动。

---

## 六、贯穿所有阶段的纪律

1. **单一解析真相**：任何语法/AST 变动必须复用 `spt-lang/src`，**绝不**在 LSP 内引入
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
