/**
 * @file ImportResolver.h
 * @brief Cross-file Import Resolution for LSP
 *
 * Resolves import statements by:
 * - Resolving relative import paths to absolute paths
 * - Loading target files and parsing their AST
 * - Finding exported symbols in target files
 * - Linking ImportSymbol to target Symbol
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "AstNodes.h"
#include "SemanticAnalyzer.h"
#include "Symbol.h"

#include <functional>
#include <string>
#include <unordered_set>

namespace lang {
namespace lsp {

// Forward declarations
class Workspace;
class SourceFile;

/**
 * @brief Resolves cross-file imports
 *
 * Usage:
 *   ImportResolver resolver(workspace, getSemanticModelCallback);
 *   resolver.resolve(importStmt, importSymbol, currentFilePath);
 */
class ImportResolver {
public:
  using GetSemanticModelFn = std::function<semantic::SemanticModel *(SourceFile *)>;

  ImportResolver(Workspace &workspace, GetSemanticModelFn getSemanticModel)
      : workspace_(workspace), getSemanticModel_(std::move(getSemanticModel)) {}

  /**
   * @brief Resolve an import statement and link symbols
   * @param importStmt The import statement AST node
   * @param importSymbol The import symbol to link
   * @param currentFilePath The file containing the import
   */
  void resolve(ast::ImportStmtNode *importStmt, semantic::ImportSymbol *importSymbol,
               const std::string &currentFilePath);

  /**
   * @brief Resolve import path to absolute path
   * @param modulePath The path from import statement (e.g., "export_test.spt")
   * @param fromFile The file containing the import (for relative path resolution)
   * @return Absolute path to the target file
   */
  [[nodiscard]] std::string resolveImportPath(std::string_view modulePath,
                                              const std::string &fromFile) const;

private:
  /**
   * @brief Find an exported symbol in a semantic model
   */
  semantic::Symbol *findExportedSymbol(semantic::SemanticModel *model, std::string_view name) const;

  Workspace &workspace_;
  GetSemanticModelFn getSemanticModel_;
  std::unordered_set<std::string> resolvingFiles_; ///< Prevent circular imports
};

// ============================================================================
// Implementation
// ============================================================================

inline void ImportResolver::resolve(ast::ImportStmtNode *importStmt,
                                    semantic::ImportSymbol *importSymbol,
                                    const std::string &currentFilePath) {
  if (!importStmt || !importSymbol)
    return;

  auto modulePath = importSymbol->modulePath();

  // Resolve the import path to absolute path
  std::string absolutePath = resolveImportPath(modulePath, currentFilePath);

  // Check for circular import
  if (resolvingFiles_.count(absolutePath) > 0) {
    return; // Circular import detected, skip
  }

  // Mark as resolving to prevent cycles
  resolvingFiles_.insert(absolutePath);

  // Load the target file
  SourceFile *targetFile = workspace_.openFileFromDisk(absolutePath);
  if (!targetFile) {
    resolvingFiles_.erase(absolutePath);
    return; // File not found
  }

  // Get semantic model for target file (this will parse and analyze it)
  semantic::SemanticModel *targetModel = getSemanticModel_(targetFile);
  if (!targetModel) {
    resolvingFiles_.erase(absolutePath);
    return;
  }

  // Find the exported symbol using the original name (before alias)
  std::string symbolName(importSymbol->originalName());
  semantic::Symbol *targetSymbol = findExportedSymbol(targetModel, symbolName);

  if (targetSymbol) {
    importSymbol->setTargetSymbol(targetSymbol);
  }

  resolvingFiles_.erase(absolutePath);
}

inline std::string ImportResolver::resolveImportPath(std::string_view modulePath,
                                                     const std::string &fromFile) const {
  // If already absolute path, return as-is
  if (modulePath.size() >= 2 && (modulePath[0] == '/' || modulePath[1] == ':')) {
    return std::string(modulePath);
  }

  // Get directory of the current file
  size_t lastSlash = fromFile.find_last_of("/\\");
  std::string baseDir = (lastSlash != std::string::npos) ? fromFile.substr(0, lastSlash) : ".";

  // Combine paths
  std::string result = baseDir;
  if (!result.empty() && result.back() != '/' && result.back() != '\\') {
    result += '/';
  }
  result += modulePath;

  // Normalize path (handle . and ..)
  std::vector<std::string> parts;
  std::string current;
  for (char c : result) {
    if (c == '/' || c == '\\') {
      if (!current.empty()) {
        if (current == "..") {
          if (!parts.empty())
            parts.pop_back();
        } else if (current != ".") {
          parts.push_back(current);
        }
        current.clear();
      }
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    if (current == "..") {
      if (!parts.empty())
        parts.pop_back();
    } else if (current != ".") {
      parts.push_back(current);
    }
  }

  // Rebuild path
  result.clear();
  for (const auto &part : parts) {
    if (!result.empty())
      result += '/';
    result += part;
  }

  return result;
}

inline semantic::Symbol *ImportResolver::findExportedSymbol(semantic::SemanticModel *model,
                                                            std::string_view name) const {
  if (!model)
    return nullptr;
  return model->findExportedSymbol(name);
}

} // namespace lsp
} // namespace lang
