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
  const Instruction *ip;   // 指令指针
  Value *slots;            // 栈帧基址
  int expectedResults = 1; // 期望返回值数量 (-1 表示全部)
};

// 执行结果
enum class InterpretResult { OK, COMPILE_ERROR, RUNTIME_ERROR };

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

  InterpretResult call(Closure *closure, int argCount, bool hasImplicitThis = false);

  // === UpValue 管理 ===
  UpValue *captureUpvalue(Value *local);
  void closeUpvalues(Value *last);

  // === 运算 ===
  void binaryOp(OpCode op);
  bool valuesEqual(Value a, Value b);

  // === 错误处理 ===
  void runtimeError(const char *format, ...);

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
  // GC
  GC gc_;

  // 回调
  ErrorHandler errorHandler_;
  PrintHandler printHandler_;
};

} // namespace spt