vm修改自2026年发布的官方的lua5.5
spt语言本质是lua，对lua做出了一些改动：
统一调用约定：所有函数 Slot 0 强制预留为 Receiver。

闭包 this 穿透 & 库适配：类方法 Slot 0 命名 "this"，普通函数/闭包命名为 "(receiver)"，实现了闭包 this 穿透。修改了 C 库（如
math、table） Slot 0 为 receiver，提取参数的索引整体 +1。

C 层回调对齐：C 层 table.sort 等底层回调逻辑强制压入 nil 垫片，适配 Slot 0。

List 长度与容量分离：list 底层实现物理容量 (asize) 与逻辑长度 (loglen) 分离。push 触发平摊扩容，pop/remove 仅缩减 loglen
并清理槽位以保证 GC 安全，不物理缩容。

List 严格边界检查：基于 0 索引，严格拦截越界读写（负数、浮点数、>= loglen 均报错），# 运算符始终精准返回 loglen。

Map 长度语义解耦：map 支持任意类型混合键值对，其 # 运算符固定返回 0，彻底与 list 的数组长度语义剥离。

List 专属 API 与底层结构全景：

新增类型标签与枚举：引入基础类型 LUA_TARRAY、变体类型 LUA_VARRAY；Table 结构新增 mode 字段，引入模式枚举 TABLE_ARRAY 与
TABLE_MAP。

新增 C-API 函数 (lapi.c)：lua_createarray、lua_arrayresize、lua_arraylen、lua_arraysetlen。

新增底层表操作函数 (ltable.h/c)：luaH_newarray、luaH_resizearray。

新增内存读写与指针宏 (ltable.h)：getArrTag、getArrVal、arr2obj、obj2arr、farr2val、fval2arr、arraylimit、lenhint。

新增类型判断与转换宏 (lobject.h)： ttisarray、avalue、setavalue2s。

新增 VM 指令与库函数：虚拟机新增 OP_NEWLIST 指令；标准库底层新增 lpush 与 lpop，并导出为 table.push 与 table.pop。

==================== Slot-0 约定的细节澄清 ====================

__call 元方法的 receiver 处理：
tryfuncTM 触发 __call 时，总是用原始对象（func 处）覆盖 Slot 0 的 receiver（func+1 处），
不移动栈、不增长栈、无需额外 checkstack。调用点的 receiver（如 mod.Func() 中的 mod）仅
用于成员查找，绝不作为额外参数泄漏给 __call。因此 __call 元方法的 self 永远是被调用的
对象本身，用户参数紧随其后从 Slot 1 开始。C 层 __call 回调（如 sptxx usertype 构造函数）
的用户参数从 index 2 开始提取，与普通 C 函数一致（receiver 已被覆盖为原始对象，不再是 nil 垫片）。
class 实例化的 __call 闭包 numparams=1（仅 cls），VARARGPREP=1，构造参数走隐藏 varargs 转发。

C 库参数索引规则：
普通 C 库函数 Slot 0 为 nil receiver 垫片，用户参数从 index 2 开始（标准 Lua 是 index 1，
整体 +1）。__call 元方法的 C 回调同样从 index 2 开始，但此时 index 1 是原始对象（被
tryfuncTM 覆盖），而非 nil 垫片。string.gsub 的 repl 参数位于 index 4（nil, s, pattern,
repl），函数/table 类型 repl 的回调强制压入 nil receiver 垫片，捕获参数索引相应 +1。

==================== GC 与 List 编译细节 ====================

lua_movearray 的 GC 写屏障：
lua_movearray 在复制元素到目标 array 时，对每个 collectable 值调用 luaC_barrierback，
确保增量 GC 下黑色容器引用白色对象时触发屏障。因 getArrVal 返回 Value* 而非 TValue*，
需先用 farr2val 构造临时 TValue 再传给 luaC_barrierback。luaC_barrierback 内部已做
iscollectable/isblack/iswhite 三重检查，无脑调用无害，仅在确实需要时产生开销。

List 字面量编译的自适应 flush 阈值：
compile_list_literal 采用自适应策略 list_maxtostore 决定何时 flush：剩余寄存器 ≥160 用
1/5，≥80 用 10，否则用 1。与 lparser.c 的 maxtostore 策略一致，保证 flush 前不耗尽
MAX_FSTACK（255）寄存器上限，避免大 list 编译失败。
==================== 外部符号声明 declare（编译期擦除） ====================

新增 `declare` 语法，用于声明「存在但实现在别处」的符号 —— 典型是 C 绑定的外部库
（如 SDL）或宿主注册的全局。两种形式：

  declare from "sdl" { int Init(int flags); class Window { void Destroy(); } ... }   // 模块声明块
  declare int SDL_Init(int flags);                                                   // 环境声明（单符号）

成员为「签名」：与普通声明同形，但函数/方法体替换为 ';'，不允许 auto、不允许初始化器。
复用既有 `from "..."`（与 import 一一对应），dotted 名（video.SetMode）天然表达子命名空间。

编译期完全擦除：codegen 对 NODE_DECLARE_MODULE 空操作，对 is_ambient 的 var/func/class
提前返回，不产生任何字节码/绑定。运行期 import 仍走真实 require，由 C 侧真实绑定接管；
声明的符号若无宿主提供则运行期为 null（证明未产生绑定），且不替换同名真实绑定。

描述：紧邻声明之前的文档注释作为符号描述，挂到 AST 节点的 doc 字段供工具（LSP）消费。
词法层新增 DOC_LINE_COMMENT（///）与 DOC_BLOCK_COMMENT（/** */，进 DOCS 通道）；
普通注释 // 与 /* */ 仍跳过，空块注释 /**/ 不算文档注释。多行 /// 累积。

实现落点（纯 C 工具链）：
  - spt_token.h：新增 TOK_DECLARE；SptToken 增 doc 字段（旁路，不污染 token 流）。
  - spt_lexer.c：keyword 表加 "declare"；skip_trivia 捕获文档注释，lex_push 挂到后随 token。
  - spt_ast.h：新增 NODE_DECLARE_MODULE；var_decl/func_decl/class_decl 增 is_ambient + doc。
  - spt_parser.c：parse_declare / parse_decl_member / parse_decl_class；parse_statement 分派 TOK_DECLARE。
  - ast_codegen.c：NODE_DECLARE_MODULE 空操作；三个 compile_*_decl 对 is_ambient 提前返回。

g4（仅作文档标准）：LangLexer.g4 增 DECLARE + 文档注释规则 + channels{DOCS}；
LangParser.g4 增 declareModule / ambientDeclaration / declarationMember / declClassMember。

测试：test/02_declarations/declare/*.spt（模块/擦除/外部库共存/文档注释/类，5 个集成用例）
+ tests/TestDeclare.c（AST 契约：节点类型、is_ambient、doc 精确文本、auto/初始化器/函数体拒绝）。

==================== 模块系统（import/export）====================

语法：类 ES6 的 import/export，底层编译为 Lua require 调用。

import 两种形式：
- 命名空间：import * as m from "path"  →  local m = require("path")
- 具名：import { a, b as c } from "path"  →  local __tmp = require("path"); local a = __tmp.a; local c = __tmp.b

export 作为声明前缀：export int x = 0; / export int f() {...} / export class C {...}
仅顶层（scope_depth==0）的 export 有效，codegen 自动收集导出名构造 exports 表并 return。

模块路径：script_dir/?.spt ; $SPT_PATH ; ./?.spt，不支持相对路径和点分路径。
可透明导入 Lua 内置库（import { abs } from "math"），因 SPT searcher 追加在标准 searcher 之后。

加载时机：运行时（执行到 import 语句才 require）。缓存：复用 package.loaded。循环依赖无保护（同 Lua）。

已知限制：
- export vars a,b=... 语法合法但不导出（codegen 不收集 NODE_MUTI_VARIABLE_DECL）
- 无导出模块返回 true，命名空间导入后访问成员报错
- 嵌套 export 语法通过但不导出（is_module_root=false）
