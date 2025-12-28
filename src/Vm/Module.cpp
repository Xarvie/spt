#include "Module.h"
#include "../Ast/ast.h"
#include "../Compiler/Compiler.h"
#include "VM.h"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace spt {

FileSystemLoader::FileSystemLoader(const std::vector<std::string> &searchPaths)
    : searchPaths_(searchPaths) {

  if (searchPaths_.empty()) {
    searchPaths_.push_back(".");
  }
}

std::string FileSystemLoader::resolvePath(const std::string &moduleName,
                                          const std::string &fromPath) {

  if (!fromPath.empty()) {
    namespace fs = std::filesystem;
    fs::path parent = fs::path(fromPath).parent_path();
    fs::path candidate = parent / (moduleName + ".flx");

    if (exists(candidate.string())) {
      return fs::absolute(candidate).string();
    }
  }

  for (const auto &searchPath : searchPaths_) {
    namespace fs = std::filesystem;
    std::vector<std::string> extensions = {".flx", ".spt", ".flxc"};

    for (const auto &ext : extensions) {
      fs::path candidate = fs::path(searchPath) / (moduleName + ext);
      if (exists(candidate.string())) {
        return fs::absolute(candidate).string();
      }
    }
  }

  if (exists(moduleName)) {
    namespace fs = std::filesystem;
    return fs::absolute(moduleName).string();
  }

  return "";
}

std::string FileSystemLoader::loadSource(const std::string &path) {
  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("Cannot open file: " + path);
  }

  std::stringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

bool FileSystemLoader::exists(const std::string &path) {
  namespace fs = std::filesystem;
  return fs::exists(path);
}

uint64_t FileSystemLoader::getTimestamp(const std::string &path) {
  namespace fs = std::filesystem;
  try {
    auto ftime = fs::last_write_time(path);
    return std::chrono::duration_cast<std::chrono::milliseconds>(ftime.time_since_epoch()).count();
  } catch (...) {
    return 0;
  }
}

void FileSystemLoader::addSearchPath(const std::string &path) { searchPaths_.push_back(path); }

std::string FileSystemLoader::normalizePath(const std::string &path) {
  namespace fs = std::filesystem;
  return fs::path(path).lexically_normal().string();
}

ModuleManager::ModuleManager(VM *vm, const ModuleManagerConfig &config) : vm_(vm), config_(config) {

  std::vector<std::string> defaultPaths = {".", "./lib", "./modules"};
  loader_ = std::make_unique<FileSystemLoader>(defaultPaths);
}

ModuleManager::~ModuleManager() {

  modules_.clear();
  pathToName_.clear();
}

Value ModuleManager::loadModule(const std::string &moduleName, const std::string &fromPath) {

  if (config_.enableCache) {
    auto it = modules_.find(moduleName);
    if (it != modules_.end()) {
      Module *cached = it->second;

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

  std::unordered_set<std::string> loadingStack;
  Module *module = loadModuleInternal(moduleName, fromPath, loadingStack);

  if (!module || module->state != ModuleState::LOADED) {
    std::string error = module ? module->errorMessage : "Module load failed";
    return createError(error);
  }

  return Value::object(module->exportsTable);
}

Module *ModuleManager::loadModuleInternal(const std::string &moduleName,
                                          const std::string &fromPath,
                                          std::unordered_set<std::string> &loadingStack) {

  if (loadingStack.find(moduleName) != loadingStack.end()) {
    Module *module = vm_->gc().allocate<Module>();
    setError(module, "Circular dependency detected: " + moduleName);
    return module;
  }

  loadingStack.insert(moduleName);

  std::string resolvedPath = loader_->resolvePath(moduleName, fromPath);
  if (resolvedPath.empty()) {
    Module *module = vm_->gc().allocate<Module>();
    setError(module, "Module not found: " + moduleName);
    loadingStack.erase(moduleName);
    return module;
  }

  Module *module = vm_->gc().allocate<Module>();

  vm_->protect(Value::object(module));

  module->metadata.name = moduleName;
  module->metadata.path = resolvedPath;
  module->metadata.timestamp = loader_->getTimestamp(resolvedPath);
  module->state = ModuleState::LOADING;

  std::string source;
  try {
    source = loader_->loadSource(resolvedPath);
  } catch (const std::exception &e) {
    setError(module, "Failed to load source: " + std::string(e.what()));
    loadingStack.erase(moduleName);

    vm_->unprotect(1);
    return module;
  }

  if (!compileModule(module, source)) {
    loadingStack.erase(moduleName);

    vm_->unprotect(1);
    return module;
  }

  resolveDependencies(module);

  for (const auto &dep : module->metadata.dependencies) {
    Module *depModule = loadModuleInternal(dep, resolvedPath, loadingStack);
    if (!depModule || depModule->state != ModuleState::LOADED) {
      setError(module, "Failed to load dependency: " + dep);
      loadingStack.erase(moduleName);

      vm_->unprotect(1);
      return module;
    }
  }

  if (!executeModule(module)) {
    loadingStack.erase(moduleName);

    vm_->unprotect(1);
    return module;
  }

  buildExportsTable(module);

  module->state = ModuleState::LOADED;

  if (config_.enableCache) {

    modules_[moduleName] = module;
    pathToName_[resolvedPath] = moduleName;
    loadOrder_.push_back(moduleName);

    if (modules_.size() > config_.maxCacheSize) {
      evictCache();
    }
  }

  loadingStack.erase(moduleName);

  vm_->unprotect(1);

  return module;
}

bool ModuleManager::compileModule(Module *module, const std::string &source) {
  try {

    AstNode *ast = loadAst(source, module->metadata.path);

    if (!ast) {
      setError(module, "Parse failed");
      return false;
    }

    Compiler compiler(module->metadata.name);

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

  } catch (const std::exception &e) {
    setError(module, "Compilation error: " + std::string(e.what()));
    return false;
  }
}

bool ModuleManager::executeModule(Module *module) {
  try {

    InterpretResult result = vm_->executeModule(module->chunk);

    if (result != InterpretResult::OK) {
      setError(module, "Module execution failed");
      return false;
    }

    return true;

  } catch (const std::exception &e) {
    setError(module, "Execution error: " + std::string(e.what()));
    return false;
  }
}

void ModuleManager::buildExportsTable(Module* module) {
  
  Value envValue = vm_->getLastModuleResult();

  
  vm_->protect(envValue);

  if (!envValue.isMap()) {
    module->exportsTable = vm_->allocateMap(0);
    vm_->unprotect(1);
    return;
  }

  MapObject* envMap = static_cast<MapObject*>(envValue.asGC());

  
  module->exportsTable = vm_->allocateMap(static_cast<int>(module->metadata.exports.size()));

  for (const auto& exportName : module->metadata.exports) {
    
    StringObject* key = vm_->allocateString(exportName);
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

InterpretResult VM::executeModule(const CompiledChunk &chunk) {

  Closure *mainClosure = allocateClosure(&chunk.mainProto);
  protect(Value::object(mainClosure));

  CallFrame frame;
  frame.closure = mainClosure;
  frame.ip = mainClosure->proto->code.data();

  frame.slots = stackTop_;

  ensureStack(mainClosure->proto->maxStackSize);

  int startFrameCount = frameCount_;
  frames_.push_back(frame);
  frameCount_++;

  Value *frameEnd = frame.slots + mainClosure->proto->maxStackSize;
  for (Value *slot = frame.slots; slot < frameEnd; ++slot) {
    *slot = Value::nil();
  }
  stackTop_ = frameEnd;

  return run(startFrameCount);
}

void ModuleManager::resolveDependencies(Module *module) {}

bool ModuleManager::reloadModule(const std::string &moduleName) {
  auto it = modules_.find(moduleName);
  if (it == modules_.end()) {
    return false;
  }

  Module *oldModule = it->second;

  std::unordered_set<std::string> loadingStack;
  Module *newModule = loadModuleInternal(moduleName, "", loadingStack);

  if (!newModule || newModule->state != ModuleState::LOADED) {
    return false;
  }

  oldModule->exportsTable->entries.clear();
  oldModule->exportsTable->entries = newModule->exportsTable->entries;

  oldModule->metadata = newModule->metadata;
  oldModule->chunk = newModule->chunk;

  return true;
}

void ModuleManager::preloadModule(const std::string &moduleName) { loadModule(moduleName); }

std::vector<std::string> ModuleManager::getDependencies(const std::string &moduleName,
                                                        bool recursive) {
  auto it = modules_.find(moduleName);
  if (it == modules_.end()) {
    return {};
  }

  std::vector<std::string> result = it->second->metadata.dependencies;

  if (recursive) {
    std::unordered_set<std::string> visited;
    std::function<void(const std::string &)> collectDeps;

    collectDeps = [&](const std::string &name) {
      if (visited.find(name) != visited.end())
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
  std::unordered_set<std::string> visited;
  std::unordered_set<std::string> stack;
  return detectCircular(moduleName, visited, stack);
}

bool ModuleManager::detectCircular(const std::string &moduleName,
                                   std::unordered_set<std::string> &visited,
                                   std::unordered_set<std::string> &stack) {
  if (stack.find(moduleName) != stack.end()) {
    return true;
  }

  if (visited.find(moduleName) != visited.end()) {
    return false;
  }

  visited.insert(moduleName);
  stack.insert(moduleName);

  auto it = modules_.find(moduleName);
  if (it != modules_.end()) {
    for (const auto &dep : it->second->metadata.dependencies) {
      if (detectCircular(dep, visited, stack)) {
        return true;
      }
    }
  }

  stack.erase(moduleName);
  return false;
}

void ModuleManager::clearCache(const std::string &moduleName) {
  if (moduleName.empty()) {

    modules_.clear();
    pathToName_.clear();
    loadOrder_.clear();
  } else {

    auto it = modules_.find(moduleName);
    if (it != modules_.end()) {
      pathToName_.erase(it->second->metadata.path);
      modules_.erase(it);

      auto orderIt = std::find(loadOrder_.begin(), loadOrder_.end(), moduleName);
      if (orderIt != loadOrder_.end()) {
        loadOrder_.erase(orderIt);
      }
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
    if (module->state == ModuleState::LOADED) {
      stats.loadedModules++;
    }
    stats.totalBytes += module->metadata.byteSize;
  }

  return stats;
}

void ModuleManager::setLoader(std::unique_ptr<ModuleLoader> loader) { loader_ = std::move(loader); }

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
  if (it != modules_.end()) {
    return it->second->metadata;
  }
  return ModuleMetadata();
}

void ModuleManager::evictCache() {

  if (loadOrder_.empty())
    return;

  std::string oldest = loadOrder_.front();
  clearCache(oldest);
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

} 