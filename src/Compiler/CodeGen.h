#pragma once

#include "../Common/OpCode.h"
#include "../Common/Types.h"
#include "CompilerTypes.h"
#include <string>
#include <vector>

namespace spt {

class CodeGen;

class CodeGen {
public:
  explicit CodeGen(const std::string &moduleName);
  ~CodeGen();

  // === 模块与函数管理 ===
  // 开始编译一个新函数（或主模块）
  void beginFunction(const std::string &source, const std::string &name, int numParams,
                     bool isVararg, bool useDefer, ILineGetter *lineGetter);
  // 结束当前函数编译，返回原型
  Prototype endFunction();

  // === 类管理 ===
  void beginClass(const std::string &name);
  void endClass();

  bool isInClass() const { return currentClass_ != nullptr; }

  // === 作用域管理 ===
  void beginScope();
  void endScope(); // 自动发射 OP_CLOSE_UPVALUE
  int currentScopeDepth() const;

  // === 循环管理 ===
  void beginLoop(int startPc);
  void endLoop();
  void patchBreaks();
  void patchContinues(int target);

  // === 变量与符号管理 ===
  int addLocal(const std::string &name);
  void declareLocal(const std::string &name); // 仅检查重定义
  int resolveLocal(const std::string &name);
  int resolveUpvalue(const std::string &name);

  // === 全局变量访问 ===
  // 发射从 _ENV (upvalue[0]) 读取全局变量的指令
  void emitGetTabUp(int dest, int upvalueIdx, int keyConstIdx);
  // 发射向 _ENV (upvalue[0]) 写入全局变量的指令
  void emitSetTabUp(int upvalueIdx, int keyConstIdx, int srcReg);
  // 检查当前函数是否为主函数（顶层）
  bool isMainFunction() const;

  // 获取 _ENV 的 upvalue 索引（始终为 0）
  int getEnvUpvalueIndex() const { return 0; }

  // === 常量管理 ===
  int addConstant(const ConstantValue &value);
  int addStringConstant(const std::string &s);

  // === 栈槽位管理 ===
  int allocSlot();
  int allocSlots(int n);
  void freeSlots(int n);
  void markInitialized(); // 标记当前作用域最新的变量为已初始化

  // === 状态查询 ===
  bool isMethod() const;
  bool isInitializer() const;
  int currentPc() const;

  // === 指令发射 (Low Level) ===
  void emit(Instruction inst);
  void emitABC(OpCode op, uint8_t a, uint8_t b, uint8_t c);
  void emitABx(OpCode op, uint8_t a, uint32_t bx);
  void emitAsBx(OpCode op, uint8_t a, int32_t sbx);
  void emitAx(OpCode op, uint32_t ax);

  // === 跳转控制 (High Level) ===
  int emitJump(OpCode op, int32_t offset = 0);
  void patchJump(int jumpInst);
  void patchJumpTo(int jumpInst, int target);

  // === 特殊辅助 ===
  void emitCloseUpvalue(int slot);

  ILineGetter *getLineGetter() const { return current_->lineGetter; }

  void setLineGetter(ILineGetter *lineGetter) { current_->lineGetter = lineGetter; }

  // 获取当前内部状态 (仅供特定高级操作使用，尽量少用)
  FunctionState *current() { return current_; }

  ClassState *currentClass() { return currentClass_; }

  const std::string &moduleName() const { return moduleName_; }

private:
  int addUpvalue(uint8_t index, bool isLocal, const std::string &name);
  // 为子函数设置继承自父函数的 _ENV upvalue
  void setupEnvUpvalue();

private:
  std::string moduleName_;
  FunctionState *current_ = nullptr;
  ClassState *currentClass_ = nullptr;
};

} // namespace spt