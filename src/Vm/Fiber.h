#pragma once
#include "../Common/Types.h"
#include "Value.h"
#include <deque>
#include <vector>

namespace spt {

// 前置声明
class VM;
struct Closure;
struct UpValue;

// ============================================================================
// CallFrame - 调用帧
// ============================================================================
struct CallFrame {
  Closure *closure = nullptr;
  const Instruction *ip = nullptr; // 指令指针
  int slotsBase = 0;               // 栈帧基址（相对于栈底的偏移量索引）
  int expectedResults = 1;         // 期望返回值数量 (-1 表示全部)
  int returnBase;
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

  // 使用 std::deque 替代 std::vector，扩容时不会移动旧元素
  // 保证 CallFrame 地址的稳定性
  std::deque<CallFrame> frames;
  int frameCount = 0;

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
  static constexpr size_t DEFAULT_STACK_SIZE = 1024;
  static constexpr int MAX_FRAMES = 64;

  // === 构造 ===
  FiberObject() {
    type = ValueType::Fiber;
    stack.resize(DEFAULT_STACK_SIZE);
    stackTop = stack.data();
    // deque 不需要 reserve
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

  void ensureStack(size_t needed) {
    size_t used = stackTop - stack.data();
    if (used + needed > stack.size()) {
      size_t newSize = stack.size() * 2;
      while (used + needed > newSize) {
        newSize *= 2;
      }

      // 保存旧栈基地址
      Value *oldStackBase = stack.data();

      // 扩容栈
      stack.resize(newSize);

      // 计算新旧地址差值
      Value *newStackBase = stack.data();
      ptrdiff_t offset = newStackBase - oldStackBase;

      // 恢复 stackTop 指针
      stackTop = newStackBase + used;

      // 由于 UpValue 是不完整类型，调用外部函数处理
      if (openUpvalues != nullptr && offset != 0) {
        fixUpvaluePointers(oldStackBase, used, offset);
      }
    }
  }

  void fixUpvaluePointers(Value *oldStackBase, size_t used, ptrdiff_t offset);

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
    frames.clear();
    frameCount = 0;
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
