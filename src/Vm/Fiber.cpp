#include "Fiber.h"
#include "Object.h"
#include "VM.h"

namespace spt {

void FiberObject::init(FiberObject *fiber) {

  fiber->stackSize = DEFAULT_STACK_SIZE;
  fiber->stack = new Value[fiber->stackSize];
  fiber->stackTop = fiber->stack;
  fiber->stackLast = fiber->stack + fiber->stackSize;

  for (size_t i = 0; i < fiber->stackSize; ++i) {
    fiber->stack[i] = Value::nil();
  }

  fiber->framesCapacity = DEFAULT_FRAMES_SIZE;
  fiber->frames = new CallFrame[fiber->framesCapacity];
  fiber->frameCount = 0;

  for (int i = 0; i < fiber->framesCapacity; ++i) {
    fiber->frames[i] = CallFrame{};
  }

  fiber->deferCapacity = DEFAULT_DEFER_SIZE;
  fiber->deferStack = new Value[fiber->deferCapacity];
  fiber->deferTop = 0;

  for (int i = 0; i < fiber->deferCapacity; ++i) {
    fiber->deferStack[i] = Value::nil();
  }

  fiber->state = FiberState::NEW;
  fiber->openUpvalues = nullptr;
  fiber->closure = nullptr;
  fiber->caller = nullptr;
  fiber->error = Value::nil();
  fiber->hasError = false;
  fiber->yieldValue = Value::nil();
}

void FiberObject::destroy(FiberObject *fiber) {

  if (fiber->stack) {
    delete[] fiber->stack;
    fiber->stack = nullptr;
    fiber->stackTop = nullptr;
    fiber->stackLast = nullptr;
    fiber->stackSize = 0;
  }

  if (fiber->frames) {
    delete[] fiber->frames;
    fiber->frames = nullptr;
    fiber->framesCapacity = 0;
    fiber->frameCount = 0;
  }

  if (fiber->deferStack) {
    delete[] fiber->deferStack;
    fiber->deferStack = nullptr;
    fiber->deferCapacity = 0;
    fiber->deferTop = 0;
  }
}

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

static void addStaticMethod(VM *vm, ClassObject *fiberClass, const char *name, NativeFn fn,
                            int arity) {
  Closure *native = vm->gc().allocateNativeClosure(0);
  vm->protect(Value::object(native));

  native->name = vm->allocateString(name);
  native->function = fn;
  native->arity = arity;
  native->receiver = Value::nil();

  vm->unprotect(1);

  fiberClass->statics[native->name] = Value::object(native);
}

void SptFiber::load(VM *vm) {
  ClassObject *fiberClass = vm->allocateClass("Fiber");
  vm->protect(Value::object(fiberClass));

  addStaticMethod(vm, fiberClass, "create", fiberCreate, 1);
  addStaticMethod(vm, fiberClass, "yield", fiberYield, -1);
  addStaticMethod(vm, fiberClass, "current", fiberCurrent, 0);
  addStaticMethod(vm, fiberClass, "abort", fiberAbort, -1);
  addStaticMethod(vm, fiberClass, "suspend", fiberSuspend, 0);

  vm->defineGlobal("Fiber", Value::object(fiberClass));
  vm->unprotect(1);
}

} // namespace spt