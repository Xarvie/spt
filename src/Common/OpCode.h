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
  OP_ADD, /* A B C   | R[A] := R[B] + R[C]                        */
  OP_SUB, /* A B C   | R[A] := R[B] - R[C]                        */
  OP_MUL, /* A B C   | R[A] := R[B] * R[C]                        */
  OP_DIV, /* A B C   | R[A] := R[B] / R[C]                        */
  OP_MOD, /* A B C   | R[A] := R[B] % R[C]                        */
  OP_UNM, /* A B     | R[A] := -R[B]                              */

  /* --- 比较与逻辑 (测试跳转模式) --- */
  OP_JMP,  /* sBx     | pc += sBx                                  */
  OP_EQ,   /* A B C   | if ((R[A] == R[B]) != C) then pc++         */
  OP_LT,   /* A B C   | if ((R[A] <  R[B]) != C) then pc++         */
  OP_LE,   /* A B C   | if ((R[A] <= R[B]) != C) then pc++         */
  OP_TEST, /* A C     | if (not R[A] == C) then pc++               */

  /* --- 函数调用 --- */
  OP_CALL,   /* A B C   | R[A], ... := R[A](R[A+1], ... ,R[A+B-1])   */
  OP_INVOKE, /* A B C   | R[A] := R[A].K[C](R[A+1], ...)             */
  OP_RETURN, /* A B     | return R[A], ... ,R[A+B-2]                 */

  /* --- 模块系统 --- */
  OP_IMPORT,      /* A Bx    | R[A] := import(K[Bx])  导入模块              */
  OP_IMPORT_FROM, /* A B C   | R[A] := import(K[B])[K[C]]  导入特定符号      */
  OP_EXPORT,      /* A       | export R[A]  标记导出                        */

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
