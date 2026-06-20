# Phase 3~5 手工测试指南

> 将此文件夹作为 LSP 工作区根目录打开，按以下步骤逐项测试。

## 环境准备

1. 编译 LSP server：`cmake --build build --config Release`
2. 用 VS Code / Neovim 等支持 LSP 的编辑器打开此文件夹作为工作区
3. 配置 LSP server 指向 `spt-lsp/server/build/bin/sptscript-lsp.exe`

## 文件总览

| 文件 | 覆盖阶段 | 测试要点 |
|------|----------|----------|
| `utils.spt` | P3/P5 | 导出函数，被多处 import；重命名测试目标 |
| `models.spt` | P3/P5 | 导出类，类型推断 + 成员跳转；重命名测试目标 |
| `services.spt` | P3/P4 | 具名导入 + 命名空间导入；signatureHelp；snippet 补全 |
| `app.spt` | P3/P4/P5 | 主入口；跨文件 definition/hover/references；codeAction |
| `format_demo.spt` | P4 | 格式化（tab/space 混合缩进） |
| `codeaction_demo.spt` | P4 | codeAction（为未 export 函数添加 export） |
| `large_demo.spt` | P5 | 大文件哈希索引性能（100+ 函数） |

---

## P3 测试：跨文件 import 解析 + 签名 + 重命名

### 3a. 跨文件 definition / hover

1. 打开 `services.spt`，光标放在第 4 行 `calculate` 上（调用处）
2. **F12（Go to Definition）**：应跳转到 `utils.spt` 第 1 行 `export int calculate` 定义处
3. **悬停 hover**：应显示 `calculate(int a, int b) -> int` 签名信息
4. 光标放在第 9 行 `User` 上（构造调用）
5. **F12**：应跳转到 `models.spt` 第 1 行 `export class User` 定义处

### 3b. 跨文件 signatureHelp

1. 打开 `services.spt`，光标放在第 5 行 `calculate(` 的 `(` 之后
2. 应弹出签名提示：`calculate(int a, int b) -> int`，参数 `a` 高亮
3. 输入逗号后，`b` 应高亮

### 3c. 跨文件 rename

1. 打开 `utils.spt`，光标放在第 1 行 `calculate` 定义名上
2. **F2（Rename）** 改为 `compute`
3. 验证 `services.spt` 中的 `calculate` 调用也被改为 `compute`
4. **撤销**（Ctrl+Z）恢复

### 3d. 命名空间导入跳转

1. 打开 `services.spt`，光标放在第 9 行 `M.formatTime` 的 `formatTime` 上
2. **F12**：应跳转到 `utils.spt` 第 5 行 `export int formatTime` 定义处

### 3e. 跨文件 references

1. 打开 `utils.spt`，光标放在 `calculate` 定义名上
2. **Shift+F12（Find References）**：应列出 `services.spt` 中的调用处

---

## P4 测试：体验打磨

### 4a. snippet 补全

1. 打开 `services.spt`，在文件末尾空行处触发补全（Ctrl+Space）
2. 输入 `calc`，应看到 `calculate` 补全项
3. 选中后，应插入 snippet：`calculate(${1:a}, ${2:b})`，光标停在 `a` 处
4. Tab 跳到 `b`，再 Tab 完成
5. 输入 `clas`，应看到 `class` 关键字补全，选中后插入 `class ${1:Name} {\n  \n}`

### 4b. 格式化

1. 打开 `format_demo.spt`（包含 tab/space 混合缩进 + 行尾空白）
2. **Shift+Alt+F（Format Document）**
3. 验证：所有 tab 被转为 4 空格，行尾空白被去除，缩进层级正确

### 4c. codeAction

1. 打开 `codeaction_demo.spt`
2. 光标放在第 1 行 `int add` 上（未 export 的函数）
3. **Ctrl+.（Quick Fix）**：应出现 "Add 'export' modifier" 选项
4. 选中后，函数声明前应插入 `export ` 关键字

### 4d. 增量同步

1. 打开 `app.spt`
2. 在第 17 行 `int result = computeScore(10, 20);`，将 `10` 改为 `99`
3. 验证诊断/inlayHint 实时更新（无需重新打开文件）
4. inlayHint 形式：实参前应出现灰色标签 `base=` `bonus=`（取自 services.spt 中 `computeScore(int base, int bonus)` 的形参名），改后视觉为 `computeScore(base= 99, bonus= 20)`
   - 若未显示：VS Code 设置确认 `editor.inlayHints.enabled` 不为 `off`
   - 输出面板（Output → SPT LSP）查看 `textDocument/inlayHint` 响应是否非空

---

## P5 测试：性能基建

### 5a. 哈希索引（大文件符号查找）

1. 打开 `large_demo.spt`（含 100+ 函数定义）
2. 在文件末尾输入 `func` 触发补全
3. 验证补全列表快速出现（哈希索引 O(1) 查找，非 O(n) 扫描）
4. 光标放在任意 `func50` 调用上，F12 跳转应瞬间到达定义

### 5b/5c. 倒排索引 + 依赖图（跨文件 rename 性能）

1. 打开 `models.spt`，光标放在 `User` 类名上
2. **F2（Rename）** 改为 `Account`
3. 验证 `services.spt` 和 `app.spt` 中的 `User` 也被同步修改
4. 验证响应速度（倒排索引 + 依赖图缩小候选集，非全量扫描）

### 5d. 增量失效

1. 打开 `utils.spt`，在 `calculate` 函数体中添加一个新函数 `export int newFunc() { return 0; }`
2. 保存文件
3. 在 `services.spt` 中输入 `newF` 触发补全
4. 验证 `newFunc` 出现在补全列表中（增量失效后单文件重建，非全量重建）
5. 验证其他文件的补全/跳转不受影响

### 5e. workspace/symbol

1. **Ctrl+T（Go to Symbol in Workspace）**
2. 输入 `calculate`，应快速列出所有文件中的 `calculate` 符号
3. 输入 `User`，应列出 `models.spt` 中的 `User` 类
4. 验证响应速度（哈希索引 + 倒排索引加速）

---

## 综合场景

1. 打开 `app.spt`，从 `main` 函数开始
2. 对 `compute` 按 F12 跳到 `utils.spt`（P3 跨文件 definition）
3. 对 `compute` 按 F2 改名为 `calculate`（P3/P5 跨文件 rename）
4. 回到 `app.spt`，在 `calculate(` 后触发 signatureHelp（P3 签名）
5. 在 `User(` 后触发补全，验证 snippet 占位符（P4 snippet）
6. 对 `app.spt` 执行格式化（P4 格式化）
7. 在 `large_demo.spt` 中验证补全响应速度（P5 哈希索引）
