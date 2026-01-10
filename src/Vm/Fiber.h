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
  const Instruction *ip = nullptr; // 指令指针
  int slotsBase = 0;               // 栈帧基址（相对于栈底的偏移量索引）
  int expectedResults = 1;         // 期望返回值数量 (-1 表示全部)
  int returnBase = 0;              // 返回值存放位置（相对于栈底的偏移量索引）
  int deferBase = 0;               // 记录进入该帧时的 defer 栈顶位置
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

  std::vector<Value> stack;
  Value *stackTop = nullptr;

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
  static constexpr int MAX_FRAMES = 64;

  // === 构造 ===
  FiberObject() {
    type = ValueType::Fiber;
    stack.resize(DEFAULT_STACK_SIZE);
    stackTop = stack.data();
    frames.resize(DEFAULT_FRAMES_SIZE);

    deferStack.resize(16);
    deferTop = 0;

    error = Value::nil();
    yieldValue = Value::nil();
  }

  // === 状态查询 ===
  bool isNew() const { return state == FiberState::NEW; }

  bool isRunning() const { return state == FiberState::RUNNING; }

  bool isSuspended() const { return state == FiberState::SUSPENDED; }

  bool isDone() const { return state == FiberState::DONE; }

  bool isError() const { return state == FiberState::ERROR; }

  bool canResume() const { return state == FiberState::NEW || state == FiberState::SUSPENDED; }

  // === 容量管理 ===
  void ensureStack(int needed) {
    size_t used = stackTop - stack.data();
    size_t required = used + static_cast<size_t>(needed);

    if (required <= stack.size()) {
      return; // 空间足够
    }

    // 按 2 倍扩容
    size_t newSize = stack.size() * 2;
    while (newSize < required) {
      newSize *= 2;
    }

    // 记录旧基地址
    Value *oldBase = stack.data();

    // 扩容
    stack.resize(newSize);

    // 获取新基地址
    Value *newBase = stack.data();

    // 指针修复
    if (oldBase != newBase) {
      // 1. 更新 stackTop
      stackTop = newBase + (stackTop - oldBase);

      // 2. 修复 UpValue 指针（通过外部函数，因为 UpValue 是不完整类型）
      if (openUpvalues != nullptr) {
        fixUpvaluePointers(oldBase, newBase);
      }
    }
  }

  void ensureDefers(int needed) {
    if (deferTop + needed <= (int)deferStack.size()) {
      return;
    }
    size_t newSize = deferStack.size() * 2;
    while (newSize < deferTop + needed) {
      newSize *= 2;
    }
    deferStack.resize(newSize);
    // deferStack 存的是 Value (副本)，不需要像 stack 那样修复指针
  }

  void ensureFrames(int needed) {
    size_t required = static_cast<size_t>(frameCount + needed);

    if (required <= frames.size()) {
      return; // 空间足够
    }

    // 按 2 倍扩容
    size_t newSize = frames.size() * 2;
    if (newSize < DEFAULT_FRAMES_SIZE) {
      newSize = DEFAULT_FRAMES_SIZE;
    }
    while (newSize < required) {
      newSize *= 2;
    }

    frames.resize(newSize);
    // 无需指针修复，因为通过索引访问帧
  }

  void fixUpvaluePointers(Value *oldBase, Value *newBase);

  size_t stackUsed() const { return stackTop - stack.data(); }

  // === 便捷栈操作 ===
  void push(Value val) {
    ensureStack(1);
    *stackTop++ = val;
  }

  Value pop() { return *--stackTop; }

  Value peek(int distance = 0) const { return stackTop[-1 - distance]; }

  // === 辅助方法：根据 slotsBase 获取实际的 slots 指针 ===
  Value *getSlots(int slotsBase) { return stack.data() + slotsBase; }

  Value *getSlots(const CallFrame &frame) { return stack.data() + frame.slotsBase; }

  // === 重置===
  void reset() {
    state = FiberState::NEW;
    stackTop = stack.data();
    // 不清空 frames，只重置计数（保留预分配的内存）
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