#include "VM.h"

#include "Fiber.h"
#include "SptDebug.hpp"
#include "SptStdlibs.h"
#include "VMDispatch.h"
#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <filesystem>

namespace spt {

VM::VM(const VMConfig &config) : config_(config), gc_(this, {}) {
  globals_.reserve(256);

  mainFiber_ = gc_.allocate<FiberObject>();
  mainFiber_->stack.resize(config.stackSize);
  mainFiber_->stackTop = mainFiber_->stack.data();
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
  strings_.clear();
  modules_.clear();

  pcallStack_.clear();
  hasError_ = false;
  errorValue_ = Value::nil();
  nativeMultiReturn_.clear();
  hasNativeMultiReturn_ = false;
}

void VM::protect(Value value) { currentFiber_->push(value); }

void VM::unprotect(int count) { currentFiber_->stackTop -= count; }

void VM::registerBuiltinFunctions() {
  registerNative(
      "print",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        std::string outputBuffer;
        for (int i = 0; i < argc; ++i) {
          if (i > 0)
            outputBuffer += " ";
          std::string s = args[i].toString();
          if (args[i].isNumber() && !args[i].isInt()) {
            size_t dotPos = s.find('.');
            if (dotPos != std::string::npos) {
              s.erase(s.find_last_not_of('0') + 1, std::string::npos);
              if (s.back() == '.')
                s.pop_back();
            }
          }
          outputBuffer += s;
        }
        outputBuffer += "\n";

        if (this->printHandler_) {
          this->printHandler_(outputBuffer);
        } else {
          printf("%s", outputBuffer.c_str());
        }
        return Value::nil();
      },
      -1);

  registerNative(
      "toInt",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::integer(0);
        Value v = args[0];
        if (v.isInt()) {
          return v;
        } else if (v.isFloat()) {
          return Value::integer(static_cast<int64_t>(v.asFloat()));
        } else if (v.isString()) {
          StringObject *str = static_cast<StringObject *>(v.asGC());
          char *endptr = nullptr;
          errno = 0;
          long long result = std::strtoll(str->data.c_str(), &endptr, 10);
          if (endptr == str->data.c_str() || errno == ERANGE) {
            return Value::integer(0);
          }
          return Value::integer(static_cast<int64_t>(result));
        } else if (v.isBool()) {
          return Value::integer(v.asBool() ? 1 : 0);
        }
        return Value::integer(0);
      },
      1);

  registerNative(
      "toFloat",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::number(0.0);
        Value v = args[0];
        if (v.isFloat()) {
          return v;
        } else if (v.isInt()) {
          return Value::number(static_cast<double>(v.asInt()));
        } else if (v.isString()) {
          StringObject *str = static_cast<StringObject *>(v.asGC());
          char *endptr = nullptr;
          errno = 0;
          double result = std::strtod(str->data.c_str(), &endptr);
          if (endptr == str->data.c_str() || errno == ERANGE) {
            return Value::number(0.0);
          }
          return Value::number(result);
        } else if (v.isBool()) {
          return Value::number(v.asBool() ? 1.0 : 0.0);
        }
        return Value::number(0.0);
      },
      1);

  registerNative(
      "toString",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::object(this->allocateString(""));
        return Value::object(this->allocateString(args[0].toString()));
      },
      1);

  registerNative(
      "toBool",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::boolean(false);
        return Value::boolean(args[0].isTruthy());
      },
      1);

  registerNative(
      "typeOf",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::object(this->allocateString("nil"));
        return Value::object(this->allocateString(args[0].typeName()));
      },
      1);

  registerNative(
      "len",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::integer(0);
        Value v = args[0];
        if (v.isString()) {
          StringObject *str = static_cast<StringObject *>(v.asGC());
          return Value::integer(static_cast<int64_t>(str->data.size()));
        } else if (v.isList()) {
          ListObject *list = static_cast<ListObject *>(v.asGC());
          return Value::integer(static_cast<int64_t>(list->elements.size()));
        } else if (v.isMap()) {
          MapObject *map = static_cast<MapObject *>(v.asGC());
          return Value::integer(static_cast<int64_t>(map->entries.size()));
        }
        return Value::integer(0);
      },
      1);

  registerNative(
      "abs",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::integer(0);
        Value v = args[0];
        if (v.isInt()) {
          int64_t val = v.asInt();
          return Value::integer(val < 0 ? -val : val);
        } else if (v.isFloat()) {
          return Value::number(std::abs(v.asFloat()));
        }
        return Value::integer(0);
      },
      1);

  registerNative(
      "floor",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::integer(0);
        Value v = args[0];
        if (v.isInt()) {
          return v;
        } else if (v.isFloat()) {
          return Value::integer(static_cast<int64_t>(std::floor(v.asFloat())));
        }
        return Value::integer(0);
      },
      1);

  registerNative(
      "ceil",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::integer(0);
        Value v = args[0];
        if (v.isInt()) {
          return v;
        } else if (v.isFloat()) {
          return Value::integer(static_cast<int64_t>(std::ceil(v.asFloat())));
        }
        return Value::integer(0);
      },
      1);

  registerNative(
      "round",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::integer(0);
        Value v = args[0];
        if (v.isInt()) {
          return v;
        } else if (v.isFloat()) {
          return Value::integer(static_cast<int64_t>(std::round(v.asFloat())));
        }
        return Value::integer(0);
      },
      1);

  registerNative(
      "sqrt",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::number(0.0);
        Value v = args[0];
        double val = v.isInt() ? static_cast<double>(v.asInt()) : v.asFloat();
        return Value::number(std::sqrt(val));
      },
      1);

  registerNative(
      "pow",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::number(0.0);
        double base = args[0].isInt() ? static_cast<double>(args[0].asInt()) : args[0].asFloat();
        double exp = args[1].isInt() ? static_cast<double>(args[1].asInt()) : args[1].asFloat();
        return Value::number(std::pow(base, exp));
      },
      2);

  registerNative(
      "min",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::nil();
        Value a = args[0];
        Value b = args[1];
        if (a.isInt() && b.isInt()) {
          return Value::integer(std::min(a.asInt(), b.asInt()));
        }
        double va = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
        double vb = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        return Value::number(std::min(va, vb));
      },
      2);

  registerNative(
      "max",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::nil();
        Value a = args[0];
        Value b = args[1];
        if (a.isInt() && b.isInt()) {
          return Value::integer(std::max(a.asInt(), b.asInt()));
        }
        double va = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
        double vb = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        return Value::number(std::max(va, vb));
      },
      2);

  registerNative(
      "char",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1 || !args[0].isInt())
          return Value::object(this->allocateString(""));
        int64_t code = args[0].asInt();
        if (code < 0 || code > 127)
          return Value::object(this->allocateString(""));
        char c = static_cast<char>(code);
        return Value::object(this->allocateString(std::string(1, c)));
      },
      1);

  registerNative(
      "ord",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1 || !args[0].isString())
          return Value::integer(0);
        StringObject *str = static_cast<StringObject *>(args[0].asGC());
        if (str->data.empty())
          return Value::integer(0);
        return Value::integer(static_cast<int64_t>(static_cast<unsigned char>(str->data[0])));
      },
      1);

  registerNative(
      "range",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::nil();
        int64_t start = args[0].isInt() ? args[0].asInt() : 0;
        int64_t end = args[1].isInt() ? args[1].asInt() : 0;
        int64_t step = (argc >= 3 && args[2].isInt()) ? args[2].asInt() : 1;
        if (step == 0)
          return Value::nil();

        ListObject *list = this->allocateList(0);
        this->protect(Value::object(list));

        if (step > 0) {
          for (int64_t i = start; i < end; i += step) {
            list->elements.push_back(Value::integer(i));
          }
        } else {
          for (int64_t i = start; i > end; i += step) {
            list->elements.push_back(Value::integer(i));
          }
        }

        this->unprotect(1);
        return Value::object(list);
      },
      -1);

  registerNative(
      "assert",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::nil();
        if (!args[0].isTruthy()) {
          std::string msg = "Assertion failed";
          if (argc >= 2 && args[1].isString()) {
            msg = static_cast<StringObject *>(args[1].asGC())->data;
          }
          this->runtimeError("%s", msg.c_str());
        }
        return Value::nil();
      },
      -1);

  registerNative(
      "error",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        Value errorVal;
        if (argc >= 1) {
          errorVal = args[0];
        } else {
          errorVal = Value::object(this->allocateString("error called without message"));
        }
        this->throwError(errorVal);
        return Value::nil();
      },
      -1);

  registerNative(
      "pcall",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1) {
          this->setNativeMultiReturn({Value::boolean(false), Value::object(this->allocateString(
                                                                 "pcall: expected a function"))});
          return Value::nil();
        }

        Value func = args[0];
        if (!func.isClosure() && !func.isNativeFunc()) {
          this->setNativeMultiReturn(
              {Value::boolean(false),
               Value::object(this->allocateString("pcall: first argument must be a function"))});
          return Value::nil();
        }

        FiberObject *fiber = currentFiber_;
        int savedFrameCount = fiber->frameCount;
        Value *savedStackTop = fiber->stackTop;
        UpValue *savedOpenUpvalues = fiber->openUpvalues;
        int savedDeferTop = fiber->deferTop;
        bool savedHasError = this->hasError_;
        Value savedErrorValue = this->errorValue_;

        this->hasError_ = false;
        this->errorValue_ = Value::nil();

        ProtectedCallContext ctx;
        ctx.fiber = fiber;
        ctx.frameCount = savedFrameCount;
        ctx.stackTop = savedStackTop;
        ctx.openUpvalues = savedOpenUpvalues;
        ctx.active = true;
        this->pcallStack_.push_back(ctx);

        int funcArgCount = argc - 1;
        Value *funcArgs = args + 1;

        Value *callBase = fiber->stackTop;
        *fiber->stackTop++ = func;
        for (int i = 0; i < funcArgCount; ++i) {
          *fiber->stackTop++ = funcArgs[i];
        }

        InterpretResult result = InterpretResult::OK;
        std::vector<Value> returnValues;

        if (func.isClosure()) {
          Closure *closure = static_cast<Closure *>(func.asGC());
          const Prototype *proto = closure->proto;

          if (!proto->isVararg && funcArgCount != proto->numParams) {
            this->hasError_ = true;
            this->errorValue_ = Value::object(this->allocateString(
                "Function expects " + std::to_string(proto->numParams) + " arguments"));
            result = InterpretResult::RUNTIME_ERROR;
          } else if (fiber->frameCount >= FiberObject::MAX_FRAMES) {
            this->hasError_ = true;
            this->errorValue_ = Value::object(this->allocateString("Stack overflow"));
            result = InterpretResult::RUNTIME_ERROR;
          } else {

            int slotsBaseOffset = static_cast<int>((callBase + 1) - fiber->stack.data());

            fiber->ensureStack(proto->maxStackSize);
            fiber->ensureFrames(1);

            Value *newSlots = fiber->stack.data() + slotsBaseOffset;
            for (Value *p = fiber->stackTop; p < newSlots + proto->maxStackSize; ++p) {
              *p = Value::nil();
            }
            fiber->stackTop = newSlots + proto->maxStackSize;

            CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
            newFrame->closure = closure;
            newFrame->ip = proto->code.data();
            newFrame->expectedResults = -1;
            newFrame->slotsBase = slotsBaseOffset;
            newFrame->returnBase = 0;
            newFrame->deferBase = fiber->deferTop;

            int savedExitFrameCount = this->exitFrameCount_;
            this->exitFrameCount_ = fiber->frameCount;

            result = this->run();

            this->exitFrameCount_ = savedExitFrameCount;

            if (result == InterpretResult::OK && !this->hasError_) {
              if (this->hasNativeMultiReturn_) {
                returnValues = std::move(this->nativeMultiReturn_);
                this->hasNativeMultiReturn_ = false;
              } else if (!this->lastModuleResult_.isNil()) {
                returnValues.push_back(this->lastModuleResult_);
              }
            }
          }
        } else if (func.isNativeFunc()) {
          NativeFunction *native = static_cast<NativeFunction *>(func.asGC());

          if (native->arity != -1 && funcArgCount != native->arity) {
            this->hasError_ = true;
            this->errorValue_ = Value::object(this->allocateString(
                "Native function expects " + std::to_string(native->arity) + " arguments"));
            result = InterpretResult::RUNTIME_ERROR;
          } else {
            this->hasNativeMultiReturn_ = false;
            Value nativeResult = native->function(this, native->receiver, funcArgCount, funcArgs);

            if (!this->hasError_) {
              if (this->hasNativeMultiReturn_) {
                returnValues = std::move(this->nativeMultiReturn_);
                this->hasNativeMultiReturn_ = false;
              } else {
                returnValues.push_back(nativeResult);
              }
            } else {
              result = InterpretResult::RUNTIME_ERROR;
            }
          }
        }

        this->pcallStack_.pop_back();
        fiber->stackTop = savedStackTop;

        if (result == InterpretResult::OK && !this->hasError_) {
          std::vector<Value> multiResult;
          multiResult.push_back(Value::boolean(true));
          for (const auto &v : returnValues) {
            multiResult.push_back(v);
          }
          this->setNativeMultiReturn(multiResult);
          this->hasError_ = savedHasError;
          this->errorValue_ = savedErrorValue;
          return Value::nil();
        } else {
          this->closeUpvalues(savedStackTop);

          while (fiber->frameCount > savedFrameCount) {

            CallFrame *currentFrame = &fiber->frames[fiber->frameCount - 1];

            this->invokeDefers(currentFrame);

            fiber->frameCount--;
          }
          fiber->deferTop = savedDeferTop;
          fiber->openUpvalues = savedOpenUpvalues;

          Value errVal = this->hasError_ ? this->errorValue_
                                         : Value::object(this->allocateString("unknown error"));
          this->setNativeMultiReturn({Value::boolean(false), errVal});
          this->hasError_ = savedHasError;
          this->errorValue_ = savedErrorValue;
          return Value::nil();
        }
      },
      -1);

  registerNative(
      "isInt",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isInt()); }, 1);
  registerNative(
      "isFloat",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isFloat()); }, 1);
  registerNative(
      "isNumber",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isNumber()); }, 1);
  registerNative(
      "isString",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isString()); }, 1);
  registerNative(
      "isBool",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isBool()); }, 1);
  registerNative(
      "isList",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isList()); }, 1);
  registerNative(
      "isMap",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c > 0 && a[0].isMap()); }, 1);
  registerNative(
      "isNull",
      [](VM *vm, Value r, int c, Value *a) { return Value::boolean(c < 1 || a[0].isNil()); }, 1);
  registerNative(
      "isFunction",
      [](VM *vm, Value r, int c, Value *a) {
        return Value::boolean(c > 0 && (a[0].isClosure() || a[0].isNativeFunc()));
      },
      1);
}

InterpretResult VM::interpret(const CompiledChunk &chunk) {

  mainFiber_->reset();
  currentFiber_ = mainFiber_;

  Closure *mainClosure = allocateClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

  currentFiber_->ensureStack(mainClosure->proto->maxStackSize);
  currentFiber_->ensureFrames(1);

  int slotsBaseOffset = static_cast<int>(currentFiber_->stackTop - currentFiber_->stack.data());

  for (int i = 0; i < mainClosure->proto->maxStackSize; ++i) {
    currentFiber_->stackTop[i] = Value::nil();
  }
  currentFiber_->stackTop += mainClosure->proto->maxStackSize;

  CallFrame *frame = &currentFiber_->frames[currentFiber_->frameCount++];
  frame->closure = mainClosure;
  frame->ip = mainClosure->proto->code.data();
  frame->slotsBase = slotsBaseOffset;
  frame->expectedResults = 1;
  frame->returnBase = 0;
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

  int slotsBaseOffset = static_cast<int>((fiber->stackTop - argCount) - fiber->stack.data());

  fiber->ensureStack(closure->proto->maxStackSize);
  fiber->ensureFrames(1);

  Value *argsStart = fiber->stack.data() + slotsBaseOffset;
  Value *frameEnd = argsStart + closure->proto->maxStackSize;
  for (Value *slot = argsStart + argCount; slot < frameEnd; ++slot) {
    *slot = Value::nil();
  }
  fiber->stackTop = frameEnd;

  CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
  newFrame->closure = closure;
  newFrame->ip = closure->proto->code.data();
  newFrame->expectedResults = 1;
  newFrame->slotsBase = slotsBaseOffset;
  newFrame->returnBase = 0;
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

  int frameStartOffset = static_cast<int>(fiber->stackTop - fiber->stack.data());

  fiber->ensureStack(mainClosure->proto->maxStackSize);
  fiber->ensureFrames(1);

  CallFrame *frame = &fiber->frames[fiber->frameCount++];
  frame->closure = mainClosure;
  frame->ip = mainClosure->proto->code.data();
  frame->slotsBase = frameStartOffset;
  frame->expectedResults = 0;
  frame->returnBase = 0;
  frame->deferBase = fiber->deferTop;

  Value *frameStart = fiber->stack.data() + frameStartOffset;
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
    fiber->stackTop = fiber->stack.data() + frameStartOffset;
  }

  unprotect(1);

  return result;
}

static inline double valueToNum(const Value &v) {
  if (v.isInt())
    return static_cast<double>(v.asInt());
  if (v.isFloat())
    return v.asFloat();
  return 0.0;
}

InterpretResult VM::run() {
  FiberObject *fiber = currentFiber_;
  CallFrame *frame = &fiber->frames[fiber->frameCount - 1];

  Value *stackBase = fiber->stack.data();
  Value *slots = stackBase + frame->slotsBase;
  uint32_t instruction;

#if SPT_USE_COMPUTED_GOTO
  SPT_DEFINE_DISPATCH_TABLE()
#endif

#define FIBER fiber
#define STACK_TOP fiber->stackTop
#define FRAMES fiber->frames
#define FRAME_COUNT fiber->frameCount

#define REFRESH_CACHE()                                                                            \
  do {                                                                                             \
    stackBase = fiber->stack.data();                                                               \
    frame = &fiber->frames[fiber->frameCount - 1];                                                 \
    slots = stackBase + frame->slotsBase;                                                          \
  } while (0)

#define REFRESH_SLOTS()                                                                            \
  do {                                                                                             \
    stackBase = fiber->stack.data();                                                               \
    slots = stackBase + frame->slotsBase;                                                          \
  } while (0)

  SPT_DISPATCH_LOOP_BEGIN()

  SPT_OPCODE(OP_MOVE) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    slots[A] = slots[B];
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADK) {
    uint8_t A = GETARG_A(instruction);
    uint32_t Bx = GETARG_Bx(instruction);
    const auto &constant = frame->closure->proto->constants[Bx];
    Value value;
    std::visit(
        [&](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::nullptr_t>) {
            value = Value::nil();
          } else if constexpr (std::is_same_v<T, bool>) {
            value = Value::boolean(arg);
          } else if constexpr (std::is_same_v<T, int64_t>) {
            value = Value::integer(arg);
          } else if constexpr (std::is_same_v<T, double>) {
            value = Value::number(arg);
          } else if constexpr (std::is_same_v<T, std::string>) {
            value = Value::object(allocateString(arg));
          }
        },
        constant);
    slots[A] = value;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADBOOL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    slots[A] = Value::boolean(B != 0);
    if (C != 0)
      frame->ip++;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADNIL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    for (int i = 0; i <= B; ++i) {
      slots[A + i] = Value::nil();
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWLIST) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    ListObject *list = allocateList(B);
    slots[A] = Value::object(list);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWMAP) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    MapObject *map = allocateMap(B);
    slots[A] = Value::object(map);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_GETINDEX) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value container = slots[B];
    Value index = slots[C];

    if (container.isList()) {
      auto *list = static_cast<ListObject *>(container.asGC());
      if (!index.isInt()) {
        runtimeError("List index must be integer");
        return InterpretResult::RUNTIME_ERROR;
      }
      int64_t idx = index.asInt();
      if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
        runtimeError("List index out of range");
        return InterpretResult::RUNTIME_ERROR;
      }
      slots[A] = list->elements[idx];
    } else if (container.isMap()) {
      auto *map = static_cast<MapObject *>(container.asGC());
      slots[A] = map->get(index);
    } else {
      runtimeError("Cannot index type: %s", container.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SETINDEX) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value container = slots[A];
    Value index = slots[B];
    Value value = slots[C];

    if (container.isList()) {
      auto *list = static_cast<ListObject *>(container.asGC());
      if (!index.isInt()) {
        runtimeError("List index must be integer");
        return InterpretResult::RUNTIME_ERROR;
      }
      int64_t idx = index.asInt();
      if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
        runtimeError("List index out of range");
        return InterpretResult::RUNTIME_ERROR;
      }
      list->elements[idx] = value;
    } else if (container.isMap()) {
      auto *map = static_cast<MapObject *>(container.asGC());
      map->set(index, value);
    } else {
      runtimeError("Cannot index-assign to type: %s", container.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_GETFIELD) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value object = slots[B];
    const auto &keyConst = frame->closure->proto->constants[C];

    if (!std::holds_alternative<std::string>(keyConst)) {
      runtimeError("GETFIELD requires string key constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &fieldName = std::get<std::string>(keyConst);

    if (object.isList() || object.isMap() || object.isString() || object.isFiber()) {
      Value result;
      if (StdlibDispatcher::getProperty(this, object, fieldName, result)) {
        slots[A] = result;
        SPT_DISPATCH();
      }
      if (!object.isMap()) {
        runtimeError("Type '%s' has no property '%s'", object.typeName(), fieldName.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    if (object.isInstance()) {
      auto *instance = static_cast<Instance *>(object.asGC());
      Value result = instance->getField(fieldName);
      if (result.isNil() && instance->klass) {
        auto it = instance->klass->methods.find(fieldName);
        if (it != instance->klass->methods.end()) {
          result = it->second;
        }
      }
      slots[A] = result;
    } else if (object.isClass()) {
      auto *klass = static_cast<ClassObject *>(object.asGC());

      if (klass->name == "Fiber" && fieldName == "current") {
        slots[A] = Value::object(currentFiber_);
        SPT_DISPATCH();
      }

      auto it = klass->methods.find(fieldName);
      if (it != klass->methods.end()) {
        slots[A] = it->second;
      } else {
        auto itStatic = klass->statics.find(fieldName);
        slots[A] = (itStatic != klass->statics.end()) ? itStatic->second : Value::nil();
      }
    } else if (object.isMap()) {
      auto *map = static_cast<MapObject *>(object.asGC());
      Value result = Value::nil();
      for (const auto &pair : map->entries) {
        if (pair.first.isString()) {
          StringObject *keyStr = static_cast<StringObject *>(pair.first.asGC());
          if (keyStr->data == fieldName) {
            result = pair.second;
            break;
          }
        }
      }
      if (result.isNil()) {
        auto it = globals_.find(fieldName);
        if (it != globals_.end()) {
          result = it->second;
        }
      }
      slots[A] = result;
    } else {
      runtimeError("Cannot get field '%s' from type: %s", fieldName.c_str(), object.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SETFIELD) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value object = slots[A];
    const auto &keyConst = frame->closure->proto->constants[B];
    Value value = slots[C];

    if (!std::holds_alternative<std::string>(keyConst)) {
      runtimeError("SETFIELD requires string key constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &fieldName = std::get<std::string>(keyConst);

    if (object.isInstance()) {
      auto *instance = static_cast<Instance *>(object.asGC());
      instance->setField(fieldName, value);
    } else if (object.isClass()) {
      auto *klass = static_cast<ClassObject *>(object.asGC());
      klass->methods[fieldName] = value;
    } else if (object.isMap()) {
      auto *map = static_cast<MapObject *>(object.asGC());
      StringObject *key = allocateString(fieldName);
      map->set(Value::object(key), value);
    } else {
      runtimeError("Cannot set field '%s' on type: %s", fieldName.c_str(), object.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWCLASS) {
    uint8_t A = GETARG_A(instruction);
    uint32_t Bx = GETARG_Bx(instruction);
    const auto &nameConst = frame->closure->proto->constants[Bx];
    if (!std::holds_alternative<std::string>(nameConst)) {
      runtimeError("Class name must be string constant");
      return InterpretResult::RUNTIME_ERROR;
    }
    const std::string &className = std::get<std::string>(nameConst);
    ClassObject *klass = allocateClass(className);
    slots[A] = Value::object(klass);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWOBJ) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);

    Value classValue = slots[B];

    if (!classValue.isClass()) {
      runtimeError("Cannot instantiate non-class type");
      return InterpretResult::RUNTIME_ERROR;
    }

    auto *klass = static_cast<ClassObject *>(classValue.asGC());
    Instance *instance = allocateInstance(klass);
    Value instanceVal = Value::object(instance);

    slots[A] = instanceVal;

    auto it = klass->methods.find("init");
    if (it != klass->methods.end()) {
      Value initializer = it->second;

      slots[B] = instanceVal;

      if (initializer.isClosure()) {
        Closure *closure = static_cast<Closure *>(initializer.asGC());
        const Prototype *proto = closure->proto;

        int providedArgs = C;
        if (proto->needsReceiver) {
          providedArgs += 1;
        }

        if (!proto->isVararg && providedArgs != proto->numParams) {
          runtimeError("init expects %d arguments, got %d", proto->numParams, providedArgs);
          return InterpretResult::RUNTIME_ERROR;
        }

        if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
          runtimeError("Stack overflow");
          return InterpretResult::RUNTIME_ERROR;
        }

        int newSlotsBase = frame->slotsBase + B;
        int neededStack = newSlotsBase + proto->maxStackSize;

        fiber->ensureStack(neededStack);
        fiber->ensureFrames(1);
        REFRESH_CACHE();

        Value *argsStart = stackBase + newSlotsBase;
        Value *targetStackTop = argsStart + proto->maxStackSize;

        for (Value *p = argsStart + providedArgs; p < targetStackTop; ++p) {
          *p = Value::nil();
        }

        STACK_TOP = targetStackTop;

        CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
        newFrame->closure = closure;
        newFrame->ip = proto->code.data();
        newFrame->expectedResults = 0;
        newFrame->slotsBase = newSlotsBase;
        newFrame->returnBase = frame->slotsBase + A;

        newFrame->deferBase = fiber->deferTop;

        frame = newFrame;
        slots = stackBase + frame->slotsBase;

      } else if (initializer.isNativeFunc()) {
        NativeFunction *native = static_cast<NativeFunction *>(initializer.asGC());

        if (native->arity != -1 && C != native->arity) {
          runtimeError("Native init expects %d arguments, got %d", native->arity, C);
          return InterpretResult::RUNTIME_ERROR;
        }

        Value *argsStart = &slots[B + 1];
        native->function(this, instanceVal, C, argsStart);
      } else {
        runtimeError("init method must be a function");
        return InterpretResult::RUNTIME_ERROR;
      }
    } else {
      if (C > 0) {
        runtimeError("Class '%s' has no init method but arguments were provided.",
                     klass->name.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_GETUPVAL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    if (B >= frame->closure->upvalues.size()) {
      runtimeError("Invalid upvalue index: %d", B);
      return InterpretResult::RUNTIME_ERROR;
    }
    UpValue *upval = frame->closure->upvalues[B];
    slots[A] = *upval->location;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SETUPVAL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    if (B >= frame->closure->upvalues.size()) {
      runtimeError("Invalid upvalue index: %d", B);
      return InterpretResult::RUNTIME_ERROR;
    }
    UpValue *upval = frame->closure->upvalues[B];
    *upval->location = slots[A];
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CLOSURE) {
    uint8_t A = GETARG_A(instruction);
    uint32_t Bx = GETARG_Bx(instruction);
    const Prototype &proto = frame->closure->proto->protos[Bx];
    Closure *closure = allocateClosure(&proto);
    protect(Value::object(closure));

    for (size_t i = 0; i < proto.numUpvalues; ++i) {
      const auto &uvDesc = proto.upvalues[i];
      if (uvDesc.isLocal) {
        closure->upvalues.push_back(captureUpvalue(&slots[uvDesc.index]));
      } else {
        closure->upvalues.push_back(frame->closure->upvalues[uvDesc.index]);
      }
    }

    unprotect(1);
    slots[A] = Value::object(closure);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CLOSE_UPVALUE) {
    uint8_t A = GETARG_A(instruction);
    closeUpvalues(&slots[A]);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_ADD) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (b.isInt() && c.isInt()) {
      slots[A] = Value::integer(b.asInt() + c.asInt());
    } else if (b.isNumber() && c.isNumber()) {
      double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
      slots[A] = Value::number(left + right);
    } else if (b.isString() || c.isString()) {
      std::string s1 = b.toString();
      std::string s2 = c.toString();
      StringObject *result = allocateString(s1 + s2);
      slots[A] = Value::object(result);
    } else {
      runtimeError("Operands must be numbers or strings");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SUB) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (b.isInt() && c.isInt()) {
      slots[A] = Value::integer(b.asInt() - c.asInt());
    } else if (b.isNumber() && c.isNumber()) {
      double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
      slots[A] = Value::number(left - right);
    } else {
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_MUL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (b.isInt() && c.isInt()) {
      slots[A] = Value::integer(b.asInt() * c.asInt());
    } else if (b.isNumber() && c.isNumber()) {
      double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
      slots[A] = Value::number(left * right);
    } else {
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_DIV) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isNumber() || !c.isNumber()) {
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }

    double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
    double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();

    if (right == 0.0) {
      runtimeError("Division by zero");
      return InterpretResult::RUNTIME_ERROR;
    }

    if (b.isInt() && c.isInt()) {
      slots[A] = Value::integer(b.asInt() / c.asInt());
    } else {
      slots[A] = Value::number(left / right);
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_MOD) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isInt() || !c.isInt()) {
      runtimeError("Modulo requires integer operands");
      return InterpretResult::RUNTIME_ERROR;
    }

    int64_t right = c.asInt();
    if (right == 0) {
      runtimeError("Modulo by zero");
      return InterpretResult::RUNTIME_ERROR;
    }

    slots[A] = Value::integer(b.asInt() % right);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_UNM) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    Value b = slots[B];

    if (b.isInt()) {
      slots[A] = Value::integer(-b.asInt());
    } else if (b.isFloat()) {
      slots[A] = Value::number(-b.asFloat());
    } else {
      runtimeError("Operand must be a number");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_JMP) {
    int32_t sBx = GETARG_sBx(instruction);
    frame->ip += sBx;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EQ) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    bool equal = valuesEqual(slots[A], slots[B]);
    if (equal != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LT) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value a = slots[A];
    Value b = slots[B];

    bool result = false;
    if (a.isInt() && b.isInt()) {
      result = a.asInt() < b.asInt();
    } else if (a.isNumber() && b.isNumber()) {
      double left = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
      double right = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      result = left < right;
    } else {
      runtimeError("Cannot compare non-numeric types");
      return InterpretResult::RUNTIME_ERROR;
    }

    if (result != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LE) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value a = slots[A];
    Value b = slots[B];

    bool result = false;
    if (a.isInt() && b.isInt()) {
      result = a.asInt() <= b.asInt();
    } else if (a.isNumber() && b.isNumber()) {
      double left = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
      double right = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      result = left <= right;
    } else {
      runtimeError("Cannot compare non-numeric types");
      return InterpretResult::RUNTIME_ERROR;
    }

    if (result != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_TEST) {
    uint8_t A = GETARG_A(instruction);
    uint8_t C = GETARG_C(instruction);
    bool isTruthy = slots[A].isTruthy();
    if (isTruthy != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CALL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    int argCount = B - 1;
    int expectedResults = C - 1;

    Value callee = slots[A];

    if (callee.isClosure()) {
      Closure *closure = static_cast<Closure *>(callee.asGC());
      const Prototype *proto = closure->proto;

      if (argCount != proto->numParams && !proto->isVararg) {
        runtimeError("Function '%s' expects %d arguments, got %d", proto->name.c_str(),
                     proto->numParams, argCount);
        return InterpretResult::RUNTIME_ERROR;
      }

      if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
        runtimeError("Stack overflow");
        return InterpretResult::RUNTIME_ERROR;
      }

      int newSlotsBase = frame->slotsBase + A + 1;
      int neededStack = newSlotsBase + proto->maxStackSize;

      fiber->ensureStack(neededStack);
      fiber->ensureFrames(1);
      REFRESH_CACHE();

      Value *argsStart = stackBase + newSlotsBase;
      Value *targetStackTop = argsStart + proto->maxStackSize;

      for (Value *p = argsStart + argCount; p < targetStackTop; ++p) {
        *p = Value::nil();
      }

      STACK_TOP = targetStackTop;

      CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();
      newFrame->expectedResults = expectedResults;
      newFrame->slotsBase = newSlotsBase;
      newFrame->returnBase = frame->slotsBase + A;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;
      slots = stackBase + frame->slotsBase;

    } else if (callee.isNativeFunc()) {
      NativeFunction *native = static_cast<NativeFunction *>(callee.asGC());

      if (native->arity != -1 && argCount != native->arity) {
        runtimeError("Native function '%s' expects %d arguments, got %d", native->name.c_str(),
                     native->arity, argCount);
        return InterpretResult::RUNTIME_ERROR;
      }

      hasNativeMultiReturn_ = false;
      nativeMultiReturn_.clear();

      Value *argsStart = &slots[A + 1];
      Value result = native->function(this, native->receiver, argCount, argsStart);

      if (yieldPending_) {
        yieldPending_ = false;
        return InterpretResult::OK;
      }

      if (fiber != currentFiber_) {

        fiber = currentFiber_;
        frame = &fiber->frames[fiber->frameCount - 1];
        REFRESH_SLOTS();
        SPT_DISPATCH();
      }

      if (hasError_ && !pcallStack_.empty()) {
        return InterpretResult::RUNTIME_ERROR;
      }

      if (hasNativeMultiReturn_) {
        int returnCount = static_cast<int>(nativeMultiReturn_.size());

        if (expectedResults == -1) {
          for (int i = 0; i < returnCount; ++i) {
            slots[A + i] = nativeMultiReturn_[i];
          }
        } else if (expectedResults > 0) {
          for (int i = 0; i < returnCount && i < expectedResults; ++i) {
            slots[A + i] = nativeMultiReturn_[i];
          }
          for (int i = returnCount; i < expectedResults; ++i) {
            slots[A + i] = Value::nil();
          }
        } else {
          if (returnCount > 0) {
            slots[A] = nativeMultiReturn_[0];
          }
        }

        hasNativeMultiReturn_ = false;
        nativeMultiReturn_.clear();
      } else {
        slots[A] = result;
      }

    } else {
      runtimeError("Attempt to call a non-function value of type '%s'", callee.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_INVOKE) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value receiver = slots[A];
    int totalArgs = B;
    int userArgCount = totalArgs - 1;

    const auto &methodNameConst = frame->closure->proto->constants[C];
    if (!std::holds_alternative<std::string>(methodNameConst)) {
      runtimeError("OP_INVOKE: method name constant must be string");
      return InterpretResult::RUNTIME_ERROR;
    }
    const std::string &methodName = std::get<std::string>(methodNameConst);

    Value method = Value::nil();
    Value *argsStart = &slots[A + 1];

    if (receiver.isList() || receiver.isMap() || receiver.isString() || receiver.isFiber()) {
      Value directResult;
      if (StdlibDispatcher::invokeMethod(this, receiver, methodName, userArgCount, argsStart,
                                         directResult)) {

        if (yieldPending_) {
          yieldPending_ = false;
          return InterpretResult::OK;
        }

        if (fiber != currentFiber_) {

          fiber = currentFiber_;
          frame = &fiber->frames[fiber->frameCount - 1];
          REFRESH_SLOTS();
          SPT_DISPATCH();
        }
        slots[A] = directResult;
        SPT_DISPATCH();
      }

      Value propertyValue;
      if (StdlibDispatcher::getProperty(this, receiver, methodName, propertyValue)) {
        if (propertyValue.isNativeFunc()) {
          method = propertyValue;
        } else {
          runtimeError("'%s.%s' is a property, not a method", receiver.typeName(),
                       methodName.c_str());
          return InterpretResult::RUNTIME_ERROR;
        }
      } else if (receiver.isMap()) {
        MapObject *map = static_cast<MapObject *>(receiver.asGC());
        StringObject *key = allocateString(methodName);
        method = map->get(Value::object(key));
      }

      if (method.isNil()) {
        runtimeError("Type '%s' has no method '%s'", receiver.typeName(), methodName.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    } else if (receiver.isInstance()) {
      Instance *instance = static_cast<Instance *>(receiver.asGC());
      method = instance->getField(methodName);

      if (method.isNil() && instance->klass) {
        auto it = instance->klass->methods.find(methodName);
        if (it != instance->klass->methods.end()) {
          method = it->second;
        }
      }

      if (method.isNil()) {
        runtimeError("Instance has no method '%s'", methodName.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    } else if (receiver.isClass()) {
      ClassObject *klass = static_cast<ClassObject *>(receiver.asGC());

      auto itStatic = klass->statics.find(methodName);
      if (itStatic != klass->statics.end()) {
        method = itStatic->second;
      }

      if (method.isNil()) {
        auto it = klass->methods.find(methodName);
        if (it != klass->methods.end()) {
          method = it->second;
        }
      }

      if (method.isNil()) {
        runtimeError("Class '%s' has no method '%s'", klass->name.c_str(), methodName.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    } else {
      runtimeError("Cannot invoke method '%s' on type '%s'", methodName.c_str(),
                   receiver.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }

    if (method.isClosure()) {
      Closure *closure = static_cast<Closure *>(method.asGC());
      const Prototype *proto = closure->proto;

      int totalArgsProvided = B;
      bool dropThis = false;

      if (proto->needsReceiver) {
        if (!proto->isVararg && totalArgsProvided != proto->numParams) {
          runtimeError("Method '%s' expects %d arguments (including 'this'), got %d",
                       methodName.c_str(), proto->numParams, totalArgsProvided);
          return InterpretResult::RUNTIME_ERROR;
        }
        dropThis = false;
      } else {
        if (!proto->isVararg && totalArgsProvided != (proto->numParams + 1)) {
          runtimeError("Method '%s' expects %d arguments, got %d", methodName.c_str(),
                       proto->numParams, totalArgsProvided - 1);
          return InterpretResult::RUNTIME_ERROR;
        }
        dropThis = true;
      }

      if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
        runtimeError("Stack overflow");
        return InterpretResult::RUNTIME_ERROR;
      }

      for (int i = totalArgsProvided - 1; i >= 0; --i) {
        slots[A + 1 + i] = slots[A + i];
      }

      int newSlotsBase = frame->slotsBase + A + (dropThis ? 2 : 1);
      int neededStack = newSlotsBase + proto->maxStackSize;

      fiber->ensureStack(neededStack);
      fiber->ensureFrames(1);
      REFRESH_CACHE();

      Value *newSlots = stackBase + newSlotsBase;
      Value *targetStackTop = newSlots + proto->maxStackSize;

      int argsPushed = dropThis ? (totalArgsProvided - 1) : totalArgsProvided;

      for (Value *p = newSlots + argsPushed; p < targetStackTop; ++p) {
        *p = Value::nil();
      }

      STACK_TOP = targetStackTop;

      CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();
      newFrame->expectedResults = 1;
      newFrame->slotsBase = newSlotsBase;
      newFrame->returnBase = frame->slotsBase + A;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;
      slots = stackBase + frame->slotsBase;
    } else if (method.isNativeFunc()) {
      NativeFunction *native = static_cast<NativeFunction *>(method.asGC());

      protect(method);

      if (native->arity != -1 && userArgCount != native->arity) {
        runtimeError("Native method '%s' expects %d arguments, got %d", native->name.c_str(),
                     native->arity, userArgCount);
        return InterpretResult::RUNTIME_ERROR;
      }

      hasNativeMultiReturn_ = false;
      nativeMultiReturn_.clear();

      Value result = native->function(this, receiver, userArgCount, argsStart);

      unprotect(1);

      if (yieldPending_) {
        yieldPending_ = false;
        return InterpretResult::OK;
      }

      if (fiber != currentFiber_) {

        fiber = currentFiber_;
        frame = &fiber->frames[fiber->frameCount - 1];
        REFRESH_SLOTS();
        SPT_DISPATCH();
      }

      if (hasError_ && !pcallStack_.empty()) {
        return InterpretResult::RUNTIME_ERROR;
      }

      if (hasNativeMultiReturn_) {
        slots[A] = nativeMultiReturn_.empty() ? Value::nil() : nativeMultiReturn_[0];
        hasNativeMultiReturn_ = false;
        nativeMultiReturn_.clear();
      } else {
        slots[A] = result;
      }
    } else {
      runtimeError("'%s.%s' is not callable", receiver.typeName(), methodName.c_str());
      return InterpretResult::RUNTIME_ERROR;
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_RETURN) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    if (fiber->deferTop > frame->deferBase) {
      invokeDefers(frame);

      if (hasError_)
        return InterpretResult::RUNTIME_ERROR;

      REFRESH_CACHE();
    }

    int returnCount = (B >= 1) ? B - 1 : 0;

    Value *returnValues = slots + A;
    int expectedResults = frame->expectedResults;

    bool isRootFrame = (FRAME_COUNT == 1);
    bool isModuleExit = (exitFrameCount_ > 0 && FRAME_COUNT == exitFrameCount_);

    Value *destSlot = nullptr;
    if (!isRootFrame && !isModuleExit) {
      destSlot = stackBase + frame->returnBase;
    }

    closeUpvalues(slots);

    FRAME_COUNT--;

    if (isRootFrame) {
      lastModuleResult_ = (returnCount > 0) ? returnValues[0] : Value::nil();

      if (!pcallStack_.empty()) {
        nativeMultiReturn_.clear();
        for (int i = 0; i < returnCount; ++i) {
          nativeMultiReturn_.push_back(returnValues[i]);
        }
        hasNativeMultiReturn_ = true;
      }

      fiber->state = FiberState::DONE;

      fiber->yieldValue = (returnCount > 0) ? returnValues[0] : Value::nil();
      lastModuleResult_ = fiber->yieldValue;

      if (fiber->caller) {
        FiberObject *caller = fiber->caller;
        fiber->caller = nullptr;
        currentFiber_ = caller;
        caller->state = FiberState::RUNNING;

        return InterpretResult::OK;
      }

      unprotect(1);
      return InterpretResult::OK;
    }

    if (isModuleExit) {
      lastModuleResult_ = (returnCount > 0) ? returnValues[0] : Value::nil();
      return InterpretResult::OK;
    }

    frame = &fiber->frames[FRAME_COUNT - 1];
    slots = stackBase + frame->slotsBase;

    if (expectedResults == -1) {
      for (int i = 0; i < returnCount; ++i) {
        destSlot[i] = returnValues[i];
      }
      STACK_TOP = destSlot + returnCount;
    } else {
      for (int i = 0; i < expectedResults; ++i) {
        if (i < returnCount) {
          destSlot[i] = returnValues[i];
        } else {
          destSlot[i] = Value::nil();
        }
      }
      STACK_TOP = slots + frame->closure->proto->maxStackSize;
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_IMPORT) {
    uint8_t A = GETARG_A(instruction);
    uint32_t Bx = GETARG_Bx(instruction);
    const auto &moduleNameConst = frame->closure->proto->constants[Bx];

    if (!std::holds_alternative<std::string>(moduleNameConst)) {
      runtimeError("Module name must be a string constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &moduleName = std::get<std::string>(moduleNameConst);
    const char *currentPath = frame->closure->proto->source.c_str();

    Value exportsTable = moduleManager_->loadModule(moduleName, currentPath);

    if (FRAME_COUNT == 0 || hasError_) {
      return InterpretResult::RUNTIME_ERROR;
    }

    frame = &FRAMES[FRAME_COUNT - 1];
    REFRESH_SLOTS();

    if (exportsTable.isMap()) {
      MapObject *errorCheck = static_cast<MapObject *>(exportsTable.asGC());
      StringObject *errorKey = allocateString("error");
      Value errorFlag = errorCheck->get(Value::object(errorKey));

      if (errorFlag.isBool() && errorFlag.asBool()) {
        StringObject *msgKey = allocateString("message");
        Value msgVal = errorCheck->get(Value::object(msgKey));
        const char *errorMsg = msgVal.isString()
                                   ? static_cast<StringObject *>(msgVal.asGC())->data.c_str()
                                   : "Module load failed";
        runtimeError("Import error: %s", errorMsg);
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    slots[A] = exportsTable;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_IMPORT_FROM) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    const auto &moduleNameConst = frame->closure->proto->constants[B];
    const auto &symbolNameConst = frame->closure->proto->constants[C];

    if (!std::holds_alternative<std::string>(moduleNameConst) ||
        !std::holds_alternative<std::string>(symbolNameConst)) {
      runtimeError("Module and symbol names must be string constants");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &moduleName = std::get<std::string>(moduleNameConst);
    const std::string &symbolName = std::get<std::string>(symbolNameConst);
    const char *currentPath = frame->closure->proto->source.c_str();

    Value exportsTable = moduleManager_->loadModule(moduleName, currentPath);

    if (FRAME_COUNT == 0 || hasError_) {
      return InterpretResult::RUNTIME_ERROR;
    }

    frame = &FRAMES[FRAME_COUNT - 1];
    REFRESH_SLOTS();

    if (exportsTable.isMap()) {
      MapObject *exports = static_cast<MapObject *>(exportsTable.asGC());

      StringObject *errorKey = allocateString("error");
      Value errorFlag = exports->get(Value::object(errorKey));

      if (errorFlag.isBool() && errorFlag.asBool()) {
        StringObject *msgKey = allocateString("message");
        Value msgVal = exports->get(Value::object(msgKey));
        const char *errorMsg = msgVal.isString()
                                   ? static_cast<StringObject *>(msgVal.asGC())->data.c_str()
                                   : "Module load failed";
        runtimeError("Import error: %s", errorMsg);
        return InterpretResult::RUNTIME_ERROR;
      }

      StringObject *symKey = allocateString(symbolName);
      Value symbolValue = exports->get(Value::object(symKey));
      slots[A] = symbolValue;
    } else {
      slots[A] = Value::nil();
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EXPORT) { SPT_DISPATCH(); }

  SPT_OPCODE(OP_DEFER) {
    uint8_t A = GETARG_A(instruction);
    Value deferClosure = slots[A];

    fiber->ensureDefers(1);

    fiber->deferStack[fiber->deferTop++] = deferClosure;

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_ADDI) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    int8_t sC = static_cast<int8_t>(GETARG_C(instruction));
    Value b = slots[B];

    if (b.isInt()) {
      slots[A] = Value::integer(b.asInt() + sC);
    } else if (b.isFloat()) {
      slots[A] = Value::number(b.asFloat() + sC);
    } else {
      runtimeError("ADDI requires numeric operand");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EQK) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    const auto &constant = frame->closure->proto->constants[B];
    Value kVal;
    std::visit(
        [&](auto &&arg) {
          using T = std::decay_t<decltype(arg)>;
          if constexpr (std::is_same_v<T, std::nullptr_t>) {
            kVal = Value::nil();
          } else if constexpr (std::is_same_v<T, bool>) {
            kVal = Value::boolean(arg);
          } else if constexpr (std::is_same_v<T, int64_t>) {
            kVal = Value::integer(arg);
          } else if constexpr (std::is_same_v<T, double>) {
            kVal = Value::number(arg);
          } else if constexpr (std::is_same_v<T, std::string>) {
            kVal = Value::object(allocateString(arg));
          }
        },
        constant);

    bool equal = valuesEqual(slots[A], kVal);
    if (equal != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EQI) {
    uint8_t A = GETARG_A(instruction);
    int8_t sB = static_cast<int8_t>(GETARG_B(instruction));
    uint8_t C = GETARG_C(instruction);
    Value a = slots[A];
    bool equal = false;
    if (a.isInt()) {
      equal = (a.asInt() == sB);
    } else if (a.isFloat()) {
      equal = (a.asFloat() == static_cast<double>(sB));
    }
    if (equal != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LTI) {
    uint8_t A = GETARG_A(instruction);
    int8_t sB = static_cast<int8_t>(GETARG_B(instruction));
    uint8_t C = GETARG_C(instruction);
    Value a = slots[A];
    bool result = false;
    if (a.isInt()) {
      result = (a.asInt() < sB);
    } else if (a.isFloat()) {
      result = (a.asFloat() < static_cast<double>(sB));
    }
    if (result != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LEI) {
    uint8_t A = GETARG_A(instruction);
    int8_t sB = static_cast<int8_t>(GETARG_B(instruction));
    uint8_t C = GETARG_C(instruction);
    Value a = slots[A];
    bool result = false;
    if (a.isInt()) {
      result = (a.asInt() <= sB);
    } else if (a.isFloat()) {
      result = (a.asFloat() <= static_cast<double>(sB));
    }
    if (result != (C != 0)) {
      frame->ip++;
    }
    SPT_DISPATCH();
  }

#define CHECK_NUM(val, msg)                                                                        \
  if (!val.isNumber()) {                                                                           \
    runtimeError(msg);                                                                             \
    return InterpretResult::RUNTIME_ERROR;                                                         \
  }

  SPT_OPCODE(OP_FORPREP) {
    uint8_t A = GETARG_A(instruction);
    int32_t sBx = GETARG_sBx(instruction);

    Value &idx = slots[A];
    Value limit = slots[A + 1];
    Value step = slots[A + 2];

    CHECK_NUM(idx, "'for' loop initial value must be a number");
    CHECK_NUM(limit, "'for' loop limit must be a number");
    CHECK_NUM(step, "'for' loop step must be a number");

    if (idx.isInt() && step.isInt() && limit.isInt()) {
      idx = Value::integer(idx.asInt() - step.asInt());
    } else {
      double nIdx = valueToNum(idx);
      double nStep = valueToNum(step);
      idx = Value::number(nIdx - nStep);
    }

    frame->ip += sBx;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_FORLOOP) {
    uint8_t A = GETARG_A(instruction);
    int32_t sBx = GETARG_sBx(instruction);

    Value &idx = slots[A];
    Value limit = slots[A + 1];
    Value step = slots[A + 2];

    CHECK_NUM(idx, "'for' loop variable corrupted (must be a number)");
    CHECK_NUM(step, "'for' loop step corrupted (must be a number)");
    CHECK_NUM(limit, "'for' loop limit corrupted (must be a number)");

    if (idx.isInt() && step.isInt() && limit.isInt()) {
      int64_t iStep = step.asInt();
      int64_t iIdx = idx.asInt() + iStep;
      idx = Value::integer(iIdx);

      int64_t iLimit = limit.asInt();

      if (iStep > 0) {
        if (iIdx <= iLimit)
          frame->ip += sBx;
      } else {
        if (iIdx >= iLimit)
          frame->ip += sBx;
      }
    } else {
      double nStep = valueToNum(step);
      double nIdx = valueToNum(idx) + nStep;
      double nLimit = valueToNum(limit);

      idx = Value::number(nIdx);

      if (nStep > 0) {
        if (nIdx <= nLimit)
          frame->ip += sBx;
      } else {
        if (nIdx >= nLimit)
          frame->ip += sBx;
      }
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADI) {
    uint8_t A = GETARG_A(instruction);
    int32_t sBx = GETARG_sBx(instruction);
    slots[A] = Value::integer(sBx);
    SPT_DISPATCH();
  }

#undef CHECK_NUM
  SPT_DISPATCH_LOOP_END()

#undef FIBER
#undef STACK_TOP
#undef FRAMES
#undef FRAME_COUNT
#undef REFRESH_SLOTS
}

FiberObject *VM::allocateFiber(Closure *closure) {
  FiberObject *fiber = gc_.allocate<FiberObject>();
  fiber->closure = closure;
  fiber->state = FiberState::NEW;
  return fiber;
}

void VM::initFiberForCall(FiberObject *fiber, Value arg) {
  fiber->stackTop = fiber->stack.data();
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
  frame->slotsBase = 0;
  frame->expectedResults = 1;
  frame->returnBase = 0;
  frame->deferBase = fiber->deferTop;

  Value *frameStart = fiber->stack.data();
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

    const uint32_t *prevIP = frame->ip - 1;
    uint32_t instruction = *prevIP;
    OpCode op = GET_OPCODE(instruction);

    if (op == OpCode::OP_CALL || op == OpCode::OP_INVOKE) {
      uint8_t A = GETARG_A(instruction);

      Value *frameSlots = fiber->stack.data() + frame->slotsBase;
      frameSlots[A] = arg;
    } else {

      *fiber->stackTop++ = arg;
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

void VM::invokeDefers(CallFrame *frame) {
  FiberObject *fiber = currentFiber_;

  while (fiber->deferTop > frame->deferBase) {

    Value deferVal = fiber->deferStack[--fiber->deferTop];

    if (deferVal.isClosure()) {
      Closure *closure = static_cast<Closure *>(deferVal.asGC());

      call(closure, 0);
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

void VM::defineGlobal(const std::string &name, Value value) { globals_[name] = value; }

Value VM::getGlobal(const std::string &name) {
  auto it = globals_.find(name);
  return (it != globals_.end()) ? it->second : Value::nil();
}

void VM::setGlobal(const std::string &name, Value value) { globals_[name] = value; }

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
      errorMsg = static_cast<StringObject *>(errorValue.asGC())->data;
    } else {
      errorMsg = "error: " + errorValue.toString();
    }
    runtimeError("%s", errorMsg.c_str());
  }
}

void VM::runtimeError(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  std::string message = buffer;
  message += "\n----------------\nCall stack:";

  FiberObject *fiber = currentFiber_;
  for (int i = fiber->frameCount - 1; i >= 0; --i) {
    CallFrame &frame = fiber->frames[i];
    const Prototype *proto = frame.closure->proto;
    size_t instruction = frame.ip - proto->code.data() - 1;
    int line = getLine(proto, instruction);
    message += "\n  [line " + std::to_string(line) + "] in ";
    message += proto->name.empty() ? "<script>" : proto->name + "()";
  }

  if (errorHandler_) {
    errorHandler_(message, 0);
  } else {
    fprintf(stderr, "\n[Runtime Error]\n%s\n\n", message.c_str());
  }

  resetStack();
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

std::string VM::getStackTrace() {
  std::string trace = "Call stack:";
  FiberObject *fiber = currentFiber_;

  for (int i = fiber->frameCount - 1; i >= 0; --i) {
    CallFrame &frame = fiber->frames[i];
    const Prototype *proto = frame.closure->proto;
    size_t instruction = frame.ip - proto->code.data() - 1;
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

StringObject *VM::allocateString(const std::string &str) {
  auto it = strings_.find(str);
  if (it != strings_.end()) {
    return it->second;
  }

  StringObject *strObj = gc_.allocate<StringObject>();
  strObj->data = str;
  strObj->hash = static_cast<uint32_t>(std::hash<std::string>{}(str));
  strings_[str] = strObj;
  return strObj;
}

Closure *VM::allocateClosure(const Prototype *proto) {
  Closure *closure = gc_.allocate<Closure>();
  closure->proto = proto;
  return closure;
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
  printf("Stack range: [%p, %p)\n", static_cast<const void *>(fiber->stack.data()),
         static_cast<const void *>(fiber->stackTop));

  for (const Value *slot = fiber->stack.data(); slot < fiber->stackTop; ++slot) {
    size_t offset = slot - fiber->stack.data();
    printf("  [%04zu] %s\n", offset, slot->toString().c_str());
  }
  printf("==================\n\n");
}

void VM::dumpGlobals() const {
  printf("\n=== Globals ===\n");
  for (const auto &[name, value] : globals_) {
    printf("  %-20s = %s\n", name.c_str(), value.toString().c_str());
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
      size_t instruction = frame.ip - proto->code.data() - 1;
      out_info->currentLine = getLine(proto, instruction);
    } break;
    }
  }
  return 1;
}

bool VM::hotReload(const std::string &moduleName, const CompiledChunk &newChunk) {
  modules_[moduleName] = newChunk;

  for (auto &[name, value] : globals_) {
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

} // namespace spt