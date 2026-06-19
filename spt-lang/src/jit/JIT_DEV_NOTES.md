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
