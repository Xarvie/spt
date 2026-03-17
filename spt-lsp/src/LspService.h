/**
 * @file LspService.h
 * @brief Language Server Protocol Service Implementation
 *
 * Provides complete LSP feature support including:
 * - Document synchronization (open, change, close)
 * - Go to definition / declaration
 * - Find references
 * - Hover information
 * - Code completion
 * - Signature help
 * - Document symbols
 * - Workspace symbols
 * - Rename symbol
 * - Code actions
 * - Diagnostics
 * - Semantic tokens
 * - Formatting
 *
 * Key Design Principles:
 * - Tolerant: Always returns useful results, even with errors
 * - Efficient: Caches parsed/analyzed results
 * - Thread-safe: Designed for concurrent access
 * - Modular: Each feature is independently testable
 *
 * @copyright Copyright (c) 2024-2025
 */

#pragma once

#include "LineOffsetTable.h"
#include "NodeFinder.h"
#include "SemanticAnalyzer.h"
#include "SourceFile.h"
#include "Workspace.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace lang {
namespace lsp {

// ============================================================================
// LSP Data Types
// ============================================================================

/**
 * @brief Location in a document
 */
struct Location {
  std::string uri;
  Range range;
};

/**
 * @brief Link to a location (for definition/declaration)
 */
struct LocationLink {
  std::string targetUri;
  Range targetRange;                         ///< Full range of target
  Range targetSelectionRange;                ///< Name/identifier range
  std::optional<Range> originSelectionRange; ///< Origin in source
};

/**
 * @brief Hover information
 */
struct HoverResult {
  std::string contents;       ///< Markdown content
  std::optional<Range> range; ///< Range of hovered element

  [[nodiscard]] bool isEmpty() const noexcept { return contents.empty(); }
};

/**
 * @brief Symbol kind (LSP standard values)
 */
enum class SymbolKind : uint8_t {
  File = 1,
  Module = 2,
  Namespace = 3,
  Package = 4,
  Class = 5,
  Method = 6,
  Property = 7,
  Field = 8,
  Constructor = 9,
  Enum = 10,
  Interface = 11,
  Function = 12,
  Variable = 13,
  Constant = 14,
  String = 15,
  Number = 16,
  Boolean = 17,
  Array = 18,
  Object = 19,
  Key = 20,
  Null = 21,
  EnumMember = 22,
  Struct = 23,
  Event = 24,
  Operator = 25,
  TypeParameter = 26,
};

/**
 * @brief Document symbol (outline item)
 */
struct DocumentSymbol {
  std::string name;
  std::string detail;
  SymbolKind kind;
  Range range;          ///< Full range including body
  Range selectionRange; ///< Name range
  std::vector<DocumentSymbol> children;
};

/**
 * @brief Workspace symbol
 */
struct WorkspaceSymbol {
  std::string name;
  SymbolKind kind;
  Location location;
  std::string containerName;
};

/**
 * @brief Completion item kind (LSP standard values)
 */
enum class CompletionItemKind : uint8_t {
  Text = 1,
  Method = 2,
  Function = 3,
  Constructor = 4,
  Field = 5,
  Variable = 6,
  Class = 7,
  Interface = 8,
  Module = 9,
  Property = 10,
  Unit = 11,
  Value = 12,
  Enum = 13,
  Keyword = 14,
  Snippet = 15,
  Color = 16,
  File = 17,
  Reference = 18,
  Folder = 19,
  EnumMember = 20,
  Constant = 21,
  Struct = 22,
  Event = 23,
  Operator = 24,
  TypeParameter = 25,
};

/**
 * @brief Insert text format (LSP standard)
 */
enum class InsertTextFormat : uint8_t {
  PlainText = 1, ///< 纯文本，按字面意思插入
  Snippet = 2,   ///< Snippet 格式，支持 $0, $1, ${1:placeholder} 等占位符
};

/**
 * @brief A single completion item
 */
struct CompletionItem {
  std::string label;
  CompletionItemKind kind = CompletionItemKind::Text;
  std::string detail;
  std::string documentation;
  std::string insertText;
  std::string filterText;
  std::string sortText;
  bool deprecated = false;
  InsertTextFormat insertTextFormat = InsertTextFormat::PlainText;

  // Text edit for insertion
  std::optional<Range> textEditRange;
  std::string textEditNewText;
};

/**
 * @brief Completion result
 */
struct CompletionResult {
  std::vector<CompletionItem> items;
  bool isIncomplete = false;
};

/**
 * @brief Parameter information for signature help
 */
struct ParameterInformation {
  std::string label;
  std::string documentation;
};

/**
 * @brief Signature information
 */
struct SignatureInformation {
  std::string label;
  std::string documentation;
  std::vector<ParameterInformation> parameters;
};

/**
 * @brief Signature help result
 */
struct SignatureHelp {
  std::vector<SignatureInformation> signatures;
  uint32_t activeSignature = 0;
  uint32_t activeParameter = 0;

  [[nodiscard]] bool isEmpty() const noexcept { return signatures.empty(); }
};

/**
 * @brief Text edit for document changes
 */
struct TextEdit {
  Range range;
  std::string newText;
};

/**
 * @brief Workspace edit (for rename, etc.)
 */
struct WorkspaceEdit {
  std::unordered_map<std::string, std::vector<TextEdit>> changes;
};

/**
 * @brief Code action kind
 */
enum class CodeActionKind {
  QuickFix,
  Refactor,
  RefactorExtract,
  RefactorInline,
  RefactorRewrite,
  Source,
  SourceOrganizeImports,
};

/**
 * @brief Code action
 */
struct CodeAction {
  std::string title;
  CodeActionKind kind;
  std::vector<Diagnostic> diagnostics;
  WorkspaceEdit edit;
  bool isPreferred = false;
};

/**
 * @brief Semantic token types
 */
enum class SemanticTokenType : uint8_t {
  Namespace,
  Type,
  Class,
  Enum,
  Interface,
  Struct,
  TypeParameter,
  Parameter,
  Variable,
  Property,
  EnumMember,
  Event,
  Function,
  Method,
  Macro,
  Keyword,
  Modifier,
  Comment,
  String,
  Number,
  Regexp,
  Operator,
};

/**
 * @brief Semantic token modifiers (bit flags)
 */
enum class SemanticTokenModifier : uint16_t {
  None = 0,
  Declaration = 1 << 0,
  Definition = 1 << 1,
  Readonly = 1 << 2,
  Static = 1 << 3,
  Deprecated = 1 << 4,
  Abstract = 1 << 5,
  Async = 1 << 6,
  Modification = 1 << 7,
  Documentation = 1 << 8,
  DefaultLibrary = 1 << 9,
};

[[nodiscard]] inline constexpr SemanticTokenModifier operator|(SemanticTokenModifier a,
                                                               SemanticTokenModifier b) noexcept {
  return static_cast<SemanticTokenModifier>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

/**
 * @brief A single semantic token
 */
struct SemanticToken {
  uint32_t line;      ///< 0-based line
  uint32_t startChar; ///< 0-based start character
  uint32_t length;    ///< Token length
  SemanticTokenType type;
  SemanticTokenModifier modifiers = SemanticTokenModifier::None;
};

/**
 * @brief Semantic tokens result (delta-encoded)
 */
struct SemanticTokensResult {
  std::vector<uint32_t>
      data; ///< Encoded tokens [deltaLine, deltaStart, length, type, modifiers, ...]
  std::string resultId;
};

/**
 * @brief Formatting options
 */
struct FormattingOptions {
  uint32_t tabSize = 4;
  bool insertSpaces = true;
  bool trimTrailingWhitespace = true;
  bool insertFinalNewline = true;
  bool trimFinalNewlines = true;
};

// ============================================================================
// LSP Service Configuration
// ============================================================================

/**
 * @brief LSP service configuration
 */
struct LspServiceConfig {
  // Feature flags
  bool enableSemanticTokens = true;
  bool enableCompletion = true;
  bool enableHover = true;
  bool enableSignatureHelp = true;
  bool enableDefinition = true;
  bool enableReferences = true;
  bool enableRename = true;
  bool enableCodeActions = true;
  bool enableFormatting = true;
  bool enableDocumentSymbols = true;
  bool enableWorkspaceSymbols = true;

  // Limits
  size_t maxCompletionItems = 100;
  size_t maxReferences = 1000;
  size_t maxDiagnosticsPerFile = 100;

  // Behavior
  bool tolerantParsing = true;
  bool incrementalSync = true;
};

// ============================================================================
// LSP Service Class
// ============================================================================

/**
 * @brief Main LSP Service Implementation
 *
 * This class orchestrates all LSP features, managing the workspace,
 * document synchronization, and semantic analysis.
 *
 * Usage:
 *   LspService service;
 *   service.initialize("/workspace/root");
 *
 *   // Open a document
 *   service.didOpen(uri, content, 1);
 *
 *   // Get completions
 *   auto completions = service.completion(uri, position);
 *
 *   // Get hover info
 *   auto hover = service.hover(uri, position);
 */
class LspService {
public:
  LspService();
  explicit LspService(LspServiceConfig config);
  ~LspService();

  // Non-copyable, movable
  LspService(const LspService &) = delete;
  LspService &operator=(const LspService &) = delete;
  LspService(LspService &&) noexcept;
  LspService &operator=(LspService &&) noexcept;

  // ========================================================================
  // Lifecycle
  // ========================================================================

  /**
   * @brief Initialize the service with workspace root
   * @param rootPath Workspace root directory
   */
  void initialize(std::string rootPath);

  /**
   * @brief Shutdown the service
   */
  void shutdown();

  /**
   * @brief Check if service is initialized
   */
  [[nodiscard]] bool isInitialized() const noexcept;

  /**
   * @brief Get configuration
   */
  [[nodiscard]] const LspServiceConfig &config() const noexcept;

  /**
   * @brief Update configuration
   */
  void setConfig(LspServiceConfig config);

  // ========================================================================
  // Document Synchronization
  // ========================================================================

  /**
   * @brief Handle document open
   * @param uri Document URI
   * @param content Document content
   * @param version Document version
   */
  void didOpen(std::string_view uri, std::string content, int64_t version);

  /**
   * @brief Handle full document change
   * @param uri Document URI
   * @param content New content
   * @param version New version
   */
  void didChange(std::string_view uri, std::string content, int64_t version);

  /**
   * @brief Handle incremental document change
   * @param uri Document URI
   * @param changes List of changes
   * @param version New version
   */
  void didChangeIncremental(std::string_view uri,
                            const std::vector<std::pair<Range, std::string>> &changes,
                            int64_t version);

  /**
   * @brief Handle document close
   * @param uri Document URI
   */
  void didClose(std::string_view uri);

  /**
   * @brief Handle document save
   * @param uri Document URI
   */
  void didSave(std::string_view uri);

  // ========================================================================
  // Language Features
  // ========================================================================

  /**
   * @brief Get hover information at position
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @return Hover information
   */
  [[nodiscard]] HoverResult hover(std::string_view uri, Position position);

  /**
   * @brief Get completion items at position
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @param triggerCharacter Optional trigger character
   * @return Completion result
   */
  [[nodiscard]] CompletionResult completion(std::string_view uri, Position position,
                                            std::optional<char> triggerCharacter = {});

  /**
   * @brief Resolve additional completion item details
   * @param item Item to resolve
   * @return Resolved item
   */
  [[nodiscard]] CompletionItem resolveCompletion(const CompletionItem &item);

  /**
   * @brief Get signature help at position
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @return Signature help
   */
  [[nodiscard]] SignatureHelp signatureHelp(std::string_view uri, Position position);

  /**
   * @brief Go to definition
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @return Definition locations
   */
  [[nodiscard]] std::vector<LocationLink> definition(std::string_view uri, Position position);

  /**
   * @brief Go to declaration
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @return Declaration locations
   */
  [[nodiscard]] std::vector<LocationLink> declaration(std::string_view uri, Position position);

  /**
   * @brief Go to type definition
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @return Type definition locations
   */
  [[nodiscard]] std::vector<LocationLink> typeDefinition(std::string_view uri, Position position);

  /**
   * @brief Find all references
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @param includeDeclaration Whether to include the declaration
   * @return Reference locations
   */
  [[nodiscard]] std::vector<Location> references(std::string_view uri, Position position,
                                                 bool includeDeclaration = true);

  /**
   * @brief Get document symbols (outline)
   * @param uri Document URI
   * @return Document symbols
   */
  [[nodiscard]] std::vector<DocumentSymbol> documentSymbols(std::string_view uri);

  /**
   * @brief Search workspace symbols
   * @param query Search query
   * @return Matching symbols
   */
  [[nodiscard]] std::vector<WorkspaceSymbol> workspaceSymbols(std::string_view query);

  /**
   * @brief Rename symbol
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @param newName New name
   * @return Workspace edit
   */
  [[nodiscard]] std::optional<WorkspaceEdit> rename(std::string_view uri, Position position,
                                                    std::string_view newName);

  /**
   * @brief Prepare rename (validate and get range)
   * @param uri Document URI
   * @param position Cursor position (0-based)
   * @return Range of symbol to rename, or nullopt if not renameable
   */
  [[nodiscard]] std::optional<Range> prepareRename(std::string_view uri, Position position);

  /**
   * @brief Get code actions
   * @param uri Document URI
   * @param range Range to get actions for
   * @param diagnostics Diagnostics in range
   * @return Available code actions
   */
  [[nodiscard]] std::vector<CodeAction> codeActions(std::string_view uri, Range range,
                                                    const std::vector<Diagnostic> &diagnostics);

  /**
   * @brief Get semantic tokens for entire document
   * @param uri Document URI
   * @return Semantic tokens
   */
  [[nodiscard]] SemanticTokensResult semanticTokensFull(std::string_view uri);

  /**
   * @brief Get semantic tokens delta
   * @param uri Document URI
   * @param previousResultId Previous result ID
   * @return Semantic tokens or delta
   */
  [[nodiscard]] SemanticTokensResult semanticTokensDelta(std::string_view uri,
                                                         std::string_view previousResultId);

  /**
   * @brief Format entire document
   * @param uri Document URI
   * @param options Formatting options
   * @return Text edits
   */
  [[nodiscard]] std::vector<TextEdit> formatting(std::string_view uri,
                                                 const FormattingOptions &options);

  /**
   * @brief Format range in document
   * @param uri Document URI
   * @param range Range to format
   * @param options Formatting options
   * @return Text edits
   */
  [[nodiscard]] std::vector<TextEdit> rangeFormatting(std::string_view uri, Range range,
                                                      const FormattingOptions &options);

  // ========================================================================
  // Diagnostics
  // ========================================================================

  /**
   * @brief Get diagnostics for a document
   * @param uri Document URI
   * @return Diagnostics
   */
  [[nodiscard]] std::vector<Diagnostic> getDiagnostics(std::string_view uri);

  /**
   * @brief Get diagnostics for all open documents
   * @return Map of URI to diagnostics
   */
  [[nodiscard]] std::unordered_map<std::string, std::vector<Diagnostic>> getAllDiagnostics();

  /**
   * @brief Register a callback for diagnostics updates
   * @param callback Called when diagnostics change
   * @return Callback ID for removal
   */
  size_t onDiagnosticsChanged(
      std::function<void(const std::string &uri, const std::vector<Diagnostic> &)> callback);

  /**
   * @brief Remove diagnostics callback
   * @param id Callback ID
   */
  void removeDiagnosticsCallback(size_t id);

  // ========================================================================
  // Workspace Management
  // ========================================================================

  /**
   * @brief Get the workspace
   */
  [[nodiscard]] Workspace &workspace() noexcept;
  [[nodiscard]] const Workspace &workspace() const noexcept;

  /**
   * @brief Add a workspace folder
   * @param uri Folder URI
   */
  void addWorkspaceFolder(std::string_view uri);

  /**
   * @brief Remove a workspace folder
   * @param uri Folder URI
   */
  void removeWorkspaceFolder(std::string_view uri);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// ============================================================================
// Utility Functions
// ============================================================================

/**
 * @brief Convert semantic symbol kind to LSP symbol kind
 */
[[nodiscard]] SymbolKind toSymbolKind(semantic::SymbolKind kind) noexcept;

/**
 * @brief Convert semantic symbol kind to completion item kind
 */
[[nodiscard]] CompletionItemKind toCompletionItemKind(semantic::SymbolKind kind) noexcept;

/**
 * @brief Format type for display
 */
[[nodiscard]] std::string formatType(types::TypeRef type);

/**
 * @brief Format symbol signature for display
 */
[[nodiscard]] std::string formatSymbolSignature(const semantic::Symbol *symbol);

/**
 * @brief Create markdown hover content
 */
[[nodiscard]] std::string createHoverMarkdown(const semantic::Symbol *symbol, types::TypeRef type);

} // namespace lsp
} // namespace lang
