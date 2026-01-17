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

struct NativeClassObject;
struct NativeInstance;
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
  InterpretResult call(Closure *closure, int argCount);  // 调用一个闭包对象

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
  // 将 C++ 函数注册到脚本环境中
  void registerNative(const std::string &name, NativeFn fn, int arity, uint8_t flags = FUNC_NONE);

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

  Closure *allocateClosure(const Prototype *proto);
  ClassObject *allocateClass(const std::string &name);
  Instance *allocateInstance(ClassObject *klass);
  ListObject *allocateList(int capacity);
  MapObject *allocateMap(int capacity);
  FiberObject *allocateFiber(Closure *closure);

  // 原生 C++ 绑定对象创建
  NativeClassObject *allocateNativeClass(const std::string &name);
  NativeInstance *allocateNativeInstance(NativeClassObject *nativeClass);
  NativeInstance *createNativeInstance(NativeClassObject *nativeClass, int argc, Value *argv);

  Value getLastModuleResult() const { return lastModuleResult_; }

  // === 栈保护 ===
  void protect(Value value);
  void unprotect(int count = 1);

  // === Fiber 访问 ===
  FiberObject *currentFiber() const { return currentFiber_; }

  FiberObject *mainFiber() const { return mainFiber_; }

  const SymbolTable &symbols() const { return *symbols_; }

  // === Fiber 操作 ===
  Value fiberCall(FiberObject *fiber, Value arg, bool isTry); // 调用纤程
  void fiberYield(Value value);                               // 纤程挂起/产出
  void fiberAbort(Value error);                               // 中止纤程并抛错

  // === 错误处理 ===
  void throwError(Value errorValue);
  void setNativeMultiReturn(std::initializer_list<Value> values);

  // =========================================================================
  // 常量表预编译 - 将 ConstantValue variant 转换为 Value 数组
  // =========================================================================
  void prepareChunk(CompiledChunk &chunk);
  void preparePrototype(Prototype *proto);
  Value constantToValue(const ConstantValue &cv);

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

  // === 原生函数多返回值支持 ===
  void setNativeMultiReturn(const std::vector<Value> &values);

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

  // === 原生函数多返回值暂存 ===
  std::vector<Value> nativeMultiReturn_;
  bool hasNativeMultiReturn_ = false;

  // 垃圾回收器
  GC gc_;

  // 错误与打印回调
  ErrorHandler errorHandler_;
  PrintHandler printHandler_;
};

} // namespace spt
