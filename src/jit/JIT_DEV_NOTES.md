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

**已知局限**：`OP_TEST`/`OP_TESTSET`（布尔真值分支 `if flag`）仍有同样的静态直落问题
（偏置布尔分支约 0.5×），但其真值 guard 测的是 value 字段、与 `docondjump` 的 `cond==k` 极性
纠缠，改动易把"正确但慢"变成"错误"，故保持原样（结果正确，只是慢）。需要重做基于 tag 的
真值 guard 才能安全修复——列为后续。
