# spt-lsp 自主验证方案

## 概述

本方案允许在没有VSCode的情况下，通过单元测试和集成测试自主验证LSP功能。

## 架构分析

LSP服务分为两层：
1. **LspService** - 核心服务层，提供API接口（可直接调用测试）
2. **LspServer** - JSON-RPC传输层（需要客户端，测试时可跳过）

测试可以直接调用 `LspService` 的API，无需启动服务器。

## 测试策略

### 第一阶段：解析器测试（验证g4语法适配）

测试目标：确保新的g4语法能正确解析

```cpp
// 测试用例示例
void test_numeric_for() {
    SourceFile file("test.spt", R"(
        for (i = 1, 10) {
            print(i);
        }
    )");
    auto* ast = file.getAst();
    ASSERT(ast != nullptr);
    ASSERT_NO_ERRORS(file);
}

void test_idiv_operator() {
    SourceFile file("test.spt", R"(
        int a = 10 ~/ 3;
        a ~/= 2;
    )");
    auto* ast = file.getAst();
    ASSERT(ast != nullptr);
}

void test_coroutine_type() {
    SourceFile file("test.spt", R"(
        coro myCoroutine() {
            return;
        }
    )");
    auto* ast = file.getAst();
    ASSERT(ast != nullptr);
}

void test_vars_multi_return() {
    SourceFile file("test.spt", R"(
        vars a, b = getValues();
    )");
    auto* ast = file.getAst();
    ASSERT(ast != nullptr);
}
```

### 第二阶段：AST构建测试

测试目标：验证AST节点正确构建

```cpp
void test_for_numeric_ast() {
    SourceFile file("test.spt", "for (int i = 1, 10, 2) { print(i); }");
    auto* ast = file.getAst();
    
    // 验证AST结构
    auto* forStmt = findNode<ForStmtNode>(ast);
    ASSERT(forStmt != nullptr);
    ASSERT(forStmt->style == ForStmtNode::Style::Numeric);
    ASSERT(forStmt->numericVar != nullptr);
    ASSERT(forStmt->start != nullptr);
    ASSERT(forStmt->end != nullptr);
    ASSERT(forStmt->step != nullptr);
}
```

### 第三阶段：LSP功能测试

测试目标：验证LSP核心功能

```cpp
void test_completion() {
    LspService service;
    service.initialize("/workspace");
    
    service.didOpen("file:///test.spt", R"(
        int myVar = 10;
        my
    )", 1);
    
    // 在 "my" 后面请求补全
    Position pos{2, 6};  // "my" 后面
    auto result = service.completion("file:///test.spt", pos);
    
    ASSERT(!result.items.empty());
    ASSERT(hasItem(result, "myVar"));
}

void test_hover() {
    LspService service;
    service.didOpen("file:///test.spt", R"(
        int myVar = 10;
        myVar
    )", 1);
    
    Position pos{2, 5};  // 在 myVar 上
    auto result = service.hover("file:///test.spt", pos);
    
    ASSERT(!result.isEmpty());
    ASSERT(result.contents.find("int") != std::string::npos);
}

void test_definition() {
    LspService service;
    service.didOpen("file:///test.spt", R"(
        int myVar = 10;
        print(myVar);
    )", 1);
    
    Position pos{2, 10};  // 在 myVar 上
    auto result = service.definition("file:///test.spt", pos);
    
    ASSERT(!result.empty());
    ASSERT(result[0].targetRange.start.line == 1);
}
```

## 测试文件结构

```
spt-lsp/
├── tests/
│   ├── CMakeLists.txt
│   ├── test_main.cpp           # 测试入口
│   ├── test_parser.cpp         # 解析器测试
│   ├── test_ast_builder.cpp    # AST构建测试
│   ├── test_lsp_features.cpp   # LSP功能测试
│   └── test_cases/             # 测试用例文件
│       ├── basic/
│       │   ├── variables.spt
│       │   ├── functions.spt
│       │   └── classes.spt
│       ├── new_syntax/
│       │   ├── numeric_for.spt
│       │   ├── idiv_operator.spt
│       │   ├── coroutine.spt
│       │   └── vars_multi_return.spt
│       └── edge_cases/
│           ├── incomplete.spt
│           └── error_recovery.spt
```

## 实现步骤

### 步骤1：创建测试框架
- 创建简单的测试宏（ASSERT, ASSERT_EQ等）
- 创建测试运行器
- 集成到CMake构建系统

### 步骤2：编写解析器测试
- 测试新语法：numeric for, idiv, coro, vars
- 测试边界情况：不完整代码、错误恢复
- 验证诊断信息正确

### 步骤3：编写AST测试
- 验证AST节点类型
- 验证节点属性
- 验证树结构

### 步骤4：编写LSP功能测试
- 补全测试
- 悬停测试
- 定义跳转测试
- 引用查找测试
- 重命名测试

### 步骤5：持续验证
- 每次修改后运行测试
- 添加新测试用例覆盖新功能

## 预期成果

1. **可自动化测试**：所有测试可通过命令行运行
2. **快速反馈**：测试在秒级完成
3. **完整覆盖**：覆盖所有新语法和LSP功能
4. **易于调试**：测试失败时输出详细信息

## 运行测试

```bash
cd spt-lsp/build
ninja lsp_tests
./tests/lsp_tests
```

## 下一步行动

1. 创建 tests/ 目录和 CMakeLists.txt
2. 实现简单的测试框架
3. 编写第一批解析器测试
4. 验证新g4语法正确工作
