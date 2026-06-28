SPT 嵌入层标准库 —— 微调规范 (Adjustment Spec)


范围：嵌入层 = Lua 5.5 自带标准库的重新适配，不新增任何模块。
上一版设计里的新增模块（json / reflect / error / testing / bench / actor / log …）
归可选库，随 spt.exe 走，不在本文范围。

设计转向 (v0.2)：既然现有代码可大改，本版不再为 Lua 向后兼容保留任何遗留语义。
全语言统一到一套约定，旧 Lua 习语该破就破。下面所谓"微调"实为"重定义行为"——
凡 Lua 1-based / 闭区间 / "table" 的地方一律推平。



0. 嵌入集（open_libraries() 焊入的库）

库嵌入微调量说明base（全局函数）是大type/pairs/ipairs/raw*/load/_G 都要改string是大全位置 0-based + pattern 捕获位置 0-basedtable是大拆分：list 库（LUA_TARRAY）+ map 库（LUA_TTABLE），不再有 table 模块math是（已适配 slot-0）中random 全部改半开区间utf8是大全字节位置 0-basedio是小(nil,err) 已等于 vars，主要是 defer 约定os是小基本只 slot-0coroutine是小resume/yield 的 slot-0 传参package是小?.spt 模板 + SPT_PATHdebug默认可排除小工具用；生产嵌入常剔除（见决策 6）


1. 横切微调（所有库都吃）


slot-0 ABI：每个 C 函数实参从索引 2 起，索引 1 是 receiver。math、
pcall/xpcall 已做（README §8.2/§8.3），其余逐库审计。
0-based：一切接受/返回的位置、索引、偏移都 0-based。
统一半开区间 [start, end)，上界永远不含：所有区间（string.sub、byte、
list.concat/unpack/move、utf8.*、find 的返回值、可选库 list.slice）一律半开，
与 lua_getarrayrange(idx, start, end) 一致。推论：end - start 即长度（无 off-by-one）；
end 省略时默认到长度；随机整数也遵守同一规则（random(m, n) → [m, n)，见 §2.4）。
负索引从尾计（-1 是最后一个元素）。
list/map 分离：所有"表操作"先判类型——list.* 操作 LUA_TARRAY，map.* 操作 LUA_TTABLE；
pairs/next 兼顾两者；ipairs 仅 list。
#list=loglen，#map=0：依赖 # 的函数（list.insert 默认位、list.concat）
按 loglen 处理 list；对 map 调用 list 操作 → 抛错。
越界抛错是结构性的：list 越界抛错（不是返回 nil）。这是 list 的原始属性，
不走元方法，所以连 rawget(list, 越界) / rawset(list, 越界, v) 都抛。
错误模型：Lua 的 (nil, errmsg) 形状正好等于 SPT 的 vars，无须改；
程序错误/异常用 error() 抛出、pcall 捕获；资源句柄配 defer { h:close(); }。
多返回 (vars)：Lua 原生保留，不动。



2. 逐库行为增量

2.1 base（全局函数）—— 改动最外科

函数Lua 行为SPT 微调pairs(x)遍历 table兼顾两者：list → 0-based 整数键；map → 哈希。底层走 iter() 按类型分发（change.md）ipairs(x)1-based 遍历直到 nillist 专用，0-based，遍历 0..loglen-1；返回 4 值（SPT 协议）+ 越界检查（change.md 已修）；对 map 返回空迭代next(t, k)下一键值list：0-based 整数键序；map：哈希序tostring(x)table: 0x…list → [a, b, c]，map → {k: v}（对齐 SPT 字面量）；其余照常tonumber(s, base)解析slot-2 适配；base 与 0-based 无关，不变select(n, ...)1-based 选第 n 个变参改 0-based：select(0, ...) = 全部变参，select(k, ...) = 跳过前 k 个；select('#', ...) 仍取个数assert / error抛出slot-2；error 的 level 语义不变pcall / xpcall保护调用已适配（自动插入 nil receiver，README §8.3）rawget/rawset绕元方法存取0-based；list 仍越界抛错（结构性，非元方法）rawlen(x)长度list → loglen；map → 0rawequal原始相等不变setmetatable/getmetatable元表slot-2；不变load/loadstring/loadfile/dofile编译块，默认全局编译出的块顶层变量默认局部，全局写需 global；块内全局写入持久化全局表（见 §3）collectgarbage(opt)GC 控制保留——这就是嵌入层的 GC 控制接口，不另设 gc 模块require加载模块走 ?.spt 扁平解析；返回 exports 表或 true（README §14.5）；import/export 糖坐其上

2.2 string —— 全位置 0-based

函数微调string.sub(s, start, end)0-based 半开 [start, end)；end 省略默认到 #s；负索引从尾计。整串 = sub(s, 0)。注意：sub(s, 0, -1) 现指"除末字节外全部"（习语已变）string.find(s, pat, init, plain)返回 (start, end) 半开——end 是匹配末字节的下一位置，故 sub(s, start, end) 恰好取出匹配；init 0-based；() 位置捕获返回 0-basedstring.match / gmatch / gsub() 位置捕获与 init 全 0-based；gsub 返回 vars(结果, 次数)string.byte(s, start, end)0-based 半开 [start, end) 字节区间string.char不变（取字节值，返回串）string.len= #s，不变string.rep/upper/lower/reverse/format无位置语义，仅 slot-2 适配string.pack/unpack/packsizeunpack 的 init 与返回的"下一位置"全 0-based（半开推进）


现在 string.sub 与可选库 list.slice、table.concat 等共用同一半开区间约定——
全语言一条规则，无分层例外。



2.3 list / map —— table 模块取消，拆分为 list + map 两库（已实施）

原 table 模块按类型严格拆分：list 库只接受 LUA_TARRAY，map 库只接受 LUA_TTABLE，
不再支持元方法兼容（duck typing）。class 实例底层是 map，归 map 库。

list 库函数（luaopen_list，所有函数校验 LUA_TARRAY）：

函数微调list.insert(l, [pos,] v)pos 0-based；默认 pos = loglen（追加）；pos ∈ [0, loglen] 合法，越界抛错list.remove(l, [pos])0-based；默认移除末尾 loglen-1；返回被移除值list.concat(l, sep, start, end)start, end 0-based 半开 [start, end)list.sort(l, cmp)原地排序；cmp slot-2list.pack(...)返回 list，loglen = 参数个数，不带 .n（用 # 取数）list.unpack(l, start, end)start, end 0-based 半开 [start, end)；展开为变参list.move(src, start, end, dst_at, dst)源区间 [start, end) 0-based 半开（对齐 lua_movearray）list.push / list.pop追加/弹出 loglen 位置list.create(n)创建空 list，容量提示 n

map 库函数（luaopen_map，所有函数校验 LUA_TTABLE）：

函数微调map.create(n)创建空 map，hash 容量提示 nmap.keys(m)返回 list，含 m 的所有键（0-based）map.values(m)返回 list，含 m 的所有值（0-based）map.has(m, k)返回 bool，等价 rawget 后判空map.get(m, k, default)等价 rawget，缺失返回 default（或 null）map.set(m, k, v)等价 rawsetmap.delete(m, k)删除键（设 nil 后 rawset）

map 的遍历仍由 pairs/next 负责（跨类型，留在 base 库）；map 库只提供
显式键操作。这样 list 与 map 的操作集正交，无交叉调用。




2.4 math —— 多数已适配，随机数对齐半开

函数微调math.random(n)返回 [0, n) —— 即长度 n 的 list 的合法 0-based 下标，arr[math.random(#arr)] 直接可用math.random(m, n)返回 [m, n)（半开，上界不含） —— 与全语言区间规则一致（同 Go rand.Intn / Rust gen_range）。骰子写 random(1, 7)；记忆点统一为"上界永远不含"math.random()无参返回 [0, 1) 浮点，不变math.randomseed不变math.type(x)返回 "integer"/"float"/null，保留（对应 SPT int/float 区分）math.tointeger/floor/ceil/fmod/modf/abs/min/max/三角/…纯数学，仅 slot-2 适配（README §8.2 已做）math.pi/huge/maxinteger/mininteger常量，不变

2.5 io —— 改动最小


io.open(path, mode) 返回 (file, nil) 或 (nil, err) —— 已是 vars 形状，不动。
io.read/io.write/io.lines、file:read/write/lines/seek/close —— slot-2 适配。
file:seek(whence, offset)：offset 是字节偏移，0 = 起点，本就 0-based 友好，不变。
io.lines / file:lines 返回迭代器，配 for (auto line : io.lines(p))。
defer 约定（非 API 变更，文档强调）：


sptauto (f, err) = io.open(path, "r");
if (err != null) { return err; }
defer { f:close(); }

2.6 os —— 改动最小


os.time/clock/date/difftime/getenv/exit/remove/rename/execute —— slot-2 适配。
os.date("*t") 返回 map（键→值）；os.getenv 缺失返回 null。
语义基本不变。


2.7 coroutine —— slot-0 传参是要点


create/resume/yield/status/wrap/running/isyieldable/close —— slot-2 适配。
关键：resume(co, a, b) 时，a, b 须按 slot-0 约定到达协程函数（receiver 在 1，
实参从 2 起）；yield/resume 的多值穿透保持。
resume 返回 (true, …) / (false, err) = vars，不变。
协程语义本身不变。


2.8 utf8 —— 全字节位置 0-based

函数微调utf8.codepoint(s, start, end)start, end 0-based 半开 [start, end) 字节区间utf8.len(s, start, end)start, end 0-based 半开 [start, end)utf8.offset(s, n, i)返回 0-based 字节位置；i 0-basedutf8.codes(s)迭代器产出 (0-based 字节位置, 码点)utf8.char不变utf8.charpattern常量串，不变

2.9 package —— 路径与缓存


package.path 用 ?.spt 模板（非 ?.lua），扁平解析（README §14.3）。
搜索顺序：脚本目录 ?.spt → 环境变量 SPT_PATH（分号分隔）→ ./?.spt。
package.loaded/preload/searchers 保留，import/export 复用之。
require 返回 exports 表或 true（README §14.5/§14.6）。


2.10 debug —— 工具库，默认可排除


traceback/getinfo/sethook/getlocal/… —— slot-2 适配，语义基本照旧。
主要供 LSP / 诊断。生产嵌入常整库剔除以减体积/收紧攻击面（见决策 6）。





4. 已定 / 仍待确认

本版已定（推翻了上版的兼容性 hedge）：


typeof() → 只返 "list"/"map"，永无 "table"。
全语言半开区间 [start, end)，上界永远不含——string.sub/find/byte、
list.concat/unpack/move、utf8.*、math.random、list.slice 全部一致。
select() → 0-based。
math.random(n)→[0,n)，math.random(m,n)→[m,n)。
list.pack 返回 list 无 .n。


5. 已完成实施（commit pending）

以下项已按半开区间规则实施完成：

- §1 规则3 半开区间全推平：
  - string.sub/find/byte/match/gmatch/gsub、utf8.* —— 早已 0-based + 半开（前迭代完成）
  - list.concat —— 改为半开 [start, end)，默认 end=loglen（ltablib.c）
  - list.move —— 改为半开 [f, e)，n=e-f（ltablib.c）
  - list.unpack —— 已是半开（前迭代完成）
  - math.random(n) → [0,n)、math.random(m,n) → [m,n)（lmathlib.c）
- §1 规则4 list/map 分离 —— table 模块取消，拆为 list + map 两库：
  - list 库（luaopen_list）：concat/create/insert/pack/unpack/remove/move/sort/push/pop，
    所有函数严格校验 LUA_TARRAY，不再支持元方法兼容（ltablib.c）
  - map 库（luaopen_map）：create/keys/values/has/get/set/delete，
    所有函数严格校验 LUA_TTABLE（ltablib.c）
  - lualib.h 新增 LUA_LISTLIBNAME/LUA_MAPLIBNAME + 位标志链；linit.c 注册两库
  - parser 支持 list/map 在表达式上下文当模块名标识符，类型上下文仍为关键字（spt_parser.c）
  - 测试文件 builtin_table.spt→builtin_list.spt、table_api.spt→list_funcs/list_api.spt
- §2.1 typeof —— 返回 "list"/"map"/"int"/"float"/"str"/"fn"/"coro"/"bool"/"null"/"userdata"（lbaselib.c，commit fe4cb9f）
- §1 负索引从尾计 —— list 负索引 -1 = 最后一个元素，-n = 第一个元素：
  - lvm.c OP_GETTABLE/OP_GETI/OP_SETTABLE/OP_SETI 四指令在 array 分支转换负索引
    （idx < 0 时 idx += loglen；JIT 不动，负索引走 side-exit 回解释器）
  - list 库函数 tinsert/tremove/tconcat/tunpack/tmove 的 pos/start/end 参数支持负索引
    （ltablib.c；相对各自 loglen 转换）
  - map 不转换负索引（-1 是普通整数 key，按 hash 查找）
  - 测试：06_literals/list_literal/negative_index.spt、10_builtins/list_funcs/negative_index_lib.spt

待实施（讨论中）：
- §1 规则5 list 越界抛错（rawget/rawset）
- §2.1 rawlen map→0、tostring list/map 格式化
- §2.10 debug upvalue slot-2