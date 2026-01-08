#pragma once

#include "lsp_types.h"
#include "lsp_parser.h"
#include "lsp_lexer.h"
#include "lsp_ast.h"
#include <string>
#include <map>
#include <memory>
#include <vector>
#include <functional>
#include <optional>

namespace lsp {

// Parsed document with AST and diagnostics
struct ParsedDocument {
    std::string uri;
    std::string content;
    int version = 0;
    std::unique_ptr<Program> ast;
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
    
    // Symbol information extracted from AST
    std::vector<DocumentSymbol> symbols;
};

// Symbol definition for go-to-definition
struct SymbolDefinition {
    std::string name;
    SymbolKind kind;
    Location location;
    std::string type;  // Type signature for functions
    std::string detail;
};

// Document analyzer that extracts information from parsed AST
class DocumentAnalyzer {
public:
    explicit DocumentAnalyzer(const ParsedDocument& doc);
    
    // Extract document symbols
    std::vector<DocumentSymbol> getDocumentSymbols();
    
    // Find definition at position
    std::optional<Location> findDefinition(const Position& pos);
    
    // Find all references to symbol at position
    std::vector<Location> findReferences(const Position& pos);
    
    // Get hover information at position
    std::optional<Hover> getHover(const Position& pos);
    
    // Get completions at position
    CompletionList getCompletions(const Position& pos);
    
    // Get signature help at position  
    std::optional<SignatureHelp> getSignatureHelp(const Position& pos);

private:
    const ParsedDocument& doc_;
    std::map<std::string, SymbolDefinition> symbolTable_;
    
    void buildSymbolTable();
    void visitNode(const AstNode* node);
    void visitStmt(const Stmt* stmt);
    void visitExpr(const Expr* expr);
    
    // Find AST node at position
    const AstNode* findNodeAt(const Position& pos);
    bool positionInRange(const Position& pos, const SourceRange& range);
    
    // Get identifier at position
    std::string getIdentifierAt(const Position& pos);
    
    // Convert source range to LSP range
    Range toLspRange(const SourceRange& range);
    
    // Add keyword completions
    void addKeywordCompletions(CompletionList& list);
    void addTypeCompletions(CompletionList& list);
    void addBuiltinCompletions(CompletionList& list);
};

// LSP Server main class
class LspServer {
public:
    LspServer();
    ~LspServer();
    
    // Run the server (reads from stdin, writes to stdout)
    void run();
    
    // Process a single JSON-RPC message
    std::string processMessage(const std::string& message);
    
private:
    std::map<std::string, ParsedDocument> documents_;
    bool initialized_ = false;
    bool shutdownRequested_ = false;
    
    // Message handlers
    std::string handleInitialize(const std::string& id, const std::string& params);
    std::string handleInitialized();
    std::string handleShutdown(const std::string& id);
    void handleExit();
    
    // Document synchronization
    void handleDidOpen(const std::string& params);
    void handleDidChange(const std::string& params);
    void handleDidClose(const std::string& params);
    void handleDidSave(const std::string& params);
    
    // Language features
    std::string handleCompletion(const std::string& id, const std::string& params);
    std::string handleHover(const std::string& id, const std::string& params);
    std::string handleDefinition(const std::string& id, const std::string& params);
    std::string handleReferences(const std::string& id, const std::string& params);
    std::string handleDocumentSymbol(const std::string& id, const std::string& params);
    std::string handleSignatureHelp(const std::string& id, const std::string& params);
    
    // Document management
    void parseDocument(const std::string& uri, const std::string& content, int version);
    void publishDiagnostics(const std::string& uri, const std::vector<Diagnostic>& diagnostics);
    
    // JSON-RPC utilities
    std::string createResponse(const std::string& id, const std::string& result);
    std::string createErrorResponse(const std::string& id, int code, const std::string& message);
    std::string createNotification(const std::string& method, const std::string& params);
    
    // I/O
    void sendMessage(const std::string& message);
    std::string readMessage();
    
    // Logging
    void log(const std::string& message);
};

} // namespace lsp
