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

    auto it = semanticModels_.find(file->uri());
    if (it != semanticModels_.end()) {
      return &it->second;
    }

    // Parse and analyze
    ast::CompilationUnitNode *ast = file->getAst();
    if (!ast)
      return nullptr;

    // 使用文件自己的 StringTable，确保 InternedString 一致
    semantic::SemanticAnalyzer analyzer(file->factory().stringTable());
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

  if (auto *ident = ast::ast_cast<ast::IdentifierNode>(node)) {
    LSP_LOG("Hover: node is IdentifierNode");
    // Look up symbol
    if (model) {
      if (auto *sym = model->getResolvedSymbol(node)) {
        LSP_LOG("Hover: found resolved symbol " << sym->name());
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
    // Handle hover on member access (e.g., abc.value)
    LSP_LOG("Hover: MemberAccessExpr, memberRange=[" << memberAccess->memberRange.begin.offset
                                                     << "-" << memberAccess->memberRange.end.offset
                                                     << "], offset=" << offset);
    // Check if offset is within memberRange (the member name part)
    if (memberAccess->memberRange.isValid() && offset >= memberAccess->memberRange.begin.offset &&
        offset < memberAccess->memberRange.end.offset) {
      LSP_LOG("Hover: offset in memberRange, looking up member type");
      if (model) {
        // Get the type of the base expression
        types::TypeRef baseType = model->getNodeType(memberAccess->base);
        if (baseType && !baseType->isUnknown()) {
          LSP_LOG("Hover: baseType=" << baseType->toString());
          // Try to find the member in the base type
          if (auto *classType = dynamic_cast<const types::ClassType *>(baseType.get())) {
            // Look for the member in the class
            std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
            std::string memberName(memberNameView);
            for (const auto &field : classType->fields()) {
              if (field.name == memberName) {
                LSP_LOG("Hover: found field " << field.name);
                result.contents = "```lang\n" + field.type->toString() + "\n```";
                result.range = file->toRange(memberAccess->memberRange);
                break;
              }
            }
          }
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

  // Find node at position
  NodeFinder finder(ast);
  auto findResult = finder.findNodeAt(offset);

  if (!findResult.valid())
    return result;

  // Special handling for MemberAccessExpr - check if offset is in memberRange
  if (auto *memberAccess = ast::ast_cast<ast::MemberAccessExprNode>(findResult.node())) {
    if (memberAccess->memberRange.isValid() && offset >= memberAccess->memberRange.begin.offset &&
        offset < memberAccess->memberRange.end.offset) {
      LSP_LOG("Definition: MemberAccessExpr member, looking up field definition");
      // Get the type of the base expression
      types::TypeRef baseType = model->getNodeType(memberAccess->base);
      if (baseType && !baseType->isUnknown()) {
        LSP_LOG("Definition: baseType=" << baseType->toString());
        if (auto *classType = dynamic_cast<const types::ClassType *>(baseType.get())) {
          std::string_view memberNameView = file->factory().strings().get(memberAccess->member);
          std::string memberName(memberNameView);

          // Find the field in the class type
          const types::FieldInfo *fieldInfo = classType->findField(memberName);
          if (fieldInfo) {
            LSP_LOG("Definition: found field " << fieldInfo->name);

            // Find the ClassSymbol to get the FieldSymbol with definition location
            // First, find the class declaration node
            ast::ClassDeclNode *classDecl = nullptr;
            for (size_t i = 0; i < ast->statements.size(); ++i) {
              if (auto *declStmt = ast::ast_cast<ast::DeclStmtNode>(ast->statements[i])) {
                if (auto *cd = ast::ast_cast<ast::ClassDeclNode>(declStmt->decl)) {
                  if (cd->name.isValid()) {
                    auto className = file->factory().strings().get(cd->name);
                    if (className == classType->name()) {
                      classDecl = cd;
                      break;
                    }
                  }
                }
              }
            }

            if (classDecl) {
              // Find the field declaration in the class
              for (auto *fieldDecl : classDecl->fields) {
                auto fieldName = file->factory().strings().get(fieldDecl->name);
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
                  return result;
                }
              }
            }
          }
        }
      }
    }
  }

  // Look up symbol
  semantic::Symbol *sym = model->getResolvedSymbol(findResult.node());
  if (!sym)
    return result;

  // Create location link
  LocationLink link;
  link.targetUri = std::string(uri); // Same file for now

  // Convert definition location
  ast::SourceLoc defLoc = sym->definitionLoc();
  if (defLoc.isValid()) {
    Position defPos = file->getPosition(defLoc.offset);
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

std::string formatSymbolSignature(const semantic::Symbol *symbol) {
  if (!symbol)
    return "";

  std::string result;

  switch (symbol->kind()) {
  case semantic::SymbolKind::Variable:
    result = "var " + symbol->name();
    if (symbol->type()) {
      result += ": " + symbol->type()->toString();
    }
    break;

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