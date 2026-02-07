#pragma once

#include "../Common/OpCode.h"
#include "../Common/Types.h"
#include "Fiber.h"
#include "GC.h"
#include "Module.h"
#include "Object.h"
#include "Value.h"
#include "config.h"
#include "unordered_dense.h"
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define SPT_LIKELY(x) __builtin_expect(!!(x), 1)
#define SPT_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define SPT_LIKELY(x) (x)
#define SPT_UNLIKELY(x) (x)
#endif

namespace spt {

struct ClassObject;
struct NativeInstance;
struct BytesObject;
class StringPool;
struct SymbolTable;

// 执行结果状态码
enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

// ============================================================================
// Protected Call (受保护调用) 上下文 - 用于 pcall 的错误恢复
// ============================================================================
// 类似于 try-catch 的底层实现，用于在发生错误时恢复到调用前的状态
struct ProtectedCallContext {
  FiberObject *fiber;    // 发起 pcall 时的纤程
  int frameCount;        // 进入 pcall 时的调用栈深度
  Value *stackTop;       // 进入 pcall 时的栈顶位置
  UpValue *openUpvalues; // 进入 pcall 时的开放 upvalue 链表头部
  bool active = false;   // 此上下文是否处于激活状态
};

// ============================================================================
// SptPanic - 脚本运行时错误异常
// ============================================================================
// 用于替代 setjmp/longjmp 的 C++ 异常类
// 当脚本发生错误且没有 pcall 保护时抛出
class SptPanic : public std::exception {
public:
  Value errorValue; // 脚本错误值（可以是任意 spt 类型）

  explicit SptPanic(Value err) : errorValue(err) {}

  const char *what() const noexcept override { return "SptPanic: script runtime error"; }
};

// ============================================================================
// CExtensionException - C 扩展异常（保留向后兼容）
// ============================================================================
class CExtensionException : public std::exception {
public:
  explicit CExtensionException(std::string msg) : message_(std::move(msg)) {}

  const char *what() const noexcept override { return message_.c_str(); }

private:
  std::string message_;
};

// 虚拟机配置参数
struct VMConfig {
  size_t stackSize = 256 * 1024;        // 栈大小（每个纤程独立）
  size_t heapSize = 64 * 1024 * 1024;   // 堆内存上限
  bool enableGC = true;                 // 是否启用垃圾回收
  bool debugMode = false;               // 是否开启调试模式
  bool enableHotReload = true;          // 是否开启代码热更新
  std::vector<std::string> modulePaths; // 模块搜索路径
};

// ============================================================================
// VM - 虚拟机核心类
// ============================================================================
class SPT_API_CLASS VM {
public:
  friend class GC;
  friend class ModuleManager;
  friend class StringPool;
  template <typename T> friend class NativeClassBuilder;

  using ErrorHandler = std::function<void(const std::string &, int line)>;
  using PrintHandler = std::function<void(const std::string &)>;

  explicit VM(const VMConfig &config = {});
  ~VM();

  // === 执行接口 ===
  InterpretResult interpret(const CompiledChunk &chunk); // 解释执行一段字节码块
  InterpretResult call(Closure *closure, int argCount, int expectedResults); // 调用一个闭包对象

  // === 热更新 ===
  // 重新加载指定模块的代码块而不重启 VM
  bool hotReload(const std::string &moduleName, CompiledChunk newChunk);

  ModuleManager *moduleManager() { return moduleManager_.get(); }

  InterpretResult executeModule(const CompiledChunk &chunk);

  // === 全局环境管理 ===
  void defineGlobal(StringObject *name, Value value); // 定义全局变量
  Value getGlobal(StringObject *name);                // 获取全局变量
  void setGlobal(StringObject *name, Value value);    // 设置全局变量值

  // 遗留 API，使用 std::string（内部会自动进行字符串驻留）
  void defineGlobal(const std::string &name, Value value);
  Value getGlobal(const std::string &name);
  void setGlobal(const std::string &name, Value value);

  // === 原生函数注册 ===
  // 注册原生函数（无 upvalue）
  void registerNative(const std::string &name, NativeFn fn, int arity);

  // === 创建原生闭包对象（不注册到全局） ===
  Closure *createNativeClosure(NativeFn fn, int arity, int nupvalues = 0);

  // === 创建并注册带 upvalue 的原生函数 ===
  void registerNativeWithUpvalues(const std::string &name, NativeFn fn, int arity,
                                  std::initializer_list<Value> upvalues);

  // === 模块系统 ===
  void registerModule(const std::string &name, CompiledChunk chunk);
  Value importModule(const std::string &path);

  // === 回调设置 ===
  void setErrorHandler(ErrorHandler handler) { errorHandler_ = std::move(handler); }

  void setPrintHandler(PrintHandler handler) { printHandler_ = std::move(handler); }

  // === 调试与内省 ===
  void dumpStack() const;   // 打印当前栈信息
  void dumpGlobals() const; // 打印所有全局变量
  int getInfo(Value *f, const char *what, DebugInfo *out_info);
  int getStack(int f, const char *what, DebugInfo *out_info);

  // === GC 控制 ===
  GC &gc() { return gc_; }

  void collectGarbage(); // 强制执行一次垃圾回收

  // === 对象创建 (分配器) ===
  // 字符串创建：始终使用驻留池
  StringObject *allocateString(std::string_view str);
  StringObject *allocateString(const std::string &str);
  StringObject *allocateString(const char *str);

  Closure *allocateScriptClosure(const Prototype *proto);
  ClassObject *allocateClass(const std::string &name);
  Instance *allocateInstance(ClassObject *klass);
  ListObject *allocateList(int capacity);
  MapObject *allocateMap(int capacity);
  FiberObject *allocateFiber(Closure *closure);
  BytesObject *allocateBytes(size_t size = 0);

  // 原生 C++ 绑定对象创建
  NativeInstance *allocateNativeInstance(ClassObject *klass, void *data = nullptr);

  Value getLastModuleResult() const { return lastModuleResult_; }

  // === 栈保护 ===
  void protect(Value value);
  void unprotect(int count = 1);

  // === Fiber 访问 ===
  FiberObject *currentFiber() const { return currentFiber_; }

  FiberObject *mainFiber() const { return mainFiber_; }

  // === 全局环境访问 ===
  // 获取当前主脚本的 _ENV 表（C API 使用）
  MapObject *getGlobalEnv() const { return globalEnv_; }

  inline void ensureGlobalEnv() {
    if (!globalEnv_) {
      globalEnv_ = allocateMap(64);
    }
    // 将 globals_ 中预注册的全局变量迁移到环境 map 中
    if (!globals_.empty()) {
      for (const auto &[name, value] : globals_) {
        globalEnv_->set(Value::object(name), value);
      }
      globals_.clear();
    }
  }

  const SymbolTable &symbols() const { return *symbols_; }

  // === Fiber 操作 ===
  Value fiberCall(FiberObject *fiber, Value arg, bool isTry); // 调用纤程
  void fiberYield(Value value);                               // 纤程挂起/产出
  void fiberAbort(Value error);                               // 中止纤程并抛错

  // === 错误处理 ===
  void throwError(Value errorValue);

  // === 直接抛出异常（用于 C API） ===
  [[noreturn]] void throwPanic(Value errorValue);

  // =========================================================================
  // 原生函数栈操作 API (Lua 风格)
  // =========================================================================
  // 原生函数通过这些方法操作栈来传递返回值
  //
  // 使用示例:
  //   int myNativeFunc(VM *vm, Closure *self, int argc, Value *args) {
  //       vm->push(Value::integer(42));
  //       vm->push(Value::boolean(true));
  //       return 2;  // 返回 2 个值
  //   }
  // =========================================================================

  // 将值压入栈顶（用于原生函数返回值）
  void push(Value value);

  // 从栈顶弹出一个值
  Value pop();

  // 查看栈顶往下第 n 个值（0 = 栈顶）
  Value peek(int distance = 0) const;

  // 获取当前栈顶指针
  Value *top() const;

  // 设置栈顶指针（谨慎使用）
  void setTop(Value *newTop);

  // 获取栈上指定索引的值（正数从栈底，负数从栈顶）
  // 类似 Lua 的索引方式: 1 = 第一个参数, -1 = 栈顶
  Value getStackValue(int index) const;

  // 设置栈上指定索引的值
  void setStackValue(int index, Value value);

  // 确保栈有足够空间容纳 n 个额外的值
  void checkStack(int n);

  // 获取当前栈中元素数量（相对于当前帧）
  int getTop() const;

  // =========================================================================
  // 常量表预编译 - 将 ConstantValue variant 转换为 Value 数组
  // =========================================================================
  void prepareChunk(CompiledChunk &chunk);
  void preparePrototype(Prototype *proto);
  Value constantToValue(const ConstantValue &cv);

  // =========================================================================
  // 用户数据指针 - 供嵌入层（如 C API）存储关联数据
  // =========================================================================
  void setUserData(void *data) { userData_ = data; }

  void *getUserData() const { return userData_; }

  // =========================================================================
  // 内部访问 API - 供内置函数和错误处理使用
  // =========================================================================

  // 打印处理器访问
  const PrintHandler &getPrintHandler() const { return printHandler_; }

  // 错误状态访问
  bool hasError() const { return hasError_; }

  Value getErrorValue() const { return errorValue_; }

  void setHasError(bool v) { hasError_ = v; }

  void setErrorValue(Value v) { errorValue_ = v; }

  void clearError() {
    hasError_ = false;
    errorValue_ = Value::nil();
  }

  // pcall 上下文栈操作
  void pushPcallContext(const ProtectedCallContext &ctx) { pcallStack_.push_back(ctx); }

  void popPcallContext() {
    if (!pcallStack_.empty())
      pcallStack_.pop_back();
  }

  bool hasPcallContext() const { return !pcallStack_.empty(); }

  // 退出帧计数访问
  int getExitFrameCount() const { return exitFrameCount_; }

  void setExitFrameCount(int v) { exitFrameCount_ = v; }

  // 内部执行 - 从当前帧开始执行直到退出帧
  InterpretResult runInternal() { return run(); }

  // closeUpvalues 公开访问（用于 pcall 错误恢复）
  void closeUpvaluesPublic(Value *last) { closeUpvalues(last); }

  // invokeDefers 公开访问（用于 pcall 错误恢复）
  void invokeDefersPublic(int targetDeferBase) { invokeDefers(targetDeferBase); }

private:
  InterpretResult run();
  void registerBuiltinFunctions();
  void resetStack();

  // === UpValue (闭包变量) 管理 ===
  UpValue *captureUpvalue(Value *local); // 捕获一个局部变量为 UpValue
  void closeUpvalues(Value *last);       // 当变量离开作用域时关闭 UpValue

  // === 运算与比较 ===
  bool valuesEqual(Value a, Value b);

  // === 运行时错误处理 ===
  void runtimeError(const char *format, ...);
  int getLine(const Prototype *proto, size_t instruction);
  size_t getCurrentInstruction(const CallFrame &frame) const;
  std::string getStackTrace(); // 生成当前的调用栈追踪字符串

  // === Fiber 内部私有操作 ===
  void initFiberForCall(FiberObject *fiber, Value arg);
  void switchToFiber(FiberObject *fiber);
  void invokeDefers(int targetDeferBase); // 调用 defer 延迟执行语句（如果语言支持）

private:
  VMConfig config_;

  // === 纤程管理 ===
  FiberObject *mainFiber_ = nullptr;
  FiberObject *currentFiber_ = nullptr;

  // === 字符串驻留与符号表 ===
  std::unique_ptr<StringPool> stringPool_;
  std::unique_ptr<SymbolTable> symbols_;

  // === 全局变量表 - 使用 StringObject* 作为键提高性能 ===
  StringMap<Value> globals_;

  // === 全局环境缓存 - 用于 C API 稳定访问 _ENV ===
  // 存储当前主脚本的 _ENV 表，避免依赖栈帧状态
  MapObject *globalEnv_ = nullptr;

  // === 模块缓存与加载 ===
  ankerl::unordered_dense::map<std::string, CompiledChunk> modules_;
  Value lastModuleResult_;
  std::unique_ptr<ModuleManager> moduleManager_;

  // === pcall 错误处理状态栈 ===
  std::vector<ProtectedCallContext> pcallStack_;
  bool hasError_ = false;
  Value errorValue_;

  // === 模块执行控制 ===
  int exitFrameCount_ = 0;
  bool yieldPending_ = false;

  // 垃圾回收器
  GC gc_;

  // 错误与打印回调
  ErrorHandler errorHandler_;
  PrintHandler printHandler_;

  // === 用户数据指针 - 供 C API 等嵌入层使用 ===
  void *userData_ = nullptr;

  void migratePreRegisteredGlobals();

  bool inDeferExecution_ = false;
};

// ============================================================================
// 内联实现 - 栈操作 API
// ============================================================================

inline void VM::push(Value value) { currentFiber_->push(value); }

inline Value VM::pop() { return currentFiber_->pop(); }

inline Value VM::peek(int distance) const { return currentFiber_->peek(distance); }

inline Value *VM::top() const { return currentFiber_->stackTop; }

inline void VM::setTop(Value *newTop) { currentFiber_->stackTop = newTop; }

inline void VM::checkStack(int n) { currentFiber_->ensureStack(n); }

inline int VM::getTop() const {
  // 返回相对于当前帧的栈元素数量
  if (currentFiber_->frameCount == 0) {
    return static_cast<int>(currentFiber_->stackTop - currentFiber_->stack);
  }
  CallFrame &frame = currentFiber_->frames[currentFiber_->frameCount - 1];
  return static_cast<int>(currentFiber_->stackTop - frame.slots);
}

inline Value VM::getStackValue(int index) const {
  Value *ptr;
  if (index > 0) {
    // 正数索引：从当前帧底部开始（1-based，类似 Lua）
    if (currentFiber_->frameCount == 0) {
      ptr = currentFiber_->stack + (index - 1);
    } else {
      CallFrame &frame = currentFiber_->frames[currentFiber_->frameCount - 1];
      ptr = frame.slots + (index - 1);
    }
  } else if (index < 0) {
    // 负数索引：从栈顶开始（-1 = 栈顶）
    ptr = currentFiber_->stackTop + index;
  } else {
    // index == 0 无效
    return Value::nil();
  }

  // 边界检查
  if (ptr < currentFiber_->stack || ptr >= currentFiber_->stackTop) {
    return Value::nil();
  }
  return *ptr;
}

inline void VM::setStackValue(int index, Value value) {
  Value *ptr;
  if (index > 0) {
    if (currentFiber_->frameCount == 0) {
      ptr = currentFiber_->stack + (index - 1);
    } else {
      CallFrame &frame = currentFiber_->frames[currentFiber_->frameCount - 1];
      ptr = frame.slots + (index - 1);
    }
  } else if (index < 0) {
    ptr = currentFiber_->stackTop + index;
  } else {
    return; // index == 0 无效
  }

  // 边界检查
  if (ptr >= currentFiber_->stack && ptr < currentFiber_->stackTop) {
    *ptr = value;
  }
}

} // namespace spt