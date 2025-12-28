#include "VM.h"
#include <algorithm>
#include <cstdarg>
#include <cstring>
#include <stdexcept>

namespace spt {

VM::VM(const VMConfig &config) : config_(config), gc_(this, {}) {

  stack_.resize(config.stackSize);
  stackTop_ = stack_.data();
  frames_.reserve(64);

  globals_.reserve(256);

  ModuleManagerConfig moduleConfig;
  moduleConfig.enableCache = true;
  moduleConfig.enableHotReload = config.enableHotReload;
  moduleManager_ = std::make_unique<ModuleManager>(this, moduleConfig);
  if (!config.modulePaths.empty()) {

    auto loader = std::make_unique<FileSystemLoader>(config.modulePaths);
    moduleManager_->setLoader(std::move(loader));
  }

  registerBuiltinFunctions();
}

VM::~VM() {

  frames_.clear();
  frameCount_ = 0;
  stackTop_ = stack_.data();

  globals_.clear();

  strings_.clear();

  modules_.clear();

  openUpvalues_ = nullptr;
}

void VM::registerBuiltinFunctions() {

  registerNative(
      "print",
      [this](int argc, Value *args) -> Value {
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
      "type",
      [this](int argc, Value *args) -> Value {
        if (argc != 1)
          return Value::nil();
        StringObject *str = this->allocateString(args[0].typeName());
        return Value::object(str);
      },
      1);
}

void VM::ensureStack(int neededSlots) {

  if (stackTop_ + neededSlots > stack_.data() + config_.stackSize) {
    runtimeError("Stack overflow: needed %d slots", neededSlots);
    return;
  }

  for (int i = 0; i < neededSlots; ++i) {
    stackTop_[i] = Value::nil();
  }

  stackTop_ += neededSlots;
}

InterpretResult VM::interpret(const CompiledChunk &chunk) {

  resetStack();
  frames_.clear();

  Closure *mainClosure = allocateClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

  CallFrame frame;
  frame.closure = mainClosure;
  frame.ip = mainClosure->proto->code.data();
  frame.slots = stackTop_;

  ensureStack(mainClosure->proto->maxStackSize);

  frames_.push_back(frame);
  frameCount_ = 1;

  return run(0);
}

InterpretResult VM::call(Closure *closure, int argCount) {

  if (!closure->proto->isVararg && argCount != closure->proto->numParams) {
    runtimeError("Function '%s' expects %d arguments, got %d", closure->proto->name.c_str(),
                 closure->proto->numParams, argCount);
    return InterpretResult::RUNTIME_ERROR;
  }

  if (frameCount_ == 64) {
    runtimeError("Stack overflow");
    return InterpretResult::RUNTIME_ERROR;
  }

  CallFrame *frame = &frames_[frameCount_++];
  frame->closure = closure;
  frame->ip = closure->proto->code.data();
  frame->expectedResults = 1;

  Value *argsStart = stackTop_ - argCount;
  frame->slots = argsStart;

  ensureStack(closure->proto->maxStackSize);

  Value *frameEnd = frame->slots + closure->proto->maxStackSize;
  for (Value *slot = frame->slots + argCount; slot < frameEnd; ++slot) {
    *slot = Value::nil();
  }

  stackTop_ = frameEnd;

  return run(frameCount_ - 1);
}

InterpretResult VM::run(int minFrameCount) {

  CallFrame *frame = &frames_[frameCount_ - 1];

#define READ_INSTRUCTION() (*reinterpret_cast<const uint32_t *>(frame->ip++))
#define READ_BYTE() (*(frame->ip++))
#define DISPATCH() goto dispatch_loop

dispatch_loop:
  for (;;) {

    uint32_t instruction = READ_INSTRUCTION();
    OpCode opcode = GET_OPCODE(instruction);
    uint8_t A = GETARG_A(instruction);
    uint8_t B = GETARG_B(instruction);
    uint8_t C = GETARG_C(instruction);
    uint32_t Bx = GETARG_Bx(instruction);
    int32_t sBx = GETARG_sBx(instruction);

    if (config_.debugMode) {
      size_t pc = frame->ip - frame->closure->proto->code.data() - 1;
      printf("[%04zd] OP=%2d A=%3d B=%3d C=%3d\n", pc, static_cast<int>(opcode), A, B, C);
    }

    switch (opcode) {

    case OpCode::OP_MOVE: {

      frame->slots[A] = frame->slots[B];
      break;
    }

    case OpCode::OP_LOADK: {

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

      frame->slots[A] = value;
      break;
    }

    case OpCode::OP_LOADBOOL: {

      frame->slots[A] = Value::boolean(B != 0);
      if (C != 0)
        frame->ip++;
      break;
    }

    case OpCode::OP_LOADNIL: {

      for (int i = 0; i <= B; ++i) {
        frame->slots[A + i] = Value::nil();
      }
      break;
    }

    case OpCode::OP_NEWLIST: {

      ListObject *list = allocateList(B);
      frame->slots[A] = Value::object(list);
      break;
    }

    case OpCode::OP_NEWMAP: {

      MapObject *map = allocateMap(B);
      frame->slots[A] = Value::object(map);
      break;
    }

    case OpCode::OP_GETINDEX: {

      Value container = frame->slots[B];
      Value index = frame->slots[C];

      if (container.isList()) {
        auto *list = static_cast<ListObject *>(container.asGC());
        if (!index.isInt()) {
          runtimeError("List index must be integer");
          return InterpretResult::RUNTIME_ERROR;
        }
        int64_t idx = index.asInt();
        if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
          runtimeError("List index %lld out of range [0, %zu)", idx, list->elements.size());
          return InterpretResult::RUNTIME_ERROR;
        }
        frame->slots[A] = list->elements[idx];

      } else if (container.isMap()) {
        auto *map = static_cast<MapObject *>(container.asGC());
        frame->slots[A] = map->get(index);

      } else {
        runtimeError("Cannot index type: %s", container.typeName());
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_SETINDEX: {

      Value container = frame->slots[A];
      Value index = frame->slots[B];
      Value value = frame->slots[C];

      if (container.isList()) {
        auto *list = static_cast<ListObject *>(container.asGC());
        if (!index.isInt()) {
          runtimeError("List index must be integer");
          return InterpretResult::RUNTIME_ERROR;
        }
        int64_t idx = index.asInt();
        if (idx < 0 || idx >= static_cast<int64_t>(list->elements.size())) {
          runtimeError("List index %lld out of range [0, %zu)", idx, list->elements.size());
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
      break;
    }

    case OpCode::OP_GETFIELD: {

      Value object = frame->slots[B];
      const auto &keyConst = frame->closure->proto->constants[C];

      if (!std::holds_alternative<std::string>(keyConst)) {
        runtimeError("GETFIELD requires string key constant");
        return InterpretResult::RUNTIME_ERROR;
      }

      const std::string &fieldName = std::get<std::string>(keyConst);

      if (object.isInstance()) {

        auto *instance = static_cast<Instance *>(object.asGC());

        Value result = instance->getField(fieldName);

        if (result.isNil() && instance->klass) {
          auto it = instance->klass->methods.find(fieldName);
          if (it != instance->klass->methods.end()) {
            result = it->second;
          }
        }

        frame->slots[A] = result;

      } else if (object.isClass()) {

        auto *klass = static_cast<ClassObject *>(object.asGC());
        auto it = klass->methods.find(fieldName);
        if (it != klass->methods.end()) {
          frame->slots[A] = it->second;
        } else {

          auto itStatic = klass->statics.find(fieldName);
          frame->slots[A] = (itStatic != klass->statics.end()) ? itStatic->second : Value::nil();
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

        frame->slots[A] = result;

      } else {
        runtimeError("Cannot get field '%s' from type: %s", fieldName.c_str(), object.typeName());
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_SETFIELD: {

      Value object = frame->slots[A];
      const auto &keyConst = frame->closure->proto->constants[B];
      Value value = frame->slots[C];

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
      break;
    }

    case OpCode::OP_NEWCLASS: {

      const auto &nameConst = frame->closure->proto->constants[Bx];
      if (!std::holds_alternative<std::string>(nameConst)) {
        runtimeError("Class name must be string constant");
        return InterpretResult::RUNTIME_ERROR;
      }

      const std::string &className = std::get<std::string>(nameConst);
      ClassObject *klass = allocateClass(className);
      frame->slots[A] = Value::object(klass);
      break;
    }

    case OpCode::OP_NEWOBJ: {

      Value classValue = frame->slots[B];

      if (!classValue.isClass()) {
        runtimeError("Cannot instantiate non-class type");
        return InterpretResult::RUNTIME_ERROR;
      }

      auto *klass = static_cast<ClassObject *>(classValue.asGC());
      Instance *instance = allocateInstance(klass);
      frame->slots[A] = Value::object(instance);

      break;
    }

    case OpCode::OP_GETUPVAL: {

      if (B >= frame->closure->upvalues.size()) {
        runtimeError("Invalid upvalue index: %d", B);
        return InterpretResult::RUNTIME_ERROR;
      }
      UpValue *upval = frame->closure->upvalues[B];
      frame->slots[A] = *upval->location;
      break;
    }

    case OpCode::OP_SETUPVAL: {

      if (B >= frame->closure->upvalues.size()) {
        runtimeError("Invalid upvalue index: %d", B);
        return InterpretResult::RUNTIME_ERROR;
      }
      UpValue *upval = frame->closure->upvalues[B];
      *upval->location = frame->slots[A];
      break;
    }

    case OpCode::OP_CLOSURE: {

      const Prototype &proto = frame->closure->proto->protos[Bx];
      Closure *closure = allocateClosure(&proto);

      for (size_t i = 0; i < proto.numUpvalues; ++i) {
        const auto &uvDesc = proto.upvalues[i];

        if (uvDesc.isLocal) {

          closure->upvalues.push_back(captureUpvalue(&frame->slots[uvDesc.index]));
        } else {

          closure->upvalues.push_back(frame->closure->upvalues[uvDesc.index]);
        }
      }

      frame->slots[A] = Value::object(closure);
      break;
    }

    case OpCode::OP_CLOSE_UPVALUE: {

      closeUpvalues(&frame->slots[A]);
      break;
    }

    case OpCode::OP_ADD: {

      Value b = frame->slots[B];
      Value c = frame->slots[C];

      if (b.isInt() && c.isInt()) {

        frame->slots[A] = Value::integer(b.asInt() + c.asInt());
      } else if (b.isNumber() && c.isNumber()) {

        double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
        frame->slots[A] = Value::number(left + right);
      }

      else if (b.isString() || c.isString()) {
        std::string s1 = b.toString();
        std::string s2 = c.toString();
        StringObject *result = allocateString(s1 + s2);
        frame->slots[A] = Value::object(result);
      } else {
        runtimeError("Operands must be numbers or strings");
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_SUB: {

      Value b = frame->slots[B];
      Value c = frame->slots[C];

      if (b.isInt() && c.isInt()) {

        frame->slots[A] = Value::integer(b.asInt() - c.asInt());
      } else if (b.isNumber() && c.isNumber()) {

        double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
        frame->slots[A] = Value::number(left - right);
      } else {
        runtimeError("Operands must be numbers");
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_MUL: {

      Value b = frame->slots[B];
      Value c = frame->slots[C];

      if (b.isInt() && c.isInt()) {

        frame->slots[A] = Value::integer(b.asInt() * c.asInt());
      } else if (b.isNumber() && c.isNumber()) {

        double left = b.isInt() ? static_cast<double>(b.asInt()) : b.asFloat();
        double right = c.isInt() ? static_cast<double>(c.asInt()) : c.asFloat();
        frame->slots[A] = Value::number(left * right);
      } else {
        runtimeError("Operands must be numbers");
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_DIV: {

      Value b = frame->slots[B];
      Value c = frame->slots[C];

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

      frame->slots[A] = Value::number(left / right);
      break;
    }

    case OpCode::OP_MOD: {

      Value b = frame->slots[B];
      Value c = frame->slots[C];

      if (!b.isInt() || !c.isInt()) {
        runtimeError("Modulo requires integer operands");
        return InterpretResult::RUNTIME_ERROR;
      }

      int64_t right = c.asInt();
      if (right == 0) {
        runtimeError("Modulo by zero");
        return InterpretResult::RUNTIME_ERROR;
      }

      frame->slots[A] = Value::integer(b.asInt() % right);
      break;
    }

    case OpCode::OP_UNM: {

      Value b = frame->slots[B];

      if (b.isInt()) {
        frame->slots[A] = Value::integer(-b.asInt());
      } else if (b.isFloat()) {
        frame->slots[A] = Value::number(-b.asFloat());
      } else {
        runtimeError("Operand must be a number");
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_JMP: {

      frame->ip += sBx;
      break;
    }

    case OpCode::OP_EQ: {

      bool equal = valuesEqual(frame->slots[A], frame->slots[B]);
      if (equal != (C != 0)) {
        frame->ip++;
      }
      break;
    }

    case OpCode::OP_LT: {

      Value a = frame->slots[A];
      Value b = frame->slots[B];

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
      break;
    }

    case OpCode::OP_LE: {

      Value a = frame->slots[A];
      Value b = frame->slots[B];

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
      break;
    }

    case OpCode::OP_TEST: {

      bool isTruthy = frame->slots[A].isTruthy();
      if (isTruthy != (C != 0)) {
        frame->ip++;
      }
      break;
    }

    case OpCode::OP_CALL: {

      int argCount = B - 1;
      int expectedResults = C - 1;

      Value callee = frame->slots[A];

      Value *argsStart = &frame->slots[A + 1];

      if (callee.isClosure()) {
        Closure *closure = static_cast<Closure *>(callee.asGC());
        const Prototype *proto = closure->proto;

        if (!proto->isVararg && argCount != proto->numParams) {
          runtimeError("Function '%s' expects %d arguments, got %d", proto->name.c_str(),
                       proto->numParams, argCount);
          return InterpretResult::RUNTIME_ERROR;
        }

        if (frameCount_ == 64) {
          runtimeError("Stack overflow");
          return InterpretResult::RUNTIME_ERROR;
        }

        CallFrame newFrame;
        newFrame.closure = closure;
        newFrame.ip = proto->code.data();
        newFrame.expectedResults = expectedResults;

        newFrame.slots = argsStart;

        Value *targetStackTop = newFrame.slots + proto->maxStackSize;
        if (targetStackTop > stack_.data() + config_.stackSize) {
          runtimeError("Stack overflow");
          return InterpretResult::RUNTIME_ERROR;
        }

        for (Value *p = newFrame.slots + argCount; p < targetStackTop; ++p) {
          *p = Value::nil();
        }

        stackTop_ = targetStackTop;
        frames_.push_back(newFrame);
        frameCount_++;

        frame = &frames_[frameCount_ - 1];

      } else if (callee.isNativeFunc()) {
        NativeFunction *native = static_cast<NativeFunction *>(callee.asGC());

        if (native->arity != -1 && argCount != native->arity) {
          runtimeError("Native function '%s' expects %d arguments, got %d", native->name.c_str(),
                       native->arity, argCount);
          return InterpretResult::RUNTIME_ERROR;
        }

        Value result = native->function(argCount, argsStart);

        frame->slots[A] = result;

      } else {

        runtimeError("Attempt to call a non-function value of type '%s'", callee.typeName());
        return InterpretResult::RUNTIME_ERROR;
      }
      break;
    }

    case OpCode::OP_RETURN: {

      Value result = (B >= 2) ? frame->slots[A] : Value::nil();

      Value *destSlot = frame->slots - 1;

      closeUpvalues(frame->slots);

      frameCount_--;
      frames_.pop_back();

      if (frameCount_ == minFrameCount) {

        lastModuleResult_ = result;

        if (minFrameCount == 0) {
          unprotect(1);
        }

        return InterpretResult::OK;
      }

      frame = &frames_[frameCount_ - 1];

      stackTop_ = frame->slots + frame->closure->proto->maxStackSize;

      *destSlot = result;

      break;
    }

    case OpCode::OP_IMPORT: {

      const auto &moduleNameConst = frame->closure->proto->constants[Bx];

      if (!std::holds_alternative<std::string>(moduleNameConst)) {
        runtimeError("Module name must be a string constant");
        return InterpretResult::RUNTIME_ERROR;
      }

      const std::string &moduleName = std::get<std::string>(moduleNameConst);

      std::string currentPath = frame->closure->proto->source;

      Value exportsTable = moduleManager_->loadModule(moduleName, currentPath);

      if (exportsTable.isMap()) {
        MapObject *errorCheck = static_cast<MapObject *>(exportsTable.asGC());

        StringObject *errorKey = allocateString("error");
        Value errorFlag = errorCheck->get(Value::object(errorKey));

        if (errorFlag.isBool() && errorFlag.asBool()) {
          StringObject *msgKey = allocateString("message");
          Value msgVal = errorCheck->get(Value::object(msgKey));

          std::string errorMsg = "Module load failed";
          if (msgVal.isString()) {
            errorMsg = static_cast<StringObject *>(msgVal.asGC())->data;
          }

          runtimeError("Import error: %s", errorMsg.c_str());
          return InterpretResult::RUNTIME_ERROR;
        }
      }

      frame->slots[A] = exportsTable;
      frame = &frames_[frameCount_ - 1];
      break;
    }

    case OpCode::OP_IMPORT_FROM: {

      const auto &moduleNameConst = frame->closure->proto->constants[B];
      const auto &symbolNameConst = frame->closure->proto->constants[C];

      if (!std::holds_alternative<std::string>(moduleNameConst)) {
        runtimeError("Module name must be a string constant");
        return InterpretResult::RUNTIME_ERROR;
      }

      if (!std::holds_alternative<std::string>(symbolNameConst)) {
        runtimeError("Symbol name must be a string constant");
        return InterpretResult::RUNTIME_ERROR;
      }

      const std::string &moduleName = std::get<std::string>(moduleNameConst);
      const std::string &symbolName = std::get<std::string>(symbolNameConst);

      std::string currentPath = frame->closure->proto->source;
      Value exportsTable = moduleManager_->loadModule(moduleName, currentPath);

      if (exportsTable.isMap()) {
        MapObject *exports = static_cast<MapObject *>(exportsTable.asGC());

        StringObject *errorKey = allocateString("error");
        Value errorFlag = exports->get(Value::object(errorKey));

        if (errorFlag.isBool() && errorFlag.asBool()) {
          StringObject *msgKey = allocateString("message");
          Value msgVal = exports->get(Value::object(msgKey));

          std::string errorMsg = "Module load failed";
          if (msgVal.isString()) {
            errorMsg = static_cast<StringObject *>(msgVal.asGC())->data;
          }

          runtimeError("Import error: %s", errorMsg.c_str());
          return InterpretResult::RUNTIME_ERROR;
        }

        StringObject *symbolKey = allocateString(symbolName);
        Value symbolValue = exports->get(Value::object(symbolKey));

        if (symbolValue.isNil()) {
          runtimeError("Module '%s' does not export '%s'", moduleName.c_str(), symbolName.c_str());
          return InterpretResult::RUNTIME_ERROR;
        }

        frame->slots[A] = symbolValue;
        frame = &frames_[frameCount_ - 1];
      } else {
        runtimeError("Import failed: expected module exports table");
        return InterpretResult::RUNTIME_ERROR;
      }

      break;
    }

    case OpCode::OP_EXPORT: {

      if (config_.debugMode) {
        Value exportedValue = frame->slots[A];
        printf("[EXPORT] Exported value: %s\n", exportedValue.toString().c_str());
      }

      break;
    }

    default:
      runtimeError("Unknown opcode: %d", static_cast<int>(opcode));
      return InterpretResult::RUNTIME_ERROR;
    }
  }

#undef READ_INSTRUCTION
#undef READ_BYTE
#undef DISPATCH
}

UpValue *VM::captureUpvalue(Value *local) {

  UpValue *prevUpvalue = nullptr;
  UpValue *upvalue = openUpvalues_;

  while (upvalue != nullptr && upvalue->location > local) {
    prevUpvalue = upvalue;
    upvalue = upvalue->nextOpen;
  }

  if (upvalue != nullptr && upvalue->location == local) {
    return upvalue;
  }

  UpValue *newUpvalue = gc_.allocate<UpValue>();
  newUpvalue->location = local;
  newUpvalue->closed = Value::nil();
  newUpvalue->nextOpen = upvalue;

  if (prevUpvalue == nullptr) {
    openUpvalues_ = newUpvalue;
  } else {
    prevUpvalue->nextOpen = newUpvalue;
  }

  return newUpvalue;
}

void VM::closeUpvalues(Value *last) {

  while (openUpvalues_ != nullptr && openUpvalues_->location >= last) {
    UpValue *upvalue = openUpvalues_;
    upvalue->closed = *upvalue->location;
    upvalue->location = &upvalue->closed;
    openUpvalues_ = upvalue->nextOpen;
  }
}

StringObject *VM::allocateString(const std::string &str) {

  auto it = strings_.find(str);
  if (it != strings_.end()) {
    return it->second;
  }

  StringObject *obj = gc_.allocate<StringObject>();
  obj->data = str;
  obj->hash = std::hash<std::string>{}(str);

  strings_[str] = obj;
  return obj;
}

Closure *VM::allocateClosure(const Prototype *proto) {
  Closure *closure = gc_.allocate<Closure>();
  closure->proto = proto;
  closure->upvalues.reserve(proto->numUpvalues);
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

void VM::defineGlobal(const std::string &name, Value value) { globals_[name] = value; }

Value VM::getGlobal(const std::string &name) {
  auto it = globals_.find(name);
  if (it != globals_.end()) {
    return it->second;
  }
  return Value::nil();
}

void VM::setGlobal(const std::string &name, Value value) { globals_[name] = value; }

void VM::registerNative(const std::string &name, NativeFn fn, int arity, uint8_t flags) {
  NativeFunction *native = gc_.allocate<NativeFunction>();
  native->name = name;
  native->function = fn;
  native->arity = arity;
  native->flags = flags;
  defineGlobal(name, Value::object(native));
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

  MapObject *exports = allocateMap(8);
  for (const auto &exportName : it->second.exports) {
    Value value = getGlobal(exportName);
    StringObject *key = allocateString(exportName);
    exports->set(Value::object(key), value);
  }

  return Value::object(exports);
}

bool VM::valuesEqual(Value a, Value b) { return a.equals(b); }

void VM::resetStack() {
  stackTop_ = stack_.data();
  frameCount_ = 0;
  openUpvalues_ = nullptr;
}

void VM::collectGarbage() { gc_.collect(); }

void VM::runtimeError(const char *format, ...) {
  char buffer[512];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);

  std::string message = buffer;

  message += "\n----------------\nCall stack:";

  for (int i = frameCount_ - 1; i >= 0; --i) {
    CallFrame &frame = frames_[i];
    const Prototype *proto = frame.closure->proto;
    size_t instruction = frame.ip - proto->code.data() - 1;

    int line = 0;
    if (instruction < proto->lineInfo.size()) {
      line = proto->lineInfo[instruction];
    }

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

void VM::dumpStack() const {
  printf("\n=== Stack Dump ===\n");
  printf("Stack range: [%p, %p)\n", static_cast<const void *>(stack_.data()),
         static_cast<const void *>(stackTop_));

  for (const Value *slot = stack_.data(); slot < stackTop_; ++slot) {
    size_t offset = slot - stack_.data();
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

} // namespace spt