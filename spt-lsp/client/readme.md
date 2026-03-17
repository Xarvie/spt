# sptScript LSP Client

VS Code 客户端扩展，用于连接 sptScript 语言服务器。

## 环境要求

- Windows 10/11
- Node.js 18.x 或更高版本
- VS Code 1.80.0 或更高版本

## 安装步骤

### 1. 安装 Node.js 和 npm

使用 winget 安装 Node.js LTS 版本（npm 会随 Node.js 一起安装）：

```powershell
winget install OpenJS.NodeJS.LTS
```

安装完成后，**重启终端** 使环境变量生效，然后验证安装：

```powershell
node --version
npm --version
```

### 2. 设置 PowerShell 执行策略

如果遇到 "running scripts is disabled on this system" 错误，需要修改执行策略：

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### 3. 安装项目依赖

进入 client 目录并安装依赖：

```powershell
cd C:\Users\ftp\Desktop\spt\spt-lsp\client
npm install
```

这将安装以下依赖：

- `vscode-languageclient` - VS Code 语言客户端库
- `typescript` - TypeScript 编译器
- `@types/node` - Node.js 类型定义
- `@types/vscode` - VS Code API 类型定义

### 4. 编译 TypeScript

```powershell
npm run compile
```

或者在开发时使用监听模式：

```powershell
npm run watch
```

编译后的 JavaScript 文件将输出到 `out/` 目录。

### 5. 配置 LSP 服务器路径

编辑 `src/extension.ts`，修改 `serverCommand` 变量为你的 LSP 服务器可执行文件路径：

```typescript
const serverCommand = 'C:\\Users\\ftp\\Desktop\\spt\\spt-lsp\\cmake-build-debug\\sptscript-lsp.exe';
```

或者在 VS Code 设置中配置：

1. 打开 VS Code 设置 (Ctrl + ,)
2. 搜索 "sptScript LSP"
3. 在 `sptscript.lsp.serverPath` 中填写服务器路径

### 6. 调试扩展

1. 在 VS Code 中打开 `spt-lsp/client` 目录
2. 按 F5 启动调试
3. 这将打开一个新的 VS Code 窗口（扩展开发主机）
4. 打开一个 `.spt` 文件测试 LSP 功能

## 可选：安装扩展生成器

如果需要创建新的 VS Code 扩展，可以安装官方生成器：

```powershell
npm install -g yo generator-code
```

## 项目结构

```
client/
├── .vscode/           # VS Code 配置
│   ├── launch.json    # 调试配置
│   └── tasks.json     # 任务配置
├── out/               # 编译输出目录
├── snippets/          # 代码片段
├── src/               # TypeScript 源码
│   └── extension.ts   # 扩展入口
├── syntaxes/          # 语法高亮
├── language-configuration.json  # 语言配置
├── package.json       # 扩展清单
└── tsconfig.json      # TypeScript 配置
```

## 常见问题

### npm 命令无法识别

确保已重启终端，或手动刷新环境变量：

```powershell
$env:Path = [System.Environment]::GetEnvironmentVariable("Path","Machine") + ";" + [System.Environment]::GetEnvironmentVariable("Path","User")
```

### 脚本执行被禁止

运行以下命令并选择 Y 确认：

```powershell
Set-ExecutionPolicy -ExecutionPolicy RemoteSigned -Scope CurrentUser
```

### LSP 服务器连接失败

1. 确认 LSP 服务器已编译
2. 检查 `serverCommand` 路径是否正确
3. 查看 VS Code 输出面板中的错误信息
