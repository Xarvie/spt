# LSP 跳转和悬浮调试计划

## 问题描述

1. **函数跳转失败**: `foo(5)`, `bar(10)`, `myFoo(3)` 无法跳转到 my_module.spt 的定义
   - 这些函数通过 `import { foo, bar } from "my_module"` 导入
   - `myFoo` 通过 `import { foo as myFoo } from "my_module"` 导入

2. **悬浮提示失败**: `obj.setId(100)` 无法显示悬浮提示，也无法跳转到定义处
   - `obj` 是 `MyModuleClass` 类型的变量
   - `setId` 是 `MyModuleClass` 的方法

## 调试步骤

### 第一阶段：添加详细日志

在以下位置添加日志：

1. **definition() 函数** (LspService.cpp:1467+)
   - 记录查找的节点类型
   - 记录符号解析过程
   - 记录 ImportSymbol 处理
   - 记录跨文件查找

2. **hover() 函数** (LspService.cpp:770+)
   - 记录查找的节点类型
   - 记录 MemberAccessExpr 处理
   - 记录符号解析过程

3. **getResolvedSymbol() 调用点**
   - 记录返回的符号信息

### 第二阶段：测试流程

1. 清空日志文件
2. 编译 LSP
3. 用户在 VSCode 中操作：
   - 尝试跳转到 foo, bar, myFoo 的定义
   - 尝试悬浮 obj.setId
4. 分析日志输出

### 第三阶段：修复问题

根据日志分析结果修复：
- 符号解析问题
- 导入符号链接问题
- 跨文件查找问题

## 日志文件位置

- `C:\Users\ftp\Desktop\spt\spt-lsp\test\lsp_trace.log` - JSON-RPC 消息跟踪
- `C:\Users\ftp\Desktop\lsp_debug.log` - LSP 调试日志

## 编译命令

```
C:\Users\ftp\Desktop\spt\spt-lsp\build.cmd
```
