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

static int fiberCreate(VM *vm, Closure *self, int argc, Value *argv) {
  if (argc < 1 || !argv[0].isClosure()) {
    vm->throwError(Value::object(vm->allocateString("Fiber.create requires a function")));
    return 0;
  }

  Closure *closure = static_cast<Closure *>(argv[0].asGC());
  FiberObject *fiber = vm->allocateFiber(closure);
  vm->push(Value::object(fiber));
  return 1;
}

static int fiberYield(VM *vm, Closure *self, int argc, Value *argv) {
  Value value = (argc > 0) ? argv[0] : Value::nil();
  vm->fiberYield(value);
  return 0;
}

static int fiberCurrent(VM *vm, Closure *self, int argc, Value *argv) {
  vm->push(Value::object(vm->currentFiber()));
  return 1;
}

static int fiberAbort(VM *vm, Closure *self, int argc, Value *argv) {
  Value error = (argc > 0) ? argv[0] : Value::object(vm->allocateString("Fiber aborted"));
  vm->fiberAbort(error);
  return 0;
}

static int fiberSuspend(VM *vm, Closure *self, int argc, Value *argv) {
  vm->fiberYield(Value::nil());
  return 0;
}

void SptFiber::load(VM *vm) {
  ClassObject *fiberClass = vm->allocateClass("Fiber");
  vm->protect(Value::object(fiberClass));

  auto addStatic = [&](const char *name, NativeFn fn, int arity) {
    Closure *native = vm->gc().allocateNativeClosure(0);
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