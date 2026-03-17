/**
 * @file Workspace.h
 * @brief Multi-File Workspace Management for LSP
 *
 * Manages multiple source files in a workspace including:
 * - File lifecycle (open, close, save)
 * - URI/path mapping
 * - Cross-file operations (find references, rename)
 * - Workspace-wide settings
 *
 * Key Features:
 * - Efficient file lookup by URI or path
 * - Lazy loading of files
 * - Change notification support
 * - Workspace-wide diagnostics collection
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "SourceFile.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lang {
namespace lsp {

/**
 * @brief Workspace configuration
 */
struct WorkspaceConfig {
  std::string rootPath;                     ///< Workspace root directory
  std::vector<std::string> includePaths;    ///< Additional include paths
  std::vector<std::string> excludePatterns; ///< Patterns to exclude

  // LSP capabilities
  bool supportsDiagnostics = true;
  bool supportsSemanticTokens = true;
  bool supportsCodeActions = true;
  bool supportsFormatting = true;

  // Parser settings
  bool tolerantParsing = true; ///< Allow recovery from errors
  int maxDiagnosticsPerFile = 100;
};

/**
 * @brief Workspace event types
 */
enum class WorkspaceEvent : uint8_t {
  FileOpened,
  FileClosed,
  FileChanged,
  FileSaved,
  FileDeleted,
  DiagnosticsUpdated
};

/**
 * @brief Workspace event data
 */
struct WorkspaceEventData {
  WorkspaceEvent event;
  std::string uri;
  int64_t version = 0;
};

/**
 * @brief Callback type for workspace events
 */
using WorkspaceEventCallback = std::function<void(const WorkspaceEventData &)>;

/**
 * @brief Manages multiple source files in a workspace
 *
 * Usage:
 *   // Create workspace
 *   Workspace workspace("/path/to/project");
 *
 *   // Open a file
 *   auto& file = workspace.openFile("file:///path/to/file.lang", content, 1);
 *
 *   // Apply changes
 *   workspace.applyChange("file:///path/to/file.lang", range, newText);
 *
 *   // Get diagnostics for all files
 *   auto allDiagnostics = workspace.collectDiagnostics();
 *
 *   // Find all .lang files in workspace
 *   auto langFiles = workspace.findFiles("*.lang");
 */
class Workspace {
public:
  /**
   * @brief Create an empty workspace
   */
  Workspace() = default;

  /**
   * @brief Create a workspace with root path
   */
  explicit Workspace(std::string rootPath) { config_.rootPath = std::move(rootPath); }

  /**
   * @brief Create a workspace with configuration
   */
  explicit Workspace(WorkspaceConfig config) : config_(std::move(config)) {}

  // Non-copyable (owns file resources)
  Workspace(const Workspace &) = delete;
  Workspace &operator=(const Workspace &) = delete;

  // Movable
  Workspace(Workspace &&) noexcept = default;
  Workspace &operator=(Workspace &&) noexcept = default;

  // ========================================================================
  // Configuration
  // ========================================================================

  [[nodiscard]] const WorkspaceConfig &config() const noexcept { return config_; }

  [[nodiscard]] WorkspaceConfig &config() noexcept { return config_; }

  [[nodiscard]] const std::string &rootPath() const noexcept { return config_.rootPath; }

  void setRootPath(std::string path) { config_.rootPath = std::move(path); }

  // ========================================================================
  // File Management
  // ========================================================================

  /**
   * @brief Open a file in the workspace
   * @param uri File URI (e.g., "file:///path/to/file.lang")
   * @param content Initial content
   * @param version Version number from LSP
   * @return Reference to the source file
   */
  SourceFile &openFile(std::string_view uri, std::string content, int64_t version = 0) {
    std::string uriStr(uri);
    std::string path = uri::uriToPath(uri);

    auto it = filesByUri_.find(uriStr);
    if (it != filesByUri_.end()) {
      // File already open - update content
      it->second->setContent(std::move(content));
      return *it->second;
    }

    // Create new file
    auto file = std::make_unique<SourceFile>(path, std::move(content));
    auto *filePtr = file.get();

    filesByUri_[uriStr] = std::move(file);
    filesByPath_[path] = filePtr;

    notifyEvent(WorkspaceEvent::FileOpened, uriStr, version);
    return *filePtr;
  }

  /**
   * @brief Open a file from disk
   * @param path File path
   * @return Reference to the source file, or nullptr if failed
   */
  SourceFile *openFileFromDisk(std::string_view path) {
    std::string pathStr(path);
    std::string uriStr = uri::pathToUri(path);

    // Check if already open
    auto it = filesByUri_.find(uriStr);
    if (it != filesByUri_.end()) {
      return it->second.get();
    }

    // Create and load
    auto file = std::make_unique<SourceFile>(pathStr);
    if (!file->loadFromDisk()) {
      return nullptr;
    }

    auto *filePtr = file.get();
    filesByUri_[uriStr] = std::move(file);
    filesByPath_[pathStr] = filePtr;

    notifyEvent(WorkspaceEvent::FileOpened, uriStr, 0);
    return filePtr;
  }

  /**
   * @brief Close a file
   * @param uri File URI
   */
  void closeFile(std::string_view uri) {
    std::string uriStr(uri);

    auto it = filesByUri_.find(uriStr);
    if (it != filesByUri_.end()) {
      std::string path = it->second->path();
      filesByPath_.erase(path);
      filesByUri_.erase(it);

      notifyEvent(WorkspaceEvent::FileClosed, uriStr, 0);
    }
  }

  /**
   * @brief Get a file by URI
   * @return Pointer to file, or nullptr if not found
   */
  [[nodiscard]] SourceFile *getFile(std::string_view uri) const {
    auto it = filesByUri_.find(std::string(uri));
    return it != filesByUri_.end() ? it->second.get() : nullptr;
  }

  /**
   * @brief Get a file by path
   */
  [[nodiscard]] SourceFile *getFileByPath(std::string_view path) const {
    auto it = filesByPath_.find(std::string(path));
    return it != filesByPath_.end() ? it->second : nullptr;
  }

  /**
   * @brief Check if a file is open (C++17 兼容版本)
   */
  [[nodiscard]] bool isFileOpen(std::string_view uri) const {
    return filesByUri_.find(std::string(uri)) != filesByUri_.end();
  }

  /**
   * @brief Get all open file URIs
   */
  [[nodiscard]] std::vector<std::string> getOpenFileUris() const {
    std::vector<std::string> uris;
    uris.reserve(filesByUri_.size());
    for (const auto &[uri, _] : filesByUri_) {
      uris.push_back(uri);
    }
    return uris;
  }

  /**
   * @brief Get number of open files
   */
  [[nodiscard]] size_t openFileCount() const noexcept { return filesByUri_.size(); }

  // ========================================================================
  // Content Modification
  // ========================================================================

  /**
   * @brief Apply a full content change
   * @param uri File URI
   * @param content New content
   * @param version New version number
   * @return true if successful
   */
  bool applyFullChange(std::string_view uri, std::string content, int64_t version) {
    auto *file = getFile(uri);
    if (!file) {
      return false;
    }

    file->setContent(std::move(content));
    notifyEvent(WorkspaceEvent::FileChanged, std::string(uri), version);
    return true;
  }

  /**
   * @brief Apply an incremental change
   * @param uri File URI
   * @param range Range to replace
   * @param text Replacement text
   * @param version New version number
   * @return true if successful
   */
  bool applyIncrementalChange(std::string_view uri, Range range, std::string_view text,
                              int64_t version) {
    auto *file = getFile(uri);
    if (!file) {
      return false;
    }

    file->applyEdit(range, text);
    notifyEvent(WorkspaceEvent::FileChanged, std::string(uri), version);
    return true;
  }

  /**
   * @brief Save a file to disk
   * @param uri File URI
   * @return true if successful
   */
  bool saveFile(std::string_view uri) {
    auto *file = getFile(uri);
    if (!file) {
      return false;
    }

    if (file->saveToDisk()) {
      notifyEvent(WorkspaceEvent::FileSaved, std::string(uri), file->version());
      return true;
    }
    return false;
  }

  // ========================================================================
  // Diagnostics
  // ========================================================================

  /**
   * @brief Collect diagnostics for a single file
   * @param uri File URI
   * @return Diagnostics for the file
   */
  [[nodiscard]] std::vector<Diagnostic> getDiagnostics(std::string_view uri) const {
    auto *file = getFile(uri);
    if (!file) {
      return {};
    }
    return file->getDiagnostics();
  }

  /**
   * @brief Collect diagnostics for all open files
   * @return Map of URI to diagnostics
   */
  [[nodiscard]] std::unordered_map<std::string, std::vector<Diagnostic>>
  collectAllDiagnostics() const {
    std::unordered_map<std::string, std::vector<Diagnostic>> result;

    for (const auto &[uri, file] : filesByUri_) {
      const auto &diags = file->getDiagnostics();
      if (!diags.empty()) {
        result[uri] = diags;
      }
    }

    return result;
  }

  /**
   * @brief Reparse all open files and collect diagnostics
   */
  void updateAllDiagnostics() {
    for (auto &[uri, file] : filesByUri_) {
      file->reparse();
      notifyEvent(WorkspaceEvent::DiagnosticsUpdated, uri, file->version());
    }
  }

  // ========================================================================
  // File Discovery
  // ========================================================================

  /**
   * @brief Find files matching a pattern in the workspace
   * @param pattern Glob pattern (e.g., "*.lang")
   * @return List of file paths
   */
  [[nodiscard]] std::vector<std::string> findFiles(std::string_view pattern) const {
    std::vector<std::string> result;

    if (config_.rootPath.empty()) {
      return result;
    }

    try {
      std::filesystem::path root(config_.rootPath);

      for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) {
          continue;
        }

        std::string filename = entry.path().filename().string();

        // Simple glob matching (just handles *.ext pattern)
        // C++17 兼容版本
        if (detail::starts_with(pattern, "*.")) {
          std::string_view ext = pattern.substr(1); // ".lang"
          if (filename.size() >= ext.size() &&
              filename.substr(filename.size() - ext.size()) == ext) {
            result.push_back(entry.path().string());
          }
        } else if (filename == pattern) {
          result.push_back(entry.path().string());
        }
      }
    } catch (const std::filesystem::filesystem_error &) {
      // Ignore filesystem errors
    }

    return result;
  }

  /**
   * @brief Resolve a module path to a file path
   * @param modulePath Module path (e.g., "utils/helpers")
   * @param fromFile File making the import (for relative resolution)
   * @return Resolved file path, or empty if not found
   */
  [[nodiscard]] std::string resolveModulePath(std::string_view modulePath,
                                              std::string_view fromFile = "") const {
    // Try relative to importing file first
    if (!fromFile.empty()) {
      std::filesystem::path basePath(uri::uriToPath(fromFile));
      basePath = basePath.parent_path();

      std::filesystem::path resolved = basePath / modulePath;
      resolved.replace_extension(".lang");

      if (std::filesystem::exists(resolved)) {
        return resolved.string();
      }
    }

    // Try workspace root
    if (!config_.rootPath.empty()) {
      std::filesystem::path resolved = std::filesystem::path(config_.rootPath) / modulePath;
      resolved.replace_extension(".lang");

      if (std::filesystem::exists(resolved)) {
        return resolved.string();
      }
    }

    // Try include paths
    for (const auto &includePath : config_.includePaths) {
      std::filesystem::path resolved = std::filesystem::path(includePath) / modulePath;
      resolved.replace_extension(".lang");

      if (std::filesystem::exists(resolved)) {
        return resolved.string();
      }
    }

    return {};
  }

  // ========================================================================
  // Event Handling
  // ========================================================================

  /**
   * @brief Register an event callback
   * @param callback Function to call on events
   * @return Callback ID for removal
   */
  size_t addEventCallback(WorkspaceEventCallback callback) {
    size_t id = nextCallbackId_++;
    eventCallbacks_[id] = std::move(callback);
    return id;
  }

  /**
   * @brief Remove an event callback
   * @param id Callback ID from addEventCallback
   */
  void removeEventCallback(size_t id) { eventCallbacks_.erase(id); }

  // ========================================================================
  // Iteration
  // ========================================================================

  /**
   * @brief Iterate over all open files
   */
  template <typename Func> void forEachFile(Func &&func) const {
    for (const auto &[uri, file] : filesByUri_) {
      func(uri, *file);
    }
  }

  /**
   * @brief Iterate over all open files (mutable)
   */
  template <typename Func> void forEachFile(Func &&func) {
    for (auto &[uri, file] : filesByUri_) {
      func(uri, *file);
    }
  }

private:
  void notifyEvent(WorkspaceEvent event, const std::string &uri, int64_t version) {
    WorkspaceEventData data{event, uri, version};
    for (const auto &[_, callback] : eventCallbacks_) {
      callback(data);
    }
  }

  // Configuration
  WorkspaceConfig config_;

  // File storage
  std::unordered_map<std::string, std::unique_ptr<SourceFile>> filesByUri_;
  std::unordered_map<std::string, SourceFile *> filesByPath_;

  // Event callbacks
  std::unordered_map<size_t, WorkspaceEventCallback> eventCallbacks_;
  size_t nextCallbackId_ = 0;
};

} // namespace lsp
} // namespace lang
