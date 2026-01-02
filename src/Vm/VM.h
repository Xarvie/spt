#pragma once

#include "../Common/OpCode.h"
#include "../Common/Types.h"
#include "GC.h"
#include "Module.h"
#include "Object.h"
#include "Value.h"
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace spt {

// 调用帧
struct CallFrame {
  Closure *closure;
  const Instruction *ip;     // 指令指针
  Value *slots;              // 栈帧基址
  int expectedResults = 1;   // 期望返回值数量 (-1 表示全部)
  std::vector<Value> defers; // 存储当前帧所有 defer 的闭包 (LIFO 队列)
};

// 执行结果
enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

// ============================================================================
// Protected call 上下文 - 用于 pcall 的错误恢复
// ============================================================================
struct ProtectedCallContext {
  int frameCount;        // 进入pcall时的帧数
  Value *stackTop;       // 进入pcall时的栈顶
  Value *resultSlot;     // 存放结果的槽位
  int expectedResults;   // 期望的返回值数量
  UpValue *openUpvalues; // 进入pcall时的开放upvalue链表
  bool active = false;   // 是否处于受保护调用中
};

// 虚拟机配置
struct VMConfig {
  size_t stackSize = 256 * 1024;      // 栈大小
  size_t heapSize = 64 * 1024 * 1024; // 堆大小
  bool enableGC = true;
  bool debugMode = false;
  bool enableHotReload = true;          // 启用模块热更新
  std::vector<std::string> modulePaths; // 模块搜索路径
};

// 虚拟机
class VM {
public:
  friend class GC;
  friend class ModuleManager;

  using ErrorHandler = std::function<void(const std::string &, int line)>;
  using PrintHandler = std::function<void(const std::string &)>;

  explicit VM(const VMConfig &config = {});
  ~VM();
  void ensureStack(int neededSlots);
  // === 执行 ===
  InterpretResult interpret(const CompiledChunk &chunk);
  InterpretResult call(Closure *closure, int argCount);

  // === 热更新 ===
  // 替换模块字节码,立即生效
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

  Value getLastModuleResult() const { return lastModuleResult_; }

  inline void protect(Value value) { *stackTop_++ = value; }

  inline void unprotect(int count = 1) { stackTop_ -= count; }

private:
  // === 执行循环 ===
  InterpretResult run(int minFrameCount);

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

  // 执行指定帧的所有 defer
  void invokeDefers(CallFrame *frame);

public:
  // === pcall 支持 ===
  // 用于 error() 函数抛出错误值
  void throwError(Value errorValue);

private:
  // 获取当前的调用栈信息作为字符串
  std::string getStackTrace();
  // 内部受保护调用实现
  InterpretResult protectedCall(Value callee, int argCount, Value *resultSlot, int expectedResults);

  // === 原生函数多返回值支持 ===
  // 设置原生函数的多个返回值，这些值会被 OP_CALL 正确处理
  void setNativeMultiReturn(const std::vector<Value> &values);
  void setNativeMultiReturn(std::initializer_list<Value> values);

  std::unique_ptr<ModuleManager> moduleManager_;

private:
  VMConfig config_;

  // 栈
  std::vector<Value> stack_;
  Value *stackTop_;

  // 调用帧
  std::vector<CallFrame> frames_;
  int frameCount_ = 0;

  // 全局变量
  std::unordered_map<std::string, Value> globals_;

  // 字符串驻留
  std::unordered_map<std::string, StringObject *> strings_;

  // 开放 UpValue 链表
  UpValue *openUpvalues_ = nullptr;

  // 模块缓存
  std::unordered_map<std::string, CompiledChunk> modules_;
  Value lastModuleResult_; // 用于暂存模块执行后的返回值 (__env map)

  // === pcall 错误状态 ===
  std::vector<ProtectedCallContext> pcallStack_; // 嵌套pcall的上下文栈
  bool hasError_ = false;                        // 是否有未处理的错误
  Value errorValue_;                             // 错误值 (error函数抛出的值)

  // === 原生函数多返回值支持 ===
  std::vector<Value> nativeMultiReturn_; // 原生函数的多返回值
  bool hasNativeMultiReturn_ = false;    // 是否有多返回值

  // GC
  GC gc_;

  // 回调
  ErrorHandler errorHandler_;
  PrintHandler printHandler_;
};

} // namespace spt