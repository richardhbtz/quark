#include "lsp/lsp_lexer.h"
#include <cctype>
#include <unordered_map>

namespace lsp {

// Token helper methods
bool Token::isKeyword() const {
    return kind >= TokenKind::KwModule && kind <= TokenKind::KwChar;
}

bool Token::isTypeKeyword() const {
    return kind >= TokenKind::KwInt && kind <= TokenKind::KwChar;
}

bool Token::isLiteral() const {
    return kind == TokenKind::Number || kind == TokenKind::Float ||
           kind == TokenKind::String || kind == TokenKind::Char ||
           kind == TokenKind::True || kind == TokenKind::False ||
           kind == TokenKind::Null;
}

bool Token::isOperator() const {
    return kind >= TokenKind::Plus && kind <= TokenKind::GreaterGreaterEqual;
}

std::string Token::kindToString(TokenKind kind) {
    static const std::unordered_map<TokenKind, std::string> names = {
        {TokenKind::EndOfFile, "EOF"},
        {TokenKind::Error, "Error"},
        {TokenKind::Number, "Number"},
        {TokenKind::Float, "Float"},
        {TokenKind::String, "String"},
        {TokenKind::Char, "Char"},
        {TokenKind::True, "true"},
        {TokenKind::False, "false"},
        {TokenKind::Null, "null"},
        {TokenKind::Identifier, "Identifier"},
        {TokenKind::KwModule, "module"},
        {TokenKind::KwImport, "import"},
        {TokenKind::KwExtern, "extern"},
        {TokenKind::KwStruct, "struct"},
        {TokenKind::KwImpl, "impl"},
        {TokenKind::KwData, "data"},
        {TokenKind::KwExtend, "extend"},
        {TokenKind::KwVar, "var"},
        {TokenKind::KwIf, "if"},
        {TokenKind::KwElif, "elif"},
        {TokenKind::KwElse, "else"},
        {TokenKind::KwWhile, "while"},
        {TokenKind::KwFor, "for"},
        {TokenKind::KwIn, "in"},
        {TokenKind::KwMatch, "match"},
        {TokenKind::KwRet, "ret"},
        {TokenKind::KwBreak, "break"},
        {TokenKind::KwContinue, "continue"},
        {TokenKind::KwThis, "this"},
        {TokenKind::KwVoid, "void"},
        {TokenKind::KwMap, "map"},
        {TokenKind::KwList, "list"},
        {TokenKind::KwInt, "int"},
        {TokenKind::KwFloat, "float"},
        {TokenKind::KwDouble, "double"},
        {TokenKind::KwBool, "bool"},
        {TokenKind::KwStr, "str"},
        {TokenKind::KwChar, "char"},
        {TokenKind::LeftParen, "("},
        {TokenKind::RightParen, ")"},
        {TokenKind::LeftBrace, "{"},
        {TokenKind::RightBrace, "}"},
        {TokenKind::LeftBracket, "["},
        {TokenKind::RightBracket, "]"},
        {TokenKind::Semicolon, ";"},
        {TokenKind::Colon, ":"},
        {TokenKind::Comma, ","},
        {TokenKind::Dot, "."},
        {TokenKind::Range, ".."},
        {TokenKind::Arrow, "->"},
        {TokenKind::FatArrow, "=>"},
        {TokenKind::Plus, "+"},
        {TokenKind::Minus, "-"},
        {TokenKind::Star, "*"},
        {TokenKind::Slash, "/"},
        {TokenKind::Percent, "%"},
        {TokenKind::Ampersand, "&"},
        {TokenKind::Pipe, "|"},
        {TokenKind::Caret, "^"},
        {TokenKind::Tilde, "~"},
        {TokenKind::Bang, "!"},
        {TokenKind::Equal, "="},
        {TokenKind::Less, "<"},
        {TokenKind::Greater, ">"},
        {TokenKind::PlusEqual, "+="},
        {TokenKind::MinusEqual, "-="},
        {TokenKind::StarEqual, "*="},
        {TokenKind::SlashEqual, "/="},
        {TokenKind::PercentEqual, "%="},
        {TokenKind::AmpEqual, "&="},
        {TokenKind::PipeEqual, "|="},
        {TokenKind::CaretEqual, "^="},
        {TokenKind::EqualEqual, "=="},
        {TokenKind::BangEqual, "!="},
        {TokenKind::LessEqual, "<="},
        {TokenKind::GreaterEqual, ">="},
        {TokenKind::AmpAmp, "&&"},
        {TokenKind::PipePipe, "||"},
        {TokenKind::LessLess, "<<"},
        {TokenKind::GreaterGreater, ">>"},
        {TokenKind::LessLessEqual, "<<="},
        {TokenKind::GreaterGreaterEqual, ">>="},
        {TokenKind::Underscore, "_"},
    };
    
    auto it = names.find(kind);
    if (it != names.end()) {
        return it->second;
    }
    return "Unknown";
}

// Lexer implementation
LspLexer::LspLexer(const std::string& source, const std::string& filename)
    : source_(source), filename_(filename) {
    // Skip UTF-8 BOM if present
    if (source_.size() >= 3 &&
        static_cast<unsigned char>(source_[0]) == 0xEF &&
        static_cast<unsigned char>(source_[1]) == 0xBB &&
        static_cast<unsigned char>(source_[2]) == 0xBF) {
        position_ = 3;
    }
}

char LspLexer::peek(size_t offset) const {
    if (position_ + offset >= source_.size()) {
        return '\0';
    }
    return source_[position_ + offset];
}

char LspLexer::advance() {
    if (isAtEnd()) {
        return '\0';
    }
    char c = source_[position_++];
    if (c == '\n') {
        line_++;
        column_ = 0;
    } else {
        column_++;
    }
    return c;
}

bool LspLexer::isAtEnd() const {
    return position_ >= source_.size();
}

void LspLexer::skipWhitespace() {
    while (!isAtEnd()) {
        char c = peek();
        switch (c) {
            case ' ':
            case '\t':
            case '\r':
            case '\n':
                advance();
                break;
            case '/':
                if (peek(1) == '/') {
                    skipLineComment();
                } else if (peek(1) == '*') {
                    skipBlockComment();
                } else {
                    return;
                }
                break;
            default:
                return;
        }
    }
}

void LspLexer::skipLineComment() {
    // Skip //
    advance();
    advance();
    while (!isAtEnd() && peek() != '\n') {
        advance();
    }
}

void LspLexer::skipBlockComment() {
    // Skip /*
    advance();
    advance();
    
    int depth = 1;
    while (!isAtEnd() && depth > 0) {
        if (peek() == '/' && peek(1) == '*') {
            advance();
            advance();
            depth++;
        } else if (peek() == '*' && peek(1) == '/') {
            advance();
            advance();
            depth--;
        } else {
            advance();
        }
    }
}

Token LspLexer::makeToken(TokenKind kind, size_t start, int startLine, int startCol) {
    Token token;
    token.kind = kind;
    token.text = source_.substr(start, position_ - start);
    token.range.startLine = startLine;
    token.range.startColumn = startCol;
    token.range.endLine = line_;
    token.range.endColumn = column_;
    token.range.startOffset = start;
    token.range.endOffset = position_;
    return token;
}

std::vector<Token> LspLexer::tokenize() {
    std::vector<Token> tokens;
    
    while (!isAtEnd()) {
        skipWhitespace();
        if (isAtEnd()) {
            break;
        }
        
        Token token = scanToken();
        tokens.push_back(token);
        
        if (token.kind == TokenKind::EndOfFile) {
            break;
        }
    }
    
    // Add EOF token if not present
    if (tokens.empty() || tokens.back().kind != TokenKind::EndOfFile) {
        Token eof;
        eof.kind = TokenKind::EndOfFile;
        eof.range.startLine = line_;
        eof.range.startColumn = column_;
        eof.range.endLine = line_;
        eof.range.endColumn = column_;
        eof.range.startOffset = position_;
        eof.range.endOffset = position_;
        tokens.push_back(eof);
    }
    
    return tokens;
}

Token LspLexer::scanToken() {
    size_t start = position_;
    int startLine = line_;
    int startCol = column_;
    
    char c = advance();
    
    // Identifiers and keywords
    if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
        while (!isAtEnd() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }
        std::string text = source_.substr(start, position_ - start);
        TokenKind kind = identifierKind(text);
        Token token = makeToken(kind, start, startLine, startCol);
        
        // Handle bool literals
        if (kind == TokenKind::True) {
            token.numberValue = 1.0;
        } else if (kind == TokenKind::False) {
            token.numberValue = 0.0;
        }
        
        return token;
    }
    
    // Numbers
    if (std::isdigit(static_cast<unsigned char>(c))) {
        // Reset position to re-scan from start
        position_ = start;
        column_ = startCol;
        line_ = startLine;
        return scanNumber();
    }
    
    // Strings
    if (c == '"') {
        position_ = start;
        column_ = startCol;
        line_ = startLine;
        return scanString();
    }
    
    // Characters
    if (c == '\'') {
        position_ = start;
        column_ = startCol;
        line_ = startLine;
        return scanChar();
    }
    
    // Two-character tokens
    switch (c) {
        case '.':
            if (peek() == '.') {
                advance();
                return makeToken(TokenKind::Range, start, startLine, startCol);
            }
            return makeToken(TokenKind::Dot, start, startLine, startCol);
            
        case '-':
            if (peek() == '>') {
                advance();
                return makeToken(TokenKind::Arrow, start, startLine, startCol);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::MinusEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Minus, start, startLine, startCol);
            
        case '=':
            if (peek() == '>') {
                advance();
                return makeToken(TokenKind::FatArrow, start, startLine, startCol);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::EqualEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Equal, start, startLine, startCol);
            
        case '!':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::BangEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Bang, start, startLine, startCol);
            
        case '<':
            if (peek() == '<') {
                advance();
                if (peek() == '=') {
                    advance();
                    return makeToken(TokenKind::LessLessEqual, start, startLine, startCol);
                }
                return makeToken(TokenKind::LessLess, start, startLine, startCol);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::LessEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Less, start, startLine, startCol);
            
        case '>':
            if (peek() == '>') {
                advance();
                if (peek() == '=') {
                    advance();
                    return makeToken(TokenKind::GreaterGreaterEqual, start, startLine, startCol);
                }
                return makeToken(TokenKind::GreaterGreater, start, startLine, startCol);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::GreaterEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Greater, start, startLine, startCol);
            
        case '&':
            if (peek() == '&') {
                advance();
                return makeToken(TokenKind::AmpAmp, start, startLine, startCol);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::AmpEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Ampersand, start, startLine, startCol);
            
        case '|':
            if (peek() == '|') {
                advance();
                return makeToken(TokenKind::PipePipe, start, startLine, startCol);
            }
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::PipeEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Pipe, start, startLine, startCol);
            
        case '+':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::PlusEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Plus, start, startLine, startCol);
            
        case '*':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::StarEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Star, start, startLine, startCol);
            
        case '/':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::SlashEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Slash, start, startLine, startCol);
            
        case '%':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::PercentEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Percent, start, startLine, startCol);
            
        case '^':
            if (peek() == '=') {
                advance();
                return makeToken(TokenKind::CaretEqual, start, startLine, startCol);
            }
            return makeToken(TokenKind::Caret, start, startLine, startCol);
            
        // Single character tokens
        case '(': return makeToken(TokenKind::LeftParen, start, startLine, startCol);
        case ')': return makeToken(TokenKind::RightParen, start, startLine, startCol);
        case '{': return makeToken(TokenKind::LeftBrace, start, startLine, startCol);
        case '}': return makeToken(TokenKind::RightBrace, start, startLine, startCol);
        case '[': return makeToken(TokenKind::LeftBracket, start, startLine, startCol);
        case ']': return makeToken(TokenKind::RightBracket, start, startLine, startCol);
        case ';': return makeToken(TokenKind::Semicolon, start, startLine, startCol);
        case ':': return makeToken(TokenKind::Colon, start, startLine, startCol);
        case ',': return makeToken(TokenKind::Comma, start, startLine, startCol);
        case '~': return makeToken(TokenKind::Tilde, start, startLine, startCol);
    }
    
    // Unknown character - create error token
    Token error = makeToken(TokenKind::Error, start, startLine, startCol);
    addError("Unexpected character: '" + std::string(1, c) + "'", error.range);
    return error;
}

Token LspLexer::scanNumber() {
    size_t start = position_;
    int startLine = line_;
    int startCol = column_;
    
    // Check for hex
    if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
        advance(); // '0'
        advance(); // 'x'
        
        while (!isAtEnd() && std::isxdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        
        Token token = makeToken(TokenKind::Number, start, startLine, startCol);
        // Parse hex value
        std::string hex = token.text.substr(2);
        unsigned long long val = 0;
        for (char c : hex) {
            unsigned d;
            if (c >= '0' && c <= '9') d = c - '0';
            else if (c >= 'a' && c <= 'f') d = 10 + (c - 'a');
            else if (c >= 'A' && c <= 'F') d = 10 + (c - 'A');
            else break;
            val = (val << 4) | d;
        }
        token.numberValue = static_cast<double>(val);
        return token;
    }
    
    // Regular number
    bool hasDecimal = false;
    while (!isAtEnd()) {
        char c = peek();
        if (std::isdigit(static_cast<unsigned char>(c))) {
            advance();
        } else if (c == '.' && !hasDecimal) {
            // Check for range operator
            if (peek(1) == '.') {
                break;
            }
            hasDecimal = true;
            advance();
        } else {
            break;
        }
    }
    
    // Check for float suffix
    bool isFloat = false;
    if (!isAtEnd() && (peek() == 'f' || peek() == 'F')) {
        isFloat = true;
        advance();
    }
    
    Token token = makeToken(isFloat ? TokenKind::Float : TokenKind::Number, start, startLine, startCol);
    
    // Parse value
    std::string numStr = token.text;
    if (isFloat && !numStr.empty() && (numStr.back() == 'f' || numStr.back() == 'F')) {
        numStr.pop_back();
    }
    try {
        token.numberValue = std::stod(numStr);
    } catch (...) {
        token.numberValue = 0.0;
    }
    
    return token;
}

Token LspLexer::scanString() {
    size_t start = position_;
    int startLine = line_;
    int startCol = column_;
    
    advance(); // Opening quote
    
    std::string value;
    while (!isAtEnd() && peek() != '"') {
        if (peek() == '\n') {
            // Unterminated string
            break;
        }
        
        if (peek() == '\\' && position_ + 1 < source_.size()) {
            advance();
            char escaped = advance();
            switch (escaped) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                case 'b': value += '\b'; break;
                case 'f': value += '\f'; break;
                case 'a': value += '\a'; break;
                case 'v': value += '\v'; break;
                case '\\': value += '\\'; break;
                case '"': value += '"'; break;
                case '\'': value += '\''; break;
                case '0': value += '\0'; break;
                default: value += escaped; break;
            }
        } else {
            value += advance();
        }
    }
    
    if (!isAtEnd() && peek() == '"') {
        advance(); // Closing quote
    } else {
        SourceRange range;
        range.startLine = startLine;
        range.startColumn = startCol;
        range.endLine = line_;
        range.endColumn = column_;
        addError("Unterminated string literal", range);
    }
    
    Token token = makeToken(TokenKind::String, start, startLine, startCol);
    token.text = value; // Store the processed string value
    return token;
}

Token LspLexer::scanChar() {
    size_t start = position_;
    int startLine = line_;
    int startCol = column_;
    
    advance(); // Opening quote
    
    char value = '\0';
    if (!isAtEnd() && peek() != '\'') {
        if (peek() == '\\' && position_ + 1 < source_.size()) {
            advance();
            char escaped = advance();
            switch (escaped) {
                case 'n': value = '\n'; break;
                case 't': value = '\t'; break;
                case 'r': value = '\r'; break;
                case 'b': value = '\b'; break;
                case 'f': value = '\f'; break;
                case 'a': value = '\a'; break;
                case 'v': value = '\v'; break;
                case '\\': value = '\\'; break;
                case '"': value = '"'; break;
                case '\'': value = '\''; break;
                case '0': value = '\0'; break;
                default: value = escaped; break;
            }
        } else {
            value = advance();
        }
    }
    
    if (!isAtEnd() && peek() == '\'') {
        advance(); // Closing quote
    } else {
        SourceRange range;
        range.startLine = startLine;
        range.startColumn = startCol;
        range.endLine = line_;
        range.endColumn = column_;
        addError("Unterminated character literal", range);
    }
    
    Token token = makeToken(TokenKind::Char, start, startLine, startCol);
    token.text = std::string(1, value);
    token.numberValue = static_cast<double>(value);
    return token;
}

void LspLexer::addError(const std::string& message, const SourceRange& range) {
    errors_.push_back({message, range});
}

TokenKind LspLexer::identifierKind(const std::string& text) const {
    static const std::unordered_map<std::string, TokenKind> keywords = {
        {"module", TokenKind::KwModule},
        {"import", TokenKind::KwImport},
        {"extern", TokenKind::KwExtern},
        {"struct", TokenKind::KwStruct},
        {"impl", TokenKind::KwImpl},
        {"data", TokenKind::KwData},
        {"extend", TokenKind::KwExtend},
        {"var", TokenKind::KwVar},
        {"if", TokenKind::KwIf},
        {"elif", TokenKind::KwElif},
        {"else", TokenKind::KwElse},
        {"while", TokenKind::KwWhile},
        {"for", TokenKind::KwFor},
        {"in", TokenKind::KwIn},
        {"match", TokenKind::KwMatch},
        {"ret", TokenKind::KwRet},
        {"break", TokenKind::KwBreak},
        {"continue", TokenKind::KwContinue},
        {"this", TokenKind::KwThis},
        {"void", TokenKind::KwVoid},
        {"map", TokenKind::KwMap},
        {"list", TokenKind::KwList},
        {"int", TokenKind::KwInt},
        {"float", TokenKind::KwFloat},
        {"double", TokenKind::KwDouble},
        {"bool", TokenKind::KwBool},
        {"str", TokenKind::KwStr},
        {"char", TokenKind::KwChar},
        {"true", TokenKind::True},
        {"false", TokenKind::False},
        {"null", TokenKind::Null},
        {"_", TokenKind::Underscore},
    };
    
    auto it = keywords.find(text);
    if (it != keywords.end()) {
        return it->second;
    }
    return TokenKind::Identifier;
}

} // namespace lsp
