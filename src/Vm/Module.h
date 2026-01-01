#pragma once

#include "../Common/Types.h"
#include "Value.h"
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace spt {

// 前置声明
class VM;
class MethodEntry;

// ============================================================================
// 模块加载状态
// ============================================================================
enum class ModuleState {
  UNLOADED, // 未加载
  LOADING,  // 加载中（用于检测循环依赖）
  LOADED,   // 已加载
  ERROR     // 加载失败
};

// ============================================================================
// 模块元数据
// ============================================================================
struct ModuleMetadata {
  std::string name;                      // 模块名称
  std::string path;                      // 文件路径
  std::vector<std::string> dependencies; // 依赖列表
  std::vector<std::string> exports;      // 导出符号
  uint32_t version;                      // 版本号
  uint64_t timestamp;                    // 加载时间戳
  size_t byteSize;                       // 字节码大小

  ModuleMetadata() : version(1), timestamp(0), byteSize(0) {}
};

// ============================================================================
// 模块实例
// ============================================================================
struct Module {
  ModuleMetadata metadata;  // 模块元数据
  ModuleState state;        // 加载状态
  CompiledChunk chunk;      // 编译后的字节码
  MapObject *exportsTable;  // 导出表（Map 对象）
  std::string errorMessage; // 错误信息

  Module() : state(ModuleState::UNLOADED), exportsTable(nullptr) {}
};

// ============================================================================
// 模块加载器接口
// ============================================================================
class ModuleLoader {
public:
  virtual ~ModuleLoader() = default;

  // 根据模块名解析文件路径
  virtual std::string resolvePath(const std::string &moduleName, const std::string &fromPath) = 0;

  // 加载源代码或字节码（返回是否成功，通过out参数返回内容或错误信息）
  virtual bool loadSource(const std::string &path, std::string &outContent,
                          std::string &outError) = 0;

  // 检查文件是否存在
  virtual bool exists(const std::string &path) = 0;

  // 获取文件修改时间（用于热更新）
  virtual uint64_t getTimestamp(const std::string &path) = 0;
};

// ============================================================================
// 默认文件系统加载器
// ============================================================================
class FileSystemLoader : public ModuleLoader {
public:
  explicit FileSystemLoader(const std::vector<std::string> &searchPaths);

  std::string resolvePath(const std::string &moduleName, const std::string &fromPath) override;
  bool loadSource(const std::string &path, std::string &outContent, std::string &outError) override;
  bool exists(const std::string &path) override;
  uint64_t getTimestamp(const std::string &path) override;

  void addSearchPath(const std::string &path);

private:
  std::vector<std::string> searchPaths_;
  std::string normalizePath(const std::string &path);
};

// ============================================================================
// 模块管理器配置
// ============================================================================
struct ModuleManagerConfig {
  bool enableCache = true;           // 启用模块缓存
  bool enableHotReload = true;       // 启用热更新
  bool checkCircularDeps = true;     // 检查循环依赖
  size_t maxCacheSize = 100;         // 最大缓存模块数
  uint64_t hotReloadInterval = 1000; // 热更新检查间隔（毫秒）
};

// ============================================================================
// 模块管理器
// ============================================================================
class ModuleManager {
public:
  explicit ModuleManager(VM *vm, const ModuleManagerConfig &config = {});
  ~ModuleManager();

  // === GC 支持 ===
  // 标记所有模块中的 exportsTable
  void markRoots();

  // === 核心加载接口 ===
  // 加载模块（返回导出表）
  Value loadModule(const std::string &moduleName, const std::string &fromPath = "");

  // 重新加载模块（热更新）
  bool reloadModule(const std::string &moduleName);

  // 预加载模块（异步准备）
  void preloadModule(const std::string &moduleName);

  Value loadCModule(const std::string &moduleName, const std::string &resolvedPath,
                    const MethodEntry *modulEntry);

  // === 依赖管理 ===
  // 获取模块依赖树
  std::vector<std::string> getDependencies(const std::string &moduleName, bool recursive = false);

  // 检测循环依赖
  bool hasCircularDependency(const std::string &moduleName);

  // === 缓存管理 ===
  // 清除模块缓存
  void clearCache(const std::string &moduleName = "");

  // 获取缓存统计
  struct CacheStats {
    size_t totalModules;
    size_t loadedModules;
    size_t totalBytes;
    size_t hitCount;
    size_t missCount;
  };

  CacheStats getCacheStats() const;

  // === 加载器管理 ===
  void setLoader(std::unique_ptr<ModuleLoader> loader);

  ModuleLoader *getLoader() const { return loader_.get(); }

  // === 热更新 ===
  // 检查并重载已修改的模块
  std::vector<std::string> checkForUpdates();

  // 启用/禁用热更新监控
  void setHotReloadEnabled(bool enabled);

  // === 调试与诊断 ===
  void dumpModules() const;
  ModuleMetadata getMetadata(const std::string &moduleName) const;

private:
  // 内部加载实现
  Module *loadModuleInternal(const std::string &moduleName, const std::string &fromPath,
                             std::unordered_set<std::string> &loadingStack);

  // 编译模块
  bool compileModule(Module *module, const std::string &source);

  // 执行模块初始化
  bool executeModule(Module *module);

  // 构建导出表
  void buildExportsTable(Module *module);

  // 依赖解析
  void resolveDependencies(Module *module);

  // 循环依赖检测
  bool detectCircular(const std::string &moduleName, std::unordered_set<std::string> &visited,
                      std::unordered_set<std::string> &stack);

  // 缓存淘汰（LRU）
  void evictCache();

  // 错误处理
  void setError(Module *module, const std::string &error);
  Value createError(const std::string &message);

private:
  VM *vm_;
  ModuleManagerConfig config_;
  std::unique_ptr<ModuleLoader> loader_;

  // 模块缓存：名称 -> 模块实例
  std::unordered_map<std::string, std::unique_ptr<Module>> modules_;
  // 路径映射：绝对路径 -> 模块名
  std::unordered_map<std::string, std::string> pathToName_;

  // 加载顺序（用于 LRU 淘汰）
  std::vector<std::string> loadOrder_;

  // 统计信息
  mutable size_t cacheHits_ = 0;
  mutable size_t cacheMisses_ = 0;
};

} // namespace spt