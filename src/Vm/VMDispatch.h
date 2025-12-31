#pragma once

#include "../Common/OpCode.h"

namespace spt {

// ============================================================================
// 编译器检测 - 自动选择分发方式
// ============================================================================

#if defined(__GNUC__) || defined(__clang__)
#define SPT_USE_COMPUTED_GOTO 1
#else
#define SPT_USE_COMPUTED_GOTO 0
#endif

// ============================================================================
// Computed Goto 分发 (GCC/Clang)
// ============================================================================

#if SPT_USE_COMPUTED_GOTO

#define SPT_DISPATCH_TABLE_BEGIN() static const void *dispatch_table[] = {

#define SPT_DISPATCH_TABLE_ENTRY(op) &&L_##op,

#define SPT_DISPATCH_TABLE_END()                                                                   \
  }                                                                                                \
  ;

#define SPT_DISPATCH()                                                                             \
  do {                                                                                             \
    instruction = *reinterpret_cast<const uint32_t *>(frame->ip++);                                \
    goto *dispatch_table[static_cast<uint8_t>(GET_OPCODE(instruction))];                           \
  } while (0)

#define SPT_DISPATCH_LOOP_BEGIN() SPT_DISPATCH();

#define SPT_DISPATCH_LOOP_END()

#define SPT_OPCODE(op) L_##op:

// ============================================================================
// Switch 分发 (MSVC 及其他编译器回退)
// ============================================================================

#else

#define SPT_DISPATCH_TABLE_BEGIN()
#define SPT_DISPATCH_TABLE_ENTRY(op)
#define SPT_DISPATCH_TABLE_END()

#define SPT_DISPATCH() continue

#define SPT_DISPATCH_LOOP_BEGIN()                                                                  \
  for (;;) {                                                                                       \
    uint32_t instruction = *reinterpret_cast<const uint32_t *>(frame->ip++);                       \
    OpCode opcode = GET_OPCODE(instruction);                                                       \
    uint8_t A = GETARG_A(instruction);                                                             \
    uint8_t B = GETARG_B(instruction);                                                             \
    uint8_t C = GETARG_C(instruction);                                                             \
    uint32_t Bx = GETARG_Bx(instruction);                                                          \
    int32_t sBx = GETARG_sBx(instruction);                                                         \
    switch (opcode) {

#define SPT_DISPATCH_LOOP_END()                                                                    \
  default:                                                                                         \
    runtimeError("Unknown opcode: %d", static_cast<int>(opcode));                                  \
    return InterpretResult::RUNTIME_ERROR;                                                         \
    }                                                                                              \
    }

#define SPT_OPCODE(op) case OpCode::op:

#endif

// ============================================================================
// 跳转表定义 - 必须与 OpCode 枚举顺序一致
// ============================================================================

#define SPT_DEFINE_DISPATCH_TABLE()                                                                \
  SPT_DISPATCH_TABLE_BEGIN()                                                                       \
  SPT_DISPATCH_TABLE_ENTRY(OP_MOVE)                                                                \
  SPT_DISPATCH_TABLE_ENTRY(OP_LOADK)                                                               \
  SPT_DISPATCH_TABLE_ENTRY(OP_LOADBOOL)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_LOADNIL)                                                             \
  SPT_DISPATCH_TABLE_ENTRY(OP_NEWLIST)                                                             \
  SPT_DISPATCH_TABLE_ENTRY(OP_NEWMAP)                                                              \
  SPT_DISPATCH_TABLE_ENTRY(OP_GETINDEX)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_SETINDEX)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_GETFIELD)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_SETFIELD)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_NEWCLASS)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_NEWOBJ)                                                              \
  SPT_DISPATCH_TABLE_ENTRY(OP_GETUPVAL)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_SETUPVAL)                                                            \
  SPT_DISPATCH_TABLE_ENTRY(OP_CLOSURE)                                                             \
  SPT_DISPATCH_TABLE_ENTRY(OP_CLOSE_UPVALUE)                                                       \
  SPT_DISPATCH_TABLE_ENTRY(OP_ADD)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_SUB)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_MUL)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_DIV)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_MOD)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_UNM)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_JMP)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_EQ)                                                                  \
  SPT_DISPATCH_TABLE_ENTRY(OP_LT)                                                                  \
  SPT_DISPATCH_TABLE_ENTRY(OP_LE)                                                                  \
  SPT_DISPATCH_TABLE_ENTRY(OP_TEST)                                                                \
  SPT_DISPATCH_TABLE_ENTRY(OP_CALL)                                                                \
  SPT_DISPATCH_TABLE_ENTRY(OP_INVOKE)                                                              \
  SPT_DISPATCH_TABLE_ENTRY(OP_RETURN)                                                              \
  SPT_DISPATCH_TABLE_ENTRY(OP_IMPORT)                                                              \
  SPT_DISPATCH_TABLE_ENTRY(OP_IMPORT_FROM)                                                         \
  SPT_DISPATCH_TABLE_ENTRY(OP_EXPORT)                                                              \
  SPT_DISPATCH_TABLE_ENTRY(OP_ADDI)                                                                \
  SPT_DISPATCH_TABLE_ENTRY(OP_EQK)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_EQI)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_LTI)                                                                 \
  SPT_DISPATCH_TABLE_ENTRY(OP_LEI)                                                                 \
  SPT_DISPATCH_TABLE_END()

} // namespace spt