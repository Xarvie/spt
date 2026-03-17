#pragma once

#include "LspService.h"
#include <functional>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace lsp_test {

struct TestContext {
    lang::lsp::LspService service;
    std::string currentFile;
    std::string currentContent;
    int64_t version = 0;
    std::vector<std::string> log;
    
    void logMessage(const std::string& msg) {
        log.push_back(msg);
    }
    
    void clearLog() {
        log.clear();
    }
};

class ActionChain {
public:
    using Action = std::function<void(TestContext& ctx)>;
    
    ActionChain& initialize(const std::string& workspacePath) {
        actions_.push_back([workspacePath](TestContext& ctx) {
            ctx.service.initialize(workspacePath);
            ctx.logMessage("INIT: workspace = " + workspacePath);
        });
        return *this;
    }
    
    ActionChain& openFile(const std::string& uri, const std::string& content) {
        actions_.push_back([uri, content](TestContext& ctx) {
            ctx.currentFile = uri;
            ctx.currentContent = content;
            ctx.version = 1;
            ctx.service.didOpen(uri, content, ctx.version);
            ctx.logMessage("OPEN: " + uri + " (version " + std::to_string(ctx.version) + ")");
        });
        return *this;
    }
    
    ActionChain& changeFile(const std::string& newContent) {
        actions_.push_back([newContent](TestContext& ctx) {
            ctx.version++;
            ctx.currentContent = newContent;
            ctx.service.didChange(ctx.currentFile, newContent, ctx.version);
            ctx.logMessage("CHANGE: version = " + std::to_string(ctx.version));
        });
        return *this;
    }
    
    ActionChain& insertText(uint32_t line, uint32_t column, const std::string& text) {
        actions_.push_back([line, column, text](TestContext& ctx) {
            std::vector<std::string> lines;
            std::istringstream iss(ctx.currentContent);
            std::string l;
            while (std::getline(iss, l)) {
                lines.push_back(l);
            }
            
            if (line < lines.size()) {
                lines[line].insert(column, text);
            } else if (line == lines.size()) {
                lines.push_back(text);
            }
            
            std::string newContent;
            for (size_t i = 0; i < lines.size(); i++) {
                newContent += lines[i];
                if (i < lines.size() - 1) newContent += "\n";
            }
            
            ctx.version++;
            ctx.currentContent = newContent;
            ctx.service.didChange(ctx.currentFile, newContent, ctx.version);
            ctx.logMessage("INSERT: line " + std::to_string(line) + ", col " + std::to_string(column) + " -> \"" + text + "\"");
        });
        return *this;
    }
    
    ActionChain& deleteText(uint32_t line, uint32_t startCol, uint32_t endCol) {
        actions_.push_back([line, startCol, endCol](TestContext& ctx) {
            std::vector<std::string> lines;
            std::istringstream iss(ctx.currentContent);
            std::string l;
            while (std::getline(iss, l)) {
                lines.push_back(l);
            }
            
            if (line < lines.size() && endCol <= lines[line].size()) {
                lines[line].erase(startCol, endCol - startCol);
            }
            
            std::string newContent;
            for (size_t i = 0; i < lines.size(); i++) {
                newContent += lines[i];
                if (i < lines.size() - 1) newContent += "\n";
            }
            
            ctx.version++;
            ctx.currentContent = newContent;
            ctx.service.didChange(ctx.currentFile, newContent, ctx.version);
            ctx.logMessage("DELETE: line " + std::to_string(line) + ", cols " + std::to_string(startCol) + "-" + std::to_string(endCol));
        });
        return *this;
    }
    
    ActionChain& deleteLine(uint32_t line) {
        actions_.push_back([line](TestContext& ctx) {
            std::vector<std::string> lines;
            std::istringstream iss(ctx.currentContent);
            std::string l;
            while (std::getline(iss, l)) {
                lines.push_back(l);
            }
            
            if (line < lines.size()) {
                lines.erase(lines.begin() + line);
            }
            
            std::string newContent;
            for (size_t i = 0; i < lines.size(); i++) {
                newContent += lines[i];
                if (i < lines.size() - 1) newContent += "\n";
            }
            
            ctx.version++;
            ctx.currentContent = newContent;
            ctx.service.didChange(ctx.currentFile, newContent, ctx.version);
            ctx.logMessage("DELETE_LINE: line " + std::to_string(line));
        });
        return *this;
    }
    
    ActionChain& testCompletion(uint32_t line, uint32_t column, 
                                 std::function<void(TestContext&, const lang::lsp::CompletionResult&)> validator) {
        actions_.push_back([line, column, validator](TestContext& ctx) {
            lang::lsp::Position pos{line, column};
            auto result = ctx.service.completion(ctx.currentFile, pos);
            ctx.logMessage("COMPLETION: line " + std::to_string(line) + ", col " + std::to_string(column) + 
                          " -> " + std::to_string(result.items.size()) + " items");
            if (validator) {
                validator(ctx, result);
            }
        });
        return *this;
    }
    
    ActionChain& testHover(uint32_t line, uint32_t column,
                          std::function<void(TestContext&, const lang::lsp::HoverResult&)> validator) {
        actions_.push_back([line, column, validator](TestContext& ctx) {
            lang::lsp::Position pos{line, column};
            auto result = ctx.service.hover(ctx.currentFile, pos);
            ctx.logMessage("HOVER: line " + std::to_string(line) + ", col " + std::to_string(column));
            if (validator) {
                validator(ctx, result);
            }
        });
        return *this;
    }
    
    ActionChain& testDefinition(uint32_t line, uint32_t column,
                               std::function<void(TestContext&, const std::vector<lang::lsp::LocationLink>&)> validator) {
        actions_.push_back([line, column, validator](TestContext& ctx) {
            lang::lsp::Position pos{line, column};
            auto result = ctx.service.definition(ctx.currentFile, pos);
            ctx.logMessage("DEFINITION: line " + std::to_string(line) + ", col " + std::to_string(column) +
                          " -> " + std::to_string(result.size()) + " locations");
            if (validator) {
                validator(ctx, result);
            }
        });
        return *this;
    }
    
    ActionChain& testDiagnostics(std::function<void(TestContext&, const std::vector<lang::lsp::Diagnostic>&)> validator) {
        actions_.push_back([validator](TestContext& ctx) {
            auto diags = ctx.service.getDiagnostics(ctx.currentFile);
            ctx.logMessage("DIAGNOSTICS: " + std::to_string(diags.size()) + " issues");
            if (validator) {
                validator(ctx, diags);
            }
        });
        return *this;
    }
    
    ActionChain& testDocumentSymbols(std::function<void(TestContext&, const std::vector<lang::lsp::DocumentSymbol>&)> validator) {
        actions_.push_back([validator](TestContext& ctx) {
            auto symbols = ctx.service.documentSymbols(ctx.currentFile);
            ctx.logMessage("DOCUMENT_SYMBOLS: " + std::to_string(symbols.size()) + " symbols");
            if (validator) {
                validator(ctx, symbols);
            }
        });
        return *this;
    }
    
    ActionChain& customAction(const std::string& name, std::function<void(TestContext&)> action) {
        actions_.push_back([name, action](TestContext& ctx) {
            ctx.logMessage("CUSTOM: " + name);
            if (action) {
                action(ctx);
            }
        });
        return *this;
    }
    
    void run(TestContext& ctx) {
        for (auto& action : actions_) {
            action(ctx);
        }
    }
    
    void runAndPrint(TestContext& ctx) {
        run(ctx);
        std::printf("\n=== Action Chain Log ===\n");
        for (const auto& msg : ctx.log) {
            std::printf("  %s\n", msg.c_str());
        }
        std::printf("========================\n\n");
    }
    
private:
    std::vector<Action> actions_;
};

inline bool hasCompletionItem(const lang::lsp::CompletionResult& result, const std::string& label) {
    for (const auto& item : result.items) {
        if (item.label == label) return true;
    }
    return false;
}

inline std::vector<std::string> getCompletionLabels(const lang::lsp::CompletionResult& result) {
    std::vector<std::string> labels;
    for (const auto& item : result.items) {
        labels.push_back(item.label);
    }
    return labels;
}

inline bool hasDiagnosticAt(const std::vector<lang::lsp::Diagnostic>& diags, uint32_t line) {
    for (const auto& d : diags) {
        if (d.range.start.line == line) return true;
    }
    return false;
}

inline size_t countDiagnostics(const std::vector<lang::lsp::Diagnostic>& diags) {
    return diags.size();
}

}
