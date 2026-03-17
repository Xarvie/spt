/**
 * @file SourceFile.h
 * @brief Single Source File Management for LSP
 *
 * Manages a single source file including:
 * - Source text storage and versioning
 * - Line offset table for position conversion
 * - AST ownership and parsing
 * - Diagnostic collection
 *
 * Key Features:
 * - Efficient incremental updates
 * - Version tracking for LSP synchronization
 * - Lazy parsing on demand
 * - Thread-safe read access (with external synchronization for writes)
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

// 启用调试日志 - 调试完成后注释掉这行
#define LSP_DEBUG_ENABLED
#include "LspLogger.h"

#include "AstFactory.h"
#include "AstNodes.h"
#include "LangLexer.h"
#include "LangParser.h"
#include "LineOffsetTable.h"
#include "TolerantAstBuilder.h"
#include "antlr4-runtime.h"

#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace lang {
namespace lsp {

// Forward declarations
class Workspace;

// ============================================================================
// C++17 兼容性辅助函数
// ============================================================================

namespace detail {

/**
 * @brief C++17 兼容的 starts_with 实现
 */
inline bool starts_with(std::string_view str, std::string_view prefix) noexcept {
  return str.size() >= prefix.size() && str.compare(0, prefix.size(), prefix) == 0;
}

inline bool starts_with(const std::string &str, std::string_view prefix) noexcept {
  return str.size() >= prefix.size() &&
         str.compare(0, prefix.size(), prefix.data(), prefix.size()) == 0;
}

/**
 * @brief C++17 兼容的 ends_with 实现
 */
inline bool ends_with(std::string_view str, std::string_view suffix) noexcept {
  return str.size() >= suffix.size() &&
         str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace detail

/**
 * @brief Diagnostic severity levels (matching LSP)
 */
enum class DiagnosticSeverity : uint8_t { Error = 1, Warning = 2, Information = 3, Hint = 4 };

/**
 * @brief A single diagnostic message
 */
struct Diagnostic {
  Range range;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string code;   ///< Optional error code
  std::string source; ///< Source identifier (e.g., "lang-parser")
  std::string message;

  // Related information (for multi-location diagnostics)
  struct RelatedInfo {
    std::string uri;
    Range range;
    std::string message;
  };

  std::vector<RelatedInfo> relatedInfo;
};

/**
 * @brief File state enumeration
 */
enum class FileState : uint8_t {
  Clean,    ///< No changes since last save
  Modified, ///< Has unsaved changes
  Error,    ///< Failed to load/parse
  Closed    ///< File was closed (in workspace)
};

/**
 * @brief Source file URI utilities
 */
namespace uri {
/**
 * @brief Convert file path to URI
 */
[[nodiscard]] inline std::string pathToUri(std::string_view path) {
  std::string result = "file://";

  // Handle Windows paths
  if (path.size() >= 2 && path[1] == ':') {
    result += '/';
  }

  for (char c : path) {
    if (c == '\\') {
      result += '/';
    } else if (c == ' ') {
      result += "%20";
    } else {
      result += c;
    }
  }

  return result;
}

/**
 * @brief Convert URI to file path
 */
[[nodiscard]] inline std::string uriToPath(std::string_view uri) {
  std::string_view path = uri;

  // Strip file:// prefix (C++17 兼容)
  if (detail::starts_with(path, "file://")) {
    path = path.substr(7);
  }

  // Handle Windows paths (file:///C:/...)
  if (path.size() >= 3 && path[0] == '/' && path[2] == ':') {
    path = path.substr(1); // Remove leading /
  }

  std::string result;
  result.reserve(path.size());

  for (size_t i = 0; i < path.size(); ++i) {
    if (path[i] == '%' && i + 2 < path.size()) {
      // Decode percent-encoded characters
      char hex[3] = {path[i + 1], path[i + 2], '\0'};
      char decoded = static_cast<char>(std::strtol(hex, nullptr, 16));
      result += decoded;
      i += 2;
    } else {
      result += path[i];
    }
  }

  return result;
}

/**
 * @brief Extract filename from path/URI
 */
[[nodiscard]] inline std::string_view getFilename(std::string_view path) {
  size_t lastSlash = path.find_last_of("/\\");
  if (lastSlash == std::string_view::npos) {
    return path;
  }
  return path.substr(lastSlash + 1);
}

/**
 * @brief Get file extension
 */
[[nodiscard]] inline std::string_view getExtension(std::string_view path) {
  auto filename = getFilename(path);
  size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos || dot == 0) {
    return {};
  }
  return filename.substr(dot);
}
} // namespace uri

/**
 * @brief Manages a single source file for LSP operations
 *
 * Usage:
 *   // Create from file path
 *   SourceFile file("path/to/source.lang");
 *   file.loadFromDisk();
 *
 *   // Or create from content (for unsaved buffers)
 *   SourceFile file("untitled.lang", sourceContent);
 *
 *   // Apply edits
 *   file.applyEdit(range, newText);
 *
 *   // Get AST (parses if needed)
 *   auto* ast = file.getAst();
 *
 *   // Get diagnostics
 *   auto diagnostics = file.getDiagnostics();
 */
class SourceFile {
public:
  /**
   * @brief Create an empty source file
   */
  SourceFile() = default;

  /**
   * @brief Create a source file with path
   */
  explicit SourceFile(std::string path) : path_(std::move(path)), uri_(uri::pathToUri(path_)) {}

  /**
   * @brief Create a source file with path and initial content
   */
  SourceFile(std::string path, std::string content)
      : path_(std::move(path)), uri_(uri::pathToUri(path_)), content_(std::move(content)) {
    lineTable_.build(content_);
  }

  // Non-copyable (owns AST memory)
  SourceFile(const SourceFile &) = delete;
  SourceFile &operator=(const SourceFile &) = delete;

  // Movable
  SourceFile(SourceFile &&) noexcept = default;
  SourceFile &operator=(SourceFile &&) noexcept = default;

  // ========================================================================
  // File Identity & Metadata
  // ========================================================================

  [[nodiscard]] const std::string &path() const noexcept { return path_; }

  [[nodiscard]] const std::string &uri() const noexcept { return uri_; }

  [[nodiscard]] std::string_view filename() const noexcept { return uri::getFilename(path_); }

  [[nodiscard]] FileState state() const noexcept { return state_; }

  [[nodiscard]] int64_t version() const noexcept { return version_; }

  void setPath(std::string newPath) {
    path_ = std::move(newPath);
    uri_ = uri::pathToUri(path_);
  }

  // ========================================================================
  // Content Access
  // ========================================================================

  /**
   * @brief Get the source content
   */
  [[nodiscard]] const std::string &content() const noexcept { return content_; }

  [[nodiscard]] std::string_view contentView() const noexcept { return content_; }

  /**
   * @brief Get content length
   */
  [[nodiscard]] size_t contentLength() const noexcept { return content_.size(); }

  /**
   * @brief Get a specific line's text
   */
  [[nodiscard]] std::string_view getLine(uint32_t line) const {
    return lineTable_.getLineText(content_, line);
  }

  /**
   * @brief Get line count
   */
  [[nodiscard]] uint32_t lineCount() const noexcept { return lineTable_.lineCount(); }

  // ========================================================================
  // Position Conversion
  // ========================================================================

  /**
   * @brief Get the line offset table
   */
  [[nodiscard]] const LineOffsetTable &lineTable() const noexcept { return lineTable_; }

  /**
   * @brief Convert position to byte offset
   */
  [[nodiscard]] uint32_t getOffset(Position pos) const noexcept {
    return lineTable_.getOffset(pos);
  }

  /**
   * @brief Convert byte offset to position
   */
  [[nodiscard]] Position getPosition(uint32_t offset) const noexcept {
    return lineTable_.getPosition(offset);
  }

  /**
   * @brief Convert AST SourceLoc to LSP Position
   */
  [[nodiscard]] Position toPosition(ast::SourceLoc loc) const noexcept {
    if (!loc.isValid()) {
      return Position::invalid();
    }
    // AST uses 1-based line/column, so just copy
    return Position{loc.line, loc.column};
  }

  /**
   * @brief Convert AST SourceRange to LSP Range
   */
  [[nodiscard]] Range toRange(ast::SourceRange range) const noexcept {
    return Range{toPosition(range.begin), toPosition(range.end)};
  }

  /**
   * @brief Convert LSP Position to AST SourceLoc
   */
  [[nodiscard]] ast::SourceLoc toSourceLoc(Position pos) const noexcept {
    if (!pos.isValid()) {
      return ast::SourceLoc::invalid();
    }
    return ast::SourceLoc{pos.line, pos.column, getOffset(pos)};
  }

  // ========================================================================
  // Content Modification
  // ========================================================================

  /**
   * @brief Set entire content (full sync)
   */
  void setContent(std::string newContent) {
    content_ = std::move(newContent);
    lineTable_.build(content_);
    invalidateAst();
    ++version_;
    state_ = FileState::Modified;
  }

  /**
   * @brief Apply an incremental edit
   * @param range The range to replace (1-based positions)
   * @param newText The replacement text
   */
  void applyEdit(Range range, std::string_view newText) {
    uint32_t startOffset = getOffset(range.start);
    uint32_t endOffset = getOffset(range.end);

    // Clamp to valid range
    startOffset = std::min(startOffset, static_cast<uint32_t>(content_.size()));
    endOffset = std::min(endOffset, static_cast<uint32_t>(content_.size()));

    if (startOffset > endOffset) {
      std::swap(startOffset, endOffset);
    }

    // Apply the edit
    content_.replace(startOffset, endOffset - startOffset, newText);

    // Update line table
    lineTable_.applyEdit(startOffset, endOffset, newText, content_);

    invalidateAst();
    ++version_;
    state_ = FileState::Modified;
  }

  /**
   * @brief Apply edit by byte offsets
   */
  void applyEditByOffset(uint32_t startOffset, uint32_t endOffset, std::string_view newText) {
    startOffset = std::min(startOffset, static_cast<uint32_t>(content_.size()));
    endOffset = std::min(endOffset, static_cast<uint32_t>(content_.size()));

    content_.replace(startOffset, endOffset - startOffset, newText);
    lineTable_.applyEdit(startOffset, endOffset, newText, content_);

    invalidateAst();
    ++version_;
    state_ = FileState::Modified;
  }

  // ========================================================================
  // File I/O
  // ========================================================================

  /**
   * @brief Load content from disk
   * @return true if successful
   */
  bool loadFromDisk();

  /**
   * @brief Save content to disk
   * @return true if successful
   */
  bool saveToDisk();

  /**
   * @brief Mark as saved (for external save operations)
   */
  void markSaved() { state_ = FileState::Clean; }

  // ========================================================================
  // AST Access
  // ========================================================================

  /**
   * @brief Get the AST (parses if needed)
   * @return The compilation unit, or nullptr if parsing failed
   */
  ast::CompilationUnitNode *getAst();

  /**
   * @brief Force a re-parse of the file
   */
  void reparse();

  /**
   * @brief Check if AST is up to date
   */
  [[nodiscard]] bool isAstValid() const noexcept { return astValid_; }

  /**
   * @brief Invalidate the AST (forces reparse on next access)
   */
  void invalidateAst() noexcept { astValid_ = false; }

  /**
   * @brief Get the AST factory (for node creation/queries)
   */
  [[nodiscard]] ast::AstFactory &factory() noexcept { return factory_; }

  [[nodiscard]] const ast::AstFactory &factory() const noexcept { return factory_; }

  // ========================================================================
  // Diagnostics
  // ========================================================================

  /**
   * @brief Get all diagnostics for this file
   */
  [[nodiscard]] const std::vector<Diagnostic> &getDiagnostics() const noexcept {
    return diagnostics_;
  }

  /**
   * @brief Add a diagnostic
   */
  void addDiagnostic(Diagnostic diag) { diagnostics_.push_back(std::move(diag)); }

  /**
   * @brief Clear all diagnostics
   */
  void clearDiagnostics() { diagnostics_.clear(); }

  /**
   * @brief Check if file has errors
   */
  [[nodiscard]] bool hasErrors() const noexcept {
    for (const auto &d : diagnostics_) {
      if (d.severity == DiagnosticSeverity::Error) {
        return true;
      }
    }
    return false;
  }

private:
  // File identity
  std::string path_;
  std::string uri_;

  // Content
  std::string content_;
  LineOffsetTable lineTable_;

  // State
  FileState state_ = FileState::Clean;
  int64_t version_ = 0;

  // AST
  ast::AstFactory factory_;
  ast::CompilationUnitNode *ast_ = nullptr;
  bool astValid_ = false;

  // Diagnostics
  std::vector<Diagnostic> diagnostics_;
};

// ============================================================================
// Implementation (inline for header-only)
// ============================================================================

inline bool SourceFile::loadFromDisk() {
  FILE *file = std::fopen(path_.c_str(), "rb");
  if (!file) {
    state_ = FileState::Error;
    return false;
  }

  // Get file size
  std::fseek(file, 0, SEEK_END);
  long size = std::ftell(file);
  std::fseek(file, 0, SEEK_SET);

  if (size < 0) {
    std::fclose(file);
    state_ = FileState::Error;
    return false;
  }

  // Read content
  content_.resize(static_cast<size_t>(size));
  size_t bytesRead = std::fread(content_.data(), 1, content_.size(), file);
  std::fclose(file);

  if (bytesRead != content_.size()) {
    content_.resize(bytesRead);
  }

  lineTable_.build(content_);
  invalidateAst();
  state_ = FileState::Clean;
  ++version_;

  return true;
}

inline bool SourceFile::saveToDisk() {
  FILE *file = std::fopen(path_.c_str(), "wb");
  if (!file) {
    return false;
  }

  size_t written = std::fwrite(content_.data(), 1, content_.size(), file);
  std::fclose(file);

  if (written == content_.size()) {
    state_ = FileState::Clean;
    return true;
  }

  return false;
}

inline ast::CompilationUnitNode *SourceFile::getAst() {
  LSP_LOG("SourceFile::getAst() - astValid_=" << astValid_ << ", ast_=" << (void *)ast_);
  if (!astValid_) {
    LSP_LOG("  -> AST invalid, calling reparse()");
    reparse();
  }
  LSP_LOG("  -> returning ast_=" << (void *)ast_);
  if (ast_) {
    LSP_LOG("     ast_->statements.size()=" << ast_->statements.size() << ", range=["
                                            << ast_->range.begin.offset << "-"
                                            << ast_->range.end.offset << "]");
  }
  return ast_;
}

inline void SourceFile::reparse() {
  LSP_LOG_SEP("SourceFile::reparse()");
  LSP_LOG("this=" << (void *)this << ", path=" << path_);

  // 1. 清理旧状态
  clearDiagnostics();
  // 重置 factory (内存池)，释放之前的 AST 节点内存
  LSP_LOG("Resetting factory, old ast_=" << (void *)ast_);
  factory_ = ast::AstFactory();
  ast_ = nullptr; // 重要：重置 ast_ 指针
  astValid_ = false;

  try {
    // 2. 准备 ANTLR 输入流
    antlr4::ANTLRInputStream input(content_);

    // 3. 词法分析
    LangLexer lexer(&input);
    // 移除默认的控制台报错监听器
    lexer.removeErrorListeners();

    antlr4::CommonTokenStream tokens(&lexer);

    // 4. 语法分析
    LangParser parser(&tokens);
    parser.removeErrorListeners();

    // [可选] 添加错误监听器，把语法错误转换成 LSP Diagnostics
    // 这里简单写一个 lambda 作为示例，或者你可以写一个类
    // 如果这里不加，VS Code 里就没有红色波浪线报错
    struct LspErrorListener : public antlr4::BaseErrorListener {
      SourceFile *file;

      LspErrorListener(SourceFile *f) : file(f) {}

      void syntaxError(antlr4::Recognizer *recognizer, antlr4::Token *offendingSymbol, size_t line,
                       size_t charPositionInLine, const std::string &msg,
                       std::exception_ptr e) override {
        Diagnostic d;
        // 注意：ANTLR 也是 1-based 行号，0-based 列号，需要对应你的 Range 定义
        // 这里假设你的 Range 需要 1-based
        Position start{static_cast<uint32_t>(line), static_cast<uint32_t>(charPositionInLine + 1)};
        Position end = start;
        if (offendingSymbol) {
          end.column += offendingSymbol->getText().length();
        }

        d.range = Range{start, end};
        d.severity = DiagnosticSeverity::Error;
        d.message = msg;
        d.source = "lang-parser";
        file->addDiagnostic(d);
      }
    };

    LspErrorListener errorListener(this);
    parser.addErrorListener(&errorListener);

    // 5. 执行解析 (生成 CST)
    // 注意：compilationUnit 必须是你 .g4 文件里的根规则名！
    auto *tree = parser.compilationUnit();

    // 6. AST 转换 (CST -> AST)
    // 实例化你的宽容构建器
    ast::TolerantAstBuilder builder(factory_, filename());

    // 执行转换
    ast_ = builder.build(tree);
    LSP_LOG("After build: ast_=" << (void *)ast_);
    if (ast_) {
      LSP_LOG("  ast_->statements.size()=" << ast_->statements.size() << ", range=["
                                           << ast_->range.begin.offset << "-"
                                           << ast_->range.end.offset << "]");
    }

    // 标记 AST 有效
    astValid_ = true;
    LSP_LOG("reparse() complete: astValid_=true, ast_=" << (void *)ast_);

  } catch (const std::exception &e) {
    // 防止解析崩溃导致服务退出
    // 发生严重错误时，创建一个空的 AST 兜底
    LSP_LOG("reparse() exception: " << e.what());
    ast_ = factory_.makeCompilationUnit(ast::SourceRange::invalid(), filename(), {});
    Diagnostic d;
    d.message = std::string("Parser crashed: ") + e.what();
    addDiagnostic(d);
  } catch (...) {
    LSP_LOG("reparse() unknown exception");
    ast_ = factory_.makeCompilationUnit(ast::SourceRange::invalid(), filename(), {});
  }
}

} // namespace lsp
} // namespace lang
