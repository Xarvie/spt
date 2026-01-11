#include "GC.h"
#include "Fiber.h"
#include "NativeBinding.h"
#include "Object.h"
#include "VM.h"
#include <algorithm>
#include <chrono>
#include <cstdio>

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

  auto startTime = std::chrono::high_resolution_clock::now();
  size_t beforeBytes = bytesAllocated_;
  size_t beforeObjects = objectCount_;

  if (GCDebug::enabled && GCDebug::logCollections) {
    fprintf(stderr, "[GC] === START #%zu === bytes=%zu objects=%zu threshold=%zu\n",
            stats_.totalCollections + 1, bytesAllocated_, objectCount_, threshold_);
  }

  markRoots();
  traceReferences();
  removeWhiteStrings();
  sweep();
  runFinalizers();

  size_t bytesFreed = beforeBytes - bytesAllocated_;
  size_t objectsFreed = beforeObjects - objectCount_;

  stats_.totalCollections++;
  stats_.totalBytesFreed += bytesFreed;
  stats_.totalObjectsFreed += objectsFreed;
  stats_.lastBytesFreed = bytesFreed;
  stats_.lastObjectsFreed = objectsFreed;

  auto endTime = std::chrono::high_resolution_clock::now();
  stats_.lastDurationUs =
      std::chrono::duration_cast<std::chrono::microseconds>(endTime - startTime).count();

  if (GCDebug::enabled && GCDebug::logCollections) {
    fprintf(stderr,
            "[GC] === END === freed %zu bytes, %zu objects in %zu us. now: bytes=%zu objects=%zu\n",
            bytesFreed, objectsFreed, stats_.lastDurationUs, bytesAllocated_, objectCount_);
  }

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

void GC::removeRoot(RootVisitor visitor) {
  auto it = std::find_if(roots_.begin(), roots_.end(), [&visitor](const RootVisitor &v) {
    return visitor.target_type() == v.target_type() &&
           visitor.target<void (*)(Value &)>() == v.target<void (*)(Value &)>();
  });
  if (it != roots_.end()) {
    roots_.erase(it);
  }
}

void GC::markRoots() {
  if (GCDebug::enabled && GCDebug::verboseMarking) {
    fprintf(stderr, "[GC] marking roots...\n");
  }

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
    Value tempValue = Value::nil();
    visitor(tempValue);
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
  for (NativeInstance *instance : nativeFinalizerQueue_) {
    markObject(instance);
  }
}

void GC::markObject(GCObject *obj) {
  if (!obj || obj->marked)
    return;

  obj->marked = true;
  grayStack_.push_back(obj);

  if (GCDebug::enabled && GCDebug::verboseMarking) {
    fprintf(stderr, "[GC] mark %s @%p\n", typeName(obj->type), (void *)obj);
  }
}

void GC::markValue(Value &value) {
  if (value.isNil() || value.isBool() || value.isInt() || value.isNumber()) {
    return;
  }
  markObject(value.asGC());
}

void GC::traceReferences() {
  if (GCDebug::enabled && GCDebug::verboseMarking) {
    fprintf(stderr, "[GC] tracing %zu gray objects...\n", grayStack_.size());
  }

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
      for (int i = 0; i < fiber->deferTop; ++i) {
        markValue(fiber->deferStack[i]);
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
    case ValueType::NativeClass: {
      auto *nativeClass = static_cast<NativeClassObject *>(obj);
      if (nativeClass->baseClass) {
        markObject(nativeClass->baseClass);
      }
      for (auto &[name, val] : nativeClass->statics) {
        Value v = val;
        markValue(v);
      }
      break;
    }
    case ValueType::NativeObject: {
      auto *nativeInstance = static_cast<NativeInstance *>(obj);
      if (nativeInstance->nativeClass) {
        markObject(nativeInstance->nativeClass);
      }
      for (auto &[name, value] : nativeInstance->fields) {
        Value v = value;
        markValue(v);
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
  size_t sweptCount = 0;

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

      if (unreached->type == ValueType::NativeObject) {
        NativeInstance *nativeInstance = static_cast<NativeInstance *>(unreached);
        if (nativeInstance->ownership == OwnershipMode::OwnedByVM && nativeInstance->nativeClass &&
            nativeInstance->nativeClass->hasDestructor() && !nativeInstance->isDestroyed &&
            nativeInstance->data) {
          nativeInstance->marked = true;
          nativeFinalizerQueue_.push_back(nativeInstance);
          pointer = &((*pointer)->next);
          continue;
        }
      }

      *pointer = unreached->next;

      if (GCDebug::enabled && GCDebug::logFrees) {
        fprintf(stderr, "[GC] free %s @%p\n", typeName(unreached->type), (void *)unreached);
      }

      updateTypeStats(unreached->type, false);
      freeObject(unreached);
      sweptCount++;
    }
  }

  if (GCDebug::enabled && GCDebug::verboseMarking) {
    fprintf(stderr, "[GC] swept %zu objects\n", sweptCount);
  }
}

void GC::removeWhiteStrings() {
  size_t removed = 0;
  for (auto it = vm_->strings_.begin(); it != vm_->strings_.end();) {
    if (!it->second->marked) {
      it = vm_->strings_.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  if (GCDebug::enabled && GCDebug::verboseMarking && removed > 0) {
    fprintf(stderr, "[GC] removed %zu interned strings\n", removed);
  }
}

void GC::freeObject(GCObject *obj) {
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
    bytesAllocated_ -= sizeof(MapObject) + (map->entries.size() * sizeof(std::pair<Value, Value>));
    delete map;
    break;
  }
  case ValueType::Closure: {
    Closure *closure = static_cast<Closure *>(obj);
    bytesAllocated_ -= sizeof(Closure) + (closure->upvalues.capacity() * sizeof(UpValue *));
    delete closure;
    break;
  }
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
                       (fiber->frames.capacity() * sizeof(CallFrame)) +
                       (fiber->deferStack.capacity() * sizeof(Value));
    delete fiber;
    break;
  }
  case ValueType::NativeClass: {
    NativeClassObject *nativeClass = static_cast<NativeClassObject *>(obj);
    bytesAllocated_ -= sizeof(NativeClassObject);
    delete nativeClass;
    break;
  }
  case ValueType::NativeObject: {
    NativeInstance *nativeInstance = static_cast<NativeInstance *>(obj);
    if (nativeInstance->ownership == OwnershipMode::OwnedByVM && !nativeInstance->isDestroyed &&
        nativeInstance->data && nativeInstance->nativeClass &&
        nativeInstance->nativeClass->destructor) {
      nativeInstance->nativeClass->destructor(nativeInstance->data);
    }
    bytesAllocated_ -=
        sizeof(NativeInstance) +
        (nativeInstance->nativeClass ? nativeInstance->nativeClass->instanceDataSize : 0);
    delete nativeInstance;
    break;
  }
  default:
    delete obj;
    break;
  }
  objectCount_--;
}

void GC::runFinalizers() {
  if (finalizerQueue_.empty() && nativeFinalizerQueue_.empty())
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

  std::vector<NativeInstance *> nativeToFinalize = std::move(nativeFinalizerQueue_);
  nativeFinalizerQueue_.clear();

  for (NativeInstance *instance : nativeToFinalize) {
    if (!instance->isDestroyed) {
      invokeNativeDestructor(instance);
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
  InterpretResult result = vm_->call(closure, 1);

  if (result != InterpretResult::OK) {
    fiber->stackTop = savedStackTop;
    fiber->frameCount = savedFrameCount;
    vm_->hasError_ = false;
    vm_->errorValue_ = Value::nil();
  }
}

void GC::invokeNativeDestructor(NativeInstance *instance) {
  if (!instance || !instance->nativeClass || !instance->data || instance->isDestroyed)
    return;
  if (instance->ownership != OwnershipMode::OwnedByVM)
    return;

  if (instance->nativeClass->destructor) {
    instance->nativeClass->destructor(instance->data);
  }
  instance->isDestroyed = true;
  instance->data = nullptr;
}

const char *GC::typeName(ValueType type) {
  switch (type) {
  case ValueType::Nil:
    return "Nil";
  case ValueType::Bool:
    return "Bool";
  case ValueType::Int:
    return "Int";
  case ValueType::Float:
    return "Float";
  case ValueType::String:
    return "String";
  case ValueType::List:
    return "List";
  case ValueType::Map:
    return "Map";
  case ValueType::Closure:
    return "Closure";
  case ValueType::Class:
    return "Class";
  case ValueType::Object:
    return "Instance";
  case ValueType::NativeFunc:
    return "NativeFunc";
  case ValueType::Upvalue:
    return "Upvalue";
  case ValueType::Fiber:
    return "Fiber";
  case ValueType::NativeClass:
    return "NativeClass";
  case ValueType::NativeObject:
    return "NativeObject";
  default:
    return "Unknown";
  }
}

void GC::updateTypeStats(ValueType type, bool isAlloc) {
  int idx = isAlloc ? 0 : 1;
  switch (type) {
  case ValueType::String:
    stats_.strings[idx]++;
    break;
  case ValueType::List:
    stats_.lists[idx]++;
    break;
  case ValueType::Map:
    stats_.maps[idx]++;
    break;
  case ValueType::Closure:
    stats_.closures[idx]++;
    break;
  case ValueType::Object:
    stats_.instances[idx]++;
    break;
  case ValueType::Fiber:
    stats_.fibers[idx]++;
    break;
  case ValueType::Upvalue:
    stats_.upvalues[idx]++;
    break;
  case ValueType::Class:
    stats_.classes[idx]++;
    break;
  case ValueType::NativeFunc:
    stats_.nativeFuncs[idx]++;
    break;
  case ValueType::NativeClass:
    stats_.nativeClasses[idx]++;
    break;
  case ValueType::NativeObject:
    stats_.nativeObjects[idx]++;
    break;
  default:
    break;
  }
}

void GC::resetStats() { stats_ = GCStats{}; }

void GC::dumpStats() const {
  fprintf(stderr, "\n========== GC Stats ==========\n");
  fprintf(stderr, "Collections: %zu (last: %zu us)\n", stats_.totalCollections,
          stats_.lastDurationUs);
  fprintf(stderr, "Freed: %zu bytes, %zu objects total\n", stats_.totalBytesFreed,
          stats_.totalObjectsFreed);
  fprintf(stderr, "Peak: %zu bytes, %zu objects\n", stats_.peakBytesAllocated,
          stats_.peakObjectCount);
  fprintf(stderr, "Now:  %zu bytes, %zu objects\n", bytesAllocated_, objectCount_);
  fprintf(stderr, "\n--- By Type (alloc/freed/live) ---\n");

#define DUMP_TYPE(name, arr)                                                                       \
  fprintf(stderr, "%-12s %6zu / %6zu / %6zu\n", name, arr[0], arr[1], arr[0] - arr[1])

  DUMP_TYPE("String", stats_.strings);
  DUMP_TYPE("List", stats_.lists);
  DUMP_TYPE("Map", stats_.maps);
  DUMP_TYPE("Closure", stats_.closures);
  DUMP_TYPE("Instance", stats_.instances);
  DUMP_TYPE("Fiber", stats_.fibers);
  DUMP_TYPE("Upvalue", stats_.upvalues);
  DUMP_TYPE("Class", stats_.classes);
  DUMP_TYPE("NativeFunc", stats_.nativeFuncs);
  DUMP_TYPE("NativeClass", stats_.nativeClasses);
  DUMP_TYPE("NativeObj", stats_.nativeObjects);

#undef DUMP_TYPE
  fprintf(stderr, "==============================\n\n");
}

void GC::dumpObjects() const {
  fprintf(stderr, "\n========== Live Objects ==========\n");
  size_t count = 0;
  GCObject *obj = objects_;
  while (obj) {
    fprintf(stderr, "[%4zu] %-12s @%p", count, typeName(obj->type), (void *)obj);

    switch (obj->type) {
    case ValueType::String: {
      auto *str = static_cast<StringObject *>(obj);
      if (str->data.length() <= 40) {
        fprintf(stderr, " \"%s\"", str->data.c_str());
      } else {
        fprintf(stderr, " \"%.40s...\"", str->data.c_str());
      }
      break;
    }
    case ValueType::List: {
      auto *list = static_cast<ListObject *>(obj);
      fprintf(stderr, " [%zu]", list->elements.size());
      break;
    }
    case ValueType::Map: {
      auto *map = static_cast<MapObject *>(obj);
      fprintf(stderr, " {%zu}", map->entries.size());
      break;
    }
    case ValueType::Class: {
      auto *klass = static_cast<ClassObject *>(obj);
      fprintf(stderr, " '%s'", klass->name.c_str());
      break;
    }
    default:
      break;
    }

    fprintf(stderr, "\n");
    obj = obj->next;
    count++;
  }
  fprintf(stderr, "Total: %zu objects\n", count);
  fprintf(stderr, "==================================\n\n");
}

void GC::markCheckpoint() {
  checkpointObjects_ = objectCount_;
  checkpointBytes_ = bytesAllocated_;
  if (GCDebug::enabled) {
    fprintf(stderr, "[GC] checkpoint: %zu objects, %zu bytes\n", checkpointObjects_,
            checkpointBytes_);
  }
}

void GC::checkLeaks() const {
  fprintf(stderr, "\n========== Leak Check ==========\n");
  fprintf(stderr, "Checkpoint: %zu objects, %zu bytes\n", checkpointObjects_, checkpointBytes_);
  fprintf(stderr, "Current:    %zu objects, %zu bytes\n", objectCount_, bytesAllocated_);

  if (objectCount_ > checkpointObjects_) {
    fprintf(stderr, "!! LEAK: +%zu objects, +%zu bytes\n", objectCount_ - checkpointObjects_,
            bytesAllocated_ - checkpointBytes_);
  } else if (objectCount_ < checkpointObjects_) {
    fprintf(stderr, "OK: -%zu objects, -%zu bytes\n", checkpointObjects_ - objectCount_,
            checkpointBytes_ - bytesAllocated_);
  } else {
    fprintf(stderr, "OK: no change\n");
  }
  fprintf(stderr, "================================\n\n");
}

size_t GC::leakedObjectCount() const {
  return (objectCount_ > checkpointObjects_) ? objectCount_ - checkpointObjects_ : 0;
}

size_t GC::leakedBytes() const {
  return (bytesAllocated_ > checkpointBytes_) ? bytesAllocated_ - checkpointBytes_ : 0;
}

} // namespace spt
