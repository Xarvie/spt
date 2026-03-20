/**
 * @file main.cpp
 * @brief Lang Language Server Main Entry Point
 *
 * This file implements the JSON-RPC transport layer for the LSP protocol,
 * connecting stdio communication with the LspService backend.
 *
 * Features:
 * - JSON-RPC 2.0 message parsing and serialization
 * - Stdio-based transport (standard LSP)
 * - Request/Response/Notification handling
 * - Graceful shutdown
 *
 * @copyright Copyright (c) 2024-2025
 */

#include "LspService.h"

#include <nlohmann/json.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <variant>
#include <vector>

using json = nlohmann::json;

namespace lang {
namespace lsp {

// ============================================================================
// JSON-RPC Trace Logger
// ============================================================================

class JsonRpcLogger {
public:
    static JsonRpcLogger& instance() {
        static JsonRpcLogger logger;
        return logger;
    }
    
    void enable(const std::string& path = "lsp_trace.log") {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!logFile_.is_open()) {
            logFile_.open(path, std::ios::out | std::ios::app);
            enabled_ = true;
            logInternal("[JsonRpcLogger] Enabled, logging to: " + path);
        }
    }
    
    void disable() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (logFile_.is_open()) {
            logInternal("[JsonRpcLogger] Disabled");
            logFile_.close();
        }
        enabled_ = false;
    }
    
    bool isEnabled() const { return enabled_; }
    
    void logRecv(const std::string& content) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        logTimestamp();
        logFile_ << "[RECV] " << content << std::endl;
        logFile_.flush();
    }
    
    void logSend(const std::string& content) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        logTimestamp();
        logFile_ << "[SEND] " << content << std::endl;
        logFile_.flush();
    }
    
    void log(const std::string& message) {
        if (!enabled_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        logTimestamp();
        logFile_ << message << std::endl;
        logFile_.flush();
    }
    
private:
    JsonRpcLogger() = default;
    
    void logInternal(const std::string& message) {
        logTimestamp();
        logFile_ << message << std::endl;
    }
    
    void logTimestamp() {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;
        
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", std::localtime(&time));
        logFile_ << "[" << buf << "." << std::setfill('0') << std::setw(3) << ms.count() << "] ";
    }
    
    std::ofstream logFile_;
    std::mutex mutex_;
    bool enabled_ = false;
};

#define JSONRPC_LOG_RECV(content) JsonRpcLogger::instance().logRecv(content)
#define JSONRPC_LOG_SEND(content) JsonRpcLogger::instance().logSend(content)
#define JSONRPC_LOG(msg) JsonRpcLogger::instance().log(msg)

// ============================================================================
// JSON-RPC Types
// ============================================================================

/// JSON-RPC request ID (can be string, int, or null)
using JsonRpcId = std::variant<std::nullptr_t, int, std::string>;

/// JSON-RPC error codes
namespace JsonRpcErrorCode {
constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;
constexpr int ServerNotInitialized = -32002;
constexpr int RequestCancelled = -32800;
} // namespace JsonRpcErrorCode

// ============================================================================
// JSON Serialization for LSP Types
// ============================================================================

// Position
inline void to_json(json &j, const Position &p) {
  j = json{{"line", p.line - 1}, {"character", p.column - 1}}; // Convert to 0-based
}

inline void from_json(const json &j, Position &p) {
  p.line = j.at("line").get<uint32_t>() + 1; // Convert from 0-based
  p.column = j.at("character").get<uint32_t>() + 1;
}

// Range
inline void to_json(json &j, const Range &r) { j = json{{"start", r.start}, {"end", r.end}}; }

inline void from_json(const json &j, Range &r) {
  j.at("start").get_to(r.start);
  j.at("end").get_to(r.end);
}

// Location
inline void to_json(json &j, const Location &l) { j = json{{"uri", l.uri}, {"range", l.range}}; }

// LocationLink
inline void to_json(json &j, const LocationLink &l) {
  j = json{{"targetUri", l.targetUri},
           {"targetRange", l.targetRange},
           {"targetSelectionRange", l.targetSelectionRange}};
  if (l.originSelectionRange) {
    j["originSelectionRange"] = *l.originSelectionRange;
  }
}

// Diagnostic
inline void to_json(json &j, const Diagnostic &d) {
  j = json{{"range", d.range}, {"severity", static_cast<int>(d.severity)}, {"message", d.message}};
  if (!d.code.empty()) {
    j["code"] = d.code;
  }
  if (!d.source.empty()) {
    j["source"] = d.source;
  }
}

// HoverResult -> Hover
inline void to_json(json &j, const HoverResult &h) {
  j = json{{"contents", {{"kind", "markdown"}, {"value", h.contents}}}};
  if (h.range) {
    j["range"] = *h.range;
  }
}

// CompletionItem
inline void to_json(json &j, const CompletionItem &c) {
  j = json{{"label", c.label}, {"kind", static_cast<int>(c.kind)}};
  if (!c.detail.empty()) {
    j["detail"] = c.detail;
  }
  if (!c.documentation.empty()) {
    j["documentation"] = {{"kind", "markdown"}, {"value", c.documentation}};
  }
  if (!c.insertText.empty()) {
    j["insertText"] = c.insertText;
    j["insertTextFormat"] = static_cast<int>(c.insertTextFormat);
  }
  if (!c.filterText.empty()) {
    j["filterText"] = c.filterText;
  }
  if (!c.sortText.empty()) {
    j["sortText"] = c.sortText;
  }
  if (c.deprecated) {
    j["deprecated"] = true;
  }
}

// CompletionResult -> CompletionList
inline void to_json(json &j, const CompletionResult &r) {
  j = json{{"isIncomplete", r.isIncomplete}, {"items", r.items}};
}

// SignatureHelp
inline void to_json(json &j, const ParameterInformation &p) {
  j = json{{"label", p.label}};
  if (!p.documentation.empty()) {
    j["documentation"] = p.documentation;
  }
}

inline void to_json(json &j, const SignatureInformation &s) {
  j = json{{"label", s.label}, {"parameters", s.parameters}};
  if (!s.documentation.empty()) {
    j["documentation"] = s.documentation;
  }
}

inline void to_json(json &j, const SignatureHelp &s) {
  j = json{{"signatures", s.signatures},
           {"activeSignature", s.activeSignature},
           {"activeParameter", s.activeParameter}};
}

// DocumentSymbol
inline void to_json(json &j, const DocumentSymbol &s) {
  j = json{{"name", s.name},
           {"kind", static_cast<int>(s.kind)},
           {"range", s.range},
           {"selectionRange", s.selectionRange}};
  if (!s.detail.empty()) {
    j["detail"] = s.detail;
  }
  if (!s.children.empty()) {
    j["children"] = s.children;
  }
}

// WorkspaceSymbol -> SymbolInformation
inline void to_json(json &j, const WorkspaceSymbol &s) {
  j = json{{"name", s.name}, {"kind", static_cast<int>(s.kind)}, {"location", s.location}};
  if (!s.containerName.empty()) {
    j["containerName"] = s.containerName;
  }
}

// TextEdit
inline void to_json(json &j, const TextEdit &e) {
  j = json{{"range", e.range}, {"newText", e.newText}};
}

// WorkspaceEdit
inline void to_json(json &j, const WorkspaceEdit &e) {
  j = json{{"changes", json::object()}};
  for (const auto &[uri, edits] : e.changes) {
    j["changes"][uri] = edits;
  }
}

// SemanticTokensResult
inline void to_json(json &j, const SemanticTokensResult &r) {
  j = json{{"data", r.data}};
  if (!r.resultId.empty()) {
    j["resultId"] = r.resultId;
  }
}

// ============================================================================
// LSP Server Class
// ============================================================================

/**
 * @brief LSP Server implementing JSON-RPC transport over stdio
 */
class LspServer {
public:
  LspServer() = default;

  /**
   * @brief Run the server main loop
   * @return Exit code (0 for clean shutdown)
   */
  int run() {
    // Set up diagnostics callback
    service_.onDiagnosticsChanged(
        [this](const std::string &uri, const std::vector<Diagnostic> &diagnostics) {
          publishDiagnostics(uri, diagnostics);
        });

    // Main message loop
    while (running_) {
      auto msg = readMessage();
      if (!msg) {
        if (!running_)
          break;
        continue;
      }

      handleMessage(*msg);
    }

    return shutdownReceived_ ? 0 : 1;
  }

private:
  // ========================================================================
  // Message I/O
  // ========================================================================

  /**
   * @brief Read a JSON-RPC message from stdin
   */
  std::optional<json> readMessage() {
    // Read headers
    long contentLength = -1;
    std::string line;

    while (std::getline(std::cin, line)) {
      // Remove trailing \r if present (Windows line endings)
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }

      // Parse Content-Length header
      const std::string prefix = "Content-Length: ";
      if (line.rfind(prefix, 0) == 0) {
        try {
          contentLength = std::stol(line.substr(prefix.length()));
        } catch (...) {
          contentLength = -1;
        }
      }
      // Empty line indicates end of headers
      else if (line.empty()) {
        break;
      }

      // Check for EOF
      if (std::cin.eof() || std::cin.bad()) {
        running_ = false;
        return std::nullopt;
      }
    }

    if (contentLength <= 0) {
      if (std::cin.eof() || std::cin.bad()) {
        running_ = false;
      }
      return std::nullopt;
    }

    // Read content
    std::string content(contentLength, '\0');
    std::cin.read(content.data(), contentLength);

    if (!std::cin || std::cin.gcount() != contentLength) {
      running_ = false;
      return std::nullopt;
    }

    // Parse JSON
    try {
      json parsed = json::parse(content);
      JSONRPC_LOG_RECV(content);
      return parsed;
    } catch (const json::parse_error &) {
      JSONRPC_LOG("[ERROR] JSON parse error for: " + content);
      return std::nullopt;
    }
  }

  /**
   * @brief Write a JSON-RPC message to stdout
   */
  void writeMessage(const json &msg) {
    std::string content = msg.dump(-1, ' ', false, json::error_handler_t::replace);
    JSONRPC_LOG_SEND(content);

    std::lock_guard<std::mutex> lock(outputMutex_);
    std::cout << "Content-Length: " << content.length() << "\r\n";
    std::cout << "\r\n";
    std::cout << content << std::flush;
  }

  /**
   * @brief Send a JSON-RPC response
   */
  void writeResponse(const JsonRpcId &id, const json &result) {
    if (std::holds_alternative<std::nullptr_t>(id)) {
      return; // Don't respond to null IDs
    }

    json response = {{"jsonrpc", "2.0"}, {"result", result}};
    std::visit([&response](auto &&arg) { response["id"] = arg; }, id);
    writeMessage(response);
  }

  /**
   * @brief Send a JSON-RPC error response
   */
  void writeErrorResponse(const JsonRpcId &id, int code, const std::string &message) {
    json response = {{"jsonrpc", "2.0"}, {"error", {{"code", code}, {"message", message}}}};
    std::visit([&response](auto &&arg) { response["id"] = arg; }, id);
    writeMessage(response);
  }

  /**
   * @brief Send a JSON-RPC notification
   */
  void writeNotification(const std::string &method, const json &params) {
    json notification = {{"jsonrpc", "2.0"}, {"method", method}};
    if (!params.is_null()) {
      notification["params"] = params;
    }
    writeMessage(notification);
  }

  // ========================================================================
  // Message Handling
  // ========================================================================

  /**
   * @brief Handle an incoming JSON-RPC message
   */
  void handleMessage(const json &msg) {
    if (!msg.is_object())
      return;
    if (!msg.contains("jsonrpc") || msg.value("jsonrpc", "") != "2.0")
      return;

    // Request or notification?
    if (msg.contains("id")) {
      // This is a request
      JsonRpcId id = parseId(msg.at("id"));

      if (msg.contains("method")) {
        std::string method = msg.value("method", "");
        json params = msg.value("params", json(nullptr));

        // Check server state
        if (shutdownReceived_) {
          writeErrorResponse(id, JsonRpcErrorCode::InvalidRequest, "Server shutting down");
          return;
        }

        if (!initialized_ && method != "initialize") {
          writeErrorResponse(id, JsonRpcErrorCode::ServerNotInitialized, "Server not initialized");
          return;
        }

        // Dispatch request
        handleRequest(method, id, params);
      }
    } else if (msg.contains("method")) {
      // This is a notification
      std::string method = msg.value("method", "");
      json params = msg.value("params", json(nullptr));

      if (shutdownReceived_ && method != "exit") {
        return;
      }

      // Ignore $/ prefixed notifications
      if (method.rfind("$/", 0) == 0) {
        return;
      }

      handleNotification(method, params);
    }
  }

  /**
   * @brief Parse JSON-RPC ID
   */
  JsonRpcId parseId(const json &idJson) {
    if (idJson.is_string())
      return idJson.get<std::string>();
    if (idJson.is_number_integer())
      return idJson.get<int>();
    return nullptr;
  }

  /**
   * @brief Handle a JSON-RPC request
   */
  void handleRequest(const std::string &method, const JsonRpcId &id, const json &params) {
    if (method == "initialize") {
      handleInitialize(id, params);
    } else if (method == "shutdown") {
      handleShutdown(id);
    } else if (method == "textDocument/completion") {
      handleCompletion(id, params);
    } else if (method == "textDocument/hover") {
      handleHover(id, params);
    } else if (method == "textDocument/definition") {
      handleDefinition(id, params);
    } else if (method == "textDocument/declaration") {
      handleDeclaration(id, params);
    } else if (method == "textDocument/typeDefinition") {
      handleTypeDefinition(id, params);
    } else if (method == "textDocument/references") {
      handleReferences(id, params);
    } else if (method == "textDocument/documentSymbol") {
      handleDocumentSymbol(id, params);
    } else if (method == "workspace/symbol") {
      handleWorkspaceSymbol(id, params);
    } else if (method == "textDocument/rename") {
      handleRename(id, params);
    } else if (method == "textDocument/prepareRename") {
      handlePrepareRename(id, params);
    } else if (method == "textDocument/signatureHelp") {
      handleSignatureHelp(id, params);
    } else if (method == "textDocument/formatting") {
      handleFormatting(id, params);
    } else if (method == "textDocument/rangeFormatting") {
      handleRangeFormatting(id, params);
    } else if (method == "textDocument/semanticTokens/full") {
      handleSemanticTokensFull(id, params);
    } else if (method == "textDocument/codeAction") {
      handleCodeAction(id, params);
    } else {
      writeErrorResponse(id, JsonRpcErrorCode::MethodNotFound, "Method not found: " + method);
    }
  }

  /**
   * @brief Handle a JSON-RPC notification
   */
  void handleNotification(const std::string &method, const json &params) {
    if (method == "initialized") {
      handleInitialized(params);
    } else if (method == "exit") {
      handleExit();
    } else if (method == "textDocument/didOpen") {
      handleDidOpen(params);
    } else if (method == "textDocument/didChange") {
      handleDidChange(params);
    } else if (method == "textDocument/didClose") {
      handleDidClose(params);
    } else if (method == "textDocument/didSave") {
      handleDidSave(params);
    }
    // Unknown notifications are silently ignored
  }

  // ========================================================================
  // Lifecycle Handlers
  // ========================================================================

  void handleInitialize(const JsonRpcId &id, const json &params) {
    // Extract root path
    std::string rootPath;
    if (params.contains("rootUri") && !params["rootUri"].is_null()) {
      rootPath = uri::uriToPath(params["rootUri"].get<std::string>());
    } else if (params.contains("rootPath") && !params["rootPath"].is_null()) {
      rootPath = params["rootPath"].get<std::string>();
    }

    // Initialize service
    service_.initialize(rootPath);

    // Build capabilities response
    json capabilities = {
        {"textDocumentSync",
         {{"openClose", true},
          {"change", 1}, // Full sync
          {"save", {{"includeText", false}}}}},
        {"hoverProvider", true},
        {"completionProvider", {{"triggerCharacters", {".", ":"}}, {"resolveProvider", false}}},
        {"signatureHelpProvider", {{"triggerCharacters", {"(", ","}}}},
        {"definitionProvider", true},
        {"declarationProvider", true},
        {"typeDefinitionProvider", true},
        {"referencesProvider", true},
        {"documentSymbolProvider", true},
        {"workspaceSymbolProvider", true},
        {"renameProvider", {{"prepareProvider", true}}},
        {"documentFormattingProvider", true},
        {"documentRangeFormattingProvider", true},
        {"codeActionProvider", true},
        {"semanticTokensProvider",
         {{"legend",
           {{"tokenTypes",
             {"namespace",     "type",      "class",    "enum",     "interface",  "struct",
              "typeParameter", "parameter", "variable", "property", "enumMember", "event",
              "function",      "method",    "macro",    "keyword",  "modifier",   "comment",
              "string",        "number",    "regexp",   "operator"}},
            {"tokenModifiers",
             {"declaration", "definition", "readonly", "static", "deprecated", "abstract", "async",
              "modification", "documentation", "defaultLibrary"}}}},
          {"full", true},
          {"delta", false}}}};

    json result = {{"capabilities", capabilities},
                   {"serverInfo", {{"name", "lang-lsp"}, {"version", "1.0.0"}}}};

    writeResponse(id, result);
  }

  void handleInitialized(const json & /*params*/) { initialized_ = true; }

  void handleShutdown(const JsonRpcId &id) {
    shutdownReceived_ = true;
    service_.shutdown();
    writeResponse(id, nullptr);
  }

  void handleExit() { running_ = false; }

  // ========================================================================
  // Document Synchronization Handlers
  // ========================================================================

  void handleDidOpen(const json &params) {
    if (!params.contains("textDocument"))
      return;

    const auto &doc = params["textDocument"];
    std::string uri = doc.value("uri", "");
    std::string text = doc.value("text", "");
    int64_t version = doc.value("version", 0);

    service_.didOpen(uri, std::move(text), version);
  }

  void handleDidChange(const json &params) {
    if (!params.contains("textDocument") || !params.contains("contentChanges"))
      return;

    const auto &doc = params["textDocument"];
    std::string uri = doc.value("uri", "");
    int64_t version = doc.value("version", 0);

    const auto &changes = params["contentChanges"];
    if (changes.empty())
      return;

    // For full sync, use the last change
    std::string newContent = changes.back().value("text", "");
    service_.didChange(uri, std::move(newContent), version);
  }

  void handleDidClose(const json &params) {
    if (!params.contains("textDocument"))
      return;

    std::string uri = params["textDocument"].value("uri", "");
    service_.didClose(uri);

    // Clear diagnostics
    publishDiagnostics(uri, {});
  }

  void handleDidSave(const json &params) {
    if (!params.contains("textDocument"))
      return;

    std::string uri = params["textDocument"].value("uri", "");
    service_.didSave(uri);
  }

  // ========================================================================
  // Language Feature Handlers
  // ========================================================================

  void handleCompletion(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    // Extract trigger character if present
    std::optional<char> triggerChar;
    if (params.contains("context") && params["context"].contains("triggerCharacter")) {
      std::string tc = params["context"]["triggerCharacter"].get<std::string>();
      if (!tc.empty()) {
        triggerChar = tc[0];
      }
    }

    auto result = service_.completion(uri, position, triggerChar);
    writeResponse(id, result);
  }

  void handleHover(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    auto result = service_.hover(uri, position);
    if (result.isEmpty()) {
      writeResponse(id, nullptr);
    } else {
      writeResponse(id, result);
    }
  }

  void handleSignatureHelp(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    auto result = service_.signatureHelp(uri, position);
    if (result.isEmpty()) {
      writeResponse(id, nullptr);
    } else {
      writeResponse(id, result);
    }
  }

  void handleDefinition(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    auto result = service_.definition(uri, position);
    if (result.empty()) {
      writeResponse(id, nullptr);
    } else {
      writeResponse(id, result);
    }
  }

  void handleDeclaration(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    auto result = service_.declaration(uri, position);
    if (result.empty()) {
      writeResponse(id, nullptr);
    } else {
      writeResponse(id, result);
    }
  }

  void handleTypeDefinition(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    auto result = service_.typeDefinition(uri, position);
    if (result.empty()) {
      writeResponse(id, nullptr);
    } else {
      writeResponse(id, result);
    }
  }

  void handleReferences(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    bool includeDeclaration = true;
    if (params.contains("context") && params["context"].contains("includeDeclaration")) {
      includeDeclaration = params["context"]["includeDeclaration"].get<bool>();
    }

    auto result = service_.references(uri, position, includeDeclaration);
    if (result.empty()) {
      writeResponse(id, json::array());
    } else {
      writeResponse(id, result);
    }
  }

  void handleDocumentSymbol(const JsonRpcId &id, const json &params) {
    if (!params.contains("textDocument")) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    std::string uri = params["textDocument"].value("uri", "");
    auto result = service_.documentSymbols(uri);
    writeResponse(id, result);
  }

  void handleWorkspaceSymbol(const JsonRpcId &id, const json &params) {
    std::string query = params.value("query", "");
    auto result = service_.workspaceSymbols(query);
    writeResponse(id, result);
  }

  void handleRename(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    std::string newName = params.value("newName", "");
    if (newName.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "New name is required");
      return;
    }

    auto result = service_.rename(uri, position, newName);
    if (result) {
      writeResponse(id, *result);
    } else {
      writeResponse(id, nullptr);
    }
  }

  void handlePrepareRename(const JsonRpcId &id, const json &params) {
    auto [uri, position] = extractTextDocumentPosition(params);
    if (uri.empty()) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    auto result = service_.prepareRename(uri, position);
    if (result) {
      writeResponse(id, *result);
    } else {
      writeResponse(id, nullptr);
    }
  }

  void handleFormatting(const JsonRpcId &id, const json &params) {
    if (!params.contains("textDocument")) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    std::string uri = params["textDocument"].value("uri", "");

    FormattingOptions options;
    if (params.contains("options")) {
      const auto &opts = params["options"];
      options.tabSize = opts.value("tabSize", 4u);
      options.insertSpaces = opts.value("insertSpaces", true);
      options.trimTrailingWhitespace = opts.value("trimTrailingWhitespace", true);
      options.insertFinalNewline = opts.value("insertFinalNewline", true);
      options.trimFinalNewlines = opts.value("trimFinalNewlines", true);
    }

    auto result = service_.formatting(uri, options);
    if (result.empty()) {
      writeResponse(id, json::array());
    } else {
      writeResponse(id, result);
    }
  }

  void handleRangeFormatting(const JsonRpcId &id, const json &params) {
    if (!params.contains("textDocument") || !params.contains("range")) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    std::string uri = params["textDocument"].value("uri", "");
    Range range;
    params["range"].get_to(range);

    FormattingOptions options;
    if (params.contains("options")) {
      const auto &opts = params["options"];
      options.tabSize = opts.value("tabSize", 4u);
      options.insertSpaces = opts.value("insertSpaces", true);
    }

    auto result = service_.rangeFormatting(uri, range, options);
    if (result.empty()) {
      writeResponse(id, json::array());
    } else {
      writeResponse(id, result);
    }
  }

  void handleSemanticTokensFull(const JsonRpcId &id, const json &params) {
    if (!params.contains("textDocument")) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    std::string uri = params["textDocument"].value("uri", "");
    auto result = service_.semanticTokensFull(uri);
    writeResponse(id, result);
  }

  void handleCodeAction(const JsonRpcId &id, const json &params) {
    if (!params.contains("textDocument") || !params.contains("range")) {
      writeErrorResponse(id, JsonRpcErrorCode::InvalidParams, "Invalid params");
      return;
    }

    std::string uri = params["textDocument"].value("uri", "");
    Range range;
    params["range"].get_to(range);

    // Extract diagnostics from context
    std::vector<Diagnostic> diagnostics;
    // Note: Would need to parse diagnostics from params["context"]["diagnostics"]

    auto result = service_.codeActions(uri, range, diagnostics);

    // Convert to JSON
    json actions = json::array();
    for (const auto &action : result) {
      json actionJson = {{"title", action.title}, {"kind", "quickfix"}};
      if (!action.edit.changes.empty()) {
        actionJson["edit"] = action.edit;
      }
      actions.push_back(actionJson);
    }

    writeResponse(id, actions);
  }

  // ========================================================================
  // Diagnostics
  // ========================================================================

  void publishDiagnostics(const std::string &uri, const std::vector<Diagnostic> &diagnostics) {
    json params = {{"uri", uri}, {"diagnostics", diagnostics}};
    writeNotification("textDocument/publishDiagnostics", params);
  }

  // ========================================================================
  // Utility Functions
  // ========================================================================

  /**
   * @brief Extract URI and position from textDocument/position params
   */
  std::pair<std::string, Position> extractTextDocumentPosition(const json &params) {
    std::string uri;
    Position position{0, 0};

    if (params.contains("textDocument") && params.contains("position")) {
      uri = params["textDocument"].value("uri", "");
      params["position"].get_to(position);
    }

    return {uri, position};
  }

  // ========================================================================
  // Member Variables
  // ========================================================================

  LspService service_;
  std::mutex outputMutex_;
  std::atomic<bool> running_{true};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> shutdownReceived_{false};
};

} // namespace lsp
} // namespace lang

// ============================================================================
// Main Entry Point
// ============================================================================
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char *argv[]) {
#ifdef _WIN32
  // Windows 下必须设置为二进制模式，否则读取 Content-Length 会出错
  _setmode(_fileno(stdin), _O_BINARY);
  _setmode(_fileno(stdout), _O_BINARY);
#endif
  std::ios_base::sync_with_stdio(false);
  std::cin.tie(nullptr);
  
  // Parse command line arguments
  std::string logPath = "C:/Users/ftp/Desktop/spt/spt-lsp/test/lsp_trace.log";
  bool enableLog = false;
  
  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];
    if (arg == "--version" || arg == "-v") {
      std::cout << "lang-lsp version 1.0.0\n";
      return 0;
    } else if (arg == "--help" || arg == "-h") {
      std::cout << "Usage: lang-lsp [options]\n";
      std::cout << "Options:\n";
      std::cout << "  --version, -v       Show version information\n";
      std::cout << "  --help, -h          Show this help message\n";
      std::cout << "  --log [path]        Enable logging (default: lsp_trace.log)\n";
      return 0;
    } else if (arg == "--log") {
      enableLog = true;
      if (i + 1 < argc && argv[i + 1][0] != '-') {
        logPath = argv[++i];
      }
    }
  }
  
  // Check environment variable for logging
  const char* envLog = std::getenv("SPT_LSP_LOG");
  if (envLog != nullptr) {
    enableLog = true;
    std::string envPath(envLog);
    if (!envPath.empty() && envPath != "1" && envPath != "true") {
      logPath = envPath;
    }
  }

  // Always enable logging for debugging
  enableLog = true;

  if (enableLog) {
    lang::lsp::JsonRpcLogger::instance().enable(logPath);
    lang::lsp::JSONRPC_LOG("=== LSP Server Started ===");
  }

  // Run the LSP server
  lang::lsp::LspServer server;
  int result = server.run();
  
  if (enableLog) {
    lang::lsp::JSONRPC_LOG("=== LSP Server Stopped (exit code: " + std::to_string(result) + ") ===");
    lang::lsp::JsonRpcLogger::instance().disable();
  }
  
  return result;
}
