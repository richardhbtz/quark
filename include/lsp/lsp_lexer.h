#pragma once

#include <string>
#include <vector>
#include <optional>
#include <memory>

namespace lsp {

// Token kind for LSP lexer (mirrors main lexer but designed for error recovery)
enum class TokenKind {
    EndOfFile,
    Error,          // Invalid token (for error recovery)
    
    // Literals
    Number,
    Float,
    String,
    Char,
    True,
    False,
    Null,
    
    // Identifiers
    Identifier,
    
    // Keywords
    KwModule,
    KwImport,
    KwExtern,
    KwStruct,
    KwImpl,
    KwData,
    KwExtend,
    KwVar,
    KwIf,
    KwElif,
    KwElse,
    KwWhile,
    KwFor,
    KwIn,
    KwMatch,
    KwRet,
    KwBreak,
    KwContinue,
    KwThis,
    KwVoid,
    KwMap,
    KwList,
    
    // Type keywords
    KwInt,
    KwFloat,
    KwDouble,
    KwBool,
    KwStr,
    KwChar,
    
    // Punctuation
    LeftParen,      // (
    RightParen,     // )
    LeftBrace,      // {
    RightBrace,     // }
    LeftBracket,    // [
    RightBracket,   // ]
    Semicolon,      // ;
    Colon,          // :
    Comma,          // ,
    Dot,            // .
    Range,          // ..
    Arrow,          // ->
    FatArrow,       // =>
    
    // Operators
    Plus,           // +
    Minus,          // -
    Star,           // *
    Slash,          // /
    Percent,        // %
    Ampersand,      // &
    Pipe,           // |
    Caret,          // ^
    Tilde,          // ~
    Bang,           // !
    Equal,          // =
    Less,           // <
    Greater,        // >
    
    // Compound operators
    PlusEqual,      // +=
    MinusEqual,     // -=
    StarEqual,      // *=
    SlashEqual,     // /=
    PercentEqual,   // %=
    AmpEqual,       // &=
    PipeEqual,      // |=
    CaretEqual,     // ^=
    
    // Comparison operators
    EqualEqual,     // ==
    BangEqual,      // !=
    LessEqual,      // <=
    GreaterEqual,   // >=
    
    // Logical operators
    AmpAmp,         // &&
    PipePipe,       // ||
    
    // Shift operators
    LessLess,       // <<
    GreaterGreater, // >>
    LessLessEqual,  // <<=
    GreaterGreaterEqual, // >>=
    
    // Special
    Underscore,     // _ (wildcard)
};

// Source range for tokens
struct SourceRange {
    int startLine = 0;      // 0-indexed
    int startColumn = 0;    // 0-indexed  
    int endLine = 0;
    int endColumn = 0;
    size_t startOffset = 0;
    size_t endOffset = 0;
};

// Token structure
struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    std::string text;
    double numberValue = 0.0;
    SourceRange range;
    
    bool isKeyword() const;
    bool isTypeKeyword() const;
    bool isLiteral() const;
    bool isOperator() const;
    static std::string kindToString(TokenKind kind);
};

// Lexer error
struct LexerError {
    std::string message;
    SourceRange range;
};

// Lexer for LSP with error recovery
class LspLexer {
public:
    explicit LspLexer(const std::string& source, const std::string& filename = "<input>");
    
    // Get all tokens (including error tokens)
    std::vector<Token> tokenize();
    
    // Get all lexer errors
    const std::vector<LexerError>& getErrors() const { return errors_; }
    
    // Get source code
    const std::string& getSource() const { return source_; }
    const std::string& getFilename() const { return filename_; }
    
private:
    std::string source_;
    std::string filename_;
    size_t position_ = 0;
    int line_ = 0;
    int column_ = 0;
    std::vector<LexerError> errors_;
    
    char peek(size_t offset = 0) const;
    char advance();
    bool isAtEnd() const;
    void skipWhitespace();
    void skipLineComment();
    void skipBlockComment();
    
    Token makeToken(TokenKind kind, size_t start, int startLine, int startCol);
    Token scanToken();
    Token scanIdentifierOrKeyword();
    Token scanNumber();
    Token scanString();
    Token scanChar();
    
    void addError(const std::string& message, const SourceRange& range);
    TokenKind identifierKind(const std::string& text) const;
};

} // namespace lsp
