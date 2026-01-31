#include "Fiber.h"
#include "VM.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace spt {
void VM::registerBuiltinFunctions() {
  registerNative(
      "print",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
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
        return 0;
      },
      -1);

  registerNative(
      "toInt",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result;
        if (argc < 1) {
          result = Value::integer(0);
        } else {
          Value v = args[0];
          if (v.isInt()) {
            result = v;
          } else if (v.isFloat()) {
            double val = v.asFloat();
            constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
            constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
            if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
                std::isinf(val)) {
              this->throwError(Value::object(this->allocateString("toInt: value out of range")));
              return 0;
            }
            result = Value::integer(static_cast<int64_t>(val));
          } else if (v.isString()) {
            StringObject *str = static_cast<StringObject *>(v.asGC());
            char *endptr = nullptr;
            errno = 0;
            long long r = std::strtoll(str->c_str(), &endptr, 10);
            if (endptr == str->c_str() || errno == ERANGE) {
              result = Value::integer(0);
            } else {
              result = Value::integer(static_cast<int64_t>(r));
            }
          } else if (v.isBool()) {
            result = Value::integer(v.asBool() ? 1 : 0);
          } else {
            result = Value::integer(0);
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "toFloat",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result;
        if (argc < 1) {
          result = Value::number(0.0);
        } else {
          Value v = args[0];
          if (v.isFloat()) {
            result = v;
          } else if (v.isInt()) {
            result = Value::number(static_cast<double>(v.asInt()));
          } else if (v.isString()) {
            StringObject *str = static_cast<StringObject *>(v.asGC());
            char *endptr = nullptr;
            errno = 0;
            double r = std::strtod(str->c_str(), &endptr);
            if (endptr == str->c_str() || errno == ERANGE) {
              result = Value::number(0.0);
            } else {
              result = Value::number(r);
            }
          } else if (v.isBool()) {
            result = Value::number(v.asBool() ? 1.0 : 0.0);
          } else {
            result = Value::number(0.0);
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "toString",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1) {
          vm->push(Value::object(this->allocateString("")));
        } else {
          vm->push(Value::object(this->allocateString(args[0].toString())));
        }
        return 1;
      },
      1);

  registerNative(
      "toBool",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1) {
          vm->push(Value::boolean(false));
        } else {
          vm->push(Value::boolean(args[0].isTruthy()));
        }
        return 1;
      },
      1);

  registerNative(
      "typeOf",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1) {
          vm->push(Value::object(this->allocateString("nil")));
        } else {
          vm->push(Value::object(this->allocateString(args[0].typeName())));
        }
        return 1;
      },
      1);

  registerNative(
      "len",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result = Value::integer(0);
        if (argc >= 1) {
          Value v = args[0];
          if (v.isString()) {
            StringObject *str = static_cast<StringObject *>(v.asGC());
            result = Value::integer(static_cast<int64_t>(str->length));
          } else if (v.isList()) {
            ListObject *list = static_cast<ListObject *>(v.asGC());
            result = Value::integer(static_cast<int64_t>(list->elements.size()));
          } else if (v.isMap()) {
            MapObject *map = static_cast<MapObject *>(v.asGC());
            result = Value::integer(static_cast<int64_t>(map->entries.size()));
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "abs",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result = Value::integer(0);
        if (argc >= 1) {
          Value v = args[0];
          if (v.isInt()) {
            int64_t val = v.asInt();
            if (val == INT64_MIN) {
              result = Value::number(static_cast<double>(INT64_MAX) + 1.0);
            } else {
              result = Value::integer(val < 0 ? -val : val);
            }
          } else if (v.isFloat()) {
            result = Value::number(std::abs(v.asFloat()));
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "floor",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result = Value::integer(0);
        if (argc >= 1) {
          Value v = args[0];
          if (v.isInt()) {
            result = v;
          } else if (v.isFloat()) {
            double val = std::floor(v.asFloat());
            constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
            constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
            if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
                std::isinf(val)) {
              result = Value::number(val);
            } else {
              result = Value::integer(static_cast<int64_t>(val));
            }
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "ceil",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result = Value::integer(0);
        if (argc >= 1) {
          Value v = args[0];
          if (v.isInt()) {
            result = v;
          } else if (v.isFloat()) {
            double val = std::ceil(v.asFloat());
            constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
            constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
            if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
                std::isinf(val)) {
              result = Value::number(val);
            } else {
              result = Value::integer(static_cast<int64_t>(val));
            }
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "round",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value result = Value::integer(0);
        if (argc >= 1) {
          Value v = args[0];
          if (v.isInt()) {
            result = v;
          } else if (v.isFloat()) {
            double val = std::round(v.asFloat());
            constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
            constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
            if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) ||
                std::isinf(val)) {
              result = Value::number(val);
            } else {
              result = Value::integer(static_cast<int64_t>(val));
            }
          }
        }
        vm->push(result);
        return 1;
      },
      1);

  registerNative(
      "sqrt",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1) {
          vm->push(Value::number(0.0));
          return 1;
        }
        Value v = args[0];
        if (!v.isNumber()) {
          vm->throwError(Value::object(vm->allocateString("sqrt: argument must be a number")));
          return 0;
        }
        double val = v.isInt() ? static_cast<double>(v.asInt()) : v.asFloat();
        vm->push(Value::number(std::sqrt(val)));
        return 1;
      },
      1);

  registerNative(
      "pow",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 2) {
          vm->push(Value::number(0.0));
          return 1;
        }
        if (!args[0].isNumber() || !args[1].isNumber()) {
          vm->throwError(Value::object(vm->allocateString("pow: arguments must be numbers")));
          return 0;
        }
        double base = args[0].isInt() ? static_cast<double>(args[0].asInt()) : args[0].asFloat();
        double exp = args[1].isInt() ? static_cast<double>(args[1].asInt()) : args[1].asFloat();
        vm->push(Value::number(std::pow(base, exp)));
        return 1;
      },
      2);

  registerNative(
      "min",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 2) {
          vm->push(Value::nil());
          return 1;
        }
        Value a = args[0];
        Value b = args[1];
        if (a.isInt() && b.isInt()) {
          vm->push(Value::integer(std::min(a.asInt(), b.asInt())));
          return 1;
        }
        if (!a.isNumber() || !b.isNumber()) {
          vm->throwError(Value::object(vm->allocateString("min: arguments must be numbers")));
          return 0;
        }
        double va = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
        double vb = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        vm->push(Value::number(std::min(va, vb)));
        return 1;
      },
      2);

  registerNative(
      "max",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 2) {
          vm->push(Value::nil());
          return 1;
        }
        Value a = args[0];
        Value b = args[1];
        if (a.isInt() && b.isInt()) {
          vm->push(Value::integer(std::max(a.asInt(), b.asInt())));
          return 1;
        }
        if (!a.isNumber() || !b.isNumber()) {
          vm->throwError(Value::object(vm->allocateString("max: arguments must be numbers")));
          return 0;
        }
        double va = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
        double vb = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        vm->push(Value::number(std::max(va, vb)));
        return 1;
      },
      2);

  registerNative(
      "char",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1 || !args[0].isInt()) {
          vm->push(Value::object(this->allocateString("")));
          return 1;
        }
        int64_t code = args[0].asInt();
        if (code < 0 || code > 127) {
          vm->push(Value::object(this->allocateString("")));
          return 1;
        }
        char c = static_cast<char>(code);
        vm->push(Value::object(this->allocateString(std::string(1, c))));
        return 1;
      },
      1);

  registerNative(
      "ord",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1 || !args[0].isString()) {
          vm->push(Value::integer(0));
          return 1;
        }
        StringObject *str = static_cast<StringObject *>(args[0].asGC());
        if (str->length == 0) {
          vm->push(Value::integer(0));
          return 1;
        }
        vm->push(Value::integer(static_cast<int64_t>(static_cast<unsigned char>(str->chars()[0]))));
        return 1;
      },
      1);

  registerNative(
      "range",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 2) {
          vm->push(Value::nil());
          return 1;
        }
        int64_t start = args[0].isInt() ? args[0].asInt() : 0;
        int64_t end = args[1].isInt() ? args[1].asInt() : 0;
        int64_t step = (argc >= 3 && args[2].isInt()) ? args[2].asInt() : 1;
        if (step == 0) {
          vm->push(Value::nil());
          return 1;
        }

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
        vm->push(Value::object(list));
        return 1;
      },
      -1);

  registerNative(
      "assert",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc >= 1 && !args[0].isTruthy()) {
          std::string msg = "Assertion failed";
          if (argc >= 2 && args[1].isString()) {
            msg = static_cast<StringObject *>(args[1].asGC())->str();
          }
          this->runtimeError("%s", msg.c_str());
        }
        return 0;
      },
      -1);

  registerNative(
      "error",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        Value errorVal;
        if (argc >= 1) {
          errorVal = args[0];
        } else {
          errorVal = Value::object(this->allocateString("error called without message"));
        }
        this->throwError(errorVal);
        return 0;
      },
      -1);

  registerNative(
      "pcall",
      [this](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1) {
          vm->push(Value::boolean(false));
          vm->push(Value::object(this->allocateString("pcall: expected a function")));
          return 2;
        }

        Value func = args[0];
        if (!func.isClosure()) {
          vm->push(Value::boolean(false));
          vm->push(Value::object(this->allocateString("pcall: first argument must be a function")));
          return 2;
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

        size_t callBaseOffset = fiber->stackTop - fiber->stack;

        *fiber->stackTop++ = func;
        for (int i = 0; i < funcArgCount; ++i) {
          *fiber->stackTop++ = funcArgs[i];
        }

        InterpretResult result = InterpretResult::OK;
        std::vector<Value> returnValues;
        Closure *closure = static_cast<Closure *>(func.asGC());

        if (closure->isScript()) {
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

            fiber->ensureStack(proto->maxStackSize);
            fiber->ensureFrames(1);

            Value *currentCallBase = fiber->stack + callBaseOffset;

            int slotsBaseOffset = static_cast<int>((currentCallBase + 1) - fiber->stack);
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

            newFrame->returnTo = currentCallBase;

            newFrame->deferBase = fiber->deferTop;

            int savedExitFrameCount = this->exitFrameCount_;
            this->exitFrameCount_ = fiber->frameCount;
            result = this->run();
            this->exitFrameCount_ = savedExitFrameCount;

            if (result == InterpretResult::OK && !this->hasError_) {

              Value *resultStart = fiber->stack + callBaseOffset;
              for (Value *p = resultStart; p < fiber->stackTop; ++p) {
                returnValues.push_back(*p);
              }
            }
          }
        } else {

          if (closure->arity != -1 && funcArgCount != closure->arity) {
            this->hasError_ = true;
            this->errorValue_ = Value::object(this->allocateString(
                "Native function expects " + std::to_string(closure->arity) + " arguments"));
            result = InterpretResult::RUNTIME_ERROR;
          } else {

            Value *currentCallBase = fiber->stack + callBaseOffset;
            fiber->stackTop = currentCallBase;

            int nresults = closure->function(this, closure, funcArgCount, funcArgs);

            if (!this->hasError_) {

              currentCallBase = fiber->stack + callBaseOffset;
              for (int i = 0; i < nresults; ++i) {
                returnValues.push_back(currentCallBase[i]);
              }
            } else {
              result = InterpretResult::RUNTIME_ERROR;
            }
          }
        }

        this->pcallStack_.pop_back();

        if (result == InterpretResult::OK && !this->hasError_) {

          fiber->stackTop = fiber->stack + savedStackTopOffset;
          vm->push(Value::boolean(true));
          for (const auto &v : returnValues) {
            vm->push(v);
          }
          this->hasError_ = savedHasError;
          this->errorValue_ = savedErrorValue;
          return 1 + static_cast<int>(returnValues.size());
        } else {

          this->closeUpvalues(fiber->stack + savedStackTopOffset);

          while (fiber->frameCount > savedFrameCount) {
            CallFrame *currentFrame = &fiber->frames[fiber->frameCount - 1];

            size_t frameSlotsOffset = currentFrame->slots - fiber->stack;
            this->closeUpvalues(fiber->stack + frameSlotsOffset);

            this->invokeDefers(currentFrame->deferBase);
            fiber->frameCount--;
          }

          fiber->deferTop = savedDeferTop;
          fiber->stackTop = fiber->stack + savedStackTopOffset;

          assert(fiber->openUpvalues == savedOpenUpvalues);

          Value errVal = this->hasError_ ? this->errorValue_
                                         : Value::object(this->allocateString("unknown error"));
          vm->push(Value::boolean(false));
          vm->push(errVal);

          this->hasError_ = savedHasError;
          this->errorValue_ = savedErrorValue;
          return 2;
        }
      },
      -1);

  registerNative(
      "isInt",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isInt()));
        return 1;
      },
      1);

  registerNative(
      "isFloat",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isFloat()));
        return 1;
      },
      1);

  registerNative(
      "isNumber",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isNumber()));
        return 1;
      },
      1);

  registerNative(
      "isString",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isString()));
        return 1;
      },
      1);

  registerNative(
      "isBool",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isBool()));
        return 1;
      },
      1);

  registerNative(
      "isList",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isList()));
        return 1;
      },
      1);

  registerNative(
      "isMap",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isMap()));
        return 1;
      },
      1);

  registerNative(
      "isNull",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc < 1 || args[0].isNil()));
        return 1;
      },
      1);

  registerNative(
      "isFunction",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        vm->push(Value::boolean(argc > 0 && args[0].isClosure()));
        return 1;
      },
      1);

  registerNative(
      "__iter_list",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 2) {
          vm->push(Value::nil());
          return 1;
        }
        Value listVal = args[0];
        Value idxVal = args[1];

        if (!listVal.isList()) {
          vm->push(Value::nil());
          return 1;
        }
        ListObject *list = static_cast<ListObject *>(listVal.asGC());

        int64_t nextIdx = 0;
        if (idxVal.isNil()) {
          nextIdx = 0;
        } else if (idxVal.isInt()) {
          nextIdx = idxVal.asInt() + 1;
        } else {
          vm->push(Value::nil());
          return 1;
        }

        if (nextIdx >= 0 && nextIdx < static_cast<int64_t>(list->elements.size())) {
          vm->push(Value::integer(nextIdx));
          vm->push(list->elements[nextIdx]);
          return 2;
        }

        vm->push(Value::nil());
        return 1;
      },
      2);

  registerNative(
      "__iter_map",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 2) {
          vm->push(Value::nil());
          return 1;
        }
        Value mapVal = args[0];
        Value keyVal = args[1];

        if (!mapVal.isMap()) {
          vm->push(Value::nil());
          return 1;
        }
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
          vm->push(it->first);
          vm->push(it->second);
          return 2;
        }

        vm->push(Value::nil());
        return 1;
      },
      2);

  registerNative(
      "pairs",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        if (argc < 1) {
          vm->push(Value::nil());
          return 1;
        }
        Value target = args[0];

        if (target.isList()) {
          Value iterFunc = vm->getGlobal("__iter_list");
          vm->push(iterFunc);
          vm->push(target);
          vm->push(Value::integer(-1));
          return 3;
        } else if (target.isMap()) {
          Value iterFunc = vm->getGlobal("__iter_map");
          vm->push(iterFunc);
          vm->push(target);
          vm->push(Value::nil());
          return 3;
        }

        vm->throwError(Value::object(vm->allocateString("pairs() expects a list or map")));
        return 0;
      },
      1);

  registerNative(
      "clock",
      [](VM *vm, Closure *self, int argc, Value *args) -> int {
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
        vm->push(Value::integer(micros));
        return 1;
      },
      0);
}

} // namespace spt