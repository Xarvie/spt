#include "VM.h"

#include "Fiber.h"
#include "NativeBinding.h"
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

  mainFiber_ = gc_.allocate<FiberObject>();
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
}

VM::~VM() {
  currentFiber_ = nullptr;
  mainFiber_ = nullptr;

  globals_.clear();
  modules_.clear();

  pcallStack_.clear();
  hasError_ = false;
  errorValue_ = Value::nil();
  nativeMultiReturn_.clear();
  hasNativeMultiReturn_ = false;
}

void VM::protect(Value value) { currentFiber_->push(value); }

void VM::unprotect(int count) { currentFiber_->stackTop -= count; }

InterpretResult VM::interpret(const CompiledChunk &chunk) {

  mainFiber_->reset();
  currentFiber_ = mainFiber_;

  Closure *mainClosure = allocateClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

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

  return run();
}

InterpretResult VM::call(Closure *closure, int argCount) {
  FiberObject *fiber = currentFiber_;

  if (!closure->proto->isVararg && argCount != closure->proto->numParams) {
    runtimeError("Function '%s' expects %d arguments, got %d", closure->proto->name.c_str(),
                 closure->proto->numParams, argCount);
    return InterpretResult::RUNTIME_ERROR;
  }

  if (fiber->frameCount >= FiberObject::MAX_FRAMES) {
    runtimeError("Stack overflow");
    return InterpretResult::RUNTIME_ERROR;
  }

  int slotsBaseOffset = static_cast<int>((fiber->stackTop - argCount) - fiber->stack);

  fiber->ensureStack(closure->proto->maxStackSize);
  fiber->ensureFrames(1);

  Value *argsStart = fiber->stack + slotsBaseOffset;
  Value *frameEnd = argsStart + closure->proto->maxStackSize;
  for (Value *slot = argsStart + argCount; slot < frameEnd; ++slot) {
    *slot = Value::nil();
  }
  fiber->stackTop = frameEnd;

  CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
  newFrame->closure = closure;
  newFrame->ip = closure->proto->code.data();
  newFrame->expectedResults = 1;
  newFrame->slots = argsStart;
  newFrame->returnTo = nullptr;
  newFrame->deferBase = fiber->deferTop;

  int savedExitFrameCount = exitFrameCount_;
  exitFrameCount_ = fiber->frameCount;

  InterpretResult result = run();

  exitFrameCount_ = savedExitFrameCount;

  return result;
}

InterpretResult VM::executeModule(const CompiledChunk &chunk) {
  FiberObject *fiber = currentFiber_;

  Closure *mainClosure = allocateClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

  int frameStartOffset = static_cast<int>(fiber->stackTop - fiber->stack);

  fiber->ensureStack(mainClosure->proto->maxStackSize);
  fiber->ensureFrames(1);

  CallFrame *frame = &fiber->frames[fiber->frameCount++];
  frame->closure = mainClosure;
  frame->ip = mainClosure->proto->code.data();
  frame->slots = fiber->stack + frameStartOffset;
  frame->expectedResults = 0;
  frame->returnTo = nullptr;
  frame->deferBase = fiber->deferTop;

  Value *frameStart = fiber->stack + frameStartOffset;
  Value *frameEnd = frameStart + mainClosure->proto->maxStackSize;
  for (Value *slot = frameStart; slot < frameEnd; ++slot) {
    *slot = Value::nil();
  }
  fiber->stackTop = frameEnd;

  int savedExitFrameCount = exitFrameCount_;
  exitFrameCount_ = fiber->frameCount;

  InterpretResult result = run();

  exitFrameCount_ = savedExitFrameCount;

  fiber = currentFiber_;

  if (result == InterpretResult::OK && fiber->frameCount >= 0) {
    fiber->stackTop = fiber->stack + frameStartOffset;
  }

  unprotect(1);

  return result;
}

FiberObject *VM::allocateFiber(Closure *closure) {
  FiberObject *fiber = gc_.allocate<FiberObject>();
  fiber->closure = closure;
  fiber->state = FiberState::NEW;
  return fiber;
}

void VM::initFiberForCall(FiberObject *fiber, Value arg) {
  fiber->stackTop = fiber->stack;
  fiber->frameCount = 0;
  fiber->deferTop = 0;
  fiber->openUpvalues = nullptr;

  const Prototype *proto = fiber->closure->proto;
  fiber->ensureStack(proto->maxStackSize);
  fiber->ensureFrames(1);

  *fiber->stackTop++ = arg;

  CallFrame *frame = &fiber->frames[fiber->frameCount++];
  frame->closure = fiber->closure;
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

  if (caller->state == FiberState::RUNNING) {
    caller->state = FiberState::SUSPENDED;
  }

  currentFiber_ = fiber;

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

  while (fiber->deferTop > targetDeferBase) {
    Value deferVal = fiber->deferStack[--fiber->deferTop];

    if (deferVal.isClosure()) {
      Closure *closure = static_cast<Closure *>(deferVal.asGC());
      protect(Value::object(closure));
      call(closure, 0);
      unprotect(1);
    }

    if (hasError_)
      return;
  }
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
  UpValue *prevUpvalue = nullptr;
  UpValue *upvalue = fiber->openUpvalues;

  while (upvalue != nullptr && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->nextOpen;
  }

  if (upvalue != nullptr && upvalue->location == local) {
    return upvalue;
  }

  UpValue *createdUpvalue = gc_.allocate<UpValue>();
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

void VM::defineGlobal(StringObject *name, Value value) { globals_[name] = value; }

Value VM::getGlobal(StringObject *name) {
  auto it = globals_.find(name);
  return (it != globals_.end()) ? it->second : Value::nil();
}

void VM::setGlobal(StringObject *name, Value value) { globals_[name] = value; }

void VM::defineGlobal(const std::string &name, Value value) {
  StringObject *nameStr = allocateString(name);
  globals_[nameStr] = value;
}

Value VM::getGlobal(const std::string &name) {

  auto it = globals_.find(std::string_view(name));
  return (it != globals_.end()) ? it->second : Value::nil();
}

void VM::setGlobal(const std::string &name, Value value) {
  StringObject *nameStr = allocateString(name);
  globals_[nameStr] = value;
}

void VM::registerNative(const std::string &name, NativeFn fn, int arity, uint8_t flags) {
  NativeFunction *native = gc_.allocate<NativeFunction>();
  native->name = name;
  native->function = fn;
  native->arity = arity;
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
  nativeMultiReturn_.clear();
  hasNativeMultiReturn_ = false;
}

void VM::collectGarbage() { gc_.collect(); }

void VM::throwError(Value errorValue) {
  if (!pcallStack_.empty()) {
    hasError_ = true;
    errorValue_ = errorValue;
  } else {
    std::string errorMsg;
    if (errorValue.isString()) {
      errorMsg = static_cast<StringObject *>(errorValue.asGC())->str();
    } else {
      errorMsg = "error: " + errorValue.toString();
    }
    runtimeError("%s", errorMsg.c_str());
  }
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

  if (pcallStack_.empty()) {
    std::string fullMessage = message;
    fullMessage += "\n----------------\nCall stack:";
    fullMessage += getStackTrace();

    if (errorHandler_) {
      errorHandler_(fullMessage, 0);
    } else {
      fprintf(stderr, "\n[Runtime Error]\n%s\n\n", fullMessage.c_str());
    }
  }

  hasError_ = true;
  errorValue_ = Value::object(allocateString(message));
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

void VM::setNativeMultiReturn(const std::vector<Value> &values) {
  hasNativeMultiReturn_ = true;
  nativeMultiReturn_ = values;
}

void VM::setNativeMultiReturn(std::initializer_list<Value> values) {
  hasNativeMultiReturn_ = true;
  nativeMultiReturn_.clear();
  nativeMultiReturn_.insert(nativeMultiReturn_.end(), values.begin(), values.end());
}

StringObject *VM::allocateString(std::string_view str) { return stringPool_->intern(str); }

StringObject *VM::allocateString(const std::string &str) {
  return stringPool_->intern(std::string_view(str));
}

StringObject *VM::allocateString(const char *str) {
  return stringPool_->intern(std::string_view(str));
}

Closure *VM::allocateClosure(const Prototype *proto) { return gc_.allocateClosure(proto); }

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

bool VM::hotReload(const std::string &moduleName, const CompiledChunk &newChunk) {
  modules_[moduleName] = newChunk;

  for (auto &[nameStr, value] : globals_) {
    if (value.isClass()) {
      auto *klass = static_cast<ClassObject *>(value.asGC());
      klass->methods.clear();
    }
  }

  return true;
}

void VM::registerModule(const std::string &name, const CompiledChunk &chunk) {
  modules_[name] = chunk;
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

NativeClassObject *VM::allocateNativeClass(const std::string &name) {
  NativeClassObject *nativeClass = gc_.allocate<NativeClassObject>();
  nativeClass->name = name;
  return nativeClass;
}

NativeInstance *VM::allocateNativeInstance(NativeClassObject *nativeClass) {
  NativeInstance *instance = gc_.allocate<NativeInstance>();
  instance->nativeClass = nativeClass;
  instance->ownership = nativeClass ? nativeClass->defaultOwnership : OwnershipMode::OwnedByVM;
  return instance;
}

NativeInstance *VM::createNativeInstance(NativeClassObject *nativeClass, int argc, Value *argv) {
  if (!nativeClass) {
    runtimeError("Cannot create instance of null native class");
    return nullptr;
  }

  if (!nativeClass->hasConstructor()) {
    runtimeError("Native class '%s' has no constructor", nativeClass->name.c_str());
    return nullptr;
  }

  NativeInstance *instance = allocateNativeInstance(nativeClass);
  protect(Value::object(instance));

  void *data = nativeClass->constructor(this, argc, argv);
  if (!data) {
    unprotect(1);
    runtimeError("Failed to construct native object of type '%s'", nativeClass->name.c_str());
    return nullptr;
  }

  instance->data = data;
  unprotect(1);

  return instance;
}

} // namespace spt
