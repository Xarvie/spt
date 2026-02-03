#include "GC.h"
#include "Bytes.h"
#include "Fiber.h"
#include "Object.h"
#include "StringPool.h"
#include "VM.h"
#include <algorithm>
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
  return closure;
}

Closure *GC::allocateNativeClosure(int nupvalues) {
  collectIfNeeded();

  size_t size = Closure::nativeAllocSize(nupvalues);
  void *mem = ::operator new(size);
  bytesAllocated_ += size;
  objectCount_++;

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

  return closure;
}

StringObject *GC::allocateString(std::string_view sv, uint32_t hash) {
  collectIfNeeded();

  size_t totalSize = StringObject::allocationSize(sv.size());
  void *mem = ::operator new(totalSize);
  bytesAllocated_ += totalSize;
  objectCount_++;

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
  return str;
}

FiberObject *GC::allocateFiber() {
  collectIfNeeded();

  FiberObject *fiber = new FiberObject();
  FiberObject::init(fiber);

  size_t totalSize = sizeof(FiberObject) + fiber->totalAllocatedBytes();
  bytesAllocated_ += totalSize;
  objectCount_++;

  fiber->next = objects_;
  objects_ = fiber;

  return fiber;
}

BytesObject *GC::allocateBytes(size_t size) {
  collectIfNeeded();

  BytesObject *bytes = new BytesObject(size);
  bytesAllocated_ += sizeof(BytesObject) + size;
  objectCount_++;

  bytes->next = objects_;
  objects_ = bytes;

  return bytes;
}

void GC::collect() {
  if (!enabled_ || inFinalizer_)
    return;

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
  if (bytesAllocated_ > threshold_) {
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

  markSymbol(syms.geter);
  markSymbol(syms.seter);
  markSymbol(syms.getitem);
  markSymbol(syms.setitem);

  markSymbol(syms.add);
  markSymbol(syms.sub);
  markSymbol(syms.mul);
  markSymbol(syms.div);
  markSymbol(syms.mod);
  markSymbol(syms.pow);
  markSymbol(syms.unm);
  markSymbol(syms.idiv);

  markSymbol(syms.eq);
  markSymbol(syms.lt);
  markSymbol(syms.le);

  markSymbol(syms.band);
  markSymbol(syms.bor);
  markSymbol(syms.bxor);
  markSymbol(syms.bnot);
  markSymbol(syms.shl);
  markSymbol(syms.shr);

  markSymbol(syms.push);
  markSymbol(syms.pop);
  markSymbol(syms.length);
  markSymbol(syms.byteLength);
  markSymbol(syms.size);
  markSymbol(syms.get);
  markSymbol(syms.set);
  markSymbol(syms.has);
  markSymbol(syms.keys);
  markSymbol(syms.values);
  markSymbol(syms.clear);
  markSymbol(syms.slice);
  markSymbol(syms.byteSlice);
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

  markSymbol(syms.Bytes);
  markSymbol(syms.resize);
  markSymbol(syms.fill);
  markSymbol(syms.readInt8);
  markSymbol(syms.readUInt8);
  markSymbol(syms.readInt16);
  markSymbol(syms.readUInt16);
  markSymbol(syms.readInt32);
  markSymbol(syms.readUInt32);
  markSymbol(syms.readFloat);
  markSymbol(syms.readDouble);
  markSymbol(syms.readString);
  markSymbol(syms.writeInt8);
  markSymbol(syms.writeUInt8);
  markSymbol(syms.writeInt16);
  markSymbol(syms.writeUInt16);
  markSymbol(syms.writeInt32);
  markSymbol(syms.writeUInt32);
  markSymbol(syms.writeFloat);
  markSymbol(syms.writeDouble);
  markSymbol(syms.writeString);
  markSymbol(syms.toHex);
  markSymbol(syms.fromList);
  markSymbol(syms.fromStr);
  markSymbol(syms.toStr);
  markSymbol(syms.fromHex);
}

void GC::markValue(Value &value) {
  if (value.type == ValueType::Nil || value.type == ValueType::Bool ||
      value.type == ValueType::Int || value.type == ValueType::Float ||
      value.type == ValueType::LightUserData) {
    return;
  }
  if (value.as.gc) {
    markObject(value.as.gc);
  }
}

void GC::markObject(GCObject *obj) {
  if (!obj || obj->marked)
    return;

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

    case ValueType::Bytes:
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

      markValue(closure->receiver);

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

      for (int i = 0; i < MM_MAX; ++i) {
        Value &method = klass->magicMethods[i];
        if (!method.isNil()) {
          markValue(method);
        }
      }

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
  case ValueType::Bytes: {
    BytesObject *bytes = static_cast<BytesObject *>(obj);
    bytesAllocated_ -= sizeof(BytesObject) + bytes->data.capacity();
    delete bytes;
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
    size_t totalSize = sizeof(FiberObject) + fiber->totalAllocatedBytes();
    bytesAllocated_ -= totalSize;
    FiberObject::destroy(fiber);
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

  Value gcMethod = klass->getMagicMethod(MM_GC);
  if (!gcMethod.isClosure())
    return;

  Closure *closure = static_cast<Closure *>(gcMethod.asGC());

  if (closure->isNative()) {
    Value instanceVal = Value::object(instance);

    try {
      closure->function(vm_, closure, 1, &instanceVal);
    } catch (...) {
    }
    return;
  }

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

} // namespace spt