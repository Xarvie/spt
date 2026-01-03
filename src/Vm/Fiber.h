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
// CallFrame - 调用帧
// ============================================================================
struct CallFrame {
  Closure *closure = nullptr;
  const Instruction *ip = nullptr; // 指令指针
  Value *slots = nullptr;          // 栈帧基址
  int expectedResults = 1;         // 期望返回值数量 (-1 表示全部)
  std::vector<Value> defers;       // defer 闭包栈 (LIFO)
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
    frames.reserve(MAX_FRAMES);
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

  // === 栈管理 ===
  void ensureStack(size_t needed) {
    size_t used = stackTop - stack.data();
    if (used + needed > stack.size()) {
      size_t newSize = stack.size() * 2;
      while (used + needed > newSize) {
        newSize *= 2;
      }

      // 保存相对偏移
      std::vector<size_t> slotOffsets;
      for (int i = 0; i < frameCount; ++i) {
        slotOffsets.push_back(frames[i].slots - stack.data());
      }

      stack.resize(newSize);

      // 恢复指针
      stackTop = stack.data() + used;
      for (int i = 0; i < frameCount; ++i) {
        frames[i].slots = stack.data() + slotOffsets[i];
      }
    }
  }

  size_t stackUsed() const { return stackTop - stack.data(); }

  // === 便捷栈操作 ===
  void push(Value val) {
    ensureStack(1);
    *stackTop++ = val;
  }

  Value pop() { return *--stackTop; }

  Value peek(int distance = 0) const { return stackTop[-1 - distance]; }

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
