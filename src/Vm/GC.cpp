#include "GC.h"
#include "Fiber.h"
#include "Object.h"
#include "StringPool.h"
#include "VM.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <new>

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

Closure *GC::allocateScriptClosure(const Prototype *proto) {
  size_t count = proto->upvalues.size();
  size_t size = Closure::scriptAllocSize(static_cast<int>(count));

  void *ptr = allocateRaw(size);
  Closure *closure = static_cast<Closure *>(ptr);

  closure->type = ValueType::Closure;
  closure->marked = false;
  closure->next = objects_;
  objects_ = closure;

  closure->kind = ClosureKind::Script;
  closure->proto = proto;
  closure->upvalueCount = static_cast<uint8_t>(count);

  closure->name = nullptr;
  closure->arity = 0;
  closure->receiver = Value::nil();

  if (count > 0) {
    std::memset(closure->scriptUpvalues, 0, count * sizeof(UpValue *));
  }

  objectCount_++;
  if (objectCount_ > stats_.peakObjectCount) {
    stats_.peakObjectCount = objectCount_;
  }

  updateTypeStats(ValueType::Closure, true);

  if (GCDebug::enabled && GCDebug::logAllocations) {
    fprintf(stderr, "[GC] alloc ScriptClosure @%p size=%zu (uv=%zu) total=%zu #%zu\n",
            (void *)closure, size, count, bytesAllocated_, objectCount_);
  }

  return closure;
}

Closure *GC::allocateNativeClosure(int nupvalues) {
  collectIfNeeded();

  size_t size = Closure::nativeAllocSize(nupvalues);

  void *mem = ::operator new(size);
  bytesAllocated_ += size;
  objectCount_++;

  if (bytesAllocated_ > stats_.peakBytesAllocated) {
    stats_.peakBytesAllocated = bytesAllocated_;
  }
  if (objectCount_ > stats_.peakObjectCount) {
    stats_.peakObjectCount = objectCount_;
  }

  Closure *closure = static_cast<Closure *>(mem);

  closure->next = objects_;
  closure->type = ValueType::Closure;
  closure->marked = false;
  objects_ = closure;

  closure->kind = ClosureKind::Native;
  closure->upvalueCount = static_cast<uint8_t>(nupvalues);

  closure->proto = nullptr;

  closure->name = nullptr;
  closure->arity = 0;
  closure->receiver = Value::nil();

  new (&closure->function) NativeFn();

  for (int i = 0; i < nupvalues; i++) {
    closure->nativeUpvalues[i] = Value::nil();
  }

  updateTypeStats(ValueType::Closure, true);

  if (GCDebug::enabled && GCDebug::logAllocations) {
    fprintf(stderr, "[GC] alloc NativeClosure @%p size=%zu upvalues=%d total=%zu #%zu\n",
            (void *)closure, size, nupvalues, bytesAllocated_, objectCount_);
  }

  return closure;
}

StringObject *GC::allocateString(std::string_view sv, uint32_t hash) {
  collectIfNeeded();

  size_t totalSize = StringObject::allocationSize(sv.size());

  void *mem = ::operator new(totalSize);
  bytesAllocated_ += totalSize;
  objectCount_++;

  if (bytesAllocated_ > stats_.peakBytesAllocated) {
    stats_.peakBytesAllocated = bytesAllocated_;
  }
  if (objectCount_ > stats_.peakObjectCount) {
    stats_.peakObjectCount = objectCount_;
  }

  StringObject *str = static_cast<StringObject *>(mem);

  str->next = objects_;
  str->type = ValueType::String;
  str->marked = false;

  str->hash = hash;
  str->length = static_cast<uint32_t>(sv.size());

  char *chars = str->chars();
  std::memcpy(chars, sv.data(), sv.size());
  chars[sv.size()] = '\0';

  objects_ = str;

  updateTypeStats(ValueType::String, true);

  if (GCDebug::enabled && GCDebug::logAllocations) {
    fprintf(stderr, "[GC] alloc String @%p size=%zu len=%zu hash=%08x total=%zu #%zu\n",
            (void *)str, totalSize, sv.size(), hash, bytesAllocated_, objectCount_);
  }

  return str;
}

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

  for (auto [nameStr, val] : vm_->globals_) {

    markObject(const_cast<StringObject *>(nameStr));

    markValue(val);
  }

  for (auto &visitor : roots_) {
    Value tempValue = Value::nil();
    visitor(tempValue);
  }

  if (vm_->hasError_) {
    markValue(vm_->errorValue_);
  }

  if (vm_->moduleManager()) {
    vm_->moduleManager()->markRoots();
  }

  for (Instance *instance : finalizerQueue_) {
    markObject(instance);
  }

  const SymbolTable &syms = vm_->symbols();
  auto markSymbol = [this](StringObject *s) {
    if (s)
      markObject(s);
  };

  markSymbol(syms.init);
  markSymbol(syms.gc);
  markSymbol(syms.str);
  markSymbol(syms.len);
  markSymbol(syms.push);
  markSymbol(syms.pop);
  markSymbol(syms.length);
  markSymbol(syms.size);
  markSymbol(syms.get);
  markSymbol(syms.set);
  markSymbol(syms.has);
  markSymbol(syms.keys);
  markSymbol(syms.values);
  markSymbol(syms.clear);
  markSymbol(syms.slice);
  markSymbol(syms.indexOf);
  markSymbol(syms.contains);
  markSymbol(syms.join);
  markSymbol(syms.split);
  markSymbol(syms.trim);
  markSymbol(syms.toUpper);
  markSymbol(syms.toLower);
  markSymbol(syms.replace);
  markSymbol(syms.startsWith);
  markSymbol(syms.endsWith);
  markSymbol(syms.find);
  markSymbol(syms.insert);
  markSymbol(syms.removeAt);
  markSymbol(syms.remove);
  markSymbol(syms.create);
  markSymbol(syms.yield);
  markSymbol(syms.current);
  markSymbol(syms.abort);
  markSymbol(syms.suspend);
  markSymbol(syms.call);
  markSymbol(syms.tryCall);
  markSymbol(syms.isDone);
  markSymbol(syms.error);
  markSymbol(syms.Fiber);
}

void GC::markValue(Value &value) {
  if (value.type == ValueType::Nil || value.type == ValueType::Bool ||
      value.type == ValueType::Int || value.type == ValueType::Float) {
    return;
  }
  if (value.as.gc) {
    markObject(value.as.gc);
  }
}

void GC::markObject(GCObject *obj) {
  if (!obj || obj->marked)
    return;

  if (GCDebug::enabled && GCDebug::verboseMarking) {
    fprintf(stderr, "[GC] mark %s @%p\n", typeName(obj->type), (void *)obj);
  }

  obj->marked = true;
  grayStack_.push_back(obj);
}

void GC::traceReferences() {
  while (!grayStack_.empty()) {
    GCObject *obj = grayStack_.back();
    grayStack_.pop_back();

    switch (obj->type) {
    case ValueType::String:
      break;

    case ValueType::List: {
      ListObject *list = static_cast<ListObject *>(obj);
      for (Value &elem : list->elements) {
        markValue(elem);
      }
      break;
    }

    case ValueType::Map: {
      MapObject *map = static_cast<MapObject *>(obj);
      for (auto &[key, val] : map->entries) {
        Value k = key;
        markValue(k);
        markValue(val);
      }
      break;
    }

    case ValueType::Closure: {
      Closure *closure = static_cast<Closure *>(obj);

      if (closure->name) {
        markObject(closure->name);
      }

      if (!closure->receiver.isNil() && closure->receiver.as.gc) {
        markObject(closure->receiver.as.gc);
      }

      if (closure->isScript()) {
        for (int i = 0; i < closure->upvalueCount; i++) {
          if (closure->scriptUpvalues[i]) {
            markObject(closure->scriptUpvalues[i]);
          }
        }
      } else {
        for (int i = 0; i < closure->upvalueCount; i++) {
          markValue(closure->nativeUpvalues[i]);
        }
      }
      break;
    }

    case ValueType::Upvalue: {
      UpValue *upvalue = static_cast<UpValue *>(obj);
      markValue(upvalue->closed);
      break;
    }

    case ValueType::Class: {
      ClassObject *klass = static_cast<ClassObject *>(obj);

      for (auto [nameKey, method] : klass->methods) {
        markObject(const_cast<StringObject *>(nameKey));
        markValue(method);
      }
      for (auto [nameKey, staticVal] : klass->statics) {
        markObject(const_cast<StringObject *>(nameKey));
        markValue(staticVal);
      }
      break;
    }

    case ValueType::Object: {
      Instance *instance = static_cast<Instance *>(obj);
      if (instance->klass) {
        markObject(instance->klass);
      }

      for (auto [nameKey, fieldVal] : instance->fields) {
        markObject(const_cast<StringObject *>(nameKey));
        markValue(fieldVal);
      }
      break;
    }

    case ValueType::NativeObject: {
      NativeInstance *nativeInstance = static_cast<NativeInstance *>(obj);
      if (nativeInstance->klass) {
        markObject(nativeInstance->klass);
      }

      for (auto [nameKey, fieldVal] : nativeInstance->fields) {
        markObject(const_cast<StringObject *>(nameKey));
        markValue(fieldVal);
      }
      break;
    }

    case ValueType::Fiber: {
      FiberObject *fiber = static_cast<FiberObject *>(obj);

      for (Value *slot = fiber->stack; slot < fiber->stackTop; ++slot) {
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

      UpValue *uv = fiber->openUpvalues;
      while (uv) {
        markObject(uv);
        uv = uv->nextOpen;
      }

      if (fiber->closure) {
        markObject(fiber->closure);
      }

      if (fiber->caller) {
        markObject(fiber->caller);
      }

      markValue(fiber->error);
      markValue(fiber->yieldValue);
      break;
    }

    default:
      break;
    }
  }
}

void GC::removeWhiteStrings() {
  if (stringPool_) {
    stringPool_->removeWhiteStrings();
  }
}

void GC::sweep() {
  GCObject **ptr = &objects_;

  while (*ptr) {
    GCObject *obj = *ptr;

    if (obj->marked) {
      obj->marked = false;
      ptr = &obj->next;
    } else {
      if (obj->type == ValueType::Object) {
        Instance *instance = static_cast<Instance *>(obj);
        if (instance->klass && instance->klass->hasFinalizer() && !instance->isFinalized) {
          finalizerQueue_.push_back(instance);
          obj->marked = true;
          ptr = &obj->next;
          continue;
        }
      } else if (obj->type == ValueType::NativeObject) {
        NativeInstance *nativeInstance = static_cast<NativeInstance *>(obj);
        if (nativeInstance->klass && nativeInstance->klass->hasFinalizer() &&
            !nativeInstance->isFinalized) {
          finalizerQueue_.push_back(reinterpret_cast<Instance *>(nativeInstance));
          obj->marked = true;
          ptr = &obj->next;
          continue;
        }
      }

      *ptr = obj->next;

      if (GCDebug::enabled && GCDebug::logFrees) {
        fprintf(stderr, "[GC] free %s @%p\n", typeName(obj->type), (void *)obj);
      }

      updateTypeStats(obj->type, false);
      freeObject(obj);
    }
  }
}

void GC::freeObject(GCObject *obj) {
  switch (obj->type) {
  case ValueType::String: {
    StringObject *str = static_cast<StringObject *>(obj);
    size_t size = str->allocationSize();
    bytesAllocated_ -= size;
    ::operator delete(str);
    break;
  }
  case ValueType::List: {
    ListObject *list = static_cast<ListObject *>(obj);
    bytesAllocated_ -= sizeof(ListObject);
    delete list;
    break;
  }
  case ValueType::Map: {
    MapObject *map = static_cast<MapObject *>(obj);
    bytesAllocated_ -= sizeof(MapObject);
    delete map;
    break;
  }
  case ValueType::Closure: {
    Closure *closure = static_cast<Closure *>(obj);
    size_t size = closure->allocSize();

    if (closure->isNative()) {
      closure->function.~NativeFn();
    }

    bytesAllocated_ -= size;
    ::operator delete(closure);
    break;
  }
  case ValueType::Class: {
    ClassObject *klass = static_cast<ClassObject *>(obj);
    bytesAllocated_ -= sizeof(ClassObject);
    delete klass;
    break;
  }
  case ValueType::Object: {
    Instance *instance = static_cast<Instance *>(obj);
    bytesAllocated_ -= sizeof(Instance);
    delete instance;
    break;
  }
  case ValueType::Upvalue: {
    UpValue *upvalue = static_cast<UpValue *>(obj);
    bytesAllocated_ -= sizeof(UpValue);
    delete upvalue;
    break;
  }
  case ValueType::Fiber: {
    FiberObject *fiber = static_cast<FiberObject *>(obj);
    bytesAllocated_ -= sizeof(FiberObject) + (fiber->stackSize * sizeof(Value)) +
                       (fiber->framesCapacity * sizeof(CallFrame)) +
                       (fiber->deferStack.capacity() * sizeof(Value));
    delete fiber;
    break;
  }

  case ValueType::NativeObject: {
    NativeInstance *nativeInstance = static_cast<NativeInstance *>(obj);

    bytesAllocated_ -= sizeof(NativeInstance);
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
  if (!instance)
    return;

  ClassObject *klass = nullptr;

  if (instance->type == ValueType::Object) {
    klass = instance->klass;
  } else if (instance->type == ValueType::NativeObject) {
    klass = reinterpret_cast<NativeInstance *>(instance)->klass;
  }

  if (!klass)
    return;

  Value gcMethod = klass->gcMethod;
  if (!gcMethod.isClosure())
    return;

  Closure *closure = static_cast<Closure *>(gcMethod.asGC());

  if (!closure->isScript())
    return;

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
  case ValueType::Upvalue:
    return "Upvalue";
  case ValueType::Fiber:
    return "Fiber";

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
      std::string_view sv = str->view();
      if (sv.length() <= 40) {
        fprintf(stderr, " \"%.*s\"", static_cast<int>(sv.length()), sv.data());
      } else {
        fprintf(stderr, " \"%.40s...\"", sv.data());
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
    case ValueType::Closure: {
      auto *closure = static_cast<Closure *>(obj);
      fprintf(stderr, " %s '%s'", closure->isNative() ? "native" : "script", closure->getName());
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