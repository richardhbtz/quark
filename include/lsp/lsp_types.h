#pragma once

#include <string>
#include <vector>
#include <optional>
#include <variant>
#include <map>
#include <memory>

namespace lsp {

// Position in a text document (0-indexed)
struct Position {
    int line = 0;
    int character = 0;
    
    bool operator==(const Position& other) const {
        return line == other.line && character == other.character;
    }
    bool operator<(const Position& other) const {
        if (line != other.line) return line < other.line;
        return character < other.character;
    }
    bool operator<=(const Position& other) const {
        return *this < other || *this == other;
    }
};

// Range in a text document
struct Range {
    Position start;
    Position end;
    
    bool contains(const Position& pos) const {
        return start <= pos && pos <= end;
    }
};

// Location with URI and range
struct Location {
    std::string uri;
    Range range;
};

// Diagnostic severity
enum class DiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4
};

// Diagnostic message
struct Diagnostic {
    Range range;
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string code;
    std::string source = "quark";
    std::string message;
    std::vector<std::string> relatedInformation;
};

// Text document identifier
struct TextDocumentIdentifier {
    std::string uri;
};

// Versioned text document identifier
struct VersionedTextDocumentIdentifier : TextDocumentIdentifier {
    int version = 0;
};

// Text document content change event
struct TextDocumentContentChangeEvent {
    std::optional<Range> range;
    std::string text;
};

// Document symbol kinds
enum class SymbolKind {
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
    TypeParameter = 26
};

// Document symbol
struct DocumentSymbol {
    std::string name;
    std::string detail;
    SymbolKind kind;
    Range range;
    Range selectionRange;
    std::vector<DocumentSymbol> children;
};

// Completion item kinds
enum class CompletionItemKind {
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
    TypeParameter = 25
};

// Completion item
struct CompletionItem {
    std::string label;
    CompletionItemKind kind = CompletionItemKind::Text;
    std::string detail;
    std::string documentation;
    std::string insertText;
    bool deprecated = false;
};

// Completion list
struct CompletionList {
    bool isIncomplete = false;
    std::vector<CompletionItem> items;
};

// Hover result
struct Hover {
    std::string contents;  // Markdown content
    std::optional<Range> range;
};

// Signature help
struct ParameterInformation {
    std::string label;
    std::string documentation;
};

struct SignatureInformation {
    std::string label;
    std::string documentation;
    std::vector<ParameterInformation> parameters;
};

struct SignatureHelp {
    std::vector<SignatureInformation> signatures;
    int activeSignature = 0;
    int activeParameter = 0;
};

// Server capabilities
struct ServerCapabilities {
    bool textDocumentSync = true;          // Full sync
    bool completionProvider = true;
    bool hoverProvider = true;
    bool signatureHelpProvider = true;
    bool definitionProvider = true;
    bool referencesProvider = true;
    bool documentHighlightProvider = true;
    bool documentSymbolProvider = true;
    bool workspaceSymbolProvider = false;  // Not implemented yet
    bool codeActionProvider = false;       // Not implemented yet
    bool documentFormattingProvider = false;
    bool renameProvider = false;           // Not implemented yet
};

// Initialize params
struct InitializeParams {
    int processId = 0;
    std::string rootUri;
    std::string rootPath;
};

// Initialize result
struct InitializeResult {
    ServerCapabilities capabilities;
};

} // namespace lsp
