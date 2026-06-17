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