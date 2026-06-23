# SPT Trace JIT 开发避坑经验

> 本文档记录开发 Trace JIT 过程中踩过的坑、关键设计决策、调试方法论，
> 供后续接手者（人类或 AI）参考，避免重复踩坑。

---

## 一、最致命的坑：x86-64 REX 前缀编码

### 坑点

`MOV r/m64, r64`（操作码 0x89）的 reg 字段是**源**，rm 字段是**目的**。
REX.R 扩展 reg（源），REX.B 扩展 rm（目的）。

**错误写法**（曾经导致 0xC0000005 崩溃）：
```c
void sptasm_mov_rr(SPTAsm *a, SPTReg dst, SPTReg src) {
  emit_rex_rm(a, 1, dst, src);  // 错！dst 当 reg，src 当 rm
  sptasm_byte(a, 0x89);
  emit_modrm(a, 3, src, dst);
}
```

`mov r12, rcx` 被编码成 `4C 89 CC`，实际解码为 `mov rsp, r9` —— **RSP 被覆盖**，后续任何栈操作立即崩溃。

### 正确写法

```c
void sptasm_mov_rr(SPTAsm *a, SPTReg dst, SPTReg src) {
  /* MOV r/m64, r64 (0x89): reg=src, rm=dst. REX.R 扩展 src, REX.B 扩展 dst. */
  emit_rex_rm(a, 1, src, dst);  // src 当 reg，dst 当 rm
  sptasm_byte(a, 0x89);
  emit_modrm(a, 3, src, dst);
}
```

### 教训

- **0x89 和 0x8B 的 reg/rm 语义相反**。0x89 是 `MOV r/m, r`（reg=源），0x8B 是 `MOV r, r/m`（reg=目的）。
- 每个新加的汇编指令，**必须用 hex dump + 手工解码验证**，不能假设编码正确。
- 崩溃在 prologue 的 `mov r12, rcx` 附近时，第一时间怀疑 REX 前缀。

---

## 二、Windows x64 栈对齐

### 规则

Windows x64 要求**函数调用前** RSP 16 字节对齐。
进入函数时（call 指令后）RSP = 8 mod 16（因为 call 压了 8 字节返回地址）。

### 本 JIT 的做法

prologue 压入 8 个 callee-saved 寄存器（64 字节），此时 RSP = 8 mod 16 + 64 = 8 mod 16。
然后 `sub rsp, frame_size`，frame_size 必须 = **8 mod 16** 才能让 RSP 回到 16 对齐。

```c
cg->frame_size = cg->shadow_space + cg->nspill_slots * 8;
cg->frame_size = (cg->frame_size + 15) & ~15;  // 先对齐到 16
cg->frame_size += 8;                            // 再 +8 使其 8 mod 16
```

### 教训

- 如果 trace 内部要调用 C 函数（如 `luaD_call`），**必须**保证调用时 RSP 16 对齐。
- 阴影空间（shadow space）32 字节不能省，Windows x64 调用约定要求。

---

## 三、SPT VM 的关键结构体布局（硬编码偏移）

JIT 代码生成中**硬编码**了以下偏移，改动 VM 结构体时必须同步更新：

```
TValue:        value_ (offset 0, 8字节) + tt_ (offset 8, 1字节) → 16字节
StackValue:    TValue val 是第一个字段 (offset 0)
CallInfo:      StkIdRel func 是第一个字段 (offset 0)
StkIdRel:      union { StkId p; ptrdiff_t offset; }
LClosure:      p 字段在 offset 24
Proto:         k 字段在 offset 56
```

### JIT 自定义调用约定

```
入口: RCX = lua_State *L, RDX = CallInfo *ci
内部: R12 = L, R13 = ci, RBX = base (ci->func.p + 1), R14 = k (Proto->k)
```

### 教训

- 用 `offsetof()` 而非魔法数字，但**仍需验证**编译后的偏移值。
- 改 `lobject.h` / `lstate.h` 结构体后，JIT 的 offset 宏会自动更新（因为用 offsetof），但 codegen 中的 `OFF_*` 宏要确认覆盖了所有访问点。

---

## 四、SPT FORLOOP 语义（与标准 Lua 不同）

### SPT 的 for 循环

```spt
for (int i = 0, 1000000) { ... }  // i 从 0 到 1000000，包含两端，共 1000001 次
```

**limit 是包含的（inclusive）！** 这与某些语言的 `range(0, 1000000)` 不同。

### 字节码布局

```
R[A]   = count（剩余迭代次数）
R[A+1] = step
R[A+2] = idx（当前索引）
```

### FORLOOP 执行流程

```c
if (count > 0) {
    count -= 1;
    idx += step;        // 先递增 idx
    store count, idx;
    jump back to body;  // body 看到的是递增后的 idx
}
```

### 教训

- 录制 trace 时，**第一次进入循环体的 idx 已经被 FORLOOP 递增过**。
- `for (int i = 0, 10)` 的 sum = 0+1+...+10 = 55，不是 45。
- 测试断言要基于 inclusive 语义写。

---

## 五、调试方法论

### 1. 二分法定位崩溃指令

JIT 崩溃时（尤其 0xC0000005），用**逐步恢复 prologue** 法：

1. 先只生成 `ret`，确认入口能跑
2. 加 push/pop，确认栈帧 OK
3. 加 `sub rsp, frame_size`，确认栈分配 OK
4. 逐条加 `mov r12, rcx` 等，哪条加完崩溃就是哪条的问题
5. 崩溃指令用 NOP 替换，确认是编码问题而非逻辑问题

### 2. Hex dump + 手工解码

```c
fprintf(stderr, "[JIT] Code dump (%zu bytes):\n", code_size);
for (size_t i = 0; i < code_size; i++) {
    fprintf(stderr, "%02X ", ((uint8_t*)result)[i]);
    if ((i + 1) % 16 == 0) fprintf(stderr, "\n");
}
```

对照 x86-64 手册解码每个字节，**不要信任自己写的编码器**。

### 3. 小循环验证 VM 语义

JIT 结果不对时，先跑一个**低于热点阈值**的小循环（< 60 次），看纯 VM 的结果：
```spt
int sum = 0;
for (int i = 0, 10) { sum = sum + i; }
print(sum);  // 55 → 确认 inclusive 语义
```

### 4. PowerShell 陷阱

- PowerShell **不支持** `&&`，用 `;` 代替
- PowerShell **不支持** heredoc `<<'EOF'`，commit message 写到临时文件用 `git commit -F`
- `2>$null` 抑制 stderr，`> out.txt 2> err.txt` 分流
- `print()` 输出到 stdout，JIT 调试信息输出到 stderr，两者可能需要分开查看

---

## 六、当前架构的关键设计决策

### 1. 为什么用 spill slot 而非寄存器分配（当前阶段）

- 简单正确优先：每个 IR ref 一个 spill slot，运算时 load 到 scratch 寄存器
- **这是性能瓶颈**，后续必须实现线性扫描寄存器分配
- 但作为第一步，spill slot 方案保证了正确性，是合理的起点

### 2. Guard + Snapshot 机制

- trace 录制时对**类型**和**值范围**做假设，发射 guard IR
- guard 失败时，按 snapshot 恢复解释器栈状态，跳回解释器
- snapshot 记录每个活栈槽对应的 IR ref（即值的来源）
- exit stub 负责把 IR ref 的当前值写回栈槽 + 设置 savedpc

### 3. 热点检测位置

在 `lvm.c` 的 **FORLOOP / TFORLOOP** 后向跳转处检测：
```c
if (sptjit_hot_check(ci, pc - GETARG_Bx(i))) { vmbreak; }
```

`vmbreak` 表示 JIT 接管了执行，解释器直接跳出当前 dispatch。

> **大括号是必须的，不能省。** 本仓库 `vmbreak` 定义为 `break`（switch dispatch），
> 即使写成 `if (cond) vmbreak;` 也能编译。但上游 Lua 的 computed-goto dispatch 把
> `vmbreak` 定义成**多语句**宏（`vmfetch(); vmdispatch(GET_OPCODE(i));`）。那种情况下
> 不加大括号，`if (cond) vmbreak;` 会展开成
> `if (cond) vmfetch(); vmdispatch(...);` —— 只有 `vmfetch()` 受条件控制，
> `vmdispatch` **无条件**执行，JIT 进入逻辑会被悄悄破坏。所以四个检测点
> （pc+sj 的 JMP、FORLOOP×2、TFORLOOP）一律用 `{ vmbreak; }`。

### 4. 可执行内存管理

- 预分配 1MB 可执行内存池（`code_buf`）
- 每个 trace 从池中分配，16 字节对齐
- `sptasm_finalize` 应用重定位后设为 RX 权限

---

## 七、已知限制清单（接手者必读）

| 限制 | 位置 | 后续方案 |
|------|------|----------|
| 无寄存器分配 | `spt_jit_codegen.c` spill_off() | 线性扫描分配器 |
| 不支持 CALL/TAILCALL | `spt_jit.c` record_inst() case OP_CALL | 发射 SPTIR_CALL，codegen 调 luaD_call |
| 不支持 TFORLOOP | 同上 | 录制迭代器 guard + 状态追踪 |
| 不支持 CONCAT | 同上 | 调用 VM concat 路径 |
| 不支持 NEWTABLE/NEWLIST | 同上 | 发射分配 IR |
| 无 trace stitching | `spt_jit.c` sptjit_trace_enter() | 侧出口计数 + 录制新 trace + 链接 |
| 无逃逸分析 | IR 优化 pass | 标记不逃逸的分配，消除 |
| PHI 节点未充分使用 | `spt_jit_ir.c` sptir_loop() | 循环变量应生成 phi |

---

## 八、文件导览

```
src/jit/
├── spt_jit.h              公共 API + 配置常量（热点阈值、最大 trace 长度等）
├── spt_jit_internal.h     内部结构：SPTTrace, SPTJitState, SPTHotEntry
├── spt_jit.c              核心：热点检测、trace 录制、trace 入口/出口
├── spt_jit_ir.h           SSA IR 定义：opcode、类型、snapshot
├── spt_jit_ir.c           IR builder + 优化（常量折叠/CSE/DCE/phi）
├── spt_jit_asm.h          x86-64 汇编器 API
├── spt_jit_asm.c          汇编器实现（编码器 + 可执行内存管理）
└── spt_jit_codegen.c      IR → x86-64 代码生成
```

### 关键函数入口

- `sptjit_trace_hot()` — 解释器每轮循环调用，热点检测 + trace 入口
- `record_trace()` — 录制 trace（解释执行一遍，建 IR）
- `record_inst()` — 逐条字节码 → IR（switch on opcode）
- `sptir_optimize()` — IR 优化 pass
- `sptjit_codegen_compile()` — IR → 机器码
- `gen_inst()` — 逐条 IR → 机器码（switch on IR opcode）
- `gen_exit_stub()` — 生成 guard 失败的恢复代码

---

## 九、给接手者的建议

1. **先跑通现有测试**：`ctest --test-dir build -C Release -j 4`，确认 239 项全过
2. **改 codegen 前先加 hex dump**：临时加回调试输出，改完验证再删
3. **新加 IR opcode 的流程**：ir.h 加枚举 → ir.c 处理 → codegen.c gen_inst() 加 case → 录制器 record_inst() 加 case
4. **新加字节码录制的流程**：record_inst() 加 case → 确认 IR opcode 已有 codegen 支持
5. **性能优化前先保证正确性**：JIT 结果必须与解释器逐位一致
6. **Windows x64 调用约定**：RCX/RDX/R8/R9 传参，32 字节阴影空间，RSP 16 对齐
7. **不要假设结构体偏移**：用 `offsetof()`，改 VM 结构体后重新验证

---

## 十、开发计划（六阶段）

详见对话中的规划，核心路径：
1. **寄存器分配**（线性扫描）— 性能地基
2. **函数调用支持** — 覆盖面最大提升
3. **for-each + 表操作** — SPT 常用模式
4. **Trace stitching** — 不规则控制流
5. **高级优化**（LICM/逃逸分析/强度削减）
6. **硬化与性能验证**（200+ 测试 + 差分测试 + ASan）

---

## 九、本轮工作：正确性修复 + 线性扫描寄存器分配器

### 9.1 修复的八个 miscompilation bug（全部经差分测试验证）

1. **UNM/BNOT/LEN 误吞下一条指令**：这三个一元运算后面**没有** MMBIN 回退指令
   （回退在 lvm.c 中内联），但记录器盲目 `pc++` 跳过下一条，把真正的指令吞掉了。
   加 `rec_skip_mmbin()`：仅当 `pc[1]` 确实是 MMBIN/MMBINI/MMBINK 时才前进。
2. **移位语义**：Lua 把 `x<<k` 编码成 `OP_SHRI by -k`，`luaV_shiftl` 是逻辑移位，
   `|移位量|>=64 → 0`。加 `rec_shift_const()` 正确处理常量移位；变量移位量用
   `GUARD_ULT` 保证落在 [0,63]（x86 会把移位量按 6 位取模，与 Lua 不一致）。
3. **比较 guard 极性反了**：记录器静态地走"跳过 JMP"（cond≠k）那条路，所以 guard
   应当是在该路成立的关系。九个比较 opcode 全部修正。新增 `SPTIR_NE`。
4. **常量折叠把 guard 折没了**：折叠 guard 会丢掉它的 side-exit 但快照还在 → 错位。
   在 try_const_fold 顶部加 `if (flags & GUARD) return 0;`。
5. **exit stub 把未触及的槽位写成 nil**：`reg_map[slot]==-1` 表示"trace 没碰过这个槽，
   解释器栈上的值仍然有效"，**不是** "写 nil"（真正的 nil 用 SPTIR_NIL ref）。
   改成 `continue`（保持栈上原值）。这个 bug 会破坏循环累加器。
6. **嵌套循环陷阱**：记录外层循环时撞到内层循环的回边，把 trace 在内层回边截断，
   生成退化 trace。FORLOOP 现在计算 `target = pc+1-Bx`，只有 `target==start_pc`
   才闭合循环，否则 `aborted=1`（保留内层 trace，外层留给解释器）。
7. **前向 JMP 把 if/else 两条臂都记录了**：前向 JMP 是无条件跳转（条件已被前面的
   比较变成 guard），必须**跟随**跳转而非 fall-through。改成 `pc += sj+1; return 1;`。
   只记录实际执行的那条臂，另一条由比较 guard 侧出。
8. **`/`（浮点除法）被记成整数除法**：SPT/Lua 里 `/`（OP_DIV）和 `^`（OP_POW）**永远**
   产生浮点，即使两个操作数都是整数（只有 `//` 和 `%` 在双整型时保持整数）。`rec_arith`
   原来对双整型一律走整数运算，于是 `i / 2` 被记成 `(int)(i/2)` 再 TOFLT，丢掉小数部分
   （3/2 算成 1.0 而非 1.5）。修复：`rec_arith` 顶部特判 DIV/POW，强制两操作数提升为
   float、结果类型 FLT，对齐 `luaV_div`。这个 bug 是 jit_bench.sh 的新用例 `s = s + i/2`
   才暴露的——原有 kernel 没覆盖"整数操作数的浮点除法"，提醒**基准用例要覆盖类型边界**。

### 9.2 寄存器分配器（headline 性能工作）

**核心思想**：把循环携带的标量变量常驻寄存器，整个循环期间不落栈。
- **驻留判定**：凡是有整型 live-in SLOAD 的槽 → 分配一个 GPR；有浮点 SLOAD 的槽
  → 分配一个 XMM。`ra_analyze()` 收集，超出寄存器池就整体回退到"全溢出"老路径
  （已验证正确），保证安全。
- **GPR 池**：`{R15,RSI,RDI,R8,R9,R10,R11}`（prologue 已保存或 leaf 不需保存；
  注意 RSI/RDI/R15 在 Win64 是 callee-saved，prologue 确实 push 了它们，所以两 ABI 都安全）；
  RAX/RCX/RDX 保留作 load/移位/除法/取模的 scratch。**XMM 池**：SysV 用 `{XMM4-7}`
  （全部 caller-saved，leaf trace 可自由用），**Win64 只用 `{XMM4,XMM5}`**——因为
  XMM6-15 在 Win64 是 callee-saved 而 prologue 没保存它们，用了就违反调用约定
  （潜伏 bug：只有当编译器恰好把活跃浮点值放在 XMM6/7 跨越 trace 调用时才显形）。
  见 `XMM_POOL` 的 `#if defined(_WIN32)`。XMM0-3 作 scratch。
- **前导块 (preheader)**：把 live-in 的 SLOAD + 类型 guard 提升到循环标签**之前**，
  只在进入时执行一次。循环体内这些指令被跳过（`ref_hoist[]`）。类型在循环内不会变
  （只跑整型/浮点运算），所以 guard 一次足矣。
- **回边不再写栈**：值在寄存器里跨回边流动，LOOP 仅 `jmp loop_label`。
- **侧出口 flush（关键正确性点）**：因为不写栈，驻留槽的栈拷贝是陈旧的。每个 exit
  stub **先**把所有驻留寄存器刷回栈（GPR→INT tag，XMM→FLT tag），**再**应用快照
  （覆盖多步更新的精确中间值）。寄存器在 guard 点持有的正是该槽此刻的值。
- **guard↔快照配对**：原来靠发射顺序 `nexits++` 配对，提升 guard 到 preheader 会打乱
  顺序。改为在 guard 指令里存 `snap_idx`（复用 `pad` 字段），exit stub 按快照索引生成。
- **二地址合并 (two-address)**：`s = s + i` 这类 `x = x OP y`（结果寄存器==第一操作数
  寄存器）直接 `add Rs, Ri` 一条指令搞定，而非 load/load/op/store 四条。可交换运算
  还能吃 `x = y + x`。浮点同理 `addsd Xs, Xi`。
- **FORLOOP guard 特化**：`GUARD_LT(0, count)` → `test count,count; jle exit`（2 条），
  省掉常量物化和多余 cmp。
- **死存储消除**：整型/指针常量和 nil/true/false 由 gen_load 按需重新物化，所以它们
  的每次迭代 spill 存储是死的，仅当绑定到寄存器时才存。

### 9.3 结果

- **正确性**：16 个 JIT kernel 全过；228 个测试文件 JIT 开/关输出逐字节一致
  （唯一"差异"是 b4_yield_return.spt 打印的堆地址，关 JIT 也每次不同，非 bug）；
  230 个 ctest 全过。
- **性能**（200M 迭代级循环，解释器 vs JIT，min-of-N）：
  - 整型 for 循环：~14×（循环体仅 6 条指令：`add / test / jle / sub / add / jmp`）
  - 整型 while 循环：~15×
  - 浮点循环：~7.6×
  - 分支循环 (if/else)：~1.2×（**已知弱点**，见下）

### 9.4 已知限制 / 后续工作

- **分支循环慢**：trace 只记录一条臂，另一条每次迭代侧出到解释器，进出开销主导。
  解决需 **if-conversion**（把简单 diamond 变成 cmov 无分支）或 **side trace**
  （为另一条臂生成挂在 exit 上的侧 trace）。两者都是较大的新机制。
- RA 在遇到 POW（libm 调用）、表操作、upvalue、CALL、CONCAT、浮点比较时整体回退
  （这些可能调用 C 破坏 caller-saved 寄存器，或 codegen 路径仍是整型/溢出版）。
- XMM 池 4 个、GPR 池 7 个；超出则回退。多步更新的中间值仍溢出（仅 SLOAD 和最终
  值绑定寄存器）。
- Win64 ABI：XMM6-15 是 callee-saved 而 prologue 没保存，所以 **Win64 下 XMM 池已收窄为
  `{XMM4,XMM5}`**（见 `XMM_POOL` 的 `#if`），消除了 ABI 违规。代价：Win64 上 3+ 个浮点
  变量的循环拿不到浮点寄存器驻留（回退到溢出路径，仍正确）。若要在 Win64 恢复 4 个浮点
  寄存器，需在 prologue/epilogue 里 save/restore XMM6/7——本仓库未做，因为无法在 SysV
  主机上验证那段栈帧改动。

## 十、中止黑名单（abort blacklist）与数组循环现状

### 10.1 中止黑名单（已实现）
**问题**：含不可 trace 操作的循环（CALL、泛型 for、数组访问目前也是）每次变热都重新录制、
中止、丢弃。`SPTHotEntry.counter` 中止后清零，循环再次变热又重试，无限 churn。实测一个数组
求和循环 **JIT 开比关慢 2.8×**（0.70s vs 0.25s）——JIT 反而拖慢了。
**修复**：`SPTHotEntry` 加 `aborts` 计数；`record_trace` 返回 NULL 时 `e->aborts++`；
`sptjit_trace_hot` 里若 `e->aborts >= SPT_JIT_MAX_ABORTS`（=8）直接 return，不再计数不再录制。
实测中止数从 50000 降到 16，数组循环回到 0.91×（与解释器持平，churn 消除）。标量循环不受影响
（它们能编译，永远不进黑名单），仍 14×+。

### 10.2 数组循环 JIT —— 读写均已实现（本轮完成）
数组迭代是真实代码里最常见的热循环。之前整条路从未真正跑过（`OP_GETTABLE/GETI` 把结果记成
`SPTT_ANY`，下游算术中止）。本轮做完了**读和写**：

**读 (`GETI`)**：
- 录制器读运行时元素类型给结果定型（`rec_array_elem_type`），下游算术不再中止。
- 补数组类型 guard（`rec_load_reg` 自带）+ 越界 guard（`0 ≤ idx < loglen`，用 LEN + GUARD_LE/LT）。
- `GETI` 变成"带 guard 的 load"：codegen 先 load 元素 tag、和期望类型比较、不匹配则侧出
  （新增 `movzx`/`[base+index]` 手工编码），再按 `[array-(k+1)*8]` load 值。
- `GETI`/`LEN` 标为 RA-safe（纯内联，只用 RAX/RCX/RDX scratch）；`ra_analyze` 改为
  **跳过只读的非数值 live-in**（数组指针留在栈上每轮读），数值计数器/累加器照常驻留寄存器。
  → 数组读循环 **2.5–3×**（含双数组点积 `a[i]*b[i]`）。

**写 (`SETI`)**：
- 录制器补越界 guard（只 JIT 越界内的写；追加/扩容则侧出到解释器）。
- **修了一个潜伏 bug**：原 SETI codegen 的 tag store REX 写错（`0x44` 少了 REX.B），
  `mov byte [R9], R10b` 实际编码成 `mov byte [RCX], R10b`——往索引值当地址乱写，会损坏内存。
  从未跑过所以没暴露。
- **重写为 RA-safe**：原版用 R8 当 scratch（R8 在 RA 池里），所以含 SETI 的 trace 只能关 RA。
  改成只用 RAX/RCX/RDX + SIB 寻址：tag store `mov byte [RAX+RCX*1+4], imm`
  (`C6 44 08 04 tag`)、value store `mov [RAX+RCX*1-8], RDX`（先 `imul RCX,8; neg RCX`，
  再 `48 89 54 08 F8`）。SIB=0x08(scale1,index=RCX,base=RAX)，逐字节用反汇编核对过。
  标为 RA-safe 后写循环拿到寄存器驻留 → **2.0–2.3×**（之前无 RA 是 1.3–1.8×）。

### 10.3 SSE2 前缀/REX 顺序 bug —— 已修（本轮发现）
反汇编浮点循环发现 `cvtsi2sd xmm0, eax`——是 **eax(32 位)不是 rax(64 位)**！原因：所有 SSE2
helper（`sse2_op_rr`/`cvtsi2sd`/`cvtsd2si`/`movsd*`/`ucomisd`）把 REX 字节发在强制前缀
(F2/66) **之前**。按 x86 规则，REX 必须紧贴 opcode、在 legacy 前缀之后，否则 REX 被忽略。
于是 `cvtsi2sd` 的 REX.W 被吞掉 → 整数→浮点转换被截断成 32 位。

之所以一直没炸：转换源永远是 RAX（低寄存器，不需要 REX.B），且测试里的整数都 < 2³¹。
对 > ±2³¹ 的整数（如 50 亿）就会错。修法：把前缀字节发在 REX 之前。对低寄存器/W=0 的算术
（addsd 等）是 no-op（那些情况本来就不发 REX），对 cvt 则正确启用 REX.W。已用 50 亿整数→浮点
测试验证修复，全差分回归通过。

### 10.4 分支录制走静态直落 → 改为跟随运行时实际分支（本轮，重大修复）
反汇编一个 1% 偏置分支 `if(i>99990){稀有}else{常见}` 发现 JIT 比解释器还慢（**0.6×**），
`entries=30M`（每次迭代都进出 trace）。根因：`record_trace` 从循环头**静态走字节码**，比较
指令（OP_LT/LE/EQ/EQI/LTI/LEI/GTI/GEI/EQK）后一律 `rc->pc++` 跳过 JMP，录的是**直落（THEN）块**，
guard 用静态 k flag。于是偏向 ELSE 的循环录成了稀有的 THEN 路径，guard 几乎每轮都失败 → 每轮侧出。

修法：新增 `rec_cond_branch`——录制时用栈上运行时值算出比较结果，决定到底是直落还是跳转：
- 直落成立 → guard 直落条件 `fop`，`pc = jmp+1`（跳过比较+JMP）。
- 跳转成立 → guard 取反 `negate(fop)`，`pc = jmp+1+sJ`（跟随 JMP 到目标块）。
9 个比较 handler 全改成调用它。效果（全部结果正确）：

| 形态 | 修前 | 修后 |
|---|---|---|
| 偏置分支(then 稀有) | 0.6× | **13.3×** |
| 偏置分支(then 常见) | — | **15.8×** |
| 数组 max `if(v[i]>m)m=v[i]` | — | **2.4×** |
| 50/50 分支 | 1.5× | ~1.3×（本质如此） |
| always-taken | 13.8× | 13.5× |

50/50 分支没法靠这个修（两条路真各占一半，无论录哪条都有一半侧出）——需要 side-trace 链接
（LuaJIT 的做法）才能根治，属于后续大件。验证：9 种分支形态（eq/neq/float/le/ge/双变量/
min/clamp/嵌套 if）差分全过，全量差分 227/228 + 16 kernel + 230 ctest 全绿。

**布尔真值分支 `OP_TEST`/`OP_TESTSET`（本轮续修，已彻底解决）**：同样的静态直落问题让
偏置布尔分支 `if(flag)` 约 0.5×。修复关键有两点：
1. **方向**：录制时算出真值（`!l_isfalse`）跟随实际分支；真值由 `GUARD_T` 保证——
   `rec_value_type` 对 true/false/nil 返回**不同**类型（SPTT_TRUE/FALSE/NIL），所以
   `rec_load_reg` 发的类型 guard 已经把 tag（进而真值）钉死，旧的 `test value` 真值 guard
   （bool 的 value 字段没用、测的是垃圾）可直接去掉。
2. **取值要从 IR 而非栈**：`bool b=(i<99990)` 里 slot 被复用（先 LOADK 存 99990，再被
   LFALSESKIP/LOADTRUE 覆写成 b）。录制器读的是**录制起点的栈快照**，复用 slot 上是上一轮的
   b、不是 99990 → 比较方向算错 → 物化的 b 和 TEST 的方向对不上 → **结果错**。

最终的取值策略（`rec_pred_num` + `rec_ref_truthy`）：
- 比较分支用**混合预测**：操作数是 KINT/KFLT 常量 → 用 IR 里的常量值（复用 slot 安全）；
  否则读该操作数**字节码寄存器**的栈值（loop 变量是当前值；上一轮算出的计算值是个不错的预测）。
  预测只要是好启发式即可——正确性由 guard 保证，录哪条都不会错。
- TEST/TESTSET 的真值从 **IR ref** 取（物化结果是 TRUE/FALSE 常量，或 bool 的 SLOAD），
  绝不读复用 slot 的过期栈值。

效果（全部正确）：`if(flag)` 偏置 0.5×→**21×**，`bool b=(i<C); if(b)` 复用 slot 案例
0.7×(且曾错)→**21×**，数组 min/max `if(v[i]<>m)` **2.5–3.1×**，计算型比较 `if(i*2<C)` 正确，
and/or 短路正确。完整回归：16 kernel + 230 ctest + 全量差分 227/228 全绿。

### 10.5 字符串相等分派 `if(s == "literal")`（本轮新增能力）
对全测试集统计录制中止的字节码，发现真实代码里最大的缺口是**函数调用**（OP_CALL/OP_SELF），
其次是字符串操作（OP_EQK/OP_CONCAT）、OP_NEWLIST、嵌套循环。调用内联需要在每次 guard 侧出时
重建虚拟调用帧（让解释器能从调用中途恢复），是个对侧出正确性极度敏感的大件，本轮不碰。

转而做了**字符串相等**这个自包含、低风险、能开启字符串分派循环（解析器/状态机）的能力：
- 关键正确性洞察：`s == "短字面量"` 用**指针比较**是精确的——短字符串（≤40 字节）在 Lua 里
  全部 intern（去重），相等的短串共享同一个对象；而长串永远不可能等于短常量（长度不同→不同对象）。
  所以只要常量是短串，指针相等 ⟺ 内容相等，无论运行时 s 是短串还是长串都成立。
- 实现：`OP_EQK` 里检查常量是 `ttisshrstring`，是则走 `rec_str_cond_branch`：guard 是指针 EQ/NE
  （op1=s 的 SLOAD 取 value 字段=TString*；op2=KSTR 取常量 TString*；复用整数比较 codegen 的 cmp）。
  方向预测用录制时两个串指针比较。
- codegen 几乎零改动：`gen_load` 对 KSTR 是 `mov_ri64` 装指针、对 SLOAD 装 value 字段（字符串
  的 value 就是 TString* 地址，和 KSTR 存的一致），整数 cmp 直接就是指针比较。
- RA 只把 INT/FLT slot 提进寄存器、STR slot 留在 spill，所以没有"驻留字符串被按 INT tag 刷回栈"
  的隐患。

**有意的限制**（保证安全）：
- `OP_EQ`（两个字符串**变量**比较）一律 abort——两者都可能是长串，指针比较不安全。
- 只支持 EQ/NE；字符串大小比较（`<`/`<=`）需要按内容比，不做。
- 字符串**数组元素**读取（`list<str> w; w[i]`）仍 abort（GETI 只处理 INT/FLT 元素），与本特性无关。

效果（全部正确）：`if(cmd=="go")` 偏置分派 **8.7×**，多路 `if(op=="add")…` **9.8×**。
完整回归：16 kernel + 230 ctest + 全量差分 227/228 全绿。

### 10.6 字符串数组读取 `list<str> w; w[i]`（本轮新增，与 10.5 组合）
退出/快照机制其实**早就支持任意类型**——`spt_type_to_tag` 覆盖 STR/ARR/TAB/FUNC，且 RA 只把
INT/FLT slot 提进寄存器（STR slot 留在栈，刷回时不会被错打成 INT tag）。GETI 的 codegen 也是
**类型无关**的：tag guard 用 `spt_type_to_tag(inst->type)`、值加载就是搬 8 字节（字符串的值就是
TString\*）。所以"读字符串数组元素"唯一的拦路是 recorder 里 `et 必须是 INT/FLT` 这一条。

改动极小：GETI/GETTABLE 的元素类型限制放宽到也允许 SPTT_STR（**只放读，不放写**——写 GC 值要
GC write barrier，是另一回事）。于是 `w[i]` 能加载成 TString\*，再喂给 10.5 的字符串相等，
打通**字符串数组分派/计数/过滤**这类常见模式（`for(...) if(names[i]=="foo")`）。

效果（全部正确）：偏置匹配 `if(w[i]=="a")`（多数命中）**2.8×**，和数值数组读取的 2.7× 持平。
50/50 分派（元素一半命中一半不命中）受限于线性 trace 的 50/50 本质问题（数值字符串都一样），
约 1.0×——需要 side-trace 才能根治。完整回归：16 kernel + 230 ctest + 全量差分 227/228 全绿。

### 10.7 嵌套数组读取 `list<list<int>>`（本轮新增，仅显式形式）
把 GETI/GETTABLE 的可读元素类型再放宽到 SPTT_ARR：数组元素是内层数组时也能加载（加载到的内层
Table\* 直接当作内层 GETI 的数组操作数）。于是**显式二维**能 JIT：
```
list<int> row = m[i];   // 把内层数组取到一个稳定 slot
for(j) s += row[j];     // 内层循环就是个 1D 求和
```
效果：显式 `row[j]` **1.9×**，全部正确。

**有意不支持链式 `m[i][j]`**：试过用"沿 IR ref 递归求值数组操作数"让它录制（rec_eval_array），
结果它确实能编译、也正确，但**比解释器还慢（0.6–0.8×）**——内层数组 m[i] 在内循环里是不变量，
却每轮都重算（两次带界/类型 guard 的 GETI + 两次 LEN），且 entries≈总迭代数（没在内部成环）。
与其编译一个慢 trace，不如让它干净地 abort（回落解释器，0.8×）。把它做快需要 **LICM 提升不变
量 GETI**（把 m[i] 提到循环外，内层就退化成 1D），属于后续。已撤掉递归求值、保留 rec_array_elem_type
读栈槽的原逻辑：显式形式（row 是稳定 SLOAD 槽）照常工作，链式形式（中间槽被复用、栈值过期）
返回 ANY → abort。

完整回归：16 kernel + 230 ctest + 全量差分 227/228 全绿。

### 10.8 两个 miscompilation bug（CSE / NOP-alias）+ 不变量 GETI 提升（本轮）

本轮主线是两个**正确性 bug**（都不在原测试集覆盖范围内，差分测试此前没碰到），外加一个干净的性能优化。

#### bug A —— CSE 把别名指向了错误的 ref（公共子表达式被算错）

`try_cse` 找到更早的相同指令（下标 `i`）后，把当前指令改成 NOP，但**没有把 op1 改成 `i`**，
而是保留了原来的第一个操作数。于是 `y = a*b` 的 NOP 别名指向了 `a`（op1=SLOAD a），而不是
`a*b` 的乘法结果。任何被实际使用的 CSE 结果都会被算错：

```
int a=4; int b=6; int s=0;
for(i){ int x=a*b; int y=a*b; s=s+x-y; }   // x-y 恒为 0，结果应为 0
```
post-opt IR（修复前）：
```
  5  int MUL  op1=1 op2=3      ; x = a*b
  6  int NOP  op1=1            ; y -> 错误地别名到 SLOAD a（应为 5）
 10  int SUB  op1=9 op2=1      ; (s+x) - a  ← 减成了 a，不是 a*b
```
解释器给 0，JIT 给 999999220（每轮 +20 = +(a*b - a)）。**修复**：`try_cse` 命中后写
`ir->op1 = i;`（别名指向相同的那条指令 = CSE 结果）。一行修复。

#### bug B —— 寄存器驻留下给 NOP-alias 绑了寄存器（x*1 / x+0 / x-0 + CSE）

代数化简把 `x*1`/`x+0`/`x-0` 折成 `SPTIR_NOP(op1=x)`，CSE 同样产出 NOP-alias。`gen_load`
对 NOP 会转发到 op1（正确），**但** RA 的两地址合并会把寄存器绑到 NOP 本身
（`ref_reg[reg_map[slot]]`）；NOP 在 codegen 里被跳过（不发射任何代码），所以那个驻留寄存器
**永远拿不到计算结果**，一直保留入口处的 live-in 旧值。

```
int s=1; for(i){ s=s+i; s=s*1; }   // s 应累加 i
```
解释器给 5000000050000001，JIT 给 781。`s*2`、`s*3` 正确（不是 NOP）。

**修复（两层互相加固）**：
1. `forward_nops`（spt_jit_ir.c，CSE 与 DCE 之间的新 pass）：把所有指令的 op1/op2、
   GUARD_LT/LE 的 aux 界 ref、以及 `reg_map[slot]` 全部沿 NOP 链转发到 canonical ref。
   之后 NOP 没有任何活引用（DCE 清掉），codegen/RA 再也见不到 NOP 当 final-ref。
   快照是录制期（优化前）拍的，单独由 `gen_load` 的 NOP 转发兜底。
2. `ra_canon_ref`（spt_jit_codegen.c，两个合并点：int 槽和 float 槽）：在
   `ref_reg/ref_xmm[final_ref]` 赋值前先沿 NOP 链找到真正算值的指令。给 NOP 绑寄存器
   永远是错的（它不发码），所以追链永远安全。

修复后 `s*1 / s+0 / s-0`、`x*1` 用两次、公共子表达式（a+b、a*b、整型/浮点、用在索引里、
三次 CSE）全部 off==on。

#### 不变量 GETI 提升（LICM 性能优化，干净的赢）

`ra_hoist_invariants` 现在会把**常数下标**的不变量数组元素读取（如 `base[0]`）连同它的
`GUARD_LT` 一起提到 preheader：
- GETI 不变 ⇔ `!has_seti && 数组不变 && 下标是 KINT 常数`。`has_seti`：循环里只要有任何
  SETI 就不提任何数组 LOAD（写可能与读别名，保守）。in-bounds 的 SETI 不改长度，所以 LEN /
  界 guard 仍安全。
- GUARD_LT 只在 `op1 是 KINT 常数 && 界（aux ref）不变` 时提升 —— 这正好排除了 FORLOOP 的
  计数 guard（界=活计数器，每轮变）和变量下标的界 guard。

效果：`s += arr[j] + base[0]` 把 `base[0]` 提出循环 → **3.2×**（原 2.5×）。`base[2]` 2.3×。
数组求和/写入/求最值不受影响（2.3–2.5×）。

**为什么限定常数下标**：变量下标的不变量 GETI（如 `b[k]`，k 是循环外不变量）一旦提升，
trace 就**不在内部成环**了（entries 变成逐迭代，0.7× 倒退）；常数下标（`base[0]`）提升后照常
成环（entries=外层数）。所以只取常数下标这个安全子集。别名安全（写-别名-读、覆盖 elem0）
全部验证正确。

#### 已解决：嵌套循环里"数组载入值相乘"不成环 —— 两个叠加的 bug（用 valgrind 定位）

这个长期谜题（点积 `s+=a[j]*b[j]`、多项式 `s+=a[j]*a[j]` 等不内部成环、0.7× 比解释器还慢）
最终查清是**两个独立 bug 叠加**，且第一个把第二个**掩盖**了。关键工具是 valgrind memcheck——
它能标记 JIT 生成代码里"条件跳转依赖未初始化值"。

**现象与误导**：编译出来的循环体与能成环的 ADD 版本**逐指令同构**（同寄存器分配、同 5 个 guard、
同无条件 back-edge），唯一区别是 `imul` vs `add`；标量 `i*i` 照常成环。`trace_exits`、
`trace_guard_fail` 两个统计**从未被自增**，"exits=0"完全不携带信息。还出现过"观察者效应"：
在热路径里加一句 `fprintf` 居然让它成环了——这正是"读到未初始化栈值、fprintf 改了栈垃圾"的信号。

**Bug 1（DCE 不跟随 guard 的 aux）**：`GUARD_LT/GUARD_LE` 的**界**（比如数组 LEN）存在 `aux`
里、是一个 IR ref，不在 op1/op2。而 `dce()` 和 `mark_used()` 标记 guard 存活时只递归 op1/op2，
**不看 aux** → LEN 被当死代码消除、根本不发射 → 边界 guard 读一块**从没写过的 spill**。
valgrind 直接报 13961 次（=trace 进入次数）"Conditional jump depends on uninitialised value,
created by a stack allocation"，偏移正是边界 guard `cmp rax, [rsp+0x28]; jge` 那条。ADD 和 MUL
**都有**这个 bug，只是那块栈垃圾恰好能过 ADD 的 guard、过不了 MUL 的（也解释了 fprintf 的观察者效应）。
- 修复：`mark_used` 与 dce 根循环里，对 `GUARD_LT/GUARD_LE` 额外 `mark_used(aux)`。
  （spt_jit_ir.c）

**Bug 2（loglen 当 8 字节读，被 Bug 1 掩盖）**：修了 Bug 1 后 valgrind 干净了、`a[j]*b[j]` 成环了，
但 `a[j]*a[j]`、`a[j]*b[k]` 变成**跨进程非确定**（entries 在 3M↔21M 之间跳，关 ASLR 后变确定）。
valgrind 这次 0 报错——因为读到的是"已定义但错"的值。用退出桩里加的**非扰动**per-snapshot 计数器
（写全局、不碰栈）定位到每次都从 snapshot 3 = 第一个边界 guard `GUARD_LT(j, LEN)` 退出。再看
preheader 反汇编：LEN 的发射是 `mov rax, [array+0x14]`（**64 位**load），但 `Table.loglen` 是
`unsigned int`（**4 字节**，偏移 0x14），紧邻的 0x18 是 `t->array` 指针——于是
`LEN = loglen | (data_ptr_low32 << 32)`，高 32 位是堆地址（ASLR）。边界比较是**有符号** `jge`，
所以 LEN 的符号取决于指针 bit31 → 时正时负 → 时而 j<LEN 过、时而不过。
- 修复：新增 `sptasm_mov_rm32`（W=0 的 `mov r32, [mem]`，x86-64 下自动零扩展到 64 位），LEN
  codegen 改用它读 loglen。（spt_jit_asm.c / spt_jit_codegen.c）

**结果**：两个修复后，**所有**数组算术 trace 都确定性成环（10/10 跑 entries=外层数），valgrind 全 0：
点积 `a[j]*b[j]` **2.8×**、`a[j]*b[k]` 2.8×、多项式 `a[j]*a[j]` 2.8×（之前都是 0.7× 比解释器慢）、
数组求和 2.4×、数组写 2.3×。这是本类问题的根治。

教训：(1) "trace 行为诡异/非确定"先上 valgrind——未初始化栈读会显示成"条件跳转依赖未初始化值"；
但 **valgrind 干净 ≠ 正确**，"已定义但错/ASLR 相关"它抓不到。(2) 退出桩里加写全局的计数器是**非扰动**
诊断（不碰栈、不在热路径加 C 调用），能定位"从哪个 snapshot 退出"。(3) 任何"4 字节字段当 8 字节读"
都会拖进相邻字段，若相邻是指针就变成 ASLR 相关的偶发 bug——读窄字段必须用窄 load。

链式 `m[i][j]` **本轮也修好了**：DCE/loglen 两个修复让它能成环后，暴露出最后一个 miscompile——
常量外层下标 `m[0][j]` 里 `m[0]` 是个**产出数组的常量下标 GETI**，被 LICM 提升后结果错（变量下标
`m[i][j]` 因为 `m[i]` 本就不被常量下标提升、每次内层迭代重算，所以一直正确）。
- 修复：在 GETI 提升条件里排除 `SPTT_ARR/SPTT_TAB` 元素类型（spt_jit_codegen.c），即**不提升产出
  数组的 GETI**——让 `m[0]` 像 `m[i]` 一样在循环体里重算（标量 `base[0]` 提升不受影响，仍 2.7×）。
- 同时重新启用沿 IR ref 递归求值数组操作数（rec_eval_array / rec_eval_int，spt_jit.c），让链式
  `m[i][j]` 能录制（中间数组的栈槽被复用、读不到，但在 IR 里能递归解出）。

验证：valgrind 全 0；off/on 差分在 6 种链式形态（矩阵求和、`m[0][j]*m[0][j]`、`m[i][j]+m[i][j]`、
浮点矩阵、`m[0][j]-1`、两个二维数组 `a[i][j]*b[i][j]`）全部一致。性能：`m[0][j]` 2.5×、矩阵求和
1.8×、矩阵×向量 1.6×、`m[1][j]` 1.2×（都快于解释器）。230 ctest + 16 kernel + 全量差分全绿。

完整回归：16 kernel + 230 ctest + 全量差分 227/228 全绿（那个 1 是 b4_yield_return 打印不确定
堆地址，JIT 关掉也不一致，非 bug）；valgrind 在标量/浮点/数组读写/字符串数组/分支多类 kernel 上全 0。

### 10.9 LEN/GETI/常量 CSE + 冗余守卫去重（本轮新增优化）

**动机**：`a[i]*a[i]`（平方）、`(a[i]-m)*(a[i]-m)`（方差/标准差）、多项式求值等"同一元素多次读取"
的模式，每次迭代都会发出重复的数组读取（GETI）、长度读取（LEN）和边界守卫。原 CSE 只对纯算术
（ADD/SUB/MUL/位运算）生效。

**实现**（spt_jit_ir.c）：
1. `try_cse` 扩展：
   - **KINT**：整数常量按值（aux）去重，让被多次发出的字面量（如两条下界守卫的 `0`）共享同一 ref，
     从而把守卫暴露成完全相同，便于后续去重。匹配键加入 aux。
   - **LEN**：trace 内 loglen 恒定（append 类操作会中止 trace，在界内 SETI 不改变 loglen），可安全 CSE。
   - **GETI**：纯元素读取，仅当 trace 内**无任何数组/表写入**（保守：has_table_write 扫描 SETI/SETFIELD）
     时才 CSE，避免别名写覆盖。CSE 后清掉别名 NOP 上的 GUARD|SNAP 标志（首个 GETI 已保证类型）。
2. 新增 `guard_dedup(b)` pass（在 forward_nops 之后、dce 之前；此时所有 bound/操作数 ref 已规范化）：
   对 GUARD_T/LE/LT/EQ/ULT，若前面已有 live 的相同 (op,op1,op2,aux) 守卫，把后者标 DEAD
   （gen_inst 跳过 DEAD；不发出代码的守卫不会生成 exit stub，snap_idx 是逐指令的 → 无 exit 错位）。

**效果**：`a[j]*a[j]` 的循环体从（2×GETI + 2×LEN + 4×守卫 + MUL）塌缩为
（1×GETI + 1×LEN + 2×守卫 + MUL(x,x)）。受控 A/B（全开 vs 全关，同主机消噪）：
`a[j]*a[j]` 快 ~11.5%，方差 `(a[i]-m)^2` 快 ~7.2%。注意：单纯去掉冗余 GETI 只有 ~1%
（冗余读取命中 L1、乱序执行下近乎免费），收益主要来自去掉冗余**守卫/分支**（利好前端/分支预测）。

**安全性**：GETI-CSE 受 has_table_write 门控（带 `a[j]=...;` 写入的循环差分验证正确、CSE 正确禁用）；
guard_dedup 标 DEAD 不发码、不生成 exit stub，snap_idx 逐指令映射故无错位。验证：9 种重复元素/对照
模式 off==on 全一致、valgrind 0、230 ctest + 16 kernel + 全量差分 227/228 全绿。

### 10.10 真正的退出计数（本轮，阶段 0）

整个项目期间 `trace_exits`/`trace_guard_fail` 从未递增，退出行为完全不可见（调试时多次受此所困）。
本轮把它做实：
- `SPTTrace` 加 `uint64_t exit_count[SPT_JIT_MAX_SNAPSHOTS]`（calloc 清零）。
- `gen_exit_stub` 顶部（放完 label、flush 之前）插 4 条指令自增 `&t->exit_count[snap_idx]`：
  `mov rcx,&cnt; mov rax,[rcx]; add rax,1; mov [rcx],rax`。退出落点处 RAX/RCX 是死的 scratch
  （守卫的临时值已无用，活值在常驻寄存器/RBX 相对槽里），故安全；冷路径零热路径开销。
- `sptjit_destroy` 按 hot_table 聚合每 trace 每快照计数 → `stats.trace_exits`；DEBUG≥2 打印
  每条 trace 每退出点 `[snap{s}->pc{offset}:{count}]`。

**`entries==exits` 恒等**是正确性自检（每次进入恰好一次退出）。实测：标量整循环 entries=1/exits=1；
数组求和内层 trace entries=exits=外层数（每外层重入一次、集中在循环结束快照）；50/50 分支退出
集中在分支守卫快照——侧 trace（阶段 2）的挂载点定位数据已就绪。验证：230 ctest + 16 kernel +
全量差分全绿、valgrind 0。`guard_fail`（侧退出，扣除循环结束）与负收益加黑推迟，见 ROADMAP 阶段 0.5。

### 10.11 差分模糊测试 + 两个真实 bug（本轮）

把 JIT 模式正确性验证从临时 bash 差分升级为**持久化的覆盖套件 + 差分模糊器**：
- `test/15_jit/kernels/` 新增 ~40 个 kernel（数组读写/长度/多数组、CSE 重复元素、链式二维、
  分支变体、布尔、字符串、循环形态、负数 mod/idiv、变量移位、有符号位运算、溢出等），
  全部经 `jit_difftest.sh` 差分 + 编译检查（no-jit=0，即每个都真的编译出 trace）。
- `scripts/jit_difftest_all.sh`（持久化全量差分，原来只在 /tmp）。
- `scripts/jit_fuzz.py`（差分模糊器）：14 个模板生成器随机化形状（数组大小/下标/算子/循环界/
  嵌套/负操作数），JIT off vs on 比对，固定 seed 可复现。生成程序保证下标不越界、无除零、
  类型一致、确定性。

**模糊器当场抓到两个真实 bug：**
1. **整数 mod/idiv 截断 vs 向下取整**：x86 `idiv` 给的是截断余数/商（符号随被除数），但 Lua 的
   `%`/`~/` 是向下取整（符号随除数）。`(-2)%1000` 解释器=998、JIT=−2。修复（codegen MOD/IDIV）：
   补向下取整修正 `if (r!=0 && (r^n)<0){ MOD: r+=n; IDIV: q-=1 }`（r!=0 时 sign(r)==sign(m)，
   故 (r^n)<0 等价 (m^n)<0）。用一个本地前向分支实现，RAX 在两路都是结果、RCX/RDX 死，RA 安全。
   覆盖 MODK/IDIVK（同 IR op）。
2. **浮点 mod/idiv 产生垃圾**：MOD/IDIV codegen 只处理 INT，浮点 `%`/`~/` 落到未初始化结果槽
   → 垃圾（7.5%2.0 解释器=1.5、JIT=4.67e-310）却照常编译。浮点 mod/idiv 需 libm(fmod/floor)+
   符号修正、不常见，故在 recorder 的 `rec_arith` 里**非双 INT 的 MOD/IDIV 一律中止**，回退到
   （正确的）解释器。

修复后：模糊器 14 生成器 × 8 seed × 200 = 1600 例 0 mismatch；负数 mod/idiv、变量移位（含 ≥64
的 Lua 饱和语义）、有符号位运算、溢出回绕全部 off==on。270 ctest + 56 kernel + 全量差分全绿、
valgrind 0。教训：**固定 kernel 漏掉的边界（负操作数、类型组合）正是差分模糊器的强项**；
**某 op 的 codegen 只处理部分类型会静默产出垃圾**——recorder 必须对不支持的类型组合中止。

### 10.12 调用内联（纯直线叶函数）—— 本轮 headline

让"热循环体里调用函数"这一常见模式能进 JIT。此前循环体一遇 OP_CALL 直接 abort，
整个循环退回解释器。现在对**纯直线叶函数**（pure straight-line leaf）做内联：

**为什么限定纯直线叶函数 —— 这让实现大幅简化：**
- 被调函数体只有寄存器搬移 / 常量加载 / 标量算术位运算 + 一个 RETURN1，没有分支/循环/
  调用/表访问/upvalue。**这样的被调体录制时不产生任何 guard**（操作数类型从调用方已知），
  于是 callee 内部没有快照、没有 side exit、不需要重建 callee 帧；callee 的临时槽在返回后即死。
- 关键简化：函数身份检查**只在 trace 入口用 C 做一次**，而非每次迭代发 IR guard
  （避免引入函数指针的 IR/codegen 机制）。

**实现要点：**
1. **frame_base 重基**（先做、验证字节级不变作为地基）：SPTRecCtx 加 `int frame_base`（根=0），
   把 spt_jit.c 里所有 `ir->reg_map[X]`/`ir->reg_type[X]` 改成 `[rc->frame_base + X]`（sed，86 处，
   rc 在三个函数中均在作用域）。maxslot 保持 raw（callee 临时槽是死的、不进快照，惟一进快照的结果
   槽在调用方槽号范围内、由调用方使用时覆盖）。快照逻辑在 IR 模块、读绝对槽，天然一致、无需改。
   rec_load_reg 加门：frame_base!=0 且槽不在 reg_map 里就 abort（不能去读根栈的错位置）。
2. **proto_is_inlinable**：扫描 callee 字节码，只允许 MOVE/LOADI/F/K/KX/FALSE/TRUE/NIL/算术/位运算/
   MMBIN*/EXTRAARG + 恰好一个 RETURN1（捕获返回寄存器）；**允许 RETURN1 之后的尾随死 RETURN0/RETURN**
   （编译器样板，永不被录制，因为 RETURN1 会把录制重定向回调用方）。非定长参/有嵌套 proto/体过大 → 拒。
3. **OP_CALL 处理**（从 abort 组里拆出）：要求 C==2（单返回值）、定长实参、depth-1。通过
   `reg_map[A]` 必须是 SLOAD（函数来自稳定栈槽 = 循环不变）解析 callee 闭包，peek 冻结栈得 proto。
   `(B-1) == numparams`（**SPT 的 numparams 含 slot-0 receiver**，sq(int x) 的 numparams=2）。
   过 proto_is_inlinable。bounds：`new_fb + maxstacksize < 256`。整条 trace 只记一个入口检查
   (slot, proto)，所有内联调用必须共享之，否则 abort。然后保存调用方 ctx、把 rc 切到 callee
   (p/k/cl=callee, pc=callee code, frame_base = A+1)，继续录制。**实参无需拷贝**：callee 槽 k =
   调用方槽 A+1+k（frame_base=A+1），实参已在 reg_map 对应绝对槽里。
4. **OP_RETURN1**（frame_base!=0 时）：把调用方结果寄存器 reg_map 绑到返回值 ref，恢复调用方 ctx，
   继续录制 CALL 之后。frame_base==0 时维持原样（发 RETURN、结束 trace）。RETURN0/RETURN 在
   frame_base!=0 时 abort（纯直线叶只会 RETURN1）。
5. **入口检查**（sptjit_trace_enter，C 侧）：若 trace->inline_fn_slot>=0，进入前校验该栈槽仍是
   inline_fn_proto 的闭包，否则不进、回退解释器（函数被重新赋值等情况安全兜底）。

**bring-up 抓到的两个坑：** (a) 实参数检查应是 `B-1 == numparams` 而非 numparams+1
（numparams 已含 receiver）；(b) 纯度扫描必须放行 RETURN1 后的尾随死 RETURN0。

**结果：** `function sq(int x){return x*x;} for(i,5e7){ s += sq(i); }` → recorded=1 compiled=1
entries=1 exits=1，结果一致，**0.032s vs 1.202s = 37×**（调用开销全消、循环体变成纯内联 i*i）。
验证：多参 add2、用两次 lin、常量返回 k、callee 内 mod 均内联且一致；分支型 absx、有副作用
bump（数组写）、自递归 f、同循环两不同函数 → 正确 abort 回退、结果一致。270 ctest + 63 kernel +
全量差分 269 + 模糊器 15 生成器（新增 gen_inline_call）6 seed×250 全绿、valgrind 0 error 0 leak。

**尚未覆盖（留作后续增量）：** 带分支/循环的 callee、C/内置函数、多返回值、SELF 方法调用、
嵌套内联（callee 再调函数）、参数数与形参不等（Lua 补 nil）。这些目前都安全 abort 回退。

### 10.13 修复：循环携带值"置换"在寄存器驻留路径下算错（迭代 fib bug）

**症状**：迭代版 fib（`t=a+b; a=b; b=t`）JIT 结果错误且随 HOT 阈值变化（说明 trace 本身把
循环体算错了，而非边界问题）。顶层复现（无函数调用，与调用内联无关），且 pre-call-inlining
备份同样有此 bug —— **是既有的核心 bug，非本轮引入**。

**根因**：寄存器驻留路径（ra_analyze，use_ra）对每个循环携带槽，把它的 live-in SLOAD 和它的
循环末值都绑到同一个寄存器，并假定末值在循环体里被算进该寄存器、跨 back-edge 直接流动（不发
任何 back-edge move）。但 `a=b`（MOVE 在 IR 里不产生指令、只改 reg_map 引用）使 a 的末值 =
b 的 **live-in SLOAD**（因为录制到 `a=b` 时 b 尚未被 `b=t` 重写）。于是同一条 SLOAD 既是 b 的
home 寄存器、又是 a 的末值 → 被绑到两个不同寄存器，最后一个赢；驻留路径无法表达 back-edge 上
`a←b, b←t` 这个**并行拷贝**，a 永远拿不到 b 的值。溢出兜底路径（use_ra=0）每次迭代从栈重读
每个槽、底部回写，天然正确。

**修复**（spt_jit_codegen.c ra_analyze）：寄存器绑定改两趟——先把各槽 live-in SLOAD 绑到各自
寄存器，再绑各槽末值；若某个末值已被绑到**另一个**寄存器（置换/别名，如 swap、或两槽末值相同），
说明需要 back-edge 并行拷贝，驻留路径表达不了 → 直接 `return`（use_ra=0）回退到正确的溢出路径。
int 和 float 两套都加。保守：宁可回退到较慢但正确的路径。

**验证**：fib(90)、swap、rotate3 修复后 interp==jit 且与 HOT 无关；累加器 `s+=i*i` 仍 13.9×
（RA 未过度回退）；新增 gen_swap 模糊生成器（fib/2-swap/3-rotate）5 seed×250 全绿 + swap_fib/
rotate3 kernel；277 ctest + 65 kernel + 全量差分 274 + valgrind 0。教训：**驻留路径的"无 back-edge
move、值直接流动"假设在循环携带值发生置换时失效**；检测到需要并行拷贝就回退溢出路径最稳。

### 10.14 修复（同类第二例）：循环携带值"陈旧别名"在驻留路径下算错

修完 §10.13 的置换后，继续探测循环携带值这一类，又抓到一例：`b = a; a = a + 1` JIT 结果偏差
（b 拿到自增**后**的 a，而非自增前）。根因同源但 §10.13 的检测漏了它：b 从不被读取、没有
live-in SLOAD，故不在 islots 里，置换冲突检测（只遍历 islots）看不到它。b 的末值 = a 的 SLOAD
（a 驻留且被原地自增的寄存器）；side exit 时快照按该寄存器读 b，读到的是 a 的**最终**值而非
拷贝时刻的值。

**修复**（ra_analyze 再加一道、更根本的检测）：遍历所有槽 0..maxslot，若某槽末值（canon 后）是
**另一个**槽的 live-in SLOAD，且那个源槽被改写（其末值 != 该 SLOAD），则 `return` 回退溢出路径。
（源槽只读时其寄存器不被覆盖，安全、不误判。）计算型拷贝（`b=a*2; a=a+1`，b 末值是 MUL 而非裸
SLOAD）每次迭代在循环体重算、天然正确，不在此列、不回退。

**验证**：b=a;a=a+1、c=b;b=a;a+=i、纯 swap、2/3/4-rotate、累加器+swap 混合、计算型拷贝全部
interp==jit；累加器仍 ~12×（RA 未过度回退）；新增 gen_copy_carry 模糊生成器，gen_swap+
gen_copy_carry 8 seed×250=2000 例全绿 + copy_carry kernel；279 ctest + 66 kernel + valgrind 0。
**这一类的通用判据**：驻留路径"无 back-edge move、SLOAD 寄存器原地更新"假设，在出现
(a) 循环携带值置换 或 (b) 某槽末值是另一个被改写槽的裸 SLOAD 时失效；两种都检测并回退溢出路径。

### 10.15 修复：for 内嵌 while 死循环（嵌套循环 back-edge）+ 重试相位偏移

用户报告 `for` 里套 `while` 在 JIT 下挂起（while 单独跑正常，for 套 for 也正常因为 FORLOOP 单独
处理）。复现 6 个最小例（纯变量 lo/hi 递增递减、数组读、数组写、读写+临时、读写无临时全挂；
while 单独+数组 OK）。

**根因**：`OP_JMP` 处理器对**任何**后向跳转都 `sptir_loop()` 收口，不检查跳转目标。录制外层 for
（start_pc=for 体首）时撞到内层 while 的后向跳转（目标=while 头 offset 8，≠ start_pc offset 6），
错误地在那里收口。于是 `lo=0; hi=20` 初始化被折进了循环体 → lo/hi 成 KINT 常量 → 退出 guard
`LT(0,20)` 恒真 → trace 无限循环（IR dump 里那条 KINT 0/KINT 20/LT 恒真的 trace 即元凶）。

**修复 1**（spt_jit.c OP_JMP）：后向跳转只在 `target == rc->start_pc` 时收口；否则是内层循环的
back-edge（单条线性 trace 无法表达嵌套循环），置 `aborted=1` 中止。这样 while 自己的 trace
（start_pc=while 头）后向跳转目标==start_pc 正常收口；for 的 trace 撞到内层 while 后向跳转则中止
回退解释器。for 套 for 不受影响（内层 FORLOOP 单独处理，本来就 OK）。

**修复 2**（spt_jit.c sptjit_trace_hot，相位偏移）：修复 1 后内层 while 在某些阈值下不编译
（aborted 多、compiled=0）。原因：while 每个外层迭代跑 N 次，录制在 while 后向跳转触发；若阈值是
N 的整数倍，命中的恰好是**退出那一次迭代**（lo>=hi），录到的是退出路径（一路到外层 FORLOOP）而非
循环体 → 中止；counter 重置为 0 后重新累计同样的相位，每次重试都落在退出迭代 → 永不编译（默认/
20/40 都中招，8/12 凑巧落在循环体所以能编译）。修复：中止后把 `e->counter = e->aborts`（而非 0），
每次重试提前若干次触发，采样不同迭代；一个 trip 里只有一次是退出迭代，故一两次重试即收敛到循环体。
correctness 中性（只改"何时重试录制"，不改语义），对真正不可 trace 的循环重试次数不变（仍 ≤
MAX_ABORTS=8 次）。

**验证**：6 个 repro 全部 exit=0 + 匹配解释器；内层 while 在 默认/20/40 阈值都 compiled=1
（entries≈外层迭代数）；for+while perf 0.253→0.076s ≈ 3.3×；for 套 for 仍匹配 + 内层 for 编译；
新增 gen_for_while 模糊生成器（纯变量/读/写/读写+临时 4 变体）+ 3 个 forwhile kernel；280 ctest +
69 kernel + 277 差分（0 失配 0 超时）+ 8 seed×250=2000 模糊例（含 for+while，每例硬超时防挂）+
valgrind 0。**教训**：单条线性 trace 无法表达嵌套循环——后向跳转必须验证目标==start_pc 才收口，
否则把内层循环的 pre-header 折进体里会造成常量退出 guard → 挂起；录制相位与内层 trip count 对齐
时会一直录到退出迭代，中止后需偏移相位重试。

### 10.16 修复：常量折叠器的逻辑移位 + floored 整除/取模语义错误

主动探 SPT 特有运算符语义时发现常量折叠器（spt_jit_ir.c try_const_fold）两处与 VM 不符。触发条件：
两个操作数都是常量。注意循环体内 `int x = -8; ... (x >> 1)` 里 x 虽是源码变量，但录制器看到
`LOADI -8` 会把 x 当 KINT 常量，于是 `x >> 1` 被折叠——折叠器比 SPT 编译器更激进，所以这类
"循环不变常量参与的运算"会走折叠器。

**Bug 1 逻辑移位**：`case SPTIR_SHR: r = x >> (y & 63)` 用的是**有符号** `>>`（算术移位、符号扩展），
但 SPT 的 `>>` 是**逻辑移位**（零填充）。`-8 >> 1`：解释器给 9223372036854775804（逻辑），折叠器
给 -4（算术）。且 `& 63` 对移位量 ≥64 是回绕而非饱和到 0，负移位量也没按"反方向移位"处理。
（注：当时"运算数计算得来"的对照例凑巧匹配，是因为迭代数恰是 4 的倍数、逻辑移位的高位在 mod 2^64
下相互抵消，与算术结果重合——是假阴性。）

**Bug 2 floored 整除/取模**：`case SPTIR_IDIV: r = x / y` / `SPTIR_MOD: r = x % y` 用 C 的**截断**
`/` `%`，但 SPT 是**floored**（商向负无穷取整、余数取除数符号）。`-7 ~/ 2`：解释器 -4，折叠器 -3；
`-7 % 2`：解释器 1，折叠器 -1。codegen 的运行时路径之前已修过 floored 校正，但折叠器自己用 C 运算
重算了一遍，漏了。

**修复**：照搬 VM 的 `luaV_shiftl`（逻辑移位 + |量|≥64 饱和 + 负量反向；SHR(x,y)==shiftl(x,-y)）和
`luaV_idiv`/`luaV_mod`（floored 校正 `if((m^n)<0 && m%n!=0) q-=1` / `if(r!=0 && (r^n)<0) r+=n`，
并特判 n==-1 避免 INT64_MIN/-1 溢出 UB），用内联小函数实现（不耦合 VM 头文件，全程无符号算术避免
UB）。顺带把 ADD/SUB/MUL 折叠改成无符号回绕，与 VM 的 intop 一致、消除有符号溢出 UB（结果不变）。

**验证**：逻辑移位（负值/≥64 饱和/负移位量/大字面量）+ floored 整除取模（各符号组合）全部
interp==jit；正值运算不受影响；溢出回绕正确；283 ctest + 71 kernel（新增 const_shr_neg、
const_idiv_mod_neg）+ 280 差分（0 失配）+ 新增 gen_const_fold 后 10 seed×250=2500 模糊例 0 失配 +
valgrind 0。**教训**：常量折叠器必须逐个运算符匹配 VM 语义——逻辑 vs 算术移位、floored vs 截断
整除取模、移位量饱和都是 SPT/Lua 与 C 原生运算符的差异点；折叠器比源码编译器更激进（会折叠
循环不变的 LOADI 常量），所以这些差异在 JIT 里更容易暴露。

### 10.17 修复：浮点操作数的位运算（录制器漏查类型，对 IEEE 位模式做整数运算）

按路图 §四点五继续探浮点边界，发现 `^`（SPT 里是按位 XOR，不是幂）等位运算在浮点操作数下出错。
`float b = 2.0; (b ^ 10.0)`：解释器把浮点转成整数再 XOR（`2 XOR 10 = 8`，非整数浮点如 `1.001^2.0`
直接报错 exit=255），但 JIT 对两个 double 的**原始 64 位 IEEE 位模式**做整数 XOR
（`0x4000... ^ 0x4024... = 0x0024...`≈1.013e16，累加成 1.013e22）。

**根因**：位运算处理器（OP_BAND/BOR/BXOR/SHL/SHR 及 K/SHLI/SHRI 变体）加载操作数后直接以
`SPTT_INT` 发 IR，**从不检查操作数真实类型**。浮点操作数的 ref 指向浮点值，位运算 codegen 按整数
处理其寄存器内容（即位模式）→ 垃圾。（注：之前 float MOD/IDIV 已加了 both-int 检查并 abort，但位
运算这几个处理器是另一条独立路径，漏了同样的检查。）

**修复**：在全部四个位运算处理器里加 both-int 检查——任一操作数非 SPTT_INT 就 `aborted=1` 回退
解释器（解释器正确处理浮点→整数转换含报错语义，JIT 不复刻）。整数位运算不受影响（reg_type 为
SPTT_INT，照常编译）。

**验证**：float `^`/`&`/`|`/`<<`/`>>`（值或移位量为浮点、两侧皆浮点、混在表达式里）全部 abort
且 interp==jit；整数位运算仍 compiled=1 且匹配（无误杀）；285 ctest + 71 kernel + 282 差分（0 失配）
+ 新增 gen_float_bitwise 后 10 seed×250=2500 模糊例 0 失配 + valgrind 0。**教训**：同一语义检查
（操作数类型、floored 校正等）散落在多个 opcode 处理器里时，**每条路径都要单独加**——位运算和
算术（rec_arith）是两条路径，别只改一条。SPT 的 `^` 是 XOR 不是幂（OP_BXOR，非 OP_POW），位运算
要求整数操作数、浮点会转整数（含报错）。

### 10.18 修复：循环内变量类型突变导致挂起（提升到 preheader 的类型守卫假设类型跨重入不变）

探类型稳定性时发现 **JIT 挂起**：`auto acc = 0; for(...){ if(i==N){ acc=acc+0.5; } acc=acc+1; }`——
int 累加器经罕见分支永久变成 float。解释器正确（如 1000001.5），JIT 挂死。

**隔离**：需要(1)每轮都参与的循环携带累加器,(2)经分支**永久**从 int 变 float。关键非对称：
`float→int` 不挂(`acc=acc+1.0` 又把 acc 变回 float,下一轮守卫照过)；`int→float` 挂
(`acc=acc+0.5` 后 `acc=acc+1` 保持 float,int trace 的守卫此后**每次重入都失败**)。

**根因**：RA 把循环携带的 int 累加器常驻 GPR,其 GUARD_T(读栈上类型 tag)被提升到 preheader
(§10.13/codegen 注释"the value stays int thereafter"——此假设只在单次 trace 执行内成立)。acc
永久变 float 后,每次从解释器重入 trace,preheader 的 GUARD_T 立刻失败退出,**但该退出没有推进
循环**(i 不前进)→ 活锁/挂起。

**修复**:在 `sptjit_trace_enter`(C,进入 trace 前,类比已有的内联调用 proto 检查)加**入口期
live-in 类型重校验**:遍历 trace->ir 的 GUARD_T,对其守卫的 SLOAD 取槽位,若当前栈值
`rec_value_type(s2v(ci->func.p+1+slot))` 与 GUARD_T 记录的类型不符,**拒绝进入**(return 0),让
解释器跑这一轮(从而推进循环)。类型稳定的 trace 照常进入,无影响。

**致命细节(踩了一次)**:GUARD_T 的 aux 存的是 **SPTType 枚举**(`(int64_t)t`, t=rec_value_type(v)),
**不是 Lua 类型 tag**。第一版误用 `ttypetag()` 比较,只有 int 因 `SPTT_INT==LUA_VNUMINT==3` 巧合
通过,其余(数组 SPTT_ARR=6 vs LUA_VARRAY=9、float 等)全被**静默拒绝**→ 编译了却 entries=0,数组
循环掉到 0.5×(比解释器还慢),但因为"拒绝=回退解释器=结果仍对"所以差分仍 MATCH,极隐蔽。改用
`rec_value_type(...) != (SPTType)gi->aux` 后修复:数组恢复 5×、标量 14×、float 正常编译。

**性能**:入口扫描 O(ninst)/次。绝大多数循环 entries=1(trace 内部自循环),扫描仅一次,可忽略;
即便嵌套循环内层 trace entries=5001,仍保持 9.8×——扫描被实际循环工作完全淹没,无需预计算。

**验证**:挂起 repro 全族 MATCH(int→float、float→int→float、多次突变、周期性翻转、其他槽变型);
286 ctest + 72 kernel(新增 type_transition_acc,编译且匹配,no-jit=0)+ 283 差分 + 新增
gen_type_transition 后 10 seed×250=2500 模糊例 0 失配 + valgrind 0。**教训**:提升到 preheader 的
类型守卫隐含"值类型跨重入不变"的假设,被循环中段永久类型突变打破→活锁;入口期 C 重校验拒绝失配
trace 是正解。**GUARD_T aux 是 SPTType 不是 Lua tag**,跨枚举比较会静默退化成"永不命中"的性能悬崖。

### 10.19 修复：对字符串取长度 `#s` 读成数组长度字段（返回垃圾）

探"JIT 会编译但没系统验证"的区(循环变体、比较运算符、数组边界、字符串)时,发现 `#s`(字符串
长度)在热循环里算出垃圾。`string s="hello"; for(...){ t=t+#s; }`:解释器=5000005(5×迭代数),
JIT=**100**(=5×20,即编译前那 20 轮的值——编译后 trace 把 #s 算成 0,t 不再累加);换 `s="hi"`
时 JIT=**2201381698**(纯垃圾,把字符串对象内存按数组长度字段读)。

**根因**:`OP_LEN` 处理器加载操作数后直接发 `SPTIR_LEN`,**不检查操作数类型**。`SPTIR_LEN` 的
codegen 读 `Table->loglen`(数组长度字段),对字符串而言这个偏移是别的字段→垃圾。录制器其实给 s
发了 `GUARD_T(SPTT_STR)` 钉住它是字符串,但 LEN 无视类型、一律按数组语义。(注:数组边界检查处
[867/898/920/937 行]也发 SPTIR_LEN,但那些操作数是已守卫为数组的下标对象,正确;只有用户级 `#x`
的 OP_LEN 可能是字符串。)

**修复**:`OP_LEN` 操作数非 `SPTT_ARR` 就 abort 回退解释器(字符串字节长度——短串/长串还存在不同
字段——、map 大小等都由解释器正确处理)。数组 `#v` 照常编译(控制用例 compiled=1 且匹配,含
`v[i % #v]` 这种把 #v 当下标模数的常见写法)。

**验证**:字符串 `#s`(原 100/垃圾)→ abort 且 MATCH(5000005、200002);字符串数组元素 `#w[i%3]`
→ abort 且匹配;数组 `#v`、`v[i%#v]` 仍 compiled=1 且匹配;286 ctest + 72 kernel + 283 差分 + 新增
gen_string 后 10 seed×250=2500 模糊例 0 失配 + valgrind 0。**教训**:延续位运算/类型那几个 bug 的
同一主题——**发某个带类型语义的 IR op 前必须校验操作数类型与 codegen 的假设一致**。`SPTIR_LEN`
的语义是"数组长度",套到字符串上就是读错字段。**已知遗留(非 bug,纯漏优化)**:`int n=#v;
v[i%n]+n` 这种把数组长度存进变量再当值用的写法目前会 abort(不是本次修改引入,数组 LEN 本身能编),
将来可放宽。

### 10.20 修复：对浮点按位取反 `~f` 读成整数补码（系统审计找到的同类第三例）

`#s` 那个 bug 后,我把所有"发带类型语义 IR 但可能不校验操作数类型"的一元/特殊 opcode 处理器系统
审了一遍,抓到 `OP_BNOT`:`~f`(f 为浮点)算出垃圾。`float f=5.0; int x=~f`:解释器 `~5=-6`,
JIT=`-7747317258985095845`(对 5.0 的 IEEE 位 `0x4014...` 做整数补码)。

**根因**:`OP_BNOT` 加载操作数后直接发 `SPTIR_BNOT` + 硬编码 `SPTT_INT`,**不查操作数类型**——
与位运算(§10.17)、`SPTIR_LEN`(§10.19)同一模式。`~` 整数专用,浮点应转整数(含报错),JIT 却对
浮点位模式做整数补码。

**修复**:`OP_BNOT` 操作数非 `SPTT_INT` 就 abort(同其他位运算)。顺手给 `OP_UNM` 加防御:操作数非
int/float 就 abort(`SPTIR_NEG` 只能负 int/float 寄存器,对字符串/数组会按整数负→垃圾,且 VM 本就
报错)。

**审计结论(这一类基本收口)**:
- `OP_BNOT` —— 修复(本节)。
- `OP_UNM` —— 传播操作数类型给 `SPTIR_NEG`,int/float 正确;加了非数值防御 abort。
- `OP_NOT` —— 真值语义,int/bool 均正确(`!int` 恒假、`!bool` 取反),无类型混淆。
- `OP_GETI`/`OP_SETI`/`OP_GETTABLE`/`OP_GETTABUP`(数组下标,常量与变量索引) —— **已正确**:都查
  `bt==SPTT_ARR`、元素类型限 int/float/str/array、并发 0<=idx<len 边界守卫。
- `OP_GETFIELD`/`OP_SETFIELD`(map 字段) —— abort(安全)。

**验证**:`~f`(原垃圾,三种浮点)→ abort 且 MATCH(-6000006 等);`~int`、UNM int/float 仍 compiled=1
且匹配;286 ctest + 72 kernel + 283 差分 + gen_float_bitwise 扩入 `~f` 后 8 seed×250=2000 模糊例
0 失配 + valgrind 0。**教训(第三次重申同一条)**:发任何带类型语义的 IR op 前,核对"这个 op 隐含
什么操作数类型 / 录制器此刻知道的实际类型"——位运算和 `~` 假设整数、`SPTIR_LEN` 假设数组、
`SPTIR_NEG` 假设数值。这次用**系统审计**(枚举所有此类 handler 逐个查)代替随机探测,把这一类一次
性扫干净,比一个一个撞更可靠。

### 10.21 能力改进(非 bug):变量下标数组访问 + 同循环内 `#v` 当值用，不再误判 abort

§10.19 留的遗留项。`for(...){ s = s + v[i%5] + #v; }` 这种"数组元素访问 + #v 作为值"的写法会 abort
(compiled=0,纯漏优化,结果一直正确)。浮点版 `list<float>` 同样 abort。

**根因**:`OP_GETTABLE`(变量下标)的元素类型**预测**读下标槽的栈值 `idxtv`,但单遍录制在回边
(迭代末)读它。当编译器把下标寄存器复用给累加器 `s` 时,迭代末该槽是 `s` 的值:整型累加器→越界
大数(`rec_array_elem_type` 返回 SPTT_ANY → abort);浮点累加器→栈上是 float(`ttisinteger(idxtv)`
失败 → abort)。**注意这不是正确性 bug**——trace 里真正的下标是 IR 值 `cref`(已 0<=cref<len 边界
守卫),GETI 加载也有类型守卫;只是"预测"用错了槽。

**修复(安全,靠运行时守卫兜底)**:(1)`rec_array_elem_type` 预测下标越界时回退到元素 0(空数组仍
返回 SPTT_ANY);(2)`OP_GETTABLE` 的 `idxtv` 非整数时,预测下标取 0 而非 abort。元素类型若预测错,
运行时 GETI 类型守卫会侧退出——正确性由边界+类型守卫保证,不依赖这个预测。同质 list(list<int>/
list<float>,常见情形)预测必中、无额外退出;异质数组(罕见)会多退出但结果正确。`OP_GETI`/
`OP_GETTABUP`(常量下标)无 stale 问题;`OP_SETTABLE`(写)不预测元素类型,均无需改。

**验证**:`v[i%5]+#v`、`int n=#v; v[i%n]+n`、`v[i%5]` 后 `+#v`、float 元素版、读写+长度混合——原 abort,
现全部 compiled=1 且 MATCH;普通数组读、float 元素读、字符串元素(`#` 触发 §10.19 的 string-LEN
abort)等控制用例不变;287 ctest + 73 kernel(新增 array_len_mix)+ 283 差分 + 新增 gen_array_len_mix
后多 seed 模糊 0 失配 + valgrind 0。**意义**:这是本轮第一个**能力/性能**改进而非正确性修复——关键
在于识别出"abort 的根因只是预测启发式失准,而正确性已由运行时守卫独立保证",于是可以安全放宽,让
一类常见写法重新进入编译路径,而不牺牲任何正确性。

### 10.22 严重性能问题诊断:含分支的热循环比解释器慢(录制方向"抛硬币");附 §10.18 入口检查回归修复

本轮最重要的发现。**含 `if` 的热循环 JIT 后比解释器慢**(2000 万次迭代,best-of-3):
偏置 90/10 `if(a<9)` = 0.31–0.35×;50/50 = 0.58–0.61×;33/33/33 = 0.54×。全部
`entries≈exits≈迭代数`——trace **不在内部循环**,而是每迭代被重新进入+退出一次。简单标量循环
(`s=s+i*i`)、数组循环 `entries=1`(正常内循环);**任何循环内 `if` 守卫都会破坏内循环**
(有无 else 都一样,与 else/前向 JMP 无关)。这些循环都是 use_ra=1。

**根因——精确定位(本轮关键洞察)**:codegen 的循环接线是对的(loop_label 已放置、LOOP 跳回、
比较守卫 cc 正确,均已逐一核验)。bug 在**录制**。逐快照退出统计(DEBUG≥2)显示某偏置坏 trace 的
全部退出集中在**一个**快照 `[snap1->pc8:1799983]`,pc8 即 `if(a<9)` 的 LTI,而 1.8M = a<9 的
**多数路径**计数。DEBUG=3 录制轨迹显示该 trace **没有录到 ADDI**(循环体被跳过),守卫 IR 是
**GE**(不是 LT):录制器录了 **a>=9 的少数路径**,于是其守卫"a>=9 才继续、a<9 就退出"在**多数
路径(a<9)每迭代都退出** → 灾难。**`rec_cond_branch` 录制的是录制那一刻分支恰好走的方向——一次
非确定的抛硬币。** HOT 敏感性扫描证实:HOT=20/30 → entries=1.8M(录到少数,坏);
HOT=21/25/28/29 → entries=200K(录到多数,好,只在少数路径退出)。所以一个热循环 trace 的质量
**取决于阈值命中那一刻分支变量的取值**;偏置分支若被录在其少数路径上,就变成永久 ~3–10× 减速。
**先前总结里"偏置分支 13×"是一次幸运录制的实例。** 这正是用户指令要避免的非确定行为。

**两个独立问题:**

1. **§10.18 入口类型检查是本轮自引入的回归**:实测偏置分支 0.35×(带)vs 0.61×(禁用)。它在
`sptjit_trace_enter` 里**每次进入都全扫 tir->ir**(O(ninst)),在 1800 万次进入上成本巨大。
**已修复(本轮已发布,安全)**:录制完成后(`sptjit_trace_record` 内 IR 定稿处)把"需要重校的
live-in"预计算成紧凑表 `livein_slot[]/livein_type[]`(按槽去重,上限 `SPT_JIT_MAX_LIVEIN=32`,
溢出则 `n_livein=-1` 回退全扫)。入口检查改为遍历这张 ~3 项的表而非全扫 IR。**仍用
`rec_value_type(...)!=SPTType` 比较**(不存原始 Lua tag——SPTT_STR 同时覆盖 VSHRSTR/VLNGSTR,
存 tag 会误判;误判虽安全(退化到解释器)但会无谓放弃 JIT)。效果 0.35×→**0.48×**;类型转换防挂
内核仍 MATCH 不挂;287 ctest + 73 kernel + 284 差分 + 多 seed 模糊 + valgrind 全绿。
**剩余 0.48× vs 0.61× 的差**是每迭代重进入本身的开销(进入函数调用 + 序/尾声 + ~3 次类型读),
属下面的结构性问题,不是这次检查能消除的。

2. **结构性问题(更大、且是预先存在的)**:上面的抛硬币录制。这是 §10.18 防挂入口检查之外的、
**让分支循环净负的根本原因**。判好坏 trace 仅凭 entry/exit 计数**无法区分**:好 trace(录到多数)
在少数路径退出,坏 trace(录到少数)在多数路径退出——两者都 entries≈exits,且都有"某快照退出≈
entries"。唯一差别是**每次进入的内循环迭代数**(好 ~10,坏 ~1.1),而测它需要每迭代一个循环计数器
(对快 trace 是开销=回归)或自改代码去掉计数器(有风险)。

**结构性修复的设计选项(下一步 #1 优先级,需聚焦投入):**
- (a)**解释器侧分支剖析 → 确定性多数方向录制**(最佳):循环变热、将要录制时,在解释器里对该循环
  PC 段的每个条件分支统计 K 次迭代的 taken/not-taken,然后录制时在每个分支强制走**多数**方向
  (守卫极性与跟随路径都按多数定)。直接消灭抛硬币,对好 trace 零运行时开销,且**确定**。代价:动
  解释器热路径(需把剖析限定在"正在剖析的那个循环",避免给所有代码加开销)。
- (b)**寄存器分配的循环计数器 + 拉黑**(安全网,保证不比解释器慢):仅给"含比较守卫(即含分支)"
  的 trace 在 LOOP 回边加计数;simple/array 循环不加→不回归。每次进入数累计后查 内循环数/进入数,
  低于阈值(经验 ~3:好≈10 留、50/50≈2 拉黑、坏偏置≈1.1 拉黑)则拉黑该 PC→回解释器。误拉黑只损失
  JIT 提速、不损正确性/不挂——安全。难点:计数器若用内存自增对快分支 trace 约 -15%(回归),需用
  寄存器计数(增加寄存器压力)规避。
- (c)**Phase-2 侧 trace**(LuaJIT 路线):热侧退出处编译另一分支臂并链接,使两臂都编译、循环内部
  闭合,与先录哪条路径无关。正确但工程量大、风险高,多 session 投入。

**结论**:含分支循环非确定地比解释器慢,是 JIT 要成为真正 top-tier 必须解决的**头号**问题;按用户
指令,这种"非确定 + 比解释器慢"的 trace 要么做成**确定性快**(a/c),要么做成**确定性安全**(b 拉黑)。
本轮只发布了安全、零正确性风险的 §10.18 回归修复(把自引入的额外减速基本抹平),结构性修复留作
下一轮聚焦工作,不在纪律("宁可回退也不发布有风险/慢/非确定的 trace")下仓促上线。完整 IR/反汇编/
字节码/逐快照退出/HOT 扫描输出见 transcript。

### 10.23 结构性修复(已实现):基于剖析的多数方向分支录制——消灭抛硬币,偏置分支循环由比解释器慢变为更快

§10.22 诊断出的头号问题(含分支热循环非确定地比解释器慢,根因是 `rec_cond_branch` 录制录制那一刻
分支恰好走的方向=抛硬币)的**结构性修复**。选了 §10.22 设计里的方案 (a):**先剖析、再按多数方向录制**。

**实现(四处改动,全部 correctness-safe):**
1. **全局快门 + 每分支统计**(spt_jit.c 顶部):`int sptjit_profiling_active`(进程级,仅在短剖析窗口内
   非 0,故 VM 钩子平时是一次预测不跳转的分支)+ 静态 `g_prof`{proto, pc 段, iters, budget, 按比较
   指令 PC 偏移统计 ft/tk}。`sptjit_profile_cond()` 按比较 PC 累加 fall-through/take。
2. **VM 钩子**(lvm.c `docondjump` 宏,9 个比较 opcode 共用的唯一一处):
   `if (l_unlikely(sptjit_profiling_active)) sptjit_profile_cond(L, pc, (cond != GETARG_k(i)));`
   ——只观察、不改控制流,故对正确性零影响;平时一次预测分支。**实测开销可忽略**(比较密集 5000 万次
   循环 JIT-off:1.308s→1.249s,在噪声内)。
3. **剖析生命周期**(`sptjit_trace_hot`):循环变热时**不再立即录制**,而是开剖析(设 g_prof、快门=1、
   iters=0、budget=512);此后该循环每条回边 ++iters,够 SPT_PROF_ITERS=64 个样本后关快门并录制;
   budget 每个 hot tick 递减,降到 0 仍没采够样(被剖析循环已停止迭代)就弃掉,避免别的循环被饿死;
   剖析期间别的循环撞阈值则等待(快门已占用),稍后重撞。保留了 abort→phase-shift 重试。
4. **录制用多数**(`rec_cond_branch`):算完预测方向后,若 g_prof 里有本分支(按比较 PC 偏移、且
   g_prof.proto==rc->p)的样本,就 `fall_through = (ft >= tk)` 覆盖。

**为什么强制多数方向是 correctness-safe**(关键论证):IR 是经 reg_map **符号化**构建的,不读活栈的值
——录制 then 体的 `s=s+1` 用的是 reg_map[s] 这个 IR ref(入口 SLOAD),哪怕本次录制迭代实际走了 else
(活栈里 s 是 else 的结果)也无所谓,IR 算的是 entry_s+1,运行时正确。预测(rec_pred_num)只用于常量
折叠和(已被覆盖的)分支决策;折叠/嵌套分支的过期预测由 §10.21(过期下标回退)和运行时守卫兜底。强制
方向只改"录哪条臂 + 守卫极性",录出的比较仍是**运行时守卫**——多数判错最坏只是 trace 次优(和原抛硬币
一样),绝不会算错。

**效果(20M 迭代 best-of-5):**
- **抛硬币彻底消失**:`if(a<9)` 偏置循环在 HOT=20/21/25/28/29/30 **全部** entries≈200K(录到多数
  a<9),此前 HOT=20/30 是 1.8M(录到少数,灾难)。录制结果不再依赖阈值命中的迭代——**确定性**。
- **偏置 90/10:0.31–0.48× → 2.84×**(由比解释器慢变为更快)。偏置 75/25 → 1.67×。带 else 同样。
- **50/50:0.58–0.61× → 0.88×**(改善但仍略低于解释器)。50/50 无多数可录,录任一臂都 ~50% 侧退出;
  其慢的根因是**非内循环 trace 的重进入开销 + JIT-on 下每条回边都过 `sptjit_trace_hot`(含 hot_lookup)
  的机器开销**,与 trace 质量无关。**试过 blacklist 近-50/50 分支但已移除**:blacklist 后该循环走解释器
  但仍每回边过 sptjit_trace_hot,实测 0.85×,并不比编译的 0.88× 好,反而有"误杀本可更快的偏置循环"的
  风险。50/50 的真正解法是 Phase-2 侧 trace(两臂都编译、循环内部闭合),留待后续。
- **无回归**:标量 12.7×、数组读 4.15×、嵌套 2D 9.49× 不变;嵌套+分支编译且 1.68×;§10.18 防挂仍
  MATCH 不挂(多数录制让它录到公共 acc-int 路径,acc 变 float 的罕见侧退出由入口检查兜底)。
- **验证**:287 ctest + 73 kernel + 284 差分 + 模糊 seeds 1-10(4000 例)0 失配 + valgrind 0 问题。

**意义**:这是本项目分支处理从"能跑但慢且非确定"到"快且确定"的关键一步,直接对齐用户"不要比解释器慢、
不要非确定"的硬要求。核心方法论:把"录哪个方向"这个一次性随机决策,换成对真实运行分布的短期剖析后取
多数——零运行时开销(快门平时不触发)、零正确性风险(符号化 IR + 运行时守卫)。剩余的 50/50 是侧 trace
的领域,不是录制方向能解决的。

### 10.24 修复(主动探测发现):running-max/min 算错值——循环携带值同时作为分支操作数且被重赋为无关值

§10.23 发布后,用"贴近真实"的模式主动探测,发现 `int m=0; for(...){ int v=i%100; if(v>m){m=v;} } print(m);`
**算错**:interp=99,jit=0(或 N=150 时给 50)。JIT 算成了 **m=最后一个 v**,而不是 max(v)。

**根因(先前就存在的 codegen bug,被多数录制可靠暴露)**:寄存器驻留冲突。槽 0(m)是循环携带值,被分到
一个 GPR R0;但它的循环末值 `ref4`(=v=i%100,在循环体顶部算出的 MOD)**也**被分到 R0——于是 v 在守卫
`LT(ref5=旧 m, ref4=v)` 读旧 m **之前**就把 R0 覆盖成了 v,守卫变成 `v<v`/读到 v,几乎恒真,m=v 几乎每次都执行。
ra_analyze 此前没有检测到"一个槽的旧值(它的 SLOAD)在它的新值被物化之后还被读取"。

**为什么只有这个模式触发**(精确刻画,已用 7 个变体隔离):仅当 (1) 槽是比较/分支操作数,(2) 被重赋为一个
**不依赖其旧值**的、在体内更早算出的值时才出错。`s=s+1`、`m=m*2`(新值由旧值导出,产生新值的那条指令本身就是
旧值的最后一次读取)、`m=50`(常量在守卫之后才物化)都安全。

**修复(ra_analyze 新增 bail,spt_jit_codegen.c)**:对每个驻留槽,令 fr=该槽循环末值的位置;扫描 fr 之后的
指令,若有非 DEAD 指令读取该槽的 SLOAD(经 ra_canon_ref 规范化,**跳过 SPTIR_LOOP——其 op1 是位置标记不是
值读取**)→ bail。一开始忘了排除 LOOP,导致 LOOP.op1=loop_start 恰好等于循环变量 SLOAD 的 ref,把标量
循环误判触发、退化到 spill(12.7×→1.5×);加 `if(op==SPTIR_LOOP) continue;` 后标量恢复 13×。

**再加 abort(而非 spill)**:bail 默认走 spill 路径(正确但对 running-max 是 0.47×,因为暖机后 m=99,守卫
`v>99` 每次都失败→每迭代侧退出,是侧 trace 的领域)。按"比解释器慢就 abort"的要求,给这个**新 bail**(不影响
置换/别名那些 spill case)加了 `cg->ra_conflict_abort` 标志:置位后 codegen 不发码、留 t->code=NULL,录制器
当作 abort 处理。

**意外之喜**:abort 触发了已有的 phase-shift 重试(e->counter=e->aborts),重试落在**更晚的迭代**(此时 m 已
很大、v>m 多半为假),于是录制器抓到了**跳过路径**(m 不变)——这条 trace 没有循环携带冲突、内部闭合、很快。
于是 running-max 从"算错"直接变成**正确且 3.62×**,running-min 3.42×。(暖机很长的 running-max 则会在冲突相
退完重试后被 blacklist→解释器 ~0.85×,也比 0.47× spill 好,且正确。)

**效果**:min/max 由 DIFF(算错)→ 正确且 3.62×;running-min 3.42×。标量 13.11× 不变,偏置分支 2.84× 不变,
break 16.21×,嵌套 9.36×。唯一仍偏慢的是 50/50(0.88×,无多数、侧 trace 领域)。

**验证**:running-max/min 参数扫描 120 配置(4 模式×6 个 N×5 个 HOT)0 失配;287 ctest + 73 kernel + 284 差分
+ HOT 扫描模糊 seeds 1-12(4200 例)0 失配 + valgrind 0 问题。

**方法论**:"差分 MATCH ≠ JIT 正确"——这个 bug 是主动用真实模式探测 + 看实际数值才暴露的(不是模糊器随机
撞到的)。ra_analyze 的寄存器驻留:循环携带槽被复用为分支操作数、又被重赋为一个更早算出的无关值 → 旧值在其
守卫读取前就被覆盖 → 算错。SPTIR_LOOP.op1 是位置标记不是值,任何"值使用"扫描都必须排除它。

### 10.25 if-conversion(分支转无分支 select)——消除简单整数 if-else/if-only 的侧退出

**问题**:~50/50 的分支(`if(i%2==0){s=s+1}else{s=s+2}`)在线性 trace 里只录一条臂、另一条臂每次侧退出。
即便用多数录制(§10.23)选常见方向,50/50 没有"常见方向",暖机后仍每隔一次就退出→重入,开销 ~15%,实测 **0.88×(比解释器慢)**。abs/clamp(`if(v<0){v=-v}`、`if(v>100){v=100}`)同理偏慢(1.1×)。这类是线性 trace 的固有短板,正常需要侧 trace。

**方案**:if-conversion。把 `if(c){slot=A}else{slot=B}` 编译成无分支的 `slot = B + (A-B)*c`,其中 c 是比较
materialize 出的 0/1 整数。两条臂都无条件求值、用普通整数算术选择,trace 内部闭合、**零侧退出**。

**为什么正确且安全**:
- 整数 add/sub/mul/bitwise 无论"分支执行"还是"两条都算再选"都按相同方式回绕(wrap),结果逐位相同——没有溢出分歧。
- **两条臂都会求值**,所以会 trap 的运算(DIV/IDIV/MOD/POW)绝不能 if-convert(未取的那条臂不能 fault)——
  `opcode_is_ifconv_safe` 把它们排除,只允许 MOVE/LOADI/ADD/SUB/MUL/ADDI/+K/BAND/BOR/BXOR/+K/SHL/SHR/BNOT/UNM。
- 结构识别保守:不符合标准 Lua if-else/if-only 形状(比较+JMP、单值产出 op±可选 MMBIN 的臂、两臂写同一槽)
  就回退到带守卫的分支(§10.23 多数录制),**回退路径已被充分验证**。
- 浮点/混合类型臂在录制前用 `ifconv_arm_int_result` 预判(读 reg_type,未加载的槽则读活栈值,不发 IR),
  非整数结果→干净回退到守卫分支(不是 abort)。

**实现**:
1. 新增 IR op `SPTIR_CMPSET`(op1/op2=比较操作数,aux=比较 SPTIROp)→ codegen 发 cmp+setcc+movzx 得 0/1。
   加入 `ra_op_is_safe`(否则用了 CMPSET 的 trace 退化到 spill)。
2. recorder 在 `rec_cond_branch` 开头先试 `rec_try_ifconv`:解析标准 if-else(比较@pc、JMP1@+1 跳过 then、
   then 臂末尾的 JMP2 跳过 else→else 在 JMP1 目标、merge 在 JMP2 目标)/if-only(JMP1 直接跳到 merge);
   每条臂限单个值产出 op(+可选 MMBIN);用 `rec_inst` 在 fork 的 reg_map[slot] 上分别录两臂,捕获 then_ref/else_ref;
   建 `cref=CMPSET(a,b,fop); diff=SUB(then,else); prod=MUL(diff,cref); res=ADD(else,prod)`;设 reg_map[slot]=res、pc=merge。
3. **关键坑(一开始没 fire)**:50/50 的累加器 s 在比较 `i%2==0` 处尚未加载(它只在臂里 `s=s+1` 才首次被读),
   `reg_map[s]<0` 导致 bail。修复:不 bail,改用 `rec_load_reg(rc,slot)` 强制把循环携带值加载为 live-in
   (rec_load_reg 发 SLOAD+类型守卫;两臂内部的 rec_load_reg 命中已加载的 ref 复用之);if-only 的 `else_ref=old_ref`
   因此有效。类型预判也改读"活栈类型 or reg_type"以便未加载的槽也能分类。

**polarity**:fop 是"落入 then(fall-through)的条件",CMPSET(fop)=1 → then 臂;`else+(then-else)*1=then`。一致。

**效果(全部正确)**:50/50 **0.88×→3.80×**,clamp 4.5×,abs 4.9×,if-else-mul 3.9×,select-const 4.3×。
偏置分支也走 if-conversion(`if(a<9){s=s+1}` 的隐式 else 是 s 不变)→ 3.2×(比 §10.23 的 2.84× 还好)。
running-max `if(v>m){m=v}` 也被 if-convert(单 MOVE)→ 无分支 2.91×(§10.24 的 abort+phase-shift 仍保留,
保护不可转换的多槽形如 `if(v>m){m=v;mi=i}`)。非分支模式(标量 13×/数组/嵌套/break/内联)完全不变。

**验证**:287 ctest + 73 kernel + 284 差分 + HOT 扫描模糊 seeds 1-44 两轮(12,720 例)+ 16 个棘手手工探测
(条件交换/双携带/max+index/嵌套分支/clamp/翻转/continue/break/混合算术/三分支)+ valgrind 全部 0 失配/0 问题。
新增 `gen_ifconv` 模糊生成器(if-else/if-only 的 clamp/abs/select-const/累加/running-max/浮点回退六种形态)锁定覆盖。

**边界/未覆盖**:仅整数槽、单值产出 op 的臂(`v=0-v` 若编译成 LOADI+SUB 共 3 指令则不转、回退;写成 `v=-v`/UNM 则转);
多槽臂(max+index)不转;浮点 if-else 不转(回退守卫,1.3-1.4×)。这些都安全回退,可后续推广到多 op 臂/浮点 select。

### 10.26 if-conversion 推广到多槽 / 多 op 臂——消除条件式多变量更新的负优化

**问题**:§10.25 只处理单槽单 op 臂。survey 出的最差残留负优化全是**条件式多变量更新**——
`if(v>m){m=v;mi=i}`(最大值+下标)实测 **0.55×**、`if(x<y){x=x+2;y=y-1}`(两个携带变量反向走)**0.30×**、
`if(...){a=..;b=..}else{a=..;b=..}` 1.21×。它们都回退到带守卫的分支,而该分支频繁侧退出→重入→比解释器还慢。

**方案**:把 if-conversion 推广到"两条臂各写若干槽"。对**两条臂写入槽的并集**逐槽生成一个
`slot = else + (then-else)*c` 的 select,**共用同一个 CMPSET**(c 只算一次)。某条臂没写某个槽时,
它在那个槽的 select 输入就是该槽的旧值(if-only 的 else 分支天然如此),所以并集 + 逐槽 select 自动正确。

**实现要点**:
1. `parse_ifconv_arm` 重写成扫描整条臂体:每个 op 必须是 `opcode_is_ifconv_safe` 的非陷阱整数 op
   (可跟 MMBIN),结果须为整数(`ifconv_arm_int_result`,操作数类型读 reg_type 或活栈),把目标槽并入 wslots
   (≤ `IFCONV_MAX_SLOTS`=4)。**关键约束:拒绝"读到本臂更早写过的槽"的 op(臂内依赖)**——因为录制前的
   类型预判此时不可靠(那个槽的类型会在臂内改变)。这把 swap(`t=a;a=b;b=t` 里 `b=t` 读了刚写的 t)排除掉、
   干净回退到守卫分支;而 max+index、两携带、双臂累加这些**无臂内依赖**的常见形态都能转。
2. 多 op 臂的录制:`while(rc->pc < arm_end) rec_inst(rc)`。`rec_inst` 自己推进 rc->pc(末尾 `rc->pc++`;
   算术 op 经 `rec_skip_mmbin` 吞掉尾随 MMBIN 再 +1),所以循环自然走完整条臂、停在 arm_end;
   循环后断言 `rc->pc==arm_end`,否则 abort(防越界)。
3. fork/select 用数组:先 `rec_load_reg` 把所有 wslot 变 resident(循环携带的下标 mi 等作为 live-in 加载+守卫);
   录 then 臂→存 then_ref[k]、还原 reg_map;录 else 臂→存 else_ref[k]、还原(if-only 则 else_ref[k]=old_ref[k]);
   再逐槽 `CMPSET→SUB→MUL→ADD`(CMPSET 只发一次,被所有槽共用)。

**为什么正确**:与 §10.25 同理——两条臂都无条件求值、纯整数回绕算术逐位一致、陷阱 op 被白名单排除;
多槽只是把"一个 select"变成"每槽一个 select",fork 对每个写入槽独立 save/restore。temp 槽(若被并入)
对应的 select 是死代码、被 DCE 清掉,不影响正确性。

**效果(全部正确)**:max+index **0.55×→1.96×**、两携带 **0.30×→1.93×**、双臂累加 1.21×→2.10×;
单槽回归不变(50/50 2.21×、running-max 2.25×、clamp 2.40×);swap 因臂内依赖正确回退(0.85×,仍正确)。

**验证**:287 ctest + 73 kernel + 284 差分 + HOT 扫描模糊 seeds 1-40 两轮(11,560 例)+ valgrind(多槽用例,exit=0,
0 问题)+ 10 个多槽手工探测(max/min+index、两携带、双臂累加、3 槽、swap 回退、不同写集、交错、混合 1/2 槽、嵌套)
全部 0 失配。`gen_ifconv` 模糊生成器扩出 4 种多槽形态(max+index、两携带、双臂累加、3 槽更新)锁定覆盖。

**边界**:仍仅整数;并集 ≤4 槽、每臂 ≤16 op;有臂内依赖的臂(swap)回退;浮点 if-else 回退。

### 10.27 条件返回函数内联——把 `if(c){return A}return B` 内联并 if-convert 成无分支 select

**问题**:最后一类负优化是**带条件返回的 helper 函数**——`clamp`/`abs`/`max`/`min` 这类
`function f(int x){ if(c){return A;} return B; }` 在热循环里被调用,实测 **0.89-0.92×(比解释器慢)**。
原因:`proto_is_inlinable` 只接受纯直线叶函数(单 RETURN1、无控制流),这种带分支+两个返回的函数不被内联,
recorder 在 CALL 处 abort→拉黑→残留机器开销让整体比纯解释还慢。这类 helper 在真实数值代码里很常见。

**方案**:把**内联**和**if-conversion**结合。识别 `if(c){return A} return B` 的精确形状,内联它并把条件返回
if-convert 成 `caller_result = B + (A-B)*c`(c 是比较 materialize 的 0/1),像 OP_RETURN1 一样绑定到调用者的
结果槽并恢复调用者上下文,**但没有侧退出**。

**实现**:
1. **静态门** `proto_is_condreturn_inlinable(p)`(放在 `proto_is_inlinable` 旁):精确匹配
   `[直线前缀][比较][JMP][then 直线+RETURN1][else 直线+RETURN1][死 boilerplate]`。每个非控制 op(前缀+两臂)
   必须是 `opcode_is_ifconv_safe` 的非陷阱整数 op——这**同时**保证两个返回值是整数(select 算术逐位精确)、
   且禁止第二个分支/调用/循环/副作用(它们不在白名单里,扫描即拒)。所以 3 分支的 sign、含循环的 helper 都被拒、
   干净回退到不内联。
2. **OP_CALL** 扩展:`proto_is_inlinable` 失败时再试 `proto_is_condreturn_inlinable`,两者任一通过就用**同样的**
   方式切入被调用者(切 frame_base、pc=callee code)。直线函数在 RETURN1 处绑定结果;条件返回函数在比较处被
   `rec_try_condreturn_ifconv` 接管。
3. **`rec_try_condreturn_ifconv`**(放在 `rec_try_ifconv` 旁,挂在 `rec_cond_branch` 开头、仅 frame_base≠0 时触发):
   解析结构(比较@pc、JMP→T1=else 起点;then 臂 [pc+2,T1) 末尾必须 RETURN1;else 臂 [T1,callee_end) 第一个
   RETURN1);对**被调用者帧**做 save/restore 的 fork,分别录 then 臂的值计算([pc+2, then_ret))和 else 臂的
   ([T1, else_ret)),各自捕获返回操作数的 ref(`reg_map[fb+then_reg]` / `[fb+else_reg]`,录后检查类型为 INT);
   建一个共用 CMPSET + `SUB→MUL→ADD` 的 select,写进 `reg_map[call_result_slot]`,再恢复调用者上下文(p/k/cl/
   frame_base/pc=save_pc),等价于一个无分支的 RETURN1。

**关键设计点**:
- 返回值的类型**不能在录制前预判**(then 的返回值往往是臂内才算出的 temp,此时 reg_map[fb+reg]<0,而
  `ifconv_reg_is_int` 的活栈兜底对被调用者帧是错的——那是根帧的栈地址)。所以靠"静态门保证整数 body" +
  "录制后检查 reg_type==INT"两道闸,不依赖活栈兜底。
- 极性同 §10.25:fop 是落入 then(fall-through)的条件,then 块在条件**真**时执行(JMP 在假时跳过),
  CMPSET(fop)=1→then→A,`B+(A-B)*1=A`。一致。
- 若 `rec_try_condreturn_ifconv` 返回 0(不适用),`rec_cond_branch` 回退到带守卫的分支——在被调用者内部录单条
  返回路径、守卫条件,**也是正确的**(只是会侧退出)。所以即便静态门偶尔放进一个 if-conversion 处理不了的形状,
  也不会错,只是不够快。

**效果(全部正确)**:clamp **0.91×→3.70×**,abs 0.91×→3.44×,max 0.92×→2.86×,min 0.90×→2.91×,
带分支返回的非叶函数 0.92×→3.85×;纯叶内联(4.16×)、§10.25/§10.26 的 if-conversion(50/50 2.20×、
max+index 1.94×)回归完全不变。

**验证**:287 ctest + 73 kernel + 284 差分 + HOT 扫描模糊 seeds 1-42 两轮(17,520 例)+ valgrind(条件返回用例,
exit=0,0 问题)+ 15 个边界探测(computed return、prefix 条件、各比较算子、嵌套两 helper、结果入表达式、
helper 后接分支;以及 float 参数 / 非热循环 / 3 分支 sign / 含循环 helper 这些回退场景)全部 0 失配。
`gen_condreturn` 模糊生成器(clamp/abs/max/min/computed/二分支回退/含循环回退 七形态)锁定覆盖。

**边界**:仅整数返回;被调用者必须是恰好一个条件返回的叶函数(无第二分支、无调用、无循环);深度 1;
其余形状(多分支 sign、条件赋值后返回 `if(c){x=..}return x`、浮点返回)回退到不内联或守卫分支。

### 10.28 条件赋值后返回函数内联 + 修复"被调用者内部带守卫分支"的严重正确性 bug

**问题**:§10.27 处理了 `if(c){return A}return B`,但它的常见**等价写法** `if(c){x=A} return x`
(clamp/ceil 等 helper 的另一种风格)仍是负优化(0.91×):`proto_is_condreturn_inlinable` 要求 then 臂以
RETURN1 结尾,而这里 then 臂是赋值、返回在 if 之后,所以不被内联,CALL 处 abort。

**方案**:复用已有的整数 if-conversion——把 `rec_try_ifconv` 推广到能在**被调用者帧**(frame_base≠0)触发。
被调用者里的条件赋值 if-convert 成 `slot = select(c, A, old)`,recorder 推进到 merge,紧跟的 `return slot`
照常绑定到调用者结果槽。无新增 codegen,全是整数 CMPSET。

实现三块:
1. `rec_try_ifconv` 去掉 `frame_base!=0 return 0` 的门,所有 reg_map/reg_type 下标从"相对寄存器"改成
   `frame_base + 相对寄存器`(根帧 frame_base=0 时退化为原样,行为不变)。
2. `ifconv_reg_is_int` 的**活栈兜底对被调用者帧禁用**:未 materialize 的槽在 frame_base≠0 时直接返回 0
   (因为 `(ci->func+1)+reg` 寻的是根帧的栈,不是被调用者帧)→ 干净 bail,绝不让后面的 `rec_load_reg`
   走到它的"被调用者帧未加载即 abort"路径。
3. 新静态门 `proto_is_condassign_inlinable`:精确匹配 `[前缀][比较][前向 JMP][then 直线赋值,无 return]
   [merge 后直线计算][唯一 RETURN1][死 boilerplate]`(if-only)。OP_CALL 加入它。

**踩到的严重 bug(本节核心)**:第一版测试 `if(x<0){x=0-x;}return x;` 直接**崩溃**(JIT 输出为空,
解释器是 104761910)。根因:`0-x` 编译成 `LOADI tmp,0; SUB x,tmp,x`,SUB 读了臂内刚写的 tmp(臂内依赖),
`parse_ifconv_arm` 正确拒绝→`rec_try_ifconv` bail→**落到了被调用者内部的"带守卫分支"兜底**。而带守卫分支
意味着一个**被调用者中段的控制流分叉**,它的快照要恢复一个"合成的被调用者帧",但单入口 trace 根本无法表达
这种中段分叉的退出——于是状态错乱、崩溃。**这条兜底路径以前从未被真正触发过**(§10.27 的"回退"测试里 helper
根本没在循环里被调用),所以是潜伏的。

**修复**:`rec_cond_branch` 里,if-conversion 全部 bail 后,**若 frame_base≠0 则直接 abort**,绝不发被调用者
中段的带守卫分支(那个调用就是不内联,回退到 0.91×,安全且绝不出错)。这条修复同时**加固了 §10.27**——任何
被调用者内部无法 if-convert 的比较现在都安全 abort,而不是发出可能损坏的中段分支。
(注:被调用者内部的**类型守卫** SLOAD+GUARD_T 是另一回事,它不分叉控制流、退出到循环头重执行,纯叶内联一直在用、
是安全的;这里只 abort 会分叉控制流的比较分支。)

**效果(全部正确)**:cond-assign clamp 0.91×→3.74×、ceil 0.91×→3.72×、x-const 3.7×、两语句后返回 3.77×、
两参数 max-into 3.7×;不可 if-convert 的 `0-x`/两语句臂内依赖现在**干净 abort**(0.91×,不再崩溃);
§10.27 条件返回(3.74×)、纯叶(4.11×)、根帧 if-conversion(50/50 2.21×、max+index 1.92×)全部回归不变。

**验证**:287 ctest + 73 kernel + 284 差分 + valgrind(条件赋值+条件返回+abort 混合用例,exit=0,0 问题)+
HOT 扫描模糊 seeds 1-40(14,500 例,gen_condassign 含 0-x abort 形态)+ 15 个手工探测(各算子、前缀计算、
两参数、post-merge 计算;以及 0-x / 两语句臂内依赖 / 条件返回 0-x 这些 abort 与正常路径)全部 0 失配。
`gen_condassign` 生成器(clamp/ceil/x-const/post-merge/两参数/0-x-abort/两语句-abort 七形态)锁定覆盖。

**边界**:仅 if-only 整数条件赋值;then 臂不能有臂内依赖(否则 abort);被调用者须无第二分支/调用/循环;深度 1。
**关键教训**:被调用者中段的控制流分叉(带守卫分支)在单入口 trace 里不可表示——只能 if-convert(无分支)或 abort;
任何"在被调用者里回退到守卫分支也正确"的假设都是错的,必须实测触发。

### 10.29 系统性加固:针对内联×if-conversion 交互的激进组合模糊(scripts/jit_fuzz_helpers.py)

§10.28 那条潜伏 bug 暴露了一个方法论问题:**标准模糊器的"差分 MATCH"不等于回退/交互路径被覆盖**——
它的生成器各管一摊,从没把"内联的 helper × 调用者侧 if-conversion × 循环携带累加 × 浮点实参 × 嵌套/重复调用"
这些**交互**和**abort 回退**组合起来打。于是新增一个专门的组合模糊器 `jit_fuzz_helpers.py`:随机定义 1-3 个
各种形状的 helper(纯叶、条件返回、条件赋值、两参数、以及**故意不可内联/会触发 abort 的** 0-x 臂内依赖、
多分支 sign、含循环 callee),再生成一个热循环用各种方式调用它们(嵌套不同 helper、结果进调用者分支、
int/float 实参混合、可选循环携带变量)。每个程序 JIT off vs on 跑 4 个 HOT 阈值比对。

**结果**:seeds 1-32 共 **3200 个程序 × 4 HOT = 12,800 次差分检查,0 失配**。覆盖度抽查(60 程序):50%
至少编译出一条 trace、18 个含 if-converted(CMPSET)trace,另 50% 走 abort-only 路径——即 bug 当年所在的
回退路径被大量真实触发。说明 §10.28 的"被调用者中段比较一律 abort"修复在交互压力下稳固,没有新 bug。

**留作长期回归资产**。教训固化:**回退路径必须用真正会走到它的输入去触发测试**,加新特性前先用组合模糊把
交互面打一遍。

### 10.30 浮点 if-conversion:位精确掩码混合(FCMPMASK/FSELECT)

**动机/问题**:整数条件分支早已被 §10.25/§10.26 if-conversion 成无分支(50/50→2.2×、max+index→1.92×),
但**浮点条件分支仍回退到守卫分支**,且其中多条是**真实负优化**(比解释器慢):
float max-into `if(v>m){m=v}` **0.61×**(相变分支,守卫每轮失败、20M 次退出);
float clamp-high `if(v>100.0){v=100.0}` **0.97×**;float two-slot **0.95×**;
float clamp(ReLU)/select-const 也仅 ~1.0–1.05×。全都因为浮点臂落到退出密集的守卫分支(每轮一次 side-exit)。
按"trace 不得慢于解释器"的硬约束,这些必须修。

**为什么不能照搬整数那套**:整数 if-conversion 用 `select = else + (then-else)*cond`(cond∈{0,1})。
浮点**不能**这样——`(then-else)*cond` 会引入**舍入**,结果不是被选中那个 double 的逐位复制。
必须用**位掩码混合**:`result = (then & mask) | (else & ~mask)`,其中 `mask` 是全 1 / 全 0 的 64 位掩码。
这样被选中的操作数被**逐位**搬运,绝对位精确(分数值 6178079.285713153 之类与解释器完全一致)。

**实现**(asm → IR → codegen → recorder 四层):
- **asm**(`spt_jit_asm.c`):新增 `sptasm_cmpsd(dst,src,imm8)`(0xF2 0F C2,标量双精度比较→掩码)、
  `sptasm_andpd/andnpd/orpd`(0x66 0F 54/55/56,打包双精度位运算;ANDNPD 是 `dst=~dst&src`)。
  打包指令作用于全 128 位但只回读低 64 位的标量 double,安全。
- **IR**(`spt_jit_ir.{h,c}`):新增 `SPTIR_FCMPMASK`(op1=a,op2=b,aux=比较 SPTIROp)和
  `SPTIR_FSELECT`(op1=then,op2=else,**aux=mask 引用**)。两者像 CMPSET 一样**不进 CSE/常量折叠**。
  **关键**:DCE 的 `mark_used` 必须像 GUARD_LT/LE 那样**跟踪 FSELECT 的 aux**(mask 引用),
  否则 FCMPMASK 被当成无用代码消除,FSELECT 读到垃圾。
- **codegen**(`spt_jit_codegen.c`):全部走 **spill 模型**(`gen_load_xmm`/`gen_store_xmm` 透明处理常驻或溢出),
  用 XMM0/1/2 作 scratch,**完全不碰寄存器常驻(XMM4-7)**,规避寄存器分配风险。
  FCMPMASK:载入 a,b → `cmpsd` → 存掩码。FSELECT:载入 then/else/mask → `andpd`/`andnpd`/`orpd` → 存结果
  (mask 寄存器既是输入又当 scratch 复用,因为 FSELECT 后即死)。两者加入 `ra_op_is_safe`(纯 SSE,无 C 调用)。
- **recorder**(`spt_jit.c`):`rec_try_ifconv` 泛化为 int/float 双路。
  `parse_ifconv_arm` 加 `is_flt` 参数;浮点臂用 `opcode_is_ifconv_safe_flt`(MOVE/LOADF/LOADK/ADD/SUB/MUL/**DIV**/+K/UNM
  ——浮点永不陷入,故 DIV 也可)+ `ifconv_arm_flt_result`。结构解析(then/else/merge)与类型无关、共用。

**NaN/inf 语义**:CMPSD 谓词精确匹配 Lua:`==`→0(EQ_OQ,NaN→假)、`<`→1(LT_OS)、`<=`→2(LE_OS)、
`!=`→4(NEQ_UQ,NaN→真);`>`/`>=` 没有"NaN 为假"的谓词,故**交换操作数发成 LT/LE**。
实测:clamp 遇 NaN 保持 -nan、max-into 遇 NaN 不更新(NaN>m 为假)、`!=` 遇 NaN 为真、max-into 遇 inf 取 inf——全部与解释器一致。

**踩坑**:`v<0.0` 被编译成 `LTI v,0`——**整数立即数 0**,recorder 传 `bt=SPTT_INT`(见 §LTI 行为)。
所以判据改为"**任一操作数是浮点即走浮点路径**"(`is_flt = at==FLT || bt==FLT`),并在构造掩码前把整数那侧 `TOFLT` 提升为 double。
初版写成 `at==FLT && bt==FLT` 时 clamp 完全不触发,正是栽在这里。

**作用域**:**Case 2 = 浮点比较(任一浮点,整数立即数提升)+ 浮点臂**(掩码用 `FCMPMASK`);
**Case 3 = 整数比较 + 浮点臂**(如 `if(i%2==0){s=s+v}else{s=s+1.0}`,掩码用 `ICMPMASK`:
整数比较→setcc→`neg`(0/1→0/-1 全 1)→`movq` 提进 XMM,整数全序故无需交换操作数,且整数按位精确比较、无精度损失)。
两者都用同一个位精确 `FSELECT` 混合。is_flt 的判定改为"**看臂写什么槽**"(偷看第一条臂指令的目标槽类型),
而非只看比较——所以整数条件+浮点臂也能正确进浮点路径;判错只会让臂解析失败、干净回退。
Case 4(浮点比较 + 整数臂)仍干净回退(整数臂在浮点解析下失败)。

**踩坑 2(Case 3 为什么不能直接把整数操作数提升为 double 再比)**:`(double)a < (double)b` 仅当 a,b 都 ≤ 2^53 时精确;
对任意 int64 会丢精度、比较结果可能不同。故 Case 3 必须用 `ICMPMASK` 在**整数域**比较,而不是提升后用 FCMPMASK。

**被内联 callee 内的浮点条件赋值 helper**:条件赋值路径用的就是 `rec_try_ifconv`(已浮点就绪),
故只需把静态门 `proto_is_condassign_inlinable` 的算子检查改用 `opcode_is_ifconv_safe_any`(int||float 并集),
`clampf`/`reluf` 风格的浮点 helper(`function f(float x){if(x<0.0){x=0.0;}return x;}`)即被内联 + if-convert(comp=1,exits=1,位精确)。
条件返回 helper 暂不支持(`rec_try_condreturn_ifconv` 仍是整数版、遇浮点 reg_type 即 abort,扩展静态门也只会干净 abort、无收益)。

**性能**(负优化全部消除,均 exits=1 无分支、逐位一致):
float max-into **0.61→2.33×**、clamp-high **0.97→2.83×**、two-slot **0.95→2.03×**、clamp(ReLU)→2.63×、select-const→3.14×。
Case 3(整数条件+浮点臂,此前 1.07–1.34×):50/50 if-else→2.18×、accum-if-only→2.03×、two-slot→1.90×。
整数 if-conversion 不受影响(50/50 2.2×、clamp、max+index、two-carried 全 exits=1)。

**验证**:287 ctest + 73 kernel + 284 差分 + valgrind(根帧浮点 + callee 浮点 if-conv + ICMPMASK 路径,exit=0)+
新模糊器 `scripts/jit_fuzz_float_ifconv.py`(Case 2 的 clamp/into/select/twoslot/ifelse_assign 五形态 × 六比较,
以及 Case 3 的整数条件 accum/ifelse/select/twoslot 四形态;分数操作数 × 3 HOT,seeds 1-22 共 2640 例 × 3 ≈ **7900 次逐位检查 0 失配**,
覆盖确认 ICMPMASK 与 FCMPMASK 都被触发)+ helper-zoo 模糊(含 callee 浮点 if-conv,实测发出 FCMPMASK)+
NaN/inf 手工探测(含 Case 3 臂内 NaN/inf)全过。边界(臂内依赖 abs/swap、多算子带临时量、Case 4 错配)全部干净回退、正确。

**关键教训**:(1)浮点 select 必须位精确——算术混合会舍入,只能用 FCMPMASK+FSELECT 的 andpd/andnpd/orpd 位掩码混合。
(2)`LTI/LEI/GTI/GEI` 即使 R[A] 是浮点也传整数立即数(`bt=INT`),浮点 if-conv 必须"任一浮点即浮点"并提升整数侧。
(3)CMPSD 无"NaN 为假"的 >/>= 谓词,一律交换操作数发 LT/LE。
(4)任何把 IR 引用存进 aux 的算子(GUARD_LT/LE 的界、FSELECT 的 mask)都必须在 DCE 里跟踪 aux,否则被错误消除。
(5)新 SSE 算子走 spill 模型 + scratch XMM0/1/2,完全不碰常驻寄存器,把寄存器分配风险降到零;mask 死后即复用为 scratch 省一个寄存器。

### 10.31 Phase 2:侧 trace 链接(栈中介 C-trampoline,非 LuaJIT 寄存器匹配)—— 覆盖不可 if-conversion 的少数臂

**动机/问题**:§10.25–§10.30 的 if-conversion 把**简单**整数/浮点 if-else/if-only 转成无分支 select,消灭了它们的侧退出。
但臂里有**副作用/循环/调用/可能陷入**的分支转不了无分支(数组写 `a[i]=x` 的 SETI、自增计数 `k=k+1` 跨迭代依赖、调用、可能 abort 的 op),
只能落到**守卫分支 + 少数臂每轮一次 side-exit**。对 `if(i%2==0){s+=i}else{a[k%8]=i;k++}` 这种 50/50、少数臂带数组写的循环,
trace 每个奇数迭代都退出到解释器跑完"另一臂 + 剩余循环体 + FORLOOP"再重入——退出密集,白白浪费。
Phase 2(roadmap §五)= **从热侧退出再录一条 trace 并链接回来**,正是这个残余问题的解法。

**设计抉择(关键,低风险优先)**:**栈中介的 C-trampoline 链接**,而非 LuaJIT 的**寄存器中介**链接。
理由:本 JIT 的退出 stub(`gen_exit_stub`)**本来就把全部活状态 flush 回解释器栈**并写好 `ci->u.l.savedpc`——
退出那一刻栈是**完全自洽的真相源**,和解释器即将 dispatch `savedpc` 时一模一样。
所以在退出 PC 重入一条 trace(其 SLOAD 会从栈重载)**完全不需要 trace 间寄存器状态匹配**:
每次交接都过"退出 stub 全量 flush → 下条 trace SLOAD 重载",栈是唯一真相。
代价是每次链接多一遍 flush/reload(性能不如寄存器直传),换来的是**风险骤降**——契合本项目"慢但对 > 快但错"的硬规矩。

**实现(asm 无新增;IR → codegen → recorder → trampoline 四层)**:
- **IR**(`spt_jit_ir.{h,c}`):新增 `sptir_exit(b, exit_pc)`——发 `SPTIR_EXIT` + 建快照(捕获当前 `reg_map`),
  把快照号写进 `snap_idx`(并镜像进 aux)。`SPTIR_EXIT` 本来就是 DCE 根、快照引用的值都会被保活,无需改 DCE。
- **codegen**(`spt_jit_codegen.c`):新增 `case SPTIR_EXIT`——`jmp` 到该快照的退出 stub(`ensure_exit_label`)。
  无条件(不像守卫有条件),控制流必从此处离开 trace;退出 stub 负责 flush + 写 savedpc=exit_pc + 回 epilogue。
- **recorder**(`spt_jit.c`):`SPTRecCtx` 加 `is_side_trace`;`record_trace` 加 `is_side` 形参。
  在 **OP_JMP 后向**与 **OP_FORLOOP** 两处,当 `target != start_pc`(根 trace 视作嵌套循环 abort,见 §10.15)时,
  **侧 trace 改为发 `sptir_exit(rc->pc)` 并干净收口(return 0)**:把控制交还解释器,由它跑那条 back-edge 再重入父 trace。
  FORLOOP 处的退出发在 count/idx 更新**之前**——解释器随后会自己执行 FORLOOP;侧 trace 没碰的槽(idx/count)
  靠快照里的 -1 项保留栈上的 flush 值,**恰好正确**。
- **trampoline**(`sptjit_trace_enter`):进入父 trace 后**循环**:读 `savedpc`,若该 PC 有已编译且入口守卫通过的 trace、
  且 `savedpc != 上次进入的 PC`(严格前进)、且在界内、且跳数 < `SPT_JIT_MAX_LINK_HOPS`(=64 绝对上限),就直接重入;
  否则跳出回解释器。**终止双保险**:严格前进(退到原地或不动则断链)+ 跳数上限。
  在**未命中分支**(退出 PC 无 trace)调 `maybe_record_side_trace`:若父某快照在该 PC 的退出计数(退出 stub 累加)
  ≥ `side_hot_threshold` 且该 PC 尚无 trace、未被拉黑,就 `record_trace(...,is_side=1)` 并登记进 hot_table。
  侧 trace 只经 trampoline 进入(其头是循环体中部 PC,**不是 back-edge**,解释器 hot-check 永不直接进它),这印证了 trampoline 的必要性。

**踩坑 1(headline 调试):侧 trace 录成了和父 trace 相同的方向,毫无收益**。
首版测 `if(i%2==0){s+=i}else{a[k%8]=i;k++}`:父 trace 在 pc20(OP_EQI `i%2==0`)守卫"偶数",奇数时退出到 pc20。
侧 trace 从 pc20 起录,**本应跟随录制时刻冻结栈的方向**(slot6=i%2=1,奇数 → 录 ELSE 臂的数组写)。
但 IR 里 `EQ slot6==0` 竟评估为**真(偶数)**、录了 `s+=i`(THEN 臂)——侧 trace 录成偶数假设,
却只在奇数时被进入,每次一进门就在根守卫 `EQ` 失败、立即退回 pc20(149908 次无用退出)。
**根因**:§10.23 的"**基于剖析的多数方向覆盖**"(`rec_cond_branch` 里按比较 PC 偏移查 `g_prof`)被套到了侧 trace 头上——
侧 trace 复用了父 trace 对该分支的剖析(50/50 时 `ft>=tk` 取 fall-through=偶数),覆盖了录制时的真实方向。
**修复**:`!rc->is_side_trace && g_prof...` 门住该覆盖。侧 trace 存在的**全部意义就是覆盖少数方向**(导致父退出的那个),
而录制时刻的冻结栈值**正是**那个少数方向(父刚在它上面退出)。修后侧 trace 含 SETI(数组写=ELSE 臂)、退到 FORLOOP,正确且有用。
(共享循环体里更深的分支仍可复用父剖析——按 PC 偏移匹配、且发射的比较始终是运行时守卫,无论方向都正确。)

**踩坑 2:在 back-edge PC 录出退化侧 trace**。修踩坑 1 后,侧 trace 退到 FORLOOP(pc30,op74),
trampoline 又在 pc30 触发录制——但从 FORLOOP 起录,第一条就是 FORLOOP 本身(`target!=start`),
立即 `sptir_exit` 收口成"在自己起点退出"的退化 trace(退到 pc30 自身)。trampoline 的严格前进检查会拒绝它(不死循环),
但仍是无用 trace + 每轮一次白进白出。**修复**:`maybe_record_side_trace` 加 back-edge 守卫——
起点 opcode 是 `OP_FORLOOP/OP_TFORLOOP/后向 OP_JMP(sJ<0)` 时不录(那种 trace 必退化;解释器跑一条 back-edge 重入父 trace 已是最优)。

**重入安全**:从 trampoline 调 `record_trace` 是安全的——录制器只**读冻结栈**、靠 `reg_map` **符号化**建 IR,
**不执行字节码、不改栈、不分配 Lua 对象/不触发可移动栈的 GC**;malloc trace/IR 与从代码池分配机器码都不动 Lua 栈。
父 trace 退出后栈已 flush 到位,录制器读到的正是退出后的精确状态。

**配置**:新增 `SPT_JIT_SIDE_HOT`(默认 `SPT_JIT_SIDE_HOT` 常量,内部 `side_hot_threshold`)——父退出计数达此阈值才录侧 trace,
低值用于测试主动触发。`SPT_JIT_MAX_LINK_HOPS`(=64)是链接跳数绝对上限。

**正确性不变量**:全程 `entries==exits`(如 300k 测试 299867==299867、2M 测试同步);
"另一臂 + 剩余循环体"现由侧 trace 原生执行,解释器每个少数迭代只跑一条 FORLOOP 后重入父 trace。

**验证**:287 ctest + **75** kernel(新增 `side_arr_write` 50/50 数组写臂、`side_biased_store` 10% 偏置稀有臂带写,
默认阈值即触发侧 trace)+ **286** 全量差分,**默认阈值与 `SIDE_HOT=30` 两档都 0 失配**;
差分 fuzz 新增 `gen_side_store` 生成器(扫:哪臂带写、50/50 vs 稀有 vs 常见偏置、是否双臂都带写),
seeds 多组共 **2300+ 例 0 失配**;valgrind(侧 trace 录制+链接路径 + 默认阈值根路径 + 现有分支 kernel,0 错误 0 泄漏)。
off vs on 逐字节一致含数组内容。

**关键教训**:
(1)栈中介链接的全部底气来自"退出 stub 本就全量 flush + 下条 trace SLOAD 重载"——栈是唯一真相,故无需寄存器匹配,风险最低。
(2)**侧 trace 必须跟随导致父退出的那个(少数)方向**,绝不能套父 trace 的多数剖析——否则录成父的镜像、一进门就退、纯负担。
(3)别在 back-edge(FORLOOP/TFORLOOP/后向 JMP)上扎侧 trace,必退化;解释器跑一条 back-edge 重入父 trace 已最优。
(4)终止靠"严格前进 + 跳数上限"双保险;任何"退到原地/savedpc 不动"都断链而非自旋。
(5)从 trampoline 递归调录制器安全,前提是录制器纯读栈、符号化建 IR、不执行不分配——这是本 JIT 录制器一贯的设计,Phase 2 直接受益。

### 10.32 Phase 2 性能修正:侧 trace 摊销门 + 拉黑路径 O(1) 化(测出来的负优化)

**问题(主动基准发现)**:§10.31 把侧 trace 做对了,但**没量性能**。补做 A/B/C 三方基准
(A=解释器、B=开侧 trace、C=高 `SIDE_HOT` 不触发侧 trace 即 Phase 2 前的根 trace+解释器兜底)后发现:
**对最典型的目标场景(50/50、轻少数臂:1 次数组写 + 自增),侧 trace 是负优化**——
20M 迭代:A 0.353s、C 0.439s(0.80×,已是负优化、§10.22 的残余难例)、**B 0.525s(0.67×,比解释器还慢)**,
且 **B<C(0.84×)**——侧 trace 把 JIT 自身基线**也**拖差了。这违反项目"trace 不得慢于解释器"硬约束。

**根因**:栈中介链接每次交接付一套**固定开销**(父退出全量 flush + 侧 trace 自己的 prologue + SLOAD 重载 + epilogue)。
对小臂,这套固定开销**超过**让解释器直接 dispatch 那几条字节码的成本。B 比 C 多了一整次侧 trace 的进入+退出,
对小 body 完全摊不平。(这正是栈中介换低风险的代价;LuaJIT 用寄存器中介直跳入侧 trace、近零开销,但高风险。)

**刻画边界(IR 总指令数 vs B/C)**:
- 轻臂 1 数组写 = **21 insts** → B/C **0.84×**(亏)
- 中臂 2 数组写 = **32 insts** → B/C **1.11×**(赚)
- 重臂 3 数组写+算术 = **49 insts** → B/C **1.25×**(赚)
- 偏置 10% 轻臂 = 21 insts → B/C **0.95×**(略亏)

**关键观察**:决定因素是 **per-link 经济性(臂大小)**,不是触发频率——小臂在任何频率都是 per-link 净亏
(频率只放大幅度:50/50 亏得多、10% 亏得少),大臂任何频率都净赚。所以**按臂大小门控**正是对的信号。

**修复 1:摊销门**(`SPT_JIT_SIDE_MIN_IR`,默认 28)。`record_trace` 在 optimize 之后、codegen **之前**
(让计数与 debug 的 post-opt `ir.ninst` 一致,且被拒的侧 trace **不占代码池**)对侧 trace 判大小:
`ninst < side_min_ir` 即丢弃(`sptir_free`+`free`,返回 NULL)。`maybe_record_side_trace` 收到 NULL 走原有
`aborts++` 分支,几次后拉黑该 PC——于是被门掉的臂**回退到 Phase 2 前的解释器兜底**,无回归。
阈值在 21(亏)与 32(赚)之间取 28;**环境可调**,`SPT_JIT_SIDE_MIN_IR=0` 强制录制每条侧 trace(测试用,
让 difftest/fuzz/valgrind 仍能在小 kernel 上压侧 trace 录制+链接路径)。

**修复 2:拉黑路径 O(1) 化**。`maybe_record_side_trace` 在**每次 trampoline 未命中**都跑(含已拉黑 PC 的稳态,
如被门掉的轻臂)。原来先扫父快照判热度(每次 miss 都扫),再 hot_lookup+拉黑判断。**重排**为:
先做廉价的 hot_lookup + `e->trace` + `e->aborts>=MAX` 拉黑判断,**通过后才扫快照**。
这样被门掉的 PC 稳态是 O(1)(无每-miss 快照扫描),被门臂的代价真正等于 C。

**修复后 B vs C(负优化消除)**:50/50 轻臂 **0.84→~1.0×**(中性、噪声内,回退到 C 兜底)、
偏置轻臂 ~1.0×、中臂 **1.09×**、重臂 **1.23×**(收益保留)。
即 Phase 2 现在对 JIT 自身基线**只增不减**:小臂中性、实质臂净赚。
(注:轻臂场景 C 本身相对解释器仍是 0.75–0.81× 负优化,那是 §10.22 的根 trace 残余、非 Phase 2 引入,
也非 Phase 2 目标;Phase 2 的职责是"别把 C 弄得更差",已达成。)

**验证**:290 ctest(新增重臂 kernel `side_heavy_arm`,默认设置即过门、录侧 trace)+ 76 kernel + 287 全量差分,
**默认设置(轻 kernel 被门、重 kernel 录侧 trace)与 `SIDE_MIN_IR=0`(强制全录)两档都 0 失配**;
`gen_side_store` fuzz 多种子(`SIDE_MIN_IR=0` 强制)0 失配;valgrind(强制侧 trace,重/轻 kernel)0 错误。

**关键教训**:
(1)**做对≠做值**:正确性全绿不代表是收益,新优化必须补 A/B/C 基准(对照 Phase 2 前基线 C),否则可能悄悄装了个负优化。
(2)栈中介链接有固定 per-hand-off 开销,只对**实质大小**的侧 trace 划算;按臂大小门控(per-link 经济性)是对的信号,频率不是。
(3)在 codegen **之前**判门,被拒的侧 trace 不占代码池;计数放 optimize 之后以匹配最终 IR 规模。
(4)每-miss 热路径里,把廉价的拉黑判断放到昂贵的快照扫描之前,让被门控/拉黑的稳态保持 O(1)、等价于兜底。
(5)门控阈值与强制开关都做成环境可调:默认走划算策略,测试用 `SIDE_MIN_IR=0` 仍能压满侧 trace 代码路径。

---

## §10.33 嵌套循环优化(Phase 3a):数据驱动定位 + 内层循环展开

**目标:顶尖 JIT。先测再优化(§10.32 教训),用基准否掉了两个红鲱鱼,定位到真正瓶颈。**

### 性能画像(20M 级 kernel,on-vs-off best-of-4)
标量 4.3×、浮点单累加 4.6×、浮点多累加 9.8×、Horner 21.4×——单循环已很强,浮点驻留不是瓶颈。
**点积 1.75×(最弱)。** 进一步切分(决定性):

| 形态 | 加速 |
|------|------|
| flt_flat 单循环数组 | 4.25× |
| flt_dot_long 嵌套内层=256 | 4.49× |
| flt_dot 嵌套内层=8 | 1.79× |
| **flt_dot_short 嵌套内层=4** | **0.98×(负优化!)** |

### 否掉的两个红鲱鱼(都靠测量,没靠直觉)
1. **数组边界检查消除(BCE)不值得做**:临时禁用变量下标 GETI 的 `GUARD_LE/GUARD_LT`(spt_jit.c ~1499),
   点积 1.75→1.77×(噪声内)。边界守卫是 compare+branch、分支预测命中、CPU 近乎免费。已还原。
2. **浮点寄存器分配不是瓶颈**:多累加(4 个、2 个要 spill)9.8×、Horner 21.4×。SysV 驻留池虽只 {XMM4,XMM5}
   (XMM6-15 caller-saved 可扩,见 codegen ~144),但当前画像下扩它收益甚微。

### 真因:嵌套循环 + 短内层
JIT 只 trace 内层(§10.15 外层 trace 在内层 back-edge abort);每个外层迭代"进入内层 trace→跑几次→
退出(全量 flush)→解释器跑外层 FORLOOP→重入(reload)",进入/退出固定开销摊不到几次内层迭代上。
内层 256 摊得开(4.49×),内层 4 时比解释器还慢(0.98×)——§10.22 式负优化。嵌套数值代码(矩阵、小向量)
是顶尖 JIT 的核心场景,必须解决。

### 修复:内层循环展开(recorder-only,不动 codegen)
外层 trace 录到内层 `OP_FORPREP` 时(`try_unroll_inner_loop`,spt_jit.c),若:
- init/limit/step 都已是 IR 整数常量(`reg_map[A/A+1/A+2]` 均 `SPTIR_KINT`——如内层体内 LOADI 设的字面量边界);
- trip count(按 VM `forprep` 同款公式算,含 step<0 降序)≤ `SPT_JIT_UNROLL_MAX`(默认 16,`SPT_JIT_UNROLL_MAX` 可调,0 关);
- 内层体 `[body_pc, forloop_pc)` 直线(无 JMP/比较/TEST/嵌套循环/调用/返回——见 `unroll_unsafe_op`)且不写控制槽 A/A+1/A+2;

就把内层体重放 `trips` 次(`while(rc->pc<forloop_pc){rec_inst}`,复用 if-conversion 录臂的模式),
每份副本前把 `reg_map[A+2]=KINT(init+k·step)` 设成该次的下标,然后跳过内层 FORLOOP 继续外层体。
整个嵌套塌成一条线性 trace(外层为唯一循环),且**变量下标 a[j] 变成常量下标**(常量折叠 + LICM 提升 → 更优)。
否则回退原行为(`pc+=bx+1`→内层 FORLOOP abort 外层 trace,内层保留自己的 trace)——**永远安全**。

### 一个致命的正确性点(快照一致性)
展开每份副本 k 不只设 idx,还必须把 **`reg_map[A]=count=trips-1-k`(FORLOOP 计数器)、`reg_map[A+1]=step`** 也设成 KINT。
否则副本内某个类型/边界守卫侧退出时,快照 flush 出的 R[A] 是错的(初值而非剩余计数),解释器会用错的计数续跑内层
→ 跑错迭代次数 → 结果错。设对后,副本 k 的守卫侧退出 → 快照恢复 count=trips-1-k、idx=init+k·step → 解释器从内层第 k 次正确续跑。
循环后状态设为 count=0、idx=init+count·step(末次副本的 idx,FORLOOP 退出时不再 bump)。

### 确认的 for-loop 布局(去风险前提)
`for(int j=0,N)`:循环变量 **j 就是 R[A+2](idx),无单独 R[A+3] 用户副本**(IR 显示体直接 SLOAD idx 槽)。
FORPREP 落入体(idx=init),尾部 FORLOOP 递减 count、idx+=step、跳回;**body 跑 count+1 次,idx=init+k·step(k=0..count)**。

### 效果(零回归)
点积 1.79→**6.33×**、内层=4 的负优化 0.98→**5.46×**;内层=256 正确地不展开(超 UNROLL_MAX)维持 4.45×;
单循环/标量/浮点全部维持(4.06/4.61/9.72/21.45×,噪声内)。

### 验证(完整门槛,干净重建后全绿)
292 ctest(新增 `nested_dot`/`nested_short_mix`)· 76 kernel · 287 全量差分 **三档(默认 UNROLL_MAX=16 / =4 / =0 关)**·
手工 16 种嵌套形态(边界 trip=15/16/17、步长 2/3、降序负步长、非零 init、嵌套数组写 SETI、三重嵌套、浮点)× 3 档 0 失配·
新 `gen_unroll_nest` 生成器(内层数组读写/浮点/步长/负步长/跨 UNROLL_MAX 边界)476 例 × 3 档 0 失配·
默认 fuzz(含新生成器)多种子 0 失配· valgrind(点积/短内层/三重+负步长+写)0 错误· `entries==exits` 保持。

**关键教训**:
(1)**先测再优化**:本轮靠基准切分(单循环 vs 长内层 vs 短内层)否掉了 BCE 和浮点寄存器两个看似合理的方向,
   省下了高风险无收益的工作——§10.32"做对≠做值"的姊妹篇是"别优化没量过的东西"。
(2)负优化(0.98×)本身就是违反"trace 不得慢于解释器"硬规的 bug,顺带修掉。
(3)recorder 级展开把变量下标转成常量下标,白嫖了已有的常量折叠/LICM——找对杠杆点,小改动撬大收益。
(4)深层 recorder 改动的正确性核心在**快照一致性**:任何 pin 成常量的循环控制状态,都要保证侧退出时 flush 出的值
   与解释器在该迭代点的真实值逐字节一致。

---

## §10.34 嵌套循环优化(Phase 3a 续):递归展开 + 两个被模糊器抓出的 bug

§10.33 的单层展开只把**最内层**循环展进它的直接父循环。再测画像发现下一个瓶颈:
**定长矩阵乘 matmul_const 仅 1.22×**(即便维度是常量!)。`SPT_JIT_DEBUG` 显示 entries=1.6M、aborted=24、只编译 1 条 trace。

### 根因:展开只做了一层
`for rep: for i: for j: for k: c[i*4+j]+=a[i*4+k]*b[k*4+j]` 四层。k(最内、常量)展进 j,j-trace 编译成功;
但录到 i-loop 时,i-body 含 j-FORPREP,`unroll_unsafe_op` 把 FORPREP/FORLOOP 当不安全 op 直接 bail →
i 不展开 → i-trace 在 j 的 back-edge abort。结果 i、rep 在解释器里跑,每个 (rep,i) 重入 j-trace 1.6M 次,
进入/退出开销淹没了内层 16 次乘法 → 1.22×。**展开必须递归**:含(可递归展开的)内层循环的循环也要能展开。

### 修复 1:递归展开(replay 天然递归)
`unroll_unsafe_op` 去掉 OP_FORPREP/OP_FORLOOP(保留 TFOR* 仍不安全)。展开的 replay 循环 `while(rc->pc<forloop_pc){rec_inst}`
本就会在内层 FORPREP 处经 `rec_inst→OP_FORPREP→try_unroll_inner_loop` **递归调用自己**,所以全常量嵌套自动逐层塌进
最外层、成一条线性 trace。无需 IR 回滚:全常量嵌套在 replay 到内层 FORPREP 时,内层边界已是 KINT(外层体的 LOADI 刚执行过),
递归 try_unroll 成功;只有"含变量边界内层"才会在 replay 中途 abort —— 那本就回退(=旧行为,无回归,且因 abort 黑名单有界)。
尺寸靠 `rec_inst` 的 `inst_count`/MAX_TRACE(4096,跨递归累计)硬封顶,巨型嵌套 abort 回退。
**快照正确性递归成立**:每层 try_unroll 只 pin 自己的控制槽,内层 replay 不动外层槽,所以任意层的守卫侧退出时,
快照能恢复**所有层**的 count/idx(逐字节对齐解释器)。效果:matmul_const **1.22→7.9×**(entries 1.6M→491,aborted 24→0),
matmul_var/nest_var(变量边界)正确地不变(1.2/1.5×),单层/标量/浮点零回归。

### 修复 2:MMBIN 误报(放开 FORPREP 后立刻撞上)
matmul 还是不展开,debug 显示 try_unroll 在 "op 47 (MMBIN) writes control slot da=6" 处 bail。
MMBIN(元方法回退,跟在算术后、数值快路径**被跳过**)的 A 字段是**操作数**(这里=循环变量 i 的槽 R[6]),不是目标。
我的"控制槽写"检查把它误判成"重新赋值循环变量"。修法:加 `unroll_writes_dest_A(op)` —— MMBIN/MMBINI/MMBINK(A 是操作数)、
SETI/SETTABLE/SETFIELD/SETTABUP/SETUPVAL(R[A] 是表/upval 基址、不重新赋值 R[A])返回 0,其余返回 1。控制槽写检查只对真正以 A 为目标的 op 生效。

### 修复 3:读改写同数组的 codegen bug(新生成器 `gen_recursive_nest` 抓出)—— 安全回退
新加的递归嵌套生成器立刻抓到一个**真 correctness bug**:`a[i+j+k]=a[i+j+k]+1`(直方图,索引碰撞)结果错。
逐步缩小后的决定性事实:
- **纯写碰撞** `a[i+j]=7` 正确;只有**读改写(RMW)同一元素** `a[c]=a[c]+...` 经碰撞常量索引才错。
- matmul(读 a/b、写**不同**数组 c,无元素别名)不受影响 —— 所以 matmul_const 通过。点积(只读)不受影响。
- 单层同字面索引 RMW(`for j:a[3]=a[3]+1`,**连续**写,任意次数)正确;只有**非连续(交错)**的同元素 RMW 才错。
- 症状:碰撞重的元素(每 rep 被读改写 ≥8 次)最终值≈1 个 rep 的量(GETI 像是读了入口缓存值、没每轮重读内存);
  碰撞轻的元素恰好多算 1 个 rep。阈值约"单元素碰撞 8 次"。post-opt IR 显示绝大多数 GETI/SETI 被去掉(只剩约 18/128)。
- 排除了 CSE(GETI CSE 在 has_table_write 时正确禁用,行 627-635 扫 SETI/SETFIELD;CSE 64 窗口、guard_dedup 128 窗口都只动守卫/纯算)。
  根因落在**大展开 trace + 同数组 RMW 时的 codegen 驻留/spill 复用**,未精确定位(需深挖 codegen)。

按项目纪律(correctness 第一;"正确但不值也要权衡"),采取**精准安全回退**而非硬啃深层根因:
**展开体若对同一数组既读又写就不展开**。检测靠数组基址寄存器:GETI/GETTABLE/GETFIELD 读 R[B]、SETI/SETTABLE/SETFIELD 写 R[A],
有寄存器同时出现在两边 → 回退。这精确避开直方图/RMW,同时**保住 matmul(c≠a/b)、点积(只读)、标量累加**的展开收益。
同数组 RMW 循环回退到内层 trace(正确、约原速)。回归 kernel:`test/15_jit/kernels/nested_histogram.spt`。

### 验证(完整门槛,reconfigure 后全绿)
293 ctest(新增 `nested_histogram`)· 78 kernel · 289 全量差分 **三档(16/4/0)**· 10 种递归嵌套边界 × 4 档 0 失配·
`gen_recursive_nest`(三/四重、MMBIN 控制槽、嵌套数组读/写碰撞、常量外+变量内回退)477 例 × 3 档 0 失配·
默认 fuzz(全 GENS)多种子 0 失配· valgrind(histogram 回退 + matmul 展开)0 错误· `entries==exits` 保持。

**关键教训**:
(1)**每修一个 bug 类就加一个回归生成器** —— `gen_recursive_nest` 正是为递归展开补的,立刻抓到了 RMW codegen bug,印证了价值。
(2)放开一个限制(FORPREP)会立刻暴露被它掩盖的下游误报(MMBIN 把操作数当目标)——"操作数 vs 目标"必须按 opcode 精确区分。
(3)缩小 correctness bug 用**正交切分**(纯写 vs 读改写、连续 vs 交错、同数组 vs 不同数组、碰撞次数阈值)定位到最小触发面,
   再决定"修根因 vs 精准回退"。当根因在深层且范围窄,精准回退(保住大收益、放弃小众场景)是符合纪律的选择。
(4)展开+codegen 的交互在**大 trace + 别名写**上才暴露——小负载全绿不代表 codegen 在重度展开下也对(§10.32 的"做对≠做值"延伸到"小对≠大对")。

---

## §10.35 变量边界内层循环:带守卫的推测展开(Phase 3a 续 2)

§10.33/§10.34 的展开都要求内层 `init/limit/step` 是**编译期整数常量**(KINT)。再测画像,变量边界短内层成了仅存的数值弱点:
matmul_var(**变量维度**矩阵乘——最典型的数值基准)仅 1.26×、nest_var 1.44×、gemv_var 1.95×(长内层 stencil_var 3.41× 已摊销、不弱)。
这些循环写成 `for j = 0, N-1`,N 是运行时才知道的不变量,展开器看到 limit 非 KINT 直接 bail。

### 思路:LuaJIT 式按观测值特化 + 守卫(比 P3b 双层 trace 风险低)
内层 trip 不是常量,但对一次热运行而言 N 是**循环不变量**(矩阵维度,设一次、不改)。于是在录制内层 `FORPREP` 时:
读出 N 的运行时值、按它算出 trip、**发一个运行时守卫把边界钉成该值**、再按常量 trip 展开。
**关键正确性洞察:正确性完全落在守卫上**——展开用的值只是个"猜测",守卫保证"边界确实等于所猜"才走展开码;
若运行时边界不同(守卫失败),就**侧退出**到 `FORPREP` 快照,解释器用真实边界重跑内层。**所以猜错也正确**(只是没加速)。
因此 eval 的精度只影响**性能**、不影响正确性。

### 三块实现(spt_jit.c,均在 try_unroll_inner_loop 前/内)
1. **`eval_invariant_int(rc, ref, *out)`**:递归把 IR ref 求成整数。`KINT`→aux;`SLOAD(slot)`→**当且仅当 `reg_map[slot]` 仍指向该 SLOAD**
   (即该槽未被重新赋值——尤其排除 PHI/循环归纳变量)且活栈 `s2v((ci->func.p+1)+slot)` 是整数,取其活值;`ADD/SUB/MUL`→两子表达式都可求则折叠;
   其余→失败。读活栈对不变量 SLOAD 在**任意递归展开深度**都正确,因为不变量值不随执行点变(`N-1` 在 i/j 各层重放里都求得同一值)。
2. **`emit_pin_guard(ir, ref, val, snap)`**:把 ref 钉成 val。`GUARD_EQ` **枚举里有但 codegen 主 switch 未实现**(只有 GUARD_LT/LE/ULT/真值),
   故用**两个 GUARD_LE**(`GUARD_LE(op1,aux)`=`R[op1]<=R[aux]`):`ref<=val`(op1=ref,aux=KINT) 且 `val<=ref`(op1=KINT,aux=ref),合起来 ==。两守卫共享 snap。
3. **try_unroll 边界段改写**:init/limit/step 各自——是 KINT 则直接用(无守卫);否则 `eval_invariant_int` 求值或 bail。
   做完体安全扫描、**钉控制槽之前**,对每个非 KINT 边界 `sptir_snapshot(FORPREP)` + `emit_pin_guard`。
   **全常量循环不发任何守卫,与 §10.33/§10.34 的常量展开逐字节相同**(零回归)。

### 为什么不会负优化(PHI 排除是关键)
归纳变量在 IR 里是 **SPTIR_PHI**(非 SLOAD)。所以 `eval_invariant_int` 对"边界=外层循环变量"的情况求值失败 → bail → 不推测展开。
于是**三角循环 `for j = 0, i`** 不会被推测成"每轮守卫失败"的负优化。实测三角循环反而 6.18×——因为外层 i 是常量边界(0,7)、
被常量展开 pin 成 KINT,内层 limit=i 随之变常量,走的是**常量路径**(不是推测路径)。而真正每轮都变的边界(如 `for j=0,lens[i]`,limit 是 GETI):
GETI 不被 eval 接受 → 不推测 → 该例 0.95×,但**这是预存的短变化内层固有成本**(特性开/关都 0.95×,见验证),非本改动引入。

### 效果(全部正确)
matmul_var **1.26→6.06×**(变量维度矩阵乘完全塌进一条 trace,`guard_fail=0`——N 不变,守卫恒过)、nest_var **1.44→8.0×**、
gemv_var **1.95→5.5×**;常量路径(matmul_const 7.9×、点积 6.3×)与单循环/标量/浮点全维持。回归 kernel `nested_var_bound.spt`。

### 验证(完整门槛,全绿)
294 ctest(新增 `nested_var_bound`)· 79 kernel · 290 全量差分 **三档(16/4/0)**· 变量边界嵌套电池 7 例(nest_N、nest_N_direct=直接 SLOAD 边界、
matmul_N、var_init、var_step、三角、N-2)× 3 档 0 失配· **新生成器 `gen_speculative_nest`(变量 limit/init/step、两层、标量+数组读)597 例 × 3 档 0 失配**·
全 GENS 多种子 1400 例 0 失配· valgrind(matmul_var、nest_var)0 错误· `entries==exits` 且 `guard_fail=0`。

**关键教训**:
(1)**对变量 trip,守卫(而非 eval 精度)是正确性的根**——猜错就侧退出,所以可以大胆按观测值特化。
(2)缺一个 codegen 算子(GUARD_EQ)时,常能用已实现的算子(两个 GUARD_LE)等价合成,避免动 codegen(降风险)。
(3)**不变量分析(叶子限 KINT/SLOAD、排除 PHI)同时服务正确性的对象选择和负优化的规避**:它既保证 eval 读到稳定值,又把"边界=循环变量"的负优化场景挡在门外。
(4)同一能力对常量与变量边界要走不同路径,但常量路径必须**字节不变**(回归护栏):新增推测仅在"非 KINT"分支生效。
(5)再次印证§10.32:先测量后优化——画像把有限改动精确投到 matmul_var 这个最高价值点。

---

## §10.36 map 读取:内联哈希槽 GETFIELD(覆盖扩展,首个非数值能力)

数值路径已强(4–21×)后重新画像,发现三类**非数值热循环 JIT 完全不编译**(每次 abort、还轻微负优化):
**map 访问** `m["key"]` abort 于 OP_GETFIELD(0.88×)、**for-each** `for x:pairs(l)` abort 于 OP_TFORCALL(0.93×)、
**方法调用** `a.add()` abort 于 OP_SELF(0.98×)。这三类是"完整 JIT"的主要短板。先攻**最自包含、价值最高**的 map 读取(常量字符串键)。

### 为什么走内联哈希槽,而不是调 luaH_getshortstr
codegen **没有 C 调用机制**(GETI 是纯内联内存访问,无 `call`;引入通用 C 调用要处理 SysV ABI/栈对齐/调用者保存寄存器/调用中 GC,风险高)。
故采用 **LuaJIT 式内联哈希槽快速路径**(纯内存访问 + 守卫,和 GETI 同类):常量短字符串键的 hash 编译期已知,
运行时算主位置、守卫该位置的节点键==我们的键、命中则载值。

### 实现
**布局**(offsetof 实测):`sizeof(Node)=24`;Table.node=32、lsizenode=11;Node 值位 i_val.value_=0、值标签 tt_=8、key_tt=9、next=12、key_val=16;TString.hash=12。
主位置 `slot = key->hash & (sizenode-1)`,`sizenode = 1<<lsizenode`(即 luaH_getshortstr 的 lmod)。短字符串驻留 → 键指针比较精确;键标签 = `ctb(LUA_VSHRSTR)=68`。
- **recorder**(OP_GETFIELD,替换原 abort stub):容器须 SPTT_TAB、键须 K[C] 短字符串。读活表算主位置,**仅当键在主位置**(`keytt(n)==ctb(VSHRSTR) && keyval(n).gc==key`)且值类型可载时录制;
  否则 abort(codegen 会恒侧退出、不值得)。发 `SPTIR_GETFIELD(et, bref, aux=key)`、置 GUARD、设 reg_map/type。
- **codegen**(新 `case SPTIR_GETFIELD`,GETI 之后):`key=(TString*)aux`、`khash=key->hash`(codegen 时读)。
  `gen_load RAX=t`;`movzx ecx,[rax+lsizenode]`;`mov rdx,[rax+node]`;`mov rax,1; shl rax,cl; sub rax,1`(掩码);`and rax,khash`(slot,掩码仅低位故精确);
  `imul rax,rax,24; add rdx,rax`(&node[slot]);**键守卫 1** `movzx eax,[rdx+9]` cmp 68 jne exit;**键守卫 2** `mov rax,[rdx+16]; mov rcx,key; cmp jne exit`;
  **值类型守卫** `movzx eax,[rdx+8]` cmp 预测标签 jne exit;`mov rax,[rdx+0]` 载值。**仅用 RAX/RCX/RDX,RA 安全**。加入 codegen 可处理白名单。

### 效果与限制
map_access **0.88→4.89×**;m2(map 值参与 float RMW 写 list)、m4(map 值做 list 索引)均编译+正确+valgrind 0 错误。
**限制(性能,非正确性)**:① 仅主位置键走快速路径——**碰撞链上的键**当前 abort 回退(整条循环 trace 若含一个链式键就不编译);
② 字符串 hash 种子**每次运行随机**(`luaL_makeseed`),故同一键是否在主位置因运行而异,即 map 读取能否 JIT 不确定——但**输出永远正确**(守卫/回退),差分门槛稳定。
链式键的健壮处理(codegen 跟 `next` 链:或生成回边循环、需局部标签设施;或定深展开)风险更高,列为独立后续单元。

### 验证(完整门槛,全绿)
295 ctest(新增 `map_read`)· 80 kernel · 291 全量差分 ×双档 · **新生成器 `gen_map_access`(int/float map、多键、主位置命中 + 碰撞回退、map 值做 list 索引)249 例 ×双档 0 失配** ·
全 GENS 多种子 1100+ 例 0 失配 · valgrind(m2/m4)0 错误 · map_access entries==exits、guard_fail=0。

**关键教训**:
(1)**画像要覆盖非数值类别**——数值全绿不代表 JIT"完整";map/for-each/方法调用三类此前 100% abort(且轻微负优化)是真实短板。
(2)缺底层设施(C 调用)时,**换一条不需要它的等价路径**(内联哈希槽 vs C 调用)能以更低风险拿下能力——和 §10.35 用两个 GUARD_LE 合成 GUARD_EQ 同理。
(3)**正确性靠守卫,快速路径可以只覆盖常见情形**(主位置键),不常见情形(碰撞链)安全侧退出/abort 即可——先拿到主位置的 4.89×,再按需健壮化。
(4)种子随机导致"是否加速"不确定,但只要正确性不依赖它,门槛就稳定——再次印证正确性与性能解耦。

---

## §10.37 map 写入:内联哈希槽 SETFIELD(免 GC 屏障,与 §10.36 配对)

紧接 §10.36 的 map 读取,补上**写**——这样 `m[k] = m[k] + x`(读改写同键:计数器/直方图桶/累加器)这类热循环能整体 JIT。
与 GETFIELD 完全对称,复用同一套内联哈希槽寻址(算主位置、守卫节点键==键),只把"载值"换成"存值"。

### 两个降风险的关键限制
1. **只写 int/float 值 → 无需 GC 写屏障**。写屏障(luaC_barrier)只在把**可回收**值(字符串/表/函数)存进表时才需要——否则 GC 可能回收仍被引用的对象 → 崩溃/损坏。
   int/float 不引用 GC 对象,故免屏障。这正是 SETI 用的同款限制。可回收值的写交给解释器。
2. **只更新已存在的主位置键**。录制时检查键已在其主位置(`keytt(n)==ctb(VSHRSTR) && keyval(n).gc==key`);
   缺失键需插入(可能 rehash/分配)、链式键不在算出的槽、表被 resize 会移动键——这些都侧退出回解释器去存。
   故 codegen 只覆盖"覆写一个已存在主位置键的值",纯内存写、无分配。

### 实现
- **recorder**(OP_SETFIELD `R[A][K[B]] := RK(C)`,替换 abort stub):容器 SPTT_TAB、键 K[B] 短字符串、值 INT/FLT(否则 abort);
  读活表确认键在主位置(否则 abort)。发 `SPTIR_SETFIELD(SPTT_NIL, op1=表, op2=值, aux=key)`、置 GUARD + snapshot(键守卫的退出)。
- **codegen**(新 `case SPTIR_SETFIELD`,GETFIELD 之后):同 GETFIELD 算 &node[slot] + 两个键守卫;然后 `gen_load RAX=值位`、
  `mov [rdx+0], rax`(node 值)、`mov byte[rdx+8], tag`(node 值标签,tag=spt_type_to_tag(值类型))。**无屏障**。仅 RAX/RCX/RDX,RA 安全。加入白名单。
- **IR opt 早已接线**:DCE 把 SETI/**SETFIELD** 列为根(store,不可消除);表写检测把 SETFIELD 当写 → **禁用含写时的 GETI/GETFIELD CSE**
  (故 `m[k]=m[k]+x` 的读不会跨写被合并——正确性所需)。这些此前已存在(IR 早定义了 SETFIELD 算子),无需改。

### 效果(全部正确)
map_write(`m[k]=m[k]+1` 等)**4.03×**(单键可靠走快速路径)、float 累加 4.x×;w2(map 写在嵌套循环、值来自 list)编译+正确。
限制同 §10.36(主位置键、hash 种子随机故是否 JIT 因运行而异,但输出永远正确;碰撞键 abort 但**已加黑限界**,非持续负优化)。

### 验证(完整门槛,全绿)
296 ctest(新增 `map_write`,单键可靠 JIT:recorded=1/compiled=1/aborted=0/entries==exits/guard_fail=0)· 81 kernel · 292 全量差分 ×双档 ·
**新生成器 `gen_map_write`(rmw_const/two_keys/nested_arr、int+float)199 例 ×双档 0 失配** · 全 GENS 多种子 600+ 例 0 失配 · valgrind(map_write/w1/w2)0 错误。

**关键教训**:
(1)**用"非可回收值"限制绕开 GC 屏障**是写类操作的低风险切入——免去屏障的 ABI/时机/正确性风险(和 SETI 同策),先拿下 int/float 写。
(2)**"只覆写已存在键"避开分配/rehash**——写的快速路径只做纯内存覆写,插入交给解释器侧退出;再次"快速路径覆盖常见情形 + 守卫兜底"。
(3)读写对称复用同一寻址与守卫,边际成本低——铺好 GETFIELD 后 SETFIELD 几乎是镜像。
(4)IR 层(DCE 根、表写禁 CSE)在算子定义时就为读写都接好线,印证"先定义完整 IR 语义、再分别实现 recorder/codegen"的分层好处。

---

## §10.38 map 读写健壮化:碰撞链遍历(消除主位置限制与种子依赖)

§10.36/§10.37 的 GETFIELD/SETFIELD 只走**主位置**:碰撞链上的键录制时 abort 回退,且因 `luaL_makeseed` 随机、
键是否落在主位置因运行而异 → map 能否 JIT 不确定(虽输出恒正确)。多键 map(如 5 键)常因某键碰撞而整条循环不编译。
本节把快速路径升级为**完整链遍历**,与 `luaH_Hgetshortstr` 逐字节对应。

### 实现(codegen 现成的标签/重定位设施使其低风险)
codegen 早有 `sptasm_newlabel/place/jmp/jcc` + 重定位补丁(CMPSET 等已用局部前向标签),故生成"带回边的链遍历循环"风险低。
新增共享辅助 **`gen_hash_find(cg, table_ref, key, exlbl)`**(GETFIELD/SETFIELD 共用),发射:
```
  RAX=t; RCX=lsizenode; RDX=t->node; RAX=(1<<cl)-1 & khash(主位置slot); RDX=&node[slot]
loop_top:
  movzx eax,[rdx+key_tt]; cmp ctb(VSHRSTR); jne advance
  mov rax,[rdx+key]; mov rcx,key; cmp; je found
advance:
  mov eax,dword[rdx+next]; test; jz exit        ; nx==0 → 未找到 → 侧退出
  movsxd rax,eax; imul rax,24; add rdx,rax; jmp loop_top   ; n += (有符号)nx
found:  (RDX = 命中节点)
```
`next` 是**有符号** int(偏移,可负),故 `movsxd`(raw `48 63 C0`)符号扩展后再 `imul 24` 前进。链终止于 `nx==0`(Lua 表保证无环 → 必终止)。
- **GETFIELD**:`gen_hash_find` 后接值类型守卫 + 载值。
- **SETFIELD**:`gen_hash_find` 后存值位 + 标签(仍限 int/float 免屏障)。
- **recorder**:两者改为**录制时走同样的链**(复刻 luaH_Hgetshortstr)——找到键才录(GETFIELD 顺便预测值类型;SETFIELD 确认键存在),
  **仅真正缺失才 abort**(原"非主位置即 abort"取消)。codegen 运行时重走链,故键在录制/运行间移动仍正确解析或侧退出。

### 效果
之前因碰撞 abort 的多键 map(m1=5 键、m3=嵌套)现在 **compiled=1、aborted=0、确定性编译**(连跑 5 次均编译,不再随种子);
正确 + valgrind 0 错误。**主位置常见情形零回归**:主位置键 `loop_top` 首轮即命中跳 found、走 0 链步,map_access 维持 **4.90×**(对比 4.89×)。
m1(5 键)4.76×、map_write 4.56×。**剩余的真·限制**仅:写仍限 int/float(可回收值需 GC 屏障)、写仍只更新已存在键(插入侧退出)。

### 验证(完整门槛,全绿)
296 ctest · 82 kernel · 293 全量差分 ×双档 · 全 GENS(含 `gen_map_access`/`gen_map_write`,均用至多 6 键 → 现覆盖链遍历路径)多种子 600+ 例 0 失配 ·
valgrind(m1/m3/map_write)0 错误 · entries==exits、guard_fail=0。

**关键教训**:
(1)**先用最小快速路径(主位置)破冰、再按需健壮化(链遍历)**——上轮拿到主位置的 4.89×验证了整条数据通路,本轮只补链遍历这一段,风险被前一步大幅压低。
(2)**复用宿主语义的精确实现**(逐字节对应 luaH_Hgetshortstr)是哈希/数据结构类 codegen 正确性的最稳来源——包括"next 是有符号偏移""nx==0 终止"这些易错细节。
(3)codegen 已有的局部标签/重定位设施让"生成带回边循环"从高风险降为常规——动手前先确认底层设施(本轮)与缺设施时换等价路径(§10.36)是对偶的两手。
(4)健壮化同时消除了"种子依赖致是否 JIT 不确定"这一可用性瑕疵——正确性本就解耦,现在性能也变确定。

---

## §10.39 同数组 RMW 展开 bug:诊断进展(根因定位,仍以 bail 规避)

§10.34 记录的"展开体对同一数组既读又写(如直方图 `a[i+j+k]++`)→ 不展开(bail)"是**安全规避一个未定位的 codegen/多-trace bug**。本轮做了**受控复现与诊断**(临时关 bail),取得关键进展,但确认根因较深、仍维持 bail。

### 复现(临时关 bail)
`list<int> a=[..10..]; for rep=0,50000: for i=0,3: for j=0,3: for k=0,3: a[i+j+k]=a[i+j+k]+1;`(三层各 0..3,idx=i+j+k∈0..9,大量碰撞)。
- **小 rep(≤5)或 HOT=1 不触发**(输出正确);**大 rep + HOT=8 才触发**——说明非每轮稳态错误,而是与 trace 生命周期/侧 trace 相关。
- 逐单元对比(rep=50001):**低索引 a[0..3] 多算 count(idx)(+1/+3/+6/+10)、a[9] +1;高索引 a[4..8] 的写几乎全丢**(只剩录制那一轮 ≈ count(idx),编译 trace 根本没更新它们)。
  即**编译 trace 丢弃了对一部分索引的 SETI**。

### 诊断发现
- **不是 GETI CSE**:`try_cse` 在 `has_table_write` 时对 GETI 返回 0(已禁用),确认无误。
- **是多 trace 组合**:该 case 关 bail 后 `recorded=4`(主 trace + 3 条侧 trace),四条 trace 的 SETI 数分别 **5 / 20 / 80 / 64**。
  本应是**一条 64-SETI 的递归展开 trace**;却被拆成不同展开深度的多条 + 侧 trace(`entries=491`、`guard_fail=0`,即非守卫失败的退出)。
  bug 出在这些**不同展开深度的 trace + 侧退出/侧 trace 的组合**:某条 trace 覆盖一部分索引的写、另一条覆盖另一部分,边界处写丢失/重复。
- bail 打开(正常)时此 case 不展开(走内层 trace)→ 没有这套多-trace 组合 → 正确。故 bail 是对症的安全网。

### 结论 / 下一步(未来专项)
根因 = **同数组 RMW 在递归展开 + 侧 trace 组合下的写覆盖不一致**(非单条 trace 的 CSE/DCE)。修复方向(择一):
(a)定位为何该嵌套被拆成 4 条而非 1 条(理想递归展开应单条),消除多余侧 trace;
(b)或在 SETI codegen / 快照里保证同数组多写的顺序与持久化在跨 trace 边界一致。
风险中等偏深,**应作为独立专项**(带本节复现脚手架)。当前继续以 bail 规避(正确、零回归)。

**关键教训**:(1)**一个看似 +2 的小偏差可能掩盖大面积错误**——必须逐单元、足够大 rep 复现才看清(a[0]+a[9] 恰好 +2,实则 a[4..8] 几乎全丢)。
(2)bug 只在"大 rep + 真实 HOT"下现形 → 复现要贴近真实 trace 生命周期(侧 trace 只在足够热时产生)。
(3)诊断先排除简单嫌疑(CSE)、再顺 `recorded=N`/SETI 计数定位到"多 trace 组合"这一层,避免在错误层面空转。
(4)**深 + 窄的 bug 维持精确 bail、记录复现脚手架、留待专项**,是比"带病修"更稳的工程选择(与 §10.34 一致)。

---

## §10.40 调研:C 调用机制可行性 + math 函数加速机会(下一里程碑,本轮未实现)

本轮系统排查了剩余高价值缺口,确认**下一个里程碑是 C 调用机制**,并定位其最佳首用例与可行路径(为专项实现铺路)。

### 排查结论
- **for-each(OP_TFORCALL)**:SPT 用改过的 generic-for——TFORCALL 经 `luaD_call` **真正调用迭代器**(`pairs(l)`→`(luaB_next,l,nil,nil)`,迭代器还被当 receiver 传)。要 JIT 须 C 调用设施或特化 luaB_next/ipairsaux(后者需暴露 static C 函数地址 + 逐字节复刻 lua_next 语义,易错)。
- **POW 是死路**:SPT 的 `^` 运行时是**整数 XOR**(`2^10=8`、`3^2=1`、非整数报错),虽 parser 写 OPR_POW、lvm 走 luai_numpow。`math.pow` 未在 math 表注册可调形态。故 SPT **无可用指数运算符**,"为 libm pow 建 C 调用"无意义。
- **math 函数可用且高价值**:`math.sqrt(16.0)=4.0` 正常,math 库齐全(sqrt/sin/cos/tan/exp/log/abs/floor/ceil…全是一元 double→double 的 libm 包装),数值循环极常见。**但** `math.sqrt(x)` 编译为 **OP_SELF(op21)+ C函数 CALL**(非纯 CALL);OP_CALL 录制仅内联 Lua 闭包(`ttisLclosure` 否则 abort)。当前干净回退(输出一致)。

### C 调用机制可行性(已确认,降险)
- **基础设施寄存器 R12=L / R13=ci / RBX=base / R14=k 是被调用者保存**(序言 push)→ C 调用按 SysV ABI 自动保留,调用后无需重载。
- **RA 池含 R8-R11/RSI/RDI(调用者保存)** → C 调用会破坏其中的循环值。安全方案:**对含调用的 trace 禁 RA**——`ra_analyze` 行 254 已有 `if(!ra_op_is_safe(op)) return;`,新调用类 op **不加入 ra_op_is_safe** 即自动禁 RA(走 spill-everything,值都在栈、跨 op 无寄存器存活,调用只破坏 scratch)。
- **栈对齐简单**:序言 8 个 push(64B)+ `sub rsp,frame_size`(16 对齐),trace 体内 RSP 恒 **≡8 mod 16** → C 调用前 `sub rsp,8` 即 16 对齐、调用后 `add rsp,8`。
- **GC 安全**:libm 数学函数是叶函数(无 GC/无 Lua 回调/无 longjmp)→ 无栈一致性顾虑(首用例最稳)。for-each 的迭代器调用会触发 GC → 需在调用点把快照 spill 成合法 Lua 栈(更复杂,后续)。

### 推荐下一里程碑实现顺序(专项)
1. **一元 math 函数(sqrt/sin/cos/exp/log/…)**:首用例,叶函数无 GC。需:(a)在 lmathlib 暴露 "C函数指针→libm指针" 映射(把对应 math_* 去 static 或加查表函数);(b)录制识别 **OP_SELF+CALL** 到已知一元 math 函数,发新 IR(如 `SPTIR_FMATH(op1=arg, aux=libm_ptr)`);(c)codegen:`gen_load_xmm XMM0=arg; sub rsp,8; mov rax,libm_ptr; call rax; add rsp,8; 存 XMM0`;(d)新 op 不入 ra_op_is_safe(自动禁 RA);(e)处理 codegen 接受门(本轮未定位——gen_inst default 仅 break,故必有上游门拒未支持 op 致回退,需找到并放行新 op);(f)valgrind 充分验证(ABI 敏感)。
2. 之后复用该 C 调用设施做 **for-each**(迭代器调用 + 调用点快照 spill 保证 GC 安全)与**方法调用**(OP_SELF + Lua/C 调用帧)。

### 本轮产出
排除死路(POW)、根因深 bug(RMW §10.39)、确立并降险下一里程碑(C 调用 + math)。代码保持清洁全绿(296 ctest / 82 kernel / 全量差分双档 / fuzz)。无新特性上线——SPT 无"纯简单 C 调用"用例(math 带 OP_SELF、for-each 带 GC/协议),C 调用须与其一同建,属实质性专注工作,不在长调研末仓促上线(遵循正确性优先、避免半成品不稳)。

---

## §10.41 GETTABUP codegen:循环内读全局 / 库表(C 调用里程碑 Unit 1)

C 调用里程碑(§10.40)的第一个独立单元。GETTABUP(`R[A]=UpVal[B][K]`,即全局读 `_ENV["g"]` 或库表 `_ENV["math"]`)此前 recorder 发 **SPTT_ANY** → 下游 ADD 等因类型未知 abort,故循环内读全局整体回退。本节让 GETTABUP **类型预测 + codegen**,本质是"ULOAD 取 _ENV 表 + GETFIELD 式哈希查找",直接复用 §10.38 的 `gen_hash_find`。

### 实现
- **recorder**(OP_GETTABUP):限顶层帧(`frame_base==0`;内联帧上值不同)。取运行闭包 `clLvalue(s2v(ci->func.p))`、上值表 `cl->upvals[B]->v.p`(须 table)、键 `K[C]`(须短串)。**走与 GETFIELD 同样的碰撞链**确认键存在并预测值类型(INT/FLT/STR/ARR/TAB,否则 abort)。发 `ULOAD(b)` + `GETTABUP(et, op1=ULOAD ref, aux=key)` + GUARD + snap。
- **codegen**(SPTIR_GETTABUP):`gen_hash_find(op1=ULOAD ref, key)` → 值类型守卫 → 载值。与 GETFIELD 唯一区别:表来自 ULOAD ref(非寄存器操作数)。运行时动态查找 + 类型守卫保证正确(全局被改类型/缺失 → 侧退出),**无需烘焙表指针**。
- GETTABUP 不在 `ra_op_is_safe` → 这类 trace **自动禁 RA**(值在 spill 槽,ULOAD 结果由 gen_hash_find 的 gen_load 读出)。GETTABUP 带 GUARD 标志 → DCE 保留;是读非写,不驱动 has_table_write。

### 效果
循环内读全局现可 JIT:`s=s+g`(int)**3.98×**、`s=s+gf*gi`(float 多全局)**2.57×**(每轮两次哈希查找 + 无 RA)。这也是 **math 函数加速的前置**(`math.sqrt` 经 GETTABUP 取 math 表)。

### 验证(完整门槛,全绿)
**297 ctest**(+kernel `test/15_jit/kernels/global_read.spt`,混 int/float 全局,1344336/160040.0)· 82+ kernel · **294 全量差分** ×双档 · 新增 fuzz 生成器 `gen_global_read`(int/float 全局、嵌套组合)200 例 0 失配,全 GENS 多种子 600+ 0 失配 · valgrind 0 错误 · entries==exits、guard_fail=0。

### 下一单元(Unit 2):math 函数 C 调用
有了 GETTABUP,`math.sqrt(x)` 仅差:(a)识别 SELF 取到的函数是已知一元 math C 函数(需 lmathlib 暴露 C函数→libm 映射,math_* 现为 static);(b)新 IR `SPTIR_FMATH(arg, libm_ptr)` + codegen 发 C 调用(`gen_load_xmm XMM0=arg; sub rsp,8; call libm; add rsp,8; 存 XMM0`,体内 RSP≡8 mod 16 已验);(c)FMATH 不入 ra_op_is_safe(自动禁 RA);(d)注意 SPT 改过的调用约定:`math.sqrt(x)` 经 SELF 把接收者作 arg1、真参在 arg2,math_* 读 `luaL_checknumber(L,2)`。

**关键教训**:把大特性(C 调用 + math 习语)**拆成可独立验证的单元**——Unit 1(GETTABUP,无 C 调用、纯哈希查找复用)先落地、全绿、且自身有价值(全局读),把 Unit 2(真正的 C 调用)的前置和风险都先清掉。复用 `gen_hash_find`(§10.38)使 GETTABUP codegen 近乎零新增风险。

---

## §10.42 math 函数 C 调用:SPTIR_FMATH + GUARD_CFUNC(C 调用里程碑 Unit 2 ✅ 阶段达成)

里程碑的核心单元:让循环内的一元 math 函数(`math.sqrt/sin/cos/tan/exp/asin/acos`)经**直接 libm C 调用**编译。这是 JIT **首个 C 调用机制**——此前任何需要调用 C 函数的路径(math、for-each、方法)都死路。

### 习语与约定
`math.sqrt(x)` 编译为 `GETTABUP(_ENV["math"]) → SELF(R[A]=math["sqrt"], R[A+1]=math 接收者) → CALL`。SPT 改过的调用约定:**接收者作 arg1、真参在 arg2**,`math_*(L)` 读 `luaL_checknumber(L,2)`、用 `l_mathop(fn)` 直调 libm。math 函数经 `luaL_newlib`(nup=0)注册 → 是**轻量 C 函数 LUA_VLCF**(非 C 闭包),值即 `lua_CFunction` 指针。

### 实现(4 部件)
1. **lmathlib.c** 暴露非 static `spt_jit_unary_math(lua_CFunction)→double(*)(double)|NULL`,映射 7 个**严格一元、恒返回 float** 的函数(排除变参的 atan/log、返回 int 的 floor/ceil/abs)。math_* 是 file-static,故映射必须住在 lmathlib。
2. **SPTIR_FMATH**(op1=float 参, aux=libm 指针, 结果 float):codegen 发 C 调用 `gen_load_xmm XMM0=arg; sub rsp,8; mov rax,libm; call rax; add rsp,8; 存 XMM0`。体内 RSP≡8 mod 16,一次 `sub rsp,8` 对齐到 16。
3. **SPTIR_GUARD_CFUNC**(op1=表 ref, op2=KPTR(方法键), aux=期望 fn 指针):正确性锚点——运行时 `gen_hash_find` 查方法键、守卫 `值标签==LUA_VLCF && 值位==期望 fn`,否则侧退出。**一个守卫即足够**:无论 math 表或 sqrt 字段如何被改,只要解析出的函数 ≠ 期望即侧退出(若解析仍是 math_sqrt 则调 libm 正确,与表怎么来无关)。
4. **recorder**:OP_SELF 从 abort 组拉出专门处理(限顶层帧)——发 GUARD_CFUNC + 设 `pending_cfn_{libm,slot}`;OP_CALL 开头若 `pending_cfn_slot==frame_base+a` 则要求 `b==3 && c==2`、取 R[A+2] 真参(int 则插 TOFLT)、发 FMATH→R[A]、类型 FLT。两者都不入 `ra_op_is_safe` → 自动禁 RA。

### 关键 BUG(本节最重要的教训)
OP_SELF 起初读 **live R[B]** 取 math 表 → `live R[8] not table` abort。根因:循环体内 `R[8]` 被 **CALL 结果覆盖**(`R[8]=sqrt 值`),录制发生在回边,live R[8] 是上轮的浮点结果而非 math 表。**修复:从稳定的 _ENV 源重新解析**——经 SELF 的 `bref` 要求是 `SPTIR_GETTABUP`,从其 ULOAD 取上值索引、从 GETTABUP.aux 取库键("math"),读 `cl->upvals[idx]->v.p`(=_ENV,稳定)→ 查 "math" → 查 "sqrt"。新增 `rec_table_getstr(Table*,TString*)` 链遍历辅助。这把 math 习语限定为**全局/库表**(`math` 是全局 → GETTABUP),正好覆盖 math.*。运行时 GUARD_CFUNC 在(动态的)GETTABUP 结果上重走,故录制期解析仅为预测。spill-everything 下每个 IR ref 有独立槽,GETTABUP ref 的槽 ≠ FMATH 的槽(顺序 GETTABUP→GUARD_CFUNC→FMATH),故 GUARD_CFUNC 读 op1 拿到的是 math 表而非被 FMATH 覆盖的结果。

### 效果与验证(完整门槛,全绿)
`math.sqrt` **1.92×**、sin/cos/exp 混合 **2.15×**、int 参数(TOFLT)**1.85×**(libm 调用 + 每轮 GUARD_CFUNC 哈希查找 + 禁 RA;加速适中但真实)。**298 ctest**(+kernel `math_call.spt`)· **295 全量差分** ×双档 · 新增 fuzz `gen_math_call`(7 函数 × float/int 参 × 多项组合)**200 例 0 失配、200 编译(100%)**,全 GENS 700 0 失配 · **valgrind 全干净**(C 调用 ABI 敏感,5 个 fuzz 案例 + 3 个手测 0 错误)· entries==exits、guard_fail=0。

### 教训
- **live-stack 读取对"循环体内被覆盖的寄存器"是陷阱**——必须从稳定源(上值/IR ref)重新派生(§10.41 GETTABUP 已是前奏,本节是其直接受益:正因 R[B] 是 GETTABUP 才能反查 _ENV)。
- C 调用机制可行性(§10.40 分析)全部兑现:infra 寄存器(L/ci/base/k)callee-saved 跨调用存活、不入 ra_op_is_safe 自动禁 RA、体内 RSP≡8 mod 16 一次 sub 对齐、libm 叶函数无 GC。**valgrind 是 ABI 正确性的命门**。
- **正确性靠运行时守卫(GUARD_CFUNC),不靠录制期解析**——表/方法被改即侧退出。
- 拆单元奏效:Unit 1(GETTABUP)清掉前置+风险,Unit 2 才聚焦真正的 C 调用;两单元各自全绿。

## §10.43 for-each 列表迭代:OP_TFORCALL/TFORLOOP 原生索引循环特化(✅ 阶段达成)

把 `for k[,v] : pairs(L)`(L 为 List)编译成**原生索引循环**。这是首个"真实代码"高频习语,且与 §10.42 形成关键对比:**完全不需要 C 调用**——故特意选它绕开本环境缺失 valgrind 带来的 ABI 校验风险。

### 习语与约定
SPT 的 `for k,v : pairs(L)` 编译为 `pairs() CALL(返回 luaB_next, 状态=L, nil, nil) → TFORPREP → 体 → TFORCALL → TFORLOOP 回边`。决定性事实(在 `ltable.c luaH_next` 中核实):**对数组态 List 经 luaB_next 迭代,键恰好是 0,1,…,loglen-1,值=L[键];越界(i≥loglen)时干净返回 nil(非报错)**。故整个 for-each 退化为索引循环。SPT 槽位(见 lvm.c):迭代器在 ra+0、状态在 ra+1(均循环不变)、键落 ra+3(兼下轮控制变量)、值落 ra+4。`pairs()` 返回 `luaB_next`(轻量 C 函数 LUA_VLCF;`ipairs` 返回的是 `ipairsaux`,不同函数)。

### 实现(无新 IR、无 codegen 改动——纯 recorder 变换)
与 §10.42 的最大不同:**不新增任何 IR opcode、不动 codegen**。整个特化只发既有的 `SLOAD/ADD/LEN/GETI/GUARD_LT/GUARD_T`,因这些全在 `ra_op_is_safe` 内。基础设施只 3 件:
1. **lbaselib.c** 暴露非 static `spt_jit_pairs_next()→luaB_next`(luaB_next 是 file-static,故访问器必须住这)。
2. **入口守卫**:SPTRecCtx/SPTTrace 加 `forin_iter_slot/forin_iter_fn`;`trace_entry_guards_ok` 中若 `forin_iter_slot>=0` 则校验该槽仍是 `ttislcf && fvalue==luaB_next`,否则拒绝进 trace(类比 inline_fn 的一次性 C 入口检查)。**迭代器身份只靠这个 C 入口守卫,不入 IR**——避免对函数槽发一个无 codegen 的 FUNC 类型守卫。
3. **recorder**:把 TFORPREP/TFORCALL/TFORLOOP 从 OP_SELF 的共享 abort 组拉出专门处理:
   - **TFORCALL**(限顶层帧、c∈[1,2]):校验迭代器==luaB_next、状态 `ttisarray`;`SLOAD 状态`(发 GUARD_T(ARR),提升进 preheader、入口 live-in);**守卫旧键**(见下);`newkey=ADD(键,1)`;`len=LEN(状态)`;若 c≥2 发 `值=GETI(状态,newkey)`(元素类型由 `rec_array_elem_type` 预测、运行时类型守卫兜底)+ 快照;**在快照之后**才提交 `reg_map[ra+3]=newkey、reg_map[ra+4]=值`(使快照携带旧键)。
   - **TFORLOOP**:回边目标检查照搬 FORLOOP(`(pc+1)-Bx==start_pc`?否则侧 trace→`sptir_exit`、根 trace→abort),再 `sptir_loop`。
   - **TFORPREP**:abort(只会在内层 for-each 嵌套时遇到——外层自身的 TFORPREP 在 start_pc 之前不录;不展开 for-each)。

### 关键 BUG(本节最重要的教训:RA 原地更新 vs 快照需旧值)
首版把循环继续守卫建在 **newkey**(自增**之后**)上,首跑即 `invalid key to 'next'`。根因:开 RA 时,循环携带的键其 `SLOAD(ra+3)` 与 `newkey=键+1` **共用一个寄存器(原地自增)**;`newkey` 的机器码在守卫之前执行,故循环末守卫的退出分支触发时寄存器**已是 newkey**;而该守卫的快照把 `ra+3` 映射到 `SLOAD(ra+3)`,于是**冲出 newkey(越界值)**给解释器 → `next(L, 越界)` 报错。更隐蔽地:**值类型守卫退出**也会冲出 newkey 并**跳过一个元素**。
**修复:对 for-each trace 禁用 RA**(`ra_analyze` 开头 `if (cg->trace->forin_iter_slot>=0) return;`)。spill-everything 下每个 IR ref 有独立槽,`SLOAD(ra+3)` 的槽始终存旧键、`newkey` 写另一槽,故所有快照都冲出正确的旧键;循环末退出重跑真实 TFORCALL(`next(L,旧键)→nil`)与解释器逐字一致。这与 §10.41/§10.42 的 RA-off 先例同构。**对 for-each 启用 RA 留作后续优化**(需照 FORLOOP 把守卫改建在旧键上,且必须消除"值 GETI 在自增后取快照"这一结构——本质是 for-each 在迭代末尾"提前取下一元素"破坏了 FORLOOP "自增是最后一步、退出分支在自增机器码之前"的安全前提)。

### 附带修复:ASan 揪出的一个**预存**越界(与 for-each 无关,顺手加固)
首次以 ASan+UBSan(本环境无 valgrind 的替身)跑 kernel,`bool_and_or`/`cmp_float` 触发 **heap-buffer-overflow**:`ifconv_arm_int_result`/`ifconv_arm_flt_result` 对 **LOADI/LOADF/LOADK** 这类 B 字段非寄存器的臂操作,仍**急切**地 `ifconv_reg_is_{int,flt}(rc, GETARG_B(ins))` → 用垃圾寄存器号读到栈外(报错栈区后 3416 字节)。这些 op 的返回值本就不用那个急切结果(LOADI/LOADF 返 1、LOADK 查 k[Bx]),故**仅是越界读、返回值一直正确**——这正是它能长期潜伏到现在的原因。**修复:把 B/C 的类型探测改为惰性**,只在真把 B/C 当寄存器的 case 内调用。与 for-each 改动正交,但作为内存安全加固一并修掉。

### 效果与验证(完整门槛,全绿;valgrind→ASan/UBSan 替身)
差分正确(off==on):int 求和、float 求和、键求和、仅键(c==1)、体内分支、**中途把状态换成另一个不同长度的数组**(fe_reassign:trace 经运行时 SLOAD/LEN 适配任意数组,正确)。安全用例均正确且不崩:map 迭代(编译=0,干净 abort)、ipairs(编译=0)、自定义 Lua 闭包迭代器(编译=0,因非 luaB_next)。**85 kernel**(+`for_each_list.spt`)· **297 ctest** ×(默认/HOT=8/HOT=2 压力)· 新增 fuzz `gen_for_each`(int/float × value/value_branch/keyvalue/key × 可选换表)**聚焦 500 例 0 失配、编译率 100%**,全量 fuzz 1250(种子 1-5)0 失配 · **ASan+UBSan 全干净**:85 kernel + 297 ctest ×(HOT=8,HOT=2) + fuzz 批次 0 错误,并**修掉其揪出的预存 ifconv 越界** · entries==exits、guard_fail=0。

### 教训
- **RA 原地更新与"快照需旧值"的根本张力**:任何在归纳变量自增**之后**取的快照都会冲掉旧值(SLOAD 与 new 值共寄存器)。FORLOOP 安全是因守卫在自增前——其退出分支的机器码在自增之前。for-each 因"末尾提前取值"破坏该结构 → 本轮以禁 RA 换正确性(慢但对 > 快但错)。
- **与 §10.42 的关键对比**:for-each over List **无需 C 调用**——luaH_next 的数组键序恰是 0..n-1,整循环退化为原生索引(仅 GETI+守卫);选它正为绕开缺 valgrind 的 ABI 风险。亦印证"能不发 C 调用就不发"。
- **正确性靠运行时守卫,录制期仅预测**:LEN/GETI 的边界与类型守卫 + C 入口迭代器固定;故状态中途换数组也正确。
- **终止逻辑不自造**:循环末退出重跑真实 TFORCALL(next→nil)。
- **缺 valgrind 时 ASan+UBSan 是有效替身**,且首跑即逮到一个预存越界——任何"从未跑过 sanitizer"的代码库都值得跑一遍。

## §10.44 链式容器访问:Map 取字段 → 索引/取字段(rec_eval_container 统一,✅ 阶段达成)

让 `m["k"][j]`(map-of-list)、`m["k1"]["k2"]`(map-of-map)等**经 Map 根的链式访问**能编译。对比:`a[i][j]`(list-of-list,纯链式 GETI)此前已支持;缺的是"中间容器来自 GETFIELD"的情形。**无 C 调用、无 GC**——故是当前最干净、最易差分验证的覆盖缺口(数据驱动画像选出)。

### 根因(数据驱动定位)
画像常见热循环的 abort,发现 `m["a"][j]` 100% abort、`a[i][j]` 正常。两处根因都是"读 live 栈而非顺 IR 解析":
1. **取索引那步(GETTABLE/GETI)abort**:`rec_array_elem_type → rec_eval_array` 只顺 `SLOAD/GETI` 解析中间数组,**不认 GETFIELD 产出的嵌套 List** → 返回 `SPTT_ANY` → abort。
2. **map-of-map 第二个 GETFIELD abort**:OP_GETFIELD recorder 读 `s2v(base+b)` 找基 map,但 `m["p"]` 是**本 trace 早先产出的中间值**,录制发生在回边、该栈槽是上一轮的陈旧值(非 map)→ `ttistable` 失败 → abort(§10.42 同款 live-stack 陈旧陷阱)。

### 实现(2 处,均改为"顺 IR 解析";无新 IR、无 codegen 改动)
1. **统一 `rec_eval_container`**:把原 `rec_eval_array` 重构为返回 `TValue` 的通用解析器,顺 `SLOAD`(栈槽)/ `GETI`(List 元素)/ `GETFIELD`(Map 字段,interned 短串键经 `luaH_getshortstr`)链向下;**List/Map 标签随 TValue 走**(`LUA_VARRAY` vs `LUA_VTABLE`),每层按标签用对应的 `avalue()`/`hvalue()`。`rec_eval_array` 变成"取 container 后过滤 array 态"的薄封装。这让任意混合链(`a[i][j]` / `m["k"][j]` / `m["k1"]["k2"]` / 更深)都能预测元素类型。
2. **OP_GETFIELD 基 map 改为顺 IR 解析**:先 `rec_eval_container(bref)` 顺 IR 求基 map,**失败再回退 `s2v(base+b)` live 栈槽**——保持全局 map(经 GETTABUP 的基)等其它无法顺链解析之基的旧行为不变(零回归),只把"中间 GETFIELD/GETI 产出之基"改走 IR。

为何无需动 codegen:GETFIELD 早已声明可产 `SPTT_ARR/SPTT_TAB` 结果、GETI/GETTABLE 早已接受 `SPTT_ARR` 基;codegen 上 GETI 的 `gen_load`、GETFIELD 的 `gen_hash_find` 直接消费 GETFIELD 产出的 `Table*` 指针(与消费 SLOAD 产出的数组指针同构)。缺的**仅是录制期"顺 IR 解析中间容器"**这一块预测逻辑。

### 效果与验证(完整门槛,全绿)
差分正确(off==on):map-of-list(int/float)、双字段 `m["a"][j]-m["b"][j]`、map-of-map `m["p"]["x"]`;list-of-list **零回归**;现存 `map_read`/`map_write` kernel **零回归**。**86 kernel**(+`nested_container.spt`)· **298 ctest** ×(默认/HOT=8/HOT=2)· 新增 fuzz `gen_nested_container`(mol_int/mol_flt/mom/lol 四形)**聚焦 500 例 0 失配、编译率 100%**,全量 1250(种子 1-5)0 失配 · **ASan+UBSan 全干净**(86 kernel + 298 ctest ×双档 + fuzz 批次;GETFIELD 在热路径上,故重点复验)· entries==exits、guard_fail=0。

### 教训
- **"顺 IR 解析,别读 live 栈"是处理链式/循环携带中间值的通用法则**:§10.42 已立(math 表)、§10.43 已用(for-each 键),本节再次受益并**推广到 Map 容器链**。live 栈对"本 trace 早先产出的中间容器"必然陈旧——这是一类反复出现的陷阱。
- **List/Map 标签随 TValue 走**(`LUA_VARRAY`/`LUA_VTABLE`),链式解析每层必须按标签选 `avalue`/`hvalue`,不能只凭 `Table*`。
- **"顺 IR 解析失败再回退 live 栈"是低风险扩展模式**:新增能力同时严格保持旧路径行为(GETFIELD 基的改动对全局 map 等零影响)。
- **数据驱动选题**:与其猜,不如画像常见热循环的 abort,直接命中最高价值的干净缺口(无 C 调用、无 GC、差分可验)——本轮即由 `m["a"][j]` 的 100% abort 定位到此。

## §10.42-fix 构建可移植性:spt_jit_unary_math 去掉 l_mathop(裸 libm 函数名)
§10.42 的 `spt_jit_unary_math` 用 `return l_mathop(sqrt);` 取函数指针,在**某些构建配置下编译失败**(报 `pointer value used where a floating-point was expected`)。

**根因**:`luaconf.h:565` 的 `#if defined(LUA_USE_C89) || (defined(HUGE_VAL) && !defined(HUGE_VALF))` 为真时,`l_mathop(op)` 被重定义为 `(lua_Number) op`。`l_mathop` 的本意是包裹一个 math **调用**(`l_mathop(sqrt)(x)` → `(lua_Number) sqrt(x)`,转的是调用**结果**);但当它包裹一个**裸函数名**(`l_mathop(sqrt)` → `(lua_Number) sqrt`)时,就成了把**函数指针**强转成 double → 编译错误。该分支是否触发取决于处理 luaconf.h 时 `HUGE_VALF` 是否可见(include 顺序 / 是否 `-DLUA_USE_C89`),故 CMake 构建命中、本地 build.sh(identity 分支)未命中。

**修复**:`spt_jit_unary_math` 直接返回裸 libm 函数名(`return sqrt;` 等)。typedef 本就是 `double(*)(double)`、libm 双精度函数恰好匹配、FMATH codegen 也以 double 运作,故裸名是**类型正确**的选择,且与可用构建里 `l_mathop`=identity 时的展开**完全一致**(行为不变)。lobject.c 中其余 `l_mathop(...)` 都是调用或常量(`(lua_Number) ldexp(r,e)`/`(lua_Number) 0.0`),不受影响。

**验证**:`-DLUA_USE_C89` 强制该分支后**整项目编译+链接通过**、math_call kernel off==on(147672.5484453346)、86 kernel difftest 全绿;正常构建 86 kernel + 298 ctest + fuzz 全绿、math 行为不变。**教训**:用宏取"函数指针"前先看它在所有 luaconf 分支下的展开——为"调用"设计的宏未必能用于"取址";跨构建可移植性要按最严格分支(C89 fallback)校验。

## §10.45 List of Map 元素:GETI/GETTABLE/for-each 放行 TAB 元素(✅ 阶段达成)

让 `list<map<...>>` 的元素访问能编译:`a[i]["k"]`(读)、`a[i]["k"]=...`(写)、`for k,mp : pairs(a)` 取 `mp["k"]`。是 §10.44(Map-of-List)的**对称补全**(List-of-Map)。**无 C 调用、无 GC**。

### 根因(数据驱动,一行白名单)
画像发现 `a[i]["v"]` 100% abort,而 `m["a"][j]` 已通(§10.44)。abort 在取索引那步(GETI/GETTABLE):它们的**元素类型白名单只放 `INT/FLT/STR/ARR`,漏了 `TAB`(Map)**;而 GETFIELD(map 读)的值白名单**早已放行 `TAB`**。即:List 能装 Map(`list<map>`),但 GETI 不让加载 Map 元素 → 一读 `a[i]` 就 abort。

### 实现(极小;无新 IR、无 codegen 改动)
GETI、GETTABLE、for-each 值加载三处白名单各加 `SPTT_TAB`。**TAB 元素与 ARR 元素表示完全相同**(8 字节 `Table*` gc 指针 + 标签守卫),GETI codegen 早已能产 TAB 结果(被 GETFIELD 复用)、`spt_type_to_tag(SPTT_TAB)=TAG_TAB`,故只需录制期放行、零 codegen 风险。后续 `a[i]["k"]` 的字段读写走既有内联哈希槽(§10.36/§10.37),内层 Map 经 `rec_eval_container` 顺 IR 解析(§10.44)。

### 效果与验证(完整门槛,全绿)
差分正确(off==on):`a[i]["v"]` 读、`a[i]["v"]=a[i]["v"]+1` 写、`for k,mp:pairs(a)` 取 `mp["v"]`;**list-of-list 零回归**;**现存数组读路径零回归**(白名单只新增放行,非 TAB 分支字节级不变,fuzz 种子 1-3 共 750 例 0 失配确认)。**87 kernel**(+`list_of_map.spt`)· **299 ctest** ×(默认/HOT=8/HOT=2)· `gen_nested_container` 增 `lom_read`/`lom_write` 形,聚焦 500 例 0 失配、编译率 100% · **ASan+UBSan 全干净**(87 kernel + 299 ctest ×双档 + fuzz;GETI/GETTABLE 是最热路径,重点复验)· entries==exits、guard_fail=0。

### 教训
- **对称性审视暴露缺口**:GETFIELD 放行 TAB 值、GETI 却不放行 TAB 元素——同样的表示、不一致的白名单。把"读 Map 的字段值"与"读 List 的元素值"对照,一眼看出该补 `TAB`。和 §10.44(Map-of-List)合起来,任意 List/Map 嵌套链(`a[i][j]` / `m["k"][j]` / `m["k1"]["k2"]` / `a[i]["k"]`)读写都覆盖了。
- **同构表示 → 录制期放行即可**:TAB 与 ARR 在 codegen 完全同构(都是 `Table*` + 标签守卫),无需任何 codegen 改动。
- **数据驱动再次奏效**:画像直接命中 `a[i]["v"]` 的 abort,改动收敛到一行白名单。

## §10.46 方法调用内联:obj.method() 纯读方法(OP_SELF + CALL,resume-at-SELF,✅ 阶段达成)

让类实例的**纯读方法**(getter / 基于 this 字段+参数的计算值,无写)在热循环里被内联进 trace:`m.scale(j)`、`p.sum()`、`v.dot()`、`b.at(i)`(变量下标)、`w.f(p,q,t)`。**纯 Lua、无 C 调用、无 GC**,故可用现有工具完整差分验证。

### 类即元表(SPT 习惯)
SPT 的 class 是 `setmetatable(inst, Class)` 且 `Class.__index = Class`(见 ast_codegen.c),所以**方法直接挂在接收者的元表上**:`rec_table_getstr(recv->metatable, key)` 即得方法闭包,无需解析 `__index`(它就是元表自身)。

### 接线(对称复用自由函数内联)
- **OP_SELF**:`R[B]` 是类实例且来自**稳定槽的 SLOAD**(⇒ 循环不变接收者:体内若重新赋值接收者就不是裸 SLOAD,自然 abort)时,解析元表上的方法 → 把接收者放进 `R[A+1]`(被调方槽 0 = this),武装 `pending_method`(下一条 CALL 内联它),并记录**一组方法身份**(recv_slot/元表/key/proto);第二个不同方法 → 失配 abort(**每 trace 一个方法**)。
- **OP_CALL**:`pending_method_slot==frame_base+a` 时,镜像自由函数内联(`proto_is_method_inlinable` 体纯度门、保存/切换 caller 上下文、`new_fb=A+1`、深度 1)。
- **入口守卫**(`trace_entry_guards_ok`):接收者槽仍是 table、其元表==记录的类、且 key 在元表上**重新解析**回同一 proto(防方法被改写后用了过期内联)。
- **OP_SETFIELD 基**:原先用裸栈槽 `s2v((func+1)+a)` 取基表——内联帧里 `a` 是被调方相对槽(0=this),绝对位置是 `frame_base+a`,取错表。改为 `rec_eval_container(aref)` **顺 IR 解析到 live 接收者**(镜像 GETFIELD 的 §10.44)。这让带字段访问的方法体能正确取到 this 的表。

### 关键正确性:内联帧退出 = 必须 resume-at-SELF(本节核心)
退出桩做 `ci->u.l.savedpc = exit_pc` 后返回解释器,而解释器仍在 **caller 的 CallInfo**(内联从未压被调方帧)。所以一个在内联方法体内触发的守卫,若快照 PC 指向**被调方字节码**,退出后会在 caller 帧下执行被调方字节码 → **栈基/常量全错 → 损坏**。两道机制根除之:

1. **resume-at-SELF**:方法体内每个守卫(字段守卫走 `rec_snap`、`sptir_guard` 建的下标越界守卫走 `rec_guard_pc`)的退出 PC 一律设为 caller 的 **SELF 指令**。守卫失败 → 解释器从 SELF 重跑 SELF+CALL+method。**仅因纯读方法体无已提交副作用、重跑幂等才成立**——故门 `proto_is_method_inlinable` 禁 `SETFIELD/SETI/SETTABLE`。快照在 CALL 处(仍在 caller 帧)取,捕获 caller 活值(循环归纳、累加器、接收者源);被调方临时值随后被重跑覆盖,无害。变量下标越界(`b.at(8)`)即走此路:退出→解释器重跑→解释器报正确的越界错(实测 off==on 同错、无崩溃)。
2. **入口前安全网**(`sptir_optimize` 后、codegen 前):扫所有 live 守卫,任一退出 PC 落在主 proto `[code,code+sizecode)` **之外**(即仍指向内联体)→ **拒绝编译、回退解释器**(同侧 trace 摊销门)。这**保证绝不发出任何 in-callee 退出桩**;它正是抓住了未重定向前的下标越界守卫,也是任何遗漏的兜底。

> 早期教训:常量 key 的字段守卫**并未被 LICM 完全消除/外提**,而是作为 live 守卫携带 in-callee 快照 PC 留存(对稳定形状实例永不触发,但是潜藏地雷)。安全网正确地把所有方法都拦了下来——证明\"靠 LICM 一定会外提\"不可依赖,必须**显式校验**(resume-at-SELF 给出主-proto 退出 PC,安全网放行;否则拦截)。

### 边界(本轮明确不做,均干净 abort 回退)
- **写/RMW 方法**(`this.f = ...`):`this.total=this.total+x` 编译后**写后还会 GETFIELD 重读** this.total(写之后的守卫)→ resume-at-SELF 重跑会**双写** → 不安全。需\"入口期字段布局守卫 + 无守卫字段访问 codegen\"(更大改动),故暂禁(门排除写)。
- **变量下标**(`this.d[i]`)纯读**支持**(越界经 resume-at-SELF 正确回退);写下标随写方法一并暂禁。
- **每 trace 多方法 / 分支方法体(cond-return)**:暂不支持(单方法、直线体)。

### 效果与验证(完整门槛,全绿)
差分正确(off==on)且 compiled≥1:`scale(v)`、`sum()` 双字段、`dot()` 浮点字段、`at(i)` 变量下标、`first()` 常量下标、零参 getter、三参方法。安全:`varying_receiver`(对象列表,接收者是 GETI 非 SLOAD → abort)、`non_class_map_field`(普通 map 不受影响)、方法内越界(off==on 同解释器错、无崩溃)均正确回退。**全局快照路径改动(`rec_snap`/`rec_guard_pc`)零回归**:88 kernel(+`method_call.spt`)· 299 ctest ×(默认/HOT=8/HOT=2)· 多种子 fuzz(HOT=8 五种子 1250 例 + HOT=2 两种子 400 例 + 聚焦 `gen_method_call` 400 例)共 ~2050 例 0 失配 · **ASan+UBSan 全干净**(88 kernel difftest + 方法用例 + 120 例 fuzz)· entries==exits、guard_fail=0。

### 教训
- **内联帧退出是调用内联的真正难点**(路线图早有预判):零守卫叶子(自由函数)绕开它;一旦方法体带字段访问就必须面对\"被调方帧不存在\"。resume-at-SELF(重跑整条调用)对**纯读**是干净解;写需要更强的入口期守卫。
- **\"实测永不触发\"≠正确**:字段守卫对稳定形状实例永不失败,但 latent in-callee 退出桩仍是地雷。**安全网把\"假设\"变\"校验\"**——这是本节最重要的正确性保证。
- **慢但对 > 快但错**:宁可把写方法干净 abort(回退解释器),也不发一个\"几乎不触发但一触发就损坏\"的退出。窄而深、可证正确的子集优先固化。
- **SLOAD 接收者 = 循环不变性的廉价证明**:要求接收者是裸 SLOAD,既保证循环不变(可入口校验一次),又自动拒绝\"对象列表\"等每次变化的接收者。

## §10.47 每 trace 多方法内联:`a.foo(); a.bar()`(OP_SELF 身份列表,✅ 阶段达成)

§10.46 的方法内联限**每 trace 一个方法**——入口守卫只钉一组身份(recv_slot/元表/key/proto),第二个不同方法即 abort。但真实 OOP 热循环常在一轮里调多个方法(`a.foo(); a.bar()`、`p.x(); p.y()`)。本节把单一身份升级为**身份列表**,让一条 trace 内联多个不同方法。**纯 recorder/入口守卫改动,无新 IR、无 codegen 改动**——每个方法仍各自 resume-at-SELF(§10.46),正确性面不变,只是入口逐一校验。

### 缺口确认(数据驱动,先证缺失)
`for j: s = s + p.sumx() + p.sumy()`(同接收者、两方法、一循环)实测 **compiled=0**,abort 落在第二个 `OP_SELF`(op21,pc_offset=36,录完第一个方法后撞上)——但 off==on 输出正确。即:第一个方法内联成功,第二个因 `method_proto != mcl->p` 触发单身份的 abort 分支。这正是 §3c.1 #2 点名的"真实 OOP 常见"缺口。

### 实现(身份单例 → 身份列表)
- **`SPTMethodId` 结构**(spt_jit_internal.h):`{recv_slot, class_mt, key, proto}` 四元组。`SPTTrace`/`SPTRecCtx` 各用 `SPTMethodId methods[SPT_JIT_MAX_METHODS]` + `int n_methods`(`SPT_JIT_MAX_METHODS=8`,真实循环极少在稳定接收者上调超过一把方法)。`pending_method_*`(SELF→CALL 交接)仍是单例——它天然顺序(SELF 武装、紧跟的 CALL 消费清零、下个 SELF 再武装),无需复数化。
- **OP_SELF**(spt_jit.c):原"`method_recv_slot<0` 则设、否则与单一身份比对、不符即 abort"改为**查重 + 追加**:遍历 `methods[]` 找匹配三元组(recv_slot/class_mt/proto),命中则复用(同方法被调多次=dedup,如 `a.f()+a.f()`),未命中且未满则追加新条目,满了才 abort。`method_self_pc` 仍每个 SELF 现设、被该方法体内守卫即时取用,顺序无冲突。
- **入口守卫 `trace_entry_guards_ok`**:原校验单一身份改为**遍历 `methods[]` 逐一校验**(每个 recv_slot 仍是 table、元表==钉住的类、key 重解析回同 proto)。同一 recv_slot 可在列表里重复(`a.f();a.g()` 两条目都查槽 S→元表 M,proto 各异),冗余但无害且正确。
- init/copy-to-trace 相应改为清零 `n_methods` / 拷贝数组。

### 为什么"无新正确性面"(关键论证)
多方法只是把单条目验证复制成 N 条目验证,**每个方法的内联机理与 §10.46 逐字相同**:体内守卫仍 resume-at-SELF(纯读幂等)、退出 PC 仍设 caller 的该方法 SELF、入口前**安全网**(扫 live 守卫、退出 PC 出主 proto 范围即拒编译)仍对每个方法的 in-callee 退出桩兜底。两方法之间无共享可变状态:`pending_method_*` 顺序消费、`method_self_pc` 每 SELF 现设现用、身份列表只增不改。`proto_is_method_inlinable` 的纯读门(禁写/RMW)对每个方法独立生效。故正确性完全继承 §10.46,仅入口校验从"一组"变"一列表"。

### 效果(全部正确)
多方法循环 `s = s + p.fx() + p.fy(j)`(64M call-pair)**1.0×(此前 abort 回退解释器)→ 5.98×**。battery 全 compiled + off==on:两/三方法同接收者、双接收者(`a.f()+b.g()`,不同 recv_slot)、float 字段方法、dedup(同方法两次)、混参。varying-receiver(对象列表,接收者非 SLOAD)仍干净 abort 回退。`entries==exits`、`guard_fail=0`。§10.46 单方法 kernel 零回归。

### 验证(完整门槛,全绿)
**303 ctest**(+kernel `multi_method.spt`:六循环,五条多方法循环编译、varying-receiver 回退)· **89 kernel** · **300 全量差分** · 新生成器 `gen_multi_method`(同接收者 2/3 方法、双接收者、dedup、float、混参 六形)60 例 0 失配/100% 编译,**全 GENS(39 个)seeds 1-10 × 300 = 3000 例 0 失配** · **ASan+UBSan 全干净**(89 kernel difftest + 方法重案例 0 sanitizer 报告 + 600 例 fuzz;方法内联在热路径,重点复验)· entries==exits、guard_fail=0。

**关键教训**:
(1)**单例→列表是低风险扩展的范式**:当新能力 = "已验证的单元素机理 × N",且元素间无共享可变状态时,把标量身份换成列表 + 逐一校验即可,正确性直接继承,无需新机理(对照 §10.36→§10.38 主位置→链遍历、§10.44 顺 IR 解析的扩展手法)。
(2)**先证缺失再动手**(§10.21 方法论延续):用最小 repro 确认 `a.foo();a.bar()` 确实 compiled=0 且 abort 落在第二个 SELF,再实现——避免对"以为缺的"特性空转。
(3)**dedup 让同方法多次调用零成本**:查重逻辑使 `a.f()+a.f()` 只占一个身份条目,既省入口校验又自然正确。
(4)安全网(§10.46)对每个新方法的 in-callee 退出桩**自动兜底**——多方法无需额外正确性设施,这是 §10.46 安全网设计的直接红利。

## §10.48 带分支方法体内联:cond-return 纯读方法 if-conversion(OP_SELF + CALL,✅ 阶段达成)

§10.46 的方法内联门 `proto_is_method_inlinable` 只收**直线体**(单 RETURN1、无分支)。但 clamp/abs/min/max 这类 getter 常写成**条件返回** `int clamp(){ if(this.v<0) return 0; return this.v; }`,此前 100% abort 回退解释器。本节把自由函数早有的 cond-return if-conversion(§10.27)接到方法路径上:识别 `if(c){return A}return B` 形状、内联、并把条件返回 **if-convert 成无分支 select**(无被调方中段守卫分支——§10.28 已证那在单入口 trace 里不可表示、会损坏)。**复用 §10.27 的 `rec_try_condreturn_ifconv`,无新 IR、无 codegen 改动。**

### 缺口确认
`for j: s = s + a.relu()`(`relu` 为 cond-return 方法)compiled=0,abort 落在方法体的比较处:方法门只认直线体,cond-return 形状不被内联 → OP_CALL abort。这正是 §3c.1 #3 点名的缺口。

### 实现(三块,均小)
1. **静态门 `proto_is_condreturn_method_inlinable`**(spt_jit.c):复制 `proto_is_condreturn_inlinable` 的形状匹配(比较+JMP、then 臂 RETURN1、else 臂 RETURN1、尾随死 boilerplate),但 prefix/两臂的 op 白名单用新 helper `condreturn_method_op_ok` = **ifconv-safe 整数 op + GETFIELD + LEN**(读 this 字段/数组长度)。
2. **OP_CALL 方法路径**:`proto_is_method_inlinable` 失败时再试 `proto_is_condreturn_method_inlinable`,任一通过即用**同样的方式**切入被调方(切 frame_base、pc=callee code、武装 `method_resume_snap`)。cond-return 的 if-conversion 在录制器撞到比较时由 `rec_try_condreturn_ifconv`(frame_base≠0)接管。
3. **放宽 `rec_try_condreturn_ifconv` 臂扫描**:原扫描只认 ifconv-safe,改用 `condreturn_method_op_ok`(超集,含 GETFIELD/LEN)。对自由函数 cond-return 无影响(其静态门 `proto_is_condreturn_inlinable` 已禁字段读,故臂里永不含 GETFIELD,超集扫描行为不变);只让**方法**的字段读臂不被误拒。臂的实际录制本就经 `rec_inst`(GETFIELD 在内联帧里走 `rec_eval_container` 解析 this,§10.44/§10.46 已就绪),故无需新录制逻辑。

### 关键设计抉择:**排除变量下标 GETI/GETTABLE**(降负优化风险)
`condreturn_method_op_ok` 放 GETFIELD/LEN 但**不放 GETI/GETTABLE**。原因:if-conversion **两臂都无条件求值**,而变量下标读带**边界守卫**。若 `if(i<n) return d[i]; return -1;`,编译后 trace 每轮都求 `d[i]`,当 `i>=n`(本该走 else)时边界守卫**每轮侧退出** → 负优化(违反"trace 不得慢于解释器")。GETFIELD/LEN 在**钉住的类**上守卫稳定(字段恒存在、类型不变)→ 稳态永不触发,故安全。变量下标 cond-return 列为后续(需"仅对实际取的臂求下标"或谓词化下标,更复杂)。

### 正确性(完全继承 §10.46)
- **无被调方中段守卫分支**:cond-return 唯一的比较被 if-convert 成无分支 select(CMPSET+SUB+MUL+ADD),不发任何"被调方 PC 退出"的守卫分支。这是 §10.28 的硬要求(被调方中段控制流分叉在单入口 trace 不可表示)。
- **字段读守卫 resume-at-SELF**:两臂里 GETFIELD 的类型/键守卫经 `rec_snap`/`rec_guard_pc`(frame_base≠0 时返回 `method_resume_snap`/`method_self_pc`)退出到 caller 的 SELF、重跑整条调用——**仅纯读体幂等才成立**,故门禁写。
- **入口前安全网**(§10.46)仍兜底:任一 live 守卫退出 PC 出主 proto 范围即拒编译。
- **整数 select 位精确**:`rec_try_condreturn_ifconv` 录制后查 then/else 返回类型==INT,float 字段/返回**干净 abort**(走守卫分支→frame_base≠0→abort,与 §10.30 一致)。

### 效果(全部正确)
cond-return 方法 `s + a.relu() + a.clamp()`(64M call-pair)**1.0×(此前 abort 回退)→ 5.94×**。battery 全 compiled + off==on:relu/abs(ReLU、绝对值)、max/min(field,param)、两字段 select、clamp-hi、**与直线方法同接收者混用**(`c.dbl()+c.clamp()`,§10.47 多方法 × 本节 cond-return,一条 trace)。**float 字段 cond-return、变量下标 cond-return(GETI 臂)干净 abort 回退**(off==on,只是不编译)。`entries==exits`、`guard_fail=0`。§10.46 直线方法、§10.47 多方法零回归。

### 验证(完整门槛,全绿)
**304 ctest**(+kernel `condreturn_method.spt`:七循环,五条 cond-return 方法编译、float+变量下标两条回退)· **90 kernel** · **301 全量差分** · 新生成器 `gen_condreturn_method`(relu/abs/max/min/pick2/clamp_hi/混直线 + float/var-index 回退 九形)80 例 0 失配(61 编译 + 19 正确回退),**全 GENS(40 个)seeds 1-10 × 300 = 3000 例 0 失配** · **ASan+UBSan 全干净**(guard-heavy 15-kernel 子集含 condreturn_method/multi_method + cond-return 案例 0 报告 + 300 例 fuzz) · entries==exits、guard_fail=0。

**关键教训**:
(1)**接已验证的机理到新上下文**:cond-return if-conversion(§10.27)早为自由函数验证过,本节只把它的静态门 + 臂扫描放宽到允许字段读、并接到方法路径,**无新 IR/codegen**——又一例"已验证机理 × 新场景"的低风险扩展(对照 §10.47 单例→列表)。
(2)**if-conversion 两臂都求值 → 带可触发守卫的 op 要慎入臂**:GETFIELD/LEN(钉住类上稳定守卫)安全,变量下标 GETI(边界守卫随下标触发)会在不取的臂里每轮侧退出 → 负优化,故排除。这是"做对≠做值"(§10.32)在 if-conversion 臂选择上的具体应用。
(3)**被调方中段不发守卫分支**始终是内联帧的铁律(§10.28):cond-return 必须 if-convert 成无分支或干净 abort,绝不发被调方 PC 的守卫分支;float/不可转形状一律 abort。
(4)安全网(§10.46)对任何遗漏的 in-callee 退出桩自动兜底,使每个新内联形态的引入都"先被拦住、再逐步放行"——本节字段读 cond-return 即按此法稳推。

## §10.49 写方法内联:单尾写 void 方法(setter/累加器,resume-at-SELF 无需入口布局守卫,✅ 阶段达成)

§10.46–§10.48 的方法内联都限**纯读**体(resume-at-SELF 仅幂等成立)。写方法 `void add(int x){ this.total = this.total + x; }`(累加器)、`void set(int x){ this.v = x; }`(setter)此前 100% abort。路线图 §3c.1 #1 把写方法标为"本项最大工作量",因**一般**情形需"入口期字段布局守卫 + 无守卫字段访问 codegen"(写后若还有守卫,resume-at-SELF 重跑会**双写**)。本节识别并拿下一个**无需那套机制的安全子集**:**单尾写 void 方法**——写是体内最后一个产生守卫的 op。

### 安全论证(本节核心,决定可不动 codegen)
写方法不安全的根源是"**写后还有守卫**":若已提交一次写之后某守卫失败、resume-at-SELF 重跑整条调用 → 双写。但若方法是 `<读 this 字段/参数 + 计算>; this.field = val; return`(**单次写、且写是最后一个产生守卫的 op**),则:
- 体内所有守卫(字段读的键/类型守卫、SETFIELD 自身的键守卫——codegen 在**存值之前**用 gen_hash_find 找节点)**全在写之前**;
- 写之后只有 void 返回(无守卫)。

故任何守卫失败都在**写提交之前**侧退出,解释器重跑整条调用**恰好写一次**——无双写。**所以这个子集用现有 resume-at-SELF + SETFIELD codegen(§10.37)即可,零新 codegen、零入口布局守卫**。一般情形(多写 `this.a=..;this.b=..`、读改写后返回 `this.v=..;return this.v`——写后有守卫)仍走 §3c.1 #1 的入口布局守卫方案(未做),本节**精准 abort 它们**。

### 实现(三块,均小;无新 IR、无 codegen 改动)
1. **静态门 `proto_is_write_method_inlinable`**:体含**恰好一个 SETFIELD**(`method_read_op_ok` = §10.46 纯读集 校验写之前的读/算 op);写之后只允许 RETURN0/RETURN/EXTRAARG(**保证写后无产生守卫的 op**)。是 void(无 RETURN1)。
2. **OP_CALL 方法路径**:`proto_is_method_inlinable`/`proto_is_condreturn_method_inlinable` 失败再试它;并把结果数检查从 `c!=2` 放宽到 **`c==1`(void,0 返回值)或 `c==2`(1 返回值)**——void 方法作语句调用编译成 `CALL ... C=1`(**起初 bug:`c!=2` 把所有 void 方法 abort 在 OP_CALL**,DEBUG 显示 abort@op69=OP_CALL,这是本节唯一的坑)。
3. **内联帧 void 返回**:OP_RETURN0(及 OP_RETURN b==1)在 `frame_base!=0` 时原 abort,改为**恢复 caller 上下文、不绑结果**(镜像 OP_RETURN1 内联帧分支去掉绑定)。
- OP_SETFIELD handler **本就**在内联帧工作(§10.46 把基址改 `rec_eval_container` 解析 this)且**本就**用 `rec_snap`→resume-at-SELF(§10.37 的快照),故写的键守卫自动 resume-at-SELF,无需改。
- `has_table_write` 在含 SETFIELD 时**禁 GETFIELD CSE**(§10.37 既有),故累加器每轮 GETFIELD 真从内存重读上轮的写——累加正确(字段走内存,非寄存器驻留)。

### 效果(全部正确,累加和精确=无双写)
累加器 `a.add(j)`(64M call)**1.0×(此前 abort 回退)→ 3.80×**(字段走内存,慢于寄存器标量但远快于解释器全调用开销)。battery 全 compiled + off==on:累加器(和**逐位精确**,双写会翻倍——这是核心正确性判据)、双累加器一条 trace(§10.47 多方法写)、setter、scale、compute-write、float 字段累加。**多写方法 `bump()`、读改写 `addret()` 干净 abort 回退**(off==on)。`entries==exits`、`guard_fail=0`。§10.46–§10.48 零回归。

### 验证(完整门槛,全绿)
**305 ctest**(+kernel `write_method.spt`:八循环,四条安全写方法编译、多写+读改写两条回退)· **91 kernel** · **302 全量差分** · 新生成器 `gen_write_method`(累加/双累加/setter/scale/compute-write/float + 多写/读改写回退 八形)100 例 0 失配(75 编译 + 25 正确回退,累加和全精确)· **全 GENS(41 个)seeds 1-10 × 300 = 3000 例 0 失配** · **ASan+UBSan 全干净**(guard-heavy 16-kernel 子集含 write_method + 写案例 0 报告 + 300 例 fuzz;**写=内存改动,ASan 是双写/越界写的命门**) · entries==exits、guard_fail=0。

**关键教训**:
(1)**找"无需重武器的安全子集"**:写方法一般情形要入口布局守卫 + 无守卫 codegen(大工程),但**单尾写**子集靠"写是最后一个守卫 op ⇒ resume-at-SELF 只在写前触发 ⇒ 无双写"的论证,用**现有**机制即可拿下最常见的 setter/累加器——又一例 §10.47/§10.48 的"窄而可证子集优先固化",把最大工作量拆出一块零 codegen 的安全增量。
(2)**写的正确性判据是"累加和逐位精确"**:双写不会被类型/越界检查抓到,只会让和翻倍/错位——fuzzer 必须用累加器(可验证的确定性和)而非随机表达式来压它。
(3)**void 方法 = `CALL C=1`**:结果数检查必须放行 0 返回值,否则 void 方法全 abort 在 OP_CALL(本节唯一 bug,DEBUG 退出 op 一眼定位)。
(4)SETFIELD codegen/快照在 §10.37/§10.46 早已为内联帧 + resume-at-SELF 铺好(rec_eval_container 基址、rec_snap 快照),故本节零 codegen——印证"先把底层语义铺完整、上层增量自然受益"的分层红利。

## §10.50 浮点 cond-return if-conversion:float relu/clamp/min/max(方法 + 自由函数,接 §10.30 位精确浮点 select,✅ 阶段达成)

§10.48 的 cond-return if-conversion(`if(c){return A}return B` → 无分支 select)是**整数版**:`rec_try_condreturn_ifconv` 遇浮点 reg_type 即 abort。故最常见的 ML 模式 **ReLU** `float relu(){ if(this.x<0.0) return 0.0; return this.x; }`、以及 float clamp/abs/min/max(方法与自由函数)全 abort 回退——§10.48 测试里 `float relu()` 方法正是被显式验证为 abort 的那个 fallback 用例。本节把 §10.30 早建好的**位精确浮点 select 机器**(`FCMPMASK` 浮点比较→掩码、`ICMPMASK` 整数比较→掩码、`FSELECT` 位精确掩码混合)接到 cond-return 路径,**无新 IR、无 codegen 改动**——纯粹复用条件赋值(§10.30 `rec_try_ifconv`)早验证过的浮点机器。同时惠及 cond-return **方法**(float 字段)与 cond-return **自由函数**(`float clampf(float x)`)。

### 缺口确认 + 实现(三处小改)
1. **`rec_try_condreturn_ifconv` 加浮点分支**:去掉 `at!=INT||bt!=INT` 早退(改 `sptt_isnum` 收浮点比较);录完两臂后取 `then_t`/`else_t`,按类型建 select:
   - 两臂 INT + INT 比较 → 整数 select `else+(then-else)*cond`(原路径)。
   - 两臂 FLT → **浮点 select**:`is_flt_cmp`(任一比较操作数浮点)时建 `FCMPMASK`(int 侧 TOFLT 提升、`>`/`>=` 换序成 `<`/`<=` 以保 Lua 的 NaN-为假语义),否则 `ICMPMASK`;再 `FSELECT` 位精确混合。**镜像 §10.30 `rec_try_ifconv` 的浮点 select 逐字**。
   - 混合臂(一 int 一 float)或 int-臂-浮点-比较 → 干净 abort(不改值的类型)。
2. **门放宽到 `opcode_is_ifconv_safe_any`**:`condreturn_method_op_ok`(方法门 + 录制器臂扫描)和 `proto_is_condreturn_inlinable`(自由函数门)原用 `opcode_is_ifconv_safe`(整数版,**无 LOADF/LOADK**)→ 返回浮点常量的臂(`return 0.0`)被拒。改用 `_any`(整数∪浮点安全集),放行 LOADF/LOADK(浮点常量)与 OP_DIV(`/`,Lua 里恒产 float、不陷)。**关键安全点**:陷阱整数除 `//`(OP_IDIV)不在任一集 → 仍被拒;`/`(OP_DIV)恒产 float → 用它的臂自动变浮点臂走浮点 select,无整数除零陷阱。
3. 起初 bug 定位:relu 等返回浮点**常量**的形态 abort@op69=OP_CALL(门因 LOADF 拒),而 maxp/minf 返回**变量**的形态能编译——一眼锁定是常量加载 op 未入 ifconv 安全集。

### 正确性(位精确 + NaN 语义,完全继承 §10.30 + §10.48)
- **位精确**:浮点 select 用掩码混合 `(then&mask)|(else&~mask)`,**绝不**用整数 `else+(then-else)*cond`(会舍入)。§10.30 已证。
- **NaN 为假**:`>`/`>=` 无"NaN 为假"的 CMPSD 谓词,故换序成 `<`/`<=`(`LT_OS`/`LE_OS`)。四谓词(`< > >= <=`)对 NaN/+inf/-inf 输入全与解释器逐位一致(kernel 专测 vnan/vinf/vninf 过四谓词,结果 `-nan` off==on)。
- **resume-at-SELF 不变**:cond-return 方法的字段读守卫仍 resume-at-SELF(§10.48);浮点 select 本身无分支无守卫。安全网(§10.46)仍兜底。
- 整数 cond-return **零回归**(门改 `_any` 只新增浮点/常量 op;`/` 产 float 故整数臂里出现 `/` 会让该臂变浮点 → 走浮点 select 或混合 abort,无整数陷阱)。

### 效果(全部位精确)
float cond-return `a.relu()+a.clamphi()`(64M call-pair)**1.0×(此前 abort 回退)→ 5.31×**。battery 全 compiled + off==on 位精确:relu(ReLU)、clamphi(`>` 换序)、ge0(`>=` 换序)、le1(`<=`)、absf(`0.0-this.x`)、maxp/minf(变量臂)、自由函数 clampf/minf、float DIV 臂、NaN/+inf/-inf。**混合臂、int-臂-浮点-比较 干净 abort**(off==on)。`entries==exits`、`guard_fail=0`。§10.46–§10.49 零回归。

### 验证(完整门槛,全绿)
**306 ctest**(+kernel `condreturn_float.spt`:九循环含 NaN/inf 过四谓词)· **92 kernel** · **303 全量差分** · 新生成器 `gen_condreturn_float`(relu/clamp/abs/max/min/ge/自由函数/NaN输入/混合回退 十形,随机注入 inf/nan/-inf)120 例 0 失配(108 编译 + 12 正确回退)· **全 GENS(42 个)seeds 1-10 × 300 = 3000 例 0 失配** · **ASan+UBSan 全干净**(guard-heavy 17-kernel 子集含 condreturn_float + 浮点案例 0 报告 + 300 例 fuzz) · entries==exits、guard_fail=0。

**关键教训**:
(1)**接已验证的浮点 select 机器到新上下文**:§10.30 早为条件赋值建好 FCMPMASK/ICMPMASK/FSELECT(位精确 + NaN 语义),本节只把它接到 cond-return、并把门放宽到允许浮点常量/除——**零新 codegen**。又一例"已验证机器 × 新场景"的低风险扩展(§10.47 单例→列表、§10.48 cond-return 接方法、§10.49 写方法子集 一脉相承)。
(2)**整数 vs 浮点安全集的差异是真实的**:`opcode_is_ifconv_safe`(无 LOADF、无 DIV)vs `_flt`(有 LOADF/LOADK/DIV)——浮点 cond-return 必须用 `_any` 才能放行浮点常量;而陷阱整数除 `//`(IDIV)在两集都没有,故放宽到 `_any` 不引入整数除零风险(`/` 恒产 float 自动转浮点臂)。这处类型分析是本节正确性的关键。
(3)**返回常量 vs 返回变量的 abort 差异**一眼定位缺口:同一形态因"是否需要加载常量"而编译/abort 分裂,直接指向常量加载 op 未入安全集——窄 repro + DEBUG 退出 op 的标准定位法(§10.21/§10.49 延续)。

## §10.51 条件写方法 if-conversion:in-place clamp / 滑动最大最小跟踪器(接 §10.48 select × §10.49 单尾写,✅ 阶段达成)

§10.49 落地了**单尾写 void 方法**(setter / 累加器,直线体)。但带分支的写方法——尤其是极常见的**滑动最大/最小跟踪器** `void track(int x){ if(x > this.peak) this.peak = x; }` 和 **in-place clamp** `void clampLow(){ if(this.peak < 0) this.peak = 0; }`——仍全 abort(分支 + 写,无门接收)。本节把这类 `if(c) this.f = A;`(if-only 条件字段写)**if-转换**成一条无条件尾写 `this.f = select(c, A, old)`,其中 old 是 this.f 的当前值。这是"**已验证机器 × 新场景**"的又一例:select 用 §10.30/§10.48 的位精确机器(int CMPSET、float FCMPMASK/FSELECT),单尾写安全性用 §10.49,**零新 codegen、零入口布局守卫**。

### 缺口确认(字节码实测,/tmp/dump.c 探针)
`void track(int x){ if(x>this.peak) this.peak=x; }`(numparams=2: this=R0, x=R1):
`[0]GETFIELD R2=this["peak"](=old,比较里读) [1]LT R2<R1(this.peak<x) [2]JMP +1 [3]SETFIELD this["peak"]=R1(x;C=寄存器,k=0) [4]RETURN0`。
`clampLow`:`[3]SETFIELD this["peak"]=K1(0;C=常量,k=32768)`。**关键观察**:被写字段在比较里作为操作数被读(=old),被写的值 = RK(C)(寄存器或常量),SETFIELD 是单条尾 op。

### 实现(门 + 录制器 + 两处接线)
1. **静态门 `proto_is_condwrite_method_inlinable`**(形状专一):恰一个 SETFIELD 且位于 then 臂末 op(`wi == t1-1`,排除 if-else 的尾 JMP)、比较 + 前向 JMP 到 merge、then 臂 compute 非陷阱(`condreturn_method_op_ok`,无条件求值)、merge 后只 void 返回、无 RETURN1。**纯形状**;"比较读被写字段"由录制器验证。接入 OP_CALL 方法门(第 4 个 `|| proto_is_condwrite_method_inlinable`)。
2. **录制器 `rec_try_condwrite_ifconv`**(`rec_cond_branch` 第三个 if-conv 尝试,在 cond-return 后):
   - `old_ref` = aref/bref 中、IR op 为 `SPTIR_GETFIELD` 且 aux(键指针)== SETFIELD 键者;否则干净 abort(无 old 来源)。**这是把"比较读被写字段"落实的关键**——免去单独发 GETFIELD。
   - 录 then 臂 compute `[cmp+2, sf_pc)`(常为空或单条常量加载),`A_ref = rec_load_rkc(*sf_pc)`(寄存器或常量)。
   - 建 `select(fop, A, old)`:int → CMPSET + `old+(A-old)*cond`;float → FCMPMASK(int 提升、`>`/`>=` 换序保 NaN 为假)/ICMPMASK + FSELECT。混合类型 abort。**复制 §10.50 的 select 逻辑**(~25 行,不重构在用代码,降风险)。
   - **直接发 SETFIELD**(复制 OP_SETFIELD handler 核心:base 经 `rec_eval_container` 解析、键存在性链遍历、`rec_snap`、`SPTIRF_GUARD` 标志),但值用 `res` 而非 RK(C)——绕开"C 可能是常量"无法 override 寄存器的问题。
   - `rc->pc = T1`(merge),返回 1。caller 续 merge → RETURN0 → void 返回(§10.49 已处理内联帧 RETURN0)。

### 正确性论证(与 §10.49 同一安全类 + §10.30 位精确)
- **无双写**:单条 SETFIELD 是最后发守卫的 op,所有守卫(字段读键/类型守卫、SETFIELD 自身键守卫——codegen 在存储前求值)都在写之前。任一守卫失败 → 写提交前侧退出 → 解释器重跑 SELF+CALL+方法恰写一次。**条件写求和字段值 = 双写探测器**:双写或选错值会改变滑动极值、使和与解释器发散。
- **if-only 语义等价**:`if(c) this.f=A` ≡ `this.f = c ? A : old`(c 假时把 old 写回 = 原值无副作用的 raw 存储,类实例字段是 rawset 语义无 __newindex)。
- **位精确 + NaN 为假**:float select 用掩码混合(绝不用整数 `else+(then-else)*cond` 会舍入);`>`/`>=` 换序成 `<`/`<=`。NaN/+inf/-inf 输入下滑动极值与解释器逐位一致。
- **安全边界全 abort 且保持正确**:双写(>1 SETFIELD,门拒)、读后写/值返回(RETURN1,门拒)、比较读别的字段(录制器 old_ref 找不到)、if-else(`wi != t1-1`,门拒)——全 compiled=0、off==on。

### 效果(全部位精确)
滑动最大 `t.track(...)`(64M 条件写)**1.0×(此前 abort)→ 2.88×**。battery 全 compiled + off==on:track max/min(寄存器写值)、clampLow(常量写值 k-flag)、cap(参数比较)、float track(FSELECT)、NaN/-inf 起始。

### 验证(完整门槛,全绿)
**307 ctest**(+kernel `condwrite_method.spt`:七循环 int/float 滑动极值 + clamp/cap + NaN/-inf)· **93 kernel** · **304 全量差分** · 新生成器 `gen_condwrite_method`(max/min/clamp/cap/float×2 + 四类安全边界 十形)160 例 0 失配(89 编译 + 71 正确 abort)· **全 GENS(43 个)seeds 1-10 × 300 = 3000 例 0 失配** · **ASan+UBSan 全干净**(guard-heavy 18-kernel 子集含 condwrite_method + 写路径 0 报告 + 300 模糊)· entries==exits、guard_fail=0。§10.46–§10.50 零回归。

**关键教训**:
(1)**组合两个已验证机器**:§10.48 的 select(分支无关求值)+ §10.49 的单尾写安全性 = 条件写,无需任何新机器。又一例低风险扩展(§10.47→48→49→50→51 一脉:单例→列表、cond-return 接方法、写方法子集、浮点 cond-return、条件写)。
(2)**从比较操作数取 old 是关键简化**:`if(c) this.f=A` 的 else 值是 this.f 当前值;常见形态(比较并更新同字段)里它恰是比较的一个操作数(被写字段的 GETFIELD)。匹配 IR op==GETFIELD && aux==键 即拿到 old,免去单独发字段读 + CSE 的复杂度,也顺带把适用范围收窄到安全且常见的"比较读被写字段"模式。
(3)**直接发 SETFIELD 而非 R[C]-override**:被写值 RK(C) 可能是常量(clamp 写 0 时 k-flag),override 寄存器无效。直接复制 SETFIELD handler 核心、用 select 结果作值,既正确又避开常量/寄存器分支。
(4)**通用多写仍是下一道坎**:本节是"单尾写 + 分支"的窄安全子集;通用多写(`this.a=..;this.b=..`、读改写)需入口期字段布局守卫让在体守卫冗余 + 可能的 NOGUARD 字段访问 codegen,是真正需要新写 codegen 的项,留作专项。

## §10.52 写后返字段方法 via 存转载前推:计数器 / ID 生成器 / 累加返新值(接 §10.49 单尾写,✅ 阶段达成)

§10.49 落地**单尾写 void 方法**(写是体内最后产生守卫的 op → resume-at-SELF 不双写),但 §10.49 的门把"写后还有 op"一律拒绝——于是极常见的 **`int inc(){ this.v = this.v + 1; return this.v; }`(自增并返新值)、`int add(int x){ this.v = this.v + x; return this.v; }`(累加返运行总和)、`int next(){ this.id = this.id + 1; return this.id; }`(ID 生成器)** 全 abort。本节用**针对性的存转载前推(store-to-load forwarding)**把写后对**同一被写字段**的重读直接前推到刚写入的值(无守卫),使 SETFIELD 仍是最后产生守卫的 op,§10.49 的单尾写安全性原样成立——**零新写 codegen、零入口布局守卫**。

### 缺口确认(字节码实测,/tmp/dump.c 探针)
`int inc(){ this.v = this.v + 1; return this.v; }`(numparams=1):
`[0]GETFIELD R1=this.v(=old) [1]ADDI R1=R1+1 [2]MMBINI [3]SETFIELD this["v"]=R1(C=寄存器) [4]GETFIELD R1=this.v(写后重读!同字段) [5]RETURN1 R1 [6]RETURN0`。
**关键观察**:`return this.v` 在 `this.v = ...` 之后编出**第二条、写后的 GETFIELD** 重读被写字段——正是这条写后守卫让 §10.49 的门拒绝它(写后有产生守卫的 op = 双写风险)。

### 实现(前推状态 + GETFIELD 前推/拦截 + 门放宽,五处接线)
1. **录制器上下文 3 字段**(SPTRecCtx):`fwd_base`(上一条 SETFIELD 的 base IR ref,-1=无)、`fwd_key`(TString* 键)、`fwd_val`(写入值的 IR ref)。
2. **SETFIELD 发射后置位**:`rc->fwd_base=aref; rc->fwd_key=key; rc->fwd_val=cref;`(在正常 OP_SETFIELD handler 内)。
3. **GETFIELD 前推 + 拦截**(仅 `frame_base != 0`,即内联方法体内;算出 bref+key 后):若 `bref==fwd_base && key==fwd_key` → **前推**:`reg_map[fb+a]=fwd_val`、`reg_type=sptir_type(fwd_val)`,不发射、不产生守卫;否则(方法体内写后的**非前推**读)→ **干净 abort**(它会在写后发产生守卫的 GETFIELD,resume-at-SELF 重跑整个方法 = 双写)。
4. **重置/失效**:trace 起始 `fwd_base=-1`;**每次方法内联入口**(两处 `frame_base=new_fb` 后)`fwd_base=-1`(每个内联方法独立前推上下文);OP_CALL / OP_SETI / OP_SETTABLE handler 顶部 `fwd_base=-1`(调用/数组写/泛写可能别名)。
5. **静态门 `proto_is_write_method_inlinable` 放宽**:写(wi)之后,除 void 尾(RETURN0/RETURN/EXTRAARG)外,**也允许**"返被写字段"尾——`GETFIELD(键 == 被写字段键)` + MOVE + RETURN1。任何**别的 op**(读别的字段、数组取下标、再算术)仍拒绝(会在写后产生守卫)。

### 正确性论证(与 §10.49 同一安全类 + 前推免守卫)
- **无双写**:前推把写后的同字段重读变成"用已写入的 IR 值",**不发产生守卫的 GETFIELD**。于是 SETFIELD 仍是最后产生守卫的 op,所有守卫在写前;守卫失败 → 写提交前侧退出 → 解释器重跑 SELF+CALL+方法恰写一次。**返回值之和(随计数器/总和逐迭代精确演化)= 正确性 + 双写双重探测器**:前推取错值或双写都会使和与解释器发散。
- **前推恒正确**:精确 `base IR ref` + `key` 匹配(同 ref ⟹ 同表;键相同 ⟹ 同字段),且方法体直线、写与读之间无调用/数组写(门保证只 GETFIELD-被写字段/MOVE/返回);别名经保守失效(调用/SETI/SETTABLE 一律清前推)排除。不同 ref 指向同表 → 不匹配 → 不前推(保守正确)。
- **拦截网兜底**:方法体内写后的任何**非前推** GETFIELD → abort,绝不发写后守卫。现有可内联形态(纯读/cond-return/写 void/条件写)**无一在写后读字段**,故前推**只对本节新形态生效或 abort**——对所有现有 case **零行为改变**(根 trace `frame_base==0` 完全不走前推;根的写-读-字段本就安全,因根快照精确、非 resume-at-SELF 重跑)。
- **跨字段前推正确**:`int f(){ this.a = this.b + 1; return this.a; }` 写 this.a(值 = this.b+1)、写后重读 this.a → 前推到 this.b+1。位精确。
- **安全边界全 abort 且保持正确**:写 a 返**别的字段** b(`this.a=x; return this.b`,拦截网 abort)、多写(`this.a=..;this.b=..`,门拒)——全 compiled=0、off==on。

### 效果(位精确)
计数器 `c.inc()`(自增并返新值,64M 次)**1.0×(此前 abort)→ 5.48×**。battery 全 compiled + off==on:inc/add/next/set(寄存器写值)、float add(浮点字段)、跨字段 f;两类安全边界(返别的字段、多写)正确 abort。

### 验证(完整门槛,全绿)
**308 ctest**(+kernel `writeret_method.spt`:inc/add/next/set/float/跨字段 六形 + 返别的字段/多写 两类 fallback,返回值之和为双写探测器)· **94 kernel**(0 no-jit)· **305 全量差分** · 新生成器 `gen_writeret_method`(inc/addret/nextid/setret/compute/cross/float + 两类 fallback 九形)160 例 0 失配(131 编译 + 29 正确 abort)· **全 GENS(44 个)seeds 1-10 × 300 = 3000 例 0 失配** · **ASan+UBSan 全干净**(写后返字段六微例 + guard-heavy 19-kernel 子集含 writeret_method + seed 777×60 模糊)· entries==exits(599964)、guard_fail=0。§10.46–§10.51 零回归(write_method.spt 中原"读后写必 fallback"的 RW 例现经前推内联,结果仍位精确——注释已更新)。

**关键教训**:
(1)**存转载前推是把"写后读"变安全的最小机器**:`this.v=A; return this.v` 的字节码必有写后重读 GETFIELD;把它前推到 A(已写入值)即免去写后守卫,§10.49 单尾写安全性原样成立。无需任何新写 codegen / 入口布局守卫——又一例"已验证机器 × 新场景"(§10.47→…→52 一脉)。
(2)**把前推限定在内联方法体 + 精确 ref/key 匹配,使影响面恰为新特性**:现有可内联形态无一写后读字段,故前推对它们从不触发;根 trace 完全不走前推。零回归面来自这一限定 + 拦截网(写后非前推读 → abort)。
(3)**前推正确性靠精确匹配 + 保守失效**:`base IR ref` 相等保证同表、`key` 相等保证同字段;调用/数组写/泛写一律清前推排除别名;不同 ref 指同表则不前推(漏优化但安全)。这套"宁可不前推也不错前推"是位精确的根基。
(4)**根与内联方法的写-读语义差异**:根 trace 快照精确(守卫失败原地恢复,不重跑既往写),故根的写-读-字段本就安全,无需前推也无双写;内联方法用 resume-at-SELF(重跑整个方法),才需前推免去写后守卫。前推与拦截网都只在 `frame_base != 0` 生效正是这一差异的体现。
(5)**通用多写仍是下一道坎**:本节是"单尾写 + 写后返同字段"的安全扩展;通用多写(`this.a=..;this.b=..`)需入口期字段布局守卫让在体守卫冗余 + 可能的 NOGUARD 字段访问 codegen,是真正需要新写 codegen 的项,本节已精准 abort,留作专项。

## §10.53 条件累加方法:`if(cond) this.sum = this.sum + x`(接 §10.51,old 取自 then 臂字段读,✅ 阶段达成)

§10.51 把 if-only 条件字段写 `if(c) this.f = A` if-转换成一条无条件尾写 `this.f = select(c, A, old)`,其中 **old(this.f 的未改值)取自比较操作数**——这要求**比较读被写字段**(`if(x > this.peak) this.peak = x`,滑动极值/clamp/cap)。但极常见的**条件累加**——`void add(int x){ if(x > 0) this.sum = this.sum + x; }`(求正数和)、`if(x >= k) this.n = this.n + 1`(条件计数器)——条件在**别的值**(x)上,old(this.sum)不在比较里 → §10.51 在比较处 abort(画像实测 abort@op=65 比较)。本节让 old **也可取自 then 臂自身对被写字段的读**:`this.sum + x` 里的 `this.sum` 正是 old。**复用 §10.51 的全部 select + 单尾写机器,零新 codegen**;改动仅 `rec_try_condwrite_ifconv` 里 old 来源一处。

### 缺口确认(画像 + 字节码)
`void add(int x){ if(x > 0) this.sum = this.sum + x; }`:then 臂 `GETFIELD this.sum(=old); ADD old+x(=A); SETFIELD this.sum=A`。比较 `x > 0` 的操作数是 x、0,均非 this.sum 的 GETFIELD → §10.51 的 `else return 0` → rec_cond_branch 在方法内 abort。**关键观察**:被写字段在 then 臂里被读(=old),只是不在比较里。

### 实现(old 双来源,一处改动)
`rec_try_condwrite_ifconv` 的 old 来源从"仅比较操作数"扩为两路(此时尚无写提交——条件写是唯一 SETFIELD 且最后发,故方法内任何对 this.f 的读都得未改值):
1. **比较操作数**(§10.51):aref/bref 中 IR op==GETFIELD 且 aux==被写键者。
2. **then 臂字段读**(§10.53,新):若来源 1 未中,**先录 then 臂**(原本就录),再在 then 臂发射的 IR 区间 `[watermark, ninst)` 里找 op==GETFIELD 且 aux==被写键的指令——即 `this.sum + x` 里的 `this.sum`。找到 → old_ref;找不到 → abort(无 old 来源,干净回退)。
实现上把 §10.51 的 `else return 0`(找 old 前提前返回)去掉,改为录 then 臂后再决定;录后找不到 old 则 `rc->aborted=1; return 1`(整条 trace 丢弃,IR 污染无碍)。其余(select 构建、直接发 SETFIELD)完全不变。

### 正确性论证(与 §10.51 同一安全类)
- **无双写**:仍是**唯一**一条 SETFIELD、最后发,所有守卫在写前 → resume-at-SELF 只在写提交前触发 → 解释器重跑恰写一次。**条件累加字段和 = 双写 + 选错值双重探测器**:双写或 old/A 取错都会使和发散。
- **old 语义正确**:条件写是唯一写,此前无写提交,故方法内任何 `this.f` 的读都是未改值 = old;then 臂的 `this.sum` 读即 old。select(cond, A, old) = `cond ? (this.sum+x) : this.sum` = `this.sum + (cond ? x : 0)`。位精确(int CMPSET、float FCMPMASK/FSELECT,NaN 为假)。
- **作用域正确**:在 then 臂发射的 IR 区间内按**被写键**匹配 GETFIELD——同一方法对 `this` 操作,故区间内键==被写键的 GETFIELD 必是 this.<被写字段>(别的字段 aux 不同)。CSE 不影响:条件写前无对该字段的写,若该字段此前未读则 then 臂的读是新发射的(在区间内);若此前读过则 CSE 到更早 ref(区间外)→ 找不到 → 干净回退(罕见,安全)。
- **安全边界全 abort 且保持正确**:跨字段条件写(`if(c) this.a = this.b + x`,then 臂不读 this.a → 找不到 old → abort)、条件常量写且条件无关(`if(x>0) this.v = 5`,old 处处不读 → abort)、多写/值返回/if-else(§10.51 既有边界)——全 compiled=0、off==on。

### 效果(位精确)
求正数和 `s.add(...)`(45M 条件累加)**1.0×(此前 abort)→ 4.36×**。battery 全 compiled + off==on:求和/条件计数器/条件乘累加(寄存器)、float 条件累加(FSELECT)、§10.51 滑动极值/clamp 零回归。

### 验证(完整门槛,全绿)
**308 ctest**(`condwrite_method.spt` 扩入 §10.53:求正数和 + 条件计数器 + float 条件累加,字段和为双写探测器)· **94 kernel** · **305 全量差分** · `gen_condwrite_method` 增 rmw_sum/rmw_count/rmw_scale/float_rmw_sum 四形,180 例 0 失配(117 编译)· **全 GENS(44 个)seeds 1-8 × 300 = 2400 例 0 失配** · **ASan+UBSan 全干净**(条件累加四微例含跨字段 abort + guard-heavy 19-kernel 子集含扩充后的 condwrite_method + seed 555×60 模糊)· entries==exits、guard_fail=0。§10.46–§10.52 零回归。

**关键教训**:
(1)**同一 if-转换、放宽 old 来源即解锁一大类**:§10.51 把 old 绑死在"比较读被写字段"上,漏掉了"条件在别处、then 臂读字段"的条件累加——而后者(求和/计数/打分的 `if(valid) acc += x`)比前者更常见。加一路 old 来源(then 臂字段读),零新机器,解锁条件累加。又一例"已验证机器 × 新场景"(§10.47→…→53)。
(2)**无写提交 ⟹ 任意字段读皆为 old**:条件写是唯一写且最后发,故 if-转换时刻此前无写,方法内 `this.f` 的任何读都得未改值。这把"哪儿取 old"从"必须是比较操作数"放宽到"方法内任一对该字段的读"——by-key 在 then 臂 IR 区间里找即可。
(3)**先录后判、污染无碍**:把"找不到 old 就提前 return 0"改成"录 then 臂后再判,找不到则 abort 整条 trace"。因 abort 即丢弃整条 trace,部分录入的 IR 不影响正确性——这让 old 的第二来源(必须录完 then 臂才知道)成为可能。
(4)**跨字段条件写仍 abort**:`if(c) this.a = this.b + x`(写 a、then 臂读 b 不读 a)的 old(this.a 未改值)无处可取,本节干净 abort。补它需在写前补发一条 `GETFIELD this.a`(读,守卫在写前,安全),留作后续小扩展。

## §10.54 if-else 条件字段写:`if(c) this.g = A; else this.g = B`(接 §10.51/§10.53,补全条件写,✅ 阶段达成)

§10.51(old 取比较操作数)+ §10.53(old 取 then 臂读)覆盖了 **if-only** 条件字段写 `if(c) this.f = A`。本节补 **if-else** 形态 `if(c) this.g = A; else this.g = B`——**两臂写同一字段**,if-转换成一条无条件尾写 `this.g = select(c, A, B)`。两臂的值都显式给出,**无需 old**。**与 if-only 路径完全并列、互不影响**:新增独立门 `proto_is_condwrite_ifelse_method_inlinable` + 独立录制 `rec_try_condwrite_ifelse_ifconv`,if-only 路径(§10.51/§10.53)一行未改 → 对其零回归。复用 §10.30 select + §10.49 单尾写安全性,**零新 codegen**。

### 缺口确认(画像 + 字节码)
`void cls(int x){ if(x > 0) this.sign = 1; else this.sign = 0-1; }`(this=R0, x=R1):
```
[0] GTI x > 0          比较
[1] JMP -> else_start  条件不成立跳 else
[2] SETFIELD this.sign=1   then 臂写
[3] JMP -> merge       跳过 else
[4] SETFIELD this.sign=-1  else 臂写
[5] RETURN0            merge
```
关键:`T1`(=[1] 的 JMP 目标)= else_start = [4];`T1-1` = [3] = **JMP**(不是 SETFIELD)。§10.51/§10.53 的结构检查要求 `T1-1 == SETFIELD`,故对 if-else 直接 return 0 → 不接管。画像实测在外层 CALL 处 abort(方法因含两条 SETFIELD 不过 if-only 门)。

### 实现(并列 if-else 路径)
**门 `proto_is_condwrite_ifelse_method_inlinable`**:恰两条 SETFIELD、无 RETURN1;比较 + JMP→else_start;`then_jmp = else_start-1` 必为 JMP、`then_sf = then_jmp-1` 必为 SETFIELD(=第一条);`merge = then_jmp 目标`;`else_sf = merge-1` 必为 SETFIELD(=第二条);**两条 SETFIELD 写同一键**;前缀 method_read_op_ok、两臂 compute condreturn_method_op_ok、merge 后 void 收尾。
**录制 `rec_try_condwrite_ifelse_ifconv`**:同结构检测(T1-1=JMP 区分);两条 SETFIELD 同键 + 同接收者寄存器(this);**各臂在 fork 帧上录 compute**(救/复原 reg_map/reg_type,cmax≤64 save_map[64],与 rec_try_condreturn_ifconv 同法,避免两臂临时寄存器互扰),A=rec_load_rkc(*then_sf)、B=rec_load_rkc(*else_sf);`select(cond, A, B)`——**无 old**,int CMPSET/SUB/MUL/ADD(`B+(A-B)*cond`)或 float FCMPMASK/ICMPMASK/FSELECT(NaN 为假);直接发一条守卫 SETFIELD(复刻 OP_SETFIELD 核:base 解析、键存在链走、快照、守卫发射);rc->pc=merge。
**接线**:rec_cond_branch 在 rec_try_condwrite_ifconv 调用后加一行;OP_CALL 方法门加 `|| !proto_is_condwrite_ifelse_method_inlinable`。

### 正确性论证(与 §10.51/§10.53 同一安全类)
- **无双写**:if-转换后只发**一条** SETFIELD、最后发,所有守卫在写前 → resume-at-SELF 只在写提交前触发 → 重跑恰写一次。字段和为双写 + 选错值双重探测器。
- **两臂值正确**:A、B 各在 fork 帧上录(两臂从同一前缀帧态出发,临时寄存器不互扰),作为 IR ref 捕获后 select。select(cond, A, B) = cond ? A : B,位精确(int 整数混合精确;float FSELECT 位级 blend,GT/GE 换 LT/LE 保 NaN 为假)。
- **同字段 + 同接收者**:门与录制都校验两条 SETFIELD 写同键且同 this 寄存器 → select 出的值写回唯一目标字段。写不同字段(`if(c) this.a=1; else this.b=2`)两条键不同 → 门拒 → 干净 abort。
- **与 if-only 零交叉**:T1-1 是 JMP(if-else)还是 SETFIELD(if-only)严格二分;rec_try_condwrite_ifconv 见 if-else 的 T1-1=JMP 即 return 0(结构检查 line `GET_OPCODE(*sf_pc)!=OP_SETFIELD`),rec_try_condwrite_ifelse_ifconv 见 if-only 的 T1-1=SETFIELD 即 return 0(要求 then_jmp 为 JMP)。两路互斥,各管各的。

### 效果(位精确)
abs-via-if-else `if(x>0) this.v=x; else this.v=-x`(45M if-else 写)**1.0×(此前 abort)→ 4.54×**。battery 全 compiled + off==on:常量/常量(sign)、寄存器/常量(max-like)、两臂 compute、float(abs,FSELECT)、条件读被写字段(cmpfld);§10.51 滑动极值 + §10.53 条件累加零回归。

### 验证(完整门槛,全绿)
**308 ctest**(`condwrite_method.spt` 扩入 §10.54:sign/pick/float-abs 三 if-else 类 + 循环)· **94 kernel** · **305 全量差分** · `gen_condwrite_method` 增 ifelse_sign/ifelse_pick/ifelse_compute/ifelse_float/ifelse_cmpfld 五形 + ifelse_difffield abort,220 例 0 失配(168 编译)· **全 GENS(44 个)seeds 1-8 × 300 = 2400 例 0 失配** · **ASan+UBSan 全干净**(if-else 四微例含写异字段 abort + guard-heavy 19-kernel 子集含扩充后的 condwrite_method + seed 333×60 模糊)· entries==exits、guard_fail=0。§10.46–§10.53 零回归。门槛计数不变(308/94/305,折入既有 condwrite 测试)。

**关键教训**:
(1)**并列路径补形态、零回归**:if-else 与 if-only 结构严格二分(T1-1 是 JMP 还是 SETFIELD)。与其把 if-else 塞进 rec_try_condwrite_ifconv(冒污染 if-only 的险),不如开一条并列的门 + 录制,if-only 一行未改。结构互斥保证两路永不打架。这是给一个成熟函数加形态的低险范式。
(2)**两臂都显式 ⟹ 无需 old**:if-only 的全部精巧都在"未改值 old 从哪来"(§10.51 比较、§10.53 then 臂)。if-else 两臂都写了显式值,select(c,A,B) 直接成立,反而比 if-only 简单——不必找 old。
(3)**fork 帧录两臂**:两臂可能复用同一批临时寄存器,顺序录会让 else 臂读到 then 臂的写。沿用 rec_try_condreturn_ifconv 的"每臂前救 reg_map、录完复原"——两臂都从同一前缀帧态出发。A、B 作为 IR ref 在各自录完即捕获,后续复原不影响已捕获的 ref。
(4)**条件写故事收口**:if-only(§10.51 比较 old / §10.53 then 臂 old)+ if-else(§10.54 两臂显式)。剩跨字段条件写(`if(c) this.a=this.b+x`,需写前补发 GETFIELD 取 old)与通用多写(需入口布局守卫)留待后续。

## §10.55 条件写 old 缺位时补发字段读:跨字段/条件常量/字段拷贝(接 §10.51/§10.53,补全 if-only old 来源,✅ 阶段达成)

§10.51(old 取比较操作数)+ §10.53(old 取 then 臂读)覆盖了 if-only 条件写 `if(c) this.f = A` 中 **old 能从已有 IR 里找到**的情形。但还有一类 if-only 条件写,**被写字段在方法里压根没被读**——条件在别的值上,且 then 臂写的值不来自被写字段:
- **跨字段写** `if(c) this.a = this.b + x`(then 臂读 this.b,不读 this.a);
- **条件常量写** `if(x>0) this.v = 5`(写常量,条件无关);
- **字段拷贝** `if(c) this.a = this.b`(把 b 拷给 a);
- **条件读异字段** `if(x>this.g) this.v = x`(条件读 this.g,then 臂写参数 x)。
这些里 old(被写字段的未改值)在比较和 then 臂里都没有 → §10.51/§10.53 在 old 缺位处 abort。本节让 recorder 在 old 缺位时**补发一条对被写字段的守卫读**(GETFIELD)取 old。**复用既有 GETFIELD 发射 + select + 单尾写机器,改动仅 old 缺位分支一处**(把"abort"换成"补发 GETFIELD")。

### 缺口确认(画像)
跨字段 `if(x>0) this.a=this.b+x`、条件常量 `if(x>0) this.v=5`、字段拷贝 `if(c) this.a=this.b`:画像实测全在方法体比较处 abort(rec_try_condwrite_ifconv 的 old 缺位分支 `rc->aborted=1`)。共同点:被写字段从不被读,old 无现成来源。

### 实现(old 缺位 → 补发 GETFIELD)
rec_try_condwrite_ifconv 的 old 三级来源:① 比较操作数(§10.51);② then 臂对被写字段的读(§10.53);③ **(新)补发一条 GETFIELD 读被写字段**(§10.55)。第三级复刻 OP_GETFIELD 核:`rec_load_reg(sf_a)` 取接收者 base、`rec_eval_container` 解析容器、走被写键的 main-position + 冲突链确认存在并预测值类型、`sptir_emit(SPTIR_GETFIELD,...)` + 快照 + 守卫;要求 old 为 INT/FLT(供 select)。得到的 GETFIELD 即 old_ref。

### 正确性论证(与 §10.51/§10.53 同一安全类)
- **读在写前 ⟹ 取到未改值**:条件写是唯一 SETFIELD 且最后发,补发的 GETFIELD 在它之前,此前无写提交 → 读得字段未改值 = old。
- **守卫在写前 ⟹ 无双写**:补发的 GETFIELD 是一条**读**(带守卫,守卫在唯一 SETFIELD 之前),不增加写;仍恰一条 SETFIELD、最后发 → resume-at-SELF 只在写提交前触发 → 重跑恰写一次。§10.49 单尾写安全性原样成立。
- **select 正确**:select(cond, A, old)。跨字段 A=this.b+x、old=this.a → cond ? this.b+x : this.a;字段拷贝 A=this.b、old=this.a → cond ? this.b : this.a;条件常量 A=5、old=this.v → cond ? 5 : this.v。全位精确(int 混合精确,float FSELECT)。
- **字段存在性**:补发 GETFIELD 走链确认被写字段存在,缺失则 abort——与紧随其后的 SETFIELD 发射同样要求字段存在,故"SETFIELD 能成 ⟺ GETFIELD 能成",一致。
- **类型不匹配/非数值 old 干净 abort**:A 与 old 类型不同(如 this.a int、this.b float)或 old 非数值 → select 回退 abort,off==on。

### 效果(位精确)
跨字段写 `if(x>0) this.a=this.b+x`(45M)**1.0×(此前 abort)→ 3.33×**。battery 全 compiled + off==on:跨字段 RMW、条件常量写、条件标志置位、字段拷贝、float 跨字段;§10.51 滑动极值 + §10.53 条件累加零回归。

### 验证(完整门槛,全绿)
**308 ctest**(`condwrite_method.spt` 扩入 §10.55:跨字段 RMW + 字段拷贝两类 + 循环)· **94 kernel** · **305 全量差分** · `gen_condwrite_method` 增 xfield_rmw/constw_unrel/fieldcpy/float_xfield 四形,且原 cmp_other abort 形现经 §10.55 编译(注释改正),260 例 0 失配(220 编译)· **全 GENS(44 个)seeds 1-8 × 300 = 2400 例 0 失配** · **ASan+UBSan 全干净**(§10.55 四微例 + guard-heavy 19-kernel 子集含扩充后 condwrite_method + seed 444×60 模糊)· entries==exits、guard_fail=0。§10.46–§10.54 零回归。门槛计数不变(308/94/305,折入既有 condwrite 测试)。

**关键教训**:
(1)**old 三级来源、逐级放宽**:§10.51 比较操作数 → §10.53 then 臂读 → §10.55 补发读。每加一级解锁一类常见条件写(跟踪器→累加→跨字段/常量/拷贝),全程零新 codegen——只是"未改值从哪取"的来源在拓宽。条件写 if-only 故事至此收口:无论被写字段是否在方法里被读,old 都有来源。
(2)**补发读是安全的 old 兜底**:当 old 在 IR 里找不到,补发一条守卫读最直接。安全性来自时序——读在唯一写之前,故取未改值;守卫在写前,故无双写。这把 §10.51/§10.53 "找得到才转换"放宽成"找不到就读出来"。
(3)**读后写 vs 写后读**:§10.55 补发的是**写前读**(安全);§10.52 处理的是**写后读**(靠存转载前推消守卫才安全)。二者时序相反、安全论证不同,但都让单尾写 SETFIELD 保持最后一个产生守卫的 op。
(4)剩通用多写(两条 SETFIELD,需入口布局守卫)、双侧 clamp(链式 cond-return,需递归 if-转换 + 比较解码)、嵌套内联(需帧栈)——皆需新机器,各自专项。

## §10.56 链式条件返回 / 双侧 clamp:折叠为嵌套无分支 select(单条 cond-return 之上的链式扩展,✅ 阶段达成)

§10.48/§10.50 把**单条** cond-return `if(c) return A; return B` if-转换为一个 select。但**值钳制(clamp)**——`int clamp(int x, int lo, int hi){ if(x<lo) return lo; if(x>hi) return hi; return x; }`——是**两条** cond-return 链:第一条的 else 臂本身又是一条 cond-return。这是极常见的数值惯用法(颜色/坐标/采样钳制),此前在 CALL 处 abort(无任何门槛接受)。本节加**并行**链式路径,把 K(≥2)条 cond-return + 末尾 return 折叠为**内向外嵌套的无分支 select** `select(c1, A1, select(c2, A2, ... select(cn, An, Afinal)...))`——无分支、无副作用、无侧出口。单条 cond-return 路径**原样不动**(单条在链式录制器里返回 0,落到旧路径)。

### 缺口确认(画像)
free-fn `clamp(x,lo,hi)`、method `clamp(x)`(读 this.lo/this.hi)、立即数边界 `if(x<2)../if(x>12)..`、float clamp、三段链——画像全在 CALL(op=69)处 abort:链式 cond-return 的第一条的 else 臂含第二个比较,而 `proto_is_condreturn_*` 门槛只找**第一个**比较、其 else 臂里出现比较即拒。字节码:`CMP;JMP→T1;RET1(lo); [T1]CMP;JMP→T2;RET1(hi); [T2]RET1(x); RET0`,两个比较均 OP_LT 寄存器式(`x>hi` 编为 `hi<x`)。

### 实现(三件,全并行)
1. **decode_compare(rc,pc,…)**:把比较指令解码成 (fop, aref, bref, at, bt)——复刻各 OP_LT/LE/LTI/LEI/GTI/GEI/EQ/EQI 处理器的操作数加载 + 由 k 标志定 fop(then 臂取向)。链式录制器**手动**处理第二条起的比较(不走 rec_inst,以免触发单路径的 bind-and-unwind),故需此解码助手。
2. **emit_select(rc,fop,…,then,else,out)**:抽出 int/float select 机器(原在各 if-转换器里重复的 ~25 行):int 用免舍入 `else+(then-else)*c`,float 用掩码混合(FCMPMASK/ICMPMASK + FSELECT,GT/GE 交换成 LT/LE 保 NaN-as-false)。折叠循环复用它;单 if-转换器各自的内联副本不动。
3. **rec_try_chained_condreturn_ifconv**:结构 PEEK——仅当第一条的 else 臂(T1 处)本身是 cond-return(链)才触发,否则返回 0(单条走旧路径)。逐段:为每段**分叉帧**(save_map[64]),录制比较前缀 [cur,ci)、decode_compare(ci)、录制 then 臂 [ci+2,then_ret) 取 A_i,还原帧,存 (fop/aref/bref/at/bt/A_i)。末段录 [cur,ri) 取 final。要求 nseg≥2 且有 final。内向外折叠:`res=final; for i=n..0: res=emit_select(c_i,A_i,res)`。绑定到 call_result_slot,unwind(与单路径尾部相同)。门槛 `proto_is_chained_condreturn(p, allow_fields)` 走同样的链结构(allow_fields 选 op 集:method 可读字段,free-fn 不可),返回 nseg≥2。接线:rec_cond_branch 链式在单条**之前**;method 门槛加 `proto_is_chained_condreturn(callee_p,1)`;free-fn 门槛加 `proto_is_chained_condreturn(callee_p,0)`。`#define SPT_MAXCHAIN 8`。

### 正确性论证
- **折叠语义**:clamp `if(x<lo) return lo; if(x>hi) return hi; return x` → res=x; i=1: select(x>hi,hi,x); i=0: select(x<lo,lo,select(x>hi,hi,x)) = `x<lo?lo:(x>hi?hi:x)`,与逐条 if 同义。每个 select 位精确(int 免舍入,float 掩码混合)。
- **段独立 + 分叉帧**:各段只读参数/字段、算一个返回值,互不传递状态;每段录制前 save、后 restore,回到方法前缀后的帧——故段 i+1 不见段 i 的残留寄存器。
- **无侧出口风险**:折叠是纯算术 + select,无分支无写;末尾绑定到调用者 R[A] 再 unwind,与单条 cond-return 尾部一致。
- **混合类型干净 abort**:emit_select 要求 then/else 同型(全 int 或全 float),否则 abort——一条链里 int 臂 + float 臂 → 干净回退,off==on。
- **超长链 abort**:nseg 超 SPT_MAXCHAIN(8)→ abort。结构不符(JMP 目标错位、then 臂非 RET1)→ abort。

### 效果(位精确)
free-fn clamp(60M 次)**1.0×(此前 abort)→ 5.22×**;method clamp **→ 4.49×**。clamp_imm(立即数)、f_clamp(float)、triple(三段)、free/method 全 compiled + off==on;§10.48 single_m/single_f(单条 cond-return method + free-fn)零回归。

### 验证(完整门槛,全绿)
**308 ctest · 95 kernel**(新增 `clamp_chained.spt`:free/imm/triple/method clamp + single + float)· **306 全量差分** · 新增 `gen_chained_condreturn` 生成器(free_reg/method/imm/float/triple + mixed_abort 六形,注册后**45 个生成器**),280 例 0 失配(229 编译)· **全 GENS seeds×300 多轮 = 数千例 0 失配** · **ASan+UBSan 全干净**(5 链式微例 + guard-heavy 19-kernel 子集 + seed 555×60 模糊)· entries==exits、guard_fail=0。§10.46–§10.55 零回归。边界:mixed(int+float 链)compiled=0 + 位精确;toolong(9 条 >MAXCHAIN)compiled=0 + 正确;degen(lo>hi)编译且正确。

**关键教训**:
(1)**并行路径再奏效(第 10 次)**:链式 cond-return 作为单条之上的独立路径,单条原样不动——结构 PEEK(第一条 else 臂是否又是 cond-return)区分链与单。`if "T1 else 臂是 cond-return"` 这一条判据把链精确从单条里分出来,零回归风险。
(2)**抽公因子降重复**:decode_compare(比较解码)+ emit_select(select 机器)抽出后,链式折叠循环只是"逐段 decode + 录 then 臂 + 折叠"。早该抽 emit_select——四个 if-转换器各抄了一份 select;本节起新代码复用它(旧的暂不动以免触热路径)。
(3)**手动处理嵌套比较**:链的第二条起比较不能走 rec_inst(会触发单路径的 bind-and-unwind 提前退出方法),必须 decode_compare 手动解;then 臂仍走 rec_inst(纯值计算,安全)。识别"哪些 op 该手动、哪些该交给录制器"是关键。
(4)**分叉帧让段独立**:每段 save/restore 帧,回到方法前缀后状态——这让"算第 i 段返回值"与"算第 j 段返回值"互不干扰,即使两段写同一批临时寄存器。与 §10.54 if-else 分叉同一手法。
(5)剩通用多写(需入口布局守卫)、嵌套内联(需帧栈)、字符串函数(需 C-call 基建)——皆需新机器,各自专项。

## §10.57 字符串长度 #s → SLEN(短字符串,首个进 trace 的字符串操作;附"恒失败守卫=编译颠簸=挂起"教训,✅ 阶段达成)

此前**任何**字符串操作进循环都 abort:`#s`(OP_LEN op=53)、string.len/byte(op=21)全在循环里退回解释器——字符串完全不进 trace。本节让 `#s`(短字符串字节长度)编译为新 IR 操作 **SLEN**,关闭这一类别空缺的第一块。短字符串(字面量/驻留串,≤~40 字符,常见情形)的长度存于 `TString.shrlen`(≥0);长字符串 shrlen 为负、长度在 `u.lnglen`。SLEN 读 shrlen 一个字节并**守卫短串**,长串侧出。

### 实现
- **asm**:新增 `sptasm_movzx_rm8(dst,base,disp)`(movzx r32, byte[mem],无 REX.W 自动零扩到 64 位),仿 `mov_rm32`(0F B6 取代 8B)。
- **IR/codegen**:新增 `SPTIR_SLEN`(op1=字符串)。codegen 仿 SPTIR_LEN:gen_load 取 TString*(字符串值在 trace 里就是 TString 指针,§字符串 == 早已如此);`movzx RCX, byte[RAX+OFF_TSTRING_SHRLEN]`;`cmp RCX, 0x80; jcc NB 侧出`(短串 shrlen 0..40 < 0x80 通过;长串 shrlen 作无符号字节 ≥0x80 侧出);store RCX。加入 codegen op-safety 表。
- **recorder**:OP_LEN 处理器加字符串分支(在数组路径之前),emit SLEN + snap + GUARD。

### ★★★ 关键教训:恒失败的运行期守卫 = 编译颠簸 = 挂起 ★★★
**最初版本对任意字符串 emit SLEN(纯靠运行期短守卫),结果长字符串直接挂起(200 次外层迭代就挂)。** 根因:长串每次迭代都触发短守卫失败 → trace 侧出 → 被**丢弃并重录** → 重录又编译出同样恒失败的 trace → 无限重编译颠簸(DEBUG 里看到"compiled trace"刷屏)。现有黑名单只数**录制 abort** 次数(`hot->aborts`),而 SLEN trace 录制是**成功**的(运行期才失败),逃过黑名单 → 不受限。
**修复**:只在能**观测到字符串确为短**时才 emit SLEN,且仅在 root(内联方法体没有活的被调帧可读 R[B])。做法:`TValue *sv = s2v((rc->ci->func.p + 1) + b); if (!ttisstring(sv) || !strisshr(tsvalue(sv))) abort;`。长串(或不可观测、或方法体内)→ **abort 录制** → 走现有黑名单(数 abort)→ 几次后拉黑 → 解释器接管,有界且位精确。运行期短守卫保留作纵深防御:罕见的"短串运行期变长串"会侧出→重录→这次观测到长→abort→拉黑,仍有界。
**通用教训**:任何**系统性恒失败**的运行期守卫(不止短串守卫,也包括将来的越界守卫等)若不被某种机制拉黑,就会无限重编译颠簸(实质挂起)。当前 JIT 只对录制 abort 拉黑,不对运行期守卫失败拉黑。在引入"几乎总失败"的守卫前,必须保证它能落到 abort 路径(本节用 record-time 观测做到)。**更彻底的修法是给运行期守卫失败也加计数+拉黑(LuaJIT 式 side-exit blacklist),那是字符串 byte 访问的前置工作。**

### 效果(位精确)
`#s` 进循环 **1.0×(此前 abort)→ 3.52×**。`a+#s`、`if(j<#s)`、`#s*2-j`、空串 `#""` 全 compiled + off==on;长串、`#(arr[j])`(算出来的操作数,不可观测)干净退回解释器(compiled=0)且位精确,**无挂起**。

### 验证(完整门槛,全绿)
**309 ctest · 96 kernel**(新增 `string_len.spt`:短串作值/作界/算术/空串/长串退回/列表退回)· **307 全量差分** · `gen_string` 更新(注释纠正——`#s` 现在编译;新增 len_long/len_cond 形)240 例 0 失配 · **多种子 seeds×300 = 千余例 0 失配** · **ASan+UBSan 全干净**(6 个 #s 微例 + 19-kernel 子集 + seed 808 模糊;**所有长串/退回路径均终止,无挂起**)· entries==exits、guard_fail=0。§10.46–§10.56 零回归。

**其它教训**:
(1)**孤立新操作=零回归风险**:SLEN 是全新只读 IR 操作,不碰任何现有路径,故对前 10 个增量零回归——与并行路径同样的安全性来源,但更彻底(全新操作)。
(2)**short/long 字符串双表示**:Lua 5.5 短串内联(内容在 `&ts->contents` 处,长度=shrlen 字节)、长串走指针(内容在 contents,长度=lnglen)。本节只做短串(守卫短),长串侧出/退回——把表示分裂的复杂度挡在外面。
(3)**字符串 byte 访问(下一步)**:SBYTE 仿 GETI(读 content[i],+短守卫 +越界守卫),需 string.byte/len 的 dot-call 识别(仿 math.* 的 SELF/pending_cfn/CALL→FMATH;但需导出 lstrlib.c 的 static str_byte/str_len 函数指针来辨识)。SBYTE 的越界守卫对正确用法(`for j in [0,#s)`)不颠簸;短守卫同样用 record-time 观测挡长串。**字符 hash/扫描/解析才是字符串支持的真正价值所在。**
(4)剩通用多写、嵌套内联——皆需新机器(写 codegen / 帧栈),各自专项。

## §10.58 字符串字节访问 string.byte / string.len → SBYTE / SLEN(字符 hash/扫描进 trace,字符串支持的真正价值,✅ 阶段达成)

§10.57 让 `#s`(SLEN)进 trace,但那是字符串支持里价值最低的一块(长度常循环不变)。本节加 **string.byte(s, i)**(→新 IR 操作 **SBYTE**)与 **string.len(s)**(→复用 SLEN),让**字符迭代**(滚动 hash、字节求和、扫描、解析)进 trace——这才是字符串支持的真正价值。SBYTE 读短字符串内联内容的第 i 字节(短串内容在 `&ts->contents` = rawgetshrstr),守卫短 + 守卫越界。

### 实现(仿 math.* 的 dot-call 识别 + SBYTE codegen)
1. **lstrlib.c** 暴露非 static `spt_jit_str_op(lua_CFunction)→1(str_len)|2(str_byte)|0`,辨识 file-static 的 str_len/str_byte——完全仿 lmathlib 的 `spt_jit_unary_math`(file-static C 函数的辨识器必须住在其所在库文件)。
2. **SPTIR_SBYTE**(op1=字符串, op2=索引):codegen 仿 GETI:gen_load TString*(RAX);`movzx RCX, byte[RAX+OFF_TSTRING_SHRLEN]`;`cmp RCX,0x80; jcc NB 侧出`(守卫短,长串侧出);gen_load RDX=索引;`cmp RDX,RCX; jcc NB 侧出`(**无符号**比较,i≥len 同时抓住 i<0 与越界);`lea RAX=[RAX+OFF_TSTRING_CONTENTS]`(短串内容基址);`add RAX,RDX`(内容+i,避开 SIB);`movzx RAX, byte[RAX]`(读字节零扩);store。string.len 复用 SLEN。新增 `OFF_TSTRING_CONTENTS`,加入 codegen op-safety 表。
3. **recorder dot-call 识别**(仿 §10.42 math.*):SELF 处理器解析出函数 fv 后,`spt_jit_unary_math` 返 NULL 时再试 `spt_jit_str_op(fvalue(fv))`;命中则发同样的 **GUARD_CFUNC**(钉住函数身份——表/字段被改即侧出)并设 `pending_str_{slot,op}`。CALL 开头若 `pending_str_slot==frame_base+a`:限 root、c==2;`string.len(s)` 要 b==3、取 R[A+2]=s 发 SLEN;`string.byte(s,i)` 要 b==4、取 R[A+2]=s、R[A+3]=i 发 SBYTE(SPT dot-call 约定:接收者=string 模块在 R[A+1],s 在 R[A+2],i 在 R[A+3];str_byte 读 arg2=s arg3=i,**0-based**)。结果→R[A] 类型 INT。trace 起点重置 pending_str。

### 安全:沿用 §10.57 的"观测短串"挡颠簸,越界天然不颠簸
- **长串挡颠簸**:同 §10.57——长串每次都失短守卫→颠簸。故 CALL lowering 处**观测活字符串确为短**(`strisshr(tsvalue(s2v(base+a+2)))`),长串/不可观测→abort→现有黑名单(数 abort)→有界。
- **越界天然安全**:string.byte 越界返回**0 个值**→ `a + (无值)` 在解释器里报错(spt 无优雅 nil)。故 SBYTE 越界守卫侧出后,解释器跑同一个越界 string.byte 报**同样的错**——既不给错值也不颠簸(程序终止于同一错误)。**验证确认:越界用例 JIT 与解释器均 rc=255 一致退出。**故越界**无需** record-time 检查,运行期守卫足矣;正确用法(`for j in [0,#s)`)守卫恒过、零颠簸。

### 效果(位精确)
字符 hash(`h*31+string.byte`)**3.95×**、字节求和 **3.50×**(此前皆 abort)。hash/sum/scan/string.len/定长索引/算出索引全 compiled + off==on;长串干净退回(compiled=0)、越界一致报错,**均无挂起**。

### 验证(完整门槛,全绿)
**310 ctest · 97 kernel**(新增 `string_byte.spt`:hash/sum/带 #s 守卫扫描/string.len/长串退回/定长索引)· **308 全量差分** · 新增 `gen_string_byte` 生成器(hash/sum/scan/len/stride/long 六形,索引保持在界,注册后**47 个生成器**)260 例 0 失配(217 编译)· **多种子 seeds×300 = 千余例 0 失配** · **ASan+UBSan 全干净**(7 微例含**边界索引 len-1**无越界 + seed 909 模糊 + string_byte kernel + 19-kernel 子集;**所有长串/越界/退回路径均终止**)· entries==exits、guard_fail=0。§10.46–§10.57 零回归。

**关键教训**:
(1)**dot-call 识别完全仿 math.***:str_byte/str_len 是 file-static,辨识器 `spt_jit_str_op` 住 lstrlib.c(同 spt_jit_unary_math 住 lmathlib)。SELF 发 GUARD_CFUNC 钉函数身份、设 pending_str;CALL 按 dot-call 约定取真参(R[A+2]=s, R[A+3]=i)降级。**复用已验证的 C-函数识别框架=低风险**。
(2)**越界 vs 长串两类守卫失败,处理不同**:长串守卫**系统性恒失败**(长串是合法值,不报错)→ 必须 record-time 观测短串挡颠簸;越界守卫失败→解释器跑越界 string.byte **报错终止**(不优雅返回)→ 天然不颠簸,运行期守卫足矣。**区分"恒失败但合法"(需 record-time 拦)与"失败即报错"(运行期守卫足矣)是关键**。
(3)**SBYTE 仿 GETI + 一个 movzx_rm8**:字节读 = 守卫短 + 无符号越界守卫 + lea 内容基址 + add 索引(避 SIB)+ movzx 字节。短串内容内联在 `&ts->contents`(rawgetshrstr)是关键事实。
(4)**孤立只读新操作再次零回归**:SBYTE/SLEN 不碰任何现有路径,对前 11 增量零回归。
(5)更彻底的 thrash 防护(运行期守卫失败黑名单)仍是未做项,但本节与 §10.57 已用 record-time 观测把字符串这两类守卫的颠簸都挡住,有界且位精确。

## §10.59 math.min / math.max → 无分支 select(数值钳制 clamp 进 trace,复用 select 机器,✅ 阶段达成)

§10.42 让 math.* 一元函数(sqrt/sin/…)进 trace,但 **math.min/math.max**(及其组合出的钳制 `math.max(lo, math.min(hi, x))`)此前 abort。钳制是基础算术之后最常见的数值习语。本节让 math.min/max 编译为**无分支 select**,**复用 §10.30/§10.50/§10.56 的 emit_select 机器,零新 IR 操作、零 codegen 改动**(同 §10.43 for-each 之"纯 recorder 变换")。

### 语义推导(与 Lua 逐位一致)
Lua `math.max(a,b)`:current=a,若 b>a 则 b ⇒ `(b>a)?b:a = (a<b)?b:a`。`math.min(a,b) = (b<a)?b:a = (a>b)?b:a`。emit_select 算 `out = (a FOP b) ? then : else`,故:
- **max** = emit_select(**LT**, a, b, then=**b**, else=**a**)
- **min** = emit_select(**GT**, a, b, then=**b**, else=**a**)
emit_select 的"比较为假→else(=a)"恰好对应 Lua"保留当前 arg,除非被严格击败":**平局**(a==b,比较为假)→a、**NaN**(任何比较为假)→a,两者都与 Lua 一致。整数走 CMPSET/SUB/MUL/ADD,浮点走 FCMPMASK/FSELECT(GT/GE 换 LT/LE 保证 NaN 假)。-0.0/0.0 边界也一致(已验证)。

### 识别(仿 math.* 一元,SELF 设 pending + CALL 降级)
1. **lmathlib.c** 暴露非 static `spt_jit_math_minmax(lua_CFunction)→1(min)|2(max)|0`(同 spt_jit_unary_math)。
2. SELF 解析出 fv 后,unary_math/str_op 都不命中再试 minmax;命中发 GUARD_CFUNC + **压 pending_minmax 栈**。
3. CALL 若栈顶 slot 匹配:取 R[A+2]/R[A+3] 两参,混合 int/float 先 TOFLT 提升(Lua 混合提升为 float),调 emit_select,结果→R[A]。

### ★ 关键:嵌套钳制是 multiret(本节两个真问题)
**问题 1:单 pending 槽不够**。钳制 `math.max(lo, math.min(hi, x))` 在两个 CALL 之前先武装外 max 再武装内 min;单槽会被内 min 覆盖、消费后清空,外 max 的 CALL 就找不到 pending → abort。**修复:pending_minmax 改为栈**(深 8),CALL 按 LIFO 弹栈(良嵌套)。
**问题 2:multiret 调用约定**。内 min 的结果**直接喂**外 max 的实参 → Lua 发**内 CALL c==0**(multiret:1 个结果留栈上)、**外 CALL b==0**(实参延伸到栈顶)。原本 `c!=2`/`b!=4` 直接 abort。**修复**:min/max 恒产 1 个结果,故 (a) 允许 c==0;(b) min/max 以 c==0 降级后,记下结果槽 `minmax_multiret_top`(只对**紧接的下一个** CALL 有效,CALL 入口即捕获并清零);(c) 外层 min/max 见 b==0 时,要求 `minmax_multiret_top == R[A+3]`(即尾参恰好是那 1 个内层结果 ⇒ 恰 2 参),据此读 R[A+2]/R[A+3]。深层(3 级)钳制每级为下一级设 multiret_top,逐层校验。**3 级嵌套、min/max 两种外层序、混合类型全部编译**。

### 效果(位精确)
整数钳制 **3.96×**、浮点钳制 **3.08×**(此前皆 abort)。single min/max、int/float/mixed 钳制、3 级深嵌套、两种钳制序、负边界、与 math.sqrt 共用(sqrt 走 pending_cfn、max 走 pending_minmax,互不冲突)全部 compiled + off==on。

### 验证(完整门槛,全绿)
**311 ctest · 98 kernel**(新增 `minmax_clamp.spt`:int/float 钳制 + single min/max + 3 级深嵌套 + min 外层序 + mixed + 负边界)· **309 全量差分** · 新增 `gen_minmax` 生成器(imin/imax/iclamp/fclamp/mixed/deep/clamp2/negclamp 八形,**48 个生成器**)260 例 **260 编译(100%)** 0 失配 · **多种子 seeds×300 = 千余例 0 失配** · **ASan+UBSan 全干净**(7 微例含 3 级嵌套/混合 + seed 606 模糊 + 缩减 kernel)· entries==exits、guard_fail=0。§10.46–§10.58 零回归(multiret 跟踪在每个 CALL 入口捕获并清零,不泄漏到非 min/max 调用)。

**关键教训**:
(1)**复用 emit_select = 零新 codegen**:min/max 本质是 select,§10.30/§10.50/§10.56 已把 int/float select(含 NaN 安全)机器备好;min/max 只是又一个使用者。**最便宜的特性是已有机器的新用法**(同 §10.43)。
(2)**multiret 是嵌套调用的隐藏约定**:Lua 对"调用结果作另一调用的尾参"用 c==0/b==0。这是 codebase 首次处理 multiret(FMATH 此前要求 c==2,故 sqrt 作尾参不编译)。本节用"只对下一 CALL 有效的 multiret_top + 尾参槽校验"精确恢复 2 参,而不引入通用 multiret 跟踪。
(3)**pending 栈而非单槽**:嵌套同类调用(钳制)需要栈;单槽是 depth-1 限制的又一处体现。栈深 8 远超钳制的深度 2。
(4)语义对齐靠**仔细推导 + 验证**(平局→a、NaN→a、-0.0),不靠"差不多"。
(5)**math.floor/ceil/abs**(返回 int 或类型保持)是自然的下一步;min/max 这种"取已有 select 机器的新用法"模式可继续(floor/ceil 需 roundsd+cvttsd2si 的新 codegen,abs 需 andpd/sar-xor-sub)。

## §10.60 math.abs —— 按参数类型分派（int 无分支 select / float FMATH(fabs)，零新 IR，✅ 阶段达成）

§10.59 让 math.min/max 进 trace，本节补上 **math.abs**。abs 此前 abort，因为它**保持类型**（int→int、float→float），不像 §10.42 的一元 math.*（恒返回 float）——这正是分派的依据。本节按**参数类型**降级：**INT → 无分支 select**（复用 §10.30/§10.50/§10.56/§10.59 的 emit_select），**FLOAT → FMATH(fabs)**（复用 §10.42 的一元 libm 调用）。**零新 IR 操作、零新 codegen**，又一处"已有机器的新用法"（同 §10.43/§10.59）。

> 交接背景：上一轮 §10.60 因**两套实现在 `spt_jit.c` 撞车**（emit_select+FMATH(fabs) 一套 vs 另起 FABS/IABS 新 IR+andpd/sar 一套，`pending_abs_*` 字段/extern/CALL 降级互相重复）而无法编译，已回退到 §10.59 干净基线并留脚手架。本节按脚手架注释里的**推荐设计**重做，**只用一套** `pending_abs_*`、零新 IR，撞车的另一套（FABS/IABS）不再引入。

### 语义推导（与 Lua 逐位一致）
Lua `math.abs`：整数 `n<0` 时 `n = (lua_Integer)(0u - (lua_Unsigned)n)`（无符号回绕）、否则 `n`；浮点 `l_mathop(fabs)(x)`。
- **INT**：`abs(x) = (x<0) ? -x : x`。用 `emit_select(LT, x, 0, then=(0-x), else=x)`，整数 select = CMPSET/SUB/MUL/ADD（`out = x + ((0-x) - x)·(x<0)`）。`0-x` 由 `SPTIR_SUB` 整数算出，**INT64_MIN 自映射**（二补回绕 `0-INT64_MIN=INT64_MIN`）与 Lua `0u-n` **逐位一致**。
- **FLOAT**：发 `FMATH(fabs)`（baked libm 指针）。**不能用 select**——`-0.0 == +0.0` 比较相等，select 会保留 `-0.0` 位型，而 Lua `fabs` 返回 `+0.0`；fabs 亦正确处理 NaN（清符号位语义）。

### 识别（仿 §10.59 min/max，SELF 设 pending + CALL 降级）
1. **lmathlib.c** 暴露非 static `spt_jit_math_abs(lua_CFunction)→fabs|NULL`（命中 file-static `math_abs` 返回 libm `fabs` 指针，供 float 路 baked 进 FMATH）。
2. SELF 解析出 fv 后，`unary_math`/`str_op`/`minmax` 都不命中再试 `math_abs`；命中发 GUARD_CFUNC + 设**一套** `pending_abs_{slot,fabs}`（abs 是一元，单槽即可，无需栈）。
3. CALL 若 `pending_abs_slot` 匹配：参在 R[A+2]，按 `reg_type` 分派 INT/FLOAT，结果→R[A]。

### 嵌套用 multiret（复用 §10.59 的 minmax_multiret_top）
abs 与 min/max 自由组合，**双向**都靠共享的 `minmax_multiret_top`：
- **abs 套 min/max**（`math.abs(math.min(…))`）：内层 min/max 以 c==0 降级、记 `minmax_multiret_top`；abs 的 CALL 见 b==0 且 `incoming_mm_top == R[A+2]`（abs 单参在 A+2，对比 min/max 尾参在 A+3）即恢复"恰 1 参"。
- **abs 喂 min/max**（`math.min(40, math.abs(…))`）：abs 自身结果若作尾参（c==0），同样记 `minmax_multiret_top`，外层 min/max 据此校验。
单参 b==3（fn+receiver+1）；尾参为 multiret 时 b==0。c==2 一结果或 c==0 续喂外层。非数值参干净 abort。

### 效果（位精确）
`math_abs.spt`（int/float abs、abs 差值、双 abs、-0.0、abs 套 min、abs 套 clamp、abs 喂 min、abs(sqrt 差)）**8 条 trace 全编译**、off==on 逐位一致、entries==exits=1600007、guard_fail=0。此前该 kernel 为 no-jit（abs 在 OP_CALL 处 abort）。

### 验证（完整门槛，全绿）
**99 kernel 差分（no-jit=0，math_abs 由 no-jit 转为 compiled）· 全量 310 match / 0 mismatch**（二者本次实测）· ctest 计数 **313 不变**（未新增测试文件，math_abs.spt 早已在树内）· 聚焦 abs 模糊（8 形态 × 8 seed × HOT∈{4,20}，**160 例 0 失配**）· **ASan+UBSan 全干净**（int/float/-0.0/嵌套 multiret 微例，FMATH 的 C 调用 ABI 正确）· **-0.0 符号判别**（`1.0/math.abs(-0.0)` 须 +inf：JIT 与解释器同得正值 → fabs 返 +0.0，select 会得 -inf）· entries==exits、guard_fail=0。§10.42–§10.59 零回归。

**关键教训**：
(1) **按类型分派 = 两条已验证路各取所需**：int 取 §10.59 的 emit_select、float 取 §10.42 的 FMATH(fabs)，无需任何新 IR/codegen。撞车的那轮恰恰错在"另起 FABS/IABS 新机器"——**已有机器的新用法永远比新机器便宜**（§10.43/§10.59 同理）。
(2) **-0.0 必须 fabs 不能 select**：浮点 select 比较 `-0.0==+0.0` 为真会留住 -0.0 位型，而 abs 的契约是返 +0.0。这是"float 不是 int 的浮点版"的又一例——符号位语义不可省。
(3) **multiret 是共享设施**：§10.59 为嵌套钳制建的 `minmax_multiret_top` 直接被 abs 复用，abs↔min/max 双向嵌套零额外机器。一元（abs，参在 A+2）与二元（min/max，尾参在 A+3）只差尾参槽偏移。
(4) **单一实现 + 一套 pending**：撞车根因是两套 `pending_abs_*`/extern/CALL 降级并存。本节自始至终只一套——先想清设计再落键。
(5) **下一步 math.floor/ceil**（返回 int，需 `roundsd`+`cvttsd2si` 的**值域守卫**：|x|>2^63 转 int 溢出，须 roundsd→cvttsd2si→cvtsi2sd 往返 + ucomisd 守卫，越界侧出回解释器）——这是 abs 之后第一个需要**新 codegen** 的 math 扩展。

## §10.61 math.floor / math.ceil —— roundsd + 值域守卫（复用死桩 SPTIR_TOINT，守卫 resume-at-SELF，✅ 阶段达成）

§10.60 让 math.abs 进 trace；本节补上 **math.floor / math.ceil**。与 abs（保持类型）不同，floor/ceil **从 float 产出 int**（Lua：整数参原样返回；浮点参 `floor/ceil(x)` 后 `pushnumint` —— 整数值在 `lua_Integer` 范围 `[-2^63, 2^63)` 内则返 int、否则返 float）。这是 abs 之后**第一个需要新 codegen** 的 math 扩展（roundsd + 转换往返值域守卫），但仍是"孤立新 IR 操作 + 守卫"的并行路径（同 §10.57/§10.58 的 SLEN/SBYTE），回归风险可控。

### 复用死桩 SPTIR_TOINT（零新枚举项）
IR 枚举里 `SPTIR_TOINT`（注释"float→int，mode in aux"）早已存在但**从未发射**——只在 codegen 的不变量/簿记分类里和 TOFLT 并列，无 recorder 发它、无真正 codegen。本节**重用这个死桩**：aux = 舍入模式（1=floor、2=ceil），给它真正的 codegen，**不加枚举项**。

### codegen（roundsd + 往返值域守卫 + NaN parity）
新增汇编助手 `sptasm_roundsd`（SSE4.1 `66 0F 3A 0B /r ib`，仿 cmpsd 的 imm8 SSE 模式；imm8=0x09 floor/0x0A ceil，含 NO_EXC 抑制不精确异常）。`case SPTIR_TOINT`：
1. `gen_load_xmm XMM0 ← x`；`roundsd XMM0, XMM0, mode`（原地取整成整数值 double）。
2. **往返值域守卫**：`cvtsd2si RAX ← XMM0`；`cvtsi2sd XMM1 ← RAX`；`ucomisd XMM0, XMM1`。范围内往返**精确相等**（含边界 -2^63：`cvtsd2si` 精确、`cvtsi2sd` 还原）；越界时 `cvtsd2si` 饱和到 INT64_MIN，往返 `≠`；NaN 则无序。`jcc NE → exit`（不等→越界）+ `jcc P → exit`（无序→NaN；**必须**，因 ucomisd 无序时 ZF=1 会骗过 NE）。范围内结果即 `cvtsd2si` 的 RAX，`gen_store`。
（XMM0/XMM1 是暂存——持久 XMM 池仅 XMM4/XMM5；RAX 暂存。整数值 double 下 `cvtsd2si` 的舍入模式无关，故用现成 cvtsd2si 而非新加 cvttsd2si。）
逐一手验每个边界:范围内不可表示的大整数（如 2^63-2048）往返精确；`d=2^63`（越界，`<2^63` 严格）饱和→侧出；`d=-2^63`（**在**范围，`>=-2^63`）精确→用 int；`d=NaN`→parity 侧出。

### ★ 关键 bug:库调用中途守卫侧出 → resume-at-SELF（本节真问题）
floor/ceil 是**首个"库调用降级中途有运行期守卫会侧出"**的情形（abs 的 select/FMATH、min/max 的 select 都不侧出）。初版 TOINT 守卫用 `rec_snap`（在 CALL 处取快照）：越界侧出后解释器恢复到 CALL 重执行，**却报 `attempt to call a table value (method 'floor')`** —— 因为 floor 的 SELF 武装 pending 时**没把 floor 函数写进 reg_map[R[A]]**，CALL 处快照不含 R[A]=floor，解释器找不到函数。
**修复:守卫快照恢复到 SELF**（而非 CALL）。捕获 SELF 的 PC（`pending_floorceil_self_pc = rc->pc`），TOINT 守卫用 `sptir_snapshot(ir, self_pc)` 取快照——解释器侧出后从 SELF 重执行 **SELF+argload+CALL**，自然把 floor 解析进 R[A] 再调用，越界/NaN 时返 float，与 Lua `pushnumint` 一致。**安全性**:floor/ceil 无副作用，重执行幂等——这正是内联纯读方法体用的 **resume-at-SELF**（`method_self_pc`/`method_resume_snap`，§10.47）同一思想，只是那里门控在 `frame_base!=0`、这里在 root 直接用 `sptir_snapshot` 指定 SELF 的 PC。`sptir_snapshot` 捕获**当前 reg_map**(R[B]=math、参源槽、循环携带值全在)+ 存指定恢复 PC，恰好够用。

### 记录时值域观测（仿 SLEN/SBYTE 短串观测，止颠簸）
仅修恢复仍有性能坑:恒越界的循环每次迭代进 trace→立即侧出→resume→再进，比纯解释慢 ~2×。仿 §10.57/§10.58 的"只在记录时观测确为短串才发射"纪律,在 FLOAT 路 emit 前**读 live 参值**:`s2v((rc->ci->func.p+1)+(a+2))`，若已在 `[-2^63,2^63)` 外则 **abort**(PC 黑名单,有界)——恒越界循环改走解释器,不再编出恒侧出的无用 trace。门控在 `frame_base==0`(参 live 槽可寻址;inlined method 跳过观测、靠运行期守卫)。in-range 值仍编译,**运行期**才越界的稀有值由 resume-at-SELF 正确侧出。

### 识别 / 嵌套（仿 §10.59/§10.60）
`lmathlib.c` 暴露 `spt_jit_math_floorceil(lua_CFunction)→1|2|0`。SELF：libm/str/minmax/abs 都不命中再试 floorceil；命中发 GUARD_CFUNC + 设 `pending_floorceil_{slot,mode,self_pc}`。CALL：`pending_floorceil_slot` 匹配则参在 R[A+2]，**INT 参恒等透传**（整数是自身的 floor/ceil，依赖 reg_type 的上游守卫，无新 op）、**FLOAT 参发 TOINT**（带 resume-at-SELF 守卫）。复用 §10.59 的 `minmax_multiret_top`：floor/ceil 与 min/max **双向嵌套**（floor 喂 min、floor 套 min）；单参 b==3、尾参 multiret 时 b==0；c==2 一结果或 c==0 续喂。

### 验证（完整门槛，全绿）
**100 kernel 差分（no-jit=0）**（新增 `math_floorceil.spt`：float floor/ceil 正负 + 整数恒等 + floor 喂 min + floor 套嵌套 min + ceil 差值 + floor/ceil 合用，**7 条 trace 全编译**、off==on 逐位一致、entries==exits=1400007、guard_fail=0）· **全量 311 match / 0 mismatch / 0 timeout**（新 kernel 使 310→311，零回归）· 新增 `gen_floorceil` 生成器（ffloor/fceil/iident/floor_min/floor_nmin/ceil_diff/floor_ceil/**cross**[跨 2^63 运行期侧出]八形，**49 个生成器**）**聚焦 208 例**（64+144，跨 8 形 × 多 seed × HOT∈{4,8,16,20,32,40}）**0 失配** + 集成混合 50 例 0 失配 · **ASan+UBSan 全干净**(in-range/记录时 abort/运行期侧出/NaN parity/2^63 边界/kernel) · **运行期路径专项**:`fcrt`(in-range→越界,resume-at-SELF)、`fcnan`(in-range→单次 NaN,parity 守卫单次侧出再重入)、`fc6`(记录时越界→abort 走解释器)均 off==on · entries==exits、guard_fail=0。§10.42–§10.60 零回归。

**关键教训**:
(1) **库调用降级的运行期守卫必须 resume-at-SELF**:CALL 处恢复需要 R[A]=函数,而 SELF 武装 pending 不写 R[A];恢复到 SELF 让解释器重解析函数再调用。floor/ceil 是首个触发此问题的库调用(abs/min/max 不侧出)——**凡库调用降级里有会侧出的守卫,快照都该指向 SELF**。§10.47 的内联方法 resume-at-SELF 在此推广到 root 帧(直接 `sptir_snapshot(ir, self_pc)`)。
(2) **已知潜在同类问题:SLEN/SBYTE**。它们的守卫(长串侧出)用 `rec_snap`(CALL 处),有**同样的"R[A] 未恢复"潜在 bug**,只因 kernel 用一致短串 + 记录时短串观测使长串走 abort 而**从未被触发**。若日后 string 路被改动或出现"记录时短、运行期长"的串,应同样改为 resume-at-SELF。本节先聚焦 floor/ceil,此点记为**已知限制**。
(3) **死桩复用 = 零新枚举项**:`SPTIR_TOINT` 早在枚举里(注释已预告"mode in aux"),给它真 codegen 即可,无需新 IR 操作号。又一处"用已有的"(同 §10.43/§10.59/§10.60),但这次确实需要新 codegen(roundsd + 守卫序列)——"复用 IR 槽"与"新 codegen"是两件事。
(4) **往返值域守卫天然处理非对称边界**:`[-2^63, 2^63)` 上界严格、下界含等号。`cvtsd2si→cvtsi2sd` 往返对范围内整数值 double 恒精确(含 -2^63)、对越界恒饱和到 INT64_MIN(往返不等),自动复现 Lua `lua_numbertointeger` 的精确边界——无需载入 ±2^63 常量比较。NaN 靠 ucomisd 无序的 parity 位单独捕获。
(5) **记录时观测 + 运行期守卫双层**:记录时观测止住"恒越界"颠簸(走解释器),运行期守卫兜住"记录时 in-range、之后越界/NaN"的稀有真值。二者缺一不可:只有守卫会颠簸,只有观测会漏判运行期变化。
(6) **下一步**:运行期守卫失败黑名单(高风险,触及 trace 生命周期) / 嵌套内联多帧 / 通用多写。floor/ceil 已补全常用 math 取整;"取已有 select/IR 机器的新用法"模式(§10.59/§10.60)与"孤立守卫"模式(§10.57/§10.58/本节)是两条低风险扩展主线。

## §10.62 SLEN / SBYTE 的 resume-at-SELF（落地 §10.61 预告的潜在 bug）+ lcode.c luaK_self 段错误修复（✅ 阶段达成）

§10.61 关键教训(2) 记下一个**已知潜在同类问题**:string.len/byte 的长串侧出守卫用 `rec_snap`（在 CALL 处取快照），与 floor/ceil 初版有**同样的"R[A] 未恢复"潜在 bug**——只因 kernel 用一致短串、且记录时短串观测让长串走 abort 而从未触发。本节把它**显式触发并修复**,并修一个正交的前端 codegen 段错误。

### ★ 触发潜在 bug：记录时短、运行期长
构造 `list<str> ws = ["short_a","short_b","<67 字符长串>"]`,循环 `s = ws[r%3]; t += string.len(s);`。记录时 `r%3` 命中短串 → 编出带 SLEN 短串守卫的 trace;运行期轮到长串 → 短串守卫侧出。初版守卫用 `rec_snap`(CALL 处),恢复到 CALL 时 R[A]=string **模块表**(SELF 把 `len` 折叠进 SLEN、从不把函数写进 R[A]),解释器对模块表发起调用 → **`attempt to call a table value (method 'len')`**,exit 255。SBYTE 同理(长串 / 越界索引侧出)。`#s`(OP_LEN→SLEN,无 SELF)无此 bug。复现:`slbug.spt`(嵌套 SLEN,off=13120164)、`sbnest.spt`(嵌套 SBYTE,32320388)、`slbug2.spt`(单循环 SLEN)。

### 修复：守卫快照恢复到 SELF（与 §10.61 floor/ceil 同构）
SELF 武装 pending 时捕获其 PC(`pending_str_self_pc = rc->pc`);CALL 降级里 SLEN(~3616 行)与 SBYTE(~3631 行)守卫改用 `sptir_snapshot(ir, str_self_pc)`(而非 `rec_snap`)。侧出后解释器从 SELF 重执行 **SELF + 参加载 + CALL**,自然把 string.len/byte 解析进 R[A] 再调用——对长串返回真实长度/字节、对越界索引报与 Lua 一致的错误。**幂等**:长度/字节读无副作用,重执行安全。这与 §10.61、§10.47 是**同一 resume-at-SELF 思想**,在 root 帧用 `sptir_snapshot` 直接指定 SELF 的 PC(`sptir_snapshot` 捕获当前 reg_map——接收者槽、参源、循环携带值全在——并存指定恢复 PC)。

### ★ 关键教训：R[B] 物化是多余且有害的弯路
中途一度怀疑嵌套(内外两层循环)需**额外把 R[B](string 模块接收者)物化进恢复快照**——担心外层复用了接收者槽、live 栈值变陈旧、重执行的 SELF 读错接收者。为此加了结构体字段(`pending_str_bref/bslot`)、在快照前后写/还原 `reg_map[bslot]=bref`。**这是错的**:
- **多余**:`sptir_snapshot` 捕获的 slot_map 本就含模块表——SELF 不覆盖 R[A]/R[B](它武装 pending),故快照里接收者槽自然是 GETTABUP 的结果(模块表)。对 A==B 的 `string.len(s)`(`GETARG_B(SELF)==GETARG_A(CALL)==同槽`),物化是**纯 no-op**(reg_map 本就是模块表)。
- **有害**:新增字段 + 跨快照写 reg_map 引入了**非确定性嵌套崩溃**(疑为 reg_map 越界 / 未初始化路径)。移除物化、回到**纯 resume-at-SELF** 后,嵌套 SLEN/SBYTE 跨 `HOT∈{4,8,16,32,100} × 各 3 次重复`**全部 off==on**、ASan 干净。

**结论**:纯 resume-at-SELF **同时修复单循环与嵌套**。参加载窗口(SELF→CALL 之间的 MOVE)里那条 SLOAD 类型守卫,虽快照里 `slot_map[R[A]]=模块表`,但**对 `list<str>` 的串恒为 SPTT_STR 故永不触发**(`rec_value_type` 把短/长串都映射为 SPTT_STR);只有 SLEN/SBYTE 短串守卫(恢复到 SELF)在短→长时触发并正确恢复——这是纯 resume-at-SELF 已足够的根本原因。

### 正交修复：lcode.c luaK_self —— obj[key]() 段错误
前端新增的 `obj[key]()` 形式(`ast_codegen.c` 的 `NODE_INDEX_ACCESS`,如 `a[3]()` / `m[k]()`,把容器作 self 传入)经 `luaK_exp2val` 使 key 成**数值 / 计算寄存器(非 VKSTR)**。`luaK_self` 旧版 `lua_assert(key->k == VKSTR)` 后径直 `strisshr(key->u.strval)`——release(NDEBUG,assert 是 no-op)下 `key->u.strval` 是联合体垃圾,`strisshr` 解引用 → **`luaK_self` 段错误(lcode.c:1278)**。**修复**:去掉 assert,条件改 `if (key->k == VKSTR && strisshr(key->u.strval) && luaK_exp2K(fs, key))`——非 VKSTR 键短路落入 move+gettable 通用路径(处理任意键寄存器)。验证:buggy 二进制对 `m[k]()`(m[k] 非可调用)在 **ASan 下 `SEGV ... in luaK_self`**;fixed 二进制优雅 `attempt to call a number value`。此修复与 JIT 嵌套崩溃**正交**("len"/"byte" 是 VKSTR、字节码不变,嵌套 SLEN 即便用 buggy lcode 也工作)。

### 验证（完整门槛，全绿）
构建 0 err / 16 warn(基线)。**100 kernel 差分 no-jit=0**(`string_len.spt` / `string_byte.spt` 嵌套短串 kernel 分别仍编译 5 / 7 条 trace、off==on)· **全量 311 match / 0 mismatch / 0 timeout**(零回归)· 9 个复现脚本全 off==on(嵌套 / 单循环 SLEN/SBYTE + floor/ceil 各变体)· 嵌套 SLEN/SBYTE 跨 HOT × 重复稳健 · **ASan 全干净**(slbug / sbnest / slbug2 / fcrt / fcnan;objkey3 优雅报错) · obj[key]() 段错误→优雅错误经 buggy-vs-fixed ASan 对照确证。§10.42–§10.61 零回归。

**关键教训**:
(1) **resume-at-SELF 普适于库调用降级的侧出守卫**:floor/ceil(§10.61)与 SLEN/SBYTE(本节)同因——CALL 处恢复缺 R[A]=函数,恢复到 SELF 让解释器重解析再调用。§10.61 预告的"已知潜在 bug"本节落地;凡库调用降级里有会侧出的守卫,快照都该指向 SELF。
(2) **"嵌套需要额外处理"常是错觉**:`sptir_snapshot` 的 slot_map 已含所需接收者(模块表);臆测的 R[B] 物化既多余(no-op)又有害(引入非确定崩溃)。**先 dump 快照实际内容、再决定是否补**——本节耗时主要花在这条弯路上,而真相是"什么都不用加"。
(3) **正交 bug 分清边界**:lcode.c 段错误只在非 VKSTR 键(obj[key]())触发,与 VKSTR 的 string.len/byte 字节码无关;两者同会话修复但互不影响,验证时用独立复现各自确证(避免"一改全好"的误判)——故意临时还原 buggy lcode 重测嵌套,确认嵌套与 lcode 正交。
(4) **联合体类型标签先行**:`expdesc.u` 是联合体,任何读 `u.strval` 前必须先确认 `k == VKSTR`(本节的 `key->k == VKSTR &&` 短路)。release 下 lua_assert 失效,断言**不能替代**运行期检查。

## §10.63 运行期守卫失败黑名单（根治颠簸，触及 trace 生命周期，✅ 阶段达成）

§10.57 关键教训记下:**恒失败的运行期守卫 = 编译颠簸 = 挂起**。当时的缓解是"只在记录时观测到短串才 emit SLEN"——把问题推到 abort 路径(走现有录制 abort 黑名单)。但这是**特例补丁**:每个新守卫(SBYTE 越界、floor/ceil 值域)都要各自加 record-time 观测,且**系统性恒失败但合法**的情形(如 trace 录了 true 臂、运行期恒走 false 臂)根本无法在 record-time 拦截。本节给运行期守卫失败一个**通用**的拉黑机制,不再依赖每个守卫各自补丁。

### 问题：录制成功但运行期恒失败的 trace 是负收益
现有黑名单(`SPTHotEntry.aborts`)只数**录制 abort** 次数,达 `SPT_JIT_MAX_ABORTS=8` 拉黑。但一个 trace 可能**录制成功**(无 abort)、却在运行期**每次进入都从守卫侧出**——trace prologue + exit-stub 的 flush 开销 > 解释器直跑循环,是**负收益**。更糟的是侧出后 trace 被丢弃、热点计数再满又重录同样的 trace → 无限编译颠簸(§10.57 最初版本的挂起正是此机制)。

### 方案：loop_end_snap 区分"循环结束"与"守卫失败"
关键观察:**FORLOOP 的 "count > 0" 守卫**是循环正常结束的退出,不是负收益。必须把它从 side_exits 计数里**排除**,否则一个跑完的正常 trace 会被误判为颠簸。

1. **`SPTTrace.loop_end_snap`**:录制 FORLOOP 时记录其 "count > 0" 守卫的 snapshot index(§10.57 的 SLEN/SBYTE 守卫、§10.61 的 TOINT 守卫等都是**真守卫**,不计入此列)。side trace / 非 FORLOOP 收尾的 trace 为 -1。
2. **`SPTTrace.entry_count`**:每次 `sptjit_trace_enter` 进入 trampoline 循环时递增。用于把 O(nsnaps) 的扫描摊到每 256 次进入一次(`SPT_JIT_BLACKLIST_CHECK_INTERVAL`)。
3. **`SPTHotEntry.runtime_fails`**:trace 被运行期黑名单丢弃时递增。达 `SPT_JIT_MAX_RUNTIME_FAILS=3` 后把 `aborts` 顶到 `SPT_JIT_MAX_ABORTS`(完全拉黑);否则把 `counter` 设为 `aborts + runtime_fails`(**相位后移重试**——给热点一段冷却期再试,避免立即重录同样失败的 trace)。

### 检查逻辑（sptjit_trace_hot，每 256 次进入）
```
if (entry_count 是 256 的倍数) {
  side_exits = Σ exit_count[i]  for i != loop_end_snap
  if (side_exits > SPT_JIT_BLACKLIST_SIDE_EXITS=10000) {
    sptir_free(&t->ir); free(t); e->trace = NULL;   /* 丢弃 */
    runtime_fails++;
    if (runtime_fails >= 3)  aborts = MAX_ABORTS;    /* 完全拉黑 */
    else                     counter = aborts + runtime_fails;  /* 冷却重试 */
  }
}
```
阈值 10000 side_exits / 256 entries ≈ 每 40 次进入就有 1 次侧出——正常 trace(偶发侧出)远低于此,恒失败 trace(每次进入都侧出)很快触发。

### 测试（runtime_blacklist.spt）
```
list<int> arr = [0];
for (int i = 0, 60000) {
    if (i < 100) { arr[0] = arr[0] + 1; }   /* true 臂有 GETI/SETI,if-conversion 失败 */
    else { arr[0] = arr[0] + 2; }            /* 回退到 guarded branch */
}
print(arr[0]);
```
剖析阶段采样 64 次(i=8..71)全 < 100 → 录制 true 臂 + "i < 100" 守卫。运行期 i≥100 的每次迭代都侧出 → side_exits 累积 → 触发黑名单。验证:`[JIT] runtime blacklist: side_exits=10240 entry_count=10240 runtime_fails=1`,输出 119902 与解释器一致。

**if-conversion 是关键前提**:若 true/false 臂都只含 ifconv-safe 操作(简单算术),if-conversion 把 if-else 折成 branchless CMPSET,**无分支守卫** → trace 从 loop_end_snap 正常退出,不会触发黑名单。测试用例在两臂加入数组读写(GETI/SETI 非 ifconv-safe)强制回退 guarded branch,才构造出"录制 true、运行期走 false"的恒失败场景。

### 验证（完整门槛，全绿）
构建 0 err。**353 全量差分 match / 0 mismatch / 0 timeout**(含新增 `runtime_blacklist.spt`)· ctest 354/355 通过(1 个预存 b3_dofile_return 失败,与本节无关)· runtime_blacklist 触发黑名单后输出仍位精确。§10.42–§10.62 零回归。

**关键教训**:
(1) **必须区分"循环结束退出"与"守卫失败退出"**:FORLOOP 的 "count > 0" 守卫是正常退出的唯一入口,用 `loop_end_snap` 标记并从 side_exits 排除。不排除会把每个跑完的正常 trace 都误判为颠簸。
(2) **相位后移重试而非立即拉黑**:第一次丢弃后不直接拉黑,而是 `counter = aborts + runtime_fails` 让热点冷却一段再试。剖析阶段可能采样到不同分支(第二次录到 false 臂),从而**自愈**——这是正确行为,不是 bug。
(3) **通用机制优于特例补丁**:§10.57 的 record-time 短串观测、§10.61 的 record-time 值域观测都是"每个守卫各自补丁"。本节给所有运行期守卫一个统一的兜底,新守卫无需再各自加观测——即便 record-time 漏判,运行期黑名单也会在几次丢弃后拉黑。
(4) **阈值取舍**:CHECK_INTERVAL=256 摊销 O(nsnaps) 扫描(每次进入都扫会拖慢热路径);SIDE_EXITS=10000 足够高以避免偶发侧出的误判,又足够低以在 ~40 次进入/侧出比时触发(恒失败 trace 几千次进入即触发)。MAX_RUNTIME_FAILS=3 给自愈机会(剖析可能换分支)又不过度容忍颠簸。

## §10.64 通用多写方法内联（入口期字段布局守卫 + 体内无守卫字段访问，✅ 阶段达成）

§10.49 的"单尾写安全"只覆盖**体内最后一个产生守卫的 op 是 SETFIELD** 的简单情形:守卫失败发生在写提交之前,resume-at-SELF 重跑恰好写一次。但实际方法体常见**多写**(`this.a=..;this.b=..`)和**写后非前推读**(`this.a=x; return this.b`——写后那条对**别的**字段的 GETFIELD 是真守卫,存转载前推不适用)。这类"写后有真守卫"若直接内联,守卫失败时 resume-at-SELF 重跑会让**第一个 SETFIELD 双写**——破坏写幂等性。

### 根因：写后守卫失败 → 重跑 → 双写
```
SETFIELD this.a, x        /* 写 1 提交 */
GETFIELD this.b           /* 真守卫:键缺失/类型不符 → 侧出 */
SETFIELD this.c, y        /* 写 2 未执行 */
```
守卫在 GETFIELD 处失败 → resume-at-SELF 重跑整段 → SETFIELD this.a 再写一次。**写幂等性破坏**(对引用语义值尤甚,如累加、链表 push)。

### 方案：把字段存在性/类型守卫上提到入口 + 体内发无守卫字段访问
关键洞察:**零体内守卫 → 无内联帧退出 → 写安全(与写次数无关)**。把所有 `this.<field>` 的存在性 + 值类型校验**上提到 trace 入口**,体内 GETFIELD/SETFIELD 全部发**无守卫**变体——入口已校验过,运行期不可能失败。

#### 1. 入口期字段布局守卫（录制期收集 + 入口 C 校验）
- **录制期**:`SPTRecCtx.multiwrite_mode` 标志(仅方法内联路径设置,自由函数内联恒为 0)。GETFIELD/SETFIELD handler 在多写模式下,emit 指令但**不发 snap、不设 `SPTIRF_GUARD`**,同时把 `(TString* key, SPTType value_type)` 注册到 `rc.field_layouts[]`(按 key 去重,后写更新类型)。
- **复制到 trace**:`SPTTrace.field_layouts[16]` + `n_field_layouts`,上限 `SPT_JIT_MAX_FIELD_LAYOUTS=16`。
- **入口校验**(`trace_entry_guards_ok`):若 `n_field_layouts > 0`,取 `methods[0].recv_slot` 的 receiver,必须 `ttistable`;对每个 layout,`luaH_getstr` 查字段——**键必须存在**且**值类型必须匹配**录制期观测。任一不符 → 返回 0(走解释器,不进 trace)。

#### 2. 体内无守卫字段访问（GETFIELD/SETFIELD codegen 分支）
- **IR flag**:`SPTIRF_GUARD` 已有(§10.42 引入)。多写模式下录制期清掉此 flag。
- **GETFIELD codegen**:若 `SPTIRF_GUARD` 置位 → 走原路径(`gen_hash_find` 跳 exlbl + 类型守卫 jcc);若清掉 → `gen_hash_find(..., exlbl=-1)`,键缺失发 `ud2`(0F 0B)而非跳转——**入口已校验过,运行期不可能缺失**,ud2 是兜底断言(若触发说明入口校验有 bug)。
- **SETFIELD codegen**:同样按 flag 分支。无守卫变体省掉键存在性检查(直接 `gen_hash_find` + 写入),`ud2` 兜底。

#### 3. 方法门 `proto_is_multiwrite_method_inlinable`
保守准入,确保入口校验后体内确实零守卫:
- 非变参、无嵌套 proto、`sizecode` ∈ [2, 64]
- **至少 2 个 SETFIELD**(单写走 §10.49 路径)
- **无 RETURN1**(值返回不支持,只允许 RETURN0/RETURN/EXTRAARG)
- 除 SETFIELD 外,其余 op 必须是 `method_read_op_ok` 认可的"安全读"(GETFIELD/GETI/LOADK/MOVE/ARITH 等),不允许 CALL/CONCAT/FORLOOP 等可能产生守卫的 op

#### 4. 安全网（§10.42 既有）
`sptir_optimize` 后扫描所有 `SPTIRF_GUARD` 指令,exit PC 落在主 proto 之外则拒绝编译。多写模式下体内 GETFIELD/SETFIELD 无 GUARD flag → 不会被误判;入口期字段布局守卫不产生 IR 指令(纯 C 校验) → 不影响此安全网。

### 测试（multiwrite_method.spt）
5 个 kernel 覆盖典型多写场景:
- **Pair.update**:`this.a=x; this.b=y`(独立多写,最简)
- **Pair.swap**:`tmp=this.a; this.a=this.b; this.b=tmp`(读后写两字段,GETFIELD 在 SETFIELD 之前)
- **Pair.cross**:`this.a=this.b; this.b=this.a+1`(写后读**别的**字段——§10.52 写后非前推读问题)
- **FPair.update**:浮点多写(验证类型守卫匹配 float)
- **Triple.bump**:3 写(验证上限内多写)

JIT on/off 输出**完全一致**(整数累加 + 浮点累加 + 交叉更新)。

### 验证（完整门槛,全绿）
构建 0 err。**356/356 ctest 全通过** · **300/300 模糊测试全通过** · 124 kernel 差分 1 mismatch(`foreach_map_str.spt` —— map 哈希迭代顺序非确定性,**预存问题**,stash 多写改动后仍 mismatch,与本节无关)· §10.42–§10.63 零回归。

**关键教训**:
(1) **写幂等性是内联安全的充要条件**:不是"单尾写"而是"零体内守卫"。单尾写(§10.49)是零体内守卫的特例——最后一个产生守卫的 op 是 SETFIELD,等价于"写后无守卫"。多写只要把所有字段守卫上提到入口,体内零守卫,任意写次数都安全。
(2) **入口校验 + 体内无守卫是通用模式**:此模式可推广到任何"录制期可观测不变量"——如数组长度不变(录 `arr.len`,入口校验)、字段集不变(本节)、类型不变(已有类型守卫)。把不变量上提到入口,体内发无守卫变体,既安全又快(省掉每次访问的守卫开销)。
(3) **`ud2` 兜底优于静默错误**:无守卫变体在键缺失时发 `ud2`(非法指令异常)而非静默读随机内存。入口校验是"应该不会失败",ud2 是"若失败则立即可见崩溃"——比 silent corruption 易调试得多。生产环境若 ud2 触发,说明入口校验逻辑有 bug,而非"运行期偶发"。
(4) **方法门保守准入**:本节方法门排除 RETURN1(值返回)、CALL(嵌套调用)、CONCAT(字符串拼接)等可能产生守卫的 op。这些场景的扩展需要额外设计(如 RETURN1 的值类型守卫如何上提),不宜在本节一并解决。保守准入确保已支持场景的确定性安全。

## §10.65 值返回多写方法内联（放宽 §10.64 的 RETURN1 排除，值返回靠入口字段布局守卫，✅ 阶段达成）

§10.64 落地**通用多写 VOID 方法**(≥2 SETFIELD,入口期字段布局守卫 + 体内无守卫字段访问),但其方法门 `proto_is_multiwrite_method_inlinable` **保守排除了 OP_RETURN1**(值返回),`int sum(){...}` 这类"改多个字段并返一个值"的极常见形态(更新器、ID/版本生成、事务式更新、改后返新状态)全 abort。本节放宽该排除,让值返回多写方法内联。**安全性与 §10.64 完全相同——零体内守卫体**;返回值的类型守卫**已被既有守卫覆盖**(字段返回靠入口字段布局守卫,参数/计算返回靠 caller 层输入守卫),故**零新 codegen、零新入口守卫**,复用 §10.64 的字段布局机器 + 早已存在的 OP_RETURN1 内联返回 handler。

### 缺口确认(§10.64 lesson 4 预告)
§10.64 lesson(4) 明记:"方法门排除 RETURN1(值返回)…这些场景的扩展需要额外设计(如 RETURN1 的值类型守卫如何上提)"。画像:`int update(int x,int y){ this.a=x; this.b=y; return this.a+this.b; }`(2 写 + 返两字段之和)在 OP_CALL 方法门处 abort——`proto_is_multiwrite_method_inlinable` 第一循环 `if (o == OP_RETURN1) return 0;`,其余 6 个方法门也都拒(纯读有写、单尾写≠2 写、cond-* 无 if 形)。**关键观察**:OP_RETURN1 的内联返回接线**早已存在**(§10.46 纯读 / §10.48 cond-return / §10.52 写后返字段都在用),只是多写门把带 RETURN1 的方法挡在门外。

### 实现(只动门;值返回机器复用,零新 codegen)
改动集中在 `proto_is_multiwrite_method_inlinable` 一处 + 一个回归 kernel + 一个模糊生成器。**recorder/codegen 主体一字未改**。
1. **方法门放宽**:第一循环把 `if (o == OP_RETURN1) return 0;` 改为置 `has_value_return = 1`;第二循环(体内 op 白名单)放行 OP_RETURN1。这样 ≥2 SETFIELD + RETURN1 的方法不再被多写门拒。
2. **值返回机器复用**:OP_CALL 方法分派(`pending_method_slot` 路径)早已允许 `c == 2`(一个结果),且对所有方法内联都设 `rc->call_result_slot = rc->frame_base + a`、按门设 `rc->multiwrite_mode = proto_is_multiwrite_method_inlinable(callee_p)`。OP_RETURN1 的内联分支(`frame_base != 0`)`rec_load_reg` 取返回寄存器的 IR ref(已在 reg_map 里——由体内的无守卫 GETFIELD/MOVE/ARITH 算出)、恢复 caller 上下文、`ir->reg_map[rc->call_result_slot] = aref` 把 ref 绑到 caller 结果槽。**这套接线对多写方法零改动即正确**——它不关心 `multiwrite_mode`。
3. **保证体内严格零守卫(值返回路径)**:多写模式只把 **GETFIELD/SETFIELD** 改无守卫(§10.64 的 2899/3092 两分支),**GETI/GETTABLE/LEN 仍发守卫**(它们不在 §10.64 的无守卫化范围内)。对**值返回**方法,体内"写后再 GETI"的守卫若失败会 resume-at-SELF 重跑 → 双写非幂等字段。故门在 `has_value_return` 时额外禁止 GETI/GETTABLE/LEN——保证值返回多写方法体**严格零守卫**(§10.64 安全论证的根基)。**VOID 路径门保持 §10.64 原样**(`has_value_return==0`,该禁令不触发)→ **零回归**。

### 正确性论证(与 §10.64 同一安全类 + 值返回类型已被既有守卫覆盖)
- **写安全(零体内守卫 → 无内联帧退出)**:值返回路径体内 GETFIELD/SETFIELD 无守卫(多写模式)、ARITH/MOVE/LOADK 本就无守卫、GETI/GETTABLE/LEN 经门禁止。**全体零守卫 ⟹ 无 in-callee 退出桩 ⟹ 任意写次数都不会 resume-at-SELF 双写**。这与 §10.64 VOID 的安全论证逐字相同,只是体内多了"算返回值"的无守卫 op + 一条 RETURN1。
- **值返回类型已被既有守卫覆盖(本节关键)**:§10.64 lesson(4) 担心的"值类型守卫如何上提",**其实早已被字段布局守卫解决**:
  - **返字段** `return this.b`:体内发**无守卫 GETFIELD**(多写分支),同时把 `(b, 类型)` 注册进 `field_layouts[]`。入口 `trace_entry_guards_ok` 校验 this.b **存在 + 类型匹配**录制观测。故运行期 this.b 必为录制类型 → GETFIELD 产该类型值 → 结果槽类型 `rt` 正确,caller 据 `rt` 用结果类型安全。**入口字段布局守卫 = 字段返回的值类型守卫**,无需新上提。
  - **返参数** `return x`:x 是 caller 在 CALL 前装入 R[A+2] 的实参,其 IR ref 的类型由 **caller 层(frame_base==0)的实参守卫**钉死。结果槽 `rt` = x 类型,正确。
  - **返计算值** `return x + this.a`:输入 x(caller 守卫)+ this.a(入口字段布局守卫)类型都已钉 → 计算结果类型确定。正确。
  - **返常量** `return 0`:LOADI 常量,类型自明,无守卫。
  故所有返回值形态的类型都已被"入口字段布局守卫 ∪ caller 层实参守卫"覆盖,**体内无需任何返回值守卫**,§10.64 的零体内守卫不变。
- **写后返同字段位精确**:`this.a=x; …; return this.a`——`return this.a` 的 GETFIELD 命中多写分支(在 §10.52 前推分支**之前**检查),发无守卫 re-read。录制时 VM 已执行 SETFIELD(节点已是 x),故类型预测读到 x 的类型(一致);运行期 SETFIELD 的 store 在 IR 顺序上先于该 GETFIELD 的 load 且同节点(gen_hash_find 同键),故 load 读回已写值。位精确。
- **返回值之和 = 双写 + 错值双重探测器**:回归 kernel 与模糊器都累加 RETURNED 值;任一 resume-at-SELF 双写或返错值都会使和与解释器发散,被逐字节差分立刻抓住。
- **GETI 值返回正确 abort 且保持正确**:`int load(list a,int j){ this.a=this.a+1; this.b=a[j]; return this.a; }`(写后 GETI)经门禁止 → compiled=0 → 解释器,off==on 验证。
- **单写 + 返回仍走 §10.52**:多写门要 `n_writes >= 2`,故 `int inc(){ this.v=this.v+1; return this.v; }`(1 写 + 返同字段)仍由 §10.52 存转载前推处理,本节不接管。边界清晰、互不干扰。

### 效果(位精确)
值返回多写方法 `b.update(x,y)`(2 写 + 返两字段之和,2.8M 次)由 abort 变为编译进 trace;同 §10.64 的 VOID 多写,提速来自免去每次调用的解释器重入 + 入口一次性字段校验(对比解释器每次方法调用都要建帧/解析)。回归 kernel `multiwrite_ret_method.spt` 6 形全编译(update/next/set2/store/reset/float upd)、负向 GETI 形正确 abort、off==on 位精确、entries==exits(=6)、guard_fail=0。

### 验证(完整门槛,全绿)
构建 0 err。**362/362 ctest 全通过**(新增 `multiwrite_ret_method.spt`,Test #300)· **kernel 差分 pass=128 fail=1**(唯一 fail = `foreach_map_str.spt`,map 哈希迭代顺序非确定性、**预存且与 JIT 无关**:compiled=0 即 JIT 完全没参与,OFF/ON 差异纯来自字符串哈希种子,HOT=40 全量差分里它又匹配——种子 flakiness 实锤)· **全量差分 match=359 mismatch=0 timeout=0**(HOT=40)· **模糊器 seeds 1-10 × 250 = 2500 例 0 失配**(新增生成器 `gen_multiwrite_ret_method`:update2/rmw2_ret/ret_b/ret_param/ret_const/triple_ret/float_update/geti_ret 八形,含负向 geti_ret;并修正 `gen_writeret_method` 的 `multiwrite_fallback` 过时注释——它本是"2 写+返回"的"MUST abort"例,自 §10.65 起改为内联)· **ASan+UBSan 全干净**(新 kernel JIT-on + 现有 multiwrite_method/writeret_method + 值返回微例 + seed 777×60 模糊,rc=0、0 issue)· entries==exits、guard_fail=0。§10.42–§10.64 零回归(multiwrite_method=5/writeret_method=7/method_call=5/multi_method=5/condwrite_method=15/condreturn_method=6/write_method=6 编译数与基线一致)。

**关键教训**:
(1) **值返回的值类型守卫"早已上提"**:§10.64 lesson(4) 把 RETURN1 当成"需额外设计值类型守卫上提"的待办,但本节发现——**入口字段布局守卫(字段返回)+ caller 层实参守卫(参数/计算返回)已经覆盖了所有返回值形态的类型**。体内本就零守卫,加一条 RETURN1 不引入任何新守卫。所谓"上提"在 §10.64 已隐式完成,本节只是放开门并复用早已存在的 OP_RETURN1 内联返回 handler。又一例"已验证机器 × 新场景"(§10.47→…→§10.64→§10.65 一脉)。
(2) **零回归靠"只在值返回时收紧体内白名单"**:多写模式只无守卫化 GETFIELD/SETFIELD,GETI/GETTABLE/LEN 仍带守卫。为保证值返回路径"严格零守卫",门在 `has_value_return` 时额外禁 GETI/GETTABLE/LEN;VOID 路径门一字未改 → 对 §10.64 零回归。把新约束**条件化到新形态**,是给共享门加形态而不动既有行为的范式(同 §10.54 if-else 与 if-only 的并列)。
(3) **写后返同字段命中多写分支即对**:多写分支在 §10.52 前推分支**之前**检查,故值返回多写方法里的写后读直接走无守卫 re-read(录制时 VM 已写、运行期 store 先于 load 同节点),不依赖前推、位精确。前推(§10.52)仍只服务**单写**+返同字段(`n_writes==1`)。
(4) **保守门的代价是"漏优化但不错"**:门在值返回时禁 GETI/GETTABLE/LEN,会让"GETI 在写**之前**"这种其实安全的形态(如 `int load(list a,int i){ this.x=a[i]; this.hits=this.hits+1; return this.x; }`,GETI 在所有写之前 → 无写后守卫)也被拒。精确做法是"只禁出现在 SETFIELD **之后**的 GETI/GETTABLE/LEN"(需按写位置扫描),可作后续小扩展;当前保守门下这些形态干净 abort 回解释器(正确、仅不加速),符合"慢但对>快但错"。
(5) **HANDOFF.md 曾严重滞后,本节一并校正**:接手时 HANDOFF.md 停在 §10.62,把 §10.63(运行期守卫黑名单)、§10.64(通用多写)列为"待办",而 ROADMAP/DEV_NOTES 已到 §10.64。本节把 HANDOFF.md 全面更新到 §10.65,与 ROADMAP/DEV_NOTES 对齐——交接文档的真实基线必须随代码同步,否则误导接手者。

## §10.66 FMATH2:二元 libm C 调用(float % + math.pow,镜像 §10.42 FMATH,✅ 阶段达成)

§10.42 的 FMATH 是 JIT **首个 C 调用机制**,但只覆盖**一元** libm(math.sqrt/sin/...)。本节加 **SPTIR_FMATH2**——二元 libm C 调用,拿下两个此前 abort 的场景:
1. **float %**:`(i * 0.1) % 3.0` 此前在 `rec_arith` 里因"float MOD/IDIV need libm fmod + sign correction"而 abort。本节通过 `spt_jit_luamodf` wrapper(直接复用解释器自己的 `luai_nummod`)emit FMATH2,**位精确**(包括 -0.0/NaN/符号修正)。
2. **math.pow(x,y)**:此前 OP_SELF 不识别 math.pow。本节加 `spt_jit_binary_math` 映射 math_pow→pow,OP_SELF 设 `pending_cfn2_{libm,slot}`,OP_CALL emit FMATH2。

### 实现(4 部件,镜像 §10.42 + 1 wrapper)
1. **SPTIR_FMATH2**(op1=float arg1, op2=float arg2, aux=double(*)(double,double) libm 指针, 结果 float):codegen 与 FMATH 完全对称,只多一条 `gen_load_xmm XMM1=arg2`(x64 SysV + Windows fastcall ABI:浮点前两参在 XMM0/XMM1)。不在 `ra_op_is_safe` → 自动禁 RA,与 FMATH 一致。
2. **spt_jit_luamodf wrapper**(lvm.c):`luai_nummod(NULL, x, y, r)` 的 L 参数是 `(void)L`(unused),故 NULL 安全。这是**解释器自己的 modulo 代码**,所以结果与解释器逐位相同——无需手写 branchless lowering、无需担心 fmod vs remainder 差异、无需复现 Lua 的符号修正(`m=fmod(a,b); if ((m>0)?(b<0):(m<0&&b>0)) m+=b;`)。wrapper 是 leaf 函数(无 lua_State 接触),C call ABI 安全。
3. **spt_jit_binary_math**(lmathlib.c,`#if defined(LUA_COMPAT_MATHLIB)` 内):映射 `math_pow → pow`。math_pow 是 file-static 且住在 LUA_COMPAT_MATHLIB 块内,故此函数也必须在同一 `#if` 内;spt_jit.c 的 extern 声明提供 `#else` NULL fallback,使 recorder 在 LUA_COMPAT_MATHLIB 未定义时也能编译(只是 math.pow 路径变成 dead code)。
4. **recorder 两处**:
   - **rec_arith float MOD**:原 `if ((MOD||IDIV) && !int-int) abort` 改为 `if (MOD && !int-int) { promote both to float; emit FMATH2(luamodf) }`。IDIV float 仍 abort(需 floor+符号修正,未做)。
   - **OP_SELF/OP_CALL**:SELF handler 新增 `libm2 = spt_jit_binary_math(f)` 检查(与既有 libm/strop/mmop/absfn/fcop 互斥链),命中则设 `pending_cfn2_{libm,slot}`;CALL handler 新增 FMATH2 分支(b==4 即 2 真参 + 接收者 + 函数,c==2 即 1 结果,取 R[A+2]/R[A+3],int 参插 TOFLT,emit FMATH2)。

### 关键设计决策
- **luamodf wrapper 而非直接 fmod**:Lua 的 `%` 不是 C 的 `%` 也不是裸 fmod——`luai_nummod` 在 fmod 之上加了符号修正使结果与除数同号。直接 emit `fmod` 会产生符号差异(如 `(-0.1)%2.5` 在 Lua 是 `2.4`,在 C fmod 是 `-0.1`)。wrapper 复用解释器代码 = **零差异保证**,且 wrapper 是 leaf(不触 GC、不触 lua_State),C call ABI 干净。
- **math.fmod 延迟**:math.fmod 有整数快速路径(`lua_isinteger` → `lua_pushinteger`),若 JIT 把 int 参 promote to float 再 fmod 会返回 float 而非 int → 语义错。故 `spt_jit_binary_math` 只映射 math_pow(恒 float),math_fmod 留待后续(需在 CALL handler 检查两参类型分派 int/float 路径)。
- **LUA_COMPAT_MATHLIB 现状**:ltests.h:14 定义了它,但 ltests.h 从未被任何 .c include → 当前构建中 LUA_COMPAT_MATHLIB **未定义** → math.pow 不存在。float % 路径(经 luamodf)已由差分测试验证;math.pow 路径(SELF/CALL→FMATH2)共享同一 codegen,由代码审查验证。

### 验证(完整门槛,全绿)
构建 0 err。**363/363 ctest 全通过**(新增 `fmath2_mod_pow.spt`,Test #363)· **kernel 差分 pass=129 fail=1**(唯一 fail = `foreach_map_str.spt` 预存 mismatch,map 哈希迭代顺序非确定性,与 FMATH2 无关)· **模糊器 200 例 0 失配**(seed=42)· **float if-conv 模糊器 200 例 0 失配**(seed=42)· `fmath2_mod_pow.spt` 4 条 trace 全编译(float % constant/varying/negative + int % sanity),off==on 位精确,entries==exits、guard_fail=0。§10.42–§10.65 零回归。

**关键教训**:
(1) **复用解释器代码 = 位精确的最短路径**:float % 的符号修正、NaN/-0.0 边界、denormal 处理都在 `luai_nummod` 里。手写 branchless lowering 不仅工作量大,还容易在边界 case 出错。用一个 leaf wrapper 直接调解释器自己的宏,**零差异保证 + 零新边界分析**。这是"已验证机器 × 新场景"的又一例(§10.42 FMATH 的 libm 调用机器 × luamodf wrapper)。
(2) **二元 libm 的 codegen 增量极小**:FMATH2 vs FMATH 只多一条 `gen_load_xmm XMM1=arg2`——x64 ABI 把浮点前两参放 XMM0/XMM1,其余(帧对齐、RSP 调整、RAX=fn、call、存 XMM0)完全相同。RA-disabled 也相同(FMATH/FMATH2 都不在 `ra_op_is_safe`)。这使二元扩展的风险极低。
(3) **LUA_COMPAT_MATHLIB 的条件编译陷阱**:math_pow 是 file-static 且在 `#if LUA_COMPAT_MATHLIB` 内,故 `spt_jit_binary_math` 也必须在同一 `#if` 内(否则引用不到 math_pow)。但 spt_jit.c 的 extern 声明在块外,需要 `#else` NULL fallback 保持链接。这是 C 条件编译跨翻译单元的典型坑。

## §10.67 math.log + math.atan JIT 内联（复用 FMATH/FMATH2，零新 IR/codegen，✅ 阶段达成）

### 背景
§10.42 FMATH 覆盖 sqrt/sin/cos/tan/exp/asin/acos（一元 libm），§10.66 FMATH2 覆盖 pow + float %（二元 libm）。math.log 和 math.atan 是剩余的两个常用 math 库函数，分别可以复用 FMATH 和 FMATH2 机制——**零新 IR 操作、零新 codegen**。

### 实现
**(1) math.log(x) → FMATH(log)**:
- `spt_jit_unary_math` 加 `if (f == math_log) return log;`
- math.log(x) 单参数：SELF 设 pending_cfn，CALL 时 b==3（self+x）→ emit FMATH(log, x)
- math.log(x, base) 双参数：b==4 → FMATH 录制路径 `b != 3` → abort（正确行为，base 分支太复杂）
- math_log 是无条件定义的（不在 LUA_COMPAT_MATHLIB 块内），识别器无需条件编译保护

**(2) math.atan(y) → FMATH2(atan2, y, kflt(1.0))**:
- `spt_jit_binary_math` 加 `if (f == math_atan) return atan2;`
- math.atan 总是调用 atan2（二元 libm），故总是走 FMATH2
- math.atan(y) 单参数：b==3 → arg2 = `sptir_kflt(ir, 1.0)`（x 默认 1.0，等价于 atan(y)）
- math.atan(y, x) 双参数：b==4 → 正常 FMATH2 路径（arg2 = R[A+3]）
- FMATH2 录制路径从 `if (b != 4) abort` 改为三分支：b==4 正常、b==3 单参数 kflt(1.0)、其他 abort

**(3) spt_jit_binary_math 移出 LUA_COMPAT_MATHLIB 块**:
- math_atan 是无条件定义的（不在 LUA_COMPAT_MATHLIB 块内），但 math_pow 在块内
- 重构：函数移出 `#if`，内部用 `#if defined(LUA_COMPAT_MATHLIB)` 保护 math_pow 识别，math_atan 识别无条件
- spt_jit.c 的 extern 声明从 `#if/#else static NULL/#endif` 改为无条件 `extern`（函数现在总是定义）

### 验证（完整门槛，全绿）
构建 0 err。**366/366 ctest 全通过**（新增 `math_log_atan.spt`）· **kernel 差分 pass=133 fail=1**（唯一 fail = `foreach_map_str.spt` 预存 map 哈希顺序 flakiness）· **模糊器 300 例 0 失配**· **float if-conv 模糊器 150 例 0 失配**· `math_log_atan.spt` 8 条 trace 全编译（log 单参数/常量、atan 单参数/双参数、混合、int 参数提升），off==on 位精确，recorded=8 compiled=8 aborted=0 guard_fail=0。§10.42–§10.66 零回归。

**关键教训**:
(1) **单参数二元函数的 kflt 技巧**:math.atan(y) 的 x 默认 1.0，用 `sptir_kflt(ir, 1.0)` 作为 FMATH2 的 arg2，无需特殊 IR 或 codegen——常量 1.0 在 codegen 时被 gen_load_xmm 直接加载到 XMM1。这是"已有机器 × 新参数模式"的又一例。
(2) **条件编译重构的增量安全**:把 spt_jit_binary_math 移出 LUA_COMPAT_MATHLIB 块时，用 `#if` 保护 math_pow 识别（它仍在块内）、math_atan 识别无条件（它不在块内）。这比"整个函数在块内 + #else NULL fallback"更干净——函数总是定义，extern 声明总是有效，链接更简单。

## §10.68a 嵌套内联多帧 阶段 1：栈化基础设施（SPTInlineFrame 数组 + inline_depth，纯重构零行为变化，✅ 阶段达成）

### 背景
现有方法内联（§10.46–§10.65）和自由函数内联（§10.12）只支持**深度 1**：一次内联 push 一个帧，RETURN pop 回主帧。嵌套内联（A→B→C = 深度 2）需要 push 多个帧。但 `SPTRecCtx` 的 caller 状态（`save_p/save_k/save_cl/save_pc/save_frame_base/call_result_slot`）和 callee 元数据（`method_self_pc/method_resume_snap/multiwrite_mode`）都是**单值字段**——第二次 push 会覆盖第一次的 caller 状态，pop 时恢复到错误的帧。

### 实现（纯重构，零行为变化）
**(1) SPTInlineFrame 结构体**（`spt_jit_internal.h`）：
```c
typedef struct SPTInlineFrame {
  Proto *p;                        /* caller proto */
  const TValue *k;                 /* caller constant table */
  LClosure *cl;                    /* caller closure */
  const Instruction *pc;           /* caller PC just after the CALL */
  int frame_base;                  /* caller frame_base */
  int call_result_slot;            /* absolute reg_map slot for return value */
  const Instruction *method_self_pc;  /* SELF PC for resume-at-SELF (callee) */
  int method_resume_snap;          /* shared snapshot for in-method guards */
  int multiwrite_mode;             /* multi-write mode for callee body */
} SPTInlineFrame;
```
`SPT_JIT_MAX_INLINE_DEPTH=8`（`spt_jit.h`）：每层 push 一个帧，8 足够（真实 OOP 链罕见超过 3-4 层）。

**(2) SPTRecCtx 字段替换**（`spt_jit.c`）：
- 删除单值字段：`save_p/save_k/save_cl/save_pc/save_frame_base/call_result_slot/method_self_pc/method_resume_snap/multiwrite_mode`
- 新增数组：`SPTInlineFrame inline_frames[SPT_JIT_MAX_INLINE_DEPTH]; int inline_depth;`
- 新增临时字段：`const Instruction *pending_method_self_pc;`（SELF 设置 → CALL 消费，避免 push 前被覆盖）

**(3) rec_inline_pop() 辅助函数**：
```c
static void rec_inline_pop(SPTRecCtx *rc) {
  SPTInlineFrame *f = &rc->inline_frames[rc->inline_depth - 1];
  rc->p = f->p; rc->k = f->k; rc->cl = f->cl;
  rc->frame_base = f->frame_base; rc->pc = f->pc;
  rc->inline_depth--;
}
```

**(4) 所有 push/pop 点修改**：
- OP_CALL 方法内联（push）：`inline_frames[depth]` 填入 caller 状态 + callee 元数据，`inline_depth++`
- OP_CALL 自由函数内联（push）：同上，但 `method_self_pc=NULL, method_resume_snap=-1, multiwrite_mode=0`
- OP_RETURN0/RETURN1/RETURN + 两处 condreturn helper（pop）：用 `rec_inline_pop(rc)`，RETURN1 额外从 `inline_frames[depth-1].call_result_slot` 绑定返回值

**(5) rec_snap / rec_guard_pc 修改**：
```c
static int rec_snap(SPTRecCtx *rc) {
  if (rc->inline_depth > 0) {
    SPTInlineFrame *f = &rc->inline_frames[rc->inline_depth - 1];
    if (f->method_resume_snap >= 0) return f->method_resume_snap;
  }
  if (rc->call_arg_self_pc != NULL) return sptir_snapshot(rc->ir, rc->call_arg_self_pc);
  return sptir_snapshot(rc->ir, rc->pc);
}
```
内联帧里的守卫快照用 callee 帧的 `method_resume_snap`（caller 的 SELF PC）。

**(6) GETFIELD/SETFIELD multiwrite_mode 检查**：
```c
if (rc->frame_base != 0 && rc->inline_depth > 0 &&
    rc->inline_frames[rc->inline_depth - 1].multiwrite_mode) { ... }
```

**(7) OP_SELF**：`rc->method_self_pc = rc->pc` → `rc->pending_method_self_pc = rc->pc`（临时字段，CALL 消费）

### 正确性（纯重构，零行为变化）
阶段 1 是**纯重构**：所有 push/pop 点的行为与单值字段时完全一致。`inline_depth` 在主帧时为 0，第一次 push 后为 1——与原来 `save_*` 单值字段等价。全测试通过确认零回归。

### 验证（完整门槛，全绿）
构建 0 err。**367/367 ctest 全通过** · **134/134 kernel 差分全通过** · **模糊器 250+150×4=850 例 0 失配**。零回归。

## §10.68b 嵌套内联多帧 阶段 2：零体内守卫方法的多帧内联（放开 frame_base!=0 abort，安全网保证正确性，✅ 阶段达成）

### 背景
阶段 1 把栈化基础设施铺好后，阶段 2 解除 `frame_base != 0` 的硬性 abort——让 OP_SELF 和 OP_CALL 方法内联路径在嵌套帧里也能触发。现有方法内联纯度检查（`proto_is_method_inlinable` 等）不允许方法体里有 CALL/SELF，所以阶段 2 目前是**前瞻性基础设施改动**——为阶段 3（通用 resume-at-call，放开 callee 纯度要求）铺路。

### 实现（三处改动 + 安全网保证）
**(1) OP_SELF 放开 frame_base != 0 abort**（`spt_jit.c`）：
```c
// 旧: if (rc->frame_base != 0) { rc->aborted = 1; return 0; }
// 新: if (rc->frame_base != 0 && op != OP_SELF) { rc->aborted = 1; return 0; }
```
SETLIST/CLOSURE/VARARG 仍在嵌套帧里 abort，只有 OP_SELF 放开。

**(2) OP_SELF 用户方法路径栈访问修复**：
```c
// 旧: TValue *recv = s2v((rc->ci->func.p + 1) + b);
// 新: TValue *recv = s2v((rc->ci->func.p + 1) + rc->frame_base + b);
```
嵌套帧里 `b` 是当前帧的寄存器号，需要加 `frame_base` 偏移才能找到正确的栈位置。

**(3) OP_CALL 方法内联路径放开 frame_base != 0 abort**：
```c
// 旧: if ((c != 1 && c != 2) || b < 1 || rc->frame_base != 0) { rc->aborted = 1; return 0; }
// 新: if ((c != 1 && c != 2) || b < 1) { rc->aborted = 1; return 0; }
```

**(4) maxslot 修复**：
```c
// 旧: if (a + 1 > ir->maxslot) ir->maxslot = a + 1;
// 新: if (rc->frame_base + a + 1 > ir->maxslot) ir->maxslot = rc->frame_base + a + 1;
```
嵌套帧里 maxslot 必须用绝对 slot（`frame_base + a + 1`），否则 codegen 会漏 spill。

### 正确性（安全网保证）
**关键安全机制 = 内联帧退出安全网**（§10.46 建立的 post-optimization guard exit-PC range check）：
- 安全网扫描所有 surviving guard 的 exit PC，如果任何 exit PC 在主 proto 的 `[code, code+sizecode)` 范围外（即在内联 callee 的 proto 里），拒绝编译。
- 嵌套帧里的方法体守卫的 exit PC 是 `method_resume_snap`（内联帧的 SELF PC）——不在主 proto 范围内 → 安全网拒绝。
- 只有**零体内守卫**的方法体（如 `proto_is_multiwrite_method_inlinable`：入口字段布局守卫 + 体内无守卫字段访问）才能通过安全网。

**入口校验（多方法）**：`sptjit_trace_enter` 遍历 `methods[0..n_methods-1]`，检查每个方法的 receiver slot 的 metatable + 方法身份。`recv_slot` 是 SLOAD 的 aux，在嵌套帧里通过参数传递链（`this` = slot 0，IR ref 是 caller 的 SLOAD）始终追溯到主帧 slot——入口校验可以正确检查。

### 当前限制
现有方法内联纯度检查（`proto_is_method_inlinable` 等）不允许方法体里有 CALL/SELF，所以阶段 2 目前没有场景能触发嵌套帧里的方法内联。要触发需要：
- 阶段 3（§10.68c）：通用 resume-at-call——放开 callee 纯度要求（允许带分支/循环的 callee），exit stub 重建 CallInfo 栈。

### 验证（完整门槛，全绿）
构建 0 err。**367/367 ctest 全通过** · **134/134 kernel 差分全通过** · **模糊器 250+150×4=850 例 0 失配**。零回归（前瞻性改动，安全网保证正确性）。

## §10.68c 嵌套内联多帧 阶段 3：通用 resume-at-call（带分支 callee 内联，exit stub 重建 callee CI，✅ 阶段达成）

### 背景
阶段 2 放开了 `frame_base != 0` 的 abort，但现有 callee 纯度检查不允许方法体里有 CALL/SELF，也没有场景能触发嵌套帧里的内联。阶段 3 实现**通用 resume-at-call**：当内联 callee 体内的守卫失败时，exit stub 重建 callee 的 CallInfo 帧（压帧、设 base、savedpc），让解释器在 callee PC 恢复执行。

### 核心机制
**resume-at-call**：exit stub 对 in-callee exit PC 调用 C helper `sptjit_exit_resume`，该函数：
1. 推送真正的 callee CI 帧（设 func/top/savedpc/callstatus）
2. 设 caller CI 的 savedpc 为 CALL 后的 PC
3. 设 `L->ci = callee CI`

**安全网修改**：post-optimization 扫描所有 surviving guard 的 exit PC，如果 exit PC 在主 proto 外但在 callee proto 的 code 范围内**且有 resume info**（`callee_proto != NULL`），允许编译。

### 实现（8 处改动）

**(1) SPTResumeInfo 结构 + exit_resume 数组**（`spt_jit_internal.h`）：
```c
typedef struct {
  Proto *callee_proto;       /* NULL = no resume-at-call */
  int callee_frame_base;     /* frame_base at inline push */
  const Instruction *caller_resume_pc; /* caller PC after CALL */
  int nresults;              /* C operand - 1 */
} SPTResumeInfo;
```
`SPTTrace` 增加 `exit_resume[SPT_JIT_MAX_SNAPSHOTS]`；`SPTInlineFrame` 增加 `callee_proto` / `nresults` 字段。

**(2) `sptjit_exit_resume` C helper**（`spt_jit.c`）：推送 callee CI（`next_ci` 复用或 `luaE_extendCI`），设 func/top/savedpc/callstatus，设 caller CI savedpc，设 `L->ci = callee CI`。

**(3) exit stub 调用 C helper**（`spt_jit_codegen.c`）：`gen_exit_stub` 对 in-callee exit PC（`exit_resume[si].callee_proto != NULL`）调用 `sptjit_exit_resume`，传 callee_proto/callee_frame_base/caller_resume_pc/nresults。

**(4) `sptjit_hot_check` 宏更新**（`lvm.c`）：
```c
#define sptjit_hot_check(ci, target_pc) \
  (savepc(ci), sptjit_trace_hot(L, ci, (target_pc)) && \
   (ci = L->ci, pc = ci->u.l.savedpc, cl = ci_func(ci), k = cl->p->k, \
    updatebase(ci), updatetrap(ci), 1))
```
trace 返回后 `sptjit_exit_resume` 已将 `L->ci` 切到 callee CI，宏必须更新 `ci/cl/k` 否则解释器用 caller 的常量表。

**(5) `sptjit_trace_enter` CI 检查**（`spt_jit.c`）：`if (L->ci != ci) break`——trace 在 in-callee guard 退出后 `L->ci` 已切到 callee CI，入口校验不能继续。

**(6) 录制时存储 resume info**（`spt_jit.c`）：
- inline push（method + free-function）处存 `callee_proto` / `nresults` 到 `SPTInlineFrame`
- `rec_snap` 对 free-function inlined callee 体内的 guard 填充 `snap_resume[snap]`
- 录制后复制 `snap_resume` → `t->exit_resume`

**(7) 安全网允许有 resume info 的 in-callee exit PC**（`spt_jit.c`）：
```c
if (epc && (epc < lo || epc >= hi)) {
  SPTResumeInfo *ri = &t->exit_resume[si];
  if (ri->callee_proto) {
    /* check epc in callee proto's code range */
    if (epc >= clo && epc < chi) continue;  /* valid in-callee exit */
  }
  /* else: refuse to compile */
}
```

**(8) `proto_is_branch_inlinable` + 放宽 `rec_cond_branch` abort**（`spt_jit.c`）：
- 新函数 `proto_is_branch_inlinable`：允许 forward conditional branches（OP_LT/LE/EQ + OP_JMP）+ 多个 RETURN1 + straight-line ops
- `rec_cond_branch` abort 放宽：仅对 method inline（`method_self_pc != NULL`）abort，允许 free-function inline 的 guarded branch

### 验证（完整门槛，全绿）
构建 0 err。**368/368 ctest 全通过** · **134/135 kernel 差分通过**（唯一 fail=`foreach_map_str` 为预存 map 哈希顺序 flakiness，stash 验证确认与本次改动无关）· **模糊器 200+200=400 例 0 失配**。零回归。

新增测试 kernel `inline_branch_multi.spt`：`classify(int x)` 函数有两个条件返回路径 + 一个默认返回，验证多返回路径 callee 的内联正确性。

## §10.69 带循环 callee 内联（扩展 proto_is_branch_inlinable + 修复 maxslot 偏移，✅ 阶段达成）

### 背景
§10.68c 完成后，`proto_is_branch_inlinable` 允许 forward conditional branches + 多 RETURN1 的 callee 内联，但仍排除 OP_FORPREP/OP_FORLOOP。这意味着含 numeric for-loop 的 callee（如 `sum_range(int n) { int s=0; for(int i=1,n) s+=i; return s; }`）无法被内联——CALL 处直接 abort。

### 核心改动（3 处）

**(1) 扩展 `proto_is_branch_inlinable` 允许 OP_FORPREP/OP_FORLOOP**：
```c
case OP_FORPREP:
case OP_FORLOOP:
  break;
```
理由：`try_unroll_inner_loop`（§10.33-§10.35）已在录制时投机展开内层 numeric for-loop——它用 `eval_invariant_int` 评估循环边界（KINT 或 loop-invariant SLOAD），用 `emit_pin_guard`（两个 GUARD_LE）固定投机值，然后逐次重放循环体。如果循环体不是 straight-line 或 trip count 超过展开上限，unroller bail，FORLOOP back-edge abort trace 安全回退到解释器。

**(2) 修复 `try_unroll_inner_loop` 的 maxslot 偏移 bug**（行 ~2835）：
```c
// 旧: if (a + 2 > ir->maxslot) ir->maxslot = a + 2;
// 新: if (base + a + 2 > ir->maxslot) ir->maxslot = base + a + 2;
```
`try_unroll_inner_loop` 用 `base = rc->frame_base` 访问 `reg_map`，但 maxslot 更新遗漏了 `base` 偏移。当 `frame_base != 0`（内联 callee）时，maxslot 被设为过小的值，导致 codegen 截断寄存器溢出。

**(3) 修复 OP_FORLOOP case 的同一 maxslot 偏移 bug**（行 ~3751）：
```c
// 旧: if (a + 2 > ir->maxslot) ir->maxslot = a + 2;
// 新: if (rc->frame_base + a + 2 > ir->maxslot) ir->maxslot = rc->frame_base + a + 2;
```

### 效果
`inline_loop.spt` 的 `sum_range(10)` 现在被成功内联到外层循环 trace 中。IR 显示 66 insts：函数引用 SLOAD + GUARD_T 后，内层 for-loop 被 `try_unroll_inner_loop` 完全展开（n=10 是常量），10 次 `s += i` 经常量折叠后变为单个常量 55，外层循环体变为 `total = total + 55`。两个 trace 编译成功（16 insts 内层独立 trace + 66 insts 外层含内联 trace），0 aborts，entries==exits，guard_fail=0。

### 验证（完整门槛，全绿）
构建 0 err。**369/369 ctest 全通过** · **136/136 kernel 差分通过**（含新增 `inline_loop.spt`）· **模糊器 40 例 0 失配**。零回归。

新增测试 kernel `inline_loop.spt`：`sum_range(int n)` 含 numeric for-loop，外层循环调用 100 万次，验证带循环 callee 的内联 + 展开 + 常量折叠正确性。
