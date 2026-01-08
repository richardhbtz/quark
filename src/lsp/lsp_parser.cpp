#include "lsp/lsp_parser.h"
#include <algorithm>

namespace lsp {

// Static token for EOF fallback
static Token eofToken = {TokenKind::EndOfFile, "", 0.0, {}};

LspParser::LspParser(const std::vector<Token>& tokens, const std::string& source)
    : tokens_(tokens), source_(source) {}

// ============================================================================
// Token navigation
// ============================================================================

const Token& LspParser::peek(size_t offset) const {
    size_t idx = current_ + offset;
    if (idx >= tokens_.size()) {
        return eofToken;
    }
    return tokens_[idx];
}

const Token& LspParser::previous() const {
    if (current_ == 0) {
        return tokens_.empty() ? eofToken : tokens_[0];
    }
    return tokens_[current_ - 1];
}

bool LspParser::isAtEnd() const {
    return current_ >= tokens_.size() || peek().kind == TokenKind::EndOfFile;
}

const Token& LspParser::advance() {
    if (!isAtEnd()) {
        current_++;
    }
    return previous();
}

bool LspParser::check(TokenKind kind) const {
    return peek().kind == kind;
}

bool LspParser::match(TokenKind kind) {
    if (check(kind)) {
        advance();
        return true;
    }
    return false;
}

bool LspParser::match(std::initializer_list<TokenKind> kinds) {
    for (auto kind : kinds) {
        if (check(kind)) {
            advance();
            return true;
        }
    }
    return false;
}

// ============================================================================
// Error handling
// ============================================================================

void LspParser::addError(const std::string& message, const SourceRange& range, 
                         const std::string& code) {
    errors_.push_back({message, range, code});
}

void LspParser::addError(const std::string& message, const std::string& code) {
    addError(message, peek().range, code);
}

const Token& LspParser::expect(TokenKind kind, const std::string& message) {
    if (check(kind)) {
        return advance();
    }
    addError(message, "E0001");
    return peek();
}

void LspParser::synchronize() {
    advance();
    
    while (!isAtEnd()) {
        // After a semicolon, we can start a new statement
        if (previous().kind == TokenKind::Semicolon) {
            return;
        }
        
        // At statement-starting keywords, we can recover
        switch (peek().kind) {
            case TokenKind::KwModule:
            case TokenKind::KwImport:
            case TokenKind::KwExtern:
            case TokenKind::KwStruct:
            case TokenKind::KwImpl:
            case TokenKind::KwVar:
            case TokenKind::KwIf:
            case TokenKind::KwWhile:
            case TokenKind::KwFor:
            case TokenKind::KwMatch:
            case TokenKind::KwRet:
            case TokenKind::KwBreak:
            case TokenKind::KwContinue:
            case TokenKind::KwInt:
            case TokenKind::KwFloat:
            case TokenKind::KwDouble:
            case TokenKind::KwBool:
            case TokenKind::KwStr:
            case TokenKind::KwChar:
            case TokenKind::KwVoid:
            case TokenKind::KwMap:
            case TokenKind::KwList:
            case TokenKind::RightBrace:
                return;
            default:
                break;
        }
        
        advance();
    }
}

void LspParser::synchronizeTo(std::initializer_list<TokenKind> kinds) {
    while (!isAtEnd()) {
        for (auto kind : kinds) {
            if (check(kind)) {
                return;
            }
        }
        advance();
    }
}

SourceRange LspParser::makeRange(const SourceRange& start, const SourceRange& end) {
    SourceRange r;
    r.startLine = start.startLine;
    r.startColumn = start.startColumn;
    r.startOffset = start.startOffset;
    r.endLine = end.endLine;
    r.endColumn = end.endColumn;
    r.endOffset = end.endOffset;
    return r;
}

SourceRange LspParser::currentRange() const {
    return peek().range;
}

std::vector<Diagnostic> LspParser::getDiagnostics() const {
    std::vector<Diagnostic> diags;
    for (const auto& err : errors_) {
        Diagnostic d;
        d.range.start.line = err.range.startLine;
        d.range.start.character = err.range.startColumn;
        d.range.end.line = err.range.endLine;
        d.range.end.character = err.range.endColumn;
        d.severity = DiagnosticSeverity::Error;
        d.code = err.code;
        d.message = err.message;
        diags.push_back(d);
    }
    return diags;
}

// ============================================================================
// Helper predicates
// ============================================================================

bool LspParser::isTypeStart() const {
    switch (peek().kind) {
        case TokenKind::KwInt:
        case TokenKind::KwFloat:
        case TokenKind::KwDouble:
        case TokenKind::KwBool:
        case TokenKind::KwStr:
        case TokenKind::KwChar:
        case TokenKind::KwVoid:
        case TokenKind::KwMap:
        case TokenKind::KwList:
        case TokenKind::Identifier:
            return true;
        default:
            return false;
    }
}

bool LspParser::isStatementStart() const {
    switch (peek().kind) {
        case TokenKind::KwModule:
        case TokenKind::KwImport:
        case TokenKind::KwExtern:
        case TokenKind::KwStruct:
        case TokenKind::KwImpl:
        case TokenKind::KwVar:
        case TokenKind::KwIf:
        case TokenKind::KwWhile:
        case TokenKind::KwFor:
        case TokenKind::KwMatch:
        case TokenKind::KwRet:
        case TokenKind::KwBreak:
        case TokenKind::KwContinue:
        case TokenKind::KwMap:
        case TokenKind::KwList:
            return true;
        default:
            return isTypeStart() || isExpressionStart();
    }
}

bool LspParser::isExpressionStart() const {
    switch (peek().kind) {
        case TokenKind::Number:
        case TokenKind::Float:
        case TokenKind::String:
        case TokenKind::Char:
        case TokenKind::True:
        case TokenKind::False:
        case TokenKind::Null:
        case TokenKind::Identifier:
        case TokenKind::KwThis:
        case TokenKind::LeftParen:
        case TokenKind::LeftBracket:
        case TokenKind::LeftBrace:
        case TokenKind::Minus:
        case TokenKind::Bang:
        case TokenKind::Tilde:
        case TokenKind::Ampersand:
        case TokenKind::Star:
            return true;
        default:
            return false;
    }
}

std::string LspParser::getTypeString(const TypeExpr* type) const {
    if (!type) return "unknown";
    
    if (auto* simple = dynamic_cast<const SimpleTypeExpr*>(type)) {
        return simple->name;
    }
    if (auto* arr = dynamic_cast<const ArrayTypeExpr*>(type)) {
        return getTypeString(arr->elementType.get()) + "[]";
    }
    if (auto* ptr = dynamic_cast<const PointerTypeExpr*>(type)) {
        return getTypeString(ptr->pointeeType.get()) + std::string(ptr->pointerDepth, '*');
    }
    if (auto* fn = dynamic_cast<const FunctionPointerTypeExpr*>(type)) {
        std::string result = "fn(";
        for (size_t i = 0; i < fn->paramTypes.size(); i++) {
            if (i > 0) result += ", ";
            result += getTypeString(fn->paramTypes[i].get());
        }
        result += ") -> " + getTypeString(fn->returnType.get());
        return result;
    }
    return "unknown";
}

// ============================================================================
// Program parsing
// ============================================================================

std::unique_ptr<Program> LspParser::parse() {
    auto program = std::make_unique<Program>();
    program->range.startLine = 0;
    program->range.startColumn = 0;
    program->range.startOffset = 0;
    
    while (!isAtEnd()) {
        try {
            auto stmt = parseStatement();
            if (stmt) {
                program->statements.push_back(std::move(stmt));
            }
        } catch (...) {
            synchronize();
        }
    }
    
    if (!tokens_.empty()) {
        program->range.endLine = tokens_.back().range.endLine;
        program->range.endColumn = tokens_.back().range.endColumn;
        program->range.endOffset = tokens_.back().range.endOffset;
    }
    
    return program;
}

// ============================================================================
// Statement parsing
// ============================================================================

std::unique_ptr<Stmt> LspParser::parseStatement() {
    // Skip error tokens
    while (check(TokenKind::Error)) {
        addError("Unexpected token", peek().range);
        advance();
    }
    
    if (isAtEnd()) {
        return nullptr;
    }
    
    switch (peek().kind) {
        case TokenKind::KwModule:
            return parseModuleDecl();
        case TokenKind::KwImport:
            return parseImportStmt();
        case TokenKind::KwExtern:
            return parseExternBlock();
        case TokenKind::KwStruct:
            return parseStructDef();
        case TokenKind::KwImpl:
            return parseImplBlock();
        case TokenKind::KwVar:
            return parseVarDecl();
        case TokenKind::KwMap:
            return parseMapDecl();
        case TokenKind::KwList:
            return parseListDecl();
        case TokenKind::KwIf:
            return parseIfStmt();
        case TokenKind::KwWhile:
            return parseWhileStmt();
        case TokenKind::KwFor:
            return parseForStmt();
        case TokenKind::KwMatch:
            return parseMatchStmt();
        case TokenKind::KwRet:
            return parseReturnStmt();
        case TokenKind::KwBreak:
            return parseBreakStmt();
        case TokenKind::KwContinue:
            return parseContinueStmt();
        default:
            break;
    }
    
    // Check for type-led statements (function def or typed var decl)
    if (isTypeStart()) {
        SourceRange startRange = peek().range;
        auto type = parseType();
        
        if (check(TokenKind::Identifier)) {
            std::string name = peek().text;
            advance();
            
            // Function definition
            if (check(TokenKind::LeftParen)) {
                return parseFunctionDef(std::move(type), name, startRange);
            }
            
            // Typed variable declaration
            if (check(TokenKind::Equal)) {
                advance();
                auto init = parseExpression();
                
                auto decl = std::make_unique<VarDecl>();
                decl->range = makeRange(startRange, previous().range);
                decl->name = name;
                decl->type = std::move(type);
                decl->initializer = std::move(init);
                decl->isVar = false;
                
                match(TokenKind::Semicolon);
                return decl;
            }
        }
    }
    
    // Otherwise, it's an assignment or expression statement
    return parseAssignOrExprStmt();
}

std::unique_ptr<ModuleDecl> LspParser::parseModuleDecl() {
    SourceRange startRange = peek().range;
    advance(); // consume 'module'
    
    auto decl = std::make_unique<ModuleDecl>("");
    
    if (check(TokenKind::Identifier)) {
        decl->name = peek().text;
        advance();
    } else {
        addError("Expected module name after 'module'");
    }
    
    match(TokenKind::Semicolon);
    decl->range = makeRange(startRange, previous().range);
    return decl;
}

std::unique_ptr<ImportStmt> LspParser::parseImportStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'import'
    
    auto stmt = std::make_unique<ImportStmt>();
    
    if (match(TokenKind::LeftBrace)) {
        // import { mod1, mod2 }
        while (!check(TokenKind::RightBrace) && !isAtEnd()) {
            if (check(TokenKind::Identifier)) {
                std::string path = peek().text;
                advance();
                while (match(TokenKind::Slash)) {
                    if (check(TokenKind::Identifier)) {
                        path += "/" + peek().text;
                        advance();
                    } else {
                        addError("Expected module name after '/'");
                        break;
                    }
                }
                stmt->paths.push_back(path);
            }
            
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        expect(TokenKind::RightBrace, "Expected '}' after import list");
    } else if (check(TokenKind::Identifier)) {
        // import mod or import mod/submod
        std::string path = peek().text;
        advance();
        while (match(TokenKind::Slash)) {
            if (check(TokenKind::Identifier)) {
                path += "/" + peek().text;
                advance();
            } else {
                addError("Expected module name after '/'");
                break;
            }
        }
        stmt->paths.push_back(path);
    } else {
        addError("Expected module name after 'import'");
    }
    
    match(TokenKind::Semicolon);
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<ExternBlock> LspParser::parseExternBlock() {
    SourceRange startRange = peek().range;
    advance(); // consume 'extern'
    
    auto block = std::make_unique<ExternBlock>();
    
    // Expect "C" string literal
    if (!check(TokenKind::String) || peek().text != "C") {
        addError("Expected '\"C\"' after 'extern'");
    } else {
        advance();
    }
    
    if (match(TokenKind::LeftBrace)) {
        // extern "C" { ... }
        while (!check(TokenKind::RightBrace) && !isAtEnd()) {
            auto decl = parseExternDecl();
            if (decl) {
                block->declarations.push_back(std::move(decl));
            }
        }
        expect(TokenKind::RightBrace, "Expected '}' after extern block");
    } else {
        // Single extern declaration
        auto decl = parseExternDecl();
        if (decl) {
            block->declarations.push_back(std::move(decl));
        }
    }
    
    block->range = makeRange(startRange, previous().range);
    return block;
}

std::unique_ptr<Stmt> LspParser::parseExternDecl() {
    SourceRange startRange = peek().range;
    
    // extern struct Name;
    if (match(TokenKind::KwStruct)) {
        std::string name;
        if (check(TokenKind::Identifier)) {
            name = peek().text;
            advance();
        } else {
            addError("Expected struct name");
        }
        expect(TokenKind::Semicolon, "Expected ';' after extern struct");
        
        auto decl = std::make_unique<ExternStructDecl>(name);
        decl->range = makeRange(startRange, previous().range);
        return decl;
    }
    
    // Parse type
    if (!isTypeStart()) {
        addError("Expected type in extern declaration");
        synchronizeTo({TokenKind::Semicolon, TokenKind::RightBrace});
        match(TokenKind::Semicolon);
        return std::make_unique<ErrorStmt>("Invalid extern declaration");
    }
    
    auto type = parseType();
    
    if (!check(TokenKind::Identifier)) {
        addError("Expected name in extern declaration");
        synchronizeTo({TokenKind::Semicolon, TokenKind::RightBrace});
        match(TokenKind::Semicolon);
        return std::make_unique<ErrorStmt>("Invalid extern declaration");
    }
    
    std::string name = peek().text;
    advance();
    
    // Function declaration
    if (match(TokenKind::LeftParen)) {
        auto decl = std::make_unique<ExternFunctionDecl>();
        decl->name = name;
        decl->returnType = std::move(type);
        
        // Parse parameters
        while (!check(TokenKind::RightParen) && !isAtEnd()) {
            // Check for variadic
            if (match(TokenKind::Range)) {
                decl->isVariadic = true;
                if (match(TokenKind::Dot)) {} // Consume extra dot if present
                break;
            }
            if (check(TokenKind::Dot)) {
                // Handle ... as three dots
                advance(); advance(); advance();
                decl->isVariadic = true;
                break;
            }
            
            std::string paramName;
            std::unique_ptr<TypeExpr> paramType;
            
            if (check(TokenKind::Identifier)) {
                std::string first = peek().text;
                advance();
                
                if (match(TokenKind::Colon)) {
                    // name: type
                    paramName = first;
                    paramType = parseType();
                } else {
                    // Just a type name
                    paramType = std::make_unique<SimpleTypeExpr>(first);
                    // Handle pointer suffixes
                    while (check(TokenKind::Star)) {
                        auto ptr = std::make_unique<PointerTypeExpr>(std::move(paramType));
                        paramType = std::move(ptr);
                        advance();
                    }
                }
            } else if (isTypeStart()) {
                paramType = parseType();
            }
            
            if (paramType) {
                decl->parameters.emplace_back(paramName, std::move(paramType));
            }
            
            if (!match(TokenKind::Comma)) {
                break;
            }
        }
        
        expect(TokenKind::RightParen, "Expected ')' after parameters");
        expect(TokenKind::Semicolon, "Expected ';' after extern function");
        
        decl->range = makeRange(startRange, previous().range);
        return decl;
    }
    
    // Variable declaration
    expect(TokenKind::Semicolon, "Expected ';' after extern variable");
    
    auto decl = std::make_unique<ExternVarDecl>();
    decl->name = name;
    decl->type = std::move(type);
    decl->range = makeRange(startRange, previous().range);
    return decl;
}

std::unique_ptr<StructDef> LspParser::parseStructDef() {
    SourceRange startRange = peek().range;
    advance(); // consume 'struct'
    
    auto def = std::make_unique<StructDef>();
    
    if (check(TokenKind::Identifier)) {
        def->name = peek().text;
        advance();
    } else {
        addError("Expected struct name");
    }
    
    // Check for inheritance: struct Child : Parent
    if (match(TokenKind::Colon)) {
        if (check(TokenKind::Identifier)) {
            def->parentName = peek().text;
            advance();
        } else {
            addError("Expected parent struct name after ':'");
        }
    }
    
    expect(TokenKind::LeftBrace, "Expected '{' after struct name");
    
    // Parse struct body
    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        // Data block
        if (match(TokenKind::KwData)) {
            expect(TokenKind::LeftBrace, "Expected '{' after 'data'");
            
            while (!check(TokenKind::RightBrace) && !isAtEnd()) {
                FieldDecl field;
                field.range = peek().range;
                
                if (check(TokenKind::Identifier)) {
                    field.name = peek().text;
                    advance();
                    
                    expect(TokenKind::Colon, "Expected ':' after field name");
                    field.type = parseType();
                    field.range = makeRange(field.range, previous().range);
                    
                    def->fields.push_back(std::move(field));
                }
                
                // Allow comma or semicolon separator
                if (!match(TokenKind::Comma) && !match(TokenKind::Semicolon)) {
                    if (!check(TokenKind::RightBrace)) {
                        addError("Expected ',' or ';' after field");
                    }
                }
            }
            
            expect(TokenKind::RightBrace, "Expected '}' after data block");
        }
        // Method definition
        else if (isTypeStart() || check(TokenKind::KwExtend) || check(TokenKind::KwImpl)) {
            MethodDef method;
            method.range = peek().range;
            
            method.isExtend = match(TokenKind::KwExtend);
            if (!method.isExtend) {
                match(TokenKind::KwImpl); // Optional impl keyword
            }
            
            method.returnType = parseType();
            
            if (check(TokenKind::Identifier)) {
                method.name = peek().text;
                advance();
            } else {
                addError("Expected method name");
            }
            
            expect(TokenKind::LeftParen, "Expected '(' after method name");
            method.parameters = parseParameterList();
            expect(TokenKind::RightParen, "Expected ')' after parameters");
            
            method.body = parseBlock();
            method.range = makeRange(method.range, previous().range);
            
            def->methods.push_back(std::move(method));
        } else {
            addError("Expected 'data' block or method in struct");
            advance();
        }
    }
    
    expect(TokenKind::RightBrace, "Expected '}' after struct body");
    def->range = makeRange(startRange, previous().range);
    return def;
}

std::unique_ptr<ImplBlock> LspParser::parseImplBlock() {
    SourceRange startRange = peek().range;
    advance(); // consume 'impl'
    
    auto impl = std::make_unique<ImplBlock>();
    
    if (check(TokenKind::Identifier)) {
        impl->structName = peek().text;
        advance();
    } else {
        addError("Expected struct name after 'impl'");
    }
    
    expect(TokenKind::LeftBrace, "Expected '{' after struct name");
    
    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        if (isTypeStart()) {
            MethodDef method;
            method.range = peek().range;
            method.returnType = parseType();
            
            if (check(TokenKind::Identifier)) {
                method.name = peek().text;
                advance();
            } else {
                addError("Expected method name");
            }
            
            expect(TokenKind::LeftParen, "Expected '('");
            method.parameters = parseParameterList();
            expect(TokenKind::RightParen, "Expected ')'");
            
            method.body = parseBlock();
            method.range = makeRange(method.range, previous().range);
            
            impl->methods.push_back(std::move(method));
        } else {
            addError("Expected method definition in impl block");
            advance();
        }
    }
    
    expect(TokenKind::RightBrace, "Expected '}' after impl block");
    impl->range = makeRange(startRange, previous().range);
    return impl;
}

std::unique_ptr<FunctionDef> LspParser::parseFunctionDef(
    std::unique_ptr<TypeExpr> returnType,
    const std::string& name,
    const SourceRange& startRange) {
    
    auto func = std::make_unique<FunctionDef>();
    func->name = name;
    func->returnType = std::move(returnType);
    
    expect(TokenKind::LeftParen, "Expected '(' after function name");
    
    // Parse parameters
    while (!check(TokenKind::RightParen) && !isAtEnd()) {
        // Check for variadic
        if (match(TokenKind::Range)) {
            func->isVariadic = true;
            break;
        }
        if (check(TokenKind::Dot)) {
            advance(); advance(); advance();
            func->isVariadic = true;
            break;
        }
        
        if (check(TokenKind::Identifier)) {
            std::string paramName = peek().text;
            advance();
            
            expect(TokenKind::Colon, "Expected ':' after parameter name");
            auto paramType = parseType();
            
            func->parameters.emplace_back(paramName, std::move(paramType));
        }
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    expect(TokenKind::RightParen, "Expected ')' after parameters");
    func->body = parseBlock();
    func->range = makeRange(startRange, previous().range);
    
    return func;
}

std::unique_ptr<VarDecl> LspParser::parseVarDecl() {
    SourceRange startRange = peek().range;
    advance(); // consume 'var'
    
    auto decl = std::make_unique<VarDecl>();
    decl->isVar = true;
    
    if (check(TokenKind::Identifier)) {
        decl->name = peek().text;
        advance();
    } else {
        addError("Expected variable name");
    }
    
    // Optional type annotation
    if (match(TokenKind::Colon)) {
        decl->type = parseType();
    }
    
    expect(TokenKind::Equal, "Expected '=' in variable declaration");
    decl->initializer = parseExpression();
    
    match(TokenKind::Semicolon);
    decl->range = makeRange(startRange, previous().range);
    return decl;
}

std::unique_ptr<MapDecl> LspParser::parseMapDecl() {
    SourceRange startRange = peek().range;
    advance(); // consume 'map'
    
    auto decl = std::make_unique<MapDecl>();
    
    if (check(TokenKind::Identifier)) {
        decl->name = peek().text;
        advance();
    } else {
        addError("Expected map name");
    }
    
    if (match(TokenKind::Equal)) {
        decl->initializer = parseExpression();
    }
    
    match(TokenKind::Semicolon);
    decl->range = makeRange(startRange, previous().range);
    return decl;
}

std::unique_ptr<ListDecl> LspParser::parseListDecl() {
    SourceRange startRange = peek().range;
    advance(); // consume 'list'
    
    auto decl = std::make_unique<ListDecl>();
    
    if (check(TokenKind::Identifier)) {
        decl->name = peek().text;
        advance();
    } else {
        addError("Expected list name");
    }
    
    if (match(TokenKind::Equal)) {
        decl->initializer = parseExpression();
    }
    
    match(TokenKind::Semicolon);
    decl->range = makeRange(startRange, previous().range);
    return decl;
}

std::unique_ptr<IfStmt> LspParser::parseIfStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'if'
    
    auto stmt = std::make_unique<IfStmt>();
    
    expect(TokenKind::LeftParen, "Expected '(' after 'if'");
    stmt->condition = parseExpression();
    expect(TokenKind::RightParen, "Expected ')' after condition");
    
    stmt->thenBody = parseBlock();
    
    // Parse elif clauses
    while (check(TokenKind::KwElif) || 
           (check(TokenKind::KwElse) && peek(1).kind == TokenKind::KwIf)) {
        if (match(TokenKind::KwElif)) {
            // elif (cond) { ... }
        } else {
            advance(); // 'else'
            advance(); // 'if'
        }
        
        expect(TokenKind::LeftParen, "Expected '(' after 'elif'");
        auto elifCond = parseExpression();
        expect(TokenKind::RightParen, "Expected ')' after condition");
        
        auto elifBody = parseBlock();
        stmt->elifs.emplace_back(std::move(elifCond), std::move(elifBody));
    }
    
    // Parse else clause
    if (match(TokenKind::KwElse)) {
        stmt->elseBody = parseBlock();
    }
    
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<WhileStmt> LspParser::parseWhileStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'while'
    
    auto stmt = std::make_unique<WhileStmt>();
    
    expect(TokenKind::LeftParen, "Expected '(' after 'while'");
    stmt->condition = parseExpression();
    expect(TokenKind::RightParen, "Expected ')' after condition");
    
    stmt->body = parseBlock();
    
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<ForStmt> LspParser::parseForStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'for'
    
    auto stmt = std::make_unique<ForStmt>();
    
    expect(TokenKind::LeftParen, "Expected '(' after 'for'");
    
    // Determine loop style: range-based or C-style
    // Range: for (var i in expr) or for (i in expr) or for (type i in expr)
    // C-style: for (init; cond; incr)
    
    // Save position for backtracking
    size_t savedPos = current_;
    bool isRangeBased = false;
    
    // Try to detect range-based
    if (match(TokenKind::KwVar)) {
        stmt->isVar = true;
        if (check(TokenKind::Identifier)) {
            stmt->variable = peek().text;
            advance();
            if (match(TokenKind::KwIn)) {
                isRangeBased = true;
            }
        }
    } else if (isTypeStart()) {
        // Could be type name in expr or C-style init
        auto type = parseType();
        if (check(TokenKind::Identifier)) {
            std::string name = peek().text;
            advance();
            if (match(TokenKind::KwIn)) {
                stmt->variable = name;
                stmt->varType = std::move(type);
                isRangeBased = true;
            } else {
                // C-style: reset and parse differently
                current_ = savedPos;
            }
        } else {
            current_ = savedPos;
        }
    } else if (check(TokenKind::Identifier)) {
        std::string name = peek().text;
        advance();
        if (match(TokenKind::KwIn)) {
            stmt->variable = name;
            isRangeBased = true;
        } else {
            current_ = savedPos;
        }
    }
    
    if (isRangeBased) {
        // Range-based for loop
        stmt->iterable = parseExpression();
    } else {
        // C-style for loop
        stmt->isCStyle = true;
        
        // Init clause
        if (!check(TokenKind::Semicolon)) {
            stmt->init = parseStatement();
        } else {
            advance(); // consume ';'
        }
        
        // Condition
        if (!check(TokenKind::Semicolon)) {
            stmt->condition = parseExpression();
        }
        expect(TokenKind::Semicolon, "Expected ';' after for condition");
        
        // Increment
        if (!check(TokenKind::RightParen)) {
            stmt->increment = parseExpression();
        }
    }
    
    expect(TokenKind::RightParen, "Expected ')' after for clauses");
    stmt->body = parseBlock();
    
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<MatchStmt> LspParser::parseMatchStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'match'
    
    auto stmt = std::make_unique<MatchStmt>();
    stmt->expression = parseExpression();
    
    expect(TokenKind::LeftBrace, "Expected '{' after match expression");
    
    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        MatchArm arm;
        arm.range = peek().range;
        
        // Wildcard pattern
        if (match(TokenKind::Underscore)) {
            arm.isWildcard = true;
        } else {
            arm.pattern = parseExpression();
        }
        
        expect(TokenKind::FatArrow, "Expected '=>' after pattern");
        
        // Body can be a block or a single statement
        if (check(TokenKind::LeftBrace)) {
            arm.body = parseBlock();
        } else {
            auto bodyStmt = parseStatement();
            if (bodyStmt) {
                arm.body.push_back(std::move(bodyStmt));
            }
        }
        
        arm.range = makeRange(arm.range, previous().range);
        stmt->arms.push_back(std::move(arm));
        
        match(TokenKind::Comma); // Optional trailing comma
    }
    
    expect(TokenKind::RightBrace, "Expected '}' after match arms");
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<ReturnStmt> LspParser::parseReturnStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'ret'
    
    auto stmt = std::make_unique<ReturnStmt>();
    
    if (!check(TokenKind::Semicolon) && !check(TokenKind::RightBrace) && !isAtEnd()) {
        stmt->value = parseExpression();
    }
    
    match(TokenKind::Semicolon);
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<BreakStmt> LspParser::parseBreakStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'break'
    
    auto stmt = std::make_unique<BreakStmt>();
    match(TokenKind::Semicolon);
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<ContinueStmt> LspParser::parseContinueStmt() {
    SourceRange startRange = peek().range;
    advance(); // consume 'continue'
    
    auto stmt = std::make_unique<ContinueStmt>();
    match(TokenKind::Semicolon);
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::unique_ptr<Stmt> LspParser::parseAssignOrExprStmt() {
    SourceRange startRange = peek().range;
    
    // Handle deref assignment: *ptr = value
    if (match(TokenKind::Star)) {
        auto ptr = parseUnary();
        if (match(TokenKind::Equal)) {
            auto value = parseExpression();
            auto stmt = std::make_unique<DerefAssignStmt>();
            stmt->pointer = std::move(ptr);
            stmt->value = std::move(value);
            match(TokenKind::Semicolon);
            stmt->range = makeRange(startRange, previous().range);
            return stmt;
        }
        // If not assignment, it was a dereference expression
        // Create a unary expression and continue
        auto deref = std::make_unique<UnaryExpr>("*", std::move(ptr));
        auto expr = parsePostfixContinuation(std::move(deref));
        
        auto exprStmt = std::make_unique<ExprStmt>();
        exprStmt->expression = std::move(expr);
        match(TokenKind::Semicolon);
        exprStmt->range = makeRange(startRange, previous().range);
        return exprStmt;
    }
    
    // Parse expression
    auto expr = parseExpression();
    
    // Check for assignment
    std::string op;
    if (match(TokenKind::Equal)) op = "=";
    else if (match(TokenKind::PlusEqual)) op = "+=";
    else if (match(TokenKind::MinusEqual)) op = "-=";
    else if (match(TokenKind::StarEqual)) op = "*=";
    else if (match(TokenKind::SlashEqual)) op = "/=";
    else if (match(TokenKind::PercentEqual)) op = "%=";
    else if (match(TokenKind::AmpEqual)) op = "&=";
    else if (match(TokenKind::PipeEqual)) op = "|=";
    else if (match(TokenKind::CaretEqual)) op = "^=";
    else if (match(TokenKind::LessLessEqual)) op = "<<=";
    else if (match(TokenKind::GreaterGreaterEqual)) op = ">>=";
    
    if (!op.empty()) {
        auto value = parseExpression();
        
        // Determine what kind of assignment this is
        if (auto* ident = dynamic_cast<IdentifierExpr*>(expr.get())) {
            auto stmt = std::make_unique<AssignStmt>();
            stmt->name = ident->name;
            stmt->op = op;
            stmt->value = std::move(value);
            match(TokenKind::Semicolon);
            stmt->range = makeRange(startRange, previous().range);
            return stmt;
        }
        if (auto* member = dynamic_cast<MemberAccessExpr*>(expr.get())) {
            auto stmt = std::make_unique<MemberAssignStmt>();
            stmt->object = std::move(member->object);
            stmt->field = member->member;
            stmt->op = op;
            stmt->value = std::move(value);
            match(TokenKind::Semicolon);
            stmt->range = makeRange(startRange, previous().range);
            return stmt;
        }
        if (auto* index = dynamic_cast<IndexExpr*>(expr.get())) {
            auto stmt = std::make_unique<IndexAssignStmt>();
            stmt->object = std::move(index->object);
            stmt->index = std::move(index->index);
            stmt->op = op;
            stmt->value = std::move(value);
            match(TokenKind::Semicolon);
            stmt->range = makeRange(startRange, previous().range);
            return stmt;
        }
        
        addError("Invalid assignment target");
    }
    
    // Expression statement
    auto stmt = std::make_unique<ExprStmt>();
    stmt->expression = std::move(expr);
    match(TokenKind::Semicolon);
    stmt->range = makeRange(startRange, previous().range);
    return stmt;
}

std::vector<std::unique_ptr<Stmt>> LspParser::parseBlock() {
    std::vector<std::unique_ptr<Stmt>> stmts;
    
    if (!match(TokenKind::LeftBrace)) {
        addError("Expected '{' to start block");
        return stmts;
    }
    
    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        auto stmt = parseStatement();
        if (stmt) {
            stmts.push_back(std::move(stmt));
        }
    }
    
    expect(TokenKind::RightBrace, "Expected '}' to end block");
    return stmts;
}

// ============================================================================
// Expression parsing (precedence climbing)
// ============================================================================

std::unique_ptr<Expr> LspParser::parseExpression() {
    return parseLogicalOr();
}

std::unique_ptr<Expr> LspParser::parseLogicalOr() {
    auto left = parseLogicalAnd();
    
    while (match(TokenKind::PipePipe)) {
        auto right = parseLogicalAnd();
        auto binary = std::make_unique<BinaryExpr>("||", std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseLogicalAnd() {
    auto left = parseBitwiseOr();
    
    while (match(TokenKind::AmpAmp)) {
        auto right = parseBitwiseOr();
        auto binary = std::make_unique<BinaryExpr>("&&", std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseBitwiseOr() {
    auto left = parseBitwiseXor();
    
    while (match(TokenKind::Pipe)) {
        auto right = parseBitwiseXor();
        auto binary = std::make_unique<BinaryExpr>("|", std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseBitwiseXor() {
    auto left = parseBitwiseAnd();
    
    while (match(TokenKind::Caret)) {
        auto right = parseBitwiseAnd();
        auto binary = std::make_unique<BinaryExpr>("^", std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseBitwiseAnd() {
    auto left = parseEquality();
    
    while (match(TokenKind::Ampersand)) {
        auto right = parseEquality();
        auto binary = std::make_unique<BinaryExpr>("&", std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseEquality() {
    auto left = parseRelational();
    
    while (true) {
        std::string op;
        if (match(TokenKind::EqualEqual)) op = "==";
        else if (match(TokenKind::BangEqual)) op = "!=";
        else break;
        
        auto right = parseRelational();
        auto binary = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseRelational() {
    auto left = parseShift();
    
    while (true) {
        std::string op;
        if (match(TokenKind::Less)) op = "<";
        else if (match(TokenKind::Greater)) op = ">";
        else if (match(TokenKind::LessEqual)) op = "<=";
        else if (match(TokenKind::GreaterEqual)) op = ">=";
        else break;
        
        auto right = parseShift();
        auto binary = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseShift() {
    auto left = parseAdditive();
    
    while (true) {
        std::string op;
        if (match(TokenKind::LessLess)) op = "<<";
        else if (match(TokenKind::GreaterGreater)) op = ">>";
        else break;
        
        auto right = parseAdditive();
        auto binary = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseAdditive() {
    auto left = parseMultiplicative();
    
    while (true) {
        std::string op;
        if (match(TokenKind::Plus)) op = "+";
        else if (match(TokenKind::Minus)) op = "-";
        else break;
        
        auto right = parseMultiplicative();
        auto binary = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseMultiplicative() {
    auto left = parseUnary();
    
    while (true) {
        std::string op;
        if (match(TokenKind::Star)) op = "*";
        else if (match(TokenKind::Slash)) op = "/";
        else if (match(TokenKind::Percent)) op = "%";
        else break;
        
        auto right = parseUnary();
        auto binary = std::make_unique<BinaryExpr>(op, std::move(left), std::move(right));
        binary->range = makeRange(left->range, right->range);
        left = std::move(binary);
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parseUnary() {
    SourceRange startRange = peek().range;
    
    if (match(TokenKind::Minus)) {
        auto operand = parseUnary();
        auto unary = std::make_unique<UnaryExpr>("-", std::move(operand));
        unary->range = makeRange(startRange, previous().range);
        return unary;
    }
    if (match(TokenKind::Bang)) {
        auto operand = parseUnary();
        auto unary = std::make_unique<UnaryExpr>("!", std::move(operand));
        unary->range = makeRange(startRange, previous().range);
        return unary;
    }
    if (match(TokenKind::Tilde)) {
        auto operand = parseUnary();
        auto unary = std::make_unique<UnaryExpr>("~", std::move(operand));
        unary->range = makeRange(startRange, previous().range);
        return unary;
    }
    if (match(TokenKind::Ampersand)) {
        auto operand = parseUnary();
        auto unary = std::make_unique<UnaryExpr>("&", std::move(operand));
        unary->range = makeRange(startRange, previous().range);
        return unary;
    }
    if (match(TokenKind::Star)) {
        auto operand = parseUnary();
        auto unary = std::make_unique<UnaryExpr>("*", std::move(operand));
        unary->range = makeRange(startRange, previous().range);
        return unary;
    }
    
    return parsePostfix();
}

// Helper for continuing postfix parsing after an already-parsed expression
std::unique_ptr<Expr> LspParser::parsePostfixContinuation(std::unique_ptr<Expr> left) {
    while (true) {
        if (match(TokenKind::Dot)) {
            if (!check(TokenKind::Identifier)) {
                addError("Expected member name after '.'");
                break;
            }
            std::string member = peek().text;
            advance();
            
            // Check for method call
            if (match(TokenKind::LeftParen)) {
                auto call = std::make_unique<MethodCallExpr>();
                call->object = std::move(left);
                call->method = member;
                call->arguments = parseArguments();
                expect(TokenKind::RightParen, "Expected ')' after arguments");
                call->range = makeRange(left->range, previous().range);
                left = std::move(call);
            } else {
                auto access = std::make_unique<MemberAccessExpr>();
                access->object = std::move(left);
                access->member = member;
                access->range = makeRange(left->range, previous().range);
                left = std::move(access);
            }
        }
        else if (match(TokenKind::LeftBracket)) {
            auto index = parseExpression();
            expect(TokenKind::RightBracket, "Expected ']' after index");
            
            auto indexExpr = std::make_unique<IndexExpr>();
            indexExpr->object = std::move(left);
            indexExpr->index = std::move(index);
            indexExpr->range = makeRange(left->range, previous().range);
            left = std::move(indexExpr);
        }
        else if (match(TokenKind::Range)) {
            auto end = parseAdditive();  // Range has lower precedence than arithmetic
            
            auto rangeExpr = std::make_unique<RangeExpr>();
            rangeExpr->start = std::move(left);
            rangeExpr->end = std::move(end);
            rangeExpr->range = makeRange(left->range, previous().range);
            left = std::move(rangeExpr);
        }
        else {
            break;
        }
    }
    
    return left;
}

std::unique_ptr<Expr> LspParser::parsePostfix() {
    auto left = parsePrimary();
    return parsePostfixContinuation(std::move(left));
}

std::unique_ptr<Expr> LspParser::parsePrimary() {
    SourceRange startRange = peek().range;
    
    // Number literal
    if (check(TokenKind::Number)) {
        auto lit = std::make_unique<NumberLiteral>(peek().numberValue, false);
        lit->range = peek().range;
        advance();
        return lit;
    }
    
    // Float literal
    if (check(TokenKind::Float)) {
        auto lit = std::make_unique<NumberLiteral>(peek().numberValue, true);
        lit->range = peek().range;
        advance();
        return lit;
    }
    
    // String literal
    if (check(TokenKind::String)) {
        auto lit = std::make_unique<StringLiteral>(peek().text);
        lit->range = peek().range;
        advance();
        return lit;
    }
    
    // Char literal
    if (check(TokenKind::Char)) {
        auto lit = std::make_unique<CharLiteral>(peek().text.empty() ? '\0' : peek().text[0]);
        lit->range = peek().range;
        advance();
        return lit;
    }
    
    // Boolean literals
    if (match(TokenKind::True)) {
        auto lit = std::make_unique<BoolLiteral>(true);
        lit->range = previous().range;
        return lit;
    }
    if (match(TokenKind::False)) {
        auto lit = std::make_unique<BoolLiteral>(false);
        lit->range = previous().range;
        return lit;
    }
    
    // Null literal
    if (match(TokenKind::Null)) {
        auto lit = std::make_unique<NullLiteral>();
        lit->range = previous().range;
        return lit;
    }
    
    // This expression
    if (match(TokenKind::KwThis)) {
        auto expr = std::make_unique<ThisExpr>();
        expr->range = previous().range;
        return expr;
    }
    
    // Array literal: [...]
    if (check(TokenKind::LeftBracket)) {
        return parseArrayLiteral();
    }
    
    // Map literal: map { ... } or just { ... } if context allows
    if (check(TokenKind::KwMap) && peek(1).kind == TokenKind::LeftBrace) {
        return parseMapLiteral();
    }
    
    // List literal: list[...]
    if (check(TokenKind::KwList) && peek(1).kind == TokenKind::LeftBracket) {
        return parseListLiteral();
    }
    
    // Parenthesized expression or cast
    if (match(TokenKind::LeftParen)) {
        // Check for cast: (type)expr
        if (isTypeStart()) {
            size_t savedPos = current_;
            auto type = parseType();
            if (match(TokenKind::RightParen)) {
                auto operand = parseUnary();
                auto cast = std::make_unique<CastExpr>();
                cast->targetType = std::move(type);
                cast->operand = std::move(operand);
                cast->range = makeRange(startRange, previous().range);
                return cast;
            }
            // Not a cast, reset and parse as expression
            current_ = savedPos;
        }
        
        auto expr = parseExpression();
        expect(TokenKind::RightParen, "Expected ')' after expression");
        return expr;
    }
    
    // Identifier (variable, function call, struct literal, or static call)
    if (check(TokenKind::Identifier)) {
        std::string name = peek().text;
        advance();
        
        // Struct literal: Name { ... }
        if (check(TokenKind::LeftBrace)) {
            return parseStructLiteral(name, startRange);
        }
        
        // Function call: name(...)
        if (match(TokenKind::LeftParen)) {
            auto call = std::make_unique<CallExpr>();
            call->callee = std::make_unique<IdentifierExpr>(name);
            call->arguments = parseArguments();
            expect(TokenKind::RightParen, "Expected ')' after arguments");
            call->range = makeRange(startRange, previous().range);
            return call;
        }
        
        // Just an identifier
        auto ident = std::make_unique<IdentifierExpr>(name);
        ident->range = makeRange(startRange, previous().range);
        return ident;
    }
    
    // Map literal with brace: { key: value } (for map contexts)
    if (check(TokenKind::LeftBrace)) {
        // Could be map literal or empty block - assume map literal in expression context
        return parseMapLiteral();
    }
    
    // Error recovery: create an error expression
    addError("Expected expression");
    auto err = std::make_unique<ErrorExpr>("Expected expression");
    err->range = peek().range;
    err->hasError = true;
    advance();
    return err;
}

std::unique_ptr<Expr> LspParser::parseArrayLiteral() {
    SourceRange startRange = peek().range;
    advance(); // consume '['
    
    auto arr = std::make_unique<ArrayLiteral>();
    
    while (!check(TokenKind::RightBracket) && !isAtEnd()) {
        arr->elements.push_back(parseExpression());
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    expect(TokenKind::RightBracket, "Expected ']' after array elements");
    arr->range = makeRange(startRange, previous().range);
    return arr;
}

std::unique_ptr<Expr> LspParser::parseMapLiteral() {
    SourceRange startRange = peek().range;
    
    // Skip 'map' keyword if present
    match(TokenKind::KwMap);
    
    expect(TokenKind::LeftBrace, "Expected '{' for map literal");
    
    auto map = std::make_unique<MapLiteral>();
    
    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        auto key = parseExpression();
        expect(TokenKind::Colon, "Expected ':' between map key and value");
        auto value = parseExpression();
        
        map->pairs.emplace_back(std::move(key), std::move(value));
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    expect(TokenKind::RightBrace, "Expected '}' after map pairs");
    map->range = makeRange(startRange, previous().range);
    return map;
}

std::unique_ptr<Expr> LspParser::parseListLiteral() {
    SourceRange startRange = peek().range;
    advance(); // consume 'list'
    
    expect(TokenKind::LeftBracket, "Expected '[' after 'list'");
    
    auto list = std::make_unique<ListLiteral>();
    
    while (!check(TokenKind::RightBracket) && !isAtEnd()) {
        list->elements.push_back(parseExpression());
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    expect(TokenKind::RightBracket, "Expected ']' after list elements");
    list->range = makeRange(startRange, previous().range);
    return list;
}

std::unique_ptr<StructLiteral> LspParser::parseStructLiteral(
    const std::string& name, const SourceRange& startRange) {
    
    advance(); // consume '{'
    
    auto lit = std::make_unique<StructLiteral>();
    lit->structName = name;
    
    while (!check(TokenKind::RightBrace) && !isAtEnd()) {
        if (!check(TokenKind::Identifier)) {
            addError("Expected field name in struct literal");
            break;
        }
        
        std::string fieldName = peek().text;
        advance();
        
        expect(TokenKind::Colon, "Expected ':' after field name");
        auto value = parseExpression();
        
        lit->fields.emplace_back(fieldName, std::move(value));
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    expect(TokenKind::RightBrace, "Expected '}' after struct fields");
    lit->range = makeRange(startRange, previous().range);
    return lit;
}

std::vector<std::unique_ptr<Expr>> LspParser::parseArguments() {
    std::vector<std::unique_ptr<Expr>> args;
    
    while (!check(TokenKind::RightParen) && !isAtEnd()) {
        args.push_back(parseExpression());
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    return args;
}

// ============================================================================
// Type parsing
// ============================================================================

std::unique_ptr<TypeExpr> LspParser::parseType() {
    auto baseType = parseBaseType();
    
    // Check for array type: type[]
    if (match(TokenKind::LeftBracket)) {
        expect(TokenKind::RightBracket, "Expected ']' for array type");
        return std::make_unique<ArrayTypeExpr>(std::move(baseType));
    }
    
    // Check for pointer type: type*, type**, etc.
    int pointerDepth = 0;
    while (match(TokenKind::Star)) {
        pointerDepth++;
    }
    
    if (pointerDepth > 0) {
        return std::make_unique<PointerTypeExpr>(std::move(baseType), pointerDepth);
    }
    
    return baseType;
}

std::unique_ptr<TypeExpr> LspParser::parseBaseType() {
    SourceRange startRange = peek().range;
    std::string typeName;
    
    switch (peek().kind) {
        case TokenKind::KwInt: typeName = "int"; break;
        case TokenKind::KwFloat: typeName = "float"; break;
        case TokenKind::KwDouble: typeName = "double"; break;
        case TokenKind::KwBool: typeName = "bool"; break;
        case TokenKind::KwStr: typeName = "str"; break;
        case TokenKind::KwChar: typeName = "char"; break;
        case TokenKind::KwVoid: typeName = "void"; break;
        case TokenKind::KwMap: typeName = "map"; break;
        case TokenKind::KwList: typeName = "list"; break;
        case TokenKind::Identifier: typeName = peek().text; break;
        default:
            addError("Expected type");
            auto err = std::make_unique<SimpleTypeExpr>("error");
            err->range = peek().range;
            return err;
    }
    
    advance();
    auto type = std::make_unique<SimpleTypeExpr>(typeName);
    type->range = makeRange(startRange, previous().range);
    return type;
}

std::unique_ptr<FunctionPointerTypeExpr> LspParser::parseFunctionPointerType() {
    SourceRange startRange = peek().range;
    advance(); // consume 'fn'
    
    auto fnType = std::make_unique<FunctionPointerTypeExpr>();
    
    expect(TokenKind::LeftParen, "Expected '(' after 'fn'");
    
    // Parse parameter types
    while (!check(TokenKind::RightParen) && !isAtEnd()) {
        fnType->paramTypes.push_back(parseType());
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    expect(TokenKind::RightParen, "Expected ')' after parameter types");
    
    // Return type is optional (defaults to void if not specified)
    // Note: The old fn(params) -> return syntax is deprecated
    // Function pointers now use void* and &funcName
    fnType->range = makeRange(startRange, previous().range);
    
    return fnType;
}

std::vector<std::pair<std::string, std::unique_ptr<TypeExpr>>> LspParser::parseParameterList() {
    std::vector<std::pair<std::string, std::unique_ptr<TypeExpr>>> params;
    
    while (!check(TokenKind::RightParen) && !isAtEnd()) {
        if (!check(TokenKind::Identifier)) {
            break;
        }
        
        std::string name = peek().text;
        advance();
        
        expect(TokenKind::Colon, "Expected ':' after parameter name");
        auto type = parseType();
        
        params.emplace_back(name, std::move(type));
        
        if (!match(TokenKind::Comma)) {
            break;
        }
    }
    
    return params;
}

} // namespace lsp
