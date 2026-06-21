# SPT Trace JIT — 状态与规划（Roadmap）

> 配套文档：实现细节见 `JIT_DEV_NOTES.md`（§10.1–§10.16）。
> 本文件是“稳定前进”的总纲：现状盘点 + 分阶段计划 + 纪律。

---

## 一、这是什么

LuaJIT 风格的**线性 trace JIT**，针对 SPT（魔改 Lua 5.5）。管线：

```
热点探测 → 沿运行时实际路径录制单函数字节码成 SSA IR
        → 优化(常量折叠/代数化简/CSE/DCE/LICM/守卫去重)
        → 直接生成 x86-64 机器码
        → 守卫失败时 exit stub 把寄存器状态回灌到栈、回解释器
```

核心：`spt_jit.c`(录制) / `spt_jit_ir.{c,h}`(IR+优化) / `spt_jit_asm.{c,h}`(汇编器) /
`spt_jit_codegen.c`(IR→机器码+RA+exit stub)。

---

## 二、已实现且经验证的能力

- **标量/控制流**：int/float 算术、位运算、比较、布尔(TEST/TESTSET)、按运行时实际方向
  录制的分支、内层 if-else、LICM、整数标量线性扫描寄存器分配(循环携带值常驻寄存器)。
- **侧 trace 链接（阶段 2）**：不可 if-conversion 的少数臂(数组写/自增/调用)从热侧退出点续录第二条
  trace,经栈中介 C-trampoline 链接回父 trace,避免每个少数迭代退回解释器(详见 §四、DEV_NOTES §10.31)。
- **数组(List)**：读 `a[j]`、写 `a[j]=`、常量下标标量提升 `base[0]`、字符串数组 `w[i]`、
  显式二维 `row=m[i];row[j]`、**链式二维** `m[i][j]`/`m[0][j]`。
- **字符串**：相等分派 `if(s=="lit")`(短串驻留→指针比较)。
- **优化**：`LEN`/`GETI`/整数常量 CSE + 冗余守卫去重(同元素多次读取模式塌缩重复
  读取/长度/守卫，受控 A/B 快 7–11%，收益主要来自去冗余分支)。

**性能**（共享主机 best-of-N，有噪声）：纯叶函数调用内联 `s+=sq(i)` **37×**、标量 10–13×、
biased 分支 13×、布尔 21×、字符串分派 11×、for+while（相位偏移后内层 while 编译）3.3×、
数组读写 1.7–1.9×、点积/矩阵 1.1–1.5×。所有结果与解释器逐字节一致。

---

## 三、质量门槛（每次改动必须全过，否则回退）

- `ctest --test-dir build -j4`：307 项
- `scripts/jit_difftest.sh`：93 个 kernel 差分（JIT-off 全为 0，即每个 kernel 都真正编译出 trace）
- `scripts/jit_difftest_all.sh`：全量 `test/**/*.spt` 差分（304 一致，跳过 `b4_yield_return`）
- **`scripts/jit_fuzz.py`：差分模糊器（28 个生成器，~2500 例/轮，多 seed）**——本项目正确性边角
  的**首要工具**，可复现 seed；mod/idiv、float mod、循环携带置换/拷贝、for+while、常量折叠移位/
  整除取模、侧 trace(`gen_side_store`)等 bug 均由它发现或复现。每改一类语义都应跑 `--seed 1..10 --count 250`。
  侧 trace 相关改动还应叠加低 `SPT_JIT_SIDE_HOT`(如 30)主动触发链接路径，并叠加 `SPT_JIT_SIDE_MIN_IR=0`
  绕过摊销门、强制录制每条侧 trace(否则小臂被门掉、压不到录制/链接代码),difftest 两脚本同理。
- valgrind `--leak-check`（必要时 `--track-origins=yes`）：不确定/未初始化读 + 泄漏
- `entries==exits` 恒等：正确性不变量（每次进入恰好一次退出；`exit=124` 超时＝挂起）
- 受控 A/B（全开 vs 全关，同主机）：量化优化贡献、消噪

---

## 四、已知边界

**干净中止（无正确性风险，仅不加速）**：带分支/循环/副作用的函数调用、写/RMW 方法与带分支的方法体、`TAILCALL`、
C/内置函数、多返回值、`CONCAT`、map 访问、`NEWLIST/SETLIST`、`CLOSURE`、`VARARG`。
注：**纯直线叶函数调用现在会内联**（见阶段 1）；**纯读方法 `obj.method()`（getter/this 字段计算值）现在会内联**（见阶段 3c / §10.46）；均不再中止。**嵌套循环**：内层循环各自从自己的头编译
（for+while 已验证）。带 **常量边界、直线体、trip ≤ `SPT_JIT_UNROLL_MAX`(默认 16)** 的短内层 for 循环，
现由 **阶段 3a 内层展开** 塌进外层 trace（整个嵌套成一条线性 trace，见 §10.33）——曾是头号性能瓶颈
（点积 1.79×、内层=4 仅 0.98× 即负优化），展开后点积 6.33×、内层=4 提到 5.46×。仍无法展开的嵌套
（变量边界、内层体含分支/调用/再嵌套、或 trip 超 UNROLL_MAX）维持原行为：外层 trace 在内层 back-edge
中止回退、内层保留自己的 trace（§10.15）——安全但内层较短时仍受重入开销影响。展开为**递归**：全常量多层嵌套
（如定长矩阵乘）逐层塌进最外层 trace（matmul_const 1.22→7.9×，§10.34）。**例外:展开体若对同一数组既读又写
（潜在 RMW，如直方图 `a[i+j]++`）不展开**——避开重度展开下同数组 RMW 的 codegen 误处理（根因未定位，列为未来调查）；
回退到内层 trace、正确但无展开提速。matmul（写不同数组）、点积（只读）不受此例外影响。**变量边界**内层循环
若边界是**循环不变量**(如 `for j=0,N-1`,N 设一次不改)由**带守卫的推测展开**处理:读运行时值、发守卫钉住、按观测 trip 展开
(matmul_var 1.26→6.06×、nest_var 1.44→8.0×,§10.35);守卫失败则侧退出回 FORPREP、解释器用真实边界重跑(故永远正确)。
不变量分析排除 PHI 归纳变量,故三角 `for j=0,i`、每轮变化的 GETI 边界 `for j=0,lens[i]` 不被推测(分别走常量路径 / 维持原行为)。

**系统性弱点**：
1. 50/50 与偏置不可预测分支:简单臂由 if-conversion 转无分支(§10.25–10.30);
   **带副作用/调用的臂由阶段 2 侧 trace 链接处理**(§10.31)——少数臂改为原生执行,
   每个少数迭代仅跑一条 FORLOOP 后重入父 trace,不再每轮全程退回解释器。
2. `trace_guard_fail` 仍为 0——它是“侧退出（扣除循环结束）”计数，需先识别循环结束快照
   （随阶段 0.5 一并推迟）。注：`trace_exits` 已实现（阶段 0 完成），退出行为现已可见，原“最大
   可观测性缺口”已消除。

---

## 四点五、正确性加固（贯穿性主线，与特性阶段并行）

特性阶段之外，**差分模糊 + 用户报告驱动的 bug 修复**已是工作主体，独立成线。方法：解释器 vs JIT
逐字节对照（`jit_fuzz.py` 随机生成 + 可复现 seed；`jit_difftest_all.sh` 全量），任何失配/挂起即定位
到 IR/字节码/反汇编层。**原则：慢但对的 trace 永远胜过快但错的；挂起是最坏结果（比错值更糟），
宁可中止回退。** 每修一类，模糊器加一个对应生成器把这类固化进回归。

已发现并修复的 bug 类别（细节见 JIT_DEV_NOTES，括号内为节号）：

1. **SPT/Lua 与 C 原生运算符的语义分歧**（最高频来源）：floored 整除取模 vs C 截断（§10.11
   codegen、§10.16 折叠器）、逻辑移位 vs 算术移位 + ≥64 饱和 + 负移位量（§10.16）、float `%`
   未支持类型产出垃圾→改为中止（§10.11）。教训：codegen 和常量折叠器**各自**都要逐运算符对齐 VM
   语义，别漏一边。
2. **寄存器驻留路径的循环携带值风险**：值置换（`a=b;b=t` swap/fib，需并行拷贝）、某槽拷贝被改写槽
   的裸 SLOAD（`b=a;a=a+1`，要旧值却读到改后值）——均在 `ra_analyze` 检测并回退溢出路径
   （§10.13、§10.14）。驻留路径"无 back-edge move、SLOAD 寄存器原地更新"的假设在这两种下失效。
3. **嵌套循环的 trace 边界 / 挂起**：后向跳转必须验证目标==start_pc 才收口，否则把内层循环初始化
   折进体里→常量退出 guard→死循环（§10.15）；录制相位与内层 trip count 对齐时会一直录到退出迭代，
   中止后偏移相位重试（§10.15）。

**仍可继续探的方向**：浮点折叠（`^` 整数 XOR vs 浮点幂、float mod/比较/NaN/inf）、int↔float
类型转换边界、字符串/拼接、位运算与变量移位的更多组合、深层表达式与多态下标。

## 五、分阶段规划（先降风险，再啃大特性）

> 核心判断：高价值特性(调用内联、侧 trace)之所以高风险，在于**宽改动面**(录制器 33+ 处
> 散落槽访问)和**退出正确性**。稳定路径 = 先铺基础设施(降风险+可测)，再分阶段啃大特性。

### 阶段 0 — 真正的退出/守卫计数 ✅已完成（本轮）
- **做了什么**：`SPTTrace` 加 `exit_count[]` 每快照计数器；exit stub 顶部用 RAX/RCX（退出落点处
  是死的 scratch）4 条指令自增（冷路径，零热路径开销）；shutdown 时按 hot_table 聚合成
  `stats.trace_exits`；DEBUG≥2 打印每条 trace 的每退出点热度（snap→pc→次数）。
- **验证**：`entries==exits` 恒等（每次进入恰好一次退出）证明正确；230 ctest + 16 kernel +
  全量差分全绿；valgrind 0。
- **实测数据印证设计**：标量整循环 entries=1/exits=1（一次进入、全程内部成环、末尾一次退出 →
  这就是标量 13× 的原因）；数组求和内层 trace 每外层迭代重入一次（entries=exits=2M，集中在
  循环结束快照）；50/50 分支退出集中在**分支守卫快照**（snap1@pc8）——这正是侧 trace 该挂载的
  热门侧退出点，阶段 2 的定位数据已就绪。

### 阶段 0.5 — 负收益 trace 加黑（推迟，需谨慎设计）
- **关键教训**：50/50 分支每 ~2 次迭代就侧退出一次，看起来“退出率畸高”，但仍净赚(~1.3×)。
  所以**“高退出率 → 加黑”是错的**，会误杀好 trace。真正的负收益信号是**每次迭代都退出**
  (entries≈总迭代数，每次进入只干 ~0 有用迭代)——那正是已修好的点积 bug 的特征。
- **结论**：加黑必须基于“净负”判定(每进入有用迭代数过低)，不是退出率。修掉那批 bug 后，
  实践中净负 trace 已罕见(多态代码用阶段 2 侧 trace 更对症)，故本项优先级下调，待实测出现
  净负 trace 再做。
- **附注**：`guard_fail` 仍为 0——它是“侧退出(扣除循环结束)”计数，需先识别循环结束快照，
  与本阶段一并推迟。

### 阶段 1 — 叶子函数调用内联（用“重构先行”降风险）—— ✅ 纯直线叶函数已完成
- **价值**：从“原语循环”迈向“真实代码”的关键一步，最高价值。
- **状态（已落地）**：对**纯直线叶函数**（仅搬移/常量/标量算术位运算 + 单 RETURN1，无分支/
  循环/调用/表访问/副作用）的内联已实现并通过全量验证。关键洞见：这类被调体录制时**零 guard**，
  故 callee 内无快照/退出/栈帧重建，函数身份检查只在 trace 入口用 C 做一次。实测
  `s += sq(i)` 37×。详见 JIT_DEV_NOTES §10.12。
  - 两步法按计划执行：先 frame_base 重基（sed 86 处、验证字节级不变作地基），再叠加 OP_CALL/
    RETURN1 接线 + proto_is_inlinable 纯度扫描 + 入口检查。
  - 退出策略落地为更简形态：纯直线 callee 无内部 guard → 无需 resume-at-call 重建；只在入口
    校验函数身份。
- **尚待后续增量**：带分支/循环的 callee、C/内置函数、多返回值、嵌套内联、
  参数数≠形参数。当前这些都安全 abort 回退（无回归）。
- **go/no-go（已满足）**：差分套件 + 模糊器新增 gen_inline_call，全绿；现已扩到 285 ctest +
  71 kernel + 全量差分 + 模糊器 + valgrind 0。

### 阶段 2 — 侧 trace 链接（栈中介 C-trampoline，根治系统性弱点）—— ✅ 已完成（本轮）
- **价值**：根治不可 if-conversion 的少数臂(数组写/自增/调用/可能陷入的臂)每轮一次的侧退出——
  §10.25–10.30 的 if-conversion 只覆盖简单 select,带副作用的臂仍退出密集。
- **状态（已落地）**：从热门侧退出点续录第二条 trace 并**链接回接**。
  **设计抉择**:用**栈中介 C-trampoline**(进入父 trace 后在 `sptjit_trace_enter` 循环重入退出 PC 处的已编译 trace),
  **而非 LuaJIT 的退出处寄存器状态匹配**。理由:退出 stub 本就把全状态 flush 回栈、下条 trace 用 SLOAD 重载,
  栈即唯一真相,**无需寄存器匹配**——以单次链接多一遍 flush/reload 的性能,换取风险骤降(契合"慢但对>快但错")。
  终止靠"严格前进(savedpc 必须变)+ `SPT_JIT_MAX_LINK_HOPS` 上限"双保险。
- **实现**:IR `sptir_exit`(无条件退出原语)→ codegen `SPTIR_EXIT`(jmp 退出 stub)→
  录制器 `is_side_trace` 模式(back-edge 处 `target!=start` 时收口为退出而非 abort)→
  trampoline 链接循环 + 未命中时 `maybe_record_side_trace`(父退出计数达 `SPT_JIT_SIDE_HOT` 才录、back-edge 不录)。
- **两个关键修复**(详见 DEV_NOTES §10.31):(a)侧 trace 必须跟随**导致父退出的少数方向**,
  不能套父 trace 的多数剖析(§10.23),否则录成父的镜像、一进门即退、纯负担;
  (b)不在 back-edge(FORLOOP/TFORLOOP/后向 JMP)扎侧 trace(必退化为立即退出)。
- **性能(实测,详见 §10.32)**:栈中介链接每次交接有固定开销(父退出全量 flush + 侧 trace prologue/SLOAD/epilogue),
  **只对实质大小的少数臂划算**。50/50 轻臂(1 数组写)起初是负优化(B/C 0.84×、比解释器还慢);
  加**摊销门** `SPT_JIT_SIDE_MIN_IR`(默认 28,IR 总指令数低于此即丢弃侧 trace、回退解释器兜底)后:
  轻臂回到 ~1.0×(中性、无回归)、中臂 1.09×、重臂 1.23×(对 Phase 2 前基线只增不减)。
  `SPT_JIT_SIDE_MIN_IR=0` 可强制录制(测试用)。
- **验证**:290 ctest(新增 `side_arr_write`/`side_biased_store`/`side_heavy_arm`)+ 76 kernel + 287 全量差分
  (默认设置与 `SIDE_MIN_IR=0` 两档)+ `gen_side_store` 模糊多种子 + valgrind,全 0 失配/0 错误/0 泄漏,
  `entries==exits` 不变量保持。
- **可选后续(非必做)**:寄存器中介链接(近零 per-link 开销,可让轻臂也净赚,但高风险)——
  这是让小侧 trace 也划算的根本路径,栈中介则以摊销门规避负优化;
  侧 trace 内联执行 FORLOOP 省解释器跳;侧-trace-of-侧-trace。

### 阶段 3 — 嵌套循环优化（数据驱动的头号瓶颈）—— 3a 内层展开 ✅ 已完成（本轮）
**通往顶尖 JIT 的第一步。性能画像(§10.33)定位:单循环/标量/浮点已强(4–21×),真正最弱是
嵌套+短内层(点积 1.79×、内层=4 仅 0.98× 即负优化)。先测量否掉了两个红鲱鱼:数组 BCE(禁用边界守卫
点积无变化——守卫被分支预测吃掉,近乎免费)、浮点寄存器分配(多累加 9.8×、Horner 21.4× 已证不是瓶颈)。**

- **3a 内层循环展开 ✅**:外层 trace 录到内层 `OP_FORPREP` 时,若 init/limit/step 是 IR 整数常量、
  trip ≤ `SPT_JIT_UNROLL_MAX`(默认 16,env 可调,0 关)、内层体直线且不写控制槽,就把内层体重放 trip 次
  (idx 取常量)、塌进外层成一条线性 trace,变量下标随之变常量下标(白嫖常量折叠/LICM)。否则回退原行为
  (内层 back-edge abort 外层、内层留自己的 trace)——永远安全。**关键正确性点**:每份副本要 pin
  `reg_map[A]=剩余计数`、`[A+1]=step`、`[A+2]=idx` 为常量,保证副本内守卫侧退出时快照 flush 出的循环状态
  与解释器逐字节一致(否则跑错迭代数)。**效果(零回归)**:点积 1.79→6.33×、内层=4 的负优化 0.98→5.46×,
  内层=256 正确地不展开维持 4.45×,单循环/标量/浮点全维持。详见 §10.33。
- **3a 递归展开 ✅(续)**:展开改为**递归**——含(可递归展开的)内层循环的循环也展开(replay 经
  `rec_inst→FORPREP→try_unroll` 自然递归)。全常量嵌套逐层塌进最外层;变量边界内层在 replay 中途 abort 回退(安全)。
  **定长矩阵乘 matmul_const 1.22→7.9×**(此前只展一层、entries 1.6M)。附带修了 MMBIN 误报(其 A 是操作数非目标,
  加 `unroll_writes_dest_A` 精确区分),详见 §10.34。
- **3a 已知限制 / RMW 回退 ✅**:**展开体若对同一数组既读又写(潜在 RMW,如直方图 `a[i+j]++`)则不展开**——
  重度展开 + 同数组 RMW 经碰撞常量索引时,codegen 误处理 load-after-store(`gen_recursive_nest` 抓出)。
  精准回退保住 matmul(写不同数组 c)、点积(只读)、标量累加的收益;同数组 RMW 回退到内层 trace(正确、约原速)。
  回归 kernel `nested_histogram.spt`。**根因(大展开 + 同数组 RMW 的 codegen 驻留/spill 复用)未精确定位,列为未来调查**:
  修掉它即可让直方图/原地数组更新也展开。详见 §10.34。
- **3a 带守卫的推测展开 ✅(续 2,变量边界)**:内层边界是**循环不变量变量**(`for j=0,N-1`,N 运行时才知道)时,
  录制时读 N 的活值、用**运行时守卫钉住**(两个 GUARD_LE 合成 ==,因 GUARD_EQ 无 codegen)、按观测 trip 展开。
  **正确性完全靠守卫**——猜错就侧退出到 FORPREP、解释器用真实边界重跑(故 eval 精度只影响性能)。不变量分析(叶子限 KINT/SLOAD、
  **排除 PHI 归纳变量**)同时规避负优化:三角 `for j=0,i`(limit=PHI)不推测(实测反走常量路径 6.18×);每轮都变的 GETI 边界不推测。
  **效果:matmul_var(变量维度矩阵乘)1.26→6.06×、nest_var 1.44→8.0×、gemv_var 1.95→5.5×,`guard_fail=0`**(N 不变守卫恒过)。
  回归 kernel `nested_var_bound.spt`、生成器 `gen_speculative_nest`。详见 §10.35。
- **3b（可选/未来）完整嵌套 trace 支持**:IR/codegen 支持两级 LOOP（更通用,覆盖**非不变量**变量 trip 内层——
  即每外层迭代真正变化且非常量可折叠的边界,如 `for j=0,lens[i]`；推测展开已覆盖不变量变量边界。需双层
  寄存器分配/快照,风险高）。优先级因推测展开覆盖了主要场景而下降。

### 阶段 3c — 非数值覆盖（数值强后的真实短板，数据驱动）
数值全绿(4–21×)后重新画像,发现三类**非数值热循环此前 JIT 100% abort**(且轻微负优化):map 访问(OP_GETFIELD)、
for-each(OP_TFORCALL)、方法调用(OP_SELF)。这是"完整 JIT"的主要缺口。
- **map 读取(GETFIELD,常量字符串键)✅**:LuaJIT 式**内联哈希槽快速路径**(纯内存访问 + 守卫,无需 C 调用)——
  常量键 hash 编译期已知,运行时算主位置、守卫节点键==键、命中载值。map_access **0.88→4.89×**。
  **限制**:仅主位置键走快速路径(碰撞链键回退);hash 种子随机故能否 JIT 因运行而异,但**输出永远正确**。
  回归 kernel `map_read.spt`、生成器 `gen_map_access`。详见 §10.36。
- **map 读写链式键 ✅**:`gen_hash_find` 共享辅助走完整碰撞链(逐字节对应 luaH_Hgetshortstr,生成带回边循环,复用 codegen 既有标签/重定位设施)。
  消除主位置限制与 hash 种子依赖:多键碰撞 map 现确定性编译(m1 5 键 4.76×),主位置常见情形零回归(4.90×)。详见 §10.38。
- **map 写入 SETFIELD ✅**:同款内联哈希槽 + 键守卫;**限 int/float 值(非可回收)免 GC 写屏障**、只覆写已存在主位置键(插入侧退出)。
  `m[k]=m[k]+x` 累加循环整体 JIT,map_write **4.03×**。回归 kernel `map_write.spt`、生成器 `gen_map_write`。详见 §10.37。
- **链式容器访问(经 Map 根)✅**:`m["k"][j]`(map-of-list)、`m["k1"]["k2"]`(map-of-map)。把容器解析统一为 `rec_eval_container`(顺 IR `SLOAD/GETI/GETFIELD` 链、按 `LUA_VARRAY/LUA_VTABLE` 标签取 `avalue/hvalue`),并把 OP_GETFIELD 的基 map 改为顺 IR 解析(失败回退 live 栈,零回归)。
  **无新 IR、无 codegen 改动**(GETFIELD 早已可产 ARR/TAB 结果、GETI 早已接受 ARR 基);缺的仅是录制期"顺 IR 解析中间容器"。`a[i][j]`(list-of-list)此前已支持。无 C 调用、无 GC。回归 kernel `nested_container.spt`、生成器 `gen_nested_container`。详见 §10.44。
- **List of Map 元素(对称补全)✅**:`a[i]["k"]` 读/写、`for k,mp:pairs(a)` 取 `mp["k"]`。根因是 GETI/GETTABLE 的元素类型白名单漏了 `TAB`(GETFIELD 早已放行 TAB 值);因 TAB 与 ARR 在 codegen 同构(`Table*`+标签守卫),仅三处白名单各加 `SPTT_TAB`,**无新 IR、无 codegen 改动**。与 Map-of-List 合起来覆盖任意 List/Map 嵌套链。回归 kernel `list_of_map.spt`。详见 §10.45。
- **for-each / OP_TFORCALL(over List)✅**:`for k[,v] : pairs(L)` 特化为**原生索引循环**——决定性事实(`luaH_next`):数组态 List 的迭代键恰为 0,1,…,loglen-1、值=L[键]、越界干净返回 nil。
  故 `newkey=键+1; GUARD newkey<#L; 值=GETI(L,newkey)`,**无 C 调用、无新 IR、无 codegen 改动**(纯 recorder 变换,只发既有 SLOAD/ADD/LEN/GETI/GUARD)。迭代器经一次性 C 入口守卫固定为 `luaB_next`,故 map/ipairs/自定义闭包迭代器均干净拒绝(交解释器)。
  **限制**:暂禁 RA(循环携带键原地自增,会冲坏循环末/类型守卫退出的快照——见 §10.43 关键 BUG;RA 留作后续)。仍远快于解释器逐元素的 luaB_next C 调用。
  回归 kernel `for_each_list.spt`、生成器 `gen_for_each`。附带修掉 ASan 揪出的预存 ifconv 越界。详见 §10.43。
- **方法调用 / OP_SELF + CALL(纯读方法 ✅;写/RMW 后续)**:SPT 类是**元表式**(`Class.__index = Class`、实例 = `setmetatable({}, Class)`),故方法直接挂在接收者元表上,`rec_table_getstr(recv->metatable, key)` 即得(`__index` 就是元表自身;`this.field` 是实例直接字段、已 JIT)。**纯读方法**(getter/基于 this 字段+参数的计算值,无写)现已内联:OP_SELF 解析元表方法 + 武装 pending、OP_CALL 镜像自由函数内联、入口守卫钉住元表+方法身份(重解析回同 proto,防改写)、OP_SETFIELD 基改用 `rec_eval_container`(内联帧里正确取 this 表)。**关键正确性 = 内联帧退出**:退出桩设 `savedpc` 后解释器在 caller 帧恢复,故方法体内守卫一律 **resume-at-SELF**(失败重跑 SELF+CALL+method,**仅纯读体幂等才成立**);**入口前安全网**扫 live 守卫,任一退出 PC 出主 proto 范围即拒编译,**保证绝不发 in-callee 退出桩**。覆盖标量/浮点字段、变量下标读(越界经 resume-at-SELF 正确回退)、零/多参;`varying_receiver`(对象列表,接收者非 SLOAD)与普通 map 干净拒绝。**限制**:写/RMW 暂禁(写后重读 = 写后守卫,resume 会双写;需入口期字段布局守卫)、每 trace 单方法、直线体。无 C 调用、无 GC。回归 kernel `method_call.spt`、生成器 `gen_method_call`。详见 §10.46。

### 阶段 3c.1 — 方法内联后续（按价值推进）—— #2 多方法 ✅、#3 cond-return ✅、#1 写方法单尾写子集 ✅；仅剩多写一般情形
本轮的 resume-at-SELF 只对**纯读**方法成立(体无副作用、重跑幂等)。三个自然增量:

1. **写/RMW 方法(`this.f = ...`)— 单尾写 void 子集 ✅(§10.49);多写一般情形仍需入口期字段布局守卫**。安全子集已落地:**单尾写 void 方法**(setter `void set(int x){this.v=x;}`、累加器 `void add(int x){this.total=this.total+x;}`)——写是体内最后一个产生守卫的 op,故所有守卫在写前、resume-at-SELF 只在写提交前触发、无双写,用**现有 resume-at-SELF + SETFIELD codegen 即可,零新 codegen / 零入口布局守卫**(累加器 1.0×→3.80×)。回归 kernel `write_method.spt`、生成器 `gen_write_method`。详见 §10.49。 **条件写扩展(§10.51)**:`if(c) this.f = A;`(if-only 条件字段写)if-转换成一条无条件尾写 `this.f = select(c, A, old)`——滑动最大/最小跟踪器 `if(x>this.peak)this.peak=x`、in-place clamp `if(this.v<0)this.v=0`、cap 等 now 内联(1.0×→2.88×)。old 取自比较操作数(被写字段的 GETFIELD);复合 §10.48 select × §10.49 单尾写安全性,**无新 codegen**。安全边界(双写/读后写/比较别的字段/if-else)全干净 abort。回归 kernel `condwrite_method.spt`、生成器 `gen_condwrite_method`。详见 §10.51。**仍未做(本项最大工作量)**:**多写**(`this.a=..;this.b=..`)与**读改写后返回**(`this.total=this.total+x; return this.total` 写后还有守卫=重读 this.total)——这类"写后有守卫"必须把字段存在性校验**上提到入口**(录制体收集所访问 `this.<field>` 集合、入口 C 校验每个字段在预期 node)+ 体内发**无守卫**字段访问(需拆 `SPTIR_GETFIELD/SETFIELD` 的守卫与访问，或加 NOGUARD 变体）。零体内守卫 → 无内联帧退出 → 写安全(与写次数无关)。本节已精准 abort 这些,留待该专项。
2. **每 trace 多方法 ✅(§10.47)**。原入口守卫只钉一组(recv_slot/元表/key/proto),第二个不同方法即 abort。已改为**身份列表**(`SPTMethodId methods[8]`)、入口逐一校验、同方法 dedup,故一个热循环里多个方法调用都内联(`a.foo(); a.bar()` 1.0×→5.98×)。纯 recorder/入口守卫改动,无新 IR、无 codegen;每方法仍各自 resume-at-SELF,正确性面不变(安全网对每个方法的 in-callee 退出桩自动兜底)。回归 kernel `multi_method.spt`、生成器 `gen_multi_method`。详见 §10.47。
3. **带分支方法体(cond-return)✅(§10.48)**。原方法门 `proto_is_method_inlinable` 只收直线体。已接入自由函数的 cond-return if-conversion(§10.27):新静态门 `proto_is_condreturn_method_inlinable`(形状 = `if(c){return A}return B`,prefix/臂允许 ifconv-safe + GETFIELD/LEN 字段读)+ OP_CALL 方法路径接线 + 放宽 `rec_try_condreturn_ifconv` 臂扫描。`int clamp(){ if(this.v<0) return 0; return this.v; }` 类(clamp/abs/min/max/两字段 select)now 内联 + 无分支 select(1.0×→5.94×)。纯读 → 仍 resume-at-SELF;**变量下标 GETI 臂排除**(两臂都求值,边界守卫会在不取的臂每轮侧退出=负优化)、float 返回起初 abort(§10.50 已补,见下)。无新 IR、无 codegen。回归 kernel `condreturn_method.spt`、生成器 `gen_condreturn_method`。详见 §10.48。 **浮点扩展(§10.50)**:`rec_try_condreturn_ifconv` 接 §10.30 的位精确浮点 select(FCMPMASK/ICMPMASK/FSELECT),float relu/clamp/abs/min/max(方法与自由函数)now 编译(1.0×→5.31×),NaN/inf 位精确;门放宽到 `opcode_is_ifconv_safe_any`(放行浮点常量与 `/`,陷阱 `//` 仍拒)。回归 kernel `condreturn_float.spt`、生成器 `gen_condreturn_float`。

**统一视角 — 内联帧退出策略阶梯（本轮最重要的结构性认识）**:内联把被调方塌进 caller trace、**不压被调方帧**,故被调方体内任何守卫退出都不能落在被调方 PC(否则 caller 帧下跑被调方字节码 → 损坏)。由弱到强四档,价值/难度递增:
- **零守卫体**(自由函数叶子,§10.12):体内根本无守卫 → 无退出问题。
- **resume-at-SELF**(纯读方法,§10.46):体内守卫退出 PC 设为 caller 的 SELF、重跑整条调用 —— **仅幂等(无副作用)体**成立。
- **入口期布局守卫**(写方法,上文 #1):把可上提的守卫全挪到入口、体内无守卫 —— 写安全,但需无守卫 codegen。
- **通用 resume-at-call**(带分支/**循环**的 callee):一般情形需在退出时**重建被调方 CallInfo**(压帧、设 base、setobjs)才能在被调方 PC 真正恢复。这是**带循环 callee 内联**(GCD/FibIter 类阻断 bench,见阶段 1 后续)与多层内联的**共同前提**,也是最大一块、风险最高。

**安全网(`sptir_optimize` 后扫 live 守卫、退出 PC 出主 proto 范围即拒编译)对以上每一档都是兜底**:任何遗漏/未外提的 in-callee 退出桩都不会被发出,最坏只是该 trace 回退解释器(不损坏)。新增任一档时,先让安全网拦着(确保不出错),再逐步把对应守卫重定向/上提到合法位置直到能编译——本轮纯读方法即按此法推进。

### 阶段 4 — 循环优化深度（未做）
- 循环展开（一般情形,非仅嵌套内层）、强度削减 / 归纳变量化简。每项 A/B/C 基准 + 完整门槛。

### 阶段 5 — 寄存器中介 trace 链接（阶段 2 可选后续，未做）
- 侧 trace 直接共享父 trace 寄存器状态、近零 per-link 开销,让轻臂侧 trace 也净赚(栈中介现以摊销门规避负优化)。
  也能帮到无法展开的嵌套。高风险。

### 阶段 6 — 分配类特性（按真实负载驱动，非必做）
- map 读取+写入(哈希槽内联)**已在阶段 3c 完成**(写限 int/float 免屏障);剩可回收值写(需屏障)、`CONCAT`(注意 O(n²) 本身收益有限)、`NEWLIST`。
- 涉及分配/哈希/GC 屏障，风险高、价值取决于实际负载。只在有明确需求时再做，
  每个单独差分+valgrind 验证。

---

## 六、贯穿所有阶段的纪律

1. 每次改动一个独立单元；改完立刻过完整门槛（见 §三：ctest + 两个差分脚本 + 模糊器 + valgrind）再继续。
2. 正确性压倒性能；**正确但不值(提速＜0 或加复杂度换 1%)也要权衡甚至回退**。
3. “trace 行为怪异/不确定”先上 valgrind——但 valgrind 干净 ≠ 正确(ASLR 相关、
   defined-but-wrong 会漏)；必要时用非扰动的每快照退出计数定位。
4. 窄字段用窄读(loglen 当作 8 字节读 → ASLR 相关间歇 bug 的教训)。
5. DCE/规范化必须跟 aux ref(边界守卫的 bound 存在 aux 里)。
6. 不提升产出数组的 GETI(链式二维 base 被提升会错)。
7. 录制器读的是录制起点的**冻结栈**；复用槽要从 IR 求值，不能读栈。
8. 守卫→快照是逐指令 `snap_idx` 映射 → NOP/DEAD 冗余守卫安全(不发码的守卫不生成 exit stub)。
9. 运算符语义（移位逻辑/算术、整除取模 floored/截断、移位量饱和、float 类型支持）在 **codegen 和
   常量折叠器两处都要对齐 VM**——折叠器会折叠循环不变的 LOADI 常量，比源码编译器更激进，分歧更易暴露。
10. 循环携带值：值置换、拷贝被改写槽的裸 SLOAD 会破坏驻留路径的"原地更新"假设 → `ra_analyze` 检测后
    回退溢出路径（慢但对）。
11. 嵌套循环不进单条线性 trace：后向跳转只在目标==start_pc 时收口，否则中止；录制相位与内层 trip
    count 对齐会一直录到退出迭代，中止后用 `counter=aborts` 偏移相位重试。
12. 差分模糊器（可复现 seed）是正确性边角的首要工具；每修一类 bug 就加一个生成器固化进回归。

### ★ 下一里程碑:C 调用机制(解锁 math 函数 / for-each / 方法)
可行性已确认(§10.40):基础设施寄存器被调用者保存自动保留、含调用 op 不入 ra_op_is_safe 即禁 RA、体内 RSP≡8 mod 16 故 `sub rsp,8` 对齐。首用例 = 一元 libm math 函数(sqrt/sin/cos/exp/log,叶函数无 GC),经 OP_SELF+CALL 识别。POW 为死路(SPT `^`=XOR)。
- **Unit 1 ✅ GETTABUP codegen(循环内读全局/库表)**:ULOAD 取 _ENV 表 + 复用 gen_hash_find 哈希查找 + 类型守卫;全局读 int 3.98×/float 2.57×;math 路径前置。详见 §10.41。
- **Unit 2 ✅ math 函数 C 调用(SPTIR_FMATH + GUARD_CFUNC)**:`math.sqrt/sin/cos/tan/exp/asin/acos` 经直接 libm C 调用编译;首个 C 调用机制;1.85–2.15×;valgrind 干净。**C 调用里程碑(阶段)达成。** 详见 §10.42。
