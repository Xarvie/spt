#include "VM.h"
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
  prepareChunk(const_cast<CompiledChunk &>(chunk));

  mainFiber_->reset();
  currentFiber_ = mainFiber_;

  Closure *mainClosure = allocateScriptClosure(&chunk.mainProto);
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
  int numParams = closure->proto->numParams;
  Value *slot = argsStart + argCount;
  for (int i = argCount; i < numParams; ++i) {
    *slot++ = Value::nil();
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

  prepareChunk(const_cast<CompiledChunk &>(chunk));

  FiberObject *fiber = currentFiber_;

  Closure *mainClosure = allocateScriptClosure(&chunk.mainProto);
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
  StringObject *nameStr = stringPool_->find(name);
  if (!nameStr)
    return Value::nil();
  auto it = globals_.find(nameStr);
  return (it != globals_.end()) ? it->second : Value::nil();
}

void VM::setGlobal(const std::string &name, Value value) {
  StringObject *nameStr = allocateString(name);
  globals_[nameStr] = value;
}

void VM::registerNative(const std::string &name, NativeFn fn, int arity) {
  Closure *native = gc_.allocateNativeClosure(0);
  native->function = std::move(fn);
  native->arity = arity;
  native->name = allocateString(name);
  native->receiver = Value::nil();

  defineGlobal(name, Value::object(native));
}

Closure *VM::createNativeClosure(NativeFn fn, int arity, int nupvalues) {
  Closure *native = gc_.allocateNativeClosure(nupvalues);
  native->function = std::move(fn);
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
  native->function = std::move(fn);
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

Prototype::~Prototype() {
  if (codePtr != nullptr) {
    delete[] codePtr;
    codePtr = nullptr;
  }
  if (k != nullptr) {
    delete[] k;
    k = nullptr;
  }
  if (upvaluePtr != nullptr) {
    delete[] upvaluePtr;
    upvaluePtr = nullptr;
  }
  if (protoPtr != nullptr) {
    delete[] protoPtr;
    protoPtr = nullptr;
  }
}

Prototype::Prototype(Prototype &&other) noexcept
    : name(std::move(other.name)), source(std::move(other.source)),
      short_src(std::move(other.short_src)), lineDefined(other.lineDefined),
      lastLineDefined(other.lastLineDefined), numParams(other.numParams),
      numUpvalues(other.numUpvalues), maxStackSize(other.maxStackSize), isVararg(other.isVararg),
      needsReceiver(other.needsReceiver), useDefer(other.useDefer), code(std::move(other.code)),
      constants(std::move(other.constants)), absLineInfo(std::move(other.absLineInfo)),
      lineInfo(std::move(other.lineInfo)), protos(std::move(other.protos)), flags(other.flags),
      upvalues(std::move(other.upvalues)),

      codePtr(other.codePtr), codeCount(other.codeCount), k(other.k), kCount(other.kCount),
      upvaluePtr(other.upvaluePtr), protoPtr(other.protoPtr), protoCount(other.protoCount),
      jitReady(other.jitReady) {

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
    if (codePtr != nullptr) {
      delete[] codePtr;
    }
    if (k != nullptr) {
      delete[] k;
    }
    if (upvaluePtr != nullptr) {
      delete[] upvaluePtr;
    }
    if (protoPtr != nullptr) {
      delete[] protoPtr;
    }

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
    other.jitReady = false;
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

  copy.codePtr = nullptr;
  copy.codeCount = 0;
  copy.k = nullptr;
  copy.kCount = 0;
  copy.upvaluePtr = nullptr;
  copy.protoPtr = nullptr;
  copy.protoCount = 0;
  copy.jitReady = false;

  return copy;
}

} // namespace spt