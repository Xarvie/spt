#pragma once

#include "Value.h"
#include <cstddef>
#include <functional>

namespace spt {

class VM;
struct GCObject;
struct Instance;
struct NativeInstance;
struct Closure;
struct StringObject;
struct FiberObject;
struct BytesObject;
class StringPool;

// ============================================================================
// GC 配置
// ============================================================================
struct GCConfig {
  size_t initialThreshold = 1024 * 1024;
  float growthFactor = 2.0f;
};

// ============================================================================
// 垃圾回收器 - 三色标记清除
// ============================================================================
class GC {
public:
  explicit GC(VM *vm, const GCConfig &config = {});
  ~GC();

  // === 内存分配 ===
  template <typename T, typename... Args> T *allocate(Args &&...args);

  Closure *allocateScriptClosure(const Prototype *proto);
  Closure *allocateNativeClosure(int nupvalues = 0);
  StringObject *allocateString(std::string_view sv, uint32_t hash);
  FiberObject *allocateFiber();
  BytesObject *allocateBytes(size_t size = 0);

  // === 回收控制 ===
  void collect();
  void collectIfNeeded();

  void setEnabled(bool enabled) { enabled_ = enabled; }

  // === 写屏障 (增量 GC 预留) ===
  void writeBarrier(GCObject *from, GCObject *to);

  // === 基本信息 ===
  size_t bytesAllocated() const { return bytesAllocated_; }

  size_t threshold() const { return threshold_; }

  size_t objectCount() const { return objectCount_; }

  // === 根集注册 ===
  using RootVisitor = std::function<void(Value &)>;
  void addRoot(RootVisitor visitor);
  void removeRoot(RootVisitor visitor);
  void markObject(GCObject *obj);

  void setStringPool(StringPool *pool) { stringPool_ = pool; }

private:
  void *allocateRaw(size_t size);
  void deallocate(GCObject *obj);

  void markRoots();
  void markValue(Value &value);
  void traceReferences();
  void sweep();

  void runFinalizers();
  void invokeGCMethod(Instance *instance);

  void freeObject(GCObject *obj);
  void removeWhiteStrings();

private:
  VM *vm_;
  GCConfig config_;
  StringPool *stringPool_ = nullptr;

  GCObject *objects_ = nullptr;
  std::vector<GCObject *> grayStack_;
  std::vector<RootVisitor> roots_;

  std::vector<Instance *> finalizerQueue_;
  bool inFinalizer_ = false;

  size_t bytesAllocated_ = 0;
  size_t threshold_;
  size_t objectCount_ = 0;
  bool enabled_ = true;
};

// ============================================================================
// 模板实现
// ============================================================================
template <typename T, typename... Args> T *GC::allocate(Args &&...args) {
  collectIfNeeded();

  T *obj = new T(std::forward<Args>(args)...);
  bytesAllocated_ += sizeof(T);
  objectCount_++;

  auto *gcObj = static_cast<GCObject *>(obj);
  gcObj->next = objects_;
  objects_ = gcObj;

  return obj;
}

} // namespace spt