#include "VM.h"
#include "Bytes.h"
#include "Fiber.h"
#include "SptDebug.hpp"
#include "SptStdlibs.h"
#include "StringPool.h"
#include "VMDispatch.h"
#include <algorithm>
#include <cerrno>
#include <climits>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>

namespace spt {

VM::VM(const VMConfig &config) : config_(config), gc_(this, {}) {
  globals_.reserve(256);

  stringPool_ = std::make_unique<StringPool>(&gc_);
  gc_.setStringPool(stringPool_.get());

  symbols_ = std::make_unique<SymbolTable>();
  symbols_->initialize(*stringPool_);
  symbols_->registerBuiltinMethods();

  mainFiber_ = gc_.allocateFiber();
  if (config.stackSize > FiberObject::DEFAULT_STACK_SIZE) {
    mainFiber_->checkStack(config.stackSize);
  }
  mainFiber_->stackTop = mainFiber_->stack;
  currentFiber_ = mainFiber_;

  ModuleManagerConfig moduleConfig;
  moduleConfig.enableCache = true;
  moduleConfig.enableHotReload = config.enableHotReload;
  moduleManager_ = std::make_unique<ModuleManager>(this, moduleConfig);
  if (!config.modulePaths.empty()) {
    FileSystemLoader *loader = FileSystemLoader_create(config.modulePaths);
    moduleManager_->setLoader(FileSystemLoader_toLoader(loader));
  }

  registerBuiltinFunctions();
  SptDebug::load(this);
  SptFiber::load(this);
  SptBytes::load(this);
}

VM::~VM() {
  currentFiber_ = nullptr;
  mainFiber_ = nullptr;

  for (auto &[name, chunk] : modules_) {
    chunk.destroyRuntimeData();
  }

  globals_.clear();
  modules_.clear();

  pcallStack_.clear();
  hasError_ = false;
  errorValue_ = Value::nil();
}

void VM::protect(Value value) { currentFiber_->push(value); }

void VM::unprotect(int count) { currentFiber_->stackTop -= count; }

void VM::migratePreRegisteredGlobals() {

  if (globals_.empty()) {
    return;
  }

  if (!mainFiber_ || mainFiber_->frameCount == 0) {
    return;
  }

  CallFrame *frame = &mainFiber_->frames[0];
  Closure *closure = frame->closure;

  if (!closure || closure->upvalueCount == 0) {
    return;
  }

  UpValue *envUpvalue = closure->scriptUpvalues[0];
  if (!envUpvalue) {
    return;
  }

  Value envValue = *envUpvalue->location;
  if (!envValue.isMap()) {
    return;
  }

  MapObject *envMap = static_cast<MapObject *>(envValue.asGC());

  for (const auto &[name, value] : globals_) {
    envMap->set(Value::object(name), value);
  }

  globals_.clear();
}

InterpretResult VM::interpret(const CompiledChunk &chunk) {
  prepareChunk(const_cast<CompiledChunk &>(chunk));

  mainFiber_->reset();
  currentFiber_ = mainFiber_;

  globalEnv_ = nullptr;

  Closure *mainClosure = allocateScriptClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

  MapObject *globalEnv = nullptr;
  if (mainClosure->upvalueCount > 0) {

    globalEnv = allocateMap(64);

    globalEnv_ = globalEnv;

    protect(Value::object(globalEnv));

    UpValue *envUpvalue = gc_.allocate<UpValue>();
    envUpvalue->closed = Value::object(globalEnv);
    envUpvalue->location = &envUpvalue->closed;
    envUpvalue->nextOpen = nullptr;

    mainClosure->scriptUpvalues[0] = envUpvalue;

    lastModuleResult_ = Value::object(globalEnv);

    unprotect(1);
  }

  currentFiber_->ensureStack(mainClosure->proto->maxStackSize);
  currentFiber_->ensureFrames(1);

  int slotsBaseOffset = static_cast<int>(currentFiber_->stackTop - currentFiber_->stack);

  for (int i = 0; i < mainClosure->proto->maxStackSize; ++i) {
    currentFiber_->stackTop[i] = Value::nil();
  }
  currentFiber_->stackTop += mainClosure->proto->maxStackSize;

  CallFrame *frame = &currentFiber_->frames[currentFiber_->frameCount++];
  frame->closure = mainClosure;
  frame->ip = mainClosure->proto->code.data();
  frame->slots = currentFiber_->stack + slotsBaseOffset;
  frame->expectedResults = 1;
  frame->returnTo = nullptr;
  frame->deferBase = currentFiber_->deferTop;

  migratePreRegisteredGlobals();

  return run();
}

InterpretResult VM::call(Closure *closure, int argCount, int expectedResults) {
  FiberObject *fiber = currentFiber_;

  Value *argsStart = fiber->stackTop - argCount;

  int funcSlotOffset = static_cast<int>((argsStart - 1) - fiber->stack);
  int argsStartOffset = static_cast<int>(argsStart - fiber->stack);

  if (closure->isNative()) {

    if (closure->arity != -1 && argCount != closure->arity) {
      runtimeError("Native function '%s' expects %d arguments, got %d", closure->getName(),
                   closure->arity, argCount);
      return InterpretResult::RUNTIME_ERROR;
    }

    int numResults = 0;
    try {

      argsStart = fiber->stack + argsStartOffset;
      numResults = closure->function(this, closure, argCount, argsStart);
    } catch (const SptPanic &e) {

      return InterpretResult::RUNTIME_ERROR;
    } catch (const CExtensionException &e) {
      if (!hasError_) {
        runtimeError("%s", e.what());
      }
      return InterpretResult::RUNTIME_ERROR;
    }

    Value *funcSlot = fiber->stack + funcSlotOffset;

    if (numResults > 0) {
      Value *resultsStart = fiber->stackTop - numResults;
      if (funcSlot != resultsStart) {
        for (int i = 0; i < numResults; ++i) {
          funcSlot[i] = resultsStart[i];
        }
      }
    }

    if (expectedResults == -1) {
      fiber->stackTop = funcSlot + numResults;
    } else {
      for (int i = numResults; i < expectedResults; ++i) {
        funcSlot[i] = Value::nil();
      }
      fiber->stackTop = funcSlot + expectedResults;
    }

    return InterpretResult::OK;
  }

  if (!closure->proto->isVararg && argCount != closure->proto->numParams) {
    runtimeError("Function '%s' expects %d arguments, got %d", closure->proto->name.c_str(),
                 closure->proto->numParams, argCount);
    return InterpretResult::RUNTIME_ERROR;
  }

  if (fiber->frameCount >= FiberObject::MAX_FRAMES) {
    runtimeError("Stack overflow");
    return InterpretResult::RUNTIME_ERROR;
  }

  int slotsBaseOffset = static_cast<int>(argsStart - fiber->stack);
  int scriptFuncSlotOffset = slotsBaseOffset - 1;

  fiber->ensureStack(closure->proto->maxStackSize);
  fiber->ensureFrames(1);

  argsStart = fiber->stack + slotsBaseOffset;
  Value *funcSlot = fiber->stack + scriptFuncSlotOffset;

  Value *frameEnd = argsStart + closure->proto->maxStackSize;
  int numParams = closure->proto->numParams;
  Value *slot = argsStart + argCount;
  for (int i = argCount; i < numParams; ++i) {
    *slot++ = Value::nil();
  }
  fiber->stackTop = frameEnd;

  CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
  newFrame->closure = closure;
  newFrame->ip = closure->proto->code.data();
  newFrame->slots = argsStart;
  newFrame->deferBase = fiber->deferTop;
  newFrame->returnTo = funcSlot;
  newFrame->expectedResults = expectedResults;

  int savedExitFrameCount = exitFrameCount_;
  exitFrameCount_ = fiber->frameCount;

  InterpretResult result = run();

  exitFrameCount_ = savedExitFrameCount;

  return result;
}

InterpretResult VM::executeModule(const CompiledChunk &chunk) {
  prepareChunk(const_cast<CompiledChunk &>(chunk));

  Closure *mainClosure = allocateScriptClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

  if (mainClosure->upvalueCount > 0) {

    MapObject *globalEnv = allocateMap(64);

    globalEnv_ = globalEnv;

    protect(Value::object(globalEnv));

    UpValue *envUpvalue = gc_.allocate<UpValue>();
    envUpvalue->closed = Value::object(globalEnv);
    envUpvalue->location = &envUpvalue->closed;
    envUpvalue->nextOpen = nullptr;

    mainClosure->scriptUpvalues[0] = envUpvalue;

    lastModuleResult_ = Value::object(globalEnv);

    unprotect(1);
  }

  currentFiber_->ensureStack(mainClosure->proto->maxStackSize);
  currentFiber_->ensureFrames(1);

  int slotsBaseOffset = static_cast<int>(currentFiber_->stackTop - currentFiber_->stack);

  for (int i = 0; i < mainClosure->proto->maxStackSize; ++i) {
    currentFiber_->push(Value::nil());
  }

  CallFrame *frame = &currentFiber_->frames[currentFiber_->frameCount++];
  frame->closure = mainClosure;
  frame->ip = mainClosure->proto->code.data();
  frame->slots = currentFiber_->stack + slotsBaseOffset;
  frame->expectedResults = 1;
  frame->returnTo = nullptr;
  frame->deferBase = currentFiber_->deferTop;

  exitFrameCount_ = currentFiber_->frameCount;

  migratePreRegisteredGlobals();

  InterpretResult result = run();

  exitFrameCount_ = 0;
  return result;
}

FiberObject *VM::allocateFiber(Closure *closure) {
  FiberObject *fiber = gc_.allocateFiber();
  fiber->closure = closure;
  fiber->state = FiberState::NEW;
  return fiber;
}

BytesObject *VM::allocateBytes(size_t size) { return gc_.allocateBytes(size); }

void VM::initFiberForCall(FiberObject *fiber, Value arg) {
  fiber->stackTop = fiber->stack;
  fiber->frameCount = 0;
  fiber->deferTop = 0;
  fiber->openUpvalues = nullptr;

  Closure *closure = fiber->closure;

  if (closure->isNative()) {
    fiber->ensureStack(16);
    fiber->ensureFrames(1);

    Value *argsStart = fiber->stackTop;
    int argCount = arg.isNil() ? 0 : 1;
    if (argCount > 0) {
      *fiber->stackTop++ = arg;
    }

    fiber->state = FiberState::RUNNING;

    int numResults = 0;
    try {
      numResults = closure->function(this, closure, argCount, argsStart);
    } catch (const SptPanic &e) {
      fiber->state = FiberState::ERROR;
      fiber->error = e.errorValue;
      fiber->hasError = true;
      return;
    } catch (const CExtensionException &e) {
      fiber->state = FiberState::ERROR;
      fiber->error = Value::object(allocateString(e.what()));
      fiber->hasError = true;
      return;
    }

    if (numResults > 0) {
      fiber->yieldValue = fiber->stackTop[-1];
    } else {
      fiber->yieldValue = Value::nil();
    }

    fiber->state = FiberState::DONE;
    return;
  }

  const Prototype *proto = closure->proto;
  fiber->ensureStack(proto->maxStackSize);
  fiber->ensureFrames(1);

  *fiber->stackTop++ = arg;

  CallFrame *frame = &fiber->frames[fiber->frameCount++];
  frame->closure = closure;
  frame->ip = proto->code.data();
  frame->slots = fiber->stack;
  frame->expectedResults = 1;
  frame->returnTo = nullptr;
  frame->deferBase = fiber->deferTop;

  Value *frameStart = fiber->stack;
  Value *frameEnd = frameStart + proto->maxStackSize;
  for (Value *p = fiber->stackTop; p < frameEnd; ++p) {
    *p = Value::nil();
  }
  fiber->stackTop = frameEnd;

  fiber->state = FiberState::RUNNING;
}

Value VM::fiberCall(FiberObject *fiber, Value arg, bool isTry) {
  if (!fiber) {
    throwError(Value::object(allocateString("Cannot call nil fiber")));
    return Value::nil();
  }

  if (!fiber->canResume()) {
    const char *state = "unknown";
    switch (fiber->state) {
    case FiberState::RUNNING:
      state = "running";
      break;
    case FiberState::DONE:
      state = "finished";
      break;
    case FiberState::ERROR:
      state = "aborted";
      break;
    default:
      break;
    }

    if (isTry) {
      fiber->state = FiberState::ERROR;
      fiber->error =
          Value::object(allocateString(std::string("Cannot call fiber that is ") + state));
      fiber->hasError = true;
      return Value::nil();
    }

    throwError(Value::object(allocateString(std::string("Cannot call fiber that is ") + state)));
    return Value::nil();
  }

  FiberObject *caller = currentFiber_;
  fiber->caller = caller;

  if (fiber->isNew()) {
    initFiberForCall(fiber, arg);

    if (fiber->state == FiberState::DONE || fiber->state == FiberState::ERROR) {
      Value returnValue = Value::nil();
      if (fiber->state == FiberState::DONE) {
        returnValue = fiber->yieldValue;
      } else if (fiber->hasError && !isTry) {
        throwError(fiber->error);
        return Value::nil();
      }
      return returnValue;
    }
  } else {
    fiber->state = FiberState::RUNNING;

    CallFrame *frame = &fiber->frames[fiber->frameCount - 1];

    const uint32_t *prevIP1 = frame->ip - 1;
    uint32_t inst1 = *prevIP1;
    OpCode op1 = GET_OPCODE(inst1);

    bool isWideInvoke = false;
    uint32_t inst2 = 0;

    if (frame->ip >= frame->closure->proto->code.data() + 2) {
      const uint32_t *prevIP2 = frame->ip - 2;
      inst2 = *prevIP2;
      if (GET_OPCODE(inst2) == OpCode::OP_INVOKE) {
        isWideInvoke = true;
      }
    }

    if (op1 == OpCode::OP_CALL) {
      uint8_t A = GETARG_A(inst1);
      Value *frameSlots = frame->slots;
      frameSlots[A] = arg;
    } else if (isWideInvoke) {
      uint8_t A = GETARG_A(inst2);
      Value *frameSlots = frame->slots;
      frameSlots[A] = arg;
    } else {
      runtimeError("Critical VM Error: Cannot resume fiber. Unknown yield origin opcode at ip-%d",
                   (int)(frame->ip - frame->closure->proto->code.data()));
    }
  }

  currentFiber_ = fiber;

  if (caller->state == FiberState::RUNNING) {
    caller->state = FiberState::SUSPENDED;
  }

  int savedExitFrameCount = exitFrameCount_;
  exitFrameCount_ = fiber->frameCount;

  InterpretResult result = run();

  exitFrameCount_ = savedExitFrameCount;
  yieldPending_ = false;

  Value returnValue = Value::nil();

  if (fiber->state == FiberState::DONE || fiber->state == FiberState::SUSPENDED) {
    returnValue = fiber->yieldValue;
  } else if (fiber->state == FiberState::ERROR) {
    if (fiber->hasError && !isTry) {
      throwError(fiber->error);
    }
    return Value::nil();
  }

  caller->state = FiberState::RUNNING;
  currentFiber_ = caller;

  return returnValue;
}

void VM::invokeDefers(int targetDeferBase) {
  FiberObject *fiber = currentFiber_;

  bool inDefer = inDeferExecution_;
  if (inDefer) {
    runtimeError("Cannot use defer inside defer execution");
    return;
  }
  inDeferExecution_ = true;
  while (fiber->deferTop > targetDeferBase) {
    Value deferVal = fiber->deferStack[--fiber->deferTop];

    if (deferVal.isClosure()) {
      Closure *closure = static_cast<Closure *>(deferVal.asGC());
      protect(Value::object(closure));
      call(closure, 0, 1);
      unprotect(1);
    }

    if (hasError_) {
      inDeferExecution_ = false;
      return;
    }
  }
  inDeferExecution_ = false;
}

void VM::fiberYield(Value value) {
  FiberObject *fiber = currentFiber_;

  if (fiber == mainFiber_) {
    throwError(Value::object(allocateString("Cannot yield from main fiber")));
    return;
  }

  FiberObject *caller = fiber->caller;
  if (!caller) {
    throwError(Value::object(allocateString("Fiber has no caller to yield to")));
    return;
  }

  fiber->state = FiberState::SUSPENDED;
  fiber->yieldValue = value;

  caller->state = FiberState::RUNNING;

  currentFiber_ = caller;

  yieldPending_ = true;
}

void VM::fiberAbort(Value error) {
  FiberObject *fiber = currentFiber_;
  fiber->state = FiberState::ERROR;
  fiber->error = error;
  fiber->hasError = true;

  FiberObject *caller = fiber->caller;
  if (caller) {
    currentFiber_ = caller;
    caller->state = FiberState::RUNNING;
    yieldPending_ = true;
  } else {
    throwError(error);
  }
}

UpValue *VM::captureUpvalue(Value *local) {
  FiberObject *fiber = currentFiber_;
  ptrdiff_t localOffset = local - fiber->stack;
  UpValue *prevUpvalue = nullptr;
  UpValue *upvalue = fiber->openUpvalues;

  while (upvalue != nullptr && (upvalue->location - fiber->stack) > localOffset) {
    prevUpvalue = upvalue;
    upvalue = upvalue->nextOpen;
  }

  if (upvalue != nullptr && (upvalue->location - fiber->stack) == localOffset) {
    return upvalue;
  }

  UpValue *createdUpvalue = gc_.allocate<UpValue>();
  local = fiber->stack + localOffset;
  createdUpvalue->location = local;
  createdUpvalue->nextOpen = upvalue;

  if (prevUpvalue == nullptr) {
    fiber->openUpvalues = createdUpvalue;
  } else {
    prevUpvalue->nextOpen = createdUpvalue;
  }

  return createdUpvalue;
}

void VM::closeUpvalues(Value *last) {
  FiberObject *fiber = currentFiber_;

  while (fiber->openUpvalues != nullptr && fiber->openUpvalues->location >= last) {
    UpValue *upvalue = fiber->openUpvalues;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    fiber->openUpvalues = upvalue->nextOpen;
  }
}

void VM::defineGlobal(StringObject *name, Value value) {

  globals_[name] = value;

  if (globalEnv_) {
    globalEnv_->set(Value::object(name), value);
    return;
  }

  if (mainFiber_ && mainFiber_->frameCount > 0) {
    CallFrame *frame = &mainFiber_->frames[0];
    Closure *closure = frame->closure;
    if (closure && closure->upvalueCount > 0 && closure->scriptUpvalues[0]) {
      Value envValue = *closure->scriptUpvalues[0]->location;
      if (envValue.isMap()) {
        MapObject *envMap = static_cast<MapObject *>(envValue.asGC());
        envMap->set(Value::object(name), value);
      }
    }
  }
}

void VM::defineGlobal(const std::string &name, Value value) {
  StringObject *nameStr = allocateString(name);
  defineGlobal(nameStr, value);
}

Value VM::getGlobal(StringObject *name) {

  if (globalEnv_) {
    Value result = globalEnv_->get(Value::object(name));
    if (!result.isNil()) {
      return result;
    }
  }

  if (mainFiber_ && mainFiber_->frameCount > 0) {
    CallFrame *frame = &mainFiber_->frames[0];
    Closure *closure = frame->closure;
    if (closure && closure->upvalueCount > 0 && closure->scriptUpvalues[0]) {
      Value envValue = *closure->scriptUpvalues[0]->location;
      if (envValue.isMap()) {
        MapObject *envMap = static_cast<MapObject *>(envValue.asGC());
        Value result = envMap->get(Value::object(name));
        if (!result.isNil()) {
          return result;
        }
      }
    }
  }

  auto it = globals_.find(name);
  if (it != globals_.end()) {
    return it->second;
  }

  return Value::nil();
}

Value VM::getGlobal(const std::string &name) {
  StringObject *nameStr = stringPool_->find(name);

  if (globalEnv_) {
    if (!nameStr) {
      nameStr = allocateString(name);
    }
    Value result = globalEnv_->get(Value::object(nameStr));
    if (!result.isNil()) {
      return result;
    }
  }

  if (mainFiber_ && mainFiber_->frameCount > 0) {
    CallFrame *frame = &mainFiber_->frames[0];
    Closure *closure = frame->closure;
    if (closure && closure->upvalueCount > 0 && closure->scriptUpvalues[0]) {
      Value envValue = *closure->scriptUpvalues[0]->location;
      if (envValue.isMap()) {
        MapObject *envMap = static_cast<MapObject *>(envValue.asGC());
        if (!nameStr) {
          nameStr = allocateString(name);
        }
        Value result = envMap->get(Value::object(nameStr));
        if (!result.isNil()) {
          return result;
        }
      }
    }
  }

  if (!nameStr) {
    nameStr = allocateString(name);
  }
  auto it = globals_.find(nameStr);
  if (it != globals_.end()) {
    return it->second;
  }

  return Value::nil();
}

void VM::setGlobal(StringObject *name, Value value) {

  globals_[name] = value;

  if (globalEnv_) {
    globalEnv_->set(Value::object(name), value);
    return;
  }

  if (mainFiber_ && mainFiber_->frameCount > 0) {
    CallFrame *frame = &mainFiber_->frames[0];
    Closure *closure = frame->closure;
    if (closure && closure->upvalueCount > 0 && closure->scriptUpvalues[0]) {
      Value envValue = *closure->scriptUpvalues[0]->location;
      if (envValue.isMap()) {
        MapObject *envMap = static_cast<MapObject *>(envValue.asGC());
        envMap->set(Value::object(name), value);
      }
    }
  }
}

void VM::setGlobal(const std::string &name, Value value) {
  StringObject *nameStr = allocateString(name);
  setGlobal(nameStr, value);
}

void VM::registerNative(const std::string &name, NativeFn fn, int arity) {
  Closure *native = gc_.allocateNativeClosure(0);
  protect(Value::object(native));
  native->function = fn;
  native->arity = arity;
  native->name = allocateString(name);
  native->receiver = Value::nil();
  defineGlobal(name, Value::object(native));
  unprotect(1);
}

Closure *VM::createNativeClosure(NativeFn fn, int arity, int nupvalues) {
  Closure *native = gc_.allocateNativeClosure(nupvalues);
  native->function = fn;
  native->arity = arity;
  native->receiver = Value::nil();
  return native;
}

void VM::registerNativeWithUpvalues(const std::string &name, NativeFn fn, int arity,
                                    std::initializer_list<Value> upvalues) {
  int nupvalues = static_cast<int>(upvalues.size());

  for (const Value &uv : upvalues) {
    protect(uv);
  }

  Closure *native = gc_.allocateNativeClosure(nupvalues);
  native->function = fn;
  native->arity = arity;
  native->name = allocateString(name);
  native->receiver = Value::nil();

  int i = 0;
  for (const Value &uv : upvalues) {
    native->nativeUpvalues[i++] = uv;
  }

  unprotect(nupvalues);

  defineGlobal(name, Value::object(native));
}

bool VM::valuesEqual(Value a, Value b) { return a.equals(b); }

void VM::resetStack() {
  if (mainFiber_) {
    mainFiber_->reset();
  }
  currentFiber_ = mainFiber_;

  pcallStack_.clear();
  hasError_ = false;
  errorValue_ = Value::nil();
}

void VM::collectGarbage() { gc_.collect(); }

void VM::throwError(Value errorValue) {

  hasError_ = true;
  errorValue_ = errorValue;

  if (!hasPcallContext()) {

    std::string errorMsg;
    if (errorValue.isString()) {
      errorMsg = static_cast<StringObject *>(errorValue.asGC())->str();
    } else {
      errorMsg = "error: " + errorValue.toString();
    }

    std::string fullMessage = errorMsg + "\n----------------\nCall stack:" + getStackTrace();

    if (errorHandler_) {
      errorHandler_(fullMessage, 0);
    } else {
      fprintf(stderr, "\n[Runtime Error]\n%s\n\n", fullMessage.c_str());
    }

    throw SptPanic(errorValue);
  }
}

[[noreturn]] void VM::throwPanic(Value errorValue) {
  hasError_ = true;
  errorValue_ = errorValue;
  throw SptPanic(errorValue);
}

void VM::runtimeError(const char *format, ...) {
  va_list args;
  va_start(args, format);
  va_list args_copy;
  va_copy(args_copy, args);
  int size = vsnprintf(nullptr, 0, format, args_copy);
  va_end(args_copy);
  std::vector<char> buffer(size + 1);
  vsnprintf(buffer.data(), buffer.size(), format, args);
  va_end(args);
  std::string message = buffer.data();

  hasError_ = true;
  errorValue_ = Value::object(allocateString(message));

  if (!hasPcallContext()) {
    std::string fullMessage = message;
    fullMessage += "\n----------------\nCall stack:";
    fullMessage += getStackTrace();

    if (errorHandler_) {
      errorHandler_(fullMessage, 0);
    } else {
      fprintf(stderr, "\n[Runtime Error]\n%s\n\n", fullMessage.c_str());
    }

    throw SptPanic(errorValue_);
  }
}

int VM::getLine(const Prototype *proto, size_t instruction) {
  int line = proto->absLineInfo.empty() ? 0 : proto->absLineInfo.front().line;

  if (!proto->absLineInfo.empty()) {
    auto abs_line_info =
        std::upper_bound(proto->absLineInfo.begin(), proto->absLineInfo.end(), instruction,
                         [](size_t pc, const auto &rhs) { return pc < rhs.pc; });
    int basePC = 0;
    if (abs_line_info != proto->absLineInfo.begin()) {
      --abs_line_info;
      basePC = abs_line_info->pc;
      line = abs_line_info->line;
    }

    while (static_cast<size_t>(basePC) < instruction &&
           basePC < static_cast<int>(proto->lineInfo.size())) {
      line += proto->lineInfo[basePC];
      basePC++;
    }
  }

  return line;
}

size_t VM::getCurrentInstruction(const CallFrame &frame) const {
  const Prototype *proto = frame.closure->proto;
  size_t instruction = frame.ip - proto->code.data();

  if (instruction > 0) {
    instruction--;

    if (instruction > 0) {
      OpCode prevOp = GET_OPCODE(proto->code[instruction - 1]);
      if (prevOp == OpCode::OP_INVOKE) {
        instruction--;
      }
    }
  }

  return instruction;
}

std::string VM::getStackTrace() {
  std::string trace = "Call stack:";
  FiberObject *fiber = currentFiber_;

  for (int i = fiber->frameCount - 1; i >= 0; --i) {
    CallFrame &frame = fiber->frames[i];
    const Prototype *proto = frame.closure->proto;

    size_t instruction = getCurrentInstruction(frame);

    int line = getLine(proto, instruction);
    trace += "\n  [line " + std::to_string(line) + "] in ";
    trace += proto->name.empty() ? "<script>" : proto->name + "()";
  }

  return trace;
}

StringObject *VM::allocateString(std::string_view str) { return stringPool_->intern(str); }

StringObject *VM::allocateString(const std::string &str) {
  return stringPool_->intern(std::string_view(str));
}

StringObject *VM::allocateString(const char *str) {
  return stringPool_->intern(std::string_view(str));
}

Closure *VM::allocateScriptClosure(const Prototype *proto) {
  return gc_.allocateScriptClosure(proto);
}

ClassObject *VM::allocateClass(const std::string &name) {
  ClassObject *klass = gc_.allocate<ClassObject>();
  klass->name = name;
  return klass;
}

Instance *VM::allocateInstance(ClassObject *klass) {
  Instance *instance = gc_.allocate<Instance>();
  instance->klass = klass;
  return instance;
}

ListObject *VM::allocateList(int capacity) {
  ListObject *list = gc_.allocate<ListObject>();
  if (capacity > 0) {
    list->elements.resize(capacity, Value::nil());
  }
  return list;
}

MapObject *VM::allocateMap(int capacity) {
  MapObject *map = gc_.allocate<MapObject>();
  if (capacity > 0) {
    map->entries.reserve(capacity);
  }
  return map;
}

void VM::dumpStack() const {
  FiberObject *fiber = currentFiber_;
  printf("\n=== Stack Dump (Fiber %p) ===\n", static_cast<const void *>(fiber));
  printf("Stack range: [%p, %p)\n", static_cast<const void *>(fiber->stack),
         static_cast<const void *>(fiber->stackTop));

  for (const Value *slot = fiber->stack; slot < fiber->stackTop; ++slot) {
    size_t offset = slot - fiber->stack;
    printf("  [%04zu] %s\n", offset, slot->toString().c_str());
  }
  printf("==================\n\n");
}

void VM::dumpGlobals() const {
  printf("\n=== Globals ===\n");
  for (const auto &[nameStr, value] : globals_) {
    printf("  %-20s = %s\n", nameStr->c_str(), value.toString().c_str());
  }
  printf("===============\n\n");
}

int VM::getInfo(Value *f, const char *what, DebugInfo *out_info) {
  if (!f || !what || !out_info)
    return 0;

  if (f->isClosure()) {
    const Closure *closure = static_cast<const Closure *>(f->asGC());
    const Prototype *proto = closure->proto;
    while (*what) {
      char c = *what++;
      switch (c) {
      case 'S':
        out_info->source = proto->source;
        out_info->shortSrc = proto->short_src;
        out_info->lineDefined = proto->lineDefined;
        out_info->lastLineDefined = proto->lastLineDefined;
        break;
      }
    }
    return 1;
  }
  return 0;
}

int VM::getStack(int f, const char *what, DebugInfo *out_info) {
  if (!what || !out_info)
    return 0;

  FiberObject *fiber = currentFiber_;
  if (f < 0 || f >= fiber->frameCount)
    return 0;

  CallFrame &frame = fiber->frames[fiber->frameCount - 1 - f];
  const Prototype *proto = frame.closure->proto;

  while (*what) {
    char c = *what++;
    switch (c) {
    case 'S':
      out_info->source = proto->source;
      out_info->shortSrc = proto->short_src;
      out_info->lineDefined = proto->lineDefined;
      out_info->lastLineDefined = proto->lastLineDefined;
      break;
    case 'l': {
      size_t instruction = getCurrentInstruction(frame);
      out_info->currentLine = getLine(proto, instruction);
    } break;
    }
  }
  return 1;
}

bool VM::hotReload(const std::string &moduleName, CompiledChunk newChunk) {

  prepareChunk(newChunk);

  auto it = modules_.find(moduleName);
  if (it != modules_.end()) {

    it->second.destroyRuntimeData();
  }

  modules_[moduleName] = std::move(newChunk);

  for (auto [nameStr, value] : globals_) {
    if (value.isClass()) {
      auto *klass = static_cast<ClassObject *>(value.asGC());
      klass->methods.clear();
    }
  }

  return true;
}

void VM::registerModule(const std::string &name, CompiledChunk chunk) {
  prepareChunk(chunk);

  auto it = modules_.find(name);
  if (it != modules_.end()) {

    it->second.destroyRuntimeData();
  }

  modules_[name] = std::move(chunk);
}

Value VM::importModule(const std::string &path) {
  auto it = modules_.find(path);
  if (it == modules_.end()) {
    runtimeError("Module not found: %s", path.c_str());
    return Value::nil();
  }

  InterpretResult result = interpret(it->second);
  if (result != InterpretResult::OK) {
    return Value::nil();
  }

  Value moduleEnv = lastModuleResult_;
  if (!moduleEnv.isMap()) {
    return Value::nil();
  }

  MapObject *envMap = static_cast<MapObject *>(moduleEnv.asGC());
  MapObject *exports = allocateMap(8);

  for (const auto &exportName : it->second.exports) {
    StringObject *key = allocateString(exportName);
    Value val = envMap->get(Value::object(key));
    exports->set(Value::object(key), val);
  }

  return Value::object(exports);
}

NativeInstance *VM::allocateNativeInstance(ClassObject *klass, void *data) {
  NativeInstance *instance = gc_.allocate<NativeInstance>();
  instance->klass = klass;
  instance->data = data;
  return instance;
}

Value VM::constantToValue(const ConstantValue &cv) {
  return std::visit(
      [this](auto &&arg) -> Value {
        using T = std::decay_t<decltype(arg)>;
        if constexpr (std::is_same_v<T, std::nullptr_t>) {
          return Value::nil();
        } else if constexpr (std::is_same_v<T, bool>) {
          return Value::boolean(arg);
        } else if constexpr (std::is_same_v<T, int64_t>) {
          return Value::integer(arg);
        } else if constexpr (std::is_same_v<T, double>) {
          return Value::number(arg);
        } else if constexpr (std::is_same_v<T, std::string>) {
          StringObject *str = allocateString(arg);
          return Value::object(str);
        }
      },
      cv);
}

void VM::preparePrototype(Prototype *proto) {
  if (proto->jitReady) {
    return;
  }

  if (!proto->code.empty()) {
    proto->codeCount = static_cast<uint32_t>(proto->code.size());
    proto->codePtr = new Instruction[proto->codeCount];
    std::memcpy(proto->codePtr, proto->code.data(), proto->codeCount * sizeof(Instruction));
  }

  size_t constCount = proto->constants.size();
  if (constCount > 0) {
    proto->k = new Value[constCount];
    proto->kCount = static_cast<uint32_t>(constCount);

    for (size_t i = 0; i < constCount; ++i) {
      proto->k[i] = constantToValue(proto->constants[i]);
    }
  }

  if (!proto->upvalues.empty()) {
    proto->upvaluePtr = new Prototype::UpvalueDesc[proto->numUpvalues];
    std::memcpy(proto->upvaluePtr, proto->upvalues.data(),
                proto->numUpvalues * sizeof(Prototype::UpvalueDesc));
  }

  if (!proto->protos.empty()) {
    proto->protoCount = static_cast<uint32_t>(proto->protos.size());
    proto->protoPtr = new Prototype *[proto->protoCount];

    for (uint32_t i = 0; i < proto->protoCount; ++i) {
      preparePrototype(&proto->protos[i]);
      proto->protoPtr[i] = &proto->protos[i];
    }
  }

  proto->jitReady = true;
}

void VM::prepareChunk(CompiledChunk &chunk) { preparePrototype(&chunk.mainProto); }

void Prototype::init(Prototype *proto) {
  if (!proto)
    return;

  proto->codePtr = nullptr;
  proto->codeCount = 0;
  proto->k = nullptr;
  proto->kCount = 0;
  proto->upvaluePtr = nullptr;
  proto->protoPtr = nullptr;
  proto->protoCount = 0;
  proto->jitReady = false;
}

void Prototype::destroy(Prototype *proto) {
  if (!proto)
    return;

  for (auto &childProto : proto->protos) {
    destroy(&childProto);
  }

  if (proto->codePtr != nullptr) {
    delete[] proto->codePtr;
    proto->codePtr = nullptr;
  }
  proto->codeCount = 0;

  if (proto->k != nullptr) {
    delete[] proto->k;
    proto->k = nullptr;
  }
  proto->kCount = 0;

  if (proto->upvaluePtr != nullptr) {
    delete[] proto->upvaluePtr;
    proto->upvaluePtr = nullptr;
  }

  if (proto->protoPtr != nullptr) {
    delete[] proto->protoPtr;
    proto->protoPtr = nullptr;
  }
  proto->protoCount = 0;

  proto->jitReady = false;
}

void Prototype::reset(Prototype *proto) {
  if (!proto)
    return;

  destroy(proto);

  proto->name.clear();
  proto->source.clear();
  proto->short_src.clear();
  proto->lineDefined = 0;
  proto->lastLineDefined = 0;
  proto->numParams = 0;
  proto->numUpvalues = 0;
  proto->maxStackSize = 0;
  proto->isVararg = false;
  proto->needsReceiver = false;
  proto->useDefer = false;
  proto->code.clear();
  proto->constants.clear();
  proto->absLineInfo.clear();
  proto->lineInfo.clear();
  proto->protos.clear();
  proto->flags = FunctionFlag::FUNC_NONE;
  proto->upvalues.clear();
}

Prototype::Prototype(Prototype &&other) noexcept
    : name(std::move(other.name)), source(std::move(other.source)),
      short_src(std::move(other.short_src)), lineDefined(other.lineDefined),
      lastLineDefined(other.lastLineDefined), numParams(other.numParams),
      numUpvalues(other.numUpvalues), maxStackSize(other.maxStackSize), isVararg(other.isVararg),
      needsReceiver(other.needsReceiver), useDefer(other.useDefer), code(std::move(other.code)),
      constants(std::move(other.constants)), absLineInfo(std::move(other.absLineInfo)),
      lineInfo(std::move(other.lineInfo)), protos(std::move(other.protos)), flags(other.flags),
      upvalues(std::move(other.upvalues)), codePtr(other.codePtr), codeCount(other.codeCount),
      k(other.k), kCount(other.kCount), upvaluePtr(other.upvaluePtr), protoPtr(other.protoPtr),
      protoCount(other.protoCount), jitReady(other.jitReady) {

  other.codePtr = nullptr;
  other.codeCount = 0;
  other.k = nullptr;
  other.kCount = 0;
  other.upvaluePtr = nullptr;
  other.protoPtr = nullptr;
  other.protoCount = 0;
  other.jitReady = false;
}

Prototype &Prototype::operator=(Prototype &&other) noexcept {
  if (this != &other) {

    destroy(this);

    name = std::move(other.name);
    source = std::move(other.source);
    short_src = std::move(other.short_src);
    lineDefined = other.lineDefined;
    lastLineDefined = other.lastLineDefined;
    numParams = other.numParams;
    numUpvalues = other.numUpvalues;
    maxStackSize = other.maxStackSize;
    isVararg = other.isVararg;
    needsReceiver = other.needsReceiver;
    useDefer = other.useDefer;
    code = std::move(other.code);
    constants = std::move(other.constants);
    absLineInfo = std::move(other.absLineInfo);
    lineInfo = std::move(other.lineInfo);
    protos = std::move(other.protos);
    flags = other.flags;
    upvalues = std::move(other.upvalues);

    codePtr = other.codePtr;
    codeCount = other.codeCount;
    k = other.k;
    kCount = other.kCount;
    upvaluePtr = other.upvaluePtr;
    protoPtr = other.protoPtr;
    protoCount = other.protoCount;
    jitReady = other.jitReady;

    other.codePtr = nullptr;
    other.codeCount = 0;
    other.k = nullptr;
    other.kCount = 0;
    other.upvaluePtr = nullptr;
    other.protoPtr = nullptr;
    other.protoCount = 0;
  }
  return *this;
}

Prototype Prototype::deepCopy() const {
  Prototype copy;

  copy.name = name;
  copy.source = source;
  copy.short_src = short_src;
  copy.lineDefined = lineDefined;
  copy.lastLineDefined = lastLineDefined;
  copy.numParams = numParams;
  copy.numUpvalues = numUpvalues;
  copy.maxStackSize = maxStackSize;
  copy.isVararg = isVararg;
  copy.needsReceiver = needsReceiver;
  copy.useDefer = useDefer;
  copy.code = code;
  copy.constants = constants;
  copy.absLineInfo = absLineInfo;
  copy.lineInfo = lineInfo;
  copy.flags = flags;
  copy.upvalues = upvalues;

  copy.protos.reserve(protos.size());
  for (const Prototype &childProto : protos) {
    copy.protos.push_back(childProto.deepCopy());
  }

  Prototype::init(&copy);

  return copy;
}

} // namespace spt