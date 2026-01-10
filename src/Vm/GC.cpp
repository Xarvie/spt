#include "GC.h"
#include "Fiber.h"
#include "Object.h"
#include "VM.h"

namespace spt {

GC::GC(VM *vm, const GCConfig &config)
    : vm_(vm), config_(config), threshold_(config.initialThreshold) {}

GC::~GC() {

  GCObject *obj = objects_;
  while (obj) {
    GCObject *next = obj->next;
    freeObject(obj);
    obj = next;
  }
}

void *GC::allocateRaw(size_t size) {
  collectIfNeeded();
  bytesAllocated_ += size;
  return ::operator new(size);
}

void GC::deallocate(GCObject *obj) { freeObject(obj); }

void GC::collect() {
  if (!enabled_ || inFinalizer_)
    return;

  size_t before = bytesAllocated_;

  markRoots();

  traceReferences();

  removeWhiteStrings();

  sweep();

  runFinalizers();

  threshold_ = static_cast<size_t>(bytesAllocated_ * config_.growthFactor);
  if (threshold_ < config_.initialThreshold) {
    threshold_ = config_.initialThreshold;
  }
}

void GC::collectIfNeeded() {
  if (config_.enableStressTest || bytesAllocated_ > threshold_) {
    collect();
  }
}

void GC::writeBarrier(GCObject *, GCObject *) {}

void GC::addRoot(RootVisitor visitor) { roots_.push_back(std::move(visitor)); }

void GC::removeRoot(RootVisitor) {}

void GC::markRoots() {

  if (vm_->mainFiber()) {
    markObject(vm_->mainFiber());
  }

  if (vm_->currentFiber() && vm_->currentFiber() != vm_->mainFiber()) {
    markObject(vm_->currentFiber());
  }

  for (auto &[name, val] : vm_->globals_) {
    markValue(val);
  }

  for (auto &visitor : roots_) {
  }

  if (vm_->hasError_) {
    markValue(vm_->errorValue_);
  }

  if (vm_->hasNativeMultiReturn_) {
    for (auto &val : vm_->nativeMultiReturn_) {
      markValue(val);
    }
  }

  if (vm_->moduleManager()) {
    vm_->moduleManager()->markRoots();
  }

  for (Instance *instance : finalizerQueue_) {
    markObject(instance);
  }
}

void GC::markObject(GCObject *obj) {
  if (!obj || obj->marked)
    return;

  obj->marked = true;
  grayStack_.push_back(obj);
}

void GC::markValue(Value &value) {
  if (value.isNil() || value.isBool() || value.isInt() || value.isNumber()) {
    return;
  }
  markObject(value.asGC());
}

void GC::traceReferences() {
  while (!grayStack_.empty()) {
    GCObject *obj = grayStack_.back();
    grayStack_.pop_back();

    switch (obj->type) {
    case ValueType::Closure: {
      auto *closure = static_cast<Closure *>(obj);
      for (auto *uv : closure->upvalues) {
        markObject(uv);
      }
      break;
    }
    case ValueType::List: {
      auto *list = static_cast<ListObject *>(obj);
      for (auto &elem : list->elements) {
        markValue(elem);
      }
      break;
    }
    case ValueType::Map: {
      auto *map = static_cast<MapObject *>(obj);
      for (auto &[k, v] : map->entries) {

        Value key = k;
        markValue(key);
        Value val = v;
        markValue(val);
      }
      break;
    }
    case ValueType::Object: {
      auto *instance = static_cast<Instance *>(obj);
      markObject(instance->klass);
      for (auto &[name, value] : instance->fields) {
        Value v = value;
        markValue(v);
      }
      break;
    }
    case ValueType::Class: {
      auto *klass = static_cast<ClassObject *>(obj);
      for (auto &[name, method] : klass->methods) {
        Value v = method;
        markValue(v);
      }
      for (auto &[name, val] : klass->statics) {
        Value v = val;
        markValue(v);
      }

      if (!klass->gcMethod.isNil()) {
        markValue(klass->gcMethod);
      }
      break;
    }
    case ValueType::Upvalue: {
      auto *uv = static_cast<UpValue *>(obj);

      markValue(*uv->location);
      break;
    }

    case ValueType::NativeFunc: {
      auto *native = static_cast<NativeFunction *>(obj);
      markValue(native->receiver);
      break;
    }

    case ValueType::Fiber: {
      auto *fiber = static_cast<FiberObject *>(obj);

      if (fiber->closure) {
        markObject(fiber->closure);
      }

      for (Value *slot = fiber->stack.data(); slot < fiber->stackTop; ++slot) {
        markValue(*slot);
      }

      for (int i = 0; i < fiber->frameCount; ++i) {
        CallFrame &frame = fiber->frames[i];
        if (frame.closure) {
          markObject(frame.closure);
        }
      }

      UpValue *upvalue = fiber->openUpvalues;
      while (upvalue != nullptr) {
        markObject(upvalue);
        upvalue = upvalue->nextOpen;
      }

      if (fiber->caller) {
        markObject(fiber->caller);
      }

      if (fiber->hasError) {
        markValue(fiber->error);
      }

      break;
    }

    default:
      break;
    }
  }
}

void GC::sweep() {
  GCObject **pointer = &objects_;

  while (*pointer) {
    if ((*pointer)->marked) {

      (*pointer)->marked = false;
      pointer = &((*pointer)->next);
    } else {
      GCObject *unreached = *pointer;

      if (unreached->type == ValueType::Object) {
        Instance *instance = static_cast<Instance *>(unreached);

        if (instance->klass && instance->klass->hasFinalizer() && !instance->isFinalized) {

          instance->marked = true;
          finalizerQueue_.push_back(instance);
          pointer = &((*pointer)->next);
          continue;
        }
      }

      *pointer = unreached->next;
      freeObject(unreached);
    }
  }
}

void GC::removeWhiteStrings() {
  for (auto it = vm_->strings_.begin(); it != vm_->strings_.end();) {

    if (!it->second->marked) {
      it = vm_->strings_.erase(it);
    } else {
      ++it;
    }
  }
}

void GC::freeObject(GCObject *obj) {
  if (vm_->config_.debugMode) {
  }

  switch (obj->type) {
  case ValueType::String: {
    StringObject *str = static_cast<StringObject *>(obj);
    bytesAllocated_ -= sizeof(StringObject) + str->data.capacity();
    delete str;
    break;
  }
  case ValueType::List: {
    ListObject *list = static_cast<ListObject *>(obj);
    bytesAllocated_ -= sizeof(ListObject) + (list->elements.capacity() * sizeof(Value));
    delete list;
    break;
  }
  case ValueType::Map: {
    MapObject *map = static_cast<MapObject *>(obj);
    bytesAllocated_ -=
        sizeof(MapObject) + (map->entries.capacity() * sizeof(std::pair<Value, Value>));
    delete map;
    break;
  }
  case ValueType::Closure:
    bytesAllocated_ -= sizeof(Closure);
    delete static_cast<Closure *>(obj);
    break;
  case ValueType::Class:
    bytesAllocated_ -= sizeof(ClassObject);
    delete static_cast<ClassObject *>(obj);
    break;
  case ValueType::Object:
    bytesAllocated_ -= sizeof(Instance);
    delete static_cast<Instance *>(obj);
    break;
  case ValueType::NativeFunc:
    bytesAllocated_ -= sizeof(NativeFunction);
    delete static_cast<NativeFunction *>(obj);
    break;
  case ValueType::Upvalue:
    bytesAllocated_ -= sizeof(UpValue);
    delete static_cast<UpValue *>(obj);
    break;

  case ValueType::Fiber: {
    FiberObject *fiber = static_cast<FiberObject *>(obj);
    bytesAllocated_ -= sizeof(FiberObject) + (fiber->stack.capacity() * sizeof(Value)) +
                       (fiber->frames.size() * sizeof(CallFrame));
    delete fiber;
    break;
  }

  default:
    delete obj;
    break;
  }
  objectCount_--;
}

void GC::runFinalizers() {
  if (finalizerQueue_.empty())
    return;

  inFinalizer_ = true;

  std::vector<Instance *> toFinalize = std::move(finalizerQueue_);
  finalizerQueue_.clear();

  for (Instance *instance : toFinalize) {
    if (!instance->isFinalized) {
      invokeGCMethod(instance);
      instance->isFinalized = true;
    }

    instance->marked = false;
  }

  inFinalizer_ = false;
}

void GC::invokeGCMethod(Instance *instance) {
  if (!instance || !instance->klass)
    return;

  Value gcMethod = instance->klass->gcMethod;

  if (!gcMethod.isClosure())
    return;

  Closure *closure = static_cast<Closure *>(gcMethod.asGC());

  FiberObject *fiber = vm_->currentFiber();
  if (!fiber)
    return;

  Value *savedStackTop = fiber->stackTop;
  int savedFrameCount = fiber->frameCount;

  fiber->push(Value::object(instance));

  InterpretResult result = vm_->call(closure, 0);

  if (result != InterpretResult::OK) {
    fiber->stackTop = savedStackTop;
    fiber->frameCount = savedFrameCount;

    vm_->hasError_ = false;
    vm_->errorValue_ = Value::nil();
  }
}

} // namespace spt
