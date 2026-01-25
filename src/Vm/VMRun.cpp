#include "Fiber.h"
#include "SptStdlibs.h"
#include "StringPool.h"
#include "VM.h"
#include "VMDispatch.h"
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

  const uint32_t *ip = frame->ip;
  Value *slots = frame->slots;

  Value *stackBase = fiber->stack;
  Value *stackLimit = fiber->stackLast;

  int trap = 0;

#if SPT_USE_COMPUTED_GOTO
  SPT_DEFINE_DISPATCH_TABLE()
  uint32_t inst;
#endif

#define FIBER fiber
#define STACK_TOP fiber->stackTop
#define FRAMES fiber->frames
#define FRAME_COUNT fiber->frameCount

#define UPDATE_BASE() (slots = frame->slots)

#define SAVE_PC() (frame->ip = ip)

#define LOAD_PC() (ip = frame->ip)

#define UPDATE_TRAP() (trap = (yieldPending_ || hasError_))

#define RESTORE_POINTERS()                                                                         \
  do {                                                                                             \
    fiber = currentFiber_;                                                                         \
    stackBase = fiber->stack;                                                                      \
    stackLimit = fiber->stackLast;                                                                 \
    frame = &fiber->frames[fiber->frameCount - 1];                                                 \
    slots = frame->slots;                                                                          \
                                                                                                   \
  } while (0)

#define RESTORE_FRAME()                                                                            \
  do {                                                                                             \
    frame = &fiber->frames[fiber->frameCount - 1];                                                 \
    slots = frame->slots;                                                                          \
  } while (0)

#define PROTECT_LIGHT(action)                                                                      \
  do {                                                                                             \
    action;                                                                                        \
    RESTORE_FRAME();                                                                               \
  } while (0)

#define RESTORE_SLOTS_ONLY()                                                                       \
  do {                                                                                             \
    stackBase = fiber->stack;                                                                      \
    stackLimit = fiber->stackLast;                                                                 \
    frame = &fiber->frames[fiber->frameCount - 1];                                                 \
    slots = frame->slots;                                                                          \
                                                                                                   \
  } while (0)

#define PROTECT(action)                                                                            \
  do {                                                                                             \
    SAVE_PC();                                                                                     \
    action;                                                                                        \
    RESTORE_POINTERS();                                                                            \
  } while (0)

#define HALF_PROTECT(action)                                                                       \
  do {                                                                                             \
    SAVE_PC();                                                                                     \
    action;                                                                                        \
  } while (0)

#define REFRESH_CACHE() RESTORE_POINTERS()
#define REFRESH_SLOTS() RESTORE_SLOTS_ONLY()

#define VM_FETCH()                                                                                 \
  do {                                                                                             \
    if (trap) {                                                                                    \
      if (yieldPending_ || hasError_) {                                                            \
      }                                                                                            \
      UPDATE_TRAP();                                                                               \
    }                                                                                              \
    inst = *ip++;                                                                                  \
  } while (0)

  (void)stackBase;
  (void)stackLimit;
  (void)trap;

  SPT_DISPATCH_LOOP_BEGIN()

  SPT_OPCODE(OP_MOVE) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    slots[A] = slots[B];
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADK) {
    uint8_t A = GETARG_A(inst);
    uint32_t Bx = GETARG_Bx(inst);

    const Value *k = frame->closure->proto->k;
    slots[A] = k[Bx];
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADBOOL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    slots[A] = Value::boolean(B != 0);
    if (C != 0)
      ip++;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADNIL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    for (int i = 0; i <= B; ++i) {
      slots[A + i] = Value::nil();
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWLIST) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);

    ListObject *list;
    PROTECT(list = allocateList(B));
    slots[A] = Value::object(list);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWMAP) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);

    MapObject *map;
    PROTECT(map = allocateMap(B));
    slots[A] = Value::object(map);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_GETINDEX) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value container = slots[B];
    Value index = slots[C];

    if (container.isList()) {
      auto *list = static_cast<ListObject *>(container.asGC());
      if (!index.isInt()) {
        SAVE_PC();
        runtimeError("List index must be integer");
        return InterpretResult::RUNTIME_ERROR;
      }
      int64_t idx = index.asInt();
      if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
        SAVE_PC();
        runtimeError("List index out of range");
        return InterpretResult::RUNTIME_ERROR;
      }
      slots[A] = list->elements[idx];
    } else if (container.isMap()) {
      auto *map = static_cast<MapObject *>(container.asGC());
      slots[A] = map->get(index);
    } else {
      SAVE_PC();
      runtimeError("Cannot index type: %s", container.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SETINDEX) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value container = slots[A];
    Value index = slots[B];
    Value value = slots[C];

    if (container.isList()) {
      auto *list = static_cast<ListObject *>(container.asGC());
      if (!index.isInt()) {
        SAVE_PC();
        runtimeError("List index must be integer");
        return InterpretResult::RUNTIME_ERROR;
      }
      int64_t idx = index.asInt();
      if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
        SAVE_PC();
        runtimeError("List index out of range");
        return InterpretResult::RUNTIME_ERROR;
      }
      list->elements[idx] = value;
    } else if (container.isMap()) {
      auto *map = static_cast<MapObject *>(container.asGC());
      map->set(index, value);
    } else {
      SAVE_PC();
      runtimeError("Cannot index-assign to type: %s", container.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_GETFIELD) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value object = slots[B];

    const Value *k = frame->closure->proto->k;
    Value keyVal = k[C];

    if (!keyVal.isString()) {
      SAVE_PC();
      runtimeError("GETFIELD requires string key constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    StringObject *fieldName = keyVal.asString();

    if (object.isList() || object.isMap() || object.isString() || object.isFiber()) {
      Value result;
      bool success;

      PROTECT(success = StdlibDispatcher::getProperty(this, object, fieldName, result));
      if (success) {
        slots[A] = result;
        SPT_DISPATCH();
      }
      if (!object.isMap()) {
        SAVE_PC();
        runtimeError("Type '%s' has no property '%s'", object.typeName(), fieldName->c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    if (object.isInstance()) {
      auto *instance = static_cast<Instance *>(object.asGC());

      Value result = instance->getField(fieldName);
      if (result.isNil() && instance->klass) {
        if (Value *v = instance->klass->methods.get(fieldName)) {
          result = *v;
        }
      }
      slots[A] = result;
      SPT_DISPATCH();
    }

    if (object.isNativeInstance()) {
      auto *instance = static_cast<NativeInstance *>(object.asGC());

      Value result = instance->getField(fieldName);

      if (result.isNil() && instance->klass) {
        if (Value *v = instance->klass->methods.get(fieldName)) {
          result = *v;
        }
      }
      slots[A] = result;
      SPT_DISPATCH();
    }

    if (object.isClass()) {
      auto *klass = static_cast<ClassObject *>(object.asGC());
      const SymbolTable &syms = this->symbols();

      if (klass->name == "Fiber" && fieldName == syms.current) {
        slots[A] = Value::object(currentFiber_);
        SPT_DISPATCH();
      }

      auto it = klass->methods.find(fieldName);
      if (it != klass->methods.end()) {
        slots[A] = it->second;
        SPT_DISPATCH();
      }

      auto itStatic = klass->statics.find(fieldName);
      slots[A] = (itStatic != klass->statics.end()) ? itStatic->second : Value::nil();
      SPT_DISPATCH();
    }

    if (object.isMap()) {
      auto *map = static_cast<MapObject *>(object.asGC());

      Value result = map->get(Value::object(fieldName));
      if (result.isNil()) {
        auto it = globals_.find(fieldName);
        if (it != globals_.end()) {
          result = it->second;
        }
      }
      slots[A] = result;
      SPT_DISPATCH();
    }

    SAVE_PC();
    runtimeError("Cannot get field '%s' from type: %s", fieldName->c_str(), object.typeName());
    return InterpretResult::RUNTIME_ERROR;
  }

  SPT_OPCODE(OP_SETFIELD) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value *k = frame->closure->proto->k;
    Value keyVal = k[B];

    if (!keyVal.isString()) {
      SAVE_PC();
      runtimeError("SETFIELD requires string key constant");
      return InterpretResult::RUNTIME_ERROR;
    }

    StringObject *fieldNameStr = static_cast<StringObject *>(keyVal.asGC());

    Value object = slots[A];
    Value value = slots[C];

    if (object.isInstance()) {
      auto *instance = static_cast<Instance *>(object.asGC());
      instance->setField(fieldNameStr, value);
    } else if (object.isNativeInstance()) {

      auto *instance = static_cast<NativeInstance *>(object.asGC());
      instance->setField(fieldNameStr, value);
    } else if (object.isClass()) {
      auto *klass = static_cast<ClassObject *>(object.asGC());
      klass->methods[fieldNameStr] = value;
    } else if (object.isMap()) {
      auto *map = static_cast<MapObject *>(object.asGC());
      map->set(Value::object(fieldNameStr), value);
    } else {
      SAVE_PC();
      runtimeError("Cannot set field '%s' on type: %s", fieldNameStr->c_str(), object.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWCLASS) {
    uint8_t A = GETARG_A(inst);
    uint32_t Bx = GETARG_Bx(inst);

    const Value *k = frame->closure->proto->k;
    Value nameVal = k[Bx];

    if (!nameVal.isString()) {
      SAVE_PC();
      runtimeError("Class name must be string constant");
      return InterpretResult::RUNTIME_ERROR;
    }
    StringObject *nameStrObj = nameVal.asString();

    ClassObject *klass;
    PROTECT(klass = allocateClass(nameStrObj->str()));

    slots[A] = Value::object(klass);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_NEWOBJ) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    Value classValue = slots[B];

    if (!classValue.isClass()) {
      SAVE_PC();
      runtimeError("Cannot instantiate non-class type '%s'", classValue.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }

    ClassObject *klass = static_cast<ClassObject *>(classValue.asGC());

    Instance *instance;
    PROTECT(instance = allocateInstance(klass));

    klass = static_cast<ClassObject *>(slots[B].asGC());
    Value instanceVal = Value::object(instance);

    slots[A] = instanceVal;

    auto it = klass->methods.find(symbols().init);
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
          SAVE_PC();
          runtimeError("init expects %d arguments, got %d", proto->numParams, providedArgs);
          return InterpretResult::RUNTIME_ERROR;
        }

        if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
          SAVE_PC();
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

        SAVE_PC();

        CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
        newFrame->closure = closure;
        newFrame->ip = proto->code.data();
        newFrame->expectedResults = 0;
        newFrame->slots = newSlots;
        newFrame->returnTo = slots + A;
        newFrame->deferBase = fiber->deferTop;

        frame = newFrame;
        slots = frame->slots;

        LOAD_PC();

      } else if (initializer.isNativeFunc()) {
        NativeFunction *native = static_cast<NativeFunction *>(initializer.asGC());

        if (native->arity != -1 && C != native->arity) {
          SAVE_PC();
          runtimeError("Native init expects %d arguments, got %d", native->arity, C);
          return InterpretResult::RUNTIME_ERROR;
        }

        Value *argsStart = &slots[B + 1];

        PROTECT(native->function(this, instanceVal, C, argsStart));
      } else {
        SAVE_PC();
        runtimeError("init method must be a function");
        return InterpretResult::RUNTIME_ERROR;
      }
    } else {
      if (C > 0) {
        SAVE_PC();
        runtimeError("Class '%s' has no init method but arguments were provided.",
                     klass->name.c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_GETUPVAL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    if (B >= frame->closure->upvalueCount) {
      SAVE_PC();
      runtimeError("Invalid upvalue index: %d", B);
      return InterpretResult::RUNTIME_ERROR;
    }
    UpValue *upval = frame->closure->upvalues[B];
    slots[A] = *upval->location;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SETUPVAL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    if (B >= frame->closure->upvalueCount) {
      SAVE_PC();
      runtimeError("Invalid upvalue index: %d", B);
      return InterpretResult::RUNTIME_ERROR;
    }
    UpValue *upval = frame->closure->upvalues[B];
    *upval->location = slots[A];
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CLOSURE) {
    uint8_t A = GETARG_A(inst);
    uint32_t Bx = GETARG_Bx(inst);
    const Prototype &proto = frame->closure->proto->protos[Bx];

    Closure *closure;
    PROTECT(closure = allocateClosure(&proto));
    protect(Value::object(closure));
    REFRESH_SLOTS();

    for (size_t i = 0; i < proto.numUpvalues; ++i) {
      const auto &uvDesc = proto.upvalues[i];
      if (uvDesc.isLocal) {
        UpValue *uv;
        PROTECT_LIGHT(uv = captureUpvalue(&slots[uvDesc.index]));
        closure->upvalues[i] = uv;
      } else {
        closure->upvalues[i] = frame->closure->upvalues[uvDesc.index];
      }
    }

    unprotect(1);
    slots[A] = Value::object(closure);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CLOSE_UPVALUE) {
    uint8_t A = GETARG_A(inst);
    closeUpvalues(&slots[A]);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_ADD) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value &b = slots[B];
    const Value &c = slots[C];

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
      SAVE_PC();
      runtimeError("Operands must be numbers or strings");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SUB) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value &b = slots[B];
    const Value &c = slots[C];

    if (b.isInt() && c.isInt()) {
      slots[A] = Value::integer(b.asInt() - c.asInt());
    } else if (b.isNumber() && c.isNumber()) {
      double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
      slots[A] = Value::number(left - right);
    } else {
      SAVE_PC();
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_MUL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value &b = slots[B];
    const Value &c = slots[C];

    if (b.isInt() && c.isInt()) {
      slots[A] = Value::integer(b.asInt() * c.asInt());
    } else if (b.isNumber() && c.isNumber()) {
      double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
      double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
      slots[A] = Value::number(left * right);
    } else {
      SAVE_PC();
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_DIV) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value &b = slots[B];
    const Value &c = slots[C];

    if (!b.isNumber() || !c.isNumber()) {
      SAVE_PC();
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }

    double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
    double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();

    if (right == 0.0) {
      SAVE_PC();
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
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isNumber() || !c.isNumber()) {
      SAVE_PC();
      runtimeError("Operands must be numbers");
      return InterpretResult::RUNTIME_ERROR;
    }

    double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
    double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();

    if (right == 0.0) {
      SAVE_PC();
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
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isInt() || !c.isInt()) {
      SAVE_PC();
      runtimeError("Modulo requires integer operands");
      return InterpretResult::RUNTIME_ERROR;
    }

    int64_t left = b.asInt();
    int64_t right = c.asInt();

    if (right == 0) {
      SAVE_PC();
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
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
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
      SAVE_PC();
      runtimeError("Operand must be a number");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

#define SPT_BW_BINARY_OP(opName, op)                                                               \
  SPT_OPCODE(opName) {                                                                             \
    uint8_t A = GETARG_A(inst);                                                                    \
    uint8_t B = GETARG_B(inst);                                                                    \
    uint8_t C = GETARG_C(inst);                                                                    \
    Value b = slots[B];                                                                            \
    Value c = slots[C];                                                                            \
                                                                                                   \
    if (!b.isInt() || !c.isInt()) {                                                                \
      SAVE_PC();                                                                                   \
      runtimeError("Operands must be integers");                                                   \
      return InterpretResult::RUNTIME_ERROR;                                                       \
    }                                                                                              \
    slots[A] = Value::integer(b.asInt() op c.asInt());                                             \
    SPT_DISPATCH();                                                                                \
  }

  SPT_BW_BINARY_OP(OP_BAND, &)
  SPT_BW_BINARY_OP(OP_BOR, |)

  SPT_OPCODE(OP_BXOR) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value b = slots[B];
    Value c = slots[C];
    if (!b.isInt() || !c.isInt()) {
      SAVE_PC();
      runtimeError("Operands must be integers");
      return InterpretResult::RUNTIME_ERROR;
    }
    slots[A] = Value::integer(b.asInt() ^ c.asInt());
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_BNOT) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    Value b = slots[B];

    if (b.isInt()) {
      slots[A] = Value::integer(~b.asInt());
    } else {
      SAVE_PC();
      runtimeError("Operand must be a integer");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

#undef SPT_BW_BINARY_OP

  SPT_OPCODE(OP_SHL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isInt() || !c.isInt()) {
      SAVE_PC();
      runtimeError("Operands must be integers");
      return InterpretResult::RUNTIME_ERROR;
    }

    int64_t shiftAmount = c.asInt();

    if (shiftAmount < 0 || shiftAmount >= 64) {
      SAVE_PC();
      runtimeError("Shift amount must be between 0 and 63");
      return InterpretResult::RUNTIME_ERROR;
    }
    slots[A] = Value::integer(b.asInt() << shiftAmount);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_SHR) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    Value b = slots[B];
    Value c = slots[C];

    if (!b.isInt() || !c.isInt()) {
      SAVE_PC();
      runtimeError("Operands must be integers");
      return InterpretResult::RUNTIME_ERROR;
    }

    int64_t shiftAmount = c.asInt();

    if (shiftAmount < 0 || shiftAmount >= 64) {
      SAVE_PC();
      runtimeError("Shift amount must be between 0 and 63");
      return InterpretResult::RUNTIME_ERROR;
    }
    slots[A] = Value::integer(b.asInt() >> shiftAmount);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_JMP) {
    int32_t sBx = GETARG_sBx(inst);
    ip += sBx;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EQ) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);
    bool equal = valuesEqual(slots[A], slots[B]);
    if (equal != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LT) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value &a = slots[A];
    const Value &b = slots[B];

    bool result = false;
    if (SPT_LIKELY(a.isInt() && b.isInt())) {
      result = a.asInt() < b.asInt();
    } else if (a.isNumber() && b.isNumber()) {
      double left = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
      double right = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();

      result = left < right;
    } else {
      SAVE_PC();
      runtimeError("Cannot compare non-numeric types");
      return InterpretResult::RUNTIME_ERROR;
    }

    if (result != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LE) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value &a = slots[A];
    const Value &b = slots[B];

    bool result = false;
    if (a.isInt() && b.isInt()) {
      result = a.asInt() <= b.asInt();
    } else if (a.isNumber() && b.isNumber()) {
      double left = a.isInt() ? static_cast<double>(a.asInt()) : a.asFloat();
      double right = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();

      result = left <= right;
    } else {
      SAVE_PC();
      runtimeError("Cannot compare non-numeric types");
      return InterpretResult::RUNTIME_ERROR;
    }

    if (result != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_TEST) {
    uint8_t A = GETARG_A(inst);
    uint8_t C = GETARG_C(inst);
    bool isTruthy = slots[A].isTruthy();
    if (isTruthy != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CALL) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

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
        SAVE_PC();
        runtimeError("Function '%s' expects %d arguments, got %d", proto->name.c_str(),
                     proto->numParams, argCount);
        return InterpretResult::RUNTIME_ERROR;
      }

      if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
        SAVE_PC();
        runtimeError("Stack overflow");
        return InterpretResult::RUNTIME_ERROR;
      }

      Value *newSlots = slots + A + 1;
      Value *neededTop = newSlots + proto->maxStackSize;

      if (SPT_UNLIKELY(neededTop > fiber->stackLast)) {
        int needed = static_cast<int>(neededTop - fiber->stackTop);
        fiber->ensureStack(needed);
        REFRESH_CACHE();
        newSlots = slots + A + 1;
      }
      if (SPT_UNLIKELY(fiber->frameCount + 1 > fiber->framesCapacity)) {
        fiber->ensureFrames(1);
        REFRESH_CACHE();
        newSlots = slots + A + 1;
      }

      Value *argsStart = newSlots;

      int numParams = proto->numParams;

      for (int i = argCount; i < numParams; ++i) {
        argsStart[i] = Value::nil();
      }

      fiber->stackTop = argsStart + proto->maxStackSize;

      SAVE_PC();

      CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();
      newFrame->expectedResults = expectedResults;
      newFrame->slots = newSlots;
      newFrame->returnTo = slots + A;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;
      slots = frame->slots;

      LOAD_PC();

    } else if (callee.isNativeFunc()) {
      NativeFunction *native = static_cast<NativeFunction *>(callee.asGC());

      if (native->arity != -1 && argCount != native->arity) {
        SAVE_PC();
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
    } else {
      SAVE_PC();
      runtimeError("Attempt to call a non-function value of type '%s'", callee.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_CALL_SELF) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

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
      SAVE_PC();
      runtimeError("Function '%s' expects %d arguments, got %d", proto->name.c_str(),
                   proto->numParams, argCount);
      return InterpretResult::RUNTIME_ERROR;
    }

    if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
      SAVE_PC();
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

    SAVE_PC();

    CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
    newFrame->closure = closure;
    newFrame->ip = proto->code.data();
    newFrame->expectedResults = expectedResults;
    newFrame->slots = newSlots;
    newFrame->returnTo = slots + A;
    newFrame->deferBase = fiber->deferTop;

    frame = newFrame;
    slots = frame->slots;

    LOAD_PC();

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_INVOKE) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    uint32_t nextInst = *ip++;
    uint32_t methodIdx = GETARG_Ax(nextInst);

    Value receiver = slots[A];

    int userArgCount;
    if (B == 0) {
      userArgCount = static_cast<int>(fiber->stackTop - (slots + A + 1));
    } else {
      userArgCount = B - 1;
    }

    const Value *k = frame->closure->proto->k;
    Value methodNameVal = k[methodIdx];
    if (!methodNameVal.isString()) {
      SAVE_PC();
      runtimeError("OP_INVOKE: method name constant must be string");
      return InterpretResult::RUNTIME_ERROR;
    }
    StringObject *methodName = methodNameVal.asString();

    Value method = Value::nil();
    Value *argsStart = &slots[A + 1];

    if (SPT_LIKELY(receiver.isInstance())) {
      auto *instance = static_cast<Instance *>(receiver.asGC());

      method = instance->getField(methodName);

      if (method.isNil() && instance->klass) {
        if (Value *v = instance->klass->methods.get(methodName)) {
          method = *v;
        }
      }

      if (method.isNil()) {
        SAVE_PC();
        runtimeError("Instance has no method '%s'", methodName->c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    } else if (receiver.isNativeInstance()) {
      auto *instance = static_cast<NativeInstance *>(receiver.asGC());

      method = instance->getField(methodName);

      if (method.isNil() && instance->klass) {
        if (Value *v = instance->klass->methods.get(methodName)) {
          method = *v;
        }
      }

      if (method.isNil()) {
        SAVE_PC();
        const char *className = instance->klass ? instance->klass->name.c_str() : "unknown";
        runtimeError("NativeInstance of '%s' has no method '%s'", className, methodName->c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    } else if (receiver.isClass()) {
      auto *klass = static_cast<ClassObject *>(receiver.asGC());

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
        SAVE_PC();
        runtimeError("Class '%s' has no method '%s'", klass->name.c_str(), methodName->c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    else if (receiver.isList() || receiver.isMap() || receiver.isString() || receiver.isFiber()) {
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
          SAVE_PC();
          runtimeError("'%s.%s' is a property, not a method", receiver.typeName(),
                       methodName->c_str());
          return InterpretResult::RUNTIME_ERROR;
        }
      } else if (receiver.isMap()) {
        auto *map = static_cast<MapObject *>(slots[A].asGC());
        method = map->get(Value::object(methodName));
      }

      if (method.isNil()) {
        SAVE_PC();
        runtimeError("Type '%s' has no method '%s'", receiver.typeName(), methodName->c_str());
        return InterpretResult::RUNTIME_ERROR;
      }
    } else {
      SAVE_PC();
      runtimeError("Cannot invoke method '%s' on type '%s'", methodName->c_str(),
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
          SAVE_PC();
          runtimeError("Method '%s' expects %d arguments (including 'this'), got %d",
                       methodName->c_str(), proto->numParams, totalArgsProvided);
          return InterpretResult::RUNTIME_ERROR;
        }
        dropThis = false;
      } else {
        if (!proto->isVararg && totalArgsProvided != (proto->numParams + 1)) {
          SAVE_PC();
          runtimeError("Method '%s' expects %d arguments, got %d", methodName->c_str(),
                       proto->numParams, totalArgsProvided - 1);
          return InterpretResult::RUNTIME_ERROR;
        }
        dropThis = true;
      }

      if (FRAME_COUNT >= FiberObject::MAX_FRAMES) {
        SAVE_PC();
        runtimeError("Stack overflow");
        return InterpretResult::RUNTIME_ERROR;
      }

      Value *newSlots = slots + A + (dropThis ? 1 : 0);
      int neededStack = static_cast<int>((newSlots - fiber->stack) + proto->maxStackSize);

      if (SPT_UNLIKELY(neededStack > static_cast<int>(fiber->stackSize))) {
        fiber->ensureStack(neededStack);
        RESTORE_SLOTS_ONLY();
        newSlots = slots + A + (dropThis ? 1 : 0);
      }
      if (SPT_UNLIKELY(fiber->frameCount + 1 > fiber->framesCapacity)) {
        fiber->ensureFrames(1);
        frame = &fiber->frames[fiber->frameCount - 1];
      }

      int argsPushed;
      if (dropThis) {
        newSlots = slots + A + 1;
        argsPushed = userArgCount;
      } else {
        newSlots = slots + A;
        argsPushed = totalArgsProvided;
      }

      Value *targetStackTop = newSlots + proto->maxStackSize;

      for (int i = argsPushed; i < proto->numParams; ++i) {
        newSlots[i] = Value::nil();
      }

      STACK_TOP = targetStackTop;

      SAVE_PC();

      CallFrame *newFrame = &fiber->frames[FRAME_COUNT++];
      newFrame->closure = closure;
      newFrame->ip = proto->code.data();
      newFrame->expectedResults = C - 1;
      newFrame->slots = newSlots;
      newFrame->returnTo = slots + A;
      newFrame->deferBase = fiber->deferTop;

      frame = newFrame;
      slots = frame->slots;

      LOAD_PC();

    } else if (method.isNativeFunc()) {
      NativeFunction *native = static_cast<NativeFunction *>(method.asGC());
      protect(method);
      REFRESH_SLOTS();

      if (native->arity != -1 && userArgCount != native->arity) {
        SAVE_PC();
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
      SAVE_PC();
      runtimeError("'%s.%s' is not callable", receiver.typeName(), methodName->c_str());
      return InterpretResult::RUNTIME_ERROR;
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_RETURN) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);

    if (fiber->deferTop > frame->deferBase) {
      SAVE_PC();
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

    LOAD_PC();

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
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
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

    LOAD_PC();

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
    uint8_t A = GETARG_A(inst);
    uint32_t Bx = GETARG_Bx(inst);

    const Value *k = frame->closure->proto->k;
    Value moduleNameVal = k[Bx];

    if (!moduleNameVal.isString()) {
      SAVE_PC();
      runtimeError("Module name must be a string constant");
      return InterpretResult::RUNTIME_ERROR;
    }
    StringObject *moduleNameStrObj = moduleNameVal.asString();
    const char *currentPath = frame->closure->proto->source.c_str();

    Value exportsTable;
    PROTECT(exportsTable = moduleManager_->loadModule(moduleNameStrObj->str(), currentPath));

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
        SAVE_PC();
        runtimeError("Import error: %s", errorMsg);
        return InterpretResult::RUNTIME_ERROR;
      }
    }

    slots[A] = exportsTable;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_IMPORT_FROM) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value *k = frame->closure->proto->k;
    Value moduleNameVal = k[B];
    Value symbolNameVal = k[C];

    if (!moduleNameVal.isString() || !symbolNameVal.isString()) {
      SAVE_PC();
      runtimeError("Module and symbol names must be string constants");
      return InterpretResult::RUNTIME_ERROR;
    }
    const char *currentPath = frame->closure->proto->source.c_str();

    Value exportsTable;
    PROTECT(exportsTable =
                moduleManager_->loadModule(moduleNameVal.asString()->str(), currentPath));

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
        SAVE_PC();
        runtimeError("Import error: %s", errorMsg);
        return InterpretResult::RUNTIME_ERROR;
      }

      StringObject *symKey = symbolNameVal.asString();
      exports = static_cast<MapObject *>(exportsTable.asGC());
      Value symbolValue = exports->get(Value::object(symKey));
      slots[A] = symbolValue;
    } else {
      slots[A] = Value::nil();
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_DEFER) {
    uint8_t A = GETARG_A(inst);
    Value deferClosure = slots[A];

    if (!deferClosure.isClosure()) {
      SAVE_PC();
      runtimeError("defer requires a function");
      return InterpretResult::RUNTIME_ERROR;
    }
    if (!frame->closure->proto->useDefer) {
      SAVE_PC();
      runtimeError("compiler error because defer was not used");
      return InterpretResult::RUNTIME_ERROR;
    }

    fiber->ensureDefers(1);

    fiber->deferStack[fiber->deferTop++] = deferClosure;

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_ADDI) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    int8_t sC = static_cast<int8_t>(GETARG_C(inst));
    Value b = slots[B];

    if (b.isInt()) {
      slots[A] = Value::integer(b.asInt() + sC);
    } else if (b.isFloat()) {
      slots[A] = Value::number(b.asFloat() + sC);
    } else {
      SAVE_PC();
      runtimeError("ADDI requires numeric operand");
      return InterpretResult::RUNTIME_ERROR;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EQK) {
    uint8_t A = GETARG_A(inst);
    uint8_t B = GETARG_B(inst);
    uint8_t C = GETARG_C(inst);

    const Value *kv = frame->closure->proto->k;
    bool equal = valuesEqual(slots[A], kv[B]);
    if (equal != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_EQI) {
    uint8_t A = GETARG_A(inst);
    int8_t sB = static_cast<int8_t>(GETARG_B(inst));
    uint8_t C = GETARG_C(inst);
    Value a = slots[A];
    bool equal = false;
    if (a.isInt()) {
      equal = (a.asInt() == sB);
    } else if (a.isFloat()) {
      equal = (a.asFloat() == static_cast<double>(sB));
    } else {
      SAVE_PC();
      runtimeError("Cannot compare %s with integer", a.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    if (equal != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LTI) {
    uint8_t A = GETARG_A(inst);
    int8_t sB = static_cast<int8_t>(GETARG_B(inst));
    uint8_t C = GETARG_C(inst);
    Value a = slots[A];
    bool result = false;
    if (a.isInt()) {
      result = (a.asInt() < sB);
    } else if (a.isFloat()) {
      result = (a.asFloat() < static_cast<double>(sB));
    } else {
      SAVE_PC();
      runtimeError("Cannot compare %s with integer", a.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    if (result != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LEI) {
    uint8_t A = GETARG_A(inst);
    int8_t sB = static_cast<int8_t>(GETARG_B(inst));
    uint8_t C = GETARG_C(inst);
    Value a = slots[A];
    bool result = false;
    if (a.isInt()) {
      result = (a.asInt() <= sB);
    } else if (a.isFloat()) {
      result = (a.asFloat() <= static_cast<double>(sB));
    } else {
      SAVE_PC();
      runtimeError("Cannot compare %s with integer", a.typeName());
      return InterpretResult::RUNTIME_ERROR;
    }
    if (result != (C != 0)) {
      ip++;
    }
    SPT_DISPATCH();
  }

#define CHECK_NUM(val, msg)                                                                        \
  if (!val.isNumber()) {                                                                           \
    SAVE_PC();                                                                                     \
    runtimeError(msg);                                                                             \
    return InterpretResult::RUNTIME_ERROR;                                                         \
  }

  SPT_OPCODE(OP_FORPREP) {
    uint8_t A = GETARG_A(inst);
    int32_t sBx = GETARG_sBx(inst);

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

    ip += sBx;
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_FORLOOP) {
    uint8_t A = GETARG_A(inst);
    int32_t sBx = GETARG_sBx(inst);

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
          ip += sBx;
      } else {
        if (iIdx >= iLimit)
          ip += sBx;
      }
    } else {
      double nStep = valueToNum(step);
      double nIdx = valueToNum(idx) + nStep;
      double nLimit = valueToNum(limit);

      idx = Value::number(nIdx);

      if (nStep > 0) {
        if (nIdx <= nLimit)
          ip += sBx;
      } else {
        if (nIdx >= nLimit)
          ip += sBx;
      }
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_LOADI) {
    uint8_t A = GETARG_A(inst);
    int32_t sBx = GETARG_sBx(inst);
    slots[A] = Value::integer(sBx);
    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_TFORCALL) {
    const uint8_t A = GETARG_A(inst);
    const uint8_t C = GETARG_C(inst);

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

      SAVE_PC();

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

      slots = frame->slots;

      LOAD_PC();

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
      SAVE_PC();
      runtimeError("attempt to iterate over a non-function value");
      return InterpretResult::RUNTIME_ERROR;
    }

    SPT_DISPATCH();
  }

  SPT_OPCODE(OP_TFORLOOP) {

    const uint8_t A = GETARG_A(inst);

    Value *var1 = &slots[A + 3];

    if (!var1->isNil()) {

      slots[A + 2] = *var1;

    } else {

      const int32_t sBx = GETARG_sBx(inst);
      ip += sBx;
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
#undef VM_FETCH
#undef UPDATE_BASE
#undef SAVE_PC
#undef LOAD_PC
#undef UPDATE_TRAP
}

} // namespace spt