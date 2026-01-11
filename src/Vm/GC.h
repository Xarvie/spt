#pragma once

#include "Value.h"
#include <cstddef>
#include <functional>

namespace spt {

// 前置声明
class VM;
struct GCObject;
struct Instance;
struct NativeInstance;

// GC 配置
struct GCConfig {
  size_t initialThreshold = 1024 * 1024; // 初始触发阈值
  float growthFactor = 2.0f;             // 阈值增长因子
  bool enableStressTest = false;         // 每次分配都触发 GC (调试用)
};

// 垃圾回收器 - 三色标记清除
class GC {
public:
  explicit GC(VM *vm, const GCConfig &config = {});
  ~GC();

  // === 内存分配 ===
  template <typename T, typename... Args> T *allocate(Args &&...args);

  void *allocateRaw(size_t size);
  void deallocate(GCObject *obj);

  // === 内存跟踪 ===
  void trackAllocation(size_t bytes) { bytesAllocated_ += bytes; }

  void trackDeallocation(size_t bytes) {
    if (bytes <= bytesAllocated_) {
      bytesAllocated_ -= bytes;
    } else {
      bytesAllocated_ = 0; // 防止下溢
    }
  }

  // === 回收控制 ===
  void collect();
  void collectIfNeeded();

  void setEnabled(bool enabled) { enabled_ = enabled; }

  // === 写屏障 (增量 GC 预留) ===
  void writeBarrier(GCObject *from, GCObject *to);

  // === 统计 ===
  size_t bytesAllocated() const { return bytesAllocated_; }

  size_t threshold() const { return threshold_; }

  size_t objectCount() const { return objectCount_; }

  // === 根集注册 ===
  using RootVisitor = std::function<void(Value &)>;
  void addRoot(RootVisitor visitor);
  void removeRoot(RootVisitor visitor);
  void markObject(GCObject *obj);

private:
  void markRoots();

  void markValue(Value &value);
  void traceReferences();
  void sweep();

  // === 终结器支持 ===
  void runFinalizers();                                  // 执行待终结对象的 __gc 方法
  void invokeGCMethod(Instance *instance);               // 调用单个对象的 __gc 方法
  void invokeNativeDestructor(NativeInstance *instance); // 调用 native 对象析构

  void freeObject(GCObject *obj);
  void removeWhiteStrings();

private:
  VM *vm_;
  GCConfig config_;

  GCObject *objects_ = nullptr;       // 所有对象链表
  std::vector<GCObject *> grayStack_; // 灰色对象栈 (三色标记)
  std::vector<RootVisitor> roots_;    // 额外根集

  // === 终结器队列 ===
  std::vector<Instance *> finalizerQueue_;             // 待执行终结器的对象
  std::vector<NativeInstance *> nativeFinalizerQueue_; // 待执行析构的 native 对象
  bool inFinalizer_ = false; // 是否正在执行终结器（防止递归 GC）

  size_t bytesAllocated_ = 0;
  size_t threshold_;
  size_t objectCount_ = 0;
  bool enabled_ = true;
};

// 分配时只记录 sizeof(T)，动态内存由对象自己跟踪
template <typename T, typename... Args> T *GC::allocate(Args &&...args) {
  collectIfNeeded();

  T *obj = new T(std::forward<Args>(args)...);
  bytesAllocated_ += sizeof(T);
  objectCount_++;

  // 链入对象链表
  auto *gcObj = static_cast<GCObject *>(obj);
  gcObj->next = objects_;
  objects_ = gcObj;

  return obj;
}

} // namespace spt