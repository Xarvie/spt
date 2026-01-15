#include "VM.h"

#include "Fiber.h"
#include "NativeBinding.h"
#include "SptStdlibs.h"
#include "VMDispatch.h"
#include <algorithm>
#include <cmath>

namespace spt {
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

  Value *stackBase = fiber->stack;
  Value *stackLimit = fiber->stackLast;
  Value *slots = frame->slots;

  const ConstantValue *k = frame->closure->proto->constants.data();

  uint32_t instruction;

#if SPT_USE_COMPUTED_GOTO
  SPT_DEFINE_DISPATCH_TABLE()
#endif

#define FIBER fiber
#define STACK_TOP fiber->stackTop
#define FRAMES fiber->frames
#define FRAME_COUNT fiber->frameCount

#define RESTORE_POINTERS()                                                                         \
  do {                                                                                             \
                                                                                                   \
    fiber = currentFiber_;                                                                         \
                                                                                                   \
    stackBase = fiber->stack;                                                                      \
    stackLimit = fiber->stackLast;                                                                 \
                                                                                                   \
    frame = &fiber->frames[fiber->frameCount - 1];                                                 \
                                                                                                   \
    slots = frame->slots;                                                                          \
                                                                                                   \
    k = frame->closure->proto->constants.data();                                                   \
  } while (0)

#define RESTORE_SLOTS_ONLY()                                                                       \
  do {                                                                                             \
    stackBase = fiber->stack;                                                                      \
    stackLimit = fiber->stackLast;                                                                 \
    frame = &fiber->frames[fiber->frameCount - 1];                                                 \
    slots = frame->slots;                                                                          \
    k = frame->closure->proto->constants.data();                                                   \
  } while (0)

#define PROTECT(action)                                                                            \
  do {                                                                                             \
    action;                                                                                        \
    RESTORE_POINTERS();                                                                            \
  } while (0)

#define PROTECT_LIGHT(action)                                                                      \
  do {                                                                                             \
    action;                                                                                        \
    RESTORE_SLOTS_ONLY();                                                                          \
  } while (0)

#define HALF_PROTECT(action)                                                                       \
  do {                                                                                             \
    frame->ip = ip;                                                                                \
    action;                                                                                        \
  } while (0)

#define REFRESH_CACHE() RESTORE_POINTERS()
#define REFRESH_SLOTS() RESTORE_SLOTS_ONLY()

  (void)stackBase;
  (void)stackLimit;
  (void)k;

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

    const auto &constant = k[Bx];
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

            StringObject *str;
            PROTECT(str = allocateString(arg));
            value = Value::object(str);
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

    ListObject *list;
    PROTECT(list = allocateList(B));
    slots[A] = Value::object(list);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWMAP) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);

    MapObject *map;
    PROTECT(map = allocateMap(B));
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

    const auto &keyConst = k[C];

    if (!std::holds_alternative<std::string>(keyConst)) {
      runtimeError("GETFIELD requires string key constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &fieldName = std::get<std::string>(keyConst);

    if (object.isList() || object.isMap() || object.isString() || object.isFiber()) {
      Value result;
      bool success;

      PROTECT(success = StdlibDispatcher::getProperty(this, object, fieldName, result));
      if (success) {
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
    } else if (object.isNativeInstance()) {

      auto *instance = static_cast<NativeInstance *>(object.asGC());

      auto fieldIt = instance->fields.find(fieldName);
      if (fieldIt != instance->fields.end()) {
        slots[A] = fieldIt->second;
        SPT_DISPATCH();
      }

      if (instance->nativeClass) {
        const NativePropertyDesc *prop = instance->nativeClass->findProperty(fieldName);
        if (prop && prop->getter) {

          Value getterResult;
          PROTECT(getterResult = prop->getter(this, instance));
          slots[A] = getterResult;
          SPT_DISPATCH();
        }

        const NativeMethodDesc *nativeMethod = instance->nativeClass->findMethod(fieldName);
        if (nativeMethod) {

          NativeFunction *boundMethod;
          PROTECT(boundMethod = gc_.allocate<NativeFunction>());
          boundMethod->name = fieldName;
          boundMethod->arity = nativeMethod->arity;
          boundMethod->receiver = object;

          boundMethod->function = [nativeMethod](VM *vm, Value receiver, int argc,
                                                 Value *args) -> Value {
            NativeInstance *inst = static_cast<NativeInstance *>(receiver.asGC());
            return nativeMethod->function(vm, inst, argc, args);
          };
          slots[A] = Value::object(boundMethod);
          SPT_DISPATCH();
        }
      }

      slots[A] = Value::nil();
    } else if (object.isNativeClass()) {

      auto *nativeClass = static_cast<NativeClassObject *>(object.asGC());

      auto it = nativeClass->statics.find(fieldName);
      if (it != nativeClass->statics.end()) {
        slots[A] = it->second;
        SPT_DISPATCH();
      }

      NativeClassObject *base = nativeClass->baseClass;
      while (base) {
        auto baseIt = base->statics.find(fieldName);
        if (baseIt != base->statics.end()) {
          slots[A] = baseIt->second;
          SPT_DISPATCH();
        }
        base = base->baseClass;
      }

      slots[A] = Value::nil();
    } else if (object.isMap()) {
      auto *map = static_cast<MapObject *>(object.asGC());
      Value result = Value::nil();
      for (const auto &pair : map->entries) {
        if (pair.first.isString()) {
          StringObject *keyStr = static_cast<StringObject *>(pair.first.asGC());
          if (keyStr->view() == fieldName) {
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

    const auto &keyConst = k[B];
    Value value = slots[C];

    if (!std::holds_alternative<std::string>(keyConst)) {
      runtimeError("SETFIELD requires string key constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &fieldName = std::get<std::string>(keyConst);

    StringObject *fieldNameStr;
    PROTECT(fieldNameStr = allocateString(fieldName));

    object = slots[A];
    value = slots[C];

    if (object.isInstance()) {
      auto *instance = static_cast<Instance *>(object.asGC());
      instance->setField(fieldNameStr, value);
    } else if (object.isNativeInstance()) {

      auto *instance = static_cast<NativeInstance *>(object.asGC());

      if (instance->nativeClass) {
        const NativePropertyDesc *prop = instance->nativeClass->findProperty(fieldName);
        if (prop) {
          if (prop->isReadOnly || !prop->setter) {
            runtimeError("Property '%s' is read-only on native class '%s'", fieldName.c_str(),
                         instance->nativeClass->name.c_str());
            return InterpretResult::RUNTIME_ERROR;
          }

          PROTECT(prop->setter(this, instance, value));
          SPT_DISPATCH();
        }
      }

      instance->fields[fieldNameStr] = value;
    } else if (object.isClass()) {
      auto *klass = static_cast<ClassObject *>(object.asGC());
      klass->methods[fieldNameStr] = value;
    } else if (object.isMap()) {
      auto *map = static_cast<MapObject *>(object.asGC());

      StringObject *key;
      PROTECT(key = allocateString(fieldName));

      map = static_cast<MapObject *>(slots[A].asGC());
      value = slots[C];
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

    const auto &nameConst = k[Bx];
    if (!std::holds_alternative<std::string>(nameConst)) {
      runtimeError("Class name must be string constant");
      return InterpretResult::RUNTIME_ERROR;
    }
    const std::string &className = std::get<std::string>(nameConst);

    ClassObject *klass;
    PROTECT(klass = allocateClass(className));
    slots[A] = Value::object(klass);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWOBJ) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);

    Value classValue = slots[B];

    if (classValue.isNativeClass()) {
      NativeClassObject *nativeClass = static_cast<NativeClassObject *>(classValue.asGC());

      if (!nativeClass->hasConstructor()) {
        runtimeError("Native class '%s' has no constructor", nativeClass->name.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }

      Value *argsStart = &slots[B + 1];

      NativeInstance *instance;
      PROTECT(instance = createNativeInstance(nativeClass, C, argsStart));

      if (!instance) {
        return InterpretResult::RUNTIME_ERROR;
      }
      slots[A] = Value::object(instance);
      SPT_DISPATCH();
    }

    if (!classValue.isClass()) {
      runtimeError("Cannot instantiate non-class type '%s'", classValue.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }

    auto *klass = static_cast<ClassObject *>(classValue.asGC());

    Instance *instance;
    PROTECT(instance = allocateInstance(klass));

    klass = static_cast<ClassObject *>(slots[B].asGC());
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

        Value *newSlots = slots + B;
        int neededStack = static_cast<int>((newSlots - fiber->stack) + proto->maxStackSize);

        fiber->ensureStack(neededStack);
        fiber->ensureFrames(1);
        REFRESH_CACHE();
        newSlots = slots + B;

        Value *argsStart = newSlots;
        Value *targetStackTop = argsStart + proto->maxStackSize;

        for (Value *p = argsStart + providedArgs; p < targetStackTop; ++p) {
          *p = Value::nil();
        }

        STACK_TOP = targetStackTop;

        CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
        newFrame->closure = closure;
        newFrame->ip = proto->code.data();
        newFrame->expectedResults = 0;
        newFrame->slots = newSlots;
        newFrame->returnTo = slots + A;

        newFrame->deferBase = fiber->deferTop;

        frame = newFrame;
        slots = frame->slots;

        k = frame->closure->proto->constants.data();

      } else if (initializer.isNativeFunc()) {
        NativeFunction *native = static_cast<NativeFunction *>(initializer.asGC());

        if (native->arity != -1 && C != native->arity) {
          runtimeError("Native init expects %d arguments, got %d", native->arity, C);
          return InterpretResult::RUNTIME_ERROR;
        }

        Value *argsStart = &slots[B + 1];

        PROTECT(native->function(this, instanceVal, C, argsStart));
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
    if (B >= frame->closure->upvalueCount) {
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
    if (B >= frame->closure->upvalueCount) {
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

    Closure *closure;
    PROTECT(closure = allocateClosure(&proto));
    protect(Value::object(closure));

    for (size_t i = 0; i < proto.numUpvalues; ++i) {
      const auto &uvDesc = proto.upvalues[i];
      if (uvDesc.isLocal) {

        closure->upvalues[i] = captureUpvalue(&slots[uvDesc.index]);
      } else {
        closure->upvalues[i] = frame->closure->upvalues[uvDesc.index];
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

      StringObject *result;
      PROTECT(result = allocateString(s1 + s2));
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

  SPT_OPCODE(OP_IDIV) {
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

    double result = std::floor(left / right);

    constexpr double maxInt = static_cast<double>(INT64_MAX);
    constexpr double minInt = static_cast<double>(INT64_MIN);

    if (result >= minInt && result <= maxInt) {
      slots[A] = Value::integer(static_cast<int64_t>(result));
    } else {
      slots[A] = Value::number(result);
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

    int64_t left = b.asInt();
    int64_t right = c.asInt();

    if (right == 0) {
      runtimeError("Modulo by zero");
      return InterpretResult::RUNTIME_ERROR;
    }

    if (left == INT64_MIN && right == -1) {
      slots[A] = Value::integer(0);
    } else {
      slots[A] = Value::integer(left % right);
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_UNM) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    Value b = slots[B];

    if (b.isInt()) {
      int64_t val = b.asInt();

      if (val == INT64_MIN) {

        slots[A] = Value::number(-static_cast<double>(val));
      } else {
        slots[A] = Value::integer(-val);
      }
    } else if (b.isFloat()) {
      slots[A] = Value::number(-b.asFloat());
    } else {
      runtimeError("Operand must be a number");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

#define SPT_BW_BINARY_OP(opName, op)                                                               \
  SPT_OPCODE(opName) {                                                                             \
    uint8_t A = GETARG_A(instruction);                                                             \
    uint8_t B = GETARG_B(instruction);                                                             \
    uint8_t C = GETARG_C(instruction);                                                             \
    Value b = slots[B];                                                                            \
    Value c = slots[C];                                                                            \
                                                                                                   \
    if (!b.isInt() || !c.isInt()) {                                                                \
      runtimeError("Operands must be integers");                                                   \
      return InterpretResult::RUNTIME_ERROR;                                                       \
    }                                                                                              \
    slots[A] = Value::integer(b.asInt() op c.asInt());                                             \
    SPT_DISPATCH();                                                                                \
  }

  SPT_BW_BINARY_OP(OP_BAND, &)
  SPT_BW_BINARY_OP(OP_BOR, |)
  SPT_OPCODE(OP_BXOR) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];
    if (!b.isInt() || !c.isInt()) {
      runtimeError("Operands must be integers");
      return InterpretResult::RUNTIME_ERROR;
    }
    slots[A] = Value::integer(b.asInt() ^ c.asInt());
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_BNOT) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    Value b = slots[B];

    if (b.isInt()) {
      slots[A] = Value::integer(~b.asInt());
    } else {
      runtimeError("Operand must be a integer");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

#undef SPT_BW_BINARY_OP

  SPT_OPCODE(OP_SHL) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isInt() || !c.isInt()) {
      runtimeError("Operands must be integers");
      return InterpretResult::RUNTIME_ERROR;
    }

    int64_t shiftAmount = c.asInt();

    if (shiftAmount < 0 || shiftAmount >= 64) {
      runtimeError("Shift amount must be between 0 and 63");
      return InterpretResult::RUNTIME_ERROR;
    }
    slots[A] = Value::integer(b.asInt() << shiftAmount);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SHR) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isInt() || !c.isInt()) {
      runtimeError("Operands must be integers");
      return InterpretResult::RUNTIME_ERROR;
    }

    int64_t shiftAmount = c.asInt();

    if (shiftAmount < 0 || shiftAmount >= 64) {
      runtimeError("Shift amount must be between 0 and 63");
      return InterpretResult::RUNTIME_ERROR;
    }
    slots[A] = Value::integer(b.asInt() >> shiftAmount);
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

    int argCount;

    if (B == 0) {
      argCount = static_cast<int>(fiber->stackTop - (slots + A + 1));
    } else {
      argCount = B - 1;
    }

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

      Value *newSlots = slots + A + 1;
      int neededStack = static_cast<int>((newSlots - fiber->stack) + proto->maxStackSize);

      fiber->ensureStack(neededStack);
      fiber->ensureFrames(1);
      REFRESH_CACHE();
      newSlots = slots + A + 1;

      Value *argsStart = newSlots;

      int numParams = proto->numParams;

      for (int i = argCount; i < numParams; ++i) {
        argsStart[i] = Value::nil();
      }

      fiber->stackTop = argsStart + proto->maxStackSize;

      CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();
      newFrame->expectedResults = expectedResults;
      newFrame->slots = newSlots;
      newFrame->returnTo = slots + A;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;
      slots = frame->slots;

      k = frame->closure->proto->constants.data();

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

      Value result;
      PROTECT(result = native->function(this, native->receiver, argCount, argsStart));

      if (yieldPending_) {
        yieldPending_ = false;
        return InterpretResult::OK;
      }

      if (hasError_) {
        return InterpretResult::RUNTIME_ERROR;
      }

      if (hasNativeMultiReturn_) {
        int returnCount = static_cast<int>(nativeMultiReturn_.size());

        if (expectedResults == -1) {

          fiber->ensureStack(returnCount);

          RESTORE_SLOTS_ONLY();

          for (int i = 0; i < returnCount; ++i) {
            slots[A + i] = nativeMultiReturn_[i];
          }
          STACK_TOP = slots + A + returnCount;
        } else if (expectedResults > 0) {
          for (int i = 0; i < returnCount && i < expectedResults; ++i) {
            slots[A + i] = nativeMultiReturn_[i];
          }
          for (int i = returnCount; i < expectedResults; ++i) {
            slots[A + i] = Value::nil();
          }
        } else {
        }

        hasNativeMultiReturn_ = false;
        nativeMultiReturn_.clear();
      } else {
        if (expectedResults != 0) {
          slots[A] = result;
          if (expectedResults == -1) {
            STACK_TOP = slots + A + 1;
          } else {
            for (int i = 1; i < expectedResults; ++i)
              slots[A + i] = Value::nil();
          }
        }
      }
    } else if (callee.isNativeClass()) {
      NativeClassObject *nativeClass = static_cast<NativeClassObject *>(callee.asGC());

      if (!nativeClass->hasConstructor()) {
        runtimeError("Native class '%s' has no constructor", nativeClass->name.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }

      Value *argsStart = &slots[A + 1];
      NativeInstance *instance = createNativeInstance(nativeClass, argCount, argsStart);

      if (!instance) {
        return InterpretResult::RUNTIME_ERROR;
      }

      slots[A] = Value::object(instance);

      if (expectedResults > 1) {
        for (int i = 1; i < expectedResults; ++i)
          slots[A + i] = Value::nil();
      } else if (expectedResults == -1) {
        STACK_TOP = slots + A + 1;
      }

    } else {
      runtimeError("Attempt to call a non-function value of type '%s'", callee.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CALL_SELF) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);

    int argCount;

    if (B == 0) {
      argCount = static_cast<int>(fiber->stackTop - (slots + A));
    } else {
      argCount = B - 1;
    }

    int expectedResults = C - 1;
    Closure *closure = frame->closure;
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

    Value *newSlots = slots + A;
    int neededStack = static_cast<int>((newSlots - fiber->stack) + proto->maxStackSize);

    fiber->ensureStack(neededStack);
    fiber->ensureFrames(1);
    REFRESH_CACHE();
    newSlots = slots + A;

    Value *argsStart = newSlots;
    int numParams = proto->numParams;

    for (int i = argCount; i < numParams; ++i) {
      argsStart[i] = Value::nil();
    }

    STACK_TOP = argsStart + proto->maxStackSize;

    CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
    newFrame->closure = closure;
    newFrame->ip = proto->code.data();
    newFrame->expectedResults = expectedResults;
    newFrame->slots = newSlots;
    newFrame->returnTo = slots + A;
    newFrame->deferBase = fiber->deferTop;

    frame = newFrame;
    slots = frame->slots;

    k = frame->closure->proto->constants.data();
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_INVOKE) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);

    uint32_t nextInst = *frame->ip++;
    uint32_t methodIdx = GETARG_Ax(nextInst);

    Value receiver = slots[A];

    int userArgCount;
    if (B == 0) {
      userArgCount = static_cast<int>(fiber->stackTop - (slots + A + 1));
    } else {
      userArgCount = B - 1;
    }

    const auto &methodNameConst = k[methodIdx];
    if (!std::holds_alternative<std::string>(methodNameConst)) {
      runtimeError("OP_INVOKE: method name constant must be string");
      return InterpretResult::RUNTIME_ERROR;
    }
    const std::string &methodName = std::get<std::string>(methodNameConst);

    Value method = Value::nil();
    Value *argsStart = &slots[A + 1];

    if (receiver.isList() || receiver.isMap() || receiver.isString() || receiver.isFiber()) {
      Value directResult;

      hasNativeMultiReturn_ = false;
      nativeMultiReturn_.clear();

      bool invokeSuccess;

      PROTECT(invokeSuccess = StdlibDispatcher::invokeMethod(
                  this, receiver, methodName, userArgCount, argsStart, directResult));
      if (invokeSuccess) {
        if (yieldPending_) {
          yieldPending_ = false;
          return InterpretResult::OK;
        }

        if (hasError_) {
          return InterpretResult::RUNTIME_ERROR;
        }

        int expectedResults = C - 1;

        if (hasNativeMultiReturn_) {
          int returnCount = static_cast<int>(nativeMultiReturn_.size());
          if (expectedResults == -1) {

            for (int i = 0; i < returnCount; ++i) {
              slots[A + i] = nativeMultiReturn_[i];
            }
            STACK_TOP = slots + A + returnCount;
          } else if (expectedResults > 0) {

            for (int i = 0; i < expectedResults; ++i) {
              slots[A + i] = (i < returnCount) ? nativeMultiReturn_[i] : Value::nil();
            }
          }
          hasNativeMultiReturn_ = false;
          nativeMultiReturn_.clear();
        } else {

          slots[A] = directResult;
          if (expectedResults == -1) {
            STACK_TOP = slots + A + 1;
          } else if (expectedResults > 1) {
            for (int i = 1; i < expectedResults; ++i) {
              slots[A + i] = Value::nil();
            }
          }
        }
        SPT_DISPATCH();
      }

      receiver = slots[A];
      argsStart = &slots[A + 1];

      Value propertyValue;
      bool getPropSuccess;

      PROTECT(getPropSuccess =
                  StdlibDispatcher::getProperty(this, receiver, methodName, propertyValue));
      if (getPropSuccess) {
        if (propertyValue.isNativeFunc()) {
          method = propertyValue;
        } else {
          runtimeError("'%s.%s' is a property, not a method", receiver.typeName(),
                       methodName.c_str());
          return InterpretResult::RUNTIME_ERROR;
        }
      } else if (receiver.isMap()) {
        MapObject *map = static_cast<MapObject *>(receiver.asGC());

        StringObject *key;
        PROTECT(key = allocateString(methodName));

        map = static_cast<MapObject *>(slots[A].asGC());
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
    } else if (receiver.isNativeInstance()) {
      NativeInstance *instance = static_cast<NativeInstance *>(receiver.asGC());
      if (!instance->nativeClass) {
        runtimeError("Native instance has no class information");
        return InterpretResult::RUNTIME_ERROR;
      }

      const NativeMethodDesc *nativeMethod = instance->nativeClass->findMethod(methodName);
      if (nativeMethod) {
        if (nativeMethod->arity != -1 && userArgCount != nativeMethod->arity) {
          runtimeError("Native method '%s' expects %d arguments, got %d", methodName.c_str(),
                       nativeMethod->arity, userArgCount);
          return InterpretResult::RUNTIME_ERROR;
        }

        hasNativeMultiReturn_ = false;
        nativeMultiReturn_.clear();

        Value result;
        PROTECT(result = nativeMethod->function(this, instance, userArgCount, argsStart));

        if (yieldPending_) {
          yieldPending_ = false;
          return InterpretResult::OK;
        }

        if (hasError_) {
          return InterpretResult::RUNTIME_ERROR;
        }

        int expectedResults = C - 1;

        if (hasNativeMultiReturn_) {
          int returnCount = static_cast<int>(nativeMultiReturn_.size());
          if (expectedResults == -1) {

            for (int i = 0; i < returnCount; ++i) {
              slots[A + i] = nativeMultiReturn_[i];
            }
            STACK_TOP = slots + A + returnCount;
          } else if (expectedResults > 0) {

            for (int i = 0; i < expectedResults; ++i) {
              slots[A + i] = (i < returnCount) ? nativeMultiReturn_[i] : Value::nil();
            }
          }
          hasNativeMultiReturn_ = false;
          nativeMultiReturn_.clear();
        } else {

          slots[A] = result;
          if (expectedResults == -1) {
            STACK_TOP = slots + A + 1;
          } else if (expectedResults > 1) {
            for (int i = 1; i < expectedResults; ++i) {
              slots[A + i] = Value::nil();
            }
          }
        }
        SPT_DISPATCH();
      }

      auto fieldIt = instance->fields.find(methodName);
      if (fieldIt != instance->fields.end()) {
        method = fieldIt->second;
      }

      if (method.isNil()) {
        runtimeError("Native instance of '%s' has no method '%s'",
                     instance->nativeClass->name.c_str(), methodName.c_str());
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
    } else if (receiver.isNativeClass()) {

      NativeClassObject *nativeClass = static_cast<NativeClassObject *>(receiver.asGC());
      auto it = nativeClass->statics.find(methodName);
      if (it != nativeClass->statics.end()) {
        method = it->second;
      }
      NativeClassObject *base = nativeClass->baseClass;
      while (method.isNil() && base) {
        auto baseIt = base->statics.find(methodName);
        if (baseIt != base->statics.end()) {
          method = baseIt->second;
          break;
        }
        base = base->baseClass;
      }
      if (method.isNil()) {
        runtimeError("Native class '%s' has no static method '%s'", nativeClass->name.c_str(),
                     methodName.c_str());
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

      int totalArgsProvided = userArgCount + 1;
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

      Value *newSlots = slots + A + (dropThis ? 2 : 1);
      int neededStack = static_cast<int>((newSlots - fiber->stack) + proto->maxStackSize);

      fiber->ensureStack(neededStack);
      fiber->ensureFrames(1);
      REFRESH_CACHE();

      int argsPushed;
      if (dropThis) {

        newSlots = slots + A + 1;
        argsPushed = userArgCount;
      } else {

        newSlots = slots + A;
        argsPushed = totalArgsProvided;
      }

      Value *targetStackTop = newSlots + proto->maxStackSize;

      for (Value *p = newSlots + argsPushed; p < targetStackTop; ++p) {
        *p = Value::nil();
      }

      STACK_TOP = targetStackTop;

      CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();
      newFrame->expectedResults = C - 1;
      newFrame->slots = newSlots;
      newFrame->returnTo = slots + A;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;
      slots = frame->slots;

      k = frame->closure->proto->constants.data();

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

      argsStart = &slots[A + 1];
      FiberObject *callFiber = currentFiber_;
      Value result;
      PROTECT(result = native->function(this, receiver, userArgCount, argsStart));

      if (currentFiber_ != callFiber) {
        callFiber->stackTop--;
      } else {
        unprotect(1);
      }

      if (yieldPending_) {
        yieldPending_ = false;
        return InterpretResult::OK;
      }

      if (hasError_) {
        return InterpretResult::RUNTIME_ERROR;
      }

      int expectedResults = C - 1;

      if (hasNativeMultiReturn_) {
        int returnCount = static_cast<int>(nativeMultiReturn_.size());
        if (expectedResults == -1) {
          for (int i = 0; i < returnCount; ++i)
            slots[A + i] = nativeMultiReturn_[i];
          STACK_TOP = slots + A + returnCount;
        } else {
          for (int i = 0; i < expectedResults; ++i) {
            slots[A + i] = (i < returnCount) ? nativeMultiReturn_[i] : Value::nil();
          }
        }
        hasNativeMultiReturn_ = false;
        nativeMultiReturn_.clear();
      } else {
        if (expectedResults != 0) {
          slots[A] = result;
          if (expectedResults == -1)
            STACK_TOP = slots + A + 1;
          else {
            for (int i = 1; i < expectedResults; ++i)
              slots[A + i] = Value::nil();
          }
        }
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
      invokeDefers(frame->deferBase);

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
      destSlot = frame->returnTo;
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

      if (!pcallStack_.empty()) {
        nativeMultiReturn_.clear();
        for (int i = 0; i < returnCount; ++i) {
          nativeMultiReturn_.push_back(returnValues[i]);
        }
        hasNativeMultiReturn_ = true;
      }
      return InterpretResult::OK;
    }

    frame = &fiber->frames[FRAME_COUNT - 1];
    slots = frame->slots;

    k = frame->closure->proto->constants.data();

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

  SPT_OPCODE(OP_RETURN_NDEF) {
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    int returnCount = (B >= 1) ? B - 1 : 0;

    Value *returnValues = slots + A;
    int expectedResults = frame->expectedResults;

    bool isRootFrame = (FRAME_COUNT == 1);
    bool isModuleExit = (exitFrameCount_ > 0 && FRAME_COUNT == exitFrameCount_);

    Value *destSlot = nullptr;
    if (!isRootFrame && !isModuleExit) {
      destSlot = frame->returnTo;
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

      if (!pcallStack_.empty()) {
        nativeMultiReturn_.clear();
        for (int i = 0; i < returnCount; ++i) {
          nativeMultiReturn_.push_back(returnValues[i]);
        }
        hasNativeMultiReturn_ = true;
      }
      return InterpretResult::OK;
    }

    frame = &fiber->frames[FRAME_COUNT - 1];
    slots = frame->slots;

    k = frame->closure->proto->constants.data();

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

    const auto &moduleNameConst = k[Bx];

    if (!std::holds_alternative<std::string>(moduleNameConst)) {
      runtimeError("Module name must be a string constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &moduleName = std::get<std::string>(moduleNameConst);
    const char *currentPath = frame->closure->proto->source.c_str();

    Value exportsTable;
    PROTECT(exportsTable = moduleManager_->loadModule(moduleName, currentPath));

    if (FRAME_COUNT == 0 || hasError_) {
      return InterpretResult::RUNTIME_ERROR;
    }

    if (exportsTable.isMap()) {
      MapObject *errorCheck = static_cast<MapObject *>(exportsTable.asGC());

      StringObject *errorKey;
      PROTECT(errorKey = allocateString("error"));

      errorCheck = static_cast<MapObject *>(exportsTable.asGC());
      Value errorFlag = errorCheck->get(Value::object(errorKey));

      if (errorFlag.isBool() && errorFlag.asBool()) {
        StringObject *msgKey;
        PROTECT(msgKey = allocateString("message"));
        errorCheck = static_cast<MapObject *>(exportsTable.asGC());
        Value msgVal = errorCheck->get(Value::object(msgKey));
        const char *errorMsg = msgVal.isString()
                                   ? static_cast<StringObject *>(msgVal.asGC())->c_str()
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

    const auto &moduleNameConst = k[B];
    const auto &symbolNameConst = k[C];

    if (!std::holds_alternative<std::string>(moduleNameConst) ||
        !std::holds_alternative<std::string>(symbolNameConst)) {
      runtimeError("Module and symbol names must be string constants");
      return InterpretResult::RUNTIME_ERROR;
    }

    const std::string &moduleName = std::get<std::string>(moduleNameConst);
    const std::string &symbolName = std::get<std::string>(symbolNameConst);
    const char *currentPath = frame->closure->proto->source.c_str();

    Value exportsTable;
    PROTECT(exportsTable = moduleManager_->loadModule(moduleName, currentPath));

    if (FRAME_COUNT == 0 || hasError_) {
      return InterpretResult::RUNTIME_ERROR;
    }

    if (exportsTable.isMap()) {
      MapObject *exports = static_cast<MapObject *>(exportsTable.asGC());

      StringObject *errorKey;
      PROTECT(errorKey = allocateString("error"));
      exports = static_cast<MapObject *>(exportsTable.asGC());
      Value errorFlag = exports->get(Value::object(errorKey));

      if (errorFlag.isBool() && errorFlag.asBool()) {
        StringObject *msgKey;
        PROTECT(msgKey = allocateString("message"));
        exports = static_cast<MapObject *>(exportsTable.asGC());
        Value msgVal = exports->get(Value::object(msgKey));
        const char *errorMsg = msgVal.isString()
                                   ? static_cast<StringObject *>(msgVal.asGC())->c_str()
                                   : "Module load failed";
        runtimeError("Import error: %s", errorMsg);
        return InterpretResult::RUNTIME_ERROR;
      }

      StringObject *symKey;
      PROTECT(symKey = allocateString(symbolName));
      exports = static_cast<MapObject *>(exportsTable.asGC());
      Value symbolValue = exports->get(Value::object(symKey));
      slots[A] = symbolValue;
    } else {
      slots[A] = Value::nil();
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_DEFER) {
    uint8_t A = GETARG_A(instruction);
    Value deferClosure = slots[A];

    if (!deferClosure.isClosure()) {
      runtimeError("defer requires a function");
      return InterpretResult::RUNTIME_ERROR;
    }
    if (!frame->closure->proto->useDefer) {
      runtimeError("compiler error because defer was not used");
      return InterpretResult::RUNTIME_ERROR;
    }

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

    const auto &constant = k[B];
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

            StringObject *str;
            PROTECT(str = allocateString(arg));
            kVal = Value::object(str);
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
    } else {

      runtimeError("Cannot compare %s with integer", a.typeName());
      return InterpretResult::RUNTIME_ERROR;
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
    } else {

      runtimeError("Cannot compare %s with integer", a.typeName());
      return InterpretResult::RUNTIME_ERROR;
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
    } else {

      runtimeError("Cannot compare %s with integer", a.typeName());
      return InterpretResult::RUNTIME_ERROR;
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

  SPT_OPCODE(OP_TFORCALL) {
    const uint8_t A = GETARG_A(instruction);
    const uint8_t C = GETARG_C(instruction);

    Value *base = &slots[A];
    Value funcVal = base[0];

    if (funcVal.isClosure()) {
      Closure *closure = static_cast<Closure *>(funcVal.asGC());
      const Prototype *proto = closure->proto;

      size_t currentSize = fiber->stackTop - fiber->stack;
      if (currentSize + 3 + proto->maxStackSize >= fiber->stackSize) {
        fiber->ensureStack(proto->maxStackSize + 3);
        REFRESH_SLOTS();
        base = &slots[A];
      }
      fiber->ensureFrames(1);

      Value *top = fiber->stackTop;
      top[0] = base[0];
      top[1] = base[1];
      top[2] = base[2];
      fiber->stackTop += 3;

      CallFrame *newFrame = &fiber->frames[fiber->frameCount++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();

      newFrame->slots = top + 1;

      newFrame->returnTo = slots + A + 3;
      newFrame->expectedResults = C;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;

      Value *newStackStart = newFrame->slots;
      Value *newStackEnd = newStackStart + proto->maxStackSize;
      while (fiber->stackTop < newStackEnd) {
        *fiber->stackTop++ = Value::nil();
      }

      REFRESH_CACHE();

    }

    else if (funcVal.isNativeFunc()) {
      NativeFunction *native = static_cast<NativeFunction *>(funcVal.asGC());

      nativeMultiReturn_.clear();
      hasNativeMultiReturn_ = false;

      Value result;
      PROTECT(result = native->function(this, Value::nil(), 2, &base[1]));

      if (hasError_)
        return InterpretResult::RUNTIME_ERROR;

      Value *dest = &slots[A + 3];

      if (hasNativeMultiReturn_) {
        const size_t count = nativeMultiReturn_.size();
        const size_t limit = (count < C) ? count : C;
        for (size_t i = 0; i < limit; ++i)
          dest[i] = nativeMultiReturn_[i];
        for (size_t i = limit; i < C; ++i)
          dest[i] = Value::nil();
      } else {
        if (C > 0) {
          dest[0] = result;
          for (int i = 1; i < C; ++i)
            dest[i] = Value::nil();
        }
      }
    } else {
      runtimeError("attempt to iterate over a non-function value");
      return InterpretResult::RUNTIME_ERROR;
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_TFORLOOP) {

    const uint8_t A = GETARG_A(instruction);

    Value *var1 = &slots[A + 3];

    if (!var1->isNil()) {

      slots[A + 2] = *var1;

    } else {

      const int32_t sBx = GETARG_sBx(instruction);
      frame->ip += sBx;
    }

    SPT_DISPATCH();
  }
#undef CHECK_NUM
  SPT_DISPATCH_LOOP_END()

#undef FIBER
#undef STACK_TOP
#undef FRAMES
#undef FRAME_COUNT
#undef REFRESH_CACHE
#undef REFRESH_SLOTS
#undef RESTORE_POINTERS
#undef RESTORE_SLOTS_ONLY
#undef PROTECT
#undef PROTECT_LIGHT
#undef HALF_PROTECT
}

} // namespace spt