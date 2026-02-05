#include "Fiber.h"
#include "VM.h"
#include <algorithm>
#include <cassert>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdlib>

namespace spt {

static int builtin_print(VM *vm, Closure *self, int argc, Value *args) {
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

  const auto &handler = vm->getPrintHandler();
  if (handler) {
    handler(outputBuffer);
  } else {
    printf("%s", outputBuffer.c_str());
  }
  return 0;
}

static int builtin_toInt(VM *vm, Closure *self, int argc, Value *args) {
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
      if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) || std::isinf(val)) {
        vm->throwError(Value::object(vm->allocateString("toInt: value out of range")));
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
}

static int builtin_toFloat(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_toString(VM *vm, Closure *self, int argc, Value *args) {
  if (argc < 1) {
    vm->push(Value::object(vm->allocateString("")));
  } else {
    vm->push(Value::object(vm->allocateString(args[0].toString())));
  }
  return 1;
}

static int builtin_toBool(VM *vm, Closure *self, int argc, Value *args) {
  if (argc < 1) {
    vm->push(Value::boolean(false));
  } else {
    vm->push(Value::boolean(args[0].isTruthy()));
  }
  return 1;
}

static int builtin_typeOf(VM *vm, Closure *self, int argc, Value *args) {
  if (argc < 1) {
    vm->push(Value::object(vm->allocateString("nil")));
  } else {
    vm->push(Value::object(vm->allocateString(args[0].typeName())));
  }
  return 1;
}

static int builtin_len(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_abs(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_floor(VM *vm, Closure *self, int argc, Value *args) {
  Value result = Value::integer(0);
  if (argc >= 1) {
    Value v = args[0];
    if (v.isInt()) {
      result = v;
    } else if (v.isFloat()) {
      double val = std::floor(v.asFloat());
      constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
      constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
      if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) || std::isinf(val)) {
        result = Value::number(val);
      } else {
        result = Value::integer(static_cast<int64_t>(val));
      }
    }
  }
  vm->push(result);
  return 1;
}

static int builtin_ceil(VM *vm, Closure *self, int argc, Value *args) {
  Value result = Value::integer(0);
  if (argc >= 1) {
    Value v = args[0];
    if (v.isInt()) {
      result = v;
    } else if (v.isFloat()) {
      double val = std::ceil(v.asFloat());
      constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
      constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
      if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) || std::isinf(val)) {
        result = Value::number(val);
      } else {
        result = Value::integer(static_cast<int64_t>(val));
      }
    }
  }
  vm->push(result);
  return 1;
}

static int builtin_round(VM *vm, Closure *self, int argc, Value *args) {
  Value result = Value::integer(0);
  if (argc >= 1) {
    Value v = args[0];
    if (v.isInt()) {
      result = v;
    } else if (v.isFloat()) {
      double val = std::round(v.asFloat());
      constexpr double maxInt64AsDouble = static_cast<double>(INT64_MAX);
      constexpr double minInt64AsDouble = static_cast<double>(INT64_MIN);
      if (val > maxInt64AsDouble || val < minInt64AsDouble || std::isnan(val) || std::isinf(val)) {
        result = Value::number(val);
      } else {
        result = Value::integer(static_cast<int64_t>(val));
      }
    }
  }
  vm->push(result);
  return 1;
}

static int builtin_sqrt(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_pow(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_min(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_max(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_char(VM *vm, Closure *self, int argc, Value *args) {
  if (argc < 1 || !args[0].isInt()) {
    vm->push(Value::object(vm->allocateString("")));
    return 1;
  }
  int64_t code = args[0].asInt();
  if (code < 0 || code > 127) {
    vm->push(Value::object(vm->allocateString("")));
    return 1;
  }
  char c = static_cast<char>(code);
  vm->push(Value::object(vm->allocateString(std::string(1, c))));
  return 1;
}

static int builtin_ord(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_range(VM *vm, Closure *self, int argc, Value *args) {
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

  ListObject *list = vm->allocateList(0);
  vm->protect(Value::object(list));

  if (step > 0) {
    for (int64_t i = start; i < end; i += step) {
      list->elements.push_back(Value::integer(i));
    }
  } else {
    for (int64_t i = start; i > end; i += step) {
      list->elements.push_back(Value::integer(i));
    }
  }

  vm->unprotect(1);
  vm->push(Value::object(list));
  return 1;
}

static int builtin_assert(VM *vm, Closure *self, int argc, Value *args) {
  if (argc >= 1 && !args[0].isTruthy()) {
    std::string msg = "Assertion failed";
    if (argc >= 2 && args[1].isString()) {
      msg = static_cast<StringObject *>(args[1].asGC())->str();
    }
    vm->throwError(Value::object(vm->allocateString(msg)));
  }
  return 0;
}

static int builtin_error(VM *vm, Closure *self, int argc, Value *args) {
  Value errorVal;
  if (argc >= 1) {
    errorVal = args[0];
  } else {
    errorVal = Value::object(vm->allocateString("error called without message"));
  }
  vm->throwError(errorVal);
  return 0;
}

static int builtin_pcall(VM *vm, Closure *self, int argc, Value *args) {
  if (argc < 1) {
    vm->push(Value::boolean(false));
    vm->push(Value::object(vm->allocateString("pcall: expected a function")));
    return 2;
  }

  Value func = args[0];
  if (!func.isClosure()) {
    vm->push(Value::boolean(false));
    vm->push(Value::object(vm->allocateString("pcall: first argument must be a function")));
    return 2;
  }

  FiberObject *fiber = vm->currentFiber();
  int savedFrameCount = fiber->frameCount;
  size_t savedStackTopOffset = fiber->stackTop - fiber->stack;
  UpValue *savedOpenUpvalues = fiber->openUpvalues;
  int savedDeferTop = fiber->deferTop;

  bool savedHasError = vm->hasError();
  Value savedErrorValue = vm->getErrorValue();

  vm->clearError();

  ProtectedCallContext ctx;
  ctx.fiber = fiber;
  ctx.frameCount = savedFrameCount;
  ctx.stackTop = fiber->stackTop;
  ctx.openUpvalues = savedOpenUpvalues;
  ctx.active = true;
  vm->pushPcallContext(ctx);

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
      vm->setHasError(true);
      vm->setErrorValue(Value::object(vm->allocateString(
          "Function expects " + std::to_string(proto->numParams) + " arguments")));
      result = InterpretResult::RUNTIME_ERROR;
    } else if (fiber->frameCount >= FiberObject::MAX_FRAMES) {
      vm->setHasError(true);
      vm->setErrorValue(Value::object(vm->allocateString("Stack overflow")));
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

      int savedExitFrameCount = vm->getExitFrameCount();
      vm->setExitFrameCount(fiber->frameCount);
      result = vm->runInternal();
      vm->setExitFrameCount(savedExitFrameCount);

      if (result == InterpretResult::OK && !vm->hasError()) {
        Value *resultStart = fiber->stack + callBaseOffset;
        for (Value *p = resultStart; p < fiber->stackTop; ++p) {
          returnValues.push_back(*p);
        }
      }
    }
  } else {

    if (closure->arity != -1 && funcArgCount != closure->arity) {
      vm->setHasError(true);
      vm->setErrorValue(Value::object(vm->allocateString(
          "Native function expects " + std::to_string(closure->arity) + " arguments")));
      result = InterpretResult::RUNTIME_ERROR;
    } else {
      Value *currentCallBase = fiber->stack + callBaseOffset;
      fiber->stackTop = currentCallBase;

      try {
        int nresults = closure->function(vm, closure, funcArgCount, funcArgs);

        if (!vm->hasError()) {
          currentCallBase = fiber->stack + callBaseOffset;
          for (int i = 0; i < nresults; ++i) {
            returnValues.push_back(currentCallBase[i]);
          }
        } else {
          result = InterpretResult::RUNTIME_ERROR;
        }
      } catch (const SptPanic &e) {

        vm->setHasError(true);
        vm->setErrorValue(e.errorValue);
        result = InterpretResult::RUNTIME_ERROR;
      } catch (const CExtensionException &e) {
        vm->setHasError(true);
        vm->setErrorValue(Value::object(vm->allocateString(e.what())));
        result = InterpretResult::RUNTIME_ERROR;
      }
    }
  }

  vm->popPcallContext();

  if (result == InterpretResult::OK && !vm->hasError()) {
    fiber->stackTop = fiber->stack + savedStackTopOffset;
    vm->push(Value::boolean(true));
    for (const auto &v : returnValues) {
      vm->push(v);
    }
    vm->setHasError(savedHasError);
    vm->setErrorValue(savedErrorValue);
    return 1 + static_cast<int>(returnValues.size());
  } else {
    vm->closeUpvaluesPublic(fiber->stack + savedStackTopOffset);

    while (fiber->frameCount > savedFrameCount) {
      CallFrame *currentFrame = &fiber->frames[fiber->frameCount - 1];

      size_t frameSlotsOffset = currentFrame->slots - fiber->stack;
      vm->closeUpvaluesPublic(fiber->stack + frameSlotsOffset);

      vm->invokeDefersPublic(currentFrame->deferBase);
      fiber->frameCount--;
    }

    fiber->deferTop = savedDeferTop;
    fiber->stackTop = fiber->stack + savedStackTopOffset;

    assert(fiber->openUpvalues == savedOpenUpvalues);

    Value errVal =
        vm->hasError() ? vm->getErrorValue() : Value::object(vm->allocateString("unknown error"));
    vm->push(Value::boolean(false));
    vm->push(errVal);

    vm->setHasError(savedHasError);
    vm->setErrorValue(savedErrorValue);
    return 2;
  }
}

static int builtin_isInt(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isInt()));
  return 1;
}

static int builtin_isFloat(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isFloat()));
  return 1;
}

static int builtin_isNumber(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isNumber()));
  return 1;
}

static int builtin_isString(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isString()));
  return 1;
}

static int builtin_isBool(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isBool()));
  return 1;
}

static int builtin_isList(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isList()));
  return 1;
}

static int builtin_isMap(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isMap()));
  return 1;
}

static int builtin_isNull(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc < 1 || args[0].isNil()));
  return 1;
}

static int builtin_isFunction(VM *vm, Closure *self, int argc, Value *args) {
  vm->push(Value::boolean(argc > 0 && args[0].isClosure()));
  return 1;
}

static int builtin_iter_list(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_iter_map(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_pairs(VM *vm, Closure *self, int argc, Value *args) {
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
}

static int builtin_clock(VM *vm, Closure *self, int argc, Value *args) {
  auto now = std::chrono::system_clock::now();
  auto duration = now.time_since_epoch();
  int64_t micros = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
  vm->push(Value::integer(micros));
  return 1;
}

void VM::registerBuiltinFunctions() {
  registerNative("print", builtin_print, -1);
  registerNative("toInt", builtin_toInt, 1);
  registerNative("toFloat", builtin_toFloat, 1);
  registerNative("toString", builtin_toString, 1);
  registerNative("toBool", builtin_toBool, 1);
  registerNative("typeOf", builtin_typeOf, 1);
  registerNative("len", builtin_len, 1);
  registerNative("abs", builtin_abs, 1);
  registerNative("floor", builtin_floor, 1);
  registerNative("ceil", builtin_ceil, 1);
  registerNative("round", builtin_round, 1);
  registerNative("sqrt", builtin_sqrt, 1);
  registerNative("pow", builtin_pow, 2);
  registerNative("min", builtin_min, 2);
  registerNative("max", builtin_max, 2);
  registerNative("char", builtin_char, 1);
  registerNative("ord", builtin_ord, 1);
  registerNative("range", builtin_range, -1);
  registerNative("assert", builtin_assert, -1);
  registerNative("error", builtin_error, -1);
  registerNative("pcall", builtin_pcall, -1);
  registerNative("isInt", builtin_isInt, 1);
  registerNative("isFloat", builtin_isFloat, 1);
  registerNative("isNumber", builtin_isNumber, 1);
  registerNative("isString", builtin_isString, 1);
  registerNative("isBool", builtin_isBool, 1);
  registerNative("isList", builtin_isList, 1);
  registerNative("isMap", builtin_isMap, 1);
  registerNative("isNull", builtin_isNull, 1);
  registerNative("isFunction", builtin_isFunction, 1);
  registerNative("__iter_list", builtin_iter_list, 2);
  registerNative("__iter_map", builtin_iter_map, 2);
  registerNative("pairs", builtin_pairs, 1);
  registerNative("clock", builtin_clock, 0);
}

} // namespace spt