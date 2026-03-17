/**
 * @file LspLogger.h
 * @brief Simple Logger for LSP Debugging
 *
 * Usage:
 *   #define LSP_DEBUG_ENABLED  // Enable before including
 *   #include "LspLogger.h"
 *
 *   LSP_LOG("Message: " << value);
 *   LSP_LOG_FUNC();  // Log function entry
 *   LSP_LOG_NODE(node);  // Log AST node info
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include <chrono>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <sstream>
#include <string>

// 默认日志文件路径 - Windows 桌面
#ifndef LSP_LOG_FILE
#define LSP_LOG_FILE "C:/Users/ftp/Desktop/lsp_debug.log"
#endif

namespace lang {
namespace lsp {
namespace debug {

/**
 * @brief Simple thread-safe logger
 */
class Logger {
public:
  static Logger &instance() {
    static Logger logger;
    return logger;
  }

  void log(const std::string &message, const char *file, int line, const char *func) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!file_.is_open()) {
      file_.open(LSP_LOG_FILE, std::ios::out | std::ios::app);
    }

    if (file_.is_open()) {
      auto now = std::chrono::system_clock::now();
      auto time = std::chrono::system_clock::to_time_t(now);
      auto ms =
          std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

      file_ << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") << "." << std::setfill('0')
            << std::setw(3) << ms.count() << "] " << "[" << extractFileName(file) << ":" << line
            << "] " << "[" << func << "] " << message << "\n";
      file_.flush();
    }
  }

  void logSeparator(const std::string &title = "") {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!file_.is_open()) {
      file_.open(LSP_LOG_FILE, std::ios::out | std::ios::app);
    }

    if (file_.is_open()) {
      file_ << "\n========== " << title << " ==========\n";
      file_.flush();
    }
  }

  ~Logger() {
    if (file_.is_open()) {
      file_.close();
    }
  }

private:
  Logger() = default;

  static std::string extractFileName(const char *path) {
    std::string p(path);
    auto pos = p.find_last_of("/\\");
    return (pos != std::string::npos) ? p.substr(pos + 1) : p;
  }

  std::ofstream file_;
  std::mutex mutex_;
};

/**
 * @brief Helper to format AST node info
 */
inline std::string formatNode(const void *node, const char *kindStr, uint32_t beginLine,
                              uint32_t beginCol, uint32_t beginOffset, uint32_t endLine,
                              uint32_t endCol, uint32_t endOffset) {
  std::ostringstream ss;
  ss << "Node{ptr=" << node << ", kind=" << kindStr << ", range=[(" << beginLine << ":" << beginCol
     << " @" << beginOffset << ")" << " - (" << endLine << ":" << endCol << " @" << endOffset
     << ")]}";
  return ss.str();
}

inline std::string formatNodeSimple(const char *kindStr, uint32_t beginOffset, uint32_t endOffset,
                                    bool isValid) {
  std::ostringstream ss;
  ss << kindStr << "[" << beginOffset << "-" << endOffset << "]" << (isValid ? "" : "(INVALID)");
  return ss.str();
}

} // namespace debug
} // namespace lsp
} // namespace lang

// ============================================================================
// Logging Macros
// ============================================================================

#ifdef LSP_DEBUG_ENABLED

#define LSP_LOG(msg)                                                                               \
  do {                                                                                             \
    std::ostringstream _ss;                                                                        \
    _ss << msg;                                                                                    \
    ::lang::lsp::debug::Logger::instance().log(_ss.str(), __FILE__, __LINE__, __FUNCTION__);       \
  } while (0)

#define LSP_LOG_FUNC() LSP_LOG(">>> ENTER")

#define LSP_LOG_SEP(title) ::lang::lsp::debug::Logger::instance().logSeparator(title)

// Log AST node - requires AstNodes.h to be included
#define LSP_LOG_NODE(node)                                                                         \
  do {                                                                                             \
    if (node) {                                                                                    \
      LSP_LOG(::lang::lsp::debug::formatNode(node, ::lang::ast::astKindToString((node)->kind),     \
                                             (node)->range.begin.line, (node)->range.begin.column, \
                                             (node)->range.begin.offset, (node)->range.end.line,   \
                                             (node)->range.end.column, (node)->range.end.offset)); \
    } else {                                                                                       \
      LSP_LOG("Node{nullptr}");                                                                    \
    }                                                                                              \
  } while (0)

// Log node with custom label
#define LSP_LOG_NODE_EX(label, node)                                                               \
  do {                                                                                             \
    if (node) {                                                                                    \
      LSP_LOG(label << ": "                                                                        \
                    << ::lang::lsp::debug::formatNodeSimple(                                       \
                           ::lang::ast::astKindToString((node)->kind), (node)->range.begin.offset, \
                           (node)->range.end.offset, (node)->range.isValid()));                    \
    } else {                                                                                       \
      LSP_LOG(label << ": nullptr");                                                               \
    }                                                                                              \
  } while (0)

// Log offset search
#define LSP_LOG_SEARCH(offset, node)                                                               \
  do {                                                                                             \
    if (node) {                                                                                    \
      LSP_LOG("Search offset=" << offset << " in " << ::lang::ast::astKindToString((node)->kind)   \
                               << "[" << (node)->range.begin.offset << "-"                         \
                               << (node)->range.end.offset << "]"                                  \
                               << " valid=" << (node)->range.isValid() << " contains="             \
                               << ((offset >= (node)->range.begin.offset &&                        \
                                    offset < (node)->range.end.offset)                             \
                                       ? "YES"                                                     \
                                       : "NO"));                                                   \
    } else {                                                                                       \
      LSP_LOG("Search offset=" << offset << " in nullptr");                                        \
    }                                                                                              \
  } while (0)

#else

// Disabled - no-op macros
#define LSP_LOG(msg) ((void)0)
#define LSP_LOG_FUNC() ((void)0)
#define LSP_LOG_SEP(title) ((void)0)
#define LSP_LOG_NODE(node) ((void)0)
#define LSP_LOG_NODE_EX(label, node) ((void)0)
#define LSP_LOG_SEARCH(offset, node) ((void)0)

#endif
