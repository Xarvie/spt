vm修改自2026年发布的官方的lua5.5
spt语言本质是lua，对lua做出了一些改动：
统一调用约定：所有函数 Slot 0 强制预留为 Receiver。

闭包 this 穿透 & 库适配：类方法 Slot 0 命名 "self"，普通函数/闭包命名为 "(receiver)"，实现了闭包 this 穿透。修改了 C 库（如
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