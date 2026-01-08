#pragma once

#include "lsp_lexer.h"
#include <string>
#include <vector>
#include <memory>
#include <variant>
#include <optional>

namespace lsp {

// Forward declarations
struct AstNode;
struct Expr;
struct Stmt;
struct TypeExpr;

// Base AST node with source range
struct AstNode {
    SourceRange range;
    bool hasError = false;  // True if this node was recovered from an error
    
    virtual ~AstNode() = default;
};

// ============================================================================
// Type expressions
// ============================================================================

struct TypeExpr : AstNode {
    virtual ~TypeExpr() = default;
};

struct SimpleTypeExpr : TypeExpr {
    std::string name;  // int, float, str, bool, void, or user-defined type
    explicit SimpleTypeExpr(const std::string& n) : name(n) {}
};

struct ArrayTypeExpr : TypeExpr {
    std::unique_ptr<TypeExpr> elementType;
    explicit ArrayTypeExpr(std::unique_ptr<TypeExpr> elem) : elementType(std::move(elem)) {}
};

struct PointerTypeExpr : TypeExpr {
    std::unique_ptr<TypeExpr> pointeeType;
    int pointerDepth = 1;  // For type**, pointerDepth = 2
    explicit PointerTypeExpr(std::unique_ptr<TypeExpr> pointee, int depth = 1)
        : pointeeType(std::move(pointee)), pointerDepth(depth) {}
};

struct FunctionPointerTypeExpr : TypeExpr {
    std::vector<std::unique_ptr<TypeExpr>> paramTypes;
    std::unique_ptr<TypeExpr> returnType;
};

// ============================================================================
// Expressions
// ============================================================================

struct Expr : AstNode {
    virtual ~Expr() = default;
};

// Literal expressions
struct NumberLiteral : Expr {
    double value;
    bool isFloat = false;  // True if it had 'f' suffix
    explicit NumberLiteral(double v, bool f = false) : value(v), isFloat(f) {}
};

struct StringLiteral : Expr {
    std::string value;
    explicit StringLiteral(const std::string& v) : value(v) {}
};

struct CharLiteral : Expr {
    char value;
    explicit CharLiteral(char v) : value(v) {}
};

struct BoolLiteral : Expr {
    bool value;
    explicit BoolLiteral(bool v) : value(v) {}
};

struct NullLiteral : Expr {};

struct ThisExpr : Expr {};

// Identifier expression
struct IdentifierExpr : Expr {
    std::string name;
    explicit IdentifierExpr(const std::string& n) : name(n) {}
};

// Binary expression
struct BinaryExpr : Expr {
    std::string op;  // +, -, *, /, %, etc.
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    BinaryExpr(const std::string& o, std::unique_ptr<Expr> l, std::unique_ptr<Expr> r)
        : op(o), left(std::move(l)), right(std::move(r)) {}
};

// Unary expression
struct UnaryExpr : Expr {
    std::string op;  // -, !, ~, &, *
    std::unique_ptr<Expr> operand;
    UnaryExpr(const std::string& o, std::unique_ptr<Expr> e) : op(o), operand(std::move(e)) {}
};

// Call expression
struct CallExpr : Expr {
    std::unique_ptr<Expr> callee;  // Can be identifier or member access
    std::vector<std::unique_ptr<Expr>> arguments;
};

// Member access: obj.field
struct MemberAccessExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string member;
};

// Method call: obj.method(args)
struct MethodCallExpr : Expr {
    std::unique_ptr<Expr> object;
    std::string method;
    std::vector<std::unique_ptr<Expr>> arguments;
};

// Array/map access: arr[index]
struct IndexExpr : Expr {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
};

// Range expression: start..end
struct RangeExpr : Expr {
    std::unique_ptr<Expr> start;
    std::unique_ptr<Expr> end;
};

// Array literal: [1, 2, 3]
struct ArrayLiteral : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
};

// Map literal: { key: value, ... }
struct MapLiteral : Expr {
    std::vector<std::pair<std::unique_ptr<Expr>, std::unique_ptr<Expr>>> pairs;
};

// List literal: list[1, 2, 3]
struct ListLiteral : Expr {
    std::vector<std::unique_ptr<Expr>> elements;
};

// Struct literal: StructName { field: value, ... }
struct StructLiteral : Expr {
    std::string structName;
    std::vector<std::pair<std::string, std::unique_ptr<Expr>>> fields;
};

// Cast expression: (type)expr
struct CastExpr : Expr {
    std::unique_ptr<TypeExpr> targetType;
    std::unique_ptr<Expr> operand;
};

// Error expression (placeholder for invalid expressions)
struct ErrorExpr : Expr {
    std::string message;
    explicit ErrorExpr(const std::string& msg = "") : message(msg) {}
};

// ============================================================================
// Statements
// ============================================================================

struct Stmt : AstNode {
    virtual ~Stmt() = default;
};

// Module declaration: module name
struct ModuleDecl : Stmt {
    std::string name;
    explicit ModuleDecl(const std::string& n) : name(n) {}
};

// Import statement: import path or import { path1, path2 }
struct ImportStmt : Stmt {
    std::vector<std::string> paths;  // import paths like "json", "http/client"
};

// Variable declaration: var name = expr or type name = expr
struct VarDecl : Stmt {
    std::string name;
    std::unique_ptr<TypeExpr> type;  // Optional type annotation
    std::unique_ptr<Expr> initializer;
    bool isVar = false;  // True if declared with 'var' keyword
};

// Map declaration: map name = expr
struct MapDecl : Stmt {
    std::string name;
    std::unique_ptr<Expr> initializer;
};

// List declaration: list name = expr
struct ListDecl : Stmt {
    std::string name;
    std::unique_ptr<Expr> initializer;
};

// Function definition
struct FunctionDef : Stmt {
    std::string name;
    std::unique_ptr<TypeExpr> returnType;
    std::vector<std::pair<std::string, std::unique_ptr<TypeExpr>>> parameters;
    std::vector<std::unique_ptr<Stmt>> body;
    bool isVariadic = false;
};

// Struct field
struct FieldDecl {
    std::string name;
    std::unique_ptr<TypeExpr> type;
    SourceRange range;
};

// Struct method (stored differently from standalone functions)
struct MethodDef {
    std::string name;
    std::unique_ptr<TypeExpr> returnType;
    std::vector<std::pair<std::string, std::unique_ptr<TypeExpr>>> parameters;
    std::vector<std::unique_ptr<Stmt>> body;
    bool isExtend = false;  // True if marked with 'extend'
    SourceRange range;
};

// Struct definition
struct StructDef : Stmt {
    std::string name;
    std::string parentName;  // For inheritance: struct Child : Parent
    std::vector<FieldDecl> fields;
    std::vector<MethodDef> methods;
};

// Impl block
struct ImplBlock : Stmt {
    std::string structName;
    std::vector<MethodDef> methods;
};

// Extern block for C FFI
struct ExternBlock : Stmt {
    std::vector<std::unique_ptr<Stmt>> declarations;
};

struct ExternFunctionDecl : Stmt {
    std::string name;
    std::unique_ptr<TypeExpr> returnType;
    std::vector<std::pair<std::string, std::unique_ptr<TypeExpr>>> parameters;
    bool isVariadic = false;
};

struct ExternStructDecl : Stmt {
    std::string name;
    explicit ExternStructDecl(const std::string& n) : name(n) {}
};

struct ExternVarDecl : Stmt {
    std::string name;
    std::unique_ptr<TypeExpr> type;
};

// If statement
struct IfStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> thenBody;
    // Elif clauses: condition + body
    std::vector<std::pair<std::unique_ptr<Expr>, std::vector<std::unique_ptr<Stmt>>>> elifs;
    std::vector<std::unique_ptr<Stmt>> elseBody;
};

// While statement
struct WhileStmt : Stmt {
    std::unique_ptr<Expr> condition;
    std::vector<std::unique_ptr<Stmt>> body;
};

// For statement
struct ForStmt : Stmt {
    std::string variable;
    std::unique_ptr<TypeExpr> varType;  // Optional type
    bool isVar = false;
    std::unique_ptr<Expr> iterable;  // Range or collection
    std::vector<std::unique_ptr<Stmt>> body;
    
    // For C-style for loops
    std::unique_ptr<Stmt> init;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> increment;
    bool isCStyle = false;
};

// Match statement
struct MatchArm {
    std::unique_ptr<Expr> pattern;  // nullptr for wildcard _
    bool isWildcard = false;
    std::vector<std::unique_ptr<Stmt>> body;
    SourceRange range;
};

struct MatchStmt : Stmt {
    std::unique_ptr<Expr> expression;
    std::vector<MatchArm> arms;
};

// Return statement
struct ReturnStmt : Stmt {
    std::unique_ptr<Expr> value;  // Optional return value
};

// Break statement
struct BreakStmt : Stmt {};

// Continue statement
struct ContinueStmt : Stmt {};

// Expression statement
struct ExprStmt : Stmt {
    std::unique_ptr<Expr> expression;
};

// Assignment statement: name = expr
struct AssignStmt : Stmt {
    std::string name;
    std::string op;  // =, +=, -=, etc.
    std::unique_ptr<Expr> value;
};

// Member assignment: obj.field = expr
struct MemberAssignStmt : Stmt {
    std::unique_ptr<Expr> object;
    std::string field;
    std::string op;
    std::unique_ptr<Expr> value;
};

// Array assignment: arr[index] = expr
struct IndexAssignStmt : Stmt {
    std::unique_ptr<Expr> object;
    std::unique_ptr<Expr> index;
    std::string op;
    std::unique_ptr<Expr> value;
};

// Deref assignment: *ptr = expr
struct DerefAssignStmt : Stmt {
    std::unique_ptr<Expr> pointer;
    std::unique_ptr<Expr> value;
};

// Error statement (placeholder for invalid statements)
struct ErrorStmt : Stmt {
    std::string message;
    explicit ErrorStmt(const std::string& msg = "") : message(msg) {}
};

// ============================================================================
// Program (root node)
// ============================================================================

struct Program : AstNode {
    std::vector<std::unique_ptr<Stmt>> statements;
};

} // namespace lsp
