#include "Module.h"
#include "../Ast/ast.h"
#include "../Compiler/Compiler.h"
#include "VM.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace spt {

static std::string FSLoader_resolvePath(ModuleLoader *self, const std::string &moduleName,
                                        const std::string &fromPath) {
  FileSystemLoader *loader = reinterpret_cast<FileSystemLoader *>(self);
  namespace fs = std::filesystem;

  std::string baseName = moduleName;
  if (baseName.size() > 4 && (baseName.substr(baseName.size() - 4) == ".spt")) {
    baseName = baseName.substr(0, baseName.size() - 4);
  } else if (baseName.size() > 5 && (baseName.substr(baseName.size() - 5) == ".sptc")) {
    baseName = baseName.substr(0, baseName.size() - 5);
  }

  std::vector<std::string> extensions = {".spt", ".sptc"};

  if (!fromPath.empty()) {
    fs::path parent = fs::path(fromPath).parent_path();
    for (const auto &ext : extensions) {
      fs::path candidate = parent / (baseName + ext);
      std::error_code ec;
      if (fs::exists(candidate, ec)) {
        return fs::absolute(candidate).string();
      }
    }
  }

  for (const auto &searchPath : loader->searchPaths) {
    for (const auto &ext : extensions) {
      fs::path candidate = fs::path(searchPath) / (baseName + ext);
      std::error_code ec;
      if (fs::exists(candidate, ec)) {
        return fs::absolute(candidate).string();
      }
    }
  }

  std::error_code ec;
  if (fs::exists(moduleName, ec)) {
    return fs::absolute(moduleName).string();
  }

  return "";
}

static bool FSLoader_loadSource(ModuleLoader *self, const std::string &path,
                                std::string &outContent, std::string &outError) {
  std::ifstream file(path);
  if (!file) {
    outError = "Cannot open file: " + path;
    return false;
  }
  std::stringstream buffer;
  buffer << file.rdbuf();
  outContent = buffer.str();
  return true;
}

static bool FSLoader_exists(ModuleLoader *self, const std::string &path) {
  return std::filesystem::exists(path);
}

static uint64_t FSLoader_getTimestamp(ModuleLoader *self, const std::string &path) {
  std::error_code ec;
  auto ftime = std::filesystem::last_write_time(path, ec);
  if (ec) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(ftime.time_since_epoch()).count();
}

static void FSLoader_destroy(ModuleLoader *self) {
  FileSystemLoader *loader = reinterpret_cast<FileSystemLoader *>(self);
  delete loader;
}

static const ModuleLoaderVTable FileSystemLoader_vtable = {FSLoader_resolvePath,
                                                           FSLoader_loadSource, FSLoader_exists,
                                                           FSLoader_getTimestamp, FSLoader_destroy};

FileSystemLoader *FileSystemLoader_create(const std::vector<std::string> &searchPaths) {
  FileSystemLoader *loader = new FileSystemLoader();
  loader->base.vtable = &FileSystemLoader_vtable;
  loader->searchPaths = searchPaths;
  if (loader->searchPaths.empty()) {
    loader->searchPaths.push_back(".");
  }
  return loader;
}

void FileSystemLoader_destroy(FileSystemLoader *loader) { delete loader; }

void FileSystemLoader_addSearchPath(FileSystemLoader *loader, const std::string &path) {
  loader->searchPaths.push_back(path);
}

ModuleManager::ModuleManager(VM *vm, const ModuleManagerConfig &config) : vm_(vm), config_(config) {
  std::vector<std::string> defaultPaths = {".", "./lib", "./modules"};
  loader_ = FileSystemLoader_toLoader(FileSystemLoader_create(defaultPaths));
}

ModuleManager::~ModuleManager() {

  for (auto &[name, module] : modules_) {
    if (module && module->state == ModuleState::LOADED) {
      module->chunk.destroyRuntimeData();
    }
  }

  modules_.clear();
  pathToName_.clear();

  if (loader_ && loader_->vtable && loader_->vtable->destroy) {
    loader_->vtable->destroy(loader_);
    loader_ = nullptr;
  }
}

Value ModuleManager::loadModule(const std::string &moduleName, const std::string &fromPath) {
  if (config_.enableCache) {
    auto it = modules_.find(moduleName);
    if (it != modules_.end()) {
      Module *cached = it->second.get();

      if (cached->state == ModuleState::LOADED) {
        cacheHits_++;
        if (config_.enableHotReload) {
          uint64_t currentTime = loader_->getTimestamp(cached->metadata.path);
          if (currentTime > cached->metadata.timestamp) {
            if (reloadModule(moduleName)) {
              return Value::object(modules_[moduleName]->exportsTable);
            }
          }
        }
        return Value::object(cached->exportsTable);
      }
      if (cached->state == ModuleState::ERROR) {
        return createError(cached->errorMessage);
      }
    }
  }

  cacheMisses_++;
  ankerl::unordered_dense::set<std::string> loadingStack;
  Module *module = loadModuleInternal(moduleName, fromPath, loadingStack);

  if (!module || module->state != ModuleState::LOADED) {
    std::string error = module ? module->errorMessage : "Module load failed";
    return createError(error);
  }

  return Value::object(module->exportsTable);
}

Module *ModuleManager::loadModuleInternal(const std::string &moduleName,
                                          const std::string &fromPath,
                                          ankerl::unordered_dense::set<std::string> &loadingStack) {

  if (loadingStack.find(moduleName) != loadingStack.end()) {
    auto module = std::make_unique<Module>();
    Module *modulePtr = module.get();
    modulePtr->metadata.name = moduleName;
    setError(modulePtr, "Circular dependency detected: " + moduleName);

    modules_[moduleName] = std::move(module);
    return modulePtr;
  }

  loadingStack.insert(moduleName);

  std::string resolvedPath = loader_->resolvePath(moduleName, fromPath);
  if (resolvedPath.empty()) {
    auto module = std::make_unique<Module>();
    Module *modulePtr = module.get();
    modulePtr->metadata.name = moduleName;
    setError(modulePtr, "Module not found: " + moduleName);

    modules_[moduleName] = std::move(module);
    loadingStack.erase(moduleName);
    return modulePtr;
  }

  auto module = std::make_unique<Module>();
  Module *modulePtr = module.get();

  modules_[moduleName] = std::move(module);

  modulePtr->metadata.name = moduleName;
  modulePtr->metadata.path = resolvedPath;
  modulePtr->metadata.timestamp = loader_->getTimestamp(resolvedPath);
  modulePtr->state = ModuleState::LOADING;

  std::string source;
  std::string loadError;
  if (!loader_->loadSource(resolvedPath, source, loadError)) {
    setError(modulePtr, "Failed to load source: " + loadError);
    loadingStack.erase(moduleName);
    return modulePtr;
  }

  if (!compileModule(modulePtr, source)) {
    loadingStack.erase(moduleName);
    return modulePtr;
  }

  resolveDependencies(modulePtr);

  for (const auto &dep : modulePtr->metadata.dependencies) {
    Module *depModule = loadModuleInternal(dep, resolvedPath, loadingStack);
    if (!depModule || depModule->state != ModuleState::LOADED) {
      setError(modulePtr, "Failed to load dependency: " + dep);
      loadingStack.erase(moduleName);
      return modulePtr;
    }
  }

  if (!executeModule(modulePtr)) {
    loadingStack.erase(moduleName);
    return modulePtr;
  }

  buildExportsTable(modulePtr);

  modulePtr->state = ModuleState::LOADED;

  if (config_.enableCache) {
    pathToName_[resolvedPath] = moduleName;
    loadOrder_.push_back(moduleName);
    if (modules_.size() > config_.maxCacheSize) {
      evictCache();
    }
  }

  loadingStack.erase(moduleName);
  return modulePtr;
}

bool ModuleManager::compileModule(Module *module, const std::string &source) {
  AstNode *ast = loadAst(source, module->metadata.path);
  if (!ast) {
    setError(module, "Parse failed");
    return false;
  }

  Compiler compiler(module->metadata.name, module->metadata.path);
  std::string compileError;
  compiler.setErrorHandler([&](const CompileError &err) {
    compileError += "Line " + std::to_string(err.line) + ": " + err.message + "\n";
  });

  module->chunk = compiler.compile(ast);
  module->metadata.exports = module->chunk.exports;
  destroyAst(ast);

  if (compiler.hasError()) {
    setError(module, "Compilation failed:\n" + compileError);
    return false;
  }

  module->metadata.byteSize = module->chunk.mainProto.code.size() * sizeof(Instruction);
  return true;
}

bool ModuleManager::executeModule(Module *module) {
  InterpretResult result = vm_->executeModule(module->chunk);
  if (result != InterpretResult::OK) {
    setError(module, "Module execution failed");
    return false;
  }
  return true;
}

void ModuleManager::buildExportsTable(Module *module) {
  Value envValue = vm_->getLastModuleResult();

  vm_->protect(envValue);

  if (!envValue.isMap()) {
    module->exportsTable = vm_->allocateMap(0);
    vm_->unprotect(1);
    return;
  }

  MapObject *envMap = static_cast<MapObject *>(envValue.asGC());
  module->exportsTable = vm_->allocateMap(static_cast<int>(module->metadata.exports.size()));

  for (const auto &exportName : module->metadata.exports) {
    StringObject *key = vm_->allocateString(exportName);
    Value keyVal = Value::object(key);

    vm_->protect(keyVal);

    Value val = envMap->get(keyVal);
    if (!val.isNil()) {
      module->exportsTable->set(keyVal, val);
    }

    vm_->unprotect(1);
  }

  vm_->unprotect(1);
}

void ModuleManager::resolveDependencies(Module *module) {}

bool ModuleManager::reloadModule(const std::string &moduleName) {
  auto it = modules_.find(moduleName);
  if (it == modules_.end())
    return false;

  Module *oldModule = it->second.get();

  auto newModule = std::make_unique<Module>();
  newModule->metadata.name = moduleName;

  std::string path = oldModule->metadata.path;
  if (path.empty()) {
    path = loader_->resolvePath(moduleName, "");
  }

  if (path.empty())
    return false;

  newModule->metadata.path = path;

  std::string source;
  std::string loadError;
  if (!loader_->loadSource(path, source, loadError)) {
    return false;
  }

  newModule->metadata.timestamp = loader_->getTimestamp(path);

  if (!compileModule(newModule.get(), source)) {
    return false;
  }

  resolveDependencies(newModule.get());

  if (!executeModule(newModule.get())) {

    newModule->chunk.destroyRuntimeData();
    return false;
  }

  buildExportsTable(newModule.get());

  oldModule->chunk.destroyRuntimeData();

  oldModule->chunk = std::move(newModule->chunk);
  oldModule->exportsTable = newModule->exportsTable;
  oldModule->metadata = newModule->metadata;
  oldModule->state = ModuleState::LOADED;
  oldModule->errorMessage.clear();

  return true;
}

void ModuleManager::preloadModule(const std::string &moduleName) { loadModule(moduleName); }

Value ModuleManager::loadCModule(const std::string &moduleName, const std::string &resolvedPath,
                                 const MethodEntry *modulEntry) {
  if (modulEntry == nullptr) {
    printf("error loadCModule modulEntry is nullptr\n");
    return Value::nil();
  }

  Module *modulePtr = nullptr;
  {
    auto module = std::make_unique<Module>();
    modulePtr = module.get();
    modules_[moduleName] = std::move(module);
  }

  modulePtr->metadata.name = moduleName;
  modulePtr->metadata.path = resolvedPath;
  modulePtr->metadata.timestamp = 0;
  modulePtr->state = ModuleState::LOADING;

  modulePtr->exportsTable = vm_->allocateMap(static_cast<int>(modulePtr->metadata.exports.size()));

  while (modulEntry->name && modulEntry->fn) {
    modulePtr->metadata.exports.push_back(modulEntry->name);
    StringObject *key = vm_->allocateString(modulEntry->name);

    Closure *native = vm_->gc().allocateNativeClosure(0);
    native->name = vm_->allocateString(modulEntry->name);
    native->function = modulEntry->fn;
    native->arity = modulEntry->arity;

    Value keyVal = Value::object(key);
    Value valueVal = Value::object(native);
    modulePtr->exportsTable->set(keyVal, valueVal);
    modulEntry++;
  }

  modulePtr->state = ModuleState::LOADED;

  if (config_.enableCache) {
    pathToName_[resolvedPath] = moduleName;
    loadOrder_.push_back(moduleName);
    if (modules_.size() > config_.maxCacheSize) {
      evictCache();
    }
  }

  if (!modulePtr || modulePtr->state != ModuleState::LOADED) {
    std::string error = modulePtr ? modulePtr->errorMessage : "Module load failed";
    return createError(error);
  }

  return Value::object(modulePtr->exportsTable);
}

std::vector<std::string> ModuleManager::getDependencies(const std::string &moduleName,
                                                        bool recursive) {
  auto it = modules_.find(moduleName);
  if (it == modules_.end())
    return {};

  std::vector<std::string> result = it->second->metadata.dependencies;
  if (recursive) {
    ankerl::unordered_dense::set<std::string> visited;
    std::function<void(const std::string &)> collectDeps;
    collectDeps = [&](const std::string &name) {
      if (visited.count(name))
        return;
      visited.insert(name);
      auto depIt = modules_.find(name);
      if (depIt == modules_.end())
        return;
      for (const auto &dep : depIt->second->metadata.dependencies) {
        result.push_back(dep);
        collectDeps(dep);
      }
    };
    collectDeps(moduleName);
  }
  return result;
}

bool ModuleManager::hasCircularDependency(const std::string &moduleName) {
  ankerl::unordered_dense::set<std::string> visited;
  ankerl::unordered_dense::set<std::string> stack;
  return detectCircular(moduleName, visited, stack);
}

bool ModuleManager::detectCircular(const std::string &moduleName,
                                   ankerl::unordered_dense::set<std::string> &visited,
                                   ankerl::unordered_dense::set<std::string> &stack) {
  if (stack.count(moduleName))
    return true;
  if (visited.count(moduleName))
    return false;

  visited.insert(moduleName);
  stack.insert(moduleName);

  auto it = modules_.find(moduleName);
  if (it != modules_.end()) {
    for (const auto &dep : it->second->metadata.dependencies) {
      if (detectCircular(dep, visited, stack))
        return true;
    }
  }

  stack.erase(moduleName);
  return false;
}

void ModuleManager::clearCache(const std::string &moduleName) {
  if (moduleName.empty()) {

    for (auto &[name, module] : modules_) {
      if (module && module->state == ModuleState::LOADED) {
        module->chunk.destroyRuntimeData();
      }
    }
    modules_.clear();
    pathToName_.clear();
    loadOrder_.clear();
  } else {
    auto it = modules_.find(moduleName);
    if (it != modules_.end()) {

      if (it->second && it->second->state == ModuleState::LOADED) {
        it->second->chunk.destroyRuntimeData();
      }
      pathToName_.erase(it->second->metadata.path);
      modules_.erase(it);
      auto orderIt = std::find(loadOrder_.begin(), loadOrder_.end(), moduleName);
      if (orderIt != loadOrder_.end())
        loadOrder_.erase(orderIt);
    }
  }
}

ModuleManager::CacheStats ModuleManager::getCacheStats() const {
  CacheStats stats;
  stats.totalModules = modules_.size();
  stats.loadedModules = 0;
  stats.totalBytes = 0;
  stats.hitCount = cacheHits_;
  stats.missCount = cacheMisses_;

  for (const auto &[name, module] : modules_) {
    if (module->state == ModuleState::LOADED)
      stats.loadedModules++;
    stats.totalBytes += module->metadata.byteSize;
  }
  return stats;
}

void ModuleManager::setLoader(ModuleLoader *loader) {
  if (loader_ && loader_->vtable && loader_->vtable->destroy) {
    loader_->vtable->destroy(loader_);
  }
  loader_ = loader;
}

std::vector<std::string> ModuleManager::checkForUpdates() {
  std::vector<std::string> updated;

  if (!config_.enableHotReload) {
    return updated;
  }

  for (const auto &[name, module] : modules_) {
    if (module->state != ModuleState::LOADED)
      continue;

    uint64_t currentTime = loader_->getTimestamp(module->metadata.path);
    if (currentTime > module->metadata.timestamp) {
      if (reloadModule(name)) {
        updated.push_back(name);
      }
    }
  }

  return updated;
}

void ModuleManager::setHotReloadEnabled(bool enabled) { config_.enableHotReload = enabled; }

void ModuleManager::dumpModules() const {
  printf("\n=== Module Manager Status ===\n");
  printf("Total modules: %zu\n", modules_.size());
  printf("Cache hits: %zu, misses: %zu\n", cacheHits_, cacheMisses_);
  printf("\nLoaded modules:\n");

  for (const auto &[name, module] : modules_) {
    const char *stateStr = "UNKNOWN";
    switch (module->state) {
    case ModuleState::UNLOADED:
      stateStr = "UNLOADED";
      break;
    case ModuleState::LOADING:
      stateStr = "LOADING";
      break;
    case ModuleState::LOADED:
      stateStr = "LOADED";
      break;
    case ModuleState::ERROR:
      stateStr = "ERROR";
      break;
    }

    printf("  [%s] %s\n", stateStr, name.c_str());
    printf("    Path: %s\n", module->metadata.path.c_str());
    printf("    Exports: %zu, Dependencies: %zu\n", module->metadata.exports.size(),
           module->metadata.dependencies.size());
    printf("    Size: %zu bytes\n", module->metadata.byteSize);
  }

  printf("============================\n\n");
}

ModuleMetadata ModuleManager::getMetadata(const std::string &moduleName) const {
  auto it = modules_.find(moduleName);
  if (it != modules_.end())
    return it->second->metadata;
  return ModuleMetadata();
}

void ModuleManager::evictCache() {
  if (loadOrder_.empty())
    return;

  const std::string &oldestModule = loadOrder_.front();
  auto it = modules_.find(oldestModule);
  if (it != modules_.end() && it->second && it->second->state == ModuleState::LOADED) {
    it->second->chunk.destroyRuntimeData();
  }

  clearCache(oldestModule);
}

void ModuleManager::setError(Module *module, const std::string &error) {
  module->state = ModuleState::ERROR;
  module->errorMessage = error;
}

Value ModuleManager::createError(const std::string &message) {
  MapObject *errorObj = vm_->allocateMap(2);
  StringObject *errorKey = vm_->allocateString("error");
  StringObject *messageKey = vm_->allocateString("message");
  StringObject *messageVal = vm_->allocateString(message);
  errorObj->set(Value::object(errorKey), Value::boolean(true));
  errorObj->set(Value::object(messageKey), Value::object(messageVal));
  return Value::object(errorObj);
}

void ModuleManager::markRoots() {
  for (auto &[name, module] : modules_) {
    if (module->exportsTable) {
      vm_->gc().markObject(module->exportsTable);
    }
  }
}

} // namespace spt