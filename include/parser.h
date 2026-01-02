#pragma once
#include "lexer.h"
#include <memory>
#include <vector>
#include <stdexcept>
#include <string>

class ErrorReporter;
class SourceManager;

struct ASTNode
{
    SourceLocation location;
    virtual ~ASTNode() = default;
};

struct ExprAST : ASTNode
{
};

struct StringExprAST : ExprAST
{
    std::string value;
    explicit StringExprAST(std::string v) : value(std::move(v)) {}
};

struct NumberExprAST : ExprAST
{
    double value;
    explicit NumberExprAST(double v) : value(v) {}
};

struct BinaryExprAST : ExprAST
{
    char op;
    std::unique_ptr<ExprAST> lhs, rhs;
    BinaryExprAST(char op, std::unique_ptr<ExprAST> l, std::unique_ptr<ExprAST> r)
        : op(op), lhs(std::move(l)), rhs(std::move(r)) {}
};

struct UnaryExprAST : ExprAST
{
    char op;
    std::unique_ptr<ExprAST> operand;
    UnaryExprAST(char op, std::unique_ptr<ExprAST> operand)
        : op(op), operand(std::move(operand)) {}
};

struct StmtAST : ASTNode
{
    virtual ~StmtAST() = default;
};

struct PrintStmtAST : StmtAST
{
    std::unique_ptr<ExprAST> expr;
    explicit PrintStmtAST(std::unique_ptr<ExprAST> e) : expr(std::move(e)) {}
};

struct BoolExprAST : ExprAST {
    bool value;
    explicit BoolExprAST(bool v) : value(v) {}
};

struct IfStmtAST : StmtAST {
    std::unique_ptr<ExprAST> cond;
    std::vector<std::unique_ptr<StmtAST>> thenBody;
    std::vector<std::pair<std::unique_ptr<ExprAST>, std::vector<std::unique_ptr<StmtAST>>>> elifs;
    std::vector<std::unique_ptr<StmtAST>> elseBody;
    IfStmtAST(std::unique_ptr<ExprAST> c,
              std::vector<std::unique_ptr<StmtAST>> t,
              std::vector<std::pair<std::unique_ptr<ExprAST>, std::vector<std::unique_ptr<StmtAST>>>> e,
              std::vector<std::unique_ptr<StmtAST>> el)
        : cond(std::move(c)), thenBody(std::move(t)), elifs(std::move(e)), elseBody(std::move(el)) {}
};

struct ProgramAST : ASTNode
{
    std::vector<std::unique_ptr<StmtAST>> stmts;
};

struct FunctionAST : StmtAST {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params;
    std::vector<std::unique_ptr<StmtAST>> body;
    FunctionAST(const std::string &n,
                const std::string &ret,
                const std::vector<std::pair<std::string, std::string>> &p,
                std::vector<std::unique_ptr<StmtAST>> &&b)
        : name(n), returnType(ret), params(p), body(std::move(b)) {}
};

struct ExternFunctionAST : StmtAST {
    std::string name;
    std::string returnType;
    std::vector<std::pair<std::string, std::string>> params;
    ExternFunctionAST(const std::string &n, const std::string &ret,
                      const std::vector<std::pair<std::string, std::string>> &p)
        : name(n), returnType(ret), params(p) {}
};

// Forward declaration of an external/opaque C struct: extern "C" { struct Name; }
struct ExternStructDeclAST : StmtAST {
    std::string name;
    explicit ExternStructDeclAST(const std::string &n) : name(n) {}
};

struct CallExprAST : ExprAST {
    std::string callee;
    std::vector<std::unique_ptr<ExprAST>> args;
    CallExprAST(const std::string &c, std::vector<std::unique_ptr<ExprAST>> &&a)
        : callee(c), args(std::move(a)) {}
};

struct VariableExprAST : ExprAST {
    std::string name;
    VariableExprAST(const std::string &n) : name(n) {}
};

struct VarDeclStmt : StmtAST {
    std::string type;
    std::string name;
    std::unique_ptr<ExprAST> init;
    VarDeclStmt(const std::string &t, const std::string &n, std::unique_ptr<ExprAST> &&i)
        : type(t), name(n), init(std::move(i)) {}
};

struct ForStmt : StmtAST {
    std::string var;
    std::unique_ptr<ExprAST> rangeExpr;
    std::vector<std::unique_ptr<StmtAST>> body;
    ForStmt(const std::string &v, std::unique_ptr<ExprAST> &&r, std::vector<std::unique_ptr<StmtAST>> &&b)
        : var(v), rangeExpr(std::move(r)), body(std::move(b)) {}
};

struct WhileStmt : StmtAST {
    std::unique_ptr<ExprAST> condition;
    std::vector<std::unique_ptr<StmtAST>> body;
    WhileStmt(std::unique_ptr<ExprAST> &&cond, std::vector<std::unique_ptr<StmtAST>> &&b)
        : condition(std::move(cond)), body(std::move(b)) {}
};

struct ExprStmtAST : StmtAST {
    std::unique_ptr<ExprAST> expr;
    ExprStmtAST(std::unique_ptr<ExprAST> e) : expr(std::move(e)) {}
};

struct ReturnStmt : StmtAST {
    std::unique_ptr<ExprAST> returnValue;
    ReturnStmt(std::unique_ptr<ExprAST> &&val)
        : returnValue(std::move(val)) {}
};

// Loop control statements
struct BreakStmt : StmtAST {
    BreakStmt() = default;
};

struct ContinueStmt : StmtAST {
    ContinueStmt() = default;
};

struct AssignStmtAST : StmtAST {
    std::string varName;
    std::unique_ptr<ExprAST> value;
    AssignStmtAST(const std::string &name, std::unique_ptr<ExprAST> &&val)
        : varName(name), value(std::move(val)) {}
};

// Import statement to inline contents of another .k file
struct IncludeStmt : StmtAST {
    std::vector<std::unique_ptr<StmtAST>> stmts;
    std::vector<std::string> importedFiles; // Track which files were imported for error reporting
    IncludeStmt() = default;
};

// Module declaration: module <name>
struct ModuleDeclStmt : StmtAST {
    std::string moduleName;
    explicit ModuleDeclStmt(const std::string& name) : moduleName(name) {}
};

// Struct definition: struct Name { data { fields... }, methods... }
struct StructDefStmt : StmtAST {
    std::string name;
    std::string parentName; // for inheritance: struct Child : Parent
    std::vector<std::pair<std::string, std::string>> fields; // field_name, field_type
    std::vector<std::unique_ptr<FunctionAST>> methods; // methods defined in struct
    StructDefStmt(const std::string &n, std::vector<std::pair<std::string, std::string>> &&f)
        : name(n), fields(std::move(f)) {}
};

// Struct literal: StructName { field1: value1, field2: value2, ... }
struct StructLiteralExpr : ExprAST {
    std::string structName;
    std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> fieldValues; // field_name, field_value
    StructLiteralExpr(const std::string &name, std::vector<std::pair<std::string, std::unique_ptr<ExprAST>>> &&values)
        : structName(name), fieldValues(std::move(values)) {}
};

// Member access: object.field
struct MemberAccessExpr : ExprAST {
    std::unique_ptr<ExprAST> object;
    std::string fieldName;
    MemberAccessExpr(std::unique_ptr<ExprAST> obj, const std::string &field)
        : object(std::move(obj)), fieldName(field) {}
};

// Method call on expression: object.methodName(args)
struct MethodCallExpr : ExprAST {
    std::unique_ptr<ExprAST> object;
    std::string methodName;
    std::vector<std::unique_ptr<ExprAST>> args;
    MethodCallExpr(std::unique_ptr<ExprAST> obj, const std::string &method, std::vector<std::unique_ptr<ExprAST>> &&a)
        : object(std::move(obj)), methodName(method), args(std::move(a)) {}
};

// Member assignment: object.field = value
struct MemberAssignStmt : StmtAST {
    std::unique_ptr<ExprAST> object;
    std::string fieldName;
    std::unique_ptr<ExprAST> value;
    MemberAssignStmt(std::unique_ptr<ExprAST> obj, const std::string &field, std::unique_ptr<ExprAST> val)
        : object(std::move(obj)), fieldName(field), value(std::move(val)) {}
};

// Implementation block: impl StructName { methods... }
struct ImplStmt : StmtAST {
    std::string structName;
    std::vector<std::unique_ptr<FunctionAST>> methods;
    ImplStmt(const std::string &name) : structName(name) {}
};

// Static method call: StructName->methodName(args)
struct StaticCallExpr : ExprAST {
    std::string structName;
    std::string methodName;
    std::vector<std::unique_ptr<ExprAST>> args;
    StaticCallExpr(const std::string &structName, const std::string &methodName)
        : structName(structName), methodName(methodName) {}
};

// Address-of expression: &variable
struct AddressOfExpr : ExprAST {
    std::unique_ptr<ExprAST> operand;
    AddressOfExpr(std::unique_ptr<ExprAST> op) : operand(std::move(op)) {}
};

// Dereference expression: *pointer
struct DereferenceExpr : ExprAST {
    std::unique_ptr<ExprAST> operand;
    DereferenceExpr(std::unique_ptr<ExprAST> op) : operand(std::move(op)) {}
};

// Range expression: start..end
struct RangeExpr : ExprAST {
    std::unique_ptr<ExprAST> start;
    std::unique_ptr<ExprAST> end;
    RangeExpr(std::unique_ptr<ExprAST> s, std::unique_ptr<ExprAST> e) 
        : start(std::move(s)), end(std::move(e)) {}
};

// Dereference assignment: *ptr = value
struct DerefAssignStmt : StmtAST {
    std::unique_ptr<DereferenceExpr> deref;
    std::unique_ptr<ExprAST> value;
    DerefAssignStmt(std::unique_ptr<DereferenceExpr> derefExpr, std::unique_ptr<ExprAST> val)
        : deref(std::move(derefExpr)), value(std::move(val)) {}
};

// Match arm: pattern => body
struct MatchArm {
    std::unique_ptr<ExprAST> pattern; // nullptr for wildcard (_)
    bool isWildcard = false;
    std::vector<std::unique_ptr<StmtAST>> body;
};

// Match statement: match expr { pattern => { body }, ... }
struct MatchStmt : StmtAST {
    std::unique_ptr<ExprAST> expr;
    std::vector<MatchArm> arms;
    MatchStmt(std::unique_ptr<ExprAST> matchExpr) : expr(std::move(matchExpr)) {}
};

// Null literal expression
struct NullExprAST : ExprAST {
    NullExprAST() = default;
};

// Array literal expression: {1, 2, 3, 4}
struct ArrayLiteralExpr : ExprAST {
    std::vector<std::unique_ptr<ExprAST>> elements;
    explicit ArrayLiteralExpr(std::vector<std::unique_ptr<ExprAST>> elems) 
        : elements(std::move(elems)) {}
};

// Map literal expression: ["key1": "value1", "key2": "value2"]
struct MapLiteralExpr : ExprAST {
    std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<ExprAST>>> pairs; // key, value
    explicit MapLiteralExpr(std::vector<std::pair<std::unique_ptr<ExprAST>, std::unique_ptr<ExprAST>>> p)
        : pairs(std::move(p)) {}
};

// Array access expression: arr[index]
struct ArrayAccessExpr : ExprAST {
    std::unique_ptr<ExprAST> array;
    std::unique_ptr<ExprAST> index;
    ArrayAccessExpr(std::unique_ptr<ExprAST> arr, std::unique_ptr<ExprAST> idx)
        : array(std::move(arr)), index(std::move(idx)) {}
};

// C-style cast expression: (type)expr
struct CastExpr : ExprAST {
    std::string targetTypeName; // e.g., "int", "float", "double", "bool"
    std::unique_ptr<ExprAST> operand;
    CastExpr(const std::string &t, std::unique_ptr<ExprAST> &&op) : targetTypeName(t), operand(std::move(op)) {}
};

// Array assignment statement: arr[index] = value
struct ArrayAssignStmt : StmtAST {
    std::unique_ptr<ExprAST> array;
    std::unique_ptr<ExprAST> index;
    std::unique_ptr<ExprAST> value;
    ArrayAssignStmt(std::unique_ptr<ExprAST> arr, std::unique_ptr<ExprAST> idx, std::unique_ptr<ExprAST> val)
        : array(std::move(arr)), index(std::move(idx)), value(std::move(val)) {}
};

class ParseError : public std::runtime_error
{
public:
    SourceLocation location;
    ParseError(const std::string &msg, const SourceLocation &loc)
        : std::runtime_error(formatError(msg, loc)), location(loc) {}
        
private:
    static std::string formatError(const std::string &msg, const SourceLocation &loc) {
        return loc.filename + ":" + std::to_string(loc.line) + ":" + std::to_string(loc.column) + ": error: " + msg;
    }
};

struct CompilationContext;

class Parser
{
public:
    explicit Parser(Lexer &lex, bool verbose = false, CompilationContext* ctx = nullptr);
    std::unique_ptr<ProgramAST> parseProgram();

private:
    Lexer &lex_;
    Token cur_;
    bool verbose_ = false;
    CompilationContext* ctx_ = nullptr;
    // One-token lookahead buffer
    bool hasPeeked_ = false;
    Token peeked_{};
    
    ErrorReporter* errorReporter() const;
    SourceManager* sourceManager() const;
    
    void next();
    Token peekToken();
    void expect(TokenKind kind, const std::string &msg);
    std::unique_ptr<StmtAST> parseStatement();
    std::unique_ptr<ExprAST> parseExpression(int precedence = 0);
    std::unique_ptr<ExprAST> parsePrimary();
    int getTokPrecedence();
    std::unique_ptr<StructDefStmt> parseStructDef();
    std::unique_ptr<StructLiteralExpr> parseStructLiteral(const std::string &structName);
    std::unique_ptr<ImplStmt> parseImpl();
    std::unique_ptr<StaticCallExpr> parseStaticCall(const std::string &structName);

    // Helper function to get type string from token
    std::string getTypeString(const Token& token) {
        switch (token.kind) {
            case tok_bool: return "bool";
            case tok_int: return "int";
            case tok_str: return "str";
            case tok_float: return "float";
            case tok_double: return "double";
            case tok_identifier: return token.text;
            default: return token.text;
        }
    }

    // Parse type including array syntax: int[], str[], etc.
    std::string parseTypeString() {
        std::string baseType = getTypeString(cur_);
        next();
        
        // Check for array syntax: type[]
        if (cur_.kind == tok_square_bracket_open) {
            next();
            if (cur_.kind != tok_square_bracket_close) {
                throw ParseError("expected ']' after '[' in array type", cur_.location);
            }
            next();
            return baseType + "[]";
        }
        
        // Support pointer suffixes: type*, type**, etc.
        while (cur_.kind == tok_mul) {
            baseType += "*";
            next();
        }
        
        return baseType;
    }

    // Helper function to check if token is a type token
    bool isTypeToken(const Token& token) {
        return token.kind == tok_bool ||
               token.kind == tok_int ||
               token.kind == tok_str ||
               token.kind == tok_float ||
               token.kind == tok_double;
    }
};
