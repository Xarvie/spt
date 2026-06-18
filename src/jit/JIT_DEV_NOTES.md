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
if (sptjit_hot_check(ci, pc - GETARG_Bx(i))) vmbreak;
```

`vmbreak` 表示 JIT 接管了执行，解释器直接跳出当前 dispatch。

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
