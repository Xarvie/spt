#include "CodeGen.h"
#include <cassert>
#include <filesystem>
#include <stdexcept>

namespace spt {

CodeGen::CodeGen(const std::string &moduleName) : moduleName_(moduleName) {
  current_ = nullptr;
  currentClass_ = nullptr;
}

CodeGen::~CodeGen() {

  while (current_) {
    FunctionState *enclosing = current_->enclosing;
    delete current_;
    current_ = enclosing;
  }
  while (currentClass_) {
    ClassState *enclosing = currentClass_->enclosing;
    delete currentClass_;
    currentClass_ = enclosing;
  }
}

void CodeGen::beginFunction(const std::string &source, const std::string &name, int numParams,
                            bool isVararg, ILineGetter *lineGetter) {
  auto *fs = new FunctionState();
  fs->enclosing = current_;
  fs->proto.name = name;
  fs->proto.source = source;
  fs->proto.short_src = std::filesystem::path(source).filename().string();
  fs->proto.numParams = static_cast<uint8_t>(numParams);
  fs->proto.isVararg = isVararg;
  fs->lastLine = fs->proto.lineDefined = lineGetter->getLine();
  fs->proto.absLineInfo.push_back({0, fs->lastLine});
  fs->lineGetter = lineGetter;
  fs->currentStackTop = numParams;
  fs->maxStack = numParams;

  current_ = fs;
  beginScope();
}

Prototype CodeGen::endFunction() {
  endScope();

  current_->proto.lastLineDefined = current_->lineGetter->getLine();
  current_->proto.maxStackSize = static_cast<uint8_t>(current_->maxStack);
  current_->proto.numUpvalues = static_cast<uint8_t>(current_->upvalues.size());

  for (const auto &uv : current_->upvalues) {
    current_->proto.upvalues.push_back({uv.index, uv.isLocal});
  }

  Prototype proto = std::move(current_->proto);

  FunctionState *enclosing = current_->enclosing;
  delete current_;
  current_ = enclosing;

  return proto;
}

void CodeGen::beginClass(const std::string &name) {
  auto *cs = new ClassState();
  cs->enclosing = currentClass_;
  cs->name = name;
  currentClass_ = cs;
}

void CodeGen::endClass() {
  ClassState *enclosing = currentClass_->enclosing;
  delete currentClass_;
  currentClass_ = enclosing;
}

void CodeGen::beginScope() { current_->scopeDepth++; }

void CodeGen::endScope() {
  current_->scopeDepth--;

  while (!current_->locals.empty() && current_->locals.back().scopeDepth > current_->scopeDepth) {

    if (current_->locals.back().isCaptured) {

      emitABC(OpCode::OP_CLOSE_UPVALUE, current_->locals.back().slot, 0, 0);
    }

    current_->locals.pop_back();
    freeSlots(1);
  }
}

int CodeGen::currentScopeDepth() const { return current_->scopeDepth; }

void CodeGen::beginLoop(int startPc) {
  current_->loops.push_back({startPc, current_->scopeDepth, {}, {}});
}

void CodeGen::endLoop() {
  if (!current_->loops.empty()) {
    current_->loops.pop_back();
  }
}

void CodeGen::patchBreaks() {
  if (current_->loops.empty())
    return;
  for (const auto &patch : current_->loops.back().breakJumps) {
    patchJump(patch.instructionIndex);
  }
}

void CodeGen::patchContinues(int target) {
  if (current_->loops.empty())
    return;
  for (const auto &patch : current_->loops.back().continueJumps) {
    patchJumpTo(patch.instructionIndex, target);
  }
}

void CodeGen::declareLocal(const std::string &name) {

  for (int i = static_cast<int>(current_->locals.size()) - 1; i >= 0; --i) {
    if (current_->locals[i].scopeDepth < current_->scopeDepth)
      break;
    if (current_->locals[i].name == name) {

      throw std::runtime_error("Variable '" + name + "' already declared in this scope");
    }
  }
}

int CodeGen::addLocal(const std::string &name) {
  declareLocal(name);
  int slot = allocSlot();
  current_->locals.push_back({name, slot, current_->scopeDepth, false});
  return slot;
}

int CodeGen::resolveLocal(const std::string &name) {
  for (int i = static_cast<int>(current_->locals.size()) - 1; i >= 0; --i) {
    if (current_->locals[i].name == name) {
      return current_->locals[i].slot;
    }
  }
  return -1;
}

int CodeGen::resolveUpvalue(const std::string &name) {
  if (!current_->enclosing)
    return -1;

  for (size_t i = 0; i < current_->upvalues.size(); ++i) {
    if (current_->upvalues[i].name == name) {
      return static_cast<int>(i);
    }
  }

  FunctionState *enclosing = current_->enclosing;
  for (int i = static_cast<int>(enclosing->locals.size()) - 1; i >= 0; --i) {
    if (enclosing->locals[i].name == name) {
      enclosing->locals[i].isCaptured = true;
      return addUpvalue(static_cast<uint8_t>(enclosing->locals[i].slot), true, name);
    }
  }

  FunctionState *saved = current_;
  current_ = enclosing;
  int upval = resolveUpvalue(name);
  current_ = saved;

  if (upval >= 0) {
    return addUpvalue(static_cast<uint8_t>(upval), false, name);
  }

  return -1;
}

int CodeGen::addUpvalue(uint8_t index, bool isLocal, const std::string &name) {
  for (size_t i = 0; i < current_->upvalues.size(); ++i) {
    const auto &uv = current_->upvalues[i];
    if (uv.index == index && uv.isLocal == isLocal) {
      return static_cast<int>(i);
    }
  }

  current_->upvalues.push_back({name, index, isLocal});
  return static_cast<int>(current_->upvalues.size() - 1);
}

int CodeGen::addConstant(const ConstantValue &value) {
  auto &constants = current_->proto.constants;
  for (size_t i = 0; i < constants.size(); ++i) {
    if (constants[i] == value) {
      return static_cast<int>(i);
    }
  }
  constants.push_back(value);
  return static_cast<int>(constants.size() - 1);
}

int CodeGen::addStringConstant(const std::string &s) { return addConstant(s); }

int CodeGen::allocSlot() { return current_->allocSlot(); }

int CodeGen::allocSlots(int n) { return current_->allocSlots(n); }

void CodeGen::freeSlots(int n) { current_->freeSlots(n); }

void CodeGen::markInitialized() {}

bool CodeGen::isMethod() const { return current_->isMethod; }

bool CodeGen::isInitializer() const { return current_->isInitializer; }

int CodeGen::currentPc() const { return static_cast<int>(current_->proto.code.size()); }

void CodeGen::emit(Instruction inst) {
  current_->proto.code.push_back(inst);
  if (current_->lineGetter) {
    int line = current_->lineGetter->getLine();
    int lineDiff = line - current_->lastLine;
    if (lineDiff < 0) {
      lineDiff = 0;
      line = current_->lastLine;
    }

    if (lineDiff >= LimitLineDiff || current_->absLineCount >= MaxAbsLine) {
      current_->proto.absLineInfo.back().pc = currentPc();
      current_->proto.absLineInfo.push_back({-1, line});
      lineDiff = UseAbsLine;
      current_->absLineCount = 0;
    }
    current_->proto.lineInfo.push_back(lineDiff);
    current_->lastLine = line;
    current_->absLineCount++;
  }
}

void CodeGen::emitABC(OpCode op, uint8_t a, uint8_t b, uint8_t c) {
  emit(MAKE_ABC(static_cast<uint8_t>(op), a, b, c));
}

void CodeGen::emitABx(OpCode op, uint8_t a, uint32_t bx) {
  emit(MAKE_ABx(static_cast<uint8_t>(op), a, bx));
}

void CodeGen::emitAsBx(OpCode op, uint8_t a, int32_t sbx) {
  emit(MAKE_AsBx(static_cast<uint8_t>(op), a, sbx));
}

int CodeGen::emitJump(OpCode op, int32_t offset) {
  int pc = currentPc();
  emitAsBx(op, 0, offset);
  return pc;
}

void CodeGen::patchJump(int jumpInst) {
  int offset = currentPc() - jumpInst - 1;
  Instruction oldInst = current_->proto.code[jumpInst];
  OpCode op = GET_OPCODE(oldInst);
  uint8_t a = GETARG_A(oldInst);

  current_->proto.code[jumpInst] = MAKE_AsBx(static_cast<uint8_t>(op), a, offset);
}

void CodeGen::patchJumpTo(int jumpInst, int target) {
  int offset = target - jumpInst - 1;
  Instruction oldInst = current_->proto.code[jumpInst];
  OpCode op = GET_OPCODE(oldInst);
  uint8_t a = GETARG_A(oldInst);

  current_->proto.code[jumpInst] = MAKE_AsBx(static_cast<uint8_t>(op), a, offset);
}

void CodeGen::emitCloseUpvalue(int slot) { emitABC(OpCode::OP_CLOSE_UPVALUE, slot, 0, 0); }

} // namespace spt