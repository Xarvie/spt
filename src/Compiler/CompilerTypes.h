#pragma once

#include "../Common/Types.h"
#include <string>
#include <variant>
#include <vector>

namespace spt {

// 局部变量信息
struct LocalVar {
  std::string name;
  int slot;        // 栈槽位
  int scopeDepth;  // 作用域深度
  bool isCaptured; // 是否被闭包捕获
};

// UpValue 信息 (编译期)
struct UpvalueInfo {
  std::string name;
  uint8_t index; // 在父函数中的索引
  bool isLocal;  // true=直接引用父函数局部变量
};

// 跳转补丁
struct JumpPatch {
  int instructionIndex; // 需要修补的指令位置
  int targetLabel;      // 目标标签 (用于break/continue)
};

// 循环上下文
struct LoopContext {
  int startPc;                          // 循环开始位置
  int scopeDepth;                       // 进入循环时的作用域深度
  std::vector<JumpPatch> breakJumps;    // break 跳转需要修补
  std::vector<JumpPatch> continueJumps; // continue 跳转需要修补
};

// 函数编译状态
struct FunctionState {
  FunctionState *enclosing = nullptr; // 外层函数
  Prototype proto;                    // 正在构建的原型

  std::vector<LocalVar> locals;      // 局部变量表
  std::vector<UpvalueInfo> upvalues; // UpValue 表
  std::vector<LoopContext> loops;    // 循环栈

  int scopeDepth = 0;      // 当前作用域深度
  int currentStackTop = 0; // 当前栈顶位置
  int maxStack = 0;        // 最大栈深度

  bool isMethod = false;      // 是否为方法
  bool isInitializer = false; // 是否为构造函数

  // 获取下一个可用栈槽
  int allocSlot() {
    int slot = currentStackTop++;
    if (currentStackTop > maxStack)
      maxStack = currentStackTop;
    return slot;
  }

  // 分配N个连续栈槽
  int allocSlots(int n) {
    int base = currentStackTop;
    currentStackTop += n;
    if (currentStackTop > maxStack)
      maxStack = currentStackTop;
    return base;
  }

  void freeSlots(int n) { currentStackTop -= n; }
};

// 类编译状态
struct ClassState {
  ClassState *enclosing = nullptr;
  std::string name;
};

// 编译错误
struct CompileError {
  std::string message;
  std::string filename;
  int line;
  int column;
};

// 赋值目标描述 (Compiler 计算，CodeGen 使用)
struct LValue {
  enum Kind { LOCAL, UPVALUE, GLOBAL, INDEX, FIELD } kind;

  int a, b, c; // 指令操作数
};

} // namespace spt