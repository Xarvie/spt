/**
 * @file LspService.cpp
 * @brief Language Server Protocol Service Implementation
 *
 * @copyright Copyright (c) 2024-2025)
 *
 * ============================================================================
 * IMPORTANT: Position Conversion Notes (坑点说明)
 * ============================================================================
 *
 * LSP positions are 0-based (line and character start from 0).
 * Internal positions are 1-based (line and column start from 1).
 *
 * The conversion from 0-based to 1-based happens in main.cpp's from_json:
 *   p.line = j.at("line").get<uint32_t>() + 1;
 *   p.column = j.at("character").get<uint32_t>() + 1;
 *
 * Therefore, in LspService methods, the Position parameter is ALREADY 1-based.
 * DO NOT call Position::fromZeroBased() again, or positions will be off by 1!
 *
 * Example bug:
 *   // WRONG - double conversion!
 *   Position internalPos = Position::fromZeroBased(position.line, position.column);
 *   uint32_t offset = file->getOffset(internalPos);  // offset will be wrong!
 *
 *   // CORRECT - position is already 1-based
 *   uint32_t offset = file->getOffset(position);
 *
 * ============================================================================
 */

#include "LspService.h"

// 启用调试日志 - 调试完成后注释掉这行
#define LSP_DEBUG_ENABLED
#include "ImportResolver.h"
#include "LspLogger.h"

#include <algorithm>
#include <cctype>
#include <iomanip>
#include <sstream>

namespace lang {
namespace lsp {

// ============================================================================
// Implementation Class
// ============================================================================

class LspService::Impl {
public:
  explicit Impl(LspServiceConfig config) : config_(std::move(config)) {}

  // Configuration
  LspServiceConfig config_;
  bool initialized_ = false;

  // Workspace
  Workspace workspace_;

  // Semantic models (cached per file)
  std::unordered_map<std::string, semantic::SemanticModel> semanticModels_;
  mutable std::mutex modelsMutex_;

  // String table for semantic analysis
  ast::StringTable stringTable_;

  // Diagnostics callbacks
  std::unordered_map<size_t,
                     std::function<void(const std::string &, const std::vector<Diagnostic> &)>>
      diagnosticsCallbacks_;
  size_t nextCallbackId_ = 0;
  mutable std::mutex callbacksMutex_;

  // ========================================================================
  // Semantic Analysis
  // ========================================================================

  /**
   * @brief Get or create semantic model for a file
   */
  semantic::SemanticModel *getSemanticModel(SourceFile *file) {
    if (!file)
      return nullptr;

    std::lock_guard<std::mutex> lock(modelsMutex_);

    // Get current AST
    ast::CompilationUnitNode *ast = file->getAst();
    if (!ast)
      return nullptr;

    auto it = semanticModels_.find(file->uri());
    if (it != semanticModels_.end()) {
      // Check if the AST has changed (re-parsed)
      if (it->second.astRoot() == ast) {
        // AST is the same, return cached model
        return &it->second;
      }
      // AST has changed, need to re-analyze
      LSP_LOG("getSemanticModel: AST changed for " << file->uri() << ", re-analyzing");
      semanticModels_.erase(it);
    }

    // 使用文件自己的 StringTable，确保 InternedString 一致
    semantic::SemanticAnalyzer analyzer(file->factory().stringTable());

    // Set current file path for import resolution
    const std::string &filePath = file->path();
    analyzer.setCurrentFilePath(&filePath);

    // Set import resolver callback
    analyzer.setImportResolver([this](ast::ImportStmtNode *importStmt,
                                      semantic::ImportSymbol *importSymbol,
                                      const std::string &currentFilePath) {
      // Create ImportResolver with callback to getSemanticModel
      ImportResolver resolver(workspace_, [this](SourceFile *f) -> semantic::SemanticModel * {
        // Note: This is called during analysis, so we need to avoid deadlock
        // The modelsMutex_ is already held by the outer getSemanticModel call
        // We use a try_lock approach or separate lock for nested calls
        return this->getSemanticModelInternal(f);
      });
      resolver.resolve(importStmt, importSymbol, currentFilePath);
    });

    auto model = analyzer.analyze(ast);

    auto [inserted, _] = semanticModels_.emplace(file->uri(), std::move(model));
    return &inserted->second;
  }

  /**
   * @brief Internal getSemanticModel without locking (for nested calls)
   */
  semantic::SemanticModel *getSemanticModelInternal(SourceFile *file) {
    if (!file)
      return nullptr;

    // Get current AST
    ast::CompilationUnitNode *ast = file->getAst();
    if (!ast)
      return nullptr;

    // Check if already exists (no lock needed if called from within locked section)
    auto it = semanticModels_.find(file->uri());
    if (it != semanticModels_.end()) {
      // Check if the AST has changed (re-parsed)
      if (it->second.astRoot() == ast) {
        return &it->second;
      }
      // AST has changed, need to re-analyze
      semanticModels_.erase(it);
    }

    semantic::SemanticAnalyzer analyzer(file->factory().stringTable());
    // Don't set import resolver for nested calls to avoid infinite recursion
    auto model = analyzer.analyze(ast);

    auto [inserted, _] = semanticModels_.emplace(file->uri(), std::move(model));
    return &inserted->second;
  }

  /**
   * @brief Invalidate semantic model for a file
   */
  void invalidateSemanticModel(const std::string &uri) {
    std::lock_guard<std::mutex> lock(modelsMutex_);
    semanticModels_.erase(uri);
  }

  // ========================================================================
  // Document Symbol Collection
  // ========================================================================

  /**
   * @brief Collect document symbols from AST
   */
  void collectDocumentSymbols(ast::AstNode *node, std::vector<DocumentSymbol> &symbols,
                              const SourceFile &file) {
    if (!node) {
      LSP_LOG("collectDocumentSymbols: node is null");
      return;
    }

    LSP_LOG("collectDocumentSymbols: node kind=" << ast::astKindToString(node->kind));

    // 使用文件自己的 StringTable，而不是 LspService::Impl 的 stringTable_
    const ast::StringTable &strings = file.factory().stringTable();
    LSP_LOG("StringTable address=" << (void *)&strings << ", size=" << strings.size());
    LSP_LOG("AstFactory address=" << (void *)&file.factory());

    switch (node->kind) {
    case ast::AstKind::FunctionDecl: {
      auto *func = static_cast<ast::FunctionDeclNode *>(node);
      LSP_LOG("  FunctionDecl: name.id=" << func->name.id);
      LSP_LOG("  StringTable size=" << strings.size());

      // 打印 StringTable 中所有字符串用于诊断
      LSP_LOG("  StringTable contents:");
      for (size_t i = 0; i < strings.size() && i < 10; ++i) {
        ast::InternedString is{static_cast<uint32_t>(i)};
        LSP_LOG("    [" << i << "] = '" << strings.get(is) << "'");
      }

      auto nameView = strings.get(func->name);
      LSP_LOG("  FunctionDecl: nameView.data()=" << (void *)nameView.data()
                                                 << ", size=" << nameView.size()
                                                 << ", empty=" << nameView.empty());
      DocumentSymbol sym;
      sym.name = std::string(nameView);
      LSP_LOG("  FunctionDecl: name='" << sym.name << "'");
      sym.kind = SymbolKind::Function;
      sym.range = file.toRange(func->range);
      sym.selectionRange = sym.range; // Could be refined to just name

      // Build detail with parameters
      std::string detail = "(";
      bool first = true;
      for (auto *param : func->parameters) {
        if (!first)
          detail += ", ";
        first = false;
        detail += strings.get(param->name);
      }
      detail += ")";
      sym.detail = detail;

      symbols.push_back(std::move(sym));
      LSP_LOG("  FunctionDecl: added to symbols");
      break;
    }

    case ast::AstKind::ClassDecl: {
      auto *cls = static_cast<ast::ClassDeclNode *>(node);
      DocumentSymbol sym;
      sym.name = std::string(strings.get(cls->name));
      sym.kind = SymbolKind::Class;
      sym.range = file.toRange(cls->range);
      sym.selectionRange = sym.range;

      // Collect children (fields and methods)
      for (auto *field : cls->fields) {
        DocumentSymbol fieldSym;
        fieldSym.name = std::string(strings.get(field->name));
        fieldSym.kind = field->isStatic() ? SymbolKind::Property : SymbolKind::Field;
        fieldSym.range = file.toRange(field->range);
        fieldSym.selectionRange = fieldSym.range;
        sym.children.push_back(std::move(fieldSym));
      }

      for (auto *method : cls->methods) {
        DocumentSymbol methodSym;
        methodSym.name = std::string(strings.get(method->name));
        methodSym.kind = SymbolKind::Method;
        methodSym.range = file.toRange(method->range);
        methodSym.selectionRange = methodSym.range;
        sym.children.push_back(std::move(methodSym));
      }

      symbols.push_back(std::move(sym));
      break;
    }

    case ast::AstKind::VarDecl: {
      auto *var = static_cast<ast::VarDeclNode *>(node);
      DocumentSymbol sym;
      sym.name = std::string(strings.get(var->name));
      sym.kind = var->isConst() ? SymbolKind::Constant : SymbolKind::Variable;
      sym.range = file.toRange(var->range);
      sym.selectionRange = sym.range;
      symbols.push_back(std::move(sym));
      break;
    }

    case ast::AstKind::CompilationUnit: {
      auto *unit = static_cast<ast::CompilationUnitNode *>(node);
      LSP_LOG("  CompilationUnit: statements.size()=" << unit->statements.size());
      for (auto *stmt : unit->statements) {
        LSP_LOG("    stmt kind=" << (stmt ? ast::astKindToString(stmt->kind) : "nullptr"));
        if (auto *declStmt = ast::ast_cast<ast::DeclStmtNode>(stmt)) {
          LSP_LOG("    -> is DeclStmt, decl="
                  << (declStmt->decl ? ast::astKindToString(declStmt->decl->kind) : "nullptr"));
          collectDocumentSymbols(declStmt->decl, symbols, file);
        }
      }
      break;
    }

    case ast::AstKind::DeclStmt: {
      auto *declStmt = static_cast<ast::DeclStmtNode *>(node);
      collectDocumentSymbols(declStmt->decl, symbols, file);
      break;
    }

    default:
      LSP_LOG("  Unhandled node kind: " << ast::astKindToString(node->kind));
      break;
    }
  }

  // ========================================================================
  // Semantic Token Collection
  // ========================================================================

  /**
   * @brief Collect semantic tokens from AST
   */
  void collectSemanticTokens(ast::AstNode *node, std::vector<SemanticToken> &tokens,
                             const SourceFile &file, semantic::SemanticModel *model) {
    if (!node)
      return;

    // Use NodeFinder to traverse
    NodeFinder::forEachChild(node, [&](ast::AstNode *child) {
      if (!child)
        return;

      switch (child->kind) {
      case ast::AstKind::Identifier: {
        auto *ident = static_cast<ast::IdentifierNode *>(child);
        SemanticToken token;
        Position pos = file.toPosition(child->range.begin);
        token.line = pos.line - 1; // Convert to 0-based
        token.startChar = pos.column - 1;
        token.length = child->range.length();

        // Determine token type based on resolved symbol
        if (model) {
          if (auto *sym = model->getResolvedSymbol(child)) {
            switch (sym->kind()) {
            case semantic::SymbolKind::Variable:
              token.type = SemanticTokenType::Variable;
              if (sym->isConst()) {
                token.modifiers = SemanticTokenModifier::Readonly;
              }
              break;
            case semantic::SymbolKind::Parameter:
              token.type = SemanticTokenType::Parameter;
              break;
            case semantic::SymbolKind::Function:
              token.type = SemanticTokenType::Function;
              break;
            case semantic::SymbolKind::Class:
              token.type = SemanticTokenType::Class;
              break;
            case semantic::SymbolKind::Field:
              token.type = SemanticTokenType::Property;
              if (sym->isStatic()) {
                token.modifiers = SemanticTokenModifier::Static;
              }
              break;
            case semantic::SymbolKind::Method:
              token.type = SemanticTokenType::Method;
              if (sym->isStatic()) {
                token.modifiers = SemanticTokenModifier::Static;
              }
              break;
            default:
              token.type = SemanticTokenType::Variable;
              break;
            }
          } else {
            token.type = SemanticTokenType::Variable;
          }
        } else {
          token.type = SemanticTokenType::Variable;
        }

        tokens.push_back(token);
        break;
      }

      case ast::AstKind::StringLiteral:
        addSemanticToken(tokens, child, file, SemanticTokenType::String);
        break;

      case ast::AstKind::IntLiteral:
      case ast::AstKind::FloatLiteral:
        addSemanticToken(tokens, child, file, SemanticTokenType::Number);
        break;

      default:
        break;
      }

      // Recurse
      collectSemanticTokens(child, tokens, file, model);
    });
  }

  void addSemanticToken(std::vector<SemanticToken> &tokens, ast::AstNode *node,
                        const SourceFile &file, SemanticTokenType type,
                        SemanticTokenModifier modifiers = SemanticTokenModifier::None) {
    SemanticToken token;
    Position pos = file.toPosition(node->range.begin);
    token.line = pos.line - 1;
    token.startChar = pos.column - 1;
    token.length = node->range.length();
    token.type = type;
    token.modifiers = modifiers;
    tokens.push_back(token);
  }

  /**
   * @brief Encode semantic tokens to LSP format
   */
  std::vector<uint32_t> encodeSemanticTokens(std::vector<SemanticToken> &tokens) {
    // Sort by position
    std::sort(tokens.begin(), tokens.end(), [](const auto &a, const auto &b) {
      if (a.line != b.line)
        return a.line < b.line;
      return a.startChar < b.startChar;
    });

    std::vector<uint32_t> data;
    data.reserve(tokens.size() * 5);

    uint32_t prevLine = 0;
    uint32_t prevChar = 0;

    for (const auto &token : tokens) {
      uint32_t deltaLine = token.line - prevLine;
      uint32_t deltaChar = (deltaLine == 0) ? (token.startChar - prevChar) : token.startChar;

      data.push_back(deltaLine);
      data.push_back(deltaChar);
      data.push_back(token.length);
      data.push_back(static_cast<uint32_t>(token.type));
      data.push_back(static_cast<uint32_t>(token.modifiers));

      prevLine = token.line;
      prevChar = token.startChar;
    }

    return data;
  }

  // ========================================================================
  // Completion Helpers
  // ========================================================================

  /**
   * @brief Add keyword completions
   */
  void addKeywordCompletions(std::vector<CompletionItem> &items) {
    static const char *keywords[] = {"if",     "else",     "while",    "for",   "return",
                                     "break",  "continue", "function", "class", "var",
                                     "const",  "new",      "true",     "false", "null",
                                     "import", "export",   "defer",    "static"};

    for (const char *kw : keywords) {
      CompletionItem item;
      item.label = kw;
      item.kind = CompletionItemKind::Keyword;
      item.insertText = kw;
      items.push_back(std::move(item));
    }
  }

  /**
   * @brief Add type completions
   */
  void addTypeCompletions(std::vector<CompletionItem> &items) {
    static const char *types[] = {"int",  "float", "number", "string", "bool",
                                  "void", "any",   "list",   "map"};

    for (const char *t : types) {
      CompletionItem item;
      item.label = t;
      item.kind = CompletionItemKind::Keyword;
      item.insertText = t;
      items.push_back(std::move(item));
    }
  }

  /**
   * @brief Add symbol completions from scope
   */
  void addScopeCompletions(std::vector<CompletionItem> &items, semantic::Scope *scope,
                           size_t maxItems) {
    if (!scope)
      return;

    auto symbols = scope->allVisibleSymbols();
    for (auto *sym : symbols) {
      if (items.size() >= maxItems)
        break;

      CompletionItem item;
      item.label = sym->name();
      item.kind = toCompletionItemKind(sym->kind());
      item.detail = formatSymbolSignature(sym);

      if (sym->isFunction()) {
        auto *funcSym = static_cast<const semantic::FunctionSymbol *>(sym);
        if (funcSym->parameters().empty()) {
          item.insertText = sym->name() + "()$0";
        } else {
          item.insertText = sym->name() + "($0)";
        }
        item.insertTextFormat = InsertTextFormat::Snippet;
      } else if (sym->isMethod()) {
        auto *methodSym = static_cast<const semantic::MethodSymbol *>(sym);
        if (methodSym->parameters().empty()) {
          item.insertText = sym->name() + "()$0";
        } else {
          item.insertText = sym->name() + "($0)";
        }
        item.insertTextFormat = InsertTextFormat::Snippet;
      } else {
        item.insertText = sym->name();
        item.insertTextFormat = InsertTextFormat::PlainText;
      }

      if (sym->type()) {
        item.documentation = "Type: " + sym->type()->toString();
      }

      items.push_back(std::move(item));
    }
  }

  /**
   * @brief Add member completions for a type
   */
  void addMemberCompletions(std::vector<CompletionItem> &items, types::TypeRef type,
                            bool staticOnly = false) {
    if (!type)
      return;

    if (type->isClass()) {
      auto *classType = static_cast<const types::ClassType *>(type.get());

      // Add fields
      for (const auto &field : classType->fields()) {
        if (staticOnly && !field.isStatic)
          continue;

        CompletionItem item;
        item.label = field.name;
        item.kind = CompletionItemKind::Field;
        if (field.type) {
          item.detail = field.type->toString();
        }
        item.insertText = field.name;
        items.push_back(std::move(item));
      }

      // Add methods
      for (const auto &method : classType->methods()) {
        if (staticOnly && !method.isStatic)
          continue;

        CompletionItem item;
        item.label = method.name;
        item.kind = CompletionItemKind::Method;
        if (method.type) {
          item.detail = method.type->toString();
        }
        item.insertText = method.name + "($0)";
        items.push_back(std::move(item));
      }
    } else if (type->isList()) {
      // Add list methods
      addListMethodCompletions(items);
    } else if (type->isMap()) {
      // Add map methods
      addMapMethodCompletions(items);
    } else if (type->isString()) {
      // Add string methods
      addStringMethodCompletions(items);
    }
  }

  void addListMethodCompletions(std::vector<CompletionItem> &items) {
    static const std::pair<const char *, const char *> methods[] = {
        {"push", "Add element to end"},        {"pop", "Remove and return last element"},
        {"insert", "Insert element at index"}, {"remove", "Remove element at index"},
        {"clear", "Remove all elements"},      {"size", "Get number of elements"},
        {"isEmpty", "Check if empty"},         {"contains", "Check if contains element"},
        {"indexOf", "Find index of element"},  {"sort", "Sort elements"},
        {"reverse", "Reverse order"},          {"map", "Transform elements"},
        {"filter", "Filter elements"},         {"reduce", "Reduce to single value"},
    };

    for (const auto &[name, doc] : methods) {
      CompletionItem item;
      item.label = name;
      item.kind = CompletionItemKind::Method;
      item.documentation = doc;
      item.insertText = std::string(name) + "($0)";
      items.push_back(std::move(item));
    }
  }

  void addMapMethodCompletions(std::vector<CompletionItem> &items) {
    static const std::pair<const char *, const char *> methods[] = {
        {"get", "Get value by key"},
        {"set", "Set value for key"},
        {"has", "Check if key exists"},
        {"delete", "Remove key"},
        {"clear", "Remove all entries"},
        {"size", "Get number of entries"},
        {"keys", "Get all keys"},
        {"values", "Get all values"},
        {"entries", "Get all key-value pairs"},
    };

    for (const auto &[name, doc] : methods) {
      CompletionItem item;
      item.label = name;
      item.kind = CompletionItemKind::Method;
      item.documentation = doc;
      item.insertText = std::string(name) + "($0)";
      items.push_back(std::move(item));
    }
  }

  void addStringMethodCompletions(std::vector<CompletionItem> &items) {
    static const std::pair<const char *, const char *> methods[] = {
        {"length", "Get string length"},
        {"charAt", "Get character at index"},
        {"substring", "Extract substring"},
        {"indexOf", "Find index of substring"},
        {"lastIndexOf", "Find last index of substring"},
        {"startsWith", "Check if starts with prefix"},
        {"endsWith", "Check if ends with suffix"},
        {"contains", "Check if contains substring"},
        {"toUpper", "Convert to uppercase"},
        {"toLower", "Convert to lowercase"},
        {"trim", "Remove whitespace from ends"},
        {"split", "Split by delimiter"},
        {"replace", "Replace occurrences"},
    };

    for (const auto &[name, doc] : methods) {
      CompletionItem item;
      item.label = name;
      item.kind = CompletionItemKind::Method;
      item.documentation = doc;
      item.insertText = std::string(name) + "($0)";
      items.push_back(std::move(item));
    }
  }

  // ========================================================================
  // Diagnostics Notification
  // ========================================================================

  void notifyDiagnosticsChanged(const std::string &uri,
                                const std::vector<Diagnostic> &diagnostics) {
    std::lock_guard<std::mutex> lock(callbacksMutex_);
    for (const auto &[_, callback] : diagnosticsCallbacks_) {
      callback(uri, diagnostics);
    }
  }
};

// ============================================================================
// LspService Implementation
// ============================================================================

LspService::LspService() : impl_(std::make_unique<Impl>(LspServiceConfig{})) {}

LspService::LspService(LspServiceConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

LspService::~LspService() = default;

LspService::LspService(LspService &&) noexcept = default;
LspService &LspService::operator=(LspService &&) noexcept = default;

// ============================================================================
// Lifecycle
// ============================================================================

void LspService::initialize(std::string rootPath) {
  impl_->workspace_ = Workspace(std::move(rootPath));
  impl_->workspace_.config().tolerantParsing = impl_->config_.tolerantParsing;
  impl_->workspace_.config().maxDiagnosticsPerFile =
      static_cast<int>(impl_->config_.maxDiagnosticsPerFile);
  impl_->initialized_ = true;
}

void LspService::shutdown() {
  impl_->semanticModels_.clear();
  impl_->initialized_ = false;
}

bool LspService::isInitialized() const noexcept { return impl_->initialized_; }

const LspServiceConfig &LspService::config() const noexcept { return impl_->config_; }

void LspService::setConfig(LspServiceConfig config) {
  impl_->config_ = std::move(config);
  if (impl_->initialized_) {
    impl_->workspace_.config().tolerantParsing = impl_->config_.tolerantParsing;
  }
}

// ============================================================================
// Document Synchronization
// ============================================================================

void LspService::didOpen(std::string_view uri, std::string content, int64_t version) {
  auto &file = impl_->workspace_.openFile(uri, std::move(content), version);

  // Parse and analyze
  file.reparse();
  impl_->invalidateSemanticModel(std::string(uri));

  // Notify diagnostics
  auto diagnostics = file.getDiagnostics();
  impl_->notifyDiagnosticsChanged(std::string(uri), diagnostics);
}

void LspService::didChange(std::string_view uri, std::string content, int64_t version) {
  if (impl_->workspace_.applyFullChange(uri, std::move(content), version)) {
    auto *file = impl_->workspace_.getFile(uri);
    if (file) {
      file->reparse();

      impl_->invalidateSemanticModel(file->uri());
      impl_->notifyDiagnosticsChanged(file->uri(), file->getDiagnostics());
    }
  }
}

void LspService::didChangeIncremental(std::string_view uri,
                                      const std::vector<std::pair<Range, std::string>> &changes,
                                      int64_t version) {
  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return;

  // Apply changes in reverse order (later positions first)
  // to avoid invalidating earlier positions
  auto sortedChanges = changes;
  std::sort(sortedChanges.begin(), sortedChanges.end(), [](const auto &a, const auto &b) {
    return a.first.start.line > b.first.start.line || (a.first.start.line == b.first.start.line &&
                                                       a.first.start.column > b.first.start.column);
  });

  for (const auto &[range, text] : sortedChanges) {
    file->applyEdit(range, text);
  }

  file->reparse();
  impl_->invalidateSemanticModel(std::string(uri));
  impl_->notifyDiagnosticsChanged(std::string(uri), file->getDiagnostics());
}

void LspService::didClose(std::string_view uri) {
  impl_->invalidateSemanticModel(std::string(uri));
  impl_->workspace_.closeFile(uri);
}

void LspService::didSave(std::string_view uri) {
  auto *file = impl_->workspace_.getFile(uri);
  if (file) {
    file->markSaved();
  }
}

// ============================================================================
// Hover
// ============================================================================

HoverResult LspService::hover(std::string_view uri, Position position) {
  HoverResult result;

  if (!impl_->config_.enableHover)
    return result;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return result;

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);
  LSP_LOG("Hover requested at pos(" << position.line << ", " << position.column
                                    << "), offset=" << offset);

  // Get AST and find node
  auto *ast = file->getAst();
  if (!ast) {
    LSP_LOG("Hover: ast is null");
    return result;
  }

  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid()) {
    LSP_LOG("Hover: findNodeAt returned invalid");
    return result;
  }

  // Get semantic model
  auto *model = impl_->getSemanticModel(file);

  // Build hover content based on node type
  ast::AstNode *node = findResult.node();
  LSP_LOG("Hover: found node kind=" << ast::astKindToString(node->kind));

  // Special handling for CallExprNode - check if offset is in callee range
  if (auto *callExpr = ast::ast_cast<ast::CallExprNode>(node)) {
    LSP_LOG("Hover: node is CallExprNode");
    LSP_LOG("Hover: callExpr->callee kind=" << ast::astKindToString(callExpr->callee->kind));
    LSP_LOG("Hover: callExpr->callee range=[" << callExpr->callee->range.begin.offset << "-"
                                              << callExpr->callee->range.end.offset << "]");

    // Check if offset is in callee range
    if (offset >= callExpr->callee->range.begin.offset &&
        offset < callExpr->callee->range.end.offset) {
      LSP_LOG("Hover: offset in callee range, looking up function symbol");

      if (model) {
        semantic::Symbol *calleeSym = model->getResolvedSymbol(callExpr->callee);
        LSP_LOG("Hover: calleeSym=" << (void *)calleeSym);

        if (calleeSym) {
          LSP_LOG("Hover: calleeSym name=" << calleeSym->name()
                                           << ", kind=" << (int)calleeSym->kind());

          // Handle ImportSymbol
          if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(calleeSym)) {
            LSP_LOG("Hover: calleeSym is ImportSymbol");
            LSP_LOG("Hover: importSym->modulePath()=" << importSym->modulePath());
            LSP_LOG("Hover: importSym->originalName()=" << importSym->originalName());

            // Re-resolve the import to avoid dangling pointer
            std::string modulePath = importSym->modulePath();
            std::string currentFilePath = file->path();
            std::string resolvedPath =
                impl_->workspace_.resolveModulePath(modulePath, currentFilePath);
            LSP_LOG("Hover: resolvedPath=" << resolvedPath);

            if (!resolvedPath.empty()) {
              SourceFile *targetFile = impl_->workspace_.openFileFromDisk(resolvedPath);
              LSP_LOG("Hover: targetFile=" << (void *)targetFile);

              if (targetFile) {
                semantic::SemanticModel *targetModel = impl_->getSemanticModel(targetFile);
                LSP_LOG("Hover: targetModel=" << (void *)targetModel);

                if (targetModel) {
                  std::string originalName = importSym->originalName();
                  LSP_LOG("Hover: looking for originalName='" << originalName << "'");

                  semantic::Symbol *exportedSym = targetModel->findExportedSymbol(originalName);
                  LSP_LOG("Hover: findExportedSymbol result=" << (void *)exportedSym);

                  if (exportedSym) {
                    LSP_LOG("Hover: exportedSym name=" << exportedSym->name()
                                                       << ", kind=" << (int)exportedSym->kind());
                    calleeSym = exportedSym;
                  }
                }
              }
            } else {
              // Fallback to stored targetSymbol
              semantic::Symbol *targetSym = importSym->targetSymbol();
              LSP_LOG("Hover: fallback targetSym=" << (void *)targetSym);

              if (targetSym) {
                LSP_LOG("Hover: targetSym name=" << targetSym->name()
                                                 << ", kind=" << (int)targetSym->kind());
                calleeSym = targetSym;
              }
            }
          }

          result.contents = createHoverMarkdown(calleeSym, calleeSym->type());
          result.range = file->toRange(callExpr->callee->range);
          LSP_LOG("Hover: returning hover content from CallExprNode callee");
          return result;
        }
      }
    }
  }

  if (auto *ident = ast::ast_cast<ast::IdentifierNode>(node)) {
    LSP_LOG("Hover: node is IdentifierNode");
    // Look up symbol
    if (model) {
      if (auto *sym = model->getResolvedSymbol(node)) {
        LSP_LOG("Hover: found resolved symbol " << sym->name() << ", kind=" << (int)sym->kind());

        // Handle ImportSymbol specially - re-resolve to avoid dangling pointer
        if (sym->kind() == semantic::SymbolKind::Import) {
          auto *importSym = static_cast<const semantic::ImportSymbol *>(sym);
          const std::string &modulePath = importSym->modulePath();
          const std::string &originalName = importSym->originalName();
          const std::string &importName = importSym->name();

          LSP_LOG("Hover: ImportSymbol - modulePath=" << modulePath << ", originalName='"
                                                      << originalName << "', importName='"
                                                      << importName << "'");

          // Check if this is a namespace import (originalName is empty)
          // For namespace import like "import * as utils from 'utils'", originalName is empty
          // For named import like "import { foo } from 'm'", originalName is 'foo'
          if (originalName.empty()) {
            // This is a namespace import like "import * as utils from 'utils'"
            LSP_LOG("Hover: namespace import detected");
            std::string resolvedPath =
                impl_->workspace_.resolveModulePath(modulePath, file->path());
            LSP_LOG("Hover: namespace import - resolvedPath=" << resolvedPath);

            if (!resolvedPath.empty()) {
              SourceFile *targetFile = impl_->workspace_.openFileFromDisk(resolvedPath);
              if (targetFile) {
                result.contents = "```lang\nnamespace " + importName + "\n```";
                result.range = file->toRange(node->range);
                LSP_LOG("Hover: returning namespace import info");
                return result;
              }
            }
          }

          // For named imports (import { foo } from 'm'), re-resolve to the actual target symbol
          // The importName is the local name (e.g., 'foo' after import)
          // We need to find the actual function/type in the target module
          std::string resolvedPath = impl_->workspace_.resolveModulePath(modulePath, file->path());
          LSP_LOG("Hover: named import - resolvedPath=" << resolvedPath);

          // Use openFileFromDisk instead of getFileByPath since the file may not be loaded yet
          SourceFile *targetFile = impl_->workspace_.openFileFromDisk(resolvedPath);
          LSP_LOG("Hover: targetFile=" << (void *)targetFile);

          if (targetFile) {
            semantic::SemanticModel *targetModel = impl_->getSemanticModel(targetFile);
            LSP_LOG("Hover: targetModel=" << (void *)targetModel);

            if (targetModel) {
              // Try to find the symbol by originalName (the name in the source module)
              semantic::Symbol *targetSym = targetModel->findExportedSymbol(originalName);
              LSP_LOG("Hover: findExportedSymbol('" << originalName
                                                    << "') result=" << (void *)targetSym);

              if (targetSym) {
                LSP_LOG("Hover: re-resolved target symbol " << targetSym->name()
                                                            << ", kind=" << (int)targetSym->kind());
                result.contents = createHoverMarkdown(targetSym, targetSym->type());
                result.range = file->toRange(node->range);
                return result;
              } else {
                LSP_LOG("Hover: findExportedSymbol returned null");
                // Try using importName as fallback (for aliased imports like import { foo as bar })
                if (importName != originalName) {
                  targetSym = targetModel->findExportedSymbol(importName);
                  LSP_LOG("Hover: findExportedSymbol('"
                          << importName << "') fallback result=" << (void *)targetSym);
                  if (targetSym) {
                    LSP_LOG("Hover: re-resolved target symbol (fallback) "
                            << targetSym->name() << ", kind=" << (int)targetSym->kind());
                    result.contents = createHoverMarkdown(targetSym, targetSym->type());
                    result.range = file->toRange(node->range);
                    return result;
                  }
                }
              }
            }
          } else {
            LSP_LOG("Hover: openFileFromDisk returned null for path: " << resolvedPath);
          }
          LSP_LOG("Hover: failed to re-resolve import symbol");
        }

        result.contents = createHoverMarkdown(sym, sym->type());
        result.range = file->toRange(node->range);
      } else if (auto *defSym = model->getDefiningSymbol(node)) {
        LSP_LOG("Hover: found defining symbol " << defSym->name());
        result.contents = createHoverMarkdown(defSym, defSym->type());
        result.range = file->toRange(node->range);
      } else {
        LSP_LOG("Hover: no symbol found for identifier");
      }
    }
  } else if (auto *varDecl = ast::ast_cast<ast::VarDeclNode>(node)) {
    LSP_LOG("Hover: node is VarDeclNode");
    if (model) {
      if (auto *defSym = model->getDefiningSymbol(node)) {
        LSP_LOG("Hover: found defining symbol " << defSym->name());
        result.contents = createHoverMarkdown(defSym, defSym->type());
        result.range = file->toRange(node->range);
      } else {
        LSP_LOG("Hover: no symbol found for vardecl");
      }
    }
  } else if (auto *inferredType = ast::ast_cast<ast::InferredTypeNode>(node)) {
    LSP_LOG("Hover: node is InferredTypeNode");
    // Handle 'auto' keyword - find the parent VarDecl and get the inferred type
    if (model) {
      auto *parent = findResult.context.parent();
      if (parent) {
        LSP_LOG("Hover: parent kind=" << ast::astKindToString(parent->kind));
      } else {
        LSP_LOG("Hover: parent is null");
      }
      if (auto *varDecl = ast::ast_cast<ast::VarDeclNode>(parent)) {
        // The type of the VarDeclNode itself is the inferred type
        types::TypeRef varType = model->getNodeType(varDecl);
        if (varType) {
          LSP_LOG("Hover: varType=" << varType->toString());
        } else {
          LSP_LOG("Hover: varType is null");
        }
        if (varType && !varType->isUnknown()) {
          result.contents = "```lang\n" + varType->toString() + "\n```";
          result.range = file->toRange(inferredType->range);
        } else {
          // Fallback to symbol type if node type is unknown
          if (auto *sym = model->getDefiningSymbol(varDecl)) {
            result.contents = "```lang\n" + sym->type()->toString() + "\n```";
            result.range = file->toRange(inferredType->range);
          }
        }
      }
    }
  } else if (auto *literal = ast::ast_cast<ast::IntLiteralNode>(node)) {
    result.contents = "```lang\nint\n```\nValue: " + std::to_string(literal->value);
    result.range = file->toRange(node->range);
  } else if (auto *literal = ast::ast_cast<ast::FloatLiteralNode>(node)) {
    std::ostringstream oss;
    oss << std::setprecision(15) << literal->value;
    result.contents = "```lang\nfloat\n```\nValue: " + oss.str();
    result.range = file->toRange(node->range);
  } else if (auto *literal = ast::ast_cast<ast::StringLiteralNode>(node)) {
    result.contents = "```lang\nstring\n```";
    result.range = file->toRange(node->range);
  } else if (auto *literal = ast::ast_cast<ast::BoolLiteralNode>(node)) {
    result.contents =
        std::string("```lang\nbool\n```\nValue: ") + (literal->value ? "true" : "false");
    result.range = file->toRange(node->range);
  } else if (auto *memberAccess = ast::ast_cast<ast::MemberAccessExprNode>(node)) {
    // Handle hover on member access (e.g., abc.value or calc.add)
    LSP_LOG("Hover: MemberAccessExpr, memberRange=[" << memberAccess->memberRange.begin.offset
                                                     << "-" << memberAccess->memberRange.end.offset
                                                     << "], offset=" << offset);
    LSP_LOG("Hover: memberAccess->base kind=" << ast::astKindToString(memberAccess->base->kind));
    LSP_LOG("Hover: memberAccess->member id=" << memberAccess->member.id);

    // Check if offset is within memberRange (the member name part)
    if (memberAccess->memberRange.isValid() && offset >= memberAccess->memberRange.begin.offset &&
        offset < memberAccess->memberRange.end.offset) {
      LSP_LOG("Hover: offset in memberRange, looking up member type");
      LSP_LOG("Hover: model=" << (void *)model);
      if (model) {
        // Get the type of the base expression
        LSP_LOG("Hover: memberAccess->base=" << (void *)memberAccess->base);
        types::TypeRef baseType = model->getNodeType(memberAccess->base);
        LSP_LOG("Hover: baseType=" << (baseType ? baseType->toString() : "null"));
        LSP_LOG("Hover: baseType->isUnknown()=" << (baseType ? baseType->isUnknown() : false));
        LSP_LOG("Hover: baseType->isClass()=" << (baseType ? baseType->isClass() : false));

        // Check if base is an ImportSymbol (namespace import)
        semantic::Symbol *baseSym = model->getResolvedSymbol(memberAccess->base);
        LSP_LOG("Hover: baseSym=" << (void *)baseSym);
        if (baseSym && baseSym->kind() == semantic::SymbolKind::Import) {
          LSP_LOG("Hover: base is ImportSymbol, trying namespace member lookup");
          auto *importSym = static_cast<const semantic::ImportSymbol *>(baseSym);
          bool isNamespaceImport =
              importSym->originalName().empty() || importSym->originalName() == importSym->name();
          LSP_LOG("Hover: isNamespaceImport=" << isNamespaceImport);

          if (isNamespaceImport) {
            // Get member name
            std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
            std::string memberName(memberNameView);
            LSP_LOG("Hover: namespace memberName='" << memberName << "'");

            // Resolve the import path
            std::string modulePath = importSym->modulePath();
            std::string resolvedPath =
                impl_->workspace_.resolveModulePath(modulePath, file->path());
            LSP_LOG("Hover: resolvedPath=" << resolvedPath);

            if (!resolvedPath.empty()) {
              SourceFile *targetFile = impl_->workspace_.openFileFromDisk(resolvedPath);
              LSP_LOG("Hover: targetFile=" << (void *)targetFile);

              if (targetFile) {
                semantic::SemanticModel *targetModel = impl_->getSemanticModel(targetFile);
                LSP_LOG("Hover: targetModel=" << (void *)targetModel);

                if (targetModel) {
                  semantic::Symbol *memberSym = targetModel->findExportedSymbol(memberName);
                  LSP_LOG("Hover: findExportedSymbol result=" << (void *)memberSym);

                  if (memberSym) {
                    LSP_LOG("Hover: memberSym name=" << memberSym->name()
                                                     << ", kind=" << (int)memberSym->kind());
                    result.contents = createHoverMarkdown(memberSym, memberSym->type());
                    result.range = file->toRange(memberAccess->memberRange);
                    LSP_LOG("Hover: returning namespace member hover");
                    return result;
                  }
                }
              }
            }
          }
        }

        if (baseType && !baseType->isUnknown()) {
          LSP_LOG("Hover: baseType=" << baseType->toString());
          // Try to find the member in the base type
          if (auto *classType = dynamic_cast<const types::ClassType *>(baseType.get())) {
            LSP_LOG("Hover: baseType is ClassType, name=" << classType->name());
            LSP_LOG("Hover: classType->fields().size()=" << classType->fields().size());
            LSP_LOG("Hover: classType->methods().size()=" << classType->methods().size());

            // Look for the member in the class
            std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
            std::string memberName(memberNameView);
            LSP_LOG("Hover: memberName='" << memberName << "'");

            // First try to find field
            bool found = false;
            LSP_LOG("Hover: searching for field...");
            for (const auto &field : classType->fields()) {
              LSP_LOG("Hover: checking field '" << field.name << "'");
              if (field.name == memberName) {
                LSP_LOG("Hover: found field " << field.name);
                result.contents = "```lang\n" + field.type->toString() + "\n```";
                result.range = file->toRange(memberAccess->memberRange);
                found = true;
                break;
              }
            }

            // If not found, try to find method
            if (!found) {
              LSP_LOG("Hover: field not found, searching for method...");
              for (const auto &method : classType->methods()) {
                LSP_LOG("Hover: checking method '" << method.name << "'");
                if (method.name == memberName) {
                  LSP_LOG("Hover: found method " << method.name);
                  // Show method signature: methodName(param1: type1, ...) -> returnType
                  std::string sig = memberName + "(";
                  if (method.type && method.type->isFunction()) {
                    auto *funcType = static_cast<const types::FunctionType *>(method.type.get());
                    for (size_t i = 0; i < funcType->paramTypes().size(); ++i) {
                      if (i > 0)
                        sig += ", ";
                      sig += funcType->paramTypes()[i]->toString();
                    }
                    sig += ")";
                    if (funcType->returnType()) {
                      sig += " -> " + funcType->returnType()->toString();
                    }
                  }
                  result.contents = "```lang\n" + sig + "\n```";
                  result.range = file->toRange(memberAccess->memberRange);
                  found = true;
                  break;
                }
              }
            }

            // Also try to get from ClassSymbol for imported classes
            if (!found) {
              semantic::Symbol *baseSym = model->getResolvedSymbol(memberAccess->base);
              if (baseSym && baseSym->isVariable()) {
                types::TypeRef varType = baseSym->type();
                if (varType && varType->isClass()) {
                  semantic::Symbol *typeSym =
                      model->symbolTable().globalScope()->resolve(classType->name());
                  if (typeSym) {
                    if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(typeSym)) {
                      typeSym = importSym->targetSymbol();
                    }
                    if (typeSym && typeSym->isClass()) {
                      auto *classSym = static_cast<semantic::ClassSymbol *>(typeSym);
                      // Try field
                      if (semantic::FieldSymbol *fieldSym = classSym->findField(memberName)) {
                        LSP_LOG("Hover: found field from ClassSymbol " << fieldSym->name());
                        result.contents = "```lang\n" + fieldSym->type()->toString() + "\n```";
                        result.range = file->toRange(memberAccess->memberRange);
                        found = true;
                      }
                      // Try method
                      if (!found) {
                        if (semantic::MethodSymbol *methodSym = classSym->findMethod(memberName)) {
                          LSP_LOG("Hover: found method from ClassSymbol " << methodSym->name());
                          // Show method signature with parameter names
                          std::string sig = memberName + "(";
                          bool first = true;
                          for (auto *param : methodSym->parameters()) {
                            if (!first)
                              sig += ", ";
                            first = false;
                            sig += param->name();
                            if (param->type()) {
                              sig += ": " + param->type()->toString();
                            }
                          }
                          sig += ")";
                          if (methodSym->returnType()) {
                            sig += " -> " + methodSym->returnType()->toString();
                          }
                          result.contents = "```lang\n" + sig + "\n```";
                          result.range = file->toRange(memberAccess->memberRange);
                          found = true;
                        }
                      }
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  } else if (auto *qualifiedId = ast::ast_cast<ast::QualifiedIdentifierNode>(node)) {
    // Handle hover on qualified identifier (e.g., Inner in "var abc: Inner")
    LSP_LOG("Hover: QualifiedIdentifier, parts=" << qualifiedId->parts.size());
    LSP_LOG("Hover: model=" << (void *)model);
    if (model) {
      // Find which part the offset is in
      for (size_t i = 0; i < qualifiedId->partsRange.size(); ++i) {
        const auto &partRange = qualifiedId->partsRange[i];
        LSP_LOG("Hover: checking partsRange[" << i << "] = [" << partRange.begin.offset << "-"
                                              << partRange.end.offset << "], offset=" << offset);
        if (partRange.isValid() && offset >= partRange.begin.offset &&
            offset < partRange.end.offset) {
          LSP_LOG("Hover: offset in partsRange[" << i << "]");
          // Get the type of this qualified identifier
          types::TypeRef type = model->getNodeType(qualifiedId);
          if (type && !type->isUnknown()) {
            LSP_LOG("Hover: type=" << type->toString());
            result.contents = "```lang\n" + type->toString() + "\n```";
            result.range = file->toRange(partRange);
          }
          break;
        }
      }
    }
  } else if (auto *multiVar = ast::ast_cast<ast::MultiVarDeclNode>(findResult.node())) {
    // Handle hover on multi-variable declaration (e.g., x in "vars x, y, z")
    LSP_LOG("Hover: MultiVarDecl, names=" << multiVar->names.size());
    if (model) {
      // Find which name the offset is in
      for (size_t i = 0; i < multiVar->nameRanges.size(); ++i) {
        const auto &nameRange = multiVar->nameRanges[i];
        LSP_LOG("Hover: nameRanges[" << i << "] range=[" << nameRange.begin.offset << "-"
                                     << nameRange.end.offset << "], offset=" << offset);
        if (nameRange.isValid() && offset >= nameRange.begin.offset &&
            offset < nameRange.end.offset) {
          LSP_LOG("Hover: offset in nameRanges[" << i << "]");
          // Get the variable name
          std::string varName(file->factory().strings().get(multiVar->names[i]));
          LSP_LOG("Hover: varName='" << varName << "'");
          // Look up the symbol
          semantic::Symbol *sym = model->symbolTable().globalScope()->resolve(varName);
          LSP_LOG("Hover: resolved symbol=" << (void *)sym);
          if (sym) {
            result.contents = createHoverMarkdown(sym, sym->type());
            result.range = file->toRange(nameRange);
          }
          break;
        }
      }
    }
  }

  return result;
}

// ============================================================================
// Completion
// ============================================================================

CompletionResult LspService::completion(std::string_view uri, Position position,
                                        std::optional<char> triggerCharacter) {
  LSP_LOG_SEP("completion");
  LSP_LOG("uri=" << uri << ", position=" << position.line << ":" << position.column);
  if (triggerCharacter) {
    LSP_LOG("triggerCharacter='" << *triggerCharacter << "'");
  }

  CompletionResult result;

  if (!impl_->config_.enableCompletion) {
    LSP_LOG("completion disabled in config");
    return result;
  }

  auto *file = impl_->workspace_.getFile(uri);
  if (!file) {
    LSP_LOG("file not found");
    return result;
  }

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);
  LSP_LOG("offset=" << offset);

  auto *ast = file->getAst();
  if (!ast) {
    LSP_LOG("ast is null");
    return result;
  }
  LSP_LOG("ast=" << (void *)ast << ", statements.size()=" << ast->statements.size());

  auto *model = impl_->getSemanticModel(file);
  LSP_LOG("model=" << (void *)model);

  // Get completion context
  NodeFinder finder(ast);
  auto completionCtx = finder.getCompletionContext(offset);
  LSP_LOG("completionCtx.trigger=" << static_cast<int>(completionCtx.trigger));
  LSP_LOG("completionCtx.enclosingNode=" << (void *)completionCtx.enclosingNode);
  if (completionCtx.enclosingNode) {
    LSP_LOG("  enclosingNode.kind=" << ast::astKindToString(completionCtx.enclosingNode->kind));
  }
  LSP_LOG("completionCtx.baseExpr=" << (void *)completionCtx.baseExpr);

  switch (completionCtx.trigger) {
  case CompletionTrigger::DotAccess:
  case CompletionTrigger::ColonAccess: {
    LSP_LOG("case: DotAccess/ColonAccess");
    // Member completion
    if (model && completionCtx.baseExpr) {
      types::TypeRef baseType = model->getNodeType(completionCtx.baseExpr);
      impl_->addMemberCompletions(result.items, baseType,
                                  completionCtx.trigger == CompletionTrigger::ColonAccess);
    }
    break;
  }

  case CompletionTrigger::TypeAnnotation: {
    LSP_LOG("case: TypeAnnotation");
    // Type completion
    impl_->addTypeCompletions(result.items);

    // Add class types
    if (model) {
      semantic::Scope *scope = model->symbolTable().globalScope();
      for (auto *sym : scope->allVisibleSymbols()) {
        if (sym->isClass()) {
          CompletionItem item;
          item.label = sym->name();
          item.kind = CompletionItemKind::Class;
          item.insertText = sym->name();
          result.items.push_back(std::move(item));
        }
      }
    }
    break;
  }

  case CompletionTrigger::Import: {
    LSP_LOG("case: Import");
    // Module completion - would need filesystem access
    // For now, just provide empty
    break;
  }

  case CompletionTrigger::Argument: {
    LSP_LOG("case: Argument");
    // 在函数调用参数位置，提供作用域内的变量补全
    if (model) {
      semantic::Scope *scope = nullptr;

      // 先尝试从包围节点获取作用域
      if (completionCtx.enclosingNode) {
        scope = model->getNodeScope(completionCtx.enclosingNode);
        LSP_LOG("Argument: scope from enclosingNode=" << (void *)scope);
      }

      // 如果没有找到，尝试从调用表达式的上下文找
      if (!scope) {
        NodeFinder finder(ast);
        auto findResult = finder.findNodeAt(offset);
        if (findResult.valid()) {
          // 尝试找到包含当前位置的函数声明
          auto *funcDecl = findResult.context.findAncestor<ast::FunctionDeclNode>();
          if (funcDecl) {
            scope = model->getNodeScope(funcDecl);
            LSP_LOG("Argument: scope from FunctionDecl=" << (void *)scope);
          }
          // 也尝试找 BlockStmt
          if (!scope) {
            auto *blockStmt = findResult.context.findAncestor<ast::BlockStmtNode>();
            if (blockStmt) {
              scope = model->getNodeScope(blockStmt);
              LSP_LOG("Argument: scope from BlockStmt=" << (void *)scope);
            }
          }
        }
      }

      // 最后使用全局作用域
      if (!scope) {
        scope = model->symbolTable().globalScope();
        LSP_LOG("Argument: using globalScope=" << (void *)scope);
      }

      if (scope) {
        LSP_LOG("Argument: scope has " << scope->allVisibleSymbols().size() << " visible symbols");
        for (auto *sym : scope->allVisibleSymbols()) {
          LSP_LOG("  symbol: " << sym->name() << " kind=" << static_cast<int>(sym->kind()));
        }
      }

      impl_->addScopeCompletions(result.items, scope, impl_->config_.maxCompletionItems);
    }

    // 在参数位置也添加关键字（如 true, false, null, new 等）
    impl_->addKeywordCompletions(result.items);
    break;
  }

  case CompletionTrigger::Identifier:
  case CompletionTrigger::None:
  default: {
    LSP_LOG("case: Identifier/None/default");
    // Scope-based completion
    if (model) {
      semantic::Scope *scope = nullptr;
      if (completionCtx.enclosingNode) {
        scope = model->getNodeScope(completionCtx.enclosingNode);
        LSP_LOG("scope from enclosingNode=" << (void *)scope);
      }
      if (!scope) {
        scope = model->symbolTable().globalScope();
        LSP_LOG("using globalScope=" << (void *)scope);
      }
      if (scope) {
        LSP_LOG("scope has " << scope->allVisibleSymbols().size() << " visible symbols");
        for (auto *sym : scope->allVisibleSymbols()) {
          LSP_LOG("  symbol: " << sym->name() << " kind=" << static_cast<int>(sym->kind()));
        }
      }
      impl_->addScopeCompletions(result.items, scope, impl_->config_.maxCompletionItems);
    } else {
      LSP_LOG("model is null, skipping scope completions");
    }

    // Add keywords
    impl_->addKeywordCompletions(result.items);
    break;
  }
  }

  LSP_LOG("result.items.size()=" << result.items.size());
  for (size_t i = 0; i < result.items.size() && i < 10; ++i) {
    LSP_LOG("  item[" << i << "]: " << result.items[i].label);
  }

  // Limit results
  if (result.items.size() > impl_->config_.maxCompletionItems) {
    result.items.resize(impl_->config_.maxCompletionItems);
    result.isIncomplete = true;
  }

  return result;
}

CompletionItem LspService::resolveCompletion(const CompletionItem &item) {
  // For now, just return the item as-is
  // Could be extended to add more documentation
  return item;
}

// ============================================================================
// Signature Help
// ============================================================================

SignatureHelp LspService::signatureHelp(std::string_view uri, Position position) {
  SignatureHelp result;

  if (!impl_->config_.enableSignatureHelp)
    return result;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return result;

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);

  auto *ast = file->getAst();
  if (!ast)
    return result;

  auto *model = impl_->getSemanticModel(file);
  if (!model)
    return result;

  // Find call expression
  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid())
    return result;

  // Look for enclosing call expression
  auto *callExpr = findResult.context.findAncestor<ast::CallExprNode>();
  
  // If no CallExprNode found, try to find identifier before cursor
  // This handles incomplete calls like "add(" where parser hasn't created CallExprNode yet
  ast::IdentifierNode *calleeIdent = nullptr;
  if (!callExpr) {
    // Check if the found node is an identifier (the function name being typed)
    calleeIdent = ast::ast_cast<ast::IdentifierNode>(findResult.context.node);
    if (!calleeIdent) {
      // Check if parent is an identifier
      for (auto it = findResult.context.ancestors.rbegin(); 
           it != findResult.context.ancestors.rend(); ++it) {
        if (auto *ident = ast::ast_cast<ast::IdentifierNode>(*it)) {
          calleeIdent = ident;
          break;
        }
      }
    }
    
    // Still not found? Try to find identifier by looking at source code
    // This handles cases like "add(" where the parser didn't create a CallExprNode
    if (!calleeIdent) {
      // Get source code and look for identifier before '('
      std::string_view source = file->contentView();
      if (offset > 0 && offset <= source.size()) {
        // Look backwards for '('
        uint32_t parenPos = offset;
        while (parenPos > 0 && (source[parenPos - 1] == ' ' || source[parenPos - 1] == '\t')) {
          parenPos--;
        }
        if (parenPos > 0 && source[parenPos - 1] == '(') {
          // Found '(', now look for identifier before it
          uint32_t identEnd = parenPos - 1;
          while (identEnd > 0 && (source[identEnd - 1] == ' ' || source[identEnd - 1] == '\t')) {
            identEnd--;
          }
          uint32_t identStart = identEnd;
          while (identStart > 0 && (std::isalnum(source[identStart - 1]) || source[identStart - 1] == '_')) {
            identStart--;
          }
          if (identStart < identEnd) {
            // Find the identifier node at identStart
            auto identResult = finder.findNodeAt(identStart);
            if (identResult.valid()) {
              calleeIdent = ast::ast_cast<ast::IdentifierNode>(identResult.context.node);
            }
          }
        }
      }
    }
  }

  if (!callExpr && !calleeIdent)
    return result;

  // Get function type and declaration
  types::TypeRef funcType;
  ast::FunctionDeclNode *funcDecl = nullptr;
  ast::MethodDeclNode *methodDecl = nullptr;
  
  if (callExpr) {
    funcType = model->getNodeType(callExpr->callee);
    // Get the function declaration from symbol
    auto *funcSymbol = model->findSymbolAt(callExpr->range.begin);
    if (funcSymbol && funcSymbol->astNode()) {
      funcDecl = ast::ast_cast<ast::FunctionDeclNode>(funcSymbol->astNode());
      methodDecl = ast::ast_cast<ast::MethodDeclNode>(funcSymbol->astNode());
    }
  } else if (calleeIdent) {
    // For incomplete call, look up the identifier's type
    funcType = model->getNodeType(calleeIdent);
    // Try to get the function declaration from resolved symbol
    auto *symbol = model->getResolvedSymbol(calleeIdent);
    if (symbol && symbol->astNode()) {
      funcDecl = ast::ast_cast<ast::FunctionDeclNode>(symbol->astNode());
      methodDecl = ast::ast_cast<ast::MethodDeclNode>(symbol->astNode());
    }
  }
  
  if (!funcType || !funcType->isFunction())
    return result;

  auto *ft = static_cast<const types::FunctionType *>(funcType.get());

  // Build signature
  SignatureInformation sig;

  std::string label = "(";
  const auto &paramTypes = ft->paramTypes();
  
  // Get StringTable for parameter name lookup
  const ast::StringTable &strings = file->factory().stringTable();
  
  // Get parameter names from declaration if available
  auto getParamName = [&](size_t i) -> std::string {
    if (funcDecl && i < funcDecl->parameters.size()) {
      auto *param = funcDecl->parameters[i];
      if (param && param->name.isValid()) {
        return std::string(strings.get(param->name));
      }
    }
    if (methodDecl && i < methodDecl->parameters.size()) {
      auto *param = methodDecl->parameters[i];
      if (param && param->name.isValid()) {
        return std::string(strings.get(param->name));
      }
    }
    return "";
  };
  
  for (size_t i = 0; i < paramTypes.size(); ++i) {
    if (i > 0)
      label += ", ";

    ParameterInformation param;
    std::string paramName = getParamName(i);
    std::string paramType = paramTypes[i] ? paramTypes[i]->toString() : "any";
    
    if (!paramName.empty()) {
      param.label = paramName + ": " + paramType;
    } else {
      param.label = paramType;
    }
    sig.parameters.push_back(std::move(param));

    label += sig.parameters.back().label;
  }

  if (ft->isVariadic()) {
    if (!paramTypes.empty())
      label += ", ";
    label += "...";
  }

  label += ")";
  if (ft->returnType()) {
    label += " -> " + ft->returnType()->toString();
  }

  sig.label = label;
  result.signatures.push_back(std::move(sig));

  // Determine active parameter
  result.activeSignature = 0;
  if (paramTypes.empty()) {
    result.activeParameter = 0;
  } else {
    result.activeParameter = static_cast<uint32_t>(
        std::min(static_cast<size_t>(findResult.completion.argumentIndex), paramTypes.size() - 1));
  }
  return result;
}

// ============================================================================
// Go to Definition
// ============================================================================

std::vector<LocationLink> LspService::definition(std::string_view uri, Position position) {
  std::vector<LocationLink> result;

  LSP_LOG_SEP("definition");
  LSP_LOG("Definition: uri=" << uri << ", position=(" << position.line << ", " << position.column
                             << ")");

  if (!impl_->config_.enableDefinition) {
    LSP_LOG("Definition: disabled in config");
    return result;
  }

  auto *file = impl_->workspace_.getFile(uri);
  if (!file) {
    LSP_LOG("Definition: file not found");
    return result;
  }

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);
  LSP_LOG("Definition: offset=" << offset);

  auto *ast = file->getAst();
  if (!ast) {
    LSP_LOG("Definition: ast is null");
    return result;
  }

  auto *model = impl_->getSemanticModel(file);
  if (!model) {
    LSP_LOG("Definition: model is null");
    return result;
  }

  // Find node at position
  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid()) {
    LSP_LOG("Definition: findResult is invalid");
    return result;
  }

  LSP_LOG("Definition: findResult.node() kind=" << ast::astKindToString(findResult.node()->kind));
  LSP_LOG("Definition: findResult.node() range=[" << findResult.node()->range.begin.offset << "-"
                                                  << findResult.node()->range.end.offset << "]");

  // Special handling for CallExprNode - check if offset is in callee range
  if (auto *callExpr = ast::ast_cast<ast::CallExprNode>(findResult.node())) {
    LSP_LOG("Definition: node is CallExprNode");
    LSP_LOG("Definition: callExpr->callee kind=" << ast::astKindToString(callExpr->callee->kind));
    LSP_LOG("Definition: callExpr->callee range=[" << callExpr->callee->range.begin.offset << "-"
                                                   << callExpr->callee->range.end.offset << "]");

    // Check if offset is in callee range
    if (offset >= callExpr->callee->range.begin.offset &&
        offset < callExpr->callee->range.end.offset) {
      LSP_LOG("Definition: offset IS in callee range, looking up function definition");

      // Try to get the resolved symbol from the callee
      semantic::Symbol *calleeSym = model->getResolvedSymbol(callExpr->callee);
      LSP_LOG("Definition: calleeSym=" << (void *)calleeSym);

      if (calleeSym) {
        LSP_LOG("Definition: calleeSym name=" << calleeSym->name()
                                              << ", kind=" << (int)calleeSym->kind());

        // Handle ImportSymbol
        if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(calleeSym)) {
          LSP_LOG("Definition: calleeSym is ImportSymbol");
          semantic::Symbol *targetSym = importSym->targetSymbol();
          LSP_LOG("Definition: targetSym=" << (void *)targetSym);

          if (targetSym) {
            LSP_LOG("Definition: targetSym name=" << targetSym->name()
                                                  << ", kind=" << (int)targetSym->kind());
            calleeSym = targetSym;
          }
        }

        // Find the file containing this symbol
        SourceFile *targetFile = nullptr;
        impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
          if (!targetFile) {
            semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
            if (targetModel) {
              for (const auto &[name, sym] : targetModel->exportedSymbols()) {
                if (sym == calleeSym) {
                  targetFile = &f;
                  LSP_LOG("Definition: found target file in exportedSymbols: " << f.uri());
                  return;
                }
              }
              // Also check all symbols
              for (const auto &symPtr : targetModel->symbolTable().allSymbols()) {
                if (symPtr.get() == calleeSym) {
                  targetFile = &f;
                  LSP_LOG("Definition: found target file in allSymbols: " << f.uri());
                  return;
                }
              }
            }
          }
        });

        if (targetFile) {
          ast::SourceLoc defLoc = calleeSym->definitionLoc();
          LSP_LOG("Definition: defLoc valid=" << defLoc.isValid() << ", offset=" << defLoc.offset);

          if (defLoc.isValid()) {
            Position defPos = targetFile->getPosition(defLoc.offset);
            LocationLink link;
            link.targetUri = targetFile->uri();
            link.targetRange = Range{defPos, defPos};
            link.targetSelectionRange = link.targetRange;
            link.originSelectionRange = file->toRange(callExpr->callee->range);
            result.push_back(std::move(link));
            LSP_LOG("Definition: returning location from CallExprNode callee");
            return result;
          }
        }
      }
    }
  }

  // Special handling for MemberAccessExpr - check if offset is in memberRange
  if (auto *memberAccess = ast::ast_cast<ast::MemberAccessExprNode>(findResult.node())) {
    LSP_LOG("Definition: node is MemberAccessExprNode");
    LSP_LOG("Definition: memberRange=[" << memberAccess->memberRange.begin.offset << "-"
                                        << memberAccess->memberRange.end.offset
                                        << "], valid=" << memberAccess->memberRange.isValid());
    LSP_LOG("Definition: member id=" << memberAccess->member.id);
    LSP_LOG("Definition: offset=" << offset << ", checking if offset in memberRange");

    if (memberAccess->memberRange.isValid() && offset >= memberAccess->memberRange.begin.offset &&
        offset < memberAccess->memberRange.end.offset) {
      LSP_LOG("Definition: offset IS in memberRange, looking up field definition");
      LSP_LOG("Definition: base node kind=" << ast::astKindToString(memberAccess->base->kind));

      // Get the type of the base expression
      types::TypeRef baseType = model->getNodeType(memberAccess->base);
      LSP_LOG("Definition: baseType=" << (baseType ? baseType->toString() : "null"));

      if (baseType && !baseType->isUnknown()) {
        LSP_LOG("Definition: baseType is valid, checking if class");
        LSP_LOG("Definition: baseType->isClass()=" << baseType->isClass());

        // First, check if base is a namespace import (ImportSymbol)
        semantic::Symbol *baseSym = model->getResolvedSymbol(memberAccess->base);
        LSP_LOG("Definition: baseSym=" << (void *)baseSym);

        if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(baseSym)) {
          LSP_LOG("Definition: base is ImportSymbol, name=" << importSym->name());
          LSP_LOG("Definition: importSym->modulePath()=" << importSym->modulePath());
          LSP_LOG("Definition: importSym->originalName()=" << importSym->originalName());

          // Check if this is a namespace import
          bool isNamespaceImport =
              importSym->originalName().empty() || importSym->originalName() == importSym->name();
          LSP_LOG("Definition: isNamespaceImport=" << isNamespaceImport);

          if (isNamespaceImport) {
            // Get member name
            std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
            std::string memberName(memberNameView);
            LSP_LOG("Definition: namespace memberName='" << memberName << "'");

            // Resolve the import path
            std::string modulePath = importSym->modulePath();
            std::string currentFilePath = file->path();
            std::string absolutePath;
            if (modulePath.size() >= 2 && (modulePath[0] == '/' || modulePath[1] == ':')) {
              absolutePath = modulePath;
            } else {
              size_t lastSlash = currentFilePath.find_last_of("/\\");
              std::string baseDir =
                  (lastSlash != std::string::npos) ? currentFilePath.substr(0, lastSlash) : ".";
              absolutePath = baseDir;
              if (!absolutePath.empty() && absolutePath.back() != '/' &&
                  absolutePath.back() != '\\') {
                absolutePath += '/';
              }
              absolutePath += modulePath;
              if (absolutePath.size() < 4 ||
                  absolutePath.substr(absolutePath.size() - 4) != ".spt") {
                absolutePath += ".spt";
              }
            }

            LSP_LOG("Definition: resolved absolutePath=" << absolutePath);

            // Open the target file
            SourceFile *targetFile = impl_->workspace_.openFileFromDisk(absolutePath);
            LSP_LOG("Definition: targetFile=" << (void *)targetFile);

            if (targetFile) {
              semantic::SemanticModel *targetModel = impl_->getSemanticModel(targetFile);
              LSP_LOG("Definition: targetModel=" << (void *)targetModel);

              if (targetModel) {
                // Look for the member in the target module
                semantic::Symbol *memberSym = targetModel->findExportedSymbol(memberName);
                LSP_LOG("Definition: findExportedSymbol result=" << (void *)memberSym);

                if (memberSym) {
                  LSP_LOG("Definition: memberSym name=" << memberSym->name()
                                                        << ", kind=" << (int)memberSym->kind());

                  ast::SourceLoc defLoc = memberSym->definitionLoc();
                  LSP_LOG("Definition: defLoc valid=" << defLoc.isValid()
                                                      << ", offset=" << defLoc.offset);

                  if (defLoc.isValid()) {
                    Position defPos = targetFile->getPosition(defLoc.offset);
                    LocationLink link;
                    link.targetUri = targetFile->uri();
                    link.targetRange = Range{defPos, defPos};
                    link.targetSelectionRange = link.targetRange;
                    link.originSelectionRange = file->toRange(memberAccess->memberRange);
                    result.push_back(std::move(link));
                    LSP_LOG("Definition: returning result from namespace import member");
                    return result;
                  }
                }
              }
            }
          }
        }

        if (auto *classType = dynamic_cast<const types::ClassType *>(baseType.get())) {
          LSP_LOG("Definition: baseType is ClassType, name=" << classType->name());

          std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
          std::string memberName(memberNameView);
          LSP_LOG("Definition: memberName='" << memberName
                                             << "' (from id=" << memberAccess->member.id << ")");

          // Log all fields in the class
          LSP_LOG("Definition: class has " << classType->fields().size() << " fields:");
          for (const auto &f : classType->fields()) {
            LSP_LOG("  - field: '" << f.name << "'");
          }

          // Find the field in the class type
          const types::FieldInfo *fieldInfo = classType->findField(memberName);
          LSP_LOG("Definition: findField result=" << (void *)fieldInfo);

          // Also try to get the ClassSymbol for the base expression
          // For imported classes, the ClassType may not have field info, but ClassSymbol does
          semantic::Symbol *baseSym = model->getResolvedSymbol(memberAccess->base);
          LSP_LOG("Definition: baseSym=" << (void *)baseSym);

          semantic::ClassSymbol *classSym = nullptr;
          if (baseSym) {
            LSP_LOG("Definition: baseSym kind=" << (int)baseSym->kind());
            // If it's a variable, get its type's class symbol
            if (baseSym->isVariable()) {
              types::TypeRef varType = baseSym->type();
              LSP_LOG("Definition: varType=" << (varType ? varType->toString() : "null"));
              if (varType && varType->isClass()) {
                // Find the ClassSymbol by name
                semantic::Symbol *typeSym =
                    model->symbolTable().globalScope()->resolve(classType->name());
                LSP_LOG("Definition: typeSym=" << (void *)typeSym);
                if (typeSym) {
                  // If it's an import symbol, get the target
                  if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(typeSym)) {
                    typeSym = importSym->targetSymbol();
                    LSP_LOG("Definition: typeSym from import=" << (void *)typeSym);
                  }
                  if (typeSym && typeSym->isClass()) {
                    classSym = static_cast<semantic::ClassSymbol *>(typeSym);
                    LSP_LOG("Definition: found ClassSymbol with " << classSym->fields().size() << " fields, "
                            << classSym->methods().size() << " methods");
                  }
                }
              }
            }
          }

          // Try to find method if field not found
          semantic::MethodSymbol *methodSym = nullptr;
          if (!fieldInfo && classSym) {
            methodSym = classSym->findMethod(memberName);
            LSP_LOG("Definition: findMethod result=" << (void *)methodSym);
          }

          if (fieldInfo) {
            LSP_LOG("Definition: found field " << fieldInfo->name);

            // Find the ClassSymbol to get the FieldSymbol with definition location
            // First, find the class declaration node
            ast::ClassDeclNode *classDecl = nullptr;
            LSP_LOG("Definition: searching for class declaration in current file...");
            for (size_t i = 0; i < ast->statements.size(); ++i) {
              if (auto *declStmt = ast::ast_cast<ast::DeclStmtNode>(ast->statements[i])) {
                if (auto *cd = ast::ast_cast<ast::ClassDeclNode>(declStmt->decl)) {
                  if (cd->name.isValid()) {
                    auto className = file->factory().strings().get(cd->name);
                    LSP_LOG("Definition: found class '" << className << "'");
                    if (className == classType->name()) {
                      classDecl = cd;
                      LSP_LOG("Definition: matched class declaration");
                      break;
                    }
                  }
                }
              }
            }

            if (classDecl) {
              LSP_LOG("Definition: classDecl found, searching for field '" << memberName
                                                                           << "' in class");
              LSP_LOG("Definition: classDecl has " << classDecl->fields.size() << " fields");
              // Find the field declaration in the class
              for (auto *fieldDecl : classDecl->fields) {
                auto fieldName = file->factory().strings().get(fieldDecl->name);
                LSP_LOG("Definition: checking field '" << fieldName << "'");
                if (fieldName == memberName) {
                  LSP_LOG("Definition: found field declaration at line "
                          << fieldDecl->range.begin.line);
                  LocationLink link;
                  link.targetUri = std::string(uri);
                  link.targetRange = file->toRange(fieldDecl->range);
                  // 使用 nameRange 作为选择范围，如果没有则使用整个 range
                  link.targetSelectionRange = fieldDecl->nameRange.isValid()
                                                  ? file->toRange(fieldDecl->nameRange)
                                                  : link.targetRange;
                  link.originSelectionRange = file->toRange(memberAccess->memberRange);
                  result.push_back(std::move(link));
                  LSP_LOG("Definition: returning result with 1 location");
                  return result;
                }
              }
              LSP_LOG("Definition: field not found in classDecl");
            } else {
              LSP_LOG("Definition: classDecl is null, class might be imported from another file");
              // TODO: Handle imported class - need to find the class in its source file
            }
          } else {
            LSP_LOG("Definition: fieldInfo is null, field not found in ClassType");

            // Try to find the field from ClassSymbol (for imported classes)
            if (classSym) {
              LSP_LOG("Definition: trying ClassSymbol, has " << classSym->fields().size()
                                                             << " fields");
              for (auto *fld : classSym->fields()) {
                LSP_LOG("Definition: ClassSymbol field '" << fld->name() << "'");
              }

              semantic::FieldSymbol *fieldSym = classSym->findField(memberName);
              LSP_LOG("Definition: ClassSymbol::findField result=" << (void *)fieldSym);

              if (fieldSym) {
                LSP_LOG("Definition: found FieldSymbol '" << fieldSym->name() << "'");

                // Get the FieldDeclNode from the symbol
                ast::FieldDeclNode *fieldDecl =
                    ast::ast_cast<ast::FieldDeclNode>(fieldSym->astNode());
                LSP_LOG("Definition: fieldDecl=" << (void *)fieldDecl);

                // Find the file containing this class
                SourceFile *targetFile = nullptr;
                impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
                  if (!targetFile) {
                    semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
                    if (targetModel) {
                      for (const auto &[name, sym] : targetModel->exportedSymbols()) {
                        if (sym == classSym) {
                          targetFile = &f;
                          return;
                        }
                      }
                    }
                  }
                });

                if (!targetFile) {
                  // Try to find by checking all ClassSymbols in all models
                  impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
                    if (!targetFile) {
                      semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
                      if (targetModel) {
                        for (const auto &symPtr : targetModel->symbolTable().allSymbols()) {
                          if (symPtr.get() == classSym) {
                            targetFile = &f;
                            return;
                          }
                        }
                      }
                    }
                  });
                }

                LSP_LOG("Definition: targetFile=" << (targetFile ? targetFile->path() : "null"));

                if (targetFile && fieldDecl) {
                  // Use nameRange if available, otherwise use the full range
                  ast::SourceRange targetRange =
                      fieldDecl->nameRange.isValid() ? fieldDecl->nameRange : fieldDecl->range;
                  LSP_LOG("Definition: targetRange=[" << targetRange.begin.offset << "-"
                                                      << targetRange.end.offset << "]");

                  Position defStartPos = targetFile->getPosition(targetRange.begin.offset);
                  Position defEndPos = targetFile->getPosition(targetRange.end.offset);
                  LocationLink link;
                  link.targetUri = targetFile->uri();
                  link.targetRange = Range{defStartPos, defEndPos};
                  link.targetSelectionRange = link.targetRange;
                  link.originSelectionRange = file->toRange(memberAccess->memberRange);
                  result.push_back(std::move(link));
                  LSP_LOG("Definition: returning result with 1 location from ClassSymbol");
                  return result;
                }
              } else if (methodSym) {
                LSP_LOG("Definition: found MethodSymbol '" << methodSym->name() << "'");

                // Get the MethodDeclNode from the symbol
                ast::MethodDeclNode *methodDecl =
                    ast::ast_cast<ast::MethodDeclNode>(methodSym->astNode());
                LSP_LOG("Definition: methodDecl=" << (void *)methodDecl);

                // Find the file containing this class (needed for both cases)
                SourceFile *targetFile = nullptr;
                impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
                  if (!targetFile) {
                    semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
                    if (targetModel) {
                      for (const auto &[name, sym] : targetModel->exportedSymbols()) {
                        if (sym == classSym) {
                          targetFile = &f;
                          return;
                        }
                      }
                    }
                  }
                });

                if (!targetFile) {
                  impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
                    if (!targetFile) {
                      semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
                      if (targetModel) {
                        for (const auto &symPtr : targetModel->symbolTable().allSymbols()) {
                          if (symPtr.get() == classSym) {
                            targetFile = &f;
                            return;
                          }
                        }
                      }
                    }
                  });
                }

                if (!targetFile) {
                  targetFile = file;
                }

                LSP_LOG("Definition: targetFile=" << (targetFile ? targetFile->path() : "null"));

                if (methodDecl) {
                  // Use the method name range for selection
                  ast::SourceRange targetRange =
                      methodDecl->nameRange.isValid() ? methodDecl->nameRange : methodDecl->range;
                  LSP_LOG("Definition: targetRange=[" << targetRange.begin.offset << "-"
                                                      << targetRange.end.offset << "]");

                  Position defStartPos = targetFile->getPosition(targetRange.begin.offset);
                  Position defEndPos = targetFile->getPosition(targetRange.end.offset);
                  LocationLink link;
                  link.targetUri = targetFile->uri();
                  link.targetRange = Range{defStartPos, defEndPos};
                  link.targetSelectionRange = link.targetRange;
                  link.originSelectionRange = file->toRange(memberAccess->memberRange);
                  result.push_back(std::move(link));
                  LSP_LOG("Definition: returning result with 1 location from MethodSymbol");
                  return result;
                } else {
                  // Fallback: use definitionLoc from the symbol
                  ast::SourceLoc defLoc = methodSym->definitionLoc();
                  LSP_LOG("Definition: methodDecl is null, using definitionLoc, valid="
                          << defLoc.isValid() << ", offset=" << defLoc.offset);

                  if (defLoc.isValid()) {
                    Position defPos = targetFile->getPosition(defLoc.offset);
                    LocationLink link;
                    link.targetUri = targetFile->uri();
                    link.targetRange = Range{defPos, defPos};
                    link.targetSelectionRange = link.targetRange;
                    link.originSelectionRange = file->toRange(memberAccess->memberRange);
                    result.push_back(std::move(link));
                    LSP_LOG("Definition: returning result using definitionLoc");
                    return result;
                  }
                }
              } else {
                LSP_LOG("Definition: fieldSym and methodSym both null");
              }
            } else {
              LSP_LOG("Definition: classSym is null");
            }
          }
        } else {
          LSP_LOG("Definition: baseType is not a ClassType");
        }
      } else {
        LSP_LOG("Definition: baseType is null or unknown");

        // Even if baseType is null, check if base is a namespace import
        semantic::Symbol *baseSym = model->getResolvedSymbol(memberAccess->base);
        LSP_LOG("Definition: baseSym (fallback)=" << (void *)baseSym);

        if (baseSym && baseSym->kind() == semantic::SymbolKind::Import) {
          LSP_LOG("Definition: base is ImportSymbol despite null baseType");
          auto *importSym = static_cast<const semantic::ImportSymbol *>(baseSym);
          bool isNamespaceImport =
              importSym->originalName().empty() || importSym->originalName() == importSym->name();
          LSP_LOG("Definition: isNamespaceImport=" << isNamespaceImport);

          if (isNamespaceImport) {
            std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
            std::string memberName(memberNameView);
            LSP_LOG("Definition: namespace memberName='" << memberName << "'");

            std::string modulePath = importSym->modulePath();
            std::string currentFilePath = file->path();
            std::string absolutePath;
            if (modulePath.size() >= 2 && (modulePath[0] == '/' || modulePath[1] == ':')) {
              absolutePath = modulePath;
            } else {
              size_t lastSlash = currentFilePath.find_last_of("/\\");
              std::string baseDir =
                  (lastSlash != std::string::npos) ? currentFilePath.substr(0, lastSlash) : ".";
              absolutePath = baseDir;
              if (!absolutePath.empty() && absolutePath.back() != '/' &&
                  absolutePath.back() != '\\') {
                absolutePath += '/';
              }
              absolutePath += modulePath;
              if (absolutePath.size() < 4 ||
                  absolutePath.substr(absolutePath.size() - 4) != ".spt") {
                absolutePath += ".spt";
              }
            }

            LSP_LOG("Definition: resolved absolutePath=" << absolutePath);
            SourceFile *targetFile = impl_->workspace_.openFileFromDisk(absolutePath);
            LSP_LOG("Definition: targetFile=" << (void *)targetFile);

            if (targetFile) {
              semantic::SemanticModel *targetModel = impl_->getSemanticModel(targetFile);
              LSP_LOG("Definition: targetModel=" << (void *)targetModel);

              if (targetModel) {
                semantic::Symbol *memberSym = targetModel->findExportedSymbol(memberName);
                LSP_LOG("Definition: findExportedSymbol result=" << (void *)memberSym);

                if (memberSym) {
                  LSP_LOG("Definition: memberSym name=" << memberSym->name()
                                                        << ", kind=" << (int)memberSym->kind());

                  ast::SourceLoc defLoc = memberSym->definitionLoc();
                  LSP_LOG("Definition: defLoc valid=" << defLoc.isValid()
                                                      << ", offset=" << defLoc.offset);

                  if (defLoc.isValid()) {
                    Position defPos = targetFile->getPosition(defLoc.offset);
                    LocationLink link;
                    link.targetUri = targetFile->uri();
                    link.targetRange = Range{defPos, defPos};
                    link.targetSelectionRange = link.targetRange;
                    link.originSelectionRange = file->toRange(memberAccess->memberRange);
                    result.push_back(std::move(link));
                    LSP_LOG("Definition: returning result from namespace import (null baseType "
                            "fallback)");
                    return result;
                  }
                }
              }
            }
          }
        }
      }
    } else {
      LSP_LOG("Definition: offset NOT in memberRange");
    }
  } else if (auto *qualifiedId = ast::ast_cast<ast::QualifiedIdentifierNode>(findResult.node())) {
    // Handle definition on qualified identifier (e.g., Inner in "var abc: Inner")
    LSP_LOG("Definition: QualifiedIdentifier, parts=" << qualifiedId->parts.size());
    if (model) {
      // First check if we have a resolved symbol (for imported types)
      semantic::Symbol *resolvedSym = model->getResolvedSymbol(qualifiedId);
      LSP_LOG("Definition: resolvedSymbol=" << (void *)resolvedSym);
      if (resolvedSym) {
        LSP_LOG("Definition: resolvedSymbol kind=" << (int)resolvedSym->kind()
                                                   << ", isClass=" << resolvedSym->isClass());
        // Find the file containing this symbol
        SourceFile *targetFile = nullptr;
        impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
          if (!targetFile) {
            semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
            if (targetModel) {
              for (const auto &[name, sym] : targetModel->exportedSymbols()) {
                if (sym == resolvedSym) {
                  targetFile = &f;
                  return;
                }
              }
            }
          }
        });

        if (targetFile) {
          // Find which part the offset is in for originSelectionRange
          for (size_t i = 0; i < qualifiedId->partsRange.size(); ++i) {
            const auto &partRange = qualifiedId->partsRange[i];
            if (partRange.isValid() && offset >= partRange.begin.offset &&
                offset < partRange.end.offset) {
              LSP_LOG("Definition: found target file " << targetFile->path());
              LocationLink link;
              link.targetUri = targetFile->uri();
              ast::SourceLoc defLoc = resolvedSym->definitionLoc();
              if (defLoc.isValid()) {
                Position defPos = targetFile->getPosition(defLoc.offset);
                link.targetRange = Range{defPos, defPos};
              } else {
                link.targetRange = Range{{0, 0}, {0, 0}};
              }
              link.targetSelectionRange = link.targetRange;
              link.originSelectionRange = file->toRange(partRange);
              result.push_back(std::move(link));
              return result;
            }
          }
        }
      }

      // Fallback: Find which part the offset is in
      for (size_t i = 0; i < qualifiedId->partsRange.size(); ++i) {
        const auto &partRange = qualifiedId->partsRange[i];
        if (partRange.isValid() && offset >= partRange.begin.offset &&
            offset < partRange.end.offset) {
          LSP_LOG("Definition: offset in partsRange[" << i << "]");
          // Get the type of this qualified identifier
          types::TypeRef type = model->getNodeType(qualifiedId);
          if (type && !type->isUnknown()) {
            LSP_LOG("Definition: type=" << type->toString());
            // If it's a class type, find its declaration
            if (auto *classType = dynamic_cast<const types::ClassType *>(type.get())) {
              // Find the class declaration in AST
              for (size_t j = 0; j < ast->statements.size(); ++j) {
                if (auto *declStmt = ast::ast_cast<ast::DeclStmtNode>(ast->statements[j])) {
                  if (auto *classDecl = ast::ast_cast<ast::ClassDeclNode>(declStmt->decl)) {
                    if (classDecl->name.isValid()) {
                      auto className = file->factory().strings().get(classDecl->name);
                      if (className == classType->name()) {
                        LSP_LOG("Definition: found class declaration at line "
                                << classDecl->range.begin.line);
                        LocationLink link;
                        link.targetUri = std::string(uri);
                        link.targetRange = file->toRange(classDecl->range);
                        link.targetSelectionRange = link.targetRange;
                        link.originSelectionRange = file->toRange(partRange);
                        result.push_back(std::move(link));
                        return result;
                      }
                    }
                  }
                }
              }
            }
          }
          break;
        }
      }
    }
  } else if (auto *importStmt = ast::ast_cast<ast::ImportStmtNode>(findResult.node())) {
    // Handle definition on import specifier (e.g., Inner in "import { Inner } from ...")
    LSP_LOG("Definition: ImportStmt, specifiers=" << importStmt->specifiers.size());
    if (model) {
      // Find which specifier the offset is in
      for (size_t i = 0; i < importStmt->specifiers.size(); ++i) {
        const auto &spec = importStmt->specifiers[i];
        LSP_LOG("Definition: specifier[" << i << "] range=[" << spec.range.begin.offset << "-"
                                         << spec.range.end.offset << "], offset=" << offset);
        if (spec.range.isValid() && offset >= spec.range.begin.offset &&
            offset < spec.range.end.offset) {
          LSP_LOG("Definition: offset in specifier[" << i << "]");
          // Get the import symbol for this specifier
          std::string specName(file->factory().strings().get(spec.alias));
          semantic::Symbol *sym = model->symbolTable().globalScope()->resolve(specName);
          LSP_LOG("Definition: resolved symbol=" << (void *)sym);
          if (sym) {
            // If it's an import symbol, get the target
            if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(sym)) {
              LSP_LOG("Definition: found ImportSymbol, targetSymbol="
                      << (void *)importSym->targetSymbol());
              if (semantic::Symbol *targetSym = importSym->targetSymbol()) {
                // Find the file containing the target symbol
                SourceFile *targetFile = nullptr;
                impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
                  if (!targetFile) {
                    semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
                    if (targetModel) {
                      for (const auto &[name, s] : targetModel->exportedSymbols()) {
                        if (s == targetSym) {
                          targetFile = &f;
                          return;
                        }
                      }
                    }
                  }
                });

                if (targetFile) {
                  LSP_LOG("Definition: found target file " << targetFile->path());
                  LocationLink link;
                  link.targetUri = targetFile->uri();
                  ast::SourceLoc defLoc = targetSym->definitionLoc();
                  if (defLoc.isValid()) {
                    Position defPos = targetFile->getPosition(defLoc.offset);
                    link.targetRange = Range{defPos, defPos};
                  } else {
                    link.targetRange = Range{{0, 0}, {0, 0}};
                  }
                  link.targetSelectionRange = link.targetRange;
                  link.originSelectionRange = file->toRange(spec.range);
                  result.push_back(std::move(link));
                  return result;
                }
              }
            }
          }
          break;
        }
      }
    }
  }

  // Look up symbol
  LSP_LOG("Definition: Looking up symbol for node kind="
          << ast::astKindToString(findResult.node()->kind));
  semantic::Symbol *sym = model->getResolvedSymbol(findResult.node());
  LSP_LOG("Definition: getResolvedSymbol returned=" << (void *)sym);

  if (!sym) {
    LSP_LOG("Definition: sym is null, trying to find symbol in other ways");

    // Try to find the symbol by name in the global scope
    if (auto *ident = ast::ast_cast<ast::IdentifierNode>(findResult.node())) {
      std::string_view nameView = file->factory().strings().get(ident->name);
      std::string name(nameView);
      LSP_LOG("Definition: identifier name='" << name << "'");

      semantic::Symbol *globalSym = model->symbolTable().globalScope()->resolve(name);
      LSP_LOG("Definition: globalScope resolve result=" << (void *)globalSym);

      if (globalSym) {
        LSP_LOG("Definition: globalSym name=" << globalSym->name()
                                              << ", kind=" << (int)globalSym->kind());
        sym = globalSym;
      }
    }

    if (!sym) {
      LSP_LOG("Definition: still no symbol found, returning empty result");
      return result;
    }
  }

  // Handle ImportSymbol - check if it has a target symbol
  if (auto *importSym = semantic::symbol_cast<semantic::ImportSymbol>(sym)) {
    LSP_LOG("Definition: symbol is ImportSymbol, name=" << sym->name());
    LSP_LOG("Definition: importSym->modulePath()=" << importSym->modulePath());
    LSP_LOG("Definition: importSym->originalName()=" << importSym->originalName());

    // Check if this is a namespace import (originalName is empty or same as name)
    bool isNamespaceImport =
        importSym->originalName().empty() || importSym->originalName() == importSym->name();
    LSP_LOG("Definition: isNamespaceImport=" << isNamespaceImport);

    semantic::Symbol *targetSym = importSym->targetSymbol();
    LSP_LOG("Definition: targetSymbol=" << (void *)targetSym);

    // Resolve the import path to find the target file
    std::string modulePath = importSym->modulePath();
    std::string currentFilePath = file->path();

    // Resolve relative path
    std::string absolutePath;
    if (modulePath.size() >= 2 && (modulePath[0] == '/' || modulePath[1] == ':')) {
      absolutePath = modulePath;
    } else {
      size_t lastSlash = currentFilePath.find_last_of("/\\");
      std::string baseDir =
          (lastSlash != std::string::npos) ? currentFilePath.substr(0, lastSlash) : ".";
      absolutePath = baseDir;
      if (!absolutePath.empty() && absolutePath.back() != '/' && absolutePath.back() != '\\') {
        absolutePath += '/';
      }
      absolutePath += modulePath;
      if (absolutePath.size() < 4 || absolutePath.substr(absolutePath.size() - 4) != ".spt") {
        absolutePath += ".spt";
      }
    }

    LSP_LOG("Definition: resolved absolutePath=" << absolutePath);

    // Open the target file
    SourceFile *targetFile = impl_->workspace_.openFileFromDisk(absolutePath);
    LSP_LOG("Definition: targetFile=" << (void *)targetFile);

    if (targetFile) {
      LSP_LOG("Definition: targetFile->uri()=" << targetFile->uri());

      // Get semantic model for target file
      semantic::SemanticModel *targetModel = impl_->getSemanticModel(targetFile);
      LSP_LOG("Definition: targetModel=" << (void *)targetModel);

      if (targetModel) {
        // For namespace import, check if there's a MemberAccessExpr context
        // The user might be clicking on namespace.member
        std::string searchName = importSym->originalName();

        // If this is a namespace import and we have a MemberAccessExpr,
        // try to get the member name from context
        if (isNamespaceImport) {
          auto *memberAccess = ast::ast_cast<ast::MemberAccessExprNode>(findResult.node());
          if (!memberAccess) {
            // Try to find it in the AST path
            const auto &ancestors = findResult.context.ancestors;
            for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
              memberAccess = ast::ast_cast<ast::MemberAccessExprNode>(*it);
              if (memberAccess)
                break;
            }
          }

          if (memberAccess) {
            // Get member name from the AST
            std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
            searchName = std::string(memberNameView);
            LSP_LOG("Definition: namespace import - looking for member '" << searchName << "'");
          } else {
            // No member access context, just use the import name
            searchName = importSym->name();
          }
        }

        LSP_LOG("Definition: looking for symbol '" << searchName << "' in target model");

        // Log all exported symbols
        LSP_LOG("Definition: targetModel->exportedSymbols().size()="
                << targetModel->exportedSymbols().size());
        for (const auto &[expName, expSym] : targetModel->exportedSymbols()) {
          LSP_LOG("Definition: exported symbol '" << expName << "', kind=" << (int)expSym->kind());
        }

        semantic::Symbol *exportedSym = targetModel->findExportedSymbol(searchName);
        LSP_LOG("Definition: findExportedSymbol result=" << (void *)exportedSym);

        if (exportedSym) {
          LSP_LOG("Definition: exportedSym name=" << exportedSym->name()
                                                  << ", kind=" << (int)exportedSym->kind());

          ast::SourceLoc defLoc = exportedSym->definitionLoc();
          LSP_LOG("Definition: defLoc valid=" << defLoc.isValid() << ", offset=" << defLoc.offset);

          if (defLoc.isValid()) {
            Position defPos = targetFile->getPosition(defLoc.offset);
            LocationLink link;
            link.targetUri = targetFile->uri();
            link.targetRange = Range{defPos, defPos};
            link.targetSelectionRange = link.targetRange;
            link.originSelectionRange = file->toRange(findResult.node()->range);
            result.push_back(std::move(link));
            LSP_LOG("Definition: returning cross-file location for namespace import member");
            return result;
          }
        }

        // If namespace import and member not found, return the module file itself
        if (isNamespaceImport) {
          LSP_LOG("Definition: namespace import member not found, returning module file");
          Position startPos = targetFile->getPosition(0);
          LocationLink link;
          link.targetUri = targetFile->uri();
          link.targetRange = Range{startPos, startPos};
          link.targetSelectionRange = link.targetRange;
          link.originSelectionRange = file->toRange(findResult.node()->range);
          result.push_back(std::move(link));
          LSP_LOG("Definition: returning module file for namespace import");
          return result;
        }

        // If namespace import failed, also try originalName as fallback
        if (isNamespaceImport && searchName != importSym->originalName()) {
          LSP_LOG("Definition: namespace member not found, trying originalName");
          semantic::Symbol *fallbackSym =
              targetModel->findExportedSymbol(importSym->originalName());
          if (fallbackSym) {
            ast::SourceLoc defLoc = fallbackSym->definitionLoc();
            if (defLoc.isValid()) {
              Position defPos = targetFile->getPosition(defLoc.offset);
              LocationLink link;
              link.targetUri = targetFile->uri();
              link.targetRange = Range{defPos, defPos};
              link.targetSelectionRange = link.targetRange;
              link.originSelectionRange = file->toRange(findResult.node()->range);
              result.push_back(std::move(link));
              LSP_LOG("Definition: returning cross-file location using originalName");
              return result;
            }
          }
        }
      }
    }

    // Fallback: try to use the stored targetSymbol if re-resolution failed
    if (targetSym) {
      LSP_LOG("Definition: fallback to stored targetSymbol name=" << targetSym->name() << ", kind="
                                                                  << (int)targetSym->kind());
      // Find the file containing the target symbol
      ast::SourceLoc targetLoc = targetSym->definitionLoc();
      LSP_LOG("Definition: targetLoc valid=" << targetLoc.isValid()
                                             << ", offset=" << targetLoc.offset);
      if (targetLoc.isValid()) {
        SourceFile *foundFile = nullptr;
        impl_->workspace_.forEachFile([&](const std::string &fileUri, SourceFile &f) {
          if (!foundFile) {
            semantic::SemanticModel *targetModel = impl_->getSemanticModel(&f);
            if (targetModel) {
              for (const auto &[name, sym] : targetModel->exportedSymbols()) {
                if (sym == targetSym) {
                  foundFile = &f;
                  LSP_LOG("Definition: found target file " << f.uri());
                  return;
                }
              }
            }
          }
        });

        if (foundFile) {
          LocationLink link;
          link.targetUri = foundFile->uri();
          Position targetPos = foundFile->getPosition(targetLoc.offset);
          link.targetRange = Range{targetPos, targetPos};
          link.targetSelectionRange = link.targetRange;
          link.originSelectionRange = file->toRange(findResult.node()->range);
          result.push_back(std::move(link));
          LSP_LOG("Definition: returning cross-file location (fallback)");
          return result;
        }
      }
    }
    LSP_LOG("Definition: ImportSymbol resolution failed, returning empty");
    return result;
  }

  // Create location link
  LocationLink link;
  link.targetUri = std::string(uri); // Same file for now

  // Convert definition location
  ast::SourceLoc defLoc = sym->definitionLoc();
  LSP_LOG("Definition: sym->name()=" << sym->name() << ", defLoc.offset=" << defLoc.offset
                                     << ", valid=" << defLoc.isValid());
  if (defLoc.isValid()) {
    Position defPos = file->getPosition(defLoc.offset);
    LSP_LOG("Definition: defPos line=" << defPos.line << ", column=" << defPos.column);
    link.targetRange = Range{defPos, defPos};
    link.targetSelectionRange = link.targetRange;
    link.originSelectionRange = file->toRange(findResult.node()->range);
    result.push_back(std::move(link));
  }

  return result;
}

std::vector<LocationLink> LspService::declaration(std::string_view uri, Position position) {
  // For this language, declaration and definition are the same
  return definition(uri, position);
}

std::vector<LocationLink> LspService::typeDefinition(std::string_view uri, Position position) {
  std::vector<LocationLink> result;

  if (!impl_->config_.enableDefinition)
    return result;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return result;

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);

  auto *ast = file->getAst();
  if (!ast)
    return result;

  auto *model = impl_->getSemanticModel(file);
  if (!model)
    return result;

  // Find node and get its type
  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid())
    return result;

  types::TypeRef type = model->getNodeType(findResult.node());
  if (!type || !type->isClass())
    return result;

  // Look up class symbol
  auto *classType = static_cast<const types::ClassType *>(type.get());
  semantic::Scope *globalScope = model->symbolTable().globalScope();
  semantic::Symbol *classSym = globalScope->resolve(classType->name());

  if (!classSym || !classSym->isClass())
    return result;

  // Create location link
  LocationLink link;
  link.targetUri = std::string(uri);

  ast::SourceLoc defLoc = classSym->definitionLoc();
  if (defLoc.isValid()) {
    Position defPos = file->getPosition(defLoc.offset);
    link.targetRange = Range{defPos, defPos};
    link.targetSelectionRange = link.targetRange;
    link.originSelectionRange = file->toRange(findResult.node()->range);
    result.push_back(std::move(link));
  }

  return result;
}

// ============================================================================
// Find References
// ============================================================================

std::vector<Location> LspService::references(std::string_view uri, Position position,
                                             bool includeDeclaration) {
  std::vector<Location> result;

  if (!impl_->config_.enableReferences)
    return result;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return result;

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);

  auto *ast = file->getAst();
  if (!ast)
    return result;

  auto *model = impl_->getSemanticModel(file);
  if (!model)
    return result;

  // Find symbol
  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid())
    return result;

  semantic::Symbol *sym = model->getResolvedSymbol(findResult.node());
  if (!sym) {
    sym = model->getDefiningSymbol(findResult.node());
  }
  if (!sym)
    return result;

  // Get all references
  auto refs = model->findReferences(sym);

  for (const auto &ref : refs) {
    if (!includeDeclaration && ref.begin == sym->definitionLoc()) {
      continue;
    }

    Location loc;
    loc.uri = std::string(uri);
    Position pos = file->getPosition(ref.begin.offset);
    loc.range = Range{pos, pos};
    result.push_back(std::move(loc));

    if (result.size() >= impl_->config_.maxReferences) {
      break;
    }
  }

  return result;
}

// ============================================================================
// Document Symbols
// ============================================================================

std::vector<DocumentSymbol> LspService::documentSymbols(std::string_view uri) {
  LSP_LOG_SEP("documentSymbols");
  LSP_LOG(">>> VERSION 3 - FIXED std::any_cast TYPE MISMATCH <<<");
  LSP_LOG("uri=" << uri);

  std::vector<DocumentSymbol> result;

  if (!impl_->config_.enableDocumentSymbols) {
    LSP_LOG("documentSymbols disabled");
    return result;
  }

  auto *file = impl_->workspace_.getFile(uri);
  if (!file) {
    LSP_LOG("file not found");
    return result;
  }

  auto *ast = file->getAst();
  if (!ast) {
    LSP_LOG("ast is null");
    return result;
  }

  LSP_LOG("calling collectDocumentSymbols");
  try {
    impl_->collectDocumentSymbols(ast, result, *file);
    LSP_LOG("collectDocumentSymbols completed, result.size()=" << result.size());
  } catch (const std::exception &e) {
    LSP_LOG("collectDocumentSymbols exception: " << e.what());
    throw;
  } catch (...) {
    LSP_LOG("collectDocumentSymbols unknown exception");
    throw;
  }

  return result;
}

// ============================================================================
// Workspace Symbols
// ============================================================================

std::vector<WorkspaceSymbol> LspService::workspaceSymbols(std::string_view query) {
  std::vector<WorkspaceSymbol> result;

  if (!impl_->config_.enableWorkspaceSymbols)
    return result;

  std::string queryLower(query);
  std::transform(queryLower.begin(), queryLower.end(), queryLower.begin(),
                 [](unsigned char c) { return std::tolower(c); });

  impl_->workspace_.forEachFile([&](const std::string &fileUri, const SourceFile &file) {
    auto *ast = const_cast<SourceFile &>(file).getAst();
    if (!ast)
      return;

    // Get semantic model
    auto *model = impl_->getSemanticModel(const_cast<SourceFile *>(&file));
    if (!model)
      return;

    // Search symbols
    for (const auto &symPtr : model->symbolTable().allSymbols()) {
      semantic::Symbol *sym = symPtr.get();

      // Filter by query
      std::string nameLower = sym->name();
      std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(),
                     [](unsigned char c) { return std::tolower(c); });

      if (nameLower.find(queryLower) == std::string::npos) {
        continue;
      }

      WorkspaceSymbol wsSym;
      wsSym.name = sym->name();
      wsSym.kind = toSymbolKind(sym->kind());
      wsSym.location.uri = fileUri;

      ast::SourceLoc defLoc = sym->definitionLoc();
      if (defLoc.isValid()) {
        Position pos = file.getPosition(defLoc.offset);
        wsSym.location.range = Range{pos, pos};
      }

      // Set container name
      if (sym->scope() && sym->scope()->parent()) {
        // Find enclosing class or function
        // Simplified - just note the scope kind
      }

      result.push_back(std::move(wsSym));
    }
  });

  return result;
}

// ============================================================================
// Rename
// ============================================================================

std::optional<WorkspaceEdit> LspService::rename(std::string_view uri, Position position,
                                                std::string_view newName) {
  if (!impl_->config_.enableRename)
    return std::nullopt;

  // First check if rename is valid
  auto range = prepareRename(uri, position);
  if (!range)
    return std::nullopt;

  auto refs = references(uri, position, true);
  if (refs.empty())
    return std::nullopt;

  WorkspaceEdit edit;

  for (const auto &ref : refs) {
    TextEdit textEdit;
    textEdit.range = ref.range;
    textEdit.newText = std::string(newName);
    edit.changes[ref.uri].push_back(std::move(textEdit));
  }

  return edit;
}

std::optional<Range> LspService::prepareRename(std::string_view uri, Position position) {
  if (!impl_->config_.enableRename)
    return std::nullopt;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return std::nullopt;

  // position is already 1-based (converted in from_json)
  uint32_t offset = file->getOffset(position);

  auto *ast = file->getAst();
  if (!ast)
    return std::nullopt;

  auto *model = impl_->getSemanticModel(file);
  if (!model)
    return std::nullopt;

  // Find identifier
  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid())
    return std::nullopt;

  // Must be an identifier
  if (!ast::ast_isa<ast::IdentifierNode>(findResult.node())) {
    return std::nullopt;
  }

  // Must resolve to a symbol
  semantic::Symbol *sym = model->getResolvedSymbol(findResult.node());
  if (!sym) {
    sym = model->getDefiningSymbol(findResult.node());
  }
  if (!sym)
    return std::nullopt;

  // Cannot rename built-in symbols
  if (sym->isBuiltin())
    return std::nullopt;

  return file->toRange(findResult.node()->range);
}

// ============================================================================
// Code Actions
// ============================================================================

std::vector<CodeAction> LspService::codeActions(std::string_view uri, Range range,
                                                const std::vector<Diagnostic> &diagnostics) {
  std::vector<CodeAction> result;

  if (!impl_->config_.enableCodeActions)
    return result;

  // Add quick fixes for diagnostics
  for (const auto &diag : diagnostics) {
    if (diag.code == "undefined-variable") {
      // Suggest declaring variable
      CodeAction action;
      action.title = "Declare variable '" + diag.message + "'";
      action.kind = CodeActionKind::QuickFix;
      action.diagnostics = {diag};
      action.isPreferred = true;
      result.push_back(std::move(action));
    } else if (diag.code == "unused-variable") {
      // Suggest removing or prefixing with underscore
      CodeAction action;
      action.title = "Remove unused variable";
      action.kind = CodeActionKind::QuickFix;
      action.diagnostics = {diag};
      result.push_back(std::move(action));

      CodeAction action2;
      action2.title = "Prefix with underscore";
      action2.kind = CodeActionKind::QuickFix;
      action2.diagnostics = {diag};
      result.push_back(std::move(action2));
    }
  }

  return result;
}

// ============================================================================
// Semantic Tokens
// ============================================================================

SemanticTokensResult LspService::semanticTokensFull(std::string_view uri) {
  SemanticTokensResult result;

  if (!impl_->config_.enableSemanticTokens)
    return result;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return result;

  auto *ast = file->getAst();
  if (!ast)
    return result;

  auto *model = impl_->getSemanticModel(file);

  std::vector<SemanticToken> tokens;
  impl_->collectSemanticTokens(ast, tokens, *file, model);

  result.data = impl_->encodeSemanticTokens(tokens);
  result.resultId = std::to_string(file->version());

  return result;
}

SemanticTokensResult LspService::semanticTokensDelta(std::string_view uri,
                                                     std::string_view previousResultId) {
  // For simplicity, just return full tokens
  // A real implementation would compute delta
  return semanticTokensFull(uri);
}

// ============================================================================
// Formatting
// ============================================================================

std::vector<TextEdit> LspService::formatting(std::string_view uri,
                                             const FormattingOptions &options) {
  std::vector<TextEdit> result;

  if (!impl_->config_.enableFormatting)
    return result;

  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return result;

  // Simple formatting: normalize indentation
  std::string_view content = file->contentView();
  std::string formatted;
  formatted.reserve(content.size());

  int indentLevel = 0;
  std::string indent(options.insertSpaces ? options.tabSize : 1, options.insertSpaces ? ' ' : '\t');

  size_t lineStart = 0;
  for (size_t i = 0; i <= content.size(); ++i) {
    if (i == content.size() || content[i] == '\n') {
      std::string_view line = content.substr(lineStart, i - lineStart);

      // Trim leading whitespace
      size_t firstNonSpace = line.find_first_not_of(" \t");
      if (firstNonSpace != std::string_view::npos) {
        std::string_view trimmedLine = line.substr(firstNonSpace);

        // Adjust indent level
        if (!trimmedLine.empty()) {
          if (trimmedLine[0] == '}' || trimmedLine[0] == ']') {
            if (indentLevel > 0)
              --indentLevel;
          }
        }

        // Add indentation
        for (int j = 0; j < indentLevel; ++j) {
          formatted += indent;
        }
        formatted += trimmedLine;

        // Adjust for next line
        if (!trimmedLine.empty()) {
          char last = trimmedLine.back();
          if (last == '{' || last == '[') {
            ++indentLevel;
          }
        }
      }

      if (i < content.size()) {
        formatted += '\n';
      }
      lineStart = i + 1;
    }
  }

  // Handle final newline
  if (options.insertFinalNewline && !formatted.empty() && formatted.back() != '\n') {
    formatted += '\n';
  }
  if (options.trimFinalNewlines) {
    while (formatted.size() > 1 && formatted[formatted.size() - 1] == '\n' &&
           formatted[formatted.size() - 2] == '\n') {
      formatted.pop_back();
    }
  }

  // Create single edit replacing entire document
  if (formatted != content) {
    TextEdit edit;
    edit.range = Range{Position{1, 1}, Position{file->lineCount() + 1, 1}};
    edit.newText = std::move(formatted);
    result.push_back(std::move(edit));
  }

  return result;
}

std::vector<TextEdit> LspService::rangeFormatting(std::string_view uri, Range range,
                                                  const FormattingOptions &options) {
  // For simplicity, just format entire document
  // A real implementation would format only the range
  return formatting(uri, options);
}

// ============================================================================
// Diagnostics
// ============================================================================

std::vector<Diagnostic> LspService::getDiagnostics(std::string_view uri) {
  auto *file = impl_->workspace_.getFile(uri);
  if (!file)
    return {};
  return file->getDiagnostics();
}

std::unordered_map<std::string, std::vector<Diagnostic>> LspService::getAllDiagnostics() {
  return impl_->workspace_.collectAllDiagnostics();
}

size_t LspService::onDiagnosticsChanged(
    std::function<void(const std::string &uri, const std::vector<Diagnostic> &)> callback) {
  std::lock_guard<std::mutex> lock(impl_->callbacksMutex_);
  size_t id = impl_->nextCallbackId_++;
  impl_->diagnosticsCallbacks_[id] = std::move(callback);
  return id;
}

void LspService::removeDiagnosticsCallback(size_t id) {
  std::lock_guard<std::mutex> lock(impl_->callbacksMutex_);
  impl_->diagnosticsCallbacks_.erase(id);
}

// ============================================================================
// Workspace Management
// ============================================================================

Workspace &LspService::workspace() noexcept { return impl_->workspace_; }

const Workspace &LspService::workspace() const noexcept { return impl_->workspace_; }

void LspService::addWorkspaceFolder(std::string_view uri) {
  // Could track multiple workspace folders
  // For now, just update root if not set
  if (impl_->workspace_.rootPath().empty()) {
    impl_->workspace_.setRootPath(uri::uriToPath(uri));
  }
}

void LspService::removeWorkspaceFolder(std::string_view uri) {
  // Close all files from this folder
  std::string path = uri::uriToPath(uri);
  auto openUris = impl_->workspace_.getOpenFileUris();
  for (const auto &fileUri : openUris) {
    std::string filePath = uri::uriToPath(fileUri);
    if (filePath.starts_with(path)) {
      didClose(fileUri);
    }
  }
}

// ============================================================================
// Utility Functions
// ============================================================================

SymbolKind toSymbolKind(semantic::SymbolKind kind) noexcept {
  switch (kind) {
  case semantic::SymbolKind::Variable:
    return SymbolKind::Variable;
  case semantic::SymbolKind::Parameter:
    return SymbolKind::Variable;
  case semantic::SymbolKind::Function:
    return SymbolKind::Function;
  case semantic::SymbolKind::Class:
    return SymbolKind::Class;
  case semantic::SymbolKind::Field:
    return SymbolKind::Field;
  case semantic::SymbolKind::Method:
    return SymbolKind::Method;
  case semantic::SymbolKind::Import:
    return SymbolKind::Module;
  case semantic::SymbolKind::Namespace:
    return SymbolKind::Namespace;
  default:
    return SymbolKind::Variable;
  }
}

CompletionItemKind toCompletionItemKind(semantic::SymbolKind kind) noexcept {
  switch (kind) {
  case semantic::SymbolKind::Variable:
    return CompletionItemKind::Variable;
  case semantic::SymbolKind::Parameter:
    return CompletionItemKind::Variable;
  case semantic::SymbolKind::Function:
    return CompletionItemKind::Function;
  case semantic::SymbolKind::Class:
    return CompletionItemKind::Class;
  case semantic::SymbolKind::Field:
    return CompletionItemKind::Field;
  case semantic::SymbolKind::Method:
    return CompletionItemKind::Method;
  case semantic::SymbolKind::Import:
    return CompletionItemKind::Module;
  case semantic::SymbolKind::Namespace:
    return CompletionItemKind::Module;
  default:
    return CompletionItemKind::Text;
  }
}

std::string formatType(types::TypeRef type) {
  if (!type)
    return "unknown";
  return type->toString();
}

std::string formatSymbolSignature(const semantic::Symbol *symbol, int depth) {
  if (!symbol)
    return "";

  // Prevent infinite recursion
  if (depth > 10) {
    return symbol->name();
  }

  std::string result;

  switch (symbol->kind()) {
  case semantic::SymbolKind::Variable: {
    auto *var = static_cast<const semantic::VariableSymbol *>(symbol);
    if (var->isGlobal())
      result += "global ";
    if (var->isConst())
      result += "const ";
    result += symbol->name();
    if (symbol->type()) {
      result += ": " + symbol->type()->toString();
    }
    break;
  }

  case semantic::SymbolKind::Parameter: {
    auto *param = static_cast<const semantic::ParameterSymbol *>(symbol);
    result = symbol->name();
    if (symbol->type()) {
      result += ": " + symbol->type()->toString();
    }
    break;
  }

  case semantic::SymbolKind::Function: {
    auto *func = static_cast<const semantic::FunctionSymbol *>(symbol);
    result = "function " + symbol->name() + "(";
    bool first = true;
    for (auto *param : func->parameters()) {
      if (!first)
        result += ", ";
      first = false;
      result += param->name();
      if (param->type()) {
        result += ": " + param->type()->toString();
      }
    }
    result += ")";
    if (func->returnType()) {
      result += " -> " + func->returnType()->toString();
    }
    break;
  }

  case semantic::SymbolKind::Class:
    result = "class " + symbol->name();
    break;

  case semantic::SymbolKind::Field: {
    auto *field = static_cast<const semantic::FieldSymbol *>(symbol);
    if (field->isStatic())
      result += "static ";
    result += symbol->name();
    if (symbol->type()) {
      result += ": " + symbol->type()->toString();
    }
    break;
  }

  case semantic::SymbolKind::Method: {
    auto *method = static_cast<const semantic::MethodSymbol *>(symbol);
    if (method->isStatic())
      result += "static ";
    result += symbol->name() + "(";
    bool first = true;
    for (auto *param : method->parameters()) {
      if (!first)
        result += ", ";
      first = false;
      result += param->name();
      if (param->type()) {
        result += ": " + param->type()->toString();
      }
    }
    result += ")";
    if (method->returnType()) {
      result += " -> " + method->returnType()->toString();
    }
    break;
  }

  case semantic::SymbolKind::Import: {
    // For import symbols, we need to find the target symbol safely
    // DO NOT use targetSymbol() directly as it may be a dangling pointer
    // Instead, we return just the name for now
    // TODO: Implement proper symbol lookup by re-resolving the import
    result = symbol->name();
    break;
  }

  default:
    result = symbol->name();
    break;
  }

  return result;
}

std::string createHoverMarkdown(const semantic::Symbol *symbol, types::TypeRef type) {
  if (!symbol)
    return "";

  std::string result = "```lang\n";
  result += formatSymbolSignature(symbol);
  result += "\n```";

  // Add additional info
  if (symbol->isConst()) {
    result += "\n\n*constant*";
  }
  if (symbol->isStatic()) {
    result += "\n\n*static*";
  }
  if (symbol->isExport()) {
    result += "\n\n*exported*";
  }

  return result;
}

} // namespace lsp
} // namespace lang