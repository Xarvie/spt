# SPT JIT —— 交接文档 (Handoff)

最后更新：§10.68c 通用 resume-at-call 完成。本文件是新接手者的**入口**。详细历史见 `src/jit/JIT_DEV_NOTES.md`（开发日志 §10.1–§10.68c，每个特性的设计/bug/教训），路线全貌见 `src/jit/JIT_ROADMAP.md`。

---

## 1. 项目是什么

`spt` 是一门自定义脚本语言，跑在**改过的 Lua 5.5 VM** 上，带一个 **LuaJIT 式线性 trace JIT**（x86-64）。
流水线：热点探测（FORLOOP 回边计数）→ 把字节码录成 SSA IR → 优化 → x86-64 机器码 → 守卫失败时经 exit stub 冲回解释器。

核心源码（`src/jit/`）：
- `spt_jit.c` —— 录制器（recorder），最大的文件。识别习语、建 IR、做 trace 级变换。
- `spt_jit_codegen.c` —— IR→x86-64 + 线性扫描寄存器分配 + exit stub。
- `spt_jit_ir.{c,h}` —— IR 定义与优化。
- `spt_jit_asm.{c,h}` —— x86-64 汇编器（整数 + SSE2）。
- `JIT_DEV_NOTES.md` / `JIT_ROADMAP.md` —— 开发日志 / 路线。

被 JIT 用到的 VM 文件（暴露 file-static C 函数给录制器辨识）：
- `src/vm/lmathlib.c` —— `spt_jit_unary_math`（math.sqrt/sin/… → libm）、`spt_jit_math_minmax`（math.min/max）。
- `src/vm/lstrlib.c` —— `spt_jit_str_op`（string.len/byte）。
- `src/vm/lbaselib.c` —— `spt_jit_pairs_next`（pairs 迭代器）。

---

## 2. 怎么构建与验证（必读）

```bash
pip install cmake --break-system-packages          # 环境若无 cmake
# Release 构建
cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
cmake --build build -j$(nproc)
# ASan+UBSan 构建（本环境无 valgrind，ASan 是替身；只构建 sptscript）
cmake -B build_asan -DCMAKE_BUILD_TYPE=Debug -DENABLE_ASAN=ON -DENABLE_UBSAN=ON -DBUILD_TESTS=ON
cmake --build build_asan --target sptscript
#   注：build_asan 下 TestDeclare 有一个与本项目无关的预存链接错误，忽略即可，只 build sptscript。
```

**验证门槛（GATE）——任何改动必须全绿才算数：**
```bash
ctest --test-dir build -j$(nproc)                  # 单元/集成测试
bash scripts/jit_difftest.sh                       # JIT 差分（HOT=8），pass=N fail=0
bash scripts/jit_difftest_all.sh                   # 全量差分（HOT=40），match=N mismatch=0 timeout=0
python3 scripts/jit_fuzz.py --seed N --count M     # 差分模糊器（49 个生成器）
# ASan：
export ASAN_OPTIONS=halt_on_error=1:detect_leaks=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
# 对单个 kernel 跑 off==on：
diff <(SPT_JIT=0 build/bin/sptscript K.spt) <(SPT_JIT=on SPT_JIT_HOT=8 build/bin/sptscript K.spt)
```

**调试开关**：`SPT_JIT=on` 开 JIT；`SPT_JIT_HOT=N` 设热阈值；`SPT_JIT_DEBUG=1` 打印 `compiled=/aborted=/entries=/exits=/guard_fail=` 与 "compiled trace"/"aborted trace"；`SPT_JIT_DEBUG=2` 额外打印 abort 的 opcode。

**环境坑**：`/bin/sh` 不是 bash 且无进程替换 `<(...)`——脚本要用 `bash -c '...'` 包。无 `/usr/bin/time`、无 valgrind——计时用 `date +%s.%N`+`bc`。ASan 比 Release 慢约 10×：全量差分会超时，用聚焦子集 `/tmp/asan_subset/`，单 kernel 超时给 ≥60s。

---

## 3. 当前状态（交接时点）

### 已完成并交付：截至 §10.68c
最近交付的完整包是 **§10.68c 通用 resume-at-call**——嵌套内联多帧 3 阶段规划的最后一步。当内联 callee 体内的守卫失败时，exit stub 调用 C helper `sptjit_exit_resume` 重建 callee 的 CallInfo 帧（压帧、设 base/savedpc/callstatus），让解释器在 callee PC 恢复执行。`sptjit_hot_check` 宏更新 `ci/cl/k` 以适配切换后的 callee CI。安全网修改为允许有 resume info（`callee_proto != NULL`）的 in-callee exit PC。新增 `proto_is_branch_inlinable` 允许 forward conditional branches + 多 RETURN1 的 callee 内联。`rec_cond_branch` abort 放宽为仅对 method inline abort。

往前数的三阶段（**嵌套内联多帧 3 阶段**）：
- **§10.68a 栈化基础设施**：SPTInlineFrame 数组 + inline_depth，纯重构零行为变化。
- **§10.68b 零体内守卫方法的多帧内联**：放开 `frame_base != 0` abort，安全网保证正确性。
- **§10.68c 通用 resume-at-call**：带分支 callee 内联，exit stub 重建 callee CI，安全网允许有 resume info 的 in-callee exit PC。

再往前：
- **§10.65 值返回多写方法内联**：放宽 §10.64 的 RETURN1 排除。
- **§10.64 通用多写方法内联**：≥2 SETFIELD 的 VOID 方法经入口期字段布局守卫内联。
- **§10.63 运行期守卫失败黑名单**：根治恒失败运行期守卫的编译颠簸。
- **§10.59–§10.62 math 库内联 + SLEN/SBYTE resume-at-SELF**：min/max/abs/floor/ceil/string.len/byte。

该状态**全门槛绿**：
- **368/368 ctest**、**kernel 差分 pass=135 fail=0**、**模糊 seeds 42+99×200=400 例 0 失配**、entries==exits、guard_fail=0。
- 加速表与各特性细节见 `JIT_ROADMAP.md` 与 `JIT_DEV_NOTES.md §10.x`。

### 已知非问题：`foreach_map_str.spt` 的 map 迭代顺序（已修复）
`pairs(map)` 的迭代顺序依赖 per-process 哈希种子（`luai_makeseed` 混合 `time(NULL)` + 栈地址 ASLR），JIT on/off 两次进程的种子不同 → 字符串键的哈希不同 → 迭代顺序不同。原测试用字符串拼接（`..`，不可交换）暴露了这个差异。**已修复**：改为求字符串长度之和（`string.len(v)`，加法可交换），不再依赖迭代顺序。

> 当前基线 = **§10.68c 完成**。可直接构建（`cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON && cmake --build build`）。

---

## 4. §10.65 值返回多写方法内联的实现（已完成，记录如下）

值返回多写方法 = `int update(int x,int y){ this.a=x; this.b=y; return this.a+this.b; }` 这类 **≥2 SETFIELD + 一个值返回**。§10.64 的多写门保守排除了 OP_RETURN1，本节放开。**采用设计：只动方法门，值返回机器复用，零新 codegen。** 以下为实现记录；完整日志见 `JIT_DEV_NOTES.md §10.65`。

1. **方法门 `proto_is_multiwrite_method_inlinable` 放宽**：第一循环把 `if (o == OP_RETURN1) return 0;` 改为置 `has_value_return=1`；第二循环（体内 op 白名单）放行 OP_RETURN1。
2. **值返回机器复用（早已存在）**：OP_CALL 方法分派早已允许 `c==2`（一个结果）、对所有方法内联设 `call_result_slot` 与 `multiwrite_mode`。OP_RETURN1 的内联分支（`frame_base!=0`）`rec_load_reg` 取返回寄存器 IR ref（已在 reg_map：体内无守卫 GETFIELD/MOVE/ARITH 算出）、恢复 caller 上下文、绑到 `call_result_slot`。这套接线不关心 `multiwrite_mode`，对多写方法零改动即正确。
3. **保证体内严格零守卫（值返回路径）**：多写模式只把 GETFIELD/SETFIELD 改无守卫，**GETI/GETTABLE/LEN 仍发守卫**。值返回方法里"写后 GETI"的守卫若失败会 resume-at-SELF 双写非幂等字段，故门在 `has_value_return` 时额外禁 GETI/GETTABLE/LEN，保证体内严格零守卫（§10.64 安全论证的根基）。VOID 路径门保持 §10.64 原样（该禁令不触发）→ 零回归。
4. **验证**：`test/15_jit/kernels/multiwrite_ret_method.spt`（update/next/set2/store/reset/float upd 六形全编译 + 负向 GETI 形正确 abort，返回值之和为双写/错值探测器）+ `scripts/jit_fuzz.py` 的 `gen_multiwrite_ret_method`（八形含负向 geti_ret）就位。跑全门槛 + ASan（值返回路径以新模式驱动无守卫 GETFIELD codegen，务必 ASan）。

> **关键洞察（本节最重要）**：§10.64 lesson(4) 把 RETURN1 当成"需额外设计值类型守卫上提"的待办，但**入口字段布局守卫（字段返回）+ caller 层实参守卫（参数/计算返回）已经覆盖了所有返回值形态的类型**——所谓"上提"在 §10.64 已隐式完成。值返回不引入任何新守卫，体内零守卫不变。又一例"已验证机器 × 新场景"。

## 5. 之后的路线（优先级，各自专项）

> 嵌套内联多帧 3 阶段（§10.68a/b/c）均已落地，已从本清单移除。

1. **带循环的 callee 内联**：§10.68c 已实现带分支 callee 的 resume-at-call，但带循环的 callee（如 GCD/FibIter）仍需处理循环回边在 callee proto 内的 exit PC。当前 `proto_is_branch_inlinable` 不允许 OP_FORLOOP/OP_FORPREP，带循环的 callee 会安全 abort 回退解释器。
2. **嵌套方法内联**：§10.68c 的 `rec_cond_branch` abort 放宽为仅对 method inline abort。要放开 method inline 的 guarded branch，需处理 method 的 resume-at-SELF 与 resume-at-call 的交互。
3. **值返回多写的精确门扩展（小项）**：当前值返回路径保守地禁了所有 GETI/GETTABLE/LEN，连"GETI 在所有写**之前**"这种其实安全的形态也拒。精确做法是只禁出现在 SETFIELD **之后**的 GETI/GETTABLE/LEN（按写位置扫描），解锁"先读数组再写多字段并返值"。低险、低价值，按需。
4. 路线 §4（循环展开 / 强度削减）、§5（寄存器中介的 trace 链接，高风险）。详见 `JIT_ROADMAP.md`。

---

## 6. 必须知道的纪律与坑（血泪教训）

- **"慢但对 > 快但错"；挂起/内存损坏是最坏的**。每个改动**过完整门槛**才算数。差分模糊是第一道防线。
- **"做对≠做值"**：优先做有真实价值的习语，别只挑容易的。
- **孤立只读新 IR 操作 = 零回归风险**（SLEN/SBYTE 即此）。**并行路径模式**：给成熟函数加变体时，单独建 gate+recorder，原路径一字不动。**取已有机器的新用法**最便宜（§10.43 for-each、§10.59 min/max 皆零/少 codegen）。
- **str_replace 坑（本会话踩了多次，§10.60 撞车也与此有关）**：在注释/函数签名**之前**插代码，容易吃掉开头的 `/*` 或签名行。**插完务必核对下一处注释/签名是否完整并恢复**。函数若在定义前被用，要加前置声明。
- **live-stack 读取对"循环体内被覆盖的寄存器"是陷阱**（§10.42 math 表被 CALL 结果覆盖）——必须从稳定源（_ENV/上值/IR ref）重新派生。
- **正确性靠运行期守卫（GUARD_CFUNC 等），不靠录制期解析**——表/方法/函数被改即侧退出。
- **multiret 是嵌套调用的隐藏约定**（§10.59）：调用结果作另一调用尾参时，内层 `c==0`、外层 `b==0`。处理见 §10.59 的 `minmax_multiret_top`（只对下一 CALL 有效 + 尾参槽校验）。
- **交付格式**：始终**单一完整项目 tar 包**；present_files 前**务必从零解包复验**（解包→cmake→build→门槛）。字符串/floor 等带"恒失败守卫"的特性，复验须**带超时的挂起检查**（长串/越界路径）。
- VM 文件里的 file-static C 函数辨识器（spt_jit_*）**必须住在其所在库文件**（math_* 住 lmathlib，str_* 住 lstrlib），因为要看见 static 符号。

---

## 7. 一句话状态

**基线 = §10.68c 通用 resume-at-call 完成（嵌套内联多帧 3 阶段最后一步：带分支 callee 内联、exit stub 调用 sptjit_exit_resume 重建 callee CI、sptjit_hot_check 宏更新 ci/cl/k、安全网允许有 resume info 的 in-callee exit PC、proto_is_branch_inlinable 允许 forward conditional branches + 多 RETURN1、rec_cond_branch abort 放宽为仅对 method inline），全门槛绿（368 ctest、kernel 差分 135 pass/0 fail、模糊 400 例 0 失配）。下一步：带循环 callee 内联 / 嵌套方法内联。** 全部历史与教训在 `src/jit/JIT_DEV_NOTES.md`，路线在 `src/jit/JIT_ROADMAP.md`。
