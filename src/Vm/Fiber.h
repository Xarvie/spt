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

  // === Value 栈 ===
  Value *stack = nullptr;     // 栈底指针 (Lua: L->stack)
  Value *stackTop = nullptr;  // 当前栈顶指针 (Lua: L->top)
  Value *stackLast = nullptr; // 栈内存的极限位置 (Lua: L->stack_last)
  size_t stackSize = 0;       // 当前栈总容量 (为了方便计算)

  // === 调用帧栈 (手动管理) ===
  CallFrame *frames = nullptr; // 帧数组指针
  int framesCapacity = 0;      // 帧数组总容量
  int frameCount = 0;          // 当前使用的帧数

  // === Defer 栈 ===
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

    // 分配初始 Value 栈内存
    stackSize = DEFAULT_STACK_SIZE;
    stack = new Value[stackSize];
    stackTop = stack;
    stackLast = stack + stackSize;

    // 初始化栈内容为 nil
    for (size_t i = 0; i < stackSize; ++i) {
      stack[i] = Value::nil();
    }

    // 分配初始帧数组
    framesCapacity = DEFAULT_FRAMES_SIZE;
    frames = new CallFrame[framesCapacity];
    frameCount = 0;

    // 初始化帧数组（CallFrame 有默认值，但显式初始化更安全）
    for (int i = 0; i < framesCapacity; ++i) {
      frames[i] = CallFrame{};
    }

    deferStack.resize(16);
    deferTop = 0;
    error = Value::nil();
    yieldValue = Value::nil();
  }

  ~FiberObject() {
    // 释放 Value 栈
    if (stack) {
      delete[] stack;
      stack = nullptr;
    }

    // 释放帧数组
    if (frames) {
      delete[] frames;
      frames = nullptr;
    }
  }

  // === 状态查询 ===
  bool isNew() const { return state == FiberState::NEW; }

  bool isRunning() const { return state == FiberState::RUNNING; }

  bool isSuspended() const { return state == FiberState::SUSPENDED; }

  bool isDone() const { return state == FiberState::DONE; }

  bool isError() const { return state == FiberState::ERROR; }

  bool canResume() const { return state == FiberState::NEW || state == FiberState::SUSPENDED; }

  // === Value 栈扩容 ===
  void checkStack(int needed) {
    // 1. 检查剩余空间是否足够
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

    // 3. 使用 unique_ptr 确保异常安全
    std::unique_ptr<Value[]> newStackPtr(new Value[newSize]);
    Value *newStack = newStackPtr.get();

    // 4. 迁移数据
    Value *oldStack = stack;
    for (size_t i = 0; i < used; ++i) {
      newStack[i] = oldStack[i];
    }
    // 初始化新增区域为 nil
    for (size_t i = used; i < newSize; ++i) {
      newStack[i] = Value::nil();
    }

    // 5. 修复 Open UpValues (闭包引用的栈变量)
    if (openUpvalues != nullptr) {
      fixUpvaluePointers(oldStack, newStack);
    }

    // 6. 修复 CallFrames (调用帧中的指针)
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

    delete[] oldStack;

    stack = newStackPtr.release();
    stackSize = newSize;
    stackTop = stack + used;
    stackLast = stack + newSize;
  }

  void ensureStack(int needed) { checkStack(needed); }

  // === Defer 栈扩容 ===
  void ensureDefers(int needed) {
    if (deferTop + needed <= (int)deferStack.size()) {
      return;
    }
    size_t newSize = deferStack.size() * 2;
    while (newSize < static_cast<size_t>(deferTop + needed)) {
      newSize *= 2;
    }
    deferStack.resize(newSize);
  }

  // === 帧栈扩容 (手动管理) ===
  void ensureFrames(int needed) {
    int required = frameCount + needed;

    if (required <= framesCapacity) {
      return;
    }

    int newCapacity = framesCapacity;
    if (newCapacity == 0) {
      newCapacity = DEFAULT_FRAMES_SIZE;
    }
    while (newCapacity < required) {
      newCapacity *= 2;
    }

    CallFrame *newFrames = new CallFrame[newCapacity];

    for (int i = 0; i < frameCount; ++i) {
      newFrames[i] = frames[i];
    }

    for (int i = frameCount; i < newCapacity; ++i) {
      newFrames[i] = CallFrame{};
    }

    if (frames) {
      delete[] frames;
    }

    frames = newFrames;
    framesCapacity = newCapacity;
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

  // === 重置 ===
  void reset() {
    state = FiberState::NEW;
    stackTop = stack;
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