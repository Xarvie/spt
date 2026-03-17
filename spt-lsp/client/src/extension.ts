// 导入 Node.js 的 'path' 模块，用于处理文件路径
import * as path from 'path';
// 导入 VS Code 扩展 API
import * as vscode from 'vscode';

// 从 vscode-languageclient/node 库导入与 LSP 客户端相关的类和类型
import {
    LanguageClient,         // 主要的语言客户端类
    LanguageClientOptions,  // 配置客户端行为的选项
    ServerOptions,          // 配置如何启动服务器的选项
    TransportKind           // 定义通信传输类型 (stdio, ipc, socket)
} from 'vscode-languageclient/node';

// 用于持有 LanguageClient 实例的变量，类型可以为 LanguageClient 或 undefined
let client: LanguageClient | undefined;

/**
 * VS Code 插件的激活入口函数。
 * 当首次打开 SptScript 文件或执行相关命令时，VS Code 会调用此函数。
 * @param context 扩展上下文，包含 API、工作区信息、存储路径等。
 */
export function activate(context: vscode.ExtensionContext) {

    // 在控制台（Help > Toggle Developer Tools）和插件自己的输出通道打印日志
    console.log('[SptScript Client] Activating SptScript LSP Client...');

    // --- 1. 确定 LSP 服务器可执行文件的路径 ---

    // 读取 VS Code 配置中用户指定的路径
    const config = vscode.workspace.getConfiguration('sptscript.lsp');
    let serverCommand: string | null = config.get<string | null>('serverPath', null);

    // 如果用户未在设置中指定路径，则计算默认路径
    if (!serverCommand) {
        // 根据操作系统确定可执行文件名
        const serverExeName = process.platform === 'win32' ? 'sptscript-lsp.exe' : 'sptscript-lsp';
        // 计算相对于扩展安装目录的路径。
        // 假设 C++ 项目构建在与 client 目录同级的 cmake-build-debug 目录下。
        // !! 请根据您的实际构建输出位置调整此相对路径 !!
        const relativePath = path.join('..', 'cmake-build-debug', serverExeName);
        // context.asAbsolutePath 将相对路径转换为扩展安装目录下的绝对路径
        serverCommand = context.asAbsolutePath(relativePath);
        console.log(`[SptScript Client] 'sptscript.lsp.serverPath' not configured. Trying default path relative to extension: ${serverCommand}`);
    } else {
        console.log(`[SptScript Client] Using configured server path: ${serverCommand}`);
    }

    // 检查最终确定的 serverCommand 是否有效
    if (!serverCommand) {
        vscode.window.showErrorMessage('SptScript LSP server path could not be determined. Please check the default path calculation in extension.ts or configure "sptscript.lsp.serverPath" in your VS Code settings.');
        console.error('[SptScript Client] CRITICAL: Failed to determine server path. Aborting activation.');
        return; // 无法启动服务器，退出激活函数
    }
    serverCommand = 'C:\\Users\\ftp\\Desktop\\spt\\spt-lsp\\build\\sptscript-lsp.exe';
    // serverCommand = 'C:\\Users\\ftp\\Desktop\\sptscript-lsp\\cmake-build-release-mingw-clang64\\sptscript-lsp.exe';

    /*
    import * as fs from 'fs';
    if (!fs.existsSync(serverCommand)) {
        vscode.window.showErrorMessage(`SptScript LSP server executable not found at the determined path: ${serverCommand}. Please check the path or your build output.`);
        console.error(`[SptScript Client] Server executable check failed. Path does not exist: ${serverCommand}`);
        return; // 文件不存在，无法启动
    }
    */

    // --- 2. 配置日志模式 ---
    // 读取配置中的日志设置
    const logConfig = vscode.workspace.getConfiguration('sptscript.lsp');
    const enableLog = logConfig.get<boolean>('enableLog', true);
    const logPath = logConfig.get<string>('logPath', '');
    // 构建服务器启动参数
    const serverArgs: string[] = [];
    if (enableLog) {
        serverArgs.push('--log');
        if (logPath) {
            serverArgs.push(logPath);
        }
        console.log(`[SptScript Client] Logging enabled. Log path: ${logPath || 'default (lsp_trace.log)'}`);
    }

    // --- 3. 配置服务器选项 (ServerOptions) ---
    // 定义如何启动服务器进程以及如何与之通信
    const serverOptions: ServerOptions = {
        command: serverCommand,            // 要运行的可执行文件路径
        args: serverArgs,                  // 启动参数（日志模式）
        transport: TransportKind.stdio,    // 使用标准输入/输出进行通信
        options: {
            // cwd: path.dirname(serverCommand), // (可选) 设置服务器的工作目录
            // env: { ...process.env, MY_ENV_VAR: 'value' } // (可选) 为服务器设置环境变量
        }
    };

    // --- 3. 配置客户端选项 (ClientOptions) ---
    // 定义语言客户端的行为，例如它应用于哪些文件，如何同步设置等
    const clientOptions: LanguageClientOptions = {
        // 指定此语言客户端服务于哪些文档
        // 这里我们指定所有 scheme 为 'file' (本地文件) 且 language ID 为 'sptscript' 的文档
        documentSelector: [{ scheme: 'file', language: 'sptscript' }],

        // (可选) 同步设置：将 VS Code 的某些设置同步给服务器
        // synchronize: {
        //     configurationSection: 'sptscript.lsp' // 将 sptscript.lsp 下的所有设置同步给服务器 (服务器需处理 workspace/didChangeConfiguration)
        // },

        // (推荐) 文件事件同步：当工作区中的 .spt 文件发生变动时通知服务器
        synchronize: {
            fileEvents: vscode.workspace.createFileSystemWatcher('**/*.spt')
        },

        // (推荐) 指定一个输出通道来显示服务器的日志 (stderr) 和客户端的内部日志
        outputChannel: vscode.window.createOutputChannel("SptScript LSP")
    };

    // --- 4. 创建并启动语言客户端实例 ---
    console.log('[SptScript Client] Creating the language client...');
    client = new LanguageClient(
        'sptscriptLsp',             // 客户端的唯一 ID (内部使用)
        'SptScript Language Server', // 显示给用户的名称 (例如在状态栏)
        serverOptions,               // 传入服务器选项
        clientOptions                // 传入客户端选项
    );

    // 启动客户端。这也会尝试启动服务器进程。
    console.log('[SptScript Client] Starting the client and server process...');
    client.start()
        .then(() => {
            console.log('[SptScript Client] Language Client and Server started successfully.');
            // 可以在这里添加客户端启动后需要执行的其他逻辑
        })
        .catch(error => {
            // 启动失败
            console.error(`[SptScript Client] Failed to start the language client: ${error}`);
            vscode.window.showErrorMessage(`Failed to start SptScript Language Server: ${error}. Check the Output panel (SptScript LSP) and developer console (Help > Toggle Developer Tools) for more details.`);
        });

    // (可选) 将客户端添加到 context.subscriptions 中，以便 VS Code 可以在插件停用时自动清理
    // context.subscriptions.push(client); // LanguageClient v9+ 不再需要手动添加
}

/**
 * VS Code 插件的停用入口函数。
 * 当 VS Code 关闭或插件被禁用/卸载时调用。
 */
export function deactivate(): Thenable<void> | undefined {
    console.log('[SptScript Client] Deactivating SptScript LSP Client...');
    if (!client) {
        // 如果客户端实例不存在（例如从未成功启动），则直接返回
        return undefined;
    }
    // 请求停止语言客户端（这也会尝试停止服务器进程）
    // stop() 返回一个 Thenable (类似 Promise)，表示停止操作完成
    return client.stop();
}