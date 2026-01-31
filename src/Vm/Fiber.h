#pragma once
#include "../Common/Types.h"
#include "Value.h"

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
// 注意：此结构不使用 RAII，必须显式调用 init()/destroy() 管理生命周期
// 这是为了兼容 setjmp/longjmp 错误处理机制
// ============================================================================
struct FiberObject : GCObject {
  // === 执行状态 ===
  FiberState state = FiberState::NEW;

  // === Value 栈 (手动管理) ===
  Value *stack = nullptr;     // 栈底指针 (Lua: L->stack)
  Value *stackTop = nullptr;  // 当前栈顶指针 (Lua: L->top)
  Value *stackLast = nullptr; // 栈内存的极限位置 (Lua: L->stack_last)
  size_t stackSize = 0;       // 当前栈总容量 (为了方便计算)

  // === 调用帧栈 (手动管理) ===
  CallFrame *frames = nullptr; // 帧数组指针
  int framesCapacity = 0;      // 帧数组总容量
  int frameCount = 0;          // 当前使用的帧数

  // === Defer 栈 (手动管理) ===
  Value *deferStack = nullptr; // defer 闭包数组
  int deferCapacity = 0;       // defer 数组容量
  int deferTop = 0;            // 指向 deferStack 的下一个可用位置

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
  static constexpr size_t DEFAULT_DEFER_SIZE = 16;
  static constexpr int MAX_FRAMES = 256;

  // =========================================================================
  // 生命周期管理 - 必须显式调用，不依赖构造/析构
  // =========================================================================

  // 初始化 Fiber（分配所有内部内存）
  // 调用时机：GC 分配 FiberObject 后立即调用
  static void init(FiberObject *fiber);

  // 销毁 Fiber（释放所有内部内存）
  // 调用时机：GC 释放 FiberObject 前调用
  static void destroy(FiberObject *fiber);

  // =========================================================================
  // 构造函数 - 仅设置类型标签，不分配内存
  // =========================================================================
  FiberObject() {
    type = ValueType::Fiber;
    error = Value::nil();
    yieldValue = Value::nil();
  }

  // 禁用析构函数 - 资源由 destroy() 显式释放
  // ~FiberObject() = default; // 隐式默认，不做任何事

  // === 状态查询 ===
  bool isNew() const { return state == FiberState::NEW; }

  bool isRunning() const { return state == FiberState::RUNNING; }

  bool isSuspended() const { return state == FiberState::SUSPENDED; }

  bool isDone() const { return state == FiberState::DONE; }

  bool isError() const { return state == FiberState::ERROR; }

  bool canResume() const { return state == FiberState::NEW || state == FiberState::SUSPENDED; }

#define SPT_FORCE_STACK_REALLOC 1

  void checkStack(int needed) {
#if SPT_FORCE_STACK_REALLOC
    // 强制测试模式：始终重新分配
    bool needRealloc = true;
#else
    // 正常模式：空间足够就返回
    bool needRealloc = (stackLast - stackTop < needed);
#endif

    if (!needRealloc) {
      return;
    }

    // 计算新容量
    size_t used = stackTop - stack;
    size_t required = used + needed;

    size_t newSize = stackSize;
    if (newSize == 0) {
      newSize = DEFAULT_STACK_SIZE;
    }

#if SPT_FORCE_STACK_REALLOC
    newSize += 1; // 强制增长，确保一定会重新分配内存
#endif

    while (newSize < required) {
      newSize *= 2;
    }

    // 分配新栈
    Value *newStack = new Value[newSize];
    Value *oldStack = stack;

    // 迁移数据
    for (size_t i = 0; i < used; ++i) {
      newStack[i] = oldStack[i];
    }
    // 初始化新增区域为 nil
    for (size_t i = used; i < newSize; ++i) {
      newStack[i] = Value::nil();
    }

    // 修复 Open UpValues
    if (openUpvalues != nullptr) {
      fixUpvaluePointers(oldStack, newStack);
    }

    // 修复 CallFrames
    for (int i = 0; i < frameCount; ++i) {
      CallFrame &frame = frames[i];
      if (frame.slots) {
        frame.slots = newStack + (frame.slots - oldStack);
      }
      if (frame.returnTo) {
        frame.returnTo = newStack + (frame.returnTo - oldStack);
      }
    }

    // 释放旧栈，更新指针
    delete[] oldStack;

    stack = newStack;
    stackSize = newSize;
    stackTop = stack + used;
    stackLast = stack + newSize;
  }

  void ensureStack(int needed) { checkStack(needed); }

  // === Defer 栈扩容 (手动管理) ===
  void ensureDefers(int needed) {
    int required = deferTop + needed;
    if (required <= deferCapacity) {
      return;
    }

    int newCapacity = deferCapacity;
    if (newCapacity == 0) {
      newCapacity = DEFAULT_DEFER_SIZE;
    }
    while (newCapacity < required) {
      newCapacity *= 2;
    }

    Value *newDefers = new Value[newCapacity];

    // 迁移现有数据
    for (int i = 0; i < deferTop; ++i) {
      newDefers[i] = deferStack[i];
    }

    // 初始化新增区域为 nil
    for (int i = deferTop; i < newCapacity; ++i) {
      newDefers[i] = Value::nil();
    }

    // 释放旧数组
    if (deferStack) {
      delete[] deferStack;
    }

    deferStack = newDefers;
    deferCapacity = newCapacity;
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

  // === 重置（保留已分配的内存） ===
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

  // === 内存大小计算（供 GC 统计使用） ===
  size_t totalAllocatedBytes() const {
    return (stackSize * sizeof(Value)) + (framesCapacity * sizeof(CallFrame)) +
           (deferCapacity * sizeof(Value));
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