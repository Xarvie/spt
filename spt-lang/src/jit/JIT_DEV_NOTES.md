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
