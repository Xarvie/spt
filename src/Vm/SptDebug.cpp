#include "SptDebug.hpp"

#include "VM.h"
#include "Value.h"

namespace spt {
static Value buildDebugInfo(VM *vm, StringObject *str, DebugInfo info) {
  auto map_object = vm->allocateMap(5);
  for (char c : str->view()) {
    switch (c) {
    case 'n': {
      map_object->set(Value::object(vm->allocateString("name")),
                      Value::object(vm->allocateString(info.name)));
    } break;
    case 'S': {
      map_object->set(Value::object(vm->allocateString("source")),
                      Value::object(vm->allocateString(info.source)));
      map_object->set(Value::object(vm->allocateString("shortSrc")),
                      Value::object(vm->allocateString(info.shortSrc)));
      map_object->set(Value::object(vm->allocateString("lineDefined")),
                      Value::integer(info.lineDefined));
      map_object->set(Value::object(vm->allocateString("lastLineDefined")),
                      Value::integer(info.lastLineDefined));
    } break;
    case 'l': {
      map_object->set(Value::object(vm->allocateString("currentLine")),
                      Value::integer(info.currentLine));
    } break;
    }
  }
  return Value::object(map_object);
}

static Value debugGetInfo(VM *vm, NativeFunction *self, int argc, Value *argv) {

  if (argc < 1 || !(argv[0].isClosure() || argv[0].isNativeFunc())) {
    vm->throwError(Value::object(vm->allocateString("debug.getInfo: arg 1 must be a function")));
    return Value::nil();
  }

  if (argc < 2) {
    vm->throwError(
        Value::object(vm->allocateString("debug.getInfo: arg 2 (what string) is required")));
    return Value::nil();
  }

  if (!argv[1].isString()) {
    vm->throwError(Value::object(vm->allocateString("debug.getInfo: arg 2 must be a string")));
    return Value::nil();
  }

  StringObject *str = static_cast<StringObject *>(argv[1].asGC());
  DebugInfo info;
  if (!vm->getInfo(&argv[0], str->c_str(), &info)) {
    vm->throwError(Value::object(vm->allocateString("debug.getInfo: vm error")));
    return Value::nil();
  }
  return buildDebugInfo(vm, str, info);
}

static Value debugGetStack(VM *vm, NativeFunction *self, int argc, Value *argv) {

  if (argc < 1 || !argv[0].isNumber()) {
    vm->throwError(Value::object(vm->allocateString("debug.getStack: arg 1 must be a number")));
    return Value::nil();
  }

  if (argc < 2) {
    vm->throwError(
        Value::object(vm->allocateString("debug.getStack: arg 2 (what string) is required")));
    return Value::nil();
  }

  if (!argv[1].isString()) {
    vm->throwError(Value::object(vm->allocateString("debug.getStack: arg 2 must be a string")));
    return Value::nil();
  }

  int f = static_cast<int>(argv[0].asNumber());
  StringObject *str = static_cast<StringObject *>(argv[1].asGC());
  DebugInfo info;
  if (!vm->getStack(f, str->c_str(), &info)) {
    vm->throwError(Value::object(vm->allocateString("debug.getStack: vm error")));
    return Value::nil();
  }
  return buildDebugInfo(vm, str, info);
}

static const MethodEntry debugMethods[] = {
    {"getInfo", debugGetInfo, 2}, {"getStack", debugGetStack, 2}, {nullptr, nullptr, 0}};

void SptDebug::load(VM *vm) {
  auto module = vm->moduleManager()->loadCModule("debug", "std:debug", debugMethods);
  vm->defineGlobal("debug", module);
}
} // namespace spt
