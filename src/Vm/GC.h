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

// ============================================================================
// GC 调试控制 - 全局开关
// ============================================================================
struct GCDebug {
  static inline bool enabled = false;        // 主开关
  static inline bool logCollections = false; // 记录 GC 周期
  static inline bool logAllocations = false; // 记录每次分配（慎用，输出量大）
  static inline bool logFrees = false;       // 记录每次释放
  static inline bool verboseMarking = false; // 详细标记过程

  // 便捷方法
  static void enableAll() {
    enabled = logCollections = logAllocations = logFrees = verboseMarking = true;
  }

  static void disableAll() {
    enabled = logCollections = logAllocations = logFrees = verboseMarking = false;
  }

  static void enableBasic() { // 推荐：只开启基本信息
    enabled = logCollections = true;
    logAllocations = logFrees = verboseMarking = false;
  }
};

// ============================================================================
// GC 统计信息
// ============================================================================
struct GCStats {
  size_t totalCollections = 0;
  size_t totalBytesFreed = 0;
  size_t totalObjectsFreed = 0;
  size_t peakBytesAllocated = 0;
  size_t peakObjectCount = 0;

  // 上次 GC 信息
  size_t lastBytesFreed = 0;
  size_t lastObjectsFreed = 0;
  size_t lastDurationUs = 0;

  // 按类型统计（分配数 / 释放数）
  size_t strings[2] = {0, 0};
  size_t lists[2] = {0, 0};
  size_t maps[2] = {0, 0};
  size_t closures[2] = {0, 0};
  size_t instances[2] = {0, 0};
  size_t fibers[2] = {0, 0};
  size_t upvalues[2] = {0, 0};
  size_t classes[2] = {0, 0};
  size_t nativeFuncs[2] = {0, 0};
  size_t nativeClasses[2] = {0, 0};
  size_t nativeObjects[2] = {0, 0};
};

// ============================================================================
// GC 配置
// ============================================================================
struct GCConfig {
  size_t initialThreshold = 1024 * 1024;
  float growthFactor = 2.0f;
  bool enableStressTest = false; // 每次分配都触发 GC
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
  void *allocateRaw(size_t size);
  void deallocate(GCObject *obj);

  // === 内存跟踪 ===
  void trackAllocation(size_t bytes) { bytesAllocated_ += bytes; }
  void trackDeallocation(size_t bytes) {
    bytesAllocated_ = (bytes <= bytesAllocated_) ? bytesAllocated_ - bytes : 0;
  }

  // === 回收控制 ===
  void collect();
  void collectIfNeeded();
  void setEnabled(bool enabled) { enabled_ = enabled; }

  // === 写屏障 (增量 GC 预留) ===
  void writeBarrier(GCObject *from, GCObject *to);

  // === 基本统计 ===
  size_t bytesAllocated() const { return bytesAllocated_; }
  size_t threshold() const { return threshold_; }
  size_t objectCount() const { return objectCount_; }

  // === 调试统计 ===
  const GCStats &stats() const { return stats_; }

  void resetStats();
  void dumpStats() const;   // 打印统计摘要
  void dumpObjects() const; // 打印所有活跃对象

  // === 泄露检测 ===
  void markCheckpoint();
  void checkLeaks() const; // 检查自检查点以来的泄露
  size_t leakedObjectCount() const;
  size_t leakedBytes() const;

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

  void runFinalizers();
  void invokeGCMethod(Instance *instance);
  void invokeNativeDestructor(NativeInstance *instance);

  void freeObject(GCObject *obj);
  void removeWhiteStrings();

  // === 调试辅助 ===
  static const char *typeName(ValueType type);
  void updateTypeStats(ValueType type, bool isAlloc);

private:
  VM *vm_;
  GCConfig config_;

  GCObject *objects_ = nullptr;
  std::vector<GCObject *> grayStack_;
  std::vector<RootVisitor> roots_;

  std::vector<Instance *> finalizerQueue_;
  std::vector<NativeInstance *> nativeFinalizerQueue_;
  bool inFinalizer_ = false;

  size_t bytesAllocated_ = 0;
  size_t threshold_;
  size_t objectCount_ = 0;
  bool enabled_ = true;

  // 调试相关
  GCStats stats_;
  size_t checkpointObjects_ = 0;
  size_t checkpointBytes_ = 0;
};

// ============================================================================
// 模板实现
// ============================================================================
template <typename T, typename... Args> T *GC::allocate(Args &&...args) {
  collectIfNeeded();

  T *obj = new T(std::forward<Args>(args)...);
  bytesAllocated_ += sizeof(T);
  objectCount_++;

  // 更新峰值
  if (bytesAllocated_ > stats_.peakBytesAllocated) {
    stats_.peakBytesAllocated = bytesAllocated_;
  }
  if (objectCount_ > stats_.peakObjectCount) {
    stats_.peakObjectCount = objectCount_;
  }

  // 链入对象链表
  auto *gcObj = static_cast<GCObject *>(obj);
  gcObj->next = objects_;
  objects_ = gcObj;

  // 调试输出
  if (GCDebug::enabled && GCDebug::logAllocations) {
    fprintf(stderr, "[GC] alloc %s @%p size=%zu total=%zu #%zu\n", typeName(gcObj->type),
            (void *)gcObj, sizeof(T), bytesAllocated_, objectCount_);
  }

  updateTypeStats(gcObj->type, true);
  return obj;
}

} // namespace spt
