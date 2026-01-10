#pragma once

#include "../Common/OpCode.h"
#include "../Common/Types.h"
#include "Fiber.h" // Fiber 定义了 CallFrame
#include "GC.h"
#include "Module.h"
#include "Object.h"
#include "Value.h"
#include "config.h"
#include "unordered_dense.h"
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace spt {

struct NativeClassObject;
struct NativeInstance;

// 执行结果
enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

// ============================================================================
// Protected call 上下文 - 用于 pcall 的错误恢复
// ============================================================================
struct ProtectedCallContext {
  FiberObject *fiber;    // pcall 时的 fiber
  int frameCount;        // 进入 pcall 时的帧数
  Value *stackTop;       // 进入 pcall 时的栈顶
  UpValue *openUpvalues; // 进入 pcall 时的开放 upvalue 链表
  bool active = false;
};

// 虚拟机配置
struct VMConfig {
  size_t stackSize = 256 * 1024;      // 栈大小（每个 fiber）
  size_t heapSize = 64 * 1024 * 1024; // 堆大小
  bool enableGC = true;
  bool debugMode = false;
  bool enableHotReload = true;
  std::vector<std::string> modulePaths;
};

// ============================================================================
// 虚拟机
// ============================================================================
class SPT_API_CLASS VM {
public:
  friend class GC;
  friend class ModuleManager;
  template <typename T> friend class NativeClassBuilder;

  using ErrorHandler = std::function<void(const std::string &, int line)>;
  using PrintHandler = std::function<void(const std::string &)>;

  explicit VM(const VMConfig &config = {});
  ~VM();

  // === 执行 ===
  InterpretResult interpret(const CompiledChunk &chunk);
  InterpretResult call(Closure *closure, int argCount);

  // === 热更新 ===
  bool hotReload(const std::string &moduleName, const CompiledChunk &newChunk);

  ModuleManager *moduleManager() { return moduleManager_.get(); }

  InterpretResult executeModule(const CompiledChunk &chunk);

  // === 全局环境 ===
  void defineGlobal(const std::string &name, Value value);
  Value getGlobal(const std::string &name);
  void setGlobal(const std::string &name, Value value);

  // === 原生函数注册 ===
  void registerNative(const std::string &name, NativeFn fn, int arity, uint8_t flags = FUNC_NONE);

  // === 模块系统 ===
  void registerModule(const std::string &name, const CompiledChunk &chunk);
  Value importModule(const std::string &path);

  // === 回调设置 ===
  void setErrorHandler(ErrorHandler handler) { errorHandler_ = std::move(handler); }

  void setPrintHandler(PrintHandler handler) { printHandler_ = std::move(handler); }

  // === 调试 ===
  void dumpStack() const;
  void dumpGlobals() const;
  int getInfo(Value *f, const char *what, DebugInfo *out_info);
  int getStack(int f, const char *what, DebugInfo *out_info);

  // === GC 控制 ===
  GC &gc() { return gc_; }

  void collectGarbage();

  // === 对象创建 ===
  StringObject *allocateString(const std::string &str);
  Closure *allocateClosure(const Prototype *proto);
  ClassObject *allocateClass(const std::string &name);
  Instance *allocateInstance(ClassObject *klass);
  ListObject *allocateList(int capacity);
  MapObject *allocateMap(int capacity);
  FiberObject *allocateFiber(Closure *closure);

  NativeClassObject *allocateNativeClass(const std::string &name);
  NativeInstance *allocateNativeInstance(NativeClassObject *nativeClass);

  NativeInstance *createNativeInstance(NativeClassObject *nativeClass, int argc, Value *argv);

  Value getLastModuleResult() const { return lastModuleResult_; }

  // === 栈保护（通过当前 fiber）===
  void protect(Value value);
  void unprotect(int count = 1);

  // === Fiber 访问 ===
  FiberObject *currentFiber() const { return currentFiber_; }

  FiberObject *mainFiber() const { return mainFiber_; }

  // === Fiber 操作（供原生函数使用）===
  // 调用 fiber，返回 yield 或 return 的值
  Value fiberCall(FiberObject *fiber, Value arg, bool isTry);
  // 从当前 fiber yield
  void fiberYield(Value value);
  // 中止当前 fiber
  void fiberAbort(Value error);

  // === 错误处理（公开供 Fiber 使用）===
  void throwError(Value errorValue);

private:
  // === 核心执行循环 ===
  InterpretResult run();

  // === 内置函数注册 ===
  void registerBuiltinFunctions();

  void resetStack();

  // === UpValue 管理 ===
  UpValue *captureUpvalue(Value *local);
  void closeUpvalues(Value *last);

  // === 运算 ===
  bool valuesEqual(Value a, Value b);

  // === 错误处理 ===
  void runtimeError(const char *format, ...);
  int getLine(const Prototype *proto, size_t instruction);
  std::string getStackTrace();

  // === 原生函数多返回值支持 ===
  void setNativeMultiReturn(const std::vector<Value> &values);
  void setNativeMultiReturn(std::initializer_list<Value> values);

  // === Fiber 内部操作 ===
  // 初始化 fiber 准备首次运行
  void initFiberForCall(FiberObject *fiber, Value arg);
  // 切换到指定 fiber
  void switchToFiber(FiberObject *fiber);

  void invokeDefers(CallFrame *frame);

private:
  VMConfig config_;

  // === Fiber 相关（核心变更）===
  FiberObject *mainFiber_ = nullptr;    // 主 fiber（VM 启动时创建）
  FiberObject *currentFiber_ = nullptr; // 当前执行的 fiber

  // === 全局变量 ===
  ankerl::unordered_dense::map<std::string, Value> globals_;

  // === 字符串驻留 ===
  ankerl::unordered_dense::map<std::string, StringObject *> strings_;

  // === 模块缓存 ===
  ankerl::unordered_dense::map<std::string, CompiledChunk> modules_;
  Value lastModuleResult_;

  std::unique_ptr<ModuleManager> moduleManager_;

  // === pcall 错误状态 ===
  std::vector<ProtectedCallContext> pcallStack_;
  bool hasError_ = false;
  Value errorValue_;

  // === 模块执行控制 ===
  int exitFrameCount_ = 0;    // run() 应该在 frameCount 降到此值时返回
  bool yieldPending_ = false; // yield 后需要退出 run()

  // === 原生函数多返回值支持 ===
  std::vector<Value> nativeMultiReturn_;
  bool hasNativeMultiReturn_ = false;

  // GC
  GC gc_;

  // 回调
  ErrorHandler errorHandler_;
  PrintHandler printHandler_;
};

} // namespace spt
