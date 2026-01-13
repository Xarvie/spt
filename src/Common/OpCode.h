#pragma once

#include <cstdint>

/*
** ============================================================================
** 指令格式说明 (复用 Lua 5.4 布局)
** ============================================================================
** iABC:  [ C(8) | B(8) | k(1) | A(8) | Op(7) ]
** iABx:  [    Bx(17)   | A(8) | Op(7) ]
** iAsBx: [   sBx(17)   | A(8) | Op(7) ]
** iAx:   [           Ax(25)   | Op(7) ]
** ============================================================================
*/

enum class OpCode : uint8_t {
  /* --- 基础数据搬运 --- */
  OP_MOVE = 0, /* A B     | R[A] := R[B]                               */
  OP_LOADK,    /* A Bx    | R[A] := K[Bx] (热更新核心：常量表加载)        */
  OP_LOADBOOL, /* A B C   | R[A] := (bool)B; if (C) pc++               */
  OP_LOADNIL,  /* A B     | R[A], R[A+1], ..., R[A+B] := nil           */

  /* --- 容器操作 (区分 Map 和 List) --- */
  OP_NEWLIST,  /* A B     | R[A] := [] (初始容量 B)                     */
  OP_NEWMAP,   /* A B     | R[A] := {} (初始容量 B)                     */
  OP_GETINDEX, /* A B C   | R[A] := R[B][R[C]] (通用索引读取)            */
  OP_SETINDEX, /* A B C   | R[A][R[B]] := R[C] (通用索引写入)            */

  /* --- 热更新友好型字段访问 (使用字符串 Key) --- */
  OP_GETFIELD, /* A B C   | R[A] := R[B][K[C]] (C为常量表字符串索引)      */
  OP_SETFIELD, /* A B C   | R[A][K[B]] := R[C] (B为常量表字符串索引)      */

  /* --- 类与对象支持 --- */
  OP_NEWCLASS, /* A Bx    | R[A] := NewClass(K[Bx]) (创建类定义)        */
  OP_NEWOBJ,   /* A B C   | R[A] := new R[B]( R[B+1] ... R[B+C] ) B:类对象基址 C:参数个数*/

  /* --- 闭包与 UpValue (Flat 模式，索引从 0 开始) --- */
  OP_GETUPVAL,      /* A B     | R[A] := UpValue[B]                         */
  OP_SETUPVAL,      /* A B     | UpValue[B] := R[A]                         */
  OP_CLOSURE,       /* A Bx    | R[A] := Closure(KPROTO[Bx])                */
  OP_CLOSE_UPVALUE, /* A       | Close UpValues >= R[A]                     */

  /* --- 算术运算 --- */
  OP_ADD,  /* A B C   | R[A] := R[B] + R[C]                        */
  OP_SUB,  /* A B C   | R[A] := R[B] - R[C]                        */
  OP_MUL,  /* A B C   | R[A] := R[B] * R[C]                        */
  OP_DIV,  /* A B C   | R[A] := R[B] / R[C]                        */
  OP_IDIV, /* A B C   | R[A] := floor(R[B] / R[C])                */
  OP_MOD,  /* A B C   | R[A] := R[B] % R[C]                        */
  OP_UNM,  /* A B     | R[A] := -R[B]                              */

  /* --- 位运算 --- */
  OP_BAND, /* A B C   | R[A] := R[B] & R[C]                        */
  OP_BOR,  /* A B C   | R[A] := R[B] | R[C]                        */
  OP_BXOR, /* A B C   | R[A] := R[B] ^ R[C]                        */
  OP_BNOT, /* A B     | R[A] := ~R[B]                              */
  OP_SHL,  /* A B C   | R[A] := R[B] << R[C]                       */
  OP_SHR,  /* A B C   | R[A] := R[B] >> R[C]                       */

  /* --- 比较与逻辑 (测试跳转模式) --- */
  OP_JMP,  /* sBx     | pc += sBx                                  */
  OP_EQ,   /* A B C   | if ((R[A] == R[B]) != C) then pc++         */
  OP_LT,   /* A B C   | if ((R[A] <  R[B]) != C) then pc++         */
  OP_LE,   /* A B C   | if ((R[A] <= R[B]) != C) then pc++         */
  OP_TEST, /* A C     | if (not R[A] == C) then pc++               */

  /* --- 函数调用 --- */
  OP_CALL,   /* A B C   | R[A], ... := R[A](R[A+1], ... ,R[A+B-1])   */
           //  OP_INVOKE, /* A B C   | R[A] := R[A].K[C](R[A+1], ...)             */
           /* ** OP_INVOKE (宽指令)
            ** 格式: A B C
            ** Word 1: [ C(8) | B(8) | k(1) | A(8) | OP_INVOKE(7) ]
            ** Word 2: [           Ax(25)            | (ignored)(7) ]  <-- 存储方法名常量索引
            **
            ** A: 接收者(Receiver)寄存器，也是返回值起始位置
            ** B: 参数个数 + 1 (0表示变长，目前 invoke 暂不支持变长参数)
            ** C: 期望返回值个数 + 1
            ** Ax: 方法名在常量表中的索引
            */
  OP_INVOKE,
  OP_RETURN, /* A B     | return R[A], ... ,R[A+B-2]                 */

  /* --- 模块系统 --- */
  OP_IMPORT,      /* A Bx    | R[A] := import(K[Bx])  导入模块              */
  OP_IMPORT_FROM, /* A B C   | R[A] := import(K[B])[K[C]]  导入特定符号      */

  OP_DEFER, /* A Bx | 将栈槽 R[A] 处的闭包压入当前函数的 defer 栈 */

  /* --- 优化指令 --- */
  OP_ADDI, /* iABC R[A] = R[B] + sC (C视为有符号8位整数) */
  OP_EQK, /* iABC if (R[A] == K[B]) != C then pc++ B: 常量表索引(0-255), C:期望结果(0或1) */
  OP_EQI, /* iABC if (R[A] == sB) != C then pc++  */
  OP_LTI, /* iABC if (R[A] < sB) != C then pc++   */
  OP_LEI, /* iABC: if (R[A] <= sB) != C then pc++ A: 寄存器, B: 有符号8位立即数, C: 期望结果*/

  OP_FORPREP, /* A sBx | R[A] -= R[A+2]; pc += sBx      初始化：预减 Step，跳转到循环尾部首次检查 */
  OP_FORLOOP, /* A sBx | R[A] += R[A+2]; if R[A] <= R[A+1] then pc += sBx 循环尾：步进 + 检查 + 回跳
               */
  OP_LOADI, /* A sBx | R[A] := sBx  加载17位有符号立即数 (用于优化小整数加载) */

  /* ** OP_TFORCALL
  ** 格式: A C
  **
  ** [输入寄存器布局 - 硬性契约]
  ** R[A]   : Generator 函数 (迭代器)
  ** R[A+1] : State (状态常量)
  ** R[A+2] : Control (控制变量，上一轮的第一个返回值)
  **
  ** [输出寄存器布局]
  ** R[A+3] ... R[A+2+C] : 本次迭代的返回值 (Loop Variables)
  **
  ** [关键行为]
  ** 1. 自动压栈调用 R[A](R[A+1], R[A+2])。
  ** 2. 若是 Script Closure：
  ** - 创建新栈帧。
  ** - 注意：新帧的 slotsBase 必须指向 R[A+1] (State)，即偏移量需 +1，
  ** 以确保被调用函数的参数 0 是 State，参数 1 是 Control。
  ** 3. 若是 Native Function：
  ** - 直接传入 &slots[A+1] 作为参数数组基址。
  ** 4. 结果强制回写到 R[A+3] 起始的位置。
  **
  ** [编译器配合]
  ** 编译器必须强制将循环变量符号 (i, v...) 绑定到 slot A+3, A+4...
  ** 而非分配新的局部变量槽位。
  */
  OP_TFORCALL,

  /* ** OP_TFORLOOP
  ** 格式: A sBx
  **
  ** [操作数]
  ** A   : 基址。指向 Generator 函数的位置 (与 TFORCALL 共享 A)。
  ** sBx : 退出跳转偏移 (Exit Jump Offset)。指向循环结束后的指令。
  **
  ** [输入检查]
  ** 检查 R[A+3] (即迭代器的第一个返回值 Var1)。
  **
  ** [逻辑分支]
  ** 1. 若 R[A+3] != nil (循环继续):
  ** - 注意:关键副作用: R[A+2] = R[A+3]
  ** (将当前的 Var1 更新为下一次调用的 Control 变量)。
  ** - 不跳转 (Fallthrough)，继续执行循环体。
  **
  ** 2. 若 R[A+3] == nil (循环结束):
  ** - PC += sBx (跳转到循环外)。
  **
  ** [典型指令流]
  ** Loop:
  ** OP_TFORCALL A, C
  ** OP_TFORLOOP A, ExitOffset
  ** ... 循环体 ...
  ** OP_JUMP     Loop
  ** Exit:
  */
  OP_TFORLOOP,
};

/* --- 指令解码宏 (完全兼容 Lua 5.4 布局) --- */
#define POS_OP 0
#define SIZE_OP 7
#define POS_A (POS_OP + SIZE_OP)
#define SIZE_A 8
#define POS_k (POS_A + SIZE_A)
#define POS_B (POS_k + 1)
#define SIZE_B 8
#define POS_C (POS_B + SIZE_B)
#define SIZE_C 8
#define POS_Bx POS_k
#define SIZE_Bx (SIZE_B + SIZE_C + 1)

#define GET_OPCODE(i) (static_cast<OpCode>((i) & 0x7F))
#define GETARG_A(i) (((i) >> POS_A) & 0xFF)
#define GETARG_B(i) (((i) >> POS_B) & 0xFF)
#define GETARG_C(i) (((i) >> POS_C) & 0xFF)
#define GETARG_Bx(i) (((i) >> POS_Bx) & 0x1FFFF)
#define GETARG_sBx(i) (static_cast<int>(GETARG_Bx(i) - (0x1FFFF >> 1)))

#define POS_Ax POS_A
#define SIZE_Ax (SIZE_A + SIZE_B + SIZE_C + 1) // 25 bits (也就是除了 OpCode 7bit 外的全部空间)

#define GETARG_Ax(i) (((i) >> POS_Ax) & 0x1FFFFFF)
#define MAKE_Ax(op, ax)                                                                            \
  ((static_cast<uint32_t>(op) & 0x7F) | ((static_cast<uint32_t>(ax) & 0x1FFFFFF) << POS_Ax))