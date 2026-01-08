#pragma once

#include "lsp_lexer.h"
#include "lsp_ast.h"
#include "lsp_types.h"
#include <vector>
#include <memory>
#include <functional>
#include <string>

namespace lsp {

// Parser error
struct ParserError {
    std::string message;
    SourceRange range;
    std::string code;  // Error code for diagnostics
};

// Error-recovering parser for LSP
// Implements the EBNF grammar with synchronization points for recovery
class LspParser {
public:
    explicit LspParser(const std::vector<Token>& tokens, const std::string& source);
    
    // Parse the entire program
    std::unique_ptr<Program> parse();
    
    // Get all parser errors
    const std::vector<ParserError>& getErrors() const { return errors_; }
    
    // Get diagnostics for LSP
    std::vector<Diagnostic> getDiagnostics() const;

private:
    const std::vector<Token>& tokens_;
    const std::string& source_;
    size_t current_ = 0;
    std::vector<ParserError> errors_;
    
    // Token navigation
    const Token& peek(size_t offset = 0) const;
    const Token& previous() const;
    bool isAtEnd() const;
    const Token& advance();
    bool check(TokenKind kind) const;
    bool match(TokenKind kind);
    bool match(std::initializer_list<TokenKind> kinds);
    
    // Error handling and recovery
    void addError(const std::string& message, const SourceRange& range, 
                  const std::string& code = "E0000");
    void addError(const std::string& message, const std::string& code = "E0000");
    const Token& expect(TokenKind kind, const std::string& message);
    void synchronize();
    void synchronizeTo(std::initializer_list<TokenKind> kinds);
    
    // Utility
    SourceRange makeRange(const SourceRange& start, const SourceRange& end);
    SourceRange currentRange() const;
    
    // ========================================================================
    // Statement parsing
    // ========================================================================
    std::unique_ptr<Stmt> parseStatement();
    std::unique_ptr<ModuleDecl> parseModuleDecl();
    std::unique_ptr<ImportStmt> parseImportStmt();
    std::unique_ptr<ExternBlock> parseExternBlock();
    std::unique_ptr<Stmt> parseExternDecl();
    std::unique_ptr<StructDef> parseStructDef();
    std::unique_ptr<ImplBlock> parseImplBlock();
    std::unique_ptr<FunctionDef> parseFunctionDef(std::unique_ptr<TypeExpr> returnType,
                                                   const std::string& name,
                                                   const SourceRange& startRange);
    std::unique_ptr<VarDecl> parseVarDecl();
    std::unique_ptr<MapDecl> parseMapDecl();
    std::unique_ptr<ListDecl> parseListDecl();
    std::unique_ptr<IfStmt> parseIfStmt();
    std::unique_ptr<WhileStmt> parseWhileStmt();
    std::unique_ptr<ForStmt> parseForStmt();
    std::unique_ptr<MatchStmt> parseMatchStmt();
    std::unique_ptr<ReturnStmt> parseReturnStmt();
    std::unique_ptr<BreakStmt> parseBreakStmt();
    std::unique_ptr<ContinueStmt> parseContinueStmt();
    std::unique_ptr<Stmt> parseAssignOrExprStmt();
    
    std::vector<std::unique_ptr<Stmt>> parseBlock();
    
    // ========================================================================
    // Expression parsing (precedence climbing)
    // ========================================================================
    std::unique_ptr<Expr> parseExpression();
    std::unique_ptr<Expr> parseLogicalOr();
    std::unique_ptr<Expr> parseLogicalAnd();
    std::unique_ptr<Expr> parseBitwiseOr();
    std::unique_ptr<Expr> parseBitwiseXor();
    std::unique_ptr<Expr> parseBitwiseAnd();
    std::unique_ptr<Expr> parseEquality();
    std::unique_ptr<Expr> parseRelational();
    std::unique_ptr<Expr> parseShift();
    std::unique_ptr<Expr> parseAdditive();
    std::unique_ptr<Expr> parseMultiplicative();
    std::unique_ptr<Expr> parseUnary();
    std::unique_ptr<Expr> parsePostfix();
    std::unique_ptr<Expr> parsePostfixContinuation(std::unique_ptr<Expr> left);
    std::unique_ptr<Expr> parsePrimary();
    
    // Special expression parsers
    std::unique_ptr<Expr> parseArrayLiteral();
    std::unique_ptr<Expr> parseMapLiteral();
    std::unique_ptr<Expr> parseListLiteral();
    std::unique_ptr<StructLiteral> parseStructLiteral(const std::string& name,
                                                       const SourceRange& startRange);
    std::vector<std::unique_ptr<Expr>> parseArguments();
    
    // ========================================================================
    // Type parsing
    // ========================================================================
    std::unique_ptr<TypeExpr> parseType();
    std::unique_ptr<TypeExpr> parseBaseType();
    std::unique_ptr<FunctionPointerTypeExpr> parseFunctionPointerType();
    std::vector<std::pair<std::string, std::unique_ptr<TypeExpr>>> parseParameterList();
    
    // ========================================================================
    // Helpers
    // ========================================================================
    bool isTypeStart() const;
    bool isStatementStart() const;
    bool isExpressionStart() const;
    std::string getTypeString(const TypeExpr* type) const;
};

} // namespace lsp
