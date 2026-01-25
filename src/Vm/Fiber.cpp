#include "Fiber.h"
#include "Object.h"
#include "VM.h"

namespace spt {

void FiberObject::fixUpvaluePointers(Value *oldBase, Value *newBase) {
  UpValue *uv = openUpvalues;
  while (uv != nullptr) {
    uv->location = newBase + (uv->location - oldBase);
    uv = uv->nextOpen;
  }
}

static Value fiberCreate(VM *vm, NativeFunction *self, int argc, Value *argv) {
  if (argc < 1 || !argv[0].isClosure()) {
    vm->throwError(Value::object(vm->allocateString("Fiber.create requires a function")));
    return Value::nil();
  }

  Closure *closure = static_cast<Closure *>(argv[0].asGC());
  FiberObject *fiber = vm->allocateFiber(closure);
  return Value::object(fiber);
}

static Value fiberYield(VM *vm, NativeFunction *self, int argc, Value *argv) {
  Value value = (argc > 0) ? argv[0] : Value::nil();
  vm->fiberYield(value);
  return Value::nil();
}

static Value fiberCurrent(VM *vm, NativeFunction *self, int argc, Value *argv) {
  return Value::object(vm->currentFiber());
}

static Value fiberAbort(VM *vm, NativeFunction *self, int argc, Value *argv) {
  Value error = (argc > 0) ? argv[0] : Value::object(vm->allocateString("Fiber aborted"));
  vm->fiberAbort(error);
  return Value::nil();
}

static Value fiberSuspend(VM *vm, NativeFunction *self, int argc, Value *argv) {
  vm->fiberYield(Value::nil());
  return Value::nil();
}

void SptFiber::load(VM *vm) {
  ClassObject *fiberClass = vm->allocateClass("Fiber");
  vm->protect(Value::object(fiberClass));

  auto addStatic = [&](const char *name, NativeFn fn, int arity) {
    NativeFunction *native = vm->gc().allocateNativeFunction(0);
    vm->protect(Value::object(native));

    native->name = vm->allocateString(name);
    native->function = fn;
    native->arity = arity;
    native->receiver = Value::nil();

    vm->unprotect(1);

    fiberClass->statics[native->name] = Value::object(native);
  };

  addStatic("create", fiberCreate, 1);
  addStatic("yield", fiberYield, -1);
  addStatic("current", fiberCurrent, 0);
  addStatic("abort", fiberAbort, -1);
  addStatic("suspend", fiberSuspend, 0);

  vm->defineGlobal("Fiber", Value::object(fiberClass));
  vm->unprotect(1);
}

} // namespace spt
