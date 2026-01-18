#include "VM.h"

#include "Fiber.h"
#include "NativeBinding.h"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace spt {
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
          double val = v.asFloat();

          constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
          constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
          if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
              std::isinf(val)) {
            this->throwError(Value::object(this->allocateString("toInt: value out of range")));
            return Value::nil();
          }
          return Value::integer(static_cast<int64_t>(val));
        } else if (v.isString()) {
          StringObject *str = static_cast<StringObject *>(v.asGC());
          char *endptr = nullptr;
          errno = 0;
          long long result = std::strtoll(str->c_str(), &endptr, 10);
          if (endptr == str->c_str() || errno == ERANGE) {
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
          double result = std::strtod(str->c_str(), &endptr);
          if (endptr == str->c_str() || errno == ERANGE) {
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
          return Value::integer(static_cast<int64_t>(str->length));
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

          if (val == INT64_MIN) {

            return Value::number(static_cast<double>(INT64_MAX) + 1.0);
          }
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
          double val = std::floor(v.asFloat());

          constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
          constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
          if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
              std::isinf(val)) {
            return Value::number(val);
          }
          return Value::integer(static_cast<int64_t>(val));
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
          double val = std::ceil(v.asFloat());

          constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
          constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
          if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
              std::isinf(val)) {
            return Value::number(val);
          }
          return Value::integer(static_cast<int64_t>(val));
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
          double val = std::round(v.asFloat());

          constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
          constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
          if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
              std::isinf(val)) {

            return Value::number(val);
          }
          return Value::integer(static_cast<int64_t>(val));
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

        if (!v.isNumber()) {
          vm->throwError(Value::object(vm->allocateString("sqrt: argument must be a number")));
          return Value::nil();
        }
        double val = v.isInt() ? static_cast<double>(v.asInt()) : v.asFloat();
        return Value::number(std::sqrt(val));
      },
      1);

  registerNative(
      "pow",
      [](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::number(0.0);

        if (!args[0].isNumber() || !args[1].isNumber()) {
          vm->throwError(Value::object(vm->allocateString("pow: arguments must be numbers")));
          return Value::nil();
        }
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
        if (!a.isNumber() || !b.isNumber()) {
          vm->throwError(Value::object(vm->allocateString("min: arguments must be numbers")));
          return Value::nil();
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
        if (!a.isNumber() || !b.isNumber()) {
          vm->throwError(Value::object(vm->allocateString("max: arguments must be numbers")));
          return Value::nil();
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
        if (str->length == 0)
          return Value::integer(0);
        return Value::integer(static_cast<int64_t>(static_cast<unsigned char>(str->chars()[0])));
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
            msg = static_cast<StringObject *>(args[1].asGC())->str();
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

        size_t savedStackTopOffset = fiber->stackTop - fiber->stack;
        UpValue *savedOpenUpvalues = fiber->openUpvalues;
        int savedDeferTop = fiber->deferTop;
        bool savedHasError = this->hasError_;
        Value savedErrorValue = this->errorValue_;

        this->hasError_ = false;
        this->errorValue_ = Value::nil();

        ProtectedCallContext ctx;
        ctx.fiber = fiber;
        ctx.frameCount = savedFrameCount;
        ctx.stackTop = fiber->stackTop;
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

            int slotsBaseOffset = static_cast<int>((callBase + 1) - fiber->stack);

            fiber->ensureStack(proto->maxStackSize);
            fiber->ensureFrames(1);

            Value *newSlots = fiber->stack + slotsBaseOffset;
            for (Value *p = fiber->stackTop; p < newSlots + proto->maxStackSize; ++p) {
              *p = Value::nil();
            }
            fiber->stackTop = newSlots + proto->maxStackSize;

            CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
            newFrame->closure = closure;
            newFrame->ip = proto->code.data();
            newFrame->expectedResults = -1;
            newFrame->slots = newSlots;
            newFrame->returnTo = callBase;
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

        fiber->stackTop = fiber->stack + savedStackTopOffset;

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
          this->closeUpvalues(fiber->stack + savedStackTopOffset);

          while (fiber->frameCount > savedFrameCount) {

            CallFrame *currentFrame = &fiber->frames[fiber->frameCount - 1];

            this->invokeDefers(currentFrame->deferBase);

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
      "apply",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1) {
          this->throwError(Value::object(this->allocateString(
              "apply: expected at least 1 argument (fn, [args], [receiver])")));
          return Value::nil();
        }

        Value func = args[0];
        Value argsList = (argc >= 2) ? args[1] : Value::nil();
        Value funcReceiver = (argc >= 3) ? args[2] : Value::nil();

        if (!func.isClosure() && !func.isNativeFunc()) {
          this->throwError(
              Value::object(this->allocateString("apply: first argument must be a function")));
          return Value::nil();
        }

        if (!argsList.isNil() && !argsList.isList()) {
          this->throwError(
              Value::object(this->allocateString("apply: second argument must be a list or nil")));
          return Value::nil();
        }

        std::vector<Value> funcArgs;
        bool hasReceiver = !funcReceiver.isNil();

        if (argsList.isList()) {
          ListObject *argList = static_cast<ListObject *>(argsList.asGC());
          if (hasReceiver) {
            funcArgs.reserve(argList->elements.size() + 1);
            funcArgs.push_back(funcReceiver);
            funcArgs.insert(funcArgs.end(), argList->elements.begin(), argList->elements.end());
          } else {
            funcArgs = argList->elements;
          }
        } else {
          if (hasReceiver) {
            funcArgs.push_back(funcReceiver);
          }
        }

        int funcArgCount = static_cast<int>(funcArgs.size());
        FiberObject *fiber = currentFiber_;

        Value *callBase = fiber->stackTop;
        *fiber->stackTop++ = func;
        for (int i = 0; i < funcArgCount; ++i) {
          *fiber->stackTop++ = funcArgs[i];
        }

        Value result = Value::nil();

        if (func.isClosure()) {
          Closure *closure = static_cast<Closure *>(func.asGC());
          const Prototype *proto = closure->proto;

          if (!proto->isVararg && funcArgCount != proto->numParams) {
            fiber->stackTop = callBase;
            this->throwError(Value::object(
                this->allocateString("apply: function expects " + std::to_string(proto->numParams) +
                                     " arguments, got " + std::to_string(funcArgCount))));
            return Value::nil();
          }

          if (fiber->frameCount >= FiberObject::MAX_FRAMES) {
            fiber->stackTop = callBase;
            this->throwError(Value::object(this->allocateString("apply: stack overflow")));
            return Value::nil();
          }

          int slotsBaseOffset = static_cast<int>((callBase + 1) - fiber->stack);

          fiber->ensureStack(proto->maxStackSize);
          fiber->ensureFrames(1);

          Value *newSlots = fiber->stack + slotsBaseOffset;
          for (Value *p = fiber->stackTop; p < newSlots + proto->maxStackSize; ++p) {
            *p = Value::nil();
          }
          fiber->stackTop = newSlots + proto->maxStackSize;

          CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
          newFrame->closure = closure;
          newFrame->ip = proto->code.data();
          newFrame->expectedResults = 1;
          newFrame->slots = newSlots;
          newFrame->returnTo = callBase;
          newFrame->deferBase = fiber->deferTop;

          int savedExitFrameCount = this->exitFrameCount_;
          this->exitFrameCount_ = fiber->frameCount;

          InterpretResult runResult = this->run();

          this->exitFrameCount_ = savedExitFrameCount;

          if (runResult == InterpretResult::OK && !this->hasError_) {
            if (this->hasNativeMultiReturn_) {
              if (!this->nativeMultiReturn_.empty()) {
                result = this->nativeMultiReturn_[0];
              }
              this->hasNativeMultiReturn_ = false;
            } else {
              result = this->lastModuleResult_;
            }
          }

        } else if (func.isNativeFunc()) {
          NativeFunction *native = static_cast<NativeFunction *>(func.asGC());

          int expectedArity = native->arity;
          int argsToCheck = funcArgCount;
          Value actualReceiver = native->receiver;
          Value *actualArgs = funcArgCount > 0 ? funcArgs.data() : nullptr;

          if (hasReceiver) {
            actualReceiver = funcReceiver;
            argsToCheck = funcArgCount - 1;
            actualArgs = funcArgCount > 1 ? funcArgs.data() + 1 : nullptr;
          }

          if (expectedArity != -1 && argsToCheck != expectedArity) {
            fiber->stackTop = callBase;
            this->throwError(Value::object(this->allocateString(
                "apply: native function expects " + std::to_string(expectedArity) +
                " arguments, got " + std::to_string(argsToCheck))));
            return Value::nil();
          }

          this->hasNativeMultiReturn_ = false;
          Value nativeResult = native->function(this, actualReceiver, argsToCheck, actualArgs);

          if (!this->hasError_) {
            if (this->hasNativeMultiReturn_) {
              if (!this->nativeMultiReturn_.empty()) {
                result = this->nativeMultiReturn_[0];
              }
              this->hasNativeMultiReturn_ = false;
            } else {
              result = nativeResult;
            }
          }

          fiber->stackTop = callBase;
        }

        return result;
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

  registerNative(
      "__iter_list",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::nil();
        Value listVal = args[0];
        Value idxVal = args[1];

        if (!listVal.isList())
          return Value::nil();
        ListObject *list = static_cast<ListObject *>(listVal.asGC());

        int64_t nextIdx = 0;
        if (idxVal.isNil()) {
          nextIdx = 0;
        } else if (idxVal.isInt()) {
          nextIdx = idxVal.asInt() + 1;
        } else {
          return Value::nil();
        }

        if (nextIdx >= 0 && nextIdx < static_cast<int64_t>(list->elements.size())) {

          vm->setNativeMultiReturn({Value::integer(nextIdx), list->elements[nextIdx]});

          return Value::integer(nextIdx);
        }

        return Value::nil();
      },
      2);

  registerNative(
      "__iter_map",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 2)
          return Value::nil();
        Value mapVal = args[0];
        Value keyVal = args[1];

        if (!mapVal.isMap())
          return Value::nil();
        MapObject *map = static_cast<MapObject *>(mapVal.asGC());

        auto &entries = map->entries;
        auto it = entries.end();

        if (keyVal.isNil()) {

          it = entries.begin();
        } else {

          it = entries.find(keyVal);
          if (it != entries.end()) {
            ++it;
          }
        }

        if (it != entries.end()) {
          vm->setNativeMultiReturn({it->first, it->second});
          return it->first;
        }

        return Value::nil();
      },
      2);

  registerNative(
      "pairs",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        if (argc < 1)
          return Value::nil();
        Value target = args[0];

        if (target.isList()) {
          Value iterFunc = vm->getGlobal("__iter_list");
          vm->setNativeMultiReturn({iterFunc, target, Value::integer(-1)});
          return iterFunc;
        } else if (target.isMap()) {
          Value iterFunc = vm->getGlobal("__iter_map");
          vm->setNativeMultiReturn({iterFunc, target, Value::nil()});
          return iterFunc;
        }

        vm->throwError(Value::object(vm->allocateString("pairs() expects a list or map")));
        return Value::nil();
      },
      1);
  registerNative(
      "clock",
      [this](VM *vm, Value receiver, int argc, Value *args) -> Value {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        return Value::integer(micros);
      },
      0);
}

} // namespace spt