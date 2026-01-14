#pragma once
#include "../Common/Types.h"
#include "Value.h"
#include <vector>

namespace spt {

// 前置声明
class VM;
struct Closure;
struct UpValue;

// ============================================================================
// CallFrame - 调用帧 (POD 类型，无析构函数)
// ============================================================================
struct CallFrame {
  Closure *closure = nullptr;
  const Instruction *ip = nullptr;

  Value *slots = nullptr;    // 指向当前帧的第一个寄存器 (对应 Lua 的 base)
  Value *returnTo = nullptr; // 指向返回值应该写入的位置 (对应 Lua 的 firstResult)

  int expectedResults = 1;
  int deferBase = 0;
};

// ============================================================================
// Fiber 状态
// ============================================================================
enum class FiberState : uint8_t {
  NEW,       // 创建但未运行
  RUNNING,   // 正在执行
  SUSPENDED, // 已暂停（yield）
  DONE,      // 正常完成
  ERROR      // 出错终止
};

// ============================================================================
// FiberObject - 执行上下文核心载体
// ============================================================================
struct FiberObject : GCObject {
  // === 执行状态 ===
  FiberState state = FiberState::NEW;

  Value *stack = nullptr;     // 栈底指针 (Lua: L->stack)
  Value *stackTop = nullptr;  // 当前栈顶指针 (Lua: L->top)
  Value *stackLast = nullptr; // 栈内存的极限位置 (Lua: L->stack_last)
  size_t stackSize = 0;       // 当前栈总容量 (为了方便计算)

  std::vector<CallFrame> frames;
  int frameCount = 0;

  std::vector<Value> deferStack;
  int deferTop = 0; // 指向 deferStack 的下一个可用位置

  UpValue *openUpvalues = nullptr;

  // === 入口闭包 ===
  Closure *closure = nullptr;

  // === 调用链 ===
  FiberObject *caller = nullptr;

  // === 错误状态 ===
  Value error;
  bool hasError = false;

  // === yield 返回值 ===
  Value yieldValue;

  // === 配置 ===
  static constexpr size_t DEFAULT_STACK_SIZE = 64;
  static constexpr size_t DEFAULT_FRAMES_SIZE = 8;
  static constexpr int MAX_FRAMES = 256;

  // === 构造 ===
  FiberObject() {
    type = ValueType::Fiber;

    // 分配初始栈内存
    stackSize = DEFAULT_STACK_SIZE;
    stack = new Value[stackSize];
    stackTop = stack;
    stackLast = stack + stackSize; // 指向末尾

    // 初始化栈内容为 nil
    for (size_t i = 0; i < stackSize; ++i) {
      stack[i] = Value::nil();
    }

    frames.resize(DEFAULT_FRAMES_SIZE);
    deferStack.resize(16);
    deferTop = 0;
    error = Value::nil();
    yieldValue = Value::nil();
  }

  ~FiberObject() {
    if (stack) {
      delete[] stack;
      stack = nullptr;
    }
  }

  // === 状态查询 ===
  bool isNew() const { return state == FiberState::NEW; }

  bool isRunning() const { return state == FiberState::RUNNING; }

  bool isSuspended() const { return state == FiberState::SUSPENDED; }

  bool isDone() const { return state == FiberState::DONE; }

  bool isError() const { return state == FiberState::ERROR; }

  bool canResume() const { return state == FiberState::NEW || state == FiberState::SUSPENDED; }

  void checkStack(int needed) {
    // 1. 检查剩余空间是否足够
    // stackLast 指向分配内存的末尾，stackTop 指向当前栈顶
    if (stackLast - stackTop >= needed) {
      return;
    }

    // 2. 计算新容量
    size_t used = stackTop - stack;
    size_t required = used + needed;

    size_t newSize = stackSize;
    if (newSize == 0)
      newSize = DEFAULT_STACK_SIZE;

    while (newSize < required) {
      newSize *= 2;
    }

    // 3. 分配新内存并保留旧指针
    Value *oldStack = stack;
    Value *newStack = new Value[newSize];

    // 4. 迁移数据
    for (size_t i = 0; i < used; ++i) {
      newStack[i] = oldStack[i];
    }
    // 初始化新增区域为 nil
    for (size_t i = used; i < newSize; ++i) {
      newStack[i] = Value::nil();
    }

    // 5. 更新 Fiber 主状态指针
    stack = newStack;
    stackSize = newSize;
    stackTop = stack + used;
    stackLast = stack + newSize;

    // 6. 修复 Open UpValues (闭包引用的栈变量)
    // oldStack 尚未释放，指针运算 (uv->location - oldStack) 是安全的
    if (openUpvalues != nullptr) {
      fixUpvaluePointers(oldStack, newStack);
    }

    // 7. 修复 CallFrames (调用帧中的指针)
    for (int i = 0; i < frameCount; ++i) {
      CallFrame &frame = frames[i];

      // 修复 slots 指针
      if (frame.slots) {
        frame.slots = newStack + (frame.slots - oldStack);
      }

      // 修复 returnTo 指针
      if (frame.returnTo) {
        frame.returnTo = newStack + (frame.returnTo - oldStack);
      }
    }

    // 8. 释放
    if (oldStack) {
      delete[] oldStack;
    }
  }

  void ensureStack(int needed) { checkStack(needed); }

  void ensureDefers(int needed) {
    if (deferTop + needed <= (int)deferStack.size()) {
      return;
    }
    size_t newSize = deferStack.size() * 2;
    while (newSize < deferTop + needed) {
      newSize *= 2;
    }
    deferStack.resize(newSize);
  }

  void ensureFrames(int needed) {
    size_t required = static_cast<size_t>(frameCount + needed);

    if (required <= frames.size()) {
      return;
    }

    size_t newSize = frames.size() * 2;
    if (newSize < DEFAULT_FRAMES_SIZE) {
      newSize = DEFAULT_FRAMES_SIZE;
    }
    while (newSize < required) {
      newSize *= 2;
    }

    frames.resize(newSize);
  }

  void fixUpvaluePointers(Value *oldBase, Value *newBase);

  size_t stackUsed() const { return stackTop - stack; }

  // === 便捷栈操作 ===
  void push(Value val) {
    ensureStack(1);
    *stackTop++ = val;
  }

  Value pop() { return *--stackTop; }

  Value peek(int distance = 0) const { return stackTop[-1 - distance]; }

  // === 辅助方法：根据 slotsBase 获取实际的 slots 指针 ===
  Value *getSlots(int slotsBase) { return stack + slotsBase; }

  // === 重置===
  void reset() {
    state = FiberState::NEW;
    stackTop = stack; // 直接重置指针
    frameCount = 0;
    deferTop = 0;
    openUpvalues = nullptr;
    caller = nullptr;
    error = Value::nil();
    hasError = false;
    yieldValue = Value::nil();
  }
};

// ============================================================================
// Fiber 标准库加载
// ============================================================================
class SptFiber {
public:
  static void load(VM *vm);
};

} // namespace spt